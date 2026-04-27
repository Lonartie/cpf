#include "runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace cpf {
   namespace detail {
      namespace {
         using source_location = source_position;

         struct cached_input_layout {
            bool initialized = false;
            const char* data = nullptr;
            std::size_t size = 0;
            std::vector<std::size_t> line_offsets;
         };

         auto line_offsets_of(std::string_view input) -> const std::vector<std::size_t>& {
            thread_local auto cache = cached_input_layout{};
            if (cache.initialized && cache.data == input.data() && cache.size == input.size()) {
               return cache.line_offsets;
            }

            cache.initialized = true;
            cache.data = input.data();
            cache.size = input.size();
            cache.line_offsets.clear();
            cache.line_offsets.push_back(0);
            for (std::size_t index = 0; index < input.size(); ++index) {
               if (input[index] == '\n') {
                  cache.line_offsets.push_back(index + 1);
               }
            }
            return cache.line_offsets;
         }

         auto locate(std::string_view input, std::size_t offset) -> source_location {
            source_location location;
            location.offset = std::min(offset, input.size());
            const auto& line_offsets = line_offsets_of(input);
            const auto line_index = static_cast<std::size_t>(
                  std::upper_bound(line_offsets.begin(), line_offsets.end(), location.offset) - line_offsets.begin() - 1);
            location.line = line_index + 1;
            location.column = 1 + location.offset - line_offsets[line_index];
            return location;
         }

         auto make_source_range(std::string_view input, std::size_t begin_offset, std::size_t end_offset)
               -> source_range {
            return source_range{locate(input, begin_offset), locate(input, end_offset)};
         }

         auto escape_string(std::string_view value) -> std::string {
            std::string escaped;
            escaped.reserve(value.size() + 4);
            for (char ch: value) {
               switch (ch) {
                  case '\\':
                     escaped += "\\\\";
                     break;
                  case '\n':
                     escaped += "\\n";
                     break;
                  case '\r':
                     escaped += "\\r";
                     break;
                  case '\t':
                     escaped += "\\t";
                     break;
                  case '"':
                     escaped += "\\\"";
                     break;
                  default:
                     escaped += ch;
                     break;
               }
            }
            return escaped;
         }

         auto found_token(std::string_view input, std::size_t offset) -> parse_error_found {
            parse_error_found found;
            if (offset >= input.size()) {
               return found;
            }
            auto end = offset;
            if (std::isspace(static_cast<unsigned char>(input[end])) != 0) {
               found.kind = parse_error_found_kind::token;
               found.text = std::string{std::string_view{input.data() + end, 1}};
               return found;
            }
            while (end < input.size() && std::isspace(static_cast<unsigned char>(input[end])) == 0) {
               ++end;
            }
            found.kind = parse_error_found_kind::token;
            found.text = std::string{input.substr(offset, std::min<std::size_t>(end - offset, 16))};
            return found;
         }

         auto describe_found(const parse_error_found& found) -> std::string {
            switch (found.kind) {
               case parse_error_found_kind::token:
                  return std::string{"\""} + escape_string(found.text) + "\"";
               case parse_error_found_kind::end_of_input:
                  return "<end of input>";
               case parse_error_found_kind::ambiguous_parse:
                  return "<ambiguous parse>";
               case parse_error_found_kind::filtered_parse:
                  return "<filtered parse>";
            }
            return "<unknown>";
         }

         auto join_expected(const std::vector<std::string>& expected) -> std::string {
            if (expected.empty()) {
               return "<unknown>";
            }
            if (expected.size() == 1) {
               return expected.front();
            }

            std::ostringstream stream;
            for (std::size_t i = 0; i < expected.size(); ++i) {
               if (i != 0) {
                  stream << (i + 1 == expected.size() ? " or " : ", ");
               }
               stream << expected[i];
            }
            return stream.str();
         }

         void skip_default_space(std::string_view input, std::size_t& position) {
            while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position])) != 0) {
               ++position;
            }
         }

         auto try_literal_at(std::string_view input, std::size_t& position, std::string_view literal,
                             std::string* capture = nullptr) -> bool {
            if (input.substr(position, literal.size()) != literal) {
               return false;
            }
            position += literal.size();
            if (capture != nullptr) {
               *capture = std::string{literal};
            }
            return true;
         }

         auto capture_simple_match(std::string_view input, std::size_t begin, std::size_t end, std::string* capture)
               -> void {
            if (capture != nullptr) {
               *capture = std::string{input.substr(begin, end - begin)};
            }
         }

         auto try_identifier_regex_at(std::string_view input, std::size_t& position, std::string* capture = nullptr)
               -> bool {
            if (position >= input.size()) {
               return false;
            }
            const auto begin = position;
            const auto first = static_cast<unsigned char>(input[position]);
            if (std::isalpha(first) == 0 && first != '_') {
               return false;
            }
            ++position;
            while (position < input.size()) {
               const auto current = static_cast<unsigned char>(input[position]);
               if (std::isalnum(current) == 0 && current != '_') {
                  break;
               }
               ++position;
            }
            capture_simple_match(input, begin, position, capture);
            return true;
         }

         auto try_decimal_regex_at(std::string_view input, std::size_t& position, std::string* capture = nullptr)
               -> bool {
            if (position >= input.size() || std::isdigit(static_cast<unsigned char>(input[position])) == 0) {
               return false;
            }
            const auto begin = position;
            do {
               ++position;
            } while (position < input.size() && std::isdigit(static_cast<unsigned char>(input[position])) != 0);
            capture_simple_match(input, begin, position, capture);
            return true;
         }

         auto try_whitespace_regex_at(std::string_view input, std::size_t& position, std::string* capture = nullptr)
               -> bool {
            if (position >= input.size()) {
               return false;
            }
            const auto begin = position;
            while (position < input.size()) {
               const auto current = input[position];
               if (current != ' ' && current != '\t' && current != '\r' && current != '\n') {
                  break;
               }
               ++position;
            }
            if (position == begin) {
               return false;
            }
            capture_simple_match(input, begin, position, capture);
            return true;
         }

         auto try_line_comment_regex_at(std::string_view input, std::size_t& position, std::string* capture = nullptr)
               -> bool {
            if (position + 1 >= input.size() || input[position] != '/' || input[position + 1] != '/') {
               return false;
            }
            const auto begin = position;
            position += 2;
            while (position < input.size() && input[position] != '\n') {
               ++position;
            }
            capture_simple_match(input, begin, position, capture);
            return true;
         }

         auto try_simple_regex_at(std::string_view input, std::size_t& position, std::string_view regex_text,
                                  std::string* capture = nullptr) -> std::optional<bool> {
            if (regex_text == "[A-Za-z_][A-Za-z0-9_]*") {
               return try_identifier_regex_at(input, position, capture);
            }
            if (regex_text == "[0-9]+") {
               return try_decimal_regex_at(input, position, capture);
            }
            if (regex_text == "[ \t\r\n]+") {
               return try_whitespace_regex_at(input, position, capture);
            }
            if (regex_text == "//[^\n]*") {
               return try_line_comment_regex_at(input, position, capture);
            }
            return std::nullopt;
         }

         auto try_regex_at(std::string_view input, std::size_t& position, const std::regex& regex,
                           std::string* capture = nullptr) -> bool {
            auto begin = input.data() + position;
            auto end = input.data() + input.size();
            std::match_results<const char*> match;
            if (!std::regex_search(begin, end, match, regex, std::regex_constants::match_continuous) ||
                match.length() == 0) {
               return false;
            }
            position += static_cast<std::size_t>(match.length());
            if (capture != nullptr) {
               *capture = match.str();
            }
            return true;
         }

         auto try_lexer_symbol_at(std::string_view input, std::size_t& position, const lexer_symbol_spec& symbol,
                                  std::string* capture = nullptr) -> bool {
            if (symbol.kind == lexer_symbol_kind::literal) {
               return try_literal_at(input, position, symbol.text, capture);
            }
            if (auto simple = try_simple_regex_at(input, position, symbol.text, capture); simple.has_value()) {
               return *simple;
            }
            if (symbol.compiled_regex != nullptr) {
               return try_regex_at(input, position, *symbol.compiled_regex, capture);
            }
            const auto regex = std::regex{std::string{symbol.text}};
            return try_regex_at(input, position, regex, capture);
         }

         struct raw_lexer_match {
            std::size_t symbol = 0;
            std::size_t end = 0;
            std::size_t precedence = 0;
            lexer_symbol_kind kind = lexer_symbol_kind::literal;
            std::string text;
            bool skip = false;
         };

         struct lexer_dispatch_key {
            const lexer_symbol_spec* token_symbols = nullptr;
            std::size_t token_symbol_count = 0;
            const lexer_symbol_spec* skip_symbols = nullptr;
            std::size_t skip_symbol_count = 0;

            [[nodiscard]] bool operator==(const lexer_dispatch_key&) const = default;
         };

         struct lexer_dispatch {
            std::array<std::vector<std::size_t>, 256> token_candidates;
            std::array<std::vector<std::size_t>, 256> skip_candidates;
            std::vector<std::size_t> token_fallback_candidates;
            std::vector<std::size_t> skip_fallback_candidates;
         };

         [[nodiscard]] auto lexer_dispatch_of(const grammar_spec& grammar) -> const lexer_dispatch&;

         auto better_raw_lexer_match(const raw_lexer_match& candidate, const std::optional<raw_lexer_match>& current,
                                     std::size_t begin) -> bool {
            if (!current.has_value()) {
               return true;
            }
            const auto candidate_length = candidate.end - begin;
            const auto current_length = current->end - begin;
            if (candidate_length != current_length) {
               return candidate_length > current_length;
            }

            if (candidate.kind != current->kind) {
               return candidate.kind == lexer_symbol_kind::literal;
            }

            return candidate.precedence < current->precedence;
         }

         auto best_skip_match_at(std::string_view input, std::size_t position, const grammar_spec& grammar)
               -> std::optional<raw_lexer_match> {
            auto best = std::optional<raw_lexer_match>{};
            const auto& dispatch = lexer_dispatch_of(grammar);
            const auto current = static_cast<unsigned char>(input[position]);

            const auto consider = [&](std::size_t index) {
               auto next = position;
               std::string capture;
               if (!try_lexer_symbol_at(input, next, grammar.skip_symbols[index], &capture) || next == position) {
                  return;
               }
               auto candidate = raw_lexer_match{index, next, grammar.skip_symbols[index].precedence,
                                                grammar.skip_symbols[index].kind,
                                                std::move(capture), true};
               if (better_raw_lexer_match(candidate, best, position)) {
                  best = std::move(candidate);
               }
            };

            for (const auto index: dispatch.skip_candidates[current]) {
               consider(index);
            }
            for (const auto index: dispatch.skip_fallback_candidates) {
               consider(index);
            }
            return best;
         }

         void skip_trivia(std::string_view input, std::size_t& position, const grammar_spec& grammar) {
            while (position < input.size()) {
               auto original = position;
               if (grammar.use_default_whitespace) {
                  skip_default_space(input, position);
               }
               auto matched_skip = best_skip_match_at(input, position, grammar);
               if (matched_skip.has_value()) {
                  position = matched_skip->end;
               }
               if (!matched_skip.has_value() && position == original) {
                  break;
               }
            }
         }

         [[maybe_unused]] auto skip_trivia_position(std::string_view input, std::size_t position,
                                                    const grammar_spec& grammar)
               -> std::size_t {
            skip_trivia(input, position, grammar);
            return position;
         }

         auto make_node_damage(std::string_view input, std::size_t begin_offset, std::size_t end_offset,
                               node_damage_reason reason, std::string detail = {}, std::string message = {})
               -> node_damage {
            return node_damage{make_source_range(input, begin_offset, end_offset), reason, std::move(detail),
                               std::move(message)};
         }

         auto make_inserted_damage(std::string_view input, std::size_t offset, std::string expected) -> node_damage {
            auto detail = std::move(expected);
            return make_node_damage(input, offset, offset, node_damage_reason::inserted_virtual_token, detail,
                                    "recovery inserted one of the available tokens at this position: " + detail);
         }

         auto make_ignored_damage(std::string_view input, std::size_t begin_offset, std::size_t end_offset,
                                  std::string expected) -> node_damage {
            auto detail = quoted(input.substr(begin_offset, end_offset - begin_offset));
            auto message = expected == "<end of input>"
                                 ? "recovery ignored trailing input because parsing had already completed"
                                 : "recovery ignored input because it could not match " + expected;
            return make_node_damage(input, begin_offset, end_offset, node_damage_reason::ignored_invalid_input,
                                    std::move(detail), std::move(message));
         }


         struct chart_item_key {
            std::size_t production = 0;
            std::size_t dot = 0;
            std::size_t start = 0;

            [[nodiscard]] bool operator==(const chart_item_key&) const = default;
         };

         struct span_key {
            std::size_t symbol = 0;
            std::size_t start = 0;
            std::size_t end = 0;

            [[nodiscard]] bool operator==(const span_key&) const = default;
         };

         struct rule_start_key {
            std::size_t rule = 0;
            std::size_t start = 0;

            [[nodiscard]] bool operator==(const rule_start_key&) const = default;
         };

         void hash_combine(std::size_t& seed, std::size_t value) {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
         }

         struct chart_item_key_hash {
            [[nodiscard]] auto operator()(const chart_item_key& key) const noexcept -> std::size_t {
               auto seed = std::size_t{0};
               hash_combine(seed, key.production);
               hash_combine(seed, key.dot);
               hash_combine(seed, key.start);
               return seed;
            }
         };

         struct span_key_hash {
            [[nodiscard]] auto operator()(const span_key& key) const noexcept -> std::size_t {
               auto seed = std::size_t{0};
               hash_combine(seed, key.symbol);
               hash_combine(seed, key.start);
               hash_combine(seed, key.end);
               return seed;
            }
         };

         struct rule_start_key_hash {
            [[nodiscard]] auto operator()(const rule_start_key& key) const noexcept -> std::size_t {
               auto seed = std::size_t{0};
               hash_combine(seed, key.rule);
               hash_combine(seed, key.start);
               return seed;
            }
         };

         struct predicate_query_key {
            const grammar_spec* grammar = nullptr;
            const std::vector<lexed_token>* tokens = nullptr;
            std::size_t rule = 0;
            std::size_t position = 0;

            [[nodiscard]] bool operator==(const predicate_query_key&) const = default;
         };

         struct predicate_query_key_hash {
            [[nodiscard]] auto operator()(const predicate_query_key& key) const noexcept -> std::size_t {
               auto seed = std::size_t{0};
               hash_combine(seed, reinterpret_cast<std::size_t>(key.grammar));
               hash_combine(seed, reinterpret_cast<std::size_t>(key.tokens));
               hash_combine(seed, key.rule);
               hash_combine(seed, key.position);
               return seed;
            }
         };

         struct production_step_key {
            std::size_t production = 0;
            std::size_t symbol_index = 0;
            std::size_t position = 0;
            std::size_t end = 0;

            [[nodiscard]] bool operator==(const production_step_key&) const = default;
         };

         struct production_step_key_hash {
            [[nodiscard]] auto operator()(const production_step_key& key) const noexcept -> std::size_t {
               auto seed = std::size_t{0};
               hash_combine(seed, key.production);
               hash_combine(seed, key.symbol_index);
               hash_combine(seed, key.position);
               hash_combine(seed, key.end);
               return seed;
            }
         };

         struct recognize_item_key {
            std::size_t state = 0;
            std::size_t start = 0;

            [[nodiscard]] bool operator==(const recognize_item_key&) const = default;
         };

         struct recognize_item_key_hash {
            [[nodiscard]] auto operator()(const recognize_item_key& key) const noexcept -> std::size_t {
               auto seed = std::size_t{0};
               hash_combine(seed, key.state);
               hash_combine(seed, key.start);
               return seed;
            }
         };

         struct completion_key {
            std::size_t rule = 0;
            std::size_t end = 0;

            [[nodiscard]] bool operator==(const completion_key&) const = default;
         };

         struct completion_key_hash {
            [[nodiscard]] auto operator()(const completion_key& key) const noexcept -> std::size_t {
               auto seed = std::size_t{0};
               hash_combine(seed, key.rule);
               hash_combine(seed, key.end);
               return seed;
            }
         };

         struct packed_pair_codec {
            std::uint32_t shift = 0;
            std::uint64_t second_mask = 0;
            bool packed = false;

            [[nodiscard]] static auto bit_count(std::size_t value) -> std::uint32_t {
               return static_cast<std::uint32_t>(std::bit_width(static_cast<std::uint64_t>(value)));
            }

            [[nodiscard]] static auto make(std::size_t first_max, std::size_t second_max) -> packed_pair_codec {
               auto codec = packed_pair_codec{};
               const auto second_bits = bit_count(second_max);
               const auto first_bits = bit_count(first_max);
               if (first_bits + second_bits >= 64) {
                  return codec;
               }

               codec.shift = second_bits;
               codec.second_mask = second_bits == 0 ? 0 : ((std::uint64_t{1} << second_bits) - 1);
               codec.packed = true;
               return codec;
            }

            [[nodiscard]] auto encode(std::size_t first, std::size_t second) const -> std::uint64_t {
               if (!packed) {
                  return std::numeric_limits<std::uint64_t>::max();
               }

               const auto left = static_cast<std::uint64_t>(first);
               const auto right = static_cast<std::uint64_t>(second);
               return (left << shift) | (right & second_mask);
            }
         };

         struct packed_key_set {
            std::vector<std::uint64_t> slots;
            std::vector<std::uint32_t> generations;
            std::uint32_t generation = 1;
            std::size_t size = 0;

            [[nodiscard]] static auto hash_of(std::uint64_t key) -> std::size_t {
               auto value = key;
               value ^= value >> 33U;
               value *= 0xff51afd7ed558ccdULL;
               value ^= value >> 33U;
               value *= 0xc4ceb9fe1a85ec53ULL;
               value ^= value >> 33U;
               return static_cast<std::size_t>(value);
            }

            void ensure_capacity_for_insert() {
               if (slots.empty()) {
                  slots.assign(32, 0);
                  generations.assign(32, 0);
                  return;
               }
               if ((size + 1) * 10 >= slots.size() * 7) {
                  rehash(slots.size() * 2);
               }
            }

            [[nodiscard]] auto occupied(std::size_t index) const -> bool {
               return generations[index] == generation;
            }

            [[nodiscard]] auto insert(std::uint64_t key) -> bool {
               ensure_capacity_for_insert();

               auto index = hash_of(key) & (slots.size() - 1);
               while (true) {
                  auto& slot = slots[index];
                  auto& slot_generation = generations[index];
                  if (slot_generation != generation) {
                     slot_generation = generation;
                     slot = key;
                     ++size;
                     return true;
                  }
                  if (slot == key) {
                     return false;
                  }
                  index = (index + 1) & (slots.size() - 1);
               }
            }

            [[nodiscard]] auto contains(std::uint64_t key) const -> bool {
               if (slots.empty()) {
                  return false;
               }

               auto index = hash_of(key) & (slots.size() - 1);
               while (true) {
                  if (!occupied(index)) {
                     return false;
                  }
                  if (slots[index] == key) {
                     return true;
                  }
                  index = (index + 1) & (slots.size() - 1);
               }
            }

            void rehash(std::size_t new_capacity) {
               auto old_slots = std::move(slots);
               auto old_generations = std::move(generations);
               const auto old_generation = generation;
               slots.assign(new_capacity, 0);
               generations.assign(new_capacity, 0);
               generation = 1;
               size = 0;
               for (std::size_t index = 0; index < old_slots.size(); ++index) {
                  if (old_generations[index] == old_generation) {
                     (void) insert(old_slots[index]);
                  }
               }
            }

            void clear() {
               size = 0;
               if (slots.empty()) {
                  generation = 1;
                  return;
               }
               if (generation == std::numeric_limits<std::uint32_t>::max()) {
                  std::fill(generations.begin(), generations.end(), 0);
                  generation = 1;
                  return;
               }
               ++generation;
            }
         };

         struct earley_machine_key {
            const production_spec* productions = nullptr;
            const std::size_t* rule_production_indices = nullptr;
            const std::size_t* rule_production_offsets = nullptr;
            const std::size_t* rule_production_counts = nullptr;
            std::size_t production_count = 0;
            std::size_t rule_count = 0;

            [[nodiscard]] bool operator==(const earley_machine_key&) const = default;
         };

         struct recognize_state {
            std::size_t production = 0;
            std::size_t dot = 0;
            std::size_t lhs = 0;
            std::size_t advance_state = 0;
            parser_symbol next{};
            bool complete = false;
         };

         struct earley_machine {
            std::vector<std::size_t> production_state_offsets;
            std::vector<recognize_state> states;
         };

         struct recognize_column {
            static constexpr auto invalid_index = std::numeric_limits<std::size_t>::max();

            struct item_record {
               std::size_t state = 0;
               std::size_t start = 0;
               std::size_t next_waiting = invalid_index;
            };

            struct completion_record {
               std::size_t rule = 0;
               std::size_t end = 0;
               std::size_t next = invalid_index;
            };

            std::vector<item_record> items;
            packed_key_set item_indices;
            std::unordered_set<recognize_item_key, recognize_item_key_hash> fallback_item_indices;
            std::vector<completion_record> completions;
            packed_key_set completion_indices;
            std::unordered_set<completion_key, completion_key_hash> fallback_completion_indices;
            std::size_t* waiting_heads = nullptr;
            std::uint32_t* waiting_head_generations = nullptr;
            std::size_t* completion_heads = nullptr;
            std::uint32_t* completion_head_generations = nullptr;
            std::uint32_t current_generation = 0;
            std::size_t rule_count = 0;

            void initialize(std::size_t* waiting_storage, std::uint32_t* waiting_generation_storage,
                            std::size_t* completion_storage, std::uint32_t* completion_generation_storage,
                            std::uint32_t generation, std::size_t count) {
               waiting_heads = waiting_storage;
               waiting_head_generations = waiting_generation_storage;
               completion_heads = completion_storage;
               completion_head_generations = completion_generation_storage;
               current_generation = generation;
               rule_count = count;
            }

            void reset(std::size_t* waiting_storage, std::uint32_t* waiting_generation_storage,
                       std::size_t* completion_storage, std::uint32_t* completion_generation_storage,
                       std::uint32_t generation, std::size_t count) {
               waiting_heads = waiting_storage;
               waiting_head_generations = waiting_generation_storage;
               completion_heads = completion_storage;
               completion_head_generations = completion_generation_storage;
               current_generation = generation;
               rule_count = count;
               items.clear();
               completions.clear();
               item_indices.clear();
               completion_indices.clear();
               fallback_item_indices.clear();
               fallback_completion_indices.clear();
            }

            [[nodiscard]] auto waiting_head_for(std::size_t rule) const -> std::size_t {
               if (waiting_head_generations[rule] != current_generation) {
                  return invalid_index;
               }
               return waiting_heads[rule];
            }

            void set_waiting_head(std::size_t rule, std::size_t value) {
               waiting_head_generations[rule] = current_generation;
               waiting_heads[rule] = value;
            }

            [[nodiscard]] auto completion_head_for(std::size_t rule) const -> std::size_t {
               if (completion_head_generations[rule] != current_generation) {
                  return invalid_index;
               }
               return completion_heads[rule];
            }

            void set_completion_head(std::size_t rule, std::size_t value) {
               completion_head_generations[rule] = current_generation;
               completion_heads[rule] = value;
            }

            [[nodiscard]] auto add_item(const earley_machine& machine, const packed_pair_codec& codec,
                                        std::size_t state, std::size_t start) -> bool {
               if (codec.packed) {
                  if (!item_indices.insert(codec.encode(state, start))) {
                     return false;
                  }
               } else if (!fallback_item_indices.emplace(recognize_item_key{state, start}).second) {
                  return false;
               }

               auto record = item_record{state, start, invalid_index};
               const auto& state_spec = machine.states[state];
               if (!state_spec.complete && state_spec.next.kind == parser_symbol_kind::nonterminal) {
                  record.next_waiting = waiting_head_for(state_spec.next.value);
                  set_waiting_head(state_spec.next.value, items.size());
               }
               items.push_back(record);
               return true;
            }

            [[nodiscard]] auto add_completion(const packed_pair_codec& codec, std::size_t rule,
                                              std::size_t end) -> bool {
               if (codec.packed) {
                  if (!completion_indices.insert(codec.encode(rule, end))) {
                     return false;
                  }
               } else if (!fallback_completion_indices.emplace(completion_key{rule, end}).second) {
                  return false;
               }

               auto record = completion_record{rule, end, completion_head_for(rule)};
               set_completion_head(rule, completions.size());
               completions.push_back(record);
               return true;
            }

            [[nodiscard]] auto has_completion(const packed_pair_codec& codec, std::size_t rule,
                                              std::size_t end) const -> bool {
               if (codec.packed) {
                  return completion_indices.contains(codec.encode(rule, end));
               }
               return fallback_completion_indices.contains(completion_key{rule, end});
            }
         };

         struct terminal_match {
            std::size_t end = 0;
            matched_string text;
         };

         struct predicate_context {
            std::unordered_map<predicate_query_key, bool, predicate_query_key_hash> matches;
            std::unordered_set<predicate_query_key, predicate_query_key_hash> in_progress;
         };

         struct recognize_fast_buffer {
            std::vector<recognize_column> chart;
            std::vector<std::uint32_t> column_generations;
            std::vector<std::size_t> waiting_heads_storage;
            std::vector<std::uint32_t> waiting_head_generations;
            std::vector<std::size_t> completion_heads_storage;
            std::vector<std::uint32_t> completion_head_generations;
            std::uint32_t generation = 0;
         };

         void add_dispatch_byte(std::array<std::vector<std::size_t>, 256>& buckets, unsigned char value,
                                std::size_t index) {
            buckets[value].push_back(index);
         }

         void add_dispatch_range(std::array<std::vector<std::size_t>, 256>& buckets, unsigned char first,
                                 unsigned char last, std::size_t index) {
            for (auto value = first; value <= last; ++value) {
               buckets[value].push_back(index);
            }
         }

         void register_dispatch_symbol(std::array<std::vector<std::size_t>, 256>& buckets,
                                       std::vector<std::size_t>& fallback, const lexer_symbol_spec& symbol,
                                       std::size_t index) {
            if (symbol.kind == lexer_symbol_kind::literal) {
               if (symbol.text.empty()) {
                  fallback.push_back(index);
                  return;
               }
               add_dispatch_byte(buckets, static_cast<unsigned char>(symbol.text.front()), index);
               return;
            }

            if (symbol.text == "[A-Za-z_][A-Za-z0-9_]*") {
               add_dispatch_range(buckets, static_cast<unsigned char>('A'), static_cast<unsigned char>('Z'), index);
               add_dispatch_range(buckets, static_cast<unsigned char>('a'), static_cast<unsigned char>('z'), index);
               add_dispatch_byte(buckets, static_cast<unsigned char>('_'), index);
               return;
            }
            if (symbol.text == "[0-9]+") {
               add_dispatch_range(buckets, static_cast<unsigned char>('0'), static_cast<unsigned char>('9'), index);
               return;
            }
            if (symbol.text == "[ \t\r\n]+") {
               add_dispatch_byte(buckets, static_cast<unsigned char>(' '), index);
               add_dispatch_byte(buckets, static_cast<unsigned char>('\t'), index);
               add_dispatch_byte(buckets, static_cast<unsigned char>('\r'), index);
               add_dispatch_byte(buckets, static_cast<unsigned char>('\n'), index);
               return;
            }
            if (symbol.text == "//[^\n]*") {
               add_dispatch_byte(buckets, static_cast<unsigned char>('/'), index);
               return;
            }

            fallback.push_back(index);
         }

         auto build_lexer_dispatch(const grammar_spec& grammar) -> lexer_dispatch {
            auto dispatch = lexer_dispatch{};
            for (std::size_t index = 0; index < grammar.token_symbol_count; ++index) {
               register_dispatch_symbol(dispatch.token_candidates, dispatch.token_fallback_candidates,
                                        grammar.token_symbols[index], index);
            }
            for (std::size_t index = 0; index < grammar.skip_symbol_count; ++index) {
               register_dispatch_symbol(dispatch.skip_candidates, dispatch.skip_fallback_candidates,
                                        grammar.skip_symbols[index], index);
            }
            return dispatch;
         }

         [[nodiscard]] auto lexer_dispatch_key_of(const grammar_spec& grammar) -> lexer_dispatch_key {
            return lexer_dispatch_key{grammar.token_symbols,
                                      grammar.token_symbol_count,
                                      grammar.skip_symbols,
                                      grammar.skip_symbol_count};
         }

         [[nodiscard]] auto lexer_dispatch_of(const grammar_spec& grammar) -> const lexer_dispatch& {
            struct cache_entry {
               lexer_dispatch_key key{};
               lexer_dispatch dispatch;
               bool valid = false;
            };

            thread_local auto cache = cache_entry{};
            const auto key = lexer_dispatch_key_of(grammar);
            if (!cache.valid || cache.key != key) {
               cache.key = key;
               cache.dispatch = build_lexer_dispatch(grammar);
               cache.valid = true;
            }
            return cache.dispatch;
         }

         auto build_earley_machine(const grammar_spec& grammar) -> earley_machine {
            auto machine = earley_machine{};
            machine.production_state_offsets.resize(grammar.production_count);

            for (std::size_t production_index = 0; production_index < grammar.production_count; ++production_index) {
               const auto& production = grammar.productions[production_index];
               const auto base = machine.states.size();
               machine.production_state_offsets[production_index] = base;
               machine.states.reserve(machine.states.size() + production.symbol_count + 1);
               for (std::size_t dot = 0; dot <= production.symbol_count; ++dot) {
                  auto state = recognize_state{};
                  state.production = production_index;
                  state.dot = dot;
                  state.lhs = production.lhs;
                  state.complete = dot == production.symbol_count;
                  if (!state.complete) {
                     state.next = production.symbols[dot];
                     state.advance_state = base + dot + 1;
                  }
                  machine.states.push_back(state);
               }
            }

            return machine;
         }

         [[nodiscard]] auto earley_machine_key_of(const grammar_spec& grammar) -> earley_machine_key {
            return earley_machine_key{grammar.productions,
                                      grammar.rule_production_indices,
                                      grammar.rule_production_offsets,
                                      grammar.rule_production_counts,
                                      grammar.production_count,
                                      grammar.rule_count};
         }

         [[nodiscard]] auto earley_machine_of(const grammar_spec& grammar) -> const earley_machine& {
            struct cache_entry {
               earley_machine_key key{};
               earley_machine machine;
               bool valid = false;
            };

            thread_local auto cache = cache_entry{};
            const auto key = earley_machine_key_of(grammar);
            if (!cache.valid || cache.key != key) {
               cache.key = key;
               cache.machine = build_earley_machine(grammar);
               cache.valid = true;
            }
            return cache.machine;
         }

         auto token_position_offset(std::string_view input, const std::vector<lexed_token>& tokens, std::size_t position)
               -> std::size_t {
            if (position >= tokens.size()) {
               return input.size();
            }
            return tokens[position].text.range.begin.offset;
         }

         auto token_begin_offset(const std::vector<lexed_token>& tokens, std::size_t position) -> std::size_t {
            return tokens[position].text.range.begin.offset;
         }

         auto token_end_offset(const std::vector<lexed_token>& tokens, std::size_t position) -> std::size_t {
            return tokens[position].text.range.end.offset;
         }

         auto token_boundary_range(std::string_view input, const std::vector<lexed_token>& tokens, std::size_t position)
               -> source_range {
            const auto offset = token_position_offset(input, tokens, position);
            return make_source_range(input, offset, offset);
         }

         auto best_token_match_at(std::string_view input, std::size_t position, const grammar_spec& grammar)
               -> std::optional<raw_lexer_match> {
            auto best = std::optional<raw_lexer_match>{};
            const auto& dispatch = lexer_dispatch_of(grammar);
            const auto current = static_cast<unsigned char>(input[position]);

            const auto consider = [&](bool skip, std::size_t index, const lexer_symbol_spec& symbol) {
               auto next = position;
               std::string capture;
               if (!try_lexer_symbol_at(input, next, symbol, &capture) || next == position) {
                  return;
               }
                auto candidate = raw_lexer_match{index, next, symbol.precedence, symbol.kind, std::move(capture), skip};
               if (better_raw_lexer_match(candidate, best, position)) {
                  best = std::move(candidate);
               }
            };

            for (const auto index: dispatch.token_candidates[current]) {
               consider(false, index, grammar.token_symbols[index]);
            }
            for (const auto index: dispatch.token_fallback_candidates) {
               consider(false, index, grammar.token_symbols[index]);
            }
            for (const auto index: dispatch.skip_candidates[current]) {
               consider(true, index, grammar.skip_symbols[index]);
            }
            for (const auto index: dispatch.skip_fallback_candidates) {
               consider(true, index, grammar.skip_symbols[index]);
            }
            return best;
         }

         auto tokenize_input(std::string_view input, const grammar_spec& grammar) -> token_sequence {
            token_sequence tokenized;
            tokenized.input = std::string{input};
            auto position = std::size_t{0};
            while (position < input.size()) {
               if (grammar.use_default_whitespace &&
                   std::isspace(static_cast<unsigned char>(input[position])) != 0) {
                  skip_default_space(input, position);
                  continue;
               }

               auto match = best_token_match_at(input, position, grammar);
               if (!match.has_value()) {
                  const auto end = position + 1;
                  lexed_token token;
                  token.invalid = true;
                  token.text.text = std::string{input.substr(position, end - position)};
                  token.text.range = make_source_range(input, position, end);
                  tokenized.tokens.push_back(std::move(token));
                  position = end;
                  continue;
               }

               if (match->skip) {
                  position = match->end;
                  continue;
               }

               lexed_token token;
               token.symbol = match->symbol;
               token.text.text = std::move(match->text);
               token.text.range = make_source_range(input, position, match->end);

               tokenized.tokens.push_back(std::move(token));
               position = match->end;
            }

            return tokenized;
         }

         auto range_of(const parse_value& value) -> source_range {
            if (std::holds_alternative<matched_string>(value)) {
               return std::get<matched_string>(value).range;
            }
            return std::get<parse_node_ptr>(value)->range;
         }

         struct input_patch {
            std::size_t offset = 0;
            std::size_t erase_count = 0;
            std::string insert_text;
         };

         struct consumed_interval {
            std::size_t begin = 0;
            std::size_t end = 0;
         };

         struct repaired_input_plan {
            std::vector<input_patch> patches;
            std::vector<consumed_interval> consumed;
            bool saw_damage = false;
         };

         void extend_range(source_range& target, const source_range& candidate) {
            if (candidate.begin.offset < target.begin.offset) {
               target.begin = candidate.begin;
            }
            if (candidate.end.offset > target.end.offset) {
               target.end = candidate.end;
            }
         }

         auto describe_expected_symbol(const parser_symbol& symbol, const grammar_spec& grammar) -> std::string {
            if (symbol.kind == parser_symbol_kind::positive_nonterminal ||
                symbol.kind == parser_symbol_kind::negative_nonterminal) {
               auto underlying = symbol;
               underlying.kind = parser_symbol_kind::nonterminal;
               auto description = describe_expected_symbol(underlying, grammar);
               return symbol.kind == parser_symbol_kind::positive_nonterminal ? description : "not " + description;
            }
            if (symbol.kind == parser_symbol_kind::positive_terminal ||
                symbol.kind == parser_symbol_kind::negative_terminal) {
               auto underlying = symbol;
               underlying.kind = parser_symbol_kind::terminal;
               auto description = describe_expected_symbol(underlying, grammar);
               return symbol.kind == parser_symbol_kind::positive_terminal ? description : "not " + description;
            }
            if (symbol.kind == parser_symbol_kind::nonterminal) {
               if (grammar.rule_expected_labels != nullptr && symbol.value < grammar.rule_count &&
                   !grammar.rule_expected_labels[symbol.value].empty()) {
                  return std::string{grammar.rule_expected_labels[symbol.value]};
               }
               return "rule '" + std::string{symbol.text} + "'";
            }
            const auto& terminal = grammar.token_symbols[symbol.value];
            if (terminal.kind == lexer_symbol_kind::literal) {
               return quoted(terminal.text);
            }
            return "pattern " + std::string{terminal.text};
         }

         auto describe_progress(const production_spec& production, std::size_t dot) -> std::string {
            return "while parsing rule '" + std::string{production.lhs_name} + "' via " +
                   std::string{production.debug_text} + " (after symbol " + std::to_string(dot) + " of " +
                   std::to_string(production.symbol_count) + ")";
         }

         auto match_terminal(const std::vector<lexed_token>& tokens, std::size_t position, const parser_symbol& symbol,
                             const grammar_spec& grammar)
               -> std::optional<terminal_match> {
            if (position >= tokens.size()) {
               return std::nullopt;
            }
            const auto& token = tokens[position];
            if (token.invalid) {
               return std::nullopt;
            }
            if (token.symbol != symbol.value) {
               const auto& expected = grammar.token_symbols[symbol.value];
               const auto& actual = grammar.token_symbols[token.symbol];
               if (expected.kind != lexer_symbol_kind::regex || actual.kind != lexer_symbol_kind::regex) {
                  return std::nullopt;
               }
               auto token_position = std::size_t{0};
               if (!try_lexer_symbol_at(token.text.text, token_position, expected) || token_position != token.text.text.size()) {
                  return std::nullopt;
               }
            }
            return terminal_match{position + 1, token.text};
         }

         [[nodiscard]] bool is_positive_lookahead(const parser_symbol& symbol) {
            return symbol.kind == parser_symbol_kind::positive_nonterminal ||
                   symbol.kind == parser_symbol_kind::positive_terminal;
         }

         [[nodiscard]] bool is_negative_lookahead(const parser_symbol& symbol) {
            return symbol.kind == parser_symbol_kind::negative_nonterminal ||
                   symbol.kind == parser_symbol_kind::negative_terminal;
         }

         [[nodiscard]] bool is_terminal_lookahead(const parser_symbol& symbol) {
            return symbol.kind == parser_symbol_kind::positive_terminal ||
                   symbol.kind == parser_symbol_kind::negative_terminal;
         }

         [[nodiscard]] bool is_nonterminal_lookahead(const parser_symbol& symbol) {
            return symbol.kind == parser_symbol_kind::positive_nonterminal ||
                   symbol.kind == parser_symbol_kind::negative_nonterminal;
         }

         [[nodiscard]] auto as_consuming_symbol(const parser_symbol& symbol) -> parser_symbol {
            auto normalized = symbol;
            if (is_terminal_lookahead(symbol)) {
               normalized.kind = parser_symbol_kind::terminal;
            } else if (is_nonterminal_lookahead(symbol)) {
               normalized.kind = parser_symbol_kind::nonterminal;
            }
            return normalized;
         }

         auto predicate_matches(std::string_view input, const std::vector<lexed_token>& tokens,
                                const grammar_spec& grammar, const parser_symbol& symbol, std::size_t position,
                                predicate_context& context) -> bool;

         auto recognize_fast(std::string_view input, const std::vector<lexed_token>& tokens,
                             const grammar_spec& grammar, std::size_t root_rule, std::size_t start_position,
                             predicate_context& context) -> bool {
            if (root_rule >= grammar.rule_count || start_position > tokens.size()) {
               return false;
            }

            const auto root_count = grammar.rule_production_counts[root_rule];
            if (root_count == 0) {
               return false;
            }

            const auto& machine = earley_machine_of(grammar);
            const auto item_codec = packed_pair_codec::make(machine.states.size(), tokens.size());
            const auto completion_codec = packed_pair_codec::make(grammar.rule_count, tokens.size());
            const auto chart_size = tokens.size() + 1;

            thread_local auto recognize_fast_buffers = std::vector<std::unique_ptr<recognize_fast_buffer>>{};
            thread_local auto recognize_fast_depth = std::size_t{0};

            ++recognize_fast_depth;
            if (recognize_fast_buffers.size() < recognize_fast_depth) {
               recognize_fast_buffers.resize(recognize_fast_depth);
            }
            if (recognize_fast_buffers[recognize_fast_depth - 1] == nullptr) {
               recognize_fast_buffers[recognize_fast_depth - 1] = std::make_unique<recognize_fast_buffer>();
            }

            auto& buffer = *recognize_fast_buffers[recognize_fast_depth - 1];
            if (buffer.chart.size() < chart_size) {
               buffer.chart.resize(chart_size);
            }
            if (buffer.column_generations.size() < chart_size) {
               buffer.column_generations.resize(chart_size, 0);
            }
            buffer.waiting_heads_storage.resize(chart_size * grammar.rule_count);
            buffer.waiting_head_generations.resize(chart_size * grammar.rule_count, 0);
            buffer.completion_heads_storage.resize(chart_size * grammar.rule_count);
            buffer.completion_head_generations.resize(chart_size * grammar.rule_count, 0);
            if (buffer.generation == std::numeric_limits<std::uint32_t>::max()) {
               std::fill(buffer.column_generations.begin(), buffer.column_generations.end(), 0);
               std::fill(buffer.waiting_head_generations.begin(), buffer.waiting_head_generations.end(), 0);
               std::fill(buffer.completion_head_generations.begin(), buffer.completion_head_generations.end(), 0);
               buffer.generation = 1;
            } else {
               ++buffer.generation;
            }
            auto& chart = buffer.chart;
            const auto generation = buffer.generation;

            const auto activate_column = [&](std::size_t position) -> recognize_column& {
               auto& column = chart[position];
               if (buffer.column_generations[position] != generation) {
                  column.reset(buffer.waiting_heads_storage.data() + position * grammar.rule_count,
                               buffer.waiting_head_generations.data() + position * grammar.rule_count,
                               buffer.completion_heads_storage.data() + position * grammar.rule_count,
                               buffer.completion_head_generations.data() + position * grammar.rule_count,
                               generation,
                               grammar.rule_count);
                  buffer.column_generations[position] = generation;
               }
               return column;
            };

            const auto enqueue_item = [&](auto&& self, std::size_t position, std::size_t state,
                                          std::size_t start) -> void {
               auto& column = activate_column(position);
               if (!column.add_item(machine, item_codec, state, start)) {
                  return;
               }

               const auto& state_spec = machine.states[state];
               if (!state_spec.complete) {
                  if (state_spec.next.kind == parser_symbol_kind::nonterminal) {
                     for (auto completion_index = column.completion_head_for(state_spec.next.value);
                          completion_index != recognize_column::invalid_index;) {
                        const auto completion = column.completions[completion_index];
                        completion_index = completion.next;
                        self(self, completion.end, state_spec.advance_state, start);
                     }
                  }
                  return;
               }

               auto& start_column = activate_column(start);
               if (!start_column.add_completion(completion_codec, state_spec.lhs, position)) {
                  return;
               }

               for (auto parent_index = start_column.waiting_head_for(state_spec.lhs);
                    parent_index != recognize_column::invalid_index;) {
                  const auto parent = start_column.items[parent_index];
                  parent_index = parent.next_waiting;
                  const auto& parent_state = machine.states[parent.state];
                  self(self, position, parent_state.advance_state, parent.start);
               }
            };

            const auto root_begin = grammar.rule_production_offsets[root_rule];
            for (std::size_t offset = 0; offset < root_count; ++offset) {
               const auto production = grammar.rule_production_indices[root_begin + offset];
               enqueue_item(enqueue_item, start_position, machine.production_state_offsets[production], start_position);
            }

            for (std::size_t position = start_position; position < chart_size; ++position) {
               if (buffer.column_generations[position] != generation) {
                  continue;
               }
               auto& column = chart[position];
               for (std::size_t index = 0; index < column.items.size(); ++index) {
                  const auto item = column.items[index];
                  const auto& state = machine.states[item.state];

                  if (state.complete) {
                     continue;
                  }

                  const auto& next = state.next;
                  if (next.kind == parser_symbol_kind::nonterminal) {
                     const auto rule_begin = grammar.rule_production_offsets[next.value];
                     const auto rule_count = grammar.rule_production_counts[next.value];
                     for (std::size_t offset = 0; offset < rule_count; ++offset) {
                        const auto production = grammar.rule_production_indices[rule_begin + offset];
                        enqueue_item(enqueue_item, position, machine.production_state_offsets[production], position);
                     }
                     continue;
                  }

                  if (is_positive_lookahead(next) || is_negative_lookahead(next)) {
                     const auto matched = predicate_matches(input, tokens, grammar, next, position, context);
                     const auto success = is_positive_lookahead(next) ? matched : !matched;
                     if (success) {
                        enqueue_item(enqueue_item, position, state.advance_state, item.start);
                     }
                     continue;
                  }

                  if (auto match = match_terminal(tokens, position, next, grammar); match.has_value()) {
                     enqueue_item(enqueue_item, match->end, state.advance_state, item.start);
                  }
               }
            }

            const auto matched = buffer.column_generations[start_position] == generation &&
                                 chart[start_position].has_completion(completion_codec, root_rule, tokens.size());
            --recognize_fast_depth;
            return matched;
         }

         auto virtual_terminal(std::string_view input, const std::vector<lexed_token>& tokens, std::size_t position,
                               std::string_view text) -> matched_string {
            matched_string match;
            match.text = std::string{text};
            match.range = token_boundary_range(input, tokens, position);
            return match;
         }

         auto ignored_symbol_end(std::size_t position, std::size_t limit) -> std::size_t {
            if (position >= limit) {
               return position;
            }
            return std::min(limit, position + 1);
         }

         auto trivia_only(std::string_view input, std::size_t begin, std::size_t end, const grammar_spec& grammar)
               -> bool {
            if (begin > end || end > input.size()) {
               return false;
            }
            auto prefix = input.substr(0, end);
            auto current = begin;
            skip_trivia(prefix, current, grammar);
            return current == end;
         }

         auto append_consumed_interval(repaired_input_plan& plan, std::size_t begin, std::size_t end,
                                       std::size_t input_size) -> bool {
            if (begin > end || end > input_size) {
               return false;
            }
            if (begin == end) {
               return true;
            }
            plan.consumed.push_back(consumed_interval{begin, end});
            return true;
         }

         auto collect_repaired_input_plan(const parse_node_ptr& tree, std::string_view input,
                                          repaired_input_plan& plan) -> bool {
            if (tree == nullptr) {
               return false;
            }

            for (const auto& damage: tree->damage) {
               const auto begin = damage.range.begin.offset;
               const auto end = damage.range.end.offset;
               if (begin > end || end > input.size()) {
                  return false;
               }

               if (damage.reason == node_damage_reason::ignored_invalid_input) {
                  if (begin == end || (damage.detail != quoted(input.substr(begin, end - begin)))) {
                     return false;
                  }
                  plan.saw_damage = true;
                  plan.patches.push_back(input_patch{begin, end - begin, {}});
                  if (!append_consumed_interval(plan, begin, end, input.size())) {
                     return false;
                  }
               } else if (damage.reason == node_damage_reason::inserted_virtual_token) {
                  if (begin != end) {
                     return false;
                  }
                  plan.saw_damage = true;
               }
            }

            for (const auto& child: tree->children) {
               if (const auto* text = std::get_if<matched_string>(&child)) {
                  const auto begin = text->range.begin.offset;
                  const auto end = text->range.end.offset;
                  if (begin > end || end > input.size()) {
                     return false;
                  }
                  if (begin != end) {
                     if (input.substr(begin, end - begin) != text->text) {
                        return false;
                     }
                     if (!append_consumed_interval(plan, begin, end, input.size())) {
                        return false;
                     }
                  } else if (!text->text.empty()) {
                     plan.patches.push_back(input_patch{begin, 0, text->text});
                  }
               } else if (!collect_repaired_input_plan(std::get<parse_node_ptr>(child), input, plan)) {
                  return false;
               }
            }

            return true;
         }

         auto validate_repaired_input_plan(std::string_view input, const repaired_input_plan& plan,
                                           const grammar_spec& grammar) -> bool {
            auto consumed = plan.consumed;
            std::sort(consumed.begin(), consumed.end(), [](const auto& lhs, const auto& rhs) {
               if (lhs.begin != rhs.begin) {
                  return lhs.begin < rhs.begin;
               }
               return lhs.end < rhs.end;
            });

            auto cursor = std::size_t{0};
            for (const auto& interval: consumed) {
               if (interval.begin < cursor || !trivia_only(input, cursor, interval.begin, grammar)) {
                  return false;
               }
               cursor = interval.end;
            }
            if (!trivia_only(input, cursor, input.size(), grammar)) {
               return false;
            }

            auto removals = std::vector<consumed_interval>{};
            for (const auto& patch: plan.patches) {
               if (patch.offset > input.size() || patch.erase_count > (input.size() - patch.offset)) {
                  return false;
               }
               if (patch.erase_count != 0) {
                  removals.push_back(consumed_interval{patch.offset, patch.offset + patch.erase_count});
               }
            }
            std::sort(removals.begin(), removals.end(), [](const auto& lhs, const auto& rhs) {
               if (lhs.begin != rhs.begin) {
                  return lhs.begin < rhs.begin;
               }
               return lhs.end < rhs.end;
            });
            for (std::size_t index = 1; index < removals.size(); ++index) {
               if (removals[index].begin < removals[index - 1].end) {
                  return false;
               }
            }

            return true;
         }

         auto apply_repaired_input_plan(std::string_view input, repaired_input_plan plan) -> std::string {
            auto output = std::string{input};
            std::sort(plan.patches.begin(), plan.patches.end(), [](const auto& lhs, const auto& rhs) {
               if (lhs.offset != rhs.offset) {
                  return lhs.offset > rhs.offset;
               }
               return lhs.erase_count > rhs.erase_count;
            });
            for (const auto& patch: plan.patches) {
               output.replace(patch.offset, patch.erase_count, patch.insert_text);
            }
            return output;
         }

         struct chart_column {
            static constexpr auto invalid_index = std::numeric_limits<std::size_t>::max();

            struct item_index_set {
               std::vector<chart_item_key> slots;
               std::size_t size = 0;

               item_index_set() : slots(16, empty_key()) {}

               [[nodiscard]] static auto empty_key() -> chart_item_key {
                  return chart_item_key{std::numeric_limits<std::size_t>::max(), 0, 0};
               }

               [[nodiscard]] static auto is_empty(const chart_item_key& key) -> bool {
                  return key.production == std::numeric_limits<std::size_t>::max();
               }

               [[nodiscard]] auto insert(const chart_item_key& key) -> bool {
                  if ((size + 1) * 10 >= slots.size() * 7) {
                     rehash(slots.size() * 2);
                  }

                  auto index = chart_item_key_hash{}(key) & (slots.size() - 1);
                  while (true) {
                     auto& slot = slots[index];
                     if (is_empty(slot)) {
                        slot = key;
                        ++size;
                        return true;
                     }
                     if (slot == key) {
                        return false;
                     }
                     index = (index + 1) & (slots.size() - 1);
                  }
               }

               void rehash(std::size_t new_capacity) {
                  auto old_slots = std::move(slots);
                  slots.assign(new_capacity, empty_key());
                  size = 0;
                  for (const auto& slot: old_slots) {
                     if (!is_empty(slot)) {
                        (void) insert(slot);
                     }
                  }
               }
            };

            struct item_record {
               std::size_t production = 0;
               std::size_t dot = 0;
               std::size_t start = 0;
               std::size_t next_waiting = invalid_index;
            };

            std::vector<item_record> items;
            item_index_set indices;
            std::size_t* waiting_heads = nullptr;

            void initialize(std::size_t* waiting_storage) {
               waiting_heads = waiting_storage;
            }

            auto add(const grammar_spec& grammar, std::size_t production, std::size_t dot, std::size_t start) -> bool {
               auto key = chart_item_key{production, dot, start};
               if (!indices.insert(key)) {
                  return false;
               }

               auto record = item_record{production, dot, start, invalid_index};
               const auto& production_spec = grammar.productions[production];
               if (dot < production_spec.symbol_count) {
                  const auto& next = production_spec.symbols[dot];
                  if (next.kind == parser_symbol_kind::nonterminal) {
                     record.next_waiting = waiting_heads[next.value];
                     waiting_heads[next.value] = items.size();
                  }
               }
               items.push_back(record);
               return true;
            }

            [[nodiscard]] auto waiting_head_for(std::size_t rule) const -> std::size_t {
               if (waiting_heads != nullptr) {
                  return waiting_heads[rule];
               }
               return invalid_index;
            }
         };

         struct prepared_parse {
            bool has_root_production = false;
            std::vector<lexed_token> tokens;
            std::size_t chart_size = 0;
            std::unordered_map<span_key, std::vector<std::size_t>, span_key_hash> completed_rules;
            std::unordered_set<span_key, span_key_hash> completed_productions;
            std::vector<std::vector<std::vector<std::size_t>>> rule_ends;
            parse_error error;
         };

         [[nodiscard]] auto rule_end_list(prepared_parse& prepared, std::size_t rule, std::size_t start)
               -> std::vector<std::size_t>& {
            return prepared.rule_ends[start][rule];
         }

         [[nodiscard]] auto rule_end_list(const prepared_parse& prepared, std::size_t rule, std::size_t start)
               -> const std::vector<std::size_t>& {
            return prepared.rule_ends[start][rule];
         }

         auto collect_root_ends(std::string_view input, const prepared_parse& prepared, const grammar_spec& grammar,
                                std::size_t root_rule, bool require_full_input) -> std::vector<std::size_t>;
         auto build_root_forest(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                const prepared_parse& prepared, const std::vector<std::size_t>& root_ends,
                                forest_span_order span_order)
               -> std::vector<parse_node_ptr>;
         auto recover_full_input(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                 const prepared_parse& prepared) -> parse_forest;

         auto rule_completed_at(const prepared_parse& prepared, std::size_t rule, std::size_t start, std::size_t end)
               -> bool {
            const auto& completed = rule_end_list(prepared, rule, start);
            return std::binary_search(completed.begin(), completed.end(), end);
         }

         auto capped_add(std::size_t left, std::size_t right, std::size_t limit) -> std::size_t {
            if (left >= limit || right >= limit) {
               return limit;
            }
            auto remaining = limit - left;
            return left + std::min(right, remaining);
         }

         auto capped_mul(std::size_t left, std::size_t right, std::size_t limit) -> std::size_t {
            if (left == 0 || right == 0) {
               return 0;
            }
            if (left >= limit || right >= limit) {
               return limit;
            }
            if (left > (limit / right)) {
               return limit;
            }
            return std::min(limit, left * right);
         }

         auto prepare_earley_parse(std::string_view input, std::vector<lexed_token> tokens, const grammar_spec& grammar,
                                   std::size_t root_rule, std::size_t start_position,
                                   predicate_context& predicate_matches_cache, bool track_errors = true)
               -> prepared_parse {
            prepared_parse prepared;
            error_tracker tracker;
            prepared.tokens = std::move(tokens);
            prepared.chart_size = prepared.tokens.size() + 1;
            prepared.rule_ends.resize(prepared.chart_size);
            for (auto& starts: prepared.rule_ends) {
               starts.resize(grammar.rule_count);
            }

            auto chart = std::vector<chart_column>(prepared.chart_size);
            auto waiting_heads_storage = std::vector<std::size_t>(prepared.chart_size * grammar.rule_count,
                                                                  chart_column::invalid_index);
            for (std::size_t index = 0; index < chart.size(); ++index) {
               chart[index].initialize(waiting_heads_storage.data() + index * grammar.rule_count);
            }

            const auto enqueue_item = [&](auto&& self, std::size_t position, std::size_t production, std::size_t dot,
                                          std::size_t start) -> bool {
               if (!chart[position].add(grammar, production, dot, start)) {
                  return false;
               }

               const auto& production_spec = grammar.productions[production];
               if (dot < production_spec.symbol_count) {
                  const auto& next = production_spec.symbols[dot];
                  if (next.kind == parser_symbol_kind::nonterminal) {
                     const auto& completed_ends = rule_end_list(prepared, next.value, position);
                     for (const auto end: completed_ends) {
                        (void) self(self, end, production, dot + 1, start);
                     }
                  }
               }

               return true;
            };

            const auto root_begin = grammar.rule_production_offsets[root_rule];
            const auto root_count = grammar.rule_production_counts[root_rule];
            for (std::size_t offset = 0; offset < root_count; ++offset) {
               (void) enqueue_item(enqueue_item, start_position, grammar.rule_production_indices[root_begin + offset], 0,
                                   start_position);
               prepared.has_root_production = true;
            }

            if (!prepared.has_root_production) {
               prepared.error.message = "Parse error: root rule has no productions";
               return prepared;
            }

            for (std::size_t position = start_position; position < chart.size(); ++position) {
               for (std::size_t index = 0; index < chart[position].items.size(); ++index) {
                  const auto item = chart[position].items[index];
                  const auto production_index = item.production;
                  const auto dot = item.dot;
                  const auto start = item.start;
                  const auto& production = grammar.productions[production_index];

                  if (dot == production.symbol_count) {
                      if (track_errors) {
                         prepared.completed_rules[span_key{production.lhs, start, position}].push_back(production_index);
                      }
                      auto& completed_ends = rule_end_list(prepared, production.lhs, start);
                      if (completed_ends.empty() || completed_ends.back() != position) {
                         completed_ends.push_back(position);
                      }
                      prepared.completed_productions.insert(span_key{production_index, start, position});

                      for (auto parent_index = chart[start].waiting_head_for(production.lhs);
                           parent_index != chart_column::invalid_index;) {
                         const auto parent = chart[start].items[parent_index];
                         parent_index = parent.next_waiting;
                         (void) enqueue_item(enqueue_item, position, parent.production, parent.dot + 1, parent.start);
                      }
                     continue;
                  }

                  const auto& next = production.symbols[dot];
                  if (next.kind == parser_symbol_kind::nonterminal) {
                      const auto rule_begin = grammar.rule_production_offsets[next.value];
                      const auto rule_count = grammar.rule_production_counts[next.value];
                      for (std::size_t offset = 0; offset < rule_count; ++offset) {
                         const auto candidate = grammar.rule_production_indices[rule_begin + offset];
                         (void) enqueue_item(enqueue_item, position, candidate, 0, position);
                     }
                     continue;
                  }

                  if (is_positive_lookahead(next) || is_negative_lookahead(next)) {
                     const auto matched = predicate_matches(input, prepared.tokens, grammar, next, position,
                                                            predicate_matches_cache);
                     const auto success = is_positive_lookahead(next) ? matched : !matched;
                     if (!success) {
                        if (!track_errors) {
                           continue;
                        }
                        tracker.record(token_position_offset(input, prepared.tokens, position),
                                       describe_expected_symbol(next, grammar), describe_progress(production, dot));
                        continue;
                     }
                     (void) enqueue_item(enqueue_item, position, production_index, dot + 1, start);
                     continue;
                  }

                  auto match = match_terminal(prepared.tokens, position, next, grammar);
                  if (!match.has_value()) {
                     if (!track_errors) {
                        continue;
                     }
                     auto expected = describe_expected_symbol(next, grammar);
                     if (grammar.rule_expected_labels != nullptr && production.lhs < grammar.rule_count &&
                         !grammar.rule_expected_labels[production.lhs].empty()) {
                        expected = std::string{grammar.rule_expected_labels[production.lhs]};
                     }
                     tracker.record(token_position_offset(input, prepared.tokens, position),
                                    std::move(expected),
                                    describe_progress(production, dot));
                     continue;
                  }
                  (void) enqueue_item(enqueue_item, match->end, production_index, dot + 1, start);
               }
            }

             auto success = false;
             for (std::size_t end = start_position; end < prepared.chart_size; ++end) {
                if (end != prepared.tokens.size()) {
                  continue;
               }
                 if (rule_completed_at(prepared, root_rule, start_position, end)) {
                  success = true;
                  break;
               }
            }

             if (!success && track_errors) {
                for (std::size_t end = start_position; end < prepared.chart_size; ++end) {
                    if (rule_completed_at(prepared, root_rule, start_position, end)) {
                     auto failure_position = token_position_offset(input, prepared.tokens, end);
                     tracker.record(failure_position, "<end of input>",
                                    "after completing rule '" + std::string{grammar.productions[root_begin].lhs_name} + "'");
                  }
               }
               prepared.error = tracker.build(input);
            }

            return prepared;
         }

         auto predicate_matches(std::string_view input, const std::vector<lexed_token>& tokens,
                                const grammar_spec& grammar, const parser_symbol& symbol, std::size_t position,
                                predicate_context& context) -> bool {
            const auto target = as_consuming_symbol(symbol);
            if (target.kind == parser_symbol_kind::terminal) {
               return match_terminal(tokens, position, target, grammar).has_value();
            }

            auto key = predicate_query_key{&grammar, &tokens, target.value, position};
            if (auto cached = context.matches.find(key); cached != context.matches.end()) {
               return cached->second;
            }
            if (!context.in_progress.insert(key).second) {
               return false;
            }

            const auto matched = recognize_fast(input, tokens, grammar, target.value, position, context);

            context.in_progress.erase(key);
            context.matches.emplace(key, matched);
            return matched;
         }

          auto prepare_earley_parse(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                    bool track_errors = true) -> prepared_parse {
            auto predicate_matches_cache = predicate_context{};
            return prepare_earley_parse(input, tokenize_input(input, grammar).tokens, grammar, root_rule, 0,
                                        predicate_matches_cache, track_errors);
         }

         auto finish_earley_parse(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                  prepared_parse prepared, bool allow_partial, forest_span_order span_order)
               -> parse_forest {
            if (!prepared.has_root_production) {
               parse_forest result;
               result.error = prepared.error;
               return result;
            }

            auto full_ends = collect_root_ends(input, prepared, grammar, root_rule, true);
            if (!full_ends.empty()) {
               parse_forest result;
               result.success = true;
               result.forest = build_root_forest(input, grammar, root_rule, prepared, full_ends, span_order);
               result.tree_partial.assign(result.forest.size(), false);
               result.tree_damage.resize(result.forest.size());
               return result;
            }

            if (!allow_partial) {
               parse_forest result;
               result.error = prepared.error;
               return result;
            }

            return recover_full_input(input, grammar, root_rule, prepared);
         }

         auto finish_earley_recognize(std::size_t root_rule, prepared_parse prepared) -> recognize_result {
            recognize_result result;
            if (!prepared.has_root_production) {
               result.error = prepared.error;
               return result;
            }

               for (std::size_t end = 0; end < prepared.chart_size; ++end) {
               if (end != prepared.tokens.size()) {
                  continue;
               }

               if (rule_completed_at(prepared, root_rule, 0, end)) {
                  result.success = true;
                  return result;
               }
            }

            result.error = prepared.error;
            return result;
         }

         auto collect_root_ends(std::string_view input, const prepared_parse& prepared, const grammar_spec& grammar,
                                std::size_t root_rule, bool require_full_input) -> std::vector<std::size_t> {
            (void) input;
            (void) grammar;
            auto ends = std::vector<std::size_t>{};
               for (std::size_t end = 0; end < prepared.chart_size; ++end) {
                if (require_full_input && end != prepared.tokens.size()) {
                  continue;
               }
               if (rule_completed_at(prepared, root_rule, 0, end)) {
                  ends.push_back(end);
               }
            }
            return ends;
         }

         auto build_root_forest(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                const prepared_parse& prepared, const std::vector<std::size_t>& root_ends,
                                forest_span_order span_order)
               -> std::vector<parse_node_ptr> {
            using node_list = std::vector<parse_node_ptr>;

            const auto empty_nodes = node_list{};
            std::unordered_map<span_key, node_list, span_key_hash> rule_cache;
            std::unordered_map<span_key, node_list, span_key_hash> production_cache;
            std::unordered_set<span_key, span_key_hash> rule_in_progress;
            std::unordered_set<span_key, span_key_hash> production_in_progress;
            auto predicate_matches_cache = predicate_context{};

            std::function<const node_list&(std::size_t, std::size_t, std::size_t)> build_rule;
            std::function<const node_list&(std::size_t, std::size_t, std::size_t)> build_production;

            build_rule = [&](std::size_t rule, std::size_t start, std::size_t end) -> const node_list& {
               auto key = span_key{rule, start, end};
               if (rule_in_progress.contains(key)) {
                  return empty_nodes;
               }
               if (auto cache = rule_cache.find(key); cache != rule_cache.end()) {
                  return cache->second;
               }

               rule_in_progress.insert(key);
               auto nodes = node_list{};
               if (auto completed = prepared.completed_rules.find(key); completed != prepared.completed_rules.end()) {
                  for (auto production_index: completed->second) {
                     const auto& produced = build_production(production_index, start, end);
                     nodes.insert(nodes.end(), produced.begin(), produced.end());
                  }
               }
               rule_in_progress.erase(key);
               return rule_cache.emplace(key, std::move(nodes)).first->second;
            };

            build_production = [&](std::size_t production_index, std::size_t start, std::size_t end)
                  -> const node_list& {
               auto key = span_key{production_index, start, end};
               if (production_in_progress.contains(key) || !prepared.completed_productions.contains(key)) {
                  return empty_nodes;
               }
               if (auto cache = production_cache.find(key); cache != production_cache.end()) {
                  return cache->second;
               }

               production_in_progress.insert(key);
               const auto& production = grammar.productions[production_index];
               auto nodes = node_list{};
               std::vector<parse_value> children;

               std::function<void(std::size_t, std::size_t)> enumerate = [&](std::size_t symbol_index,
                                                                             std::size_t position) {
                  if (symbol_index == production.symbol_count) {
                     if (position == end) {
                         auto range = make_source_range(input, token_position_offset(input, prepared.tokens, start),
                                                        token_position_offset(input, prepared.tokens, end));
                        if (!children.empty()) {
                           range.begin = range_of(children.front()).begin;
                           range.end = range_of(children.back()).end;
                        }
                        nodes.push_back(std::make_shared<parse_node>(
                              parse_node{production.lhs, production_index, start, end, range, children, {}, false}));
                     }
                     return;
                  }

                  const auto& symbol = production.symbols[symbol_index];
                  if (is_positive_lookahead(symbol) || is_negative_lookahead(symbol)) {
                     const auto matched = predicate_matches(input, prepared.tokens, grammar, symbol, position,
                                                            predicate_matches_cache);
                     if ((is_positive_lookahead(symbol) && matched) ||
                         (is_negative_lookahead(symbol) && !matched)) {
                        enumerate(symbol_index + 1, position);
                     }
                     return;
                  }
                  if (symbol.kind == parser_symbol_kind::nonterminal) {
                     const auto& child_ends = rule_end_list(prepared, symbol.value, position);
                     if (child_ends.empty()) {
                        return;
                     }

                     const auto append_children = [&](std::size_t child_end) {
                        if (child_end > end) {
                           return;
                        }
                        const auto& child_nodes = build_rule(symbol.value, position, child_end);
                        for (const auto& child: child_nodes) {
                           children.emplace_back(child);
                           enumerate(symbol_index + 1, child_end);
                           children.pop_back();
                        }
                     };
                     if (span_order == forest_span_order::ascending) {
                        for (const auto child_end: child_ends) {
                           if (child_end > end) {
                              break;
                           }
                           append_children(child_end);
                        }
                     } else {
                        for (auto child_it = child_ends.rbegin(); child_it != child_ends.rend(); ++child_it) {
                           append_children(*child_it);
                        }
                     }
                     return;
                  }

                      auto match = match_terminal(prepared.tokens, position, symbol, grammar);
                  if (!match.has_value() || match->end > end) {
                     return;
                  }
                  children.emplace_back(match->text);
                  enumerate(symbol_index + 1, match->end);
                  children.pop_back();
               };

               enumerate(0, start);
               production_in_progress.erase(key);
               return production_cache.emplace(key, std::move(nodes)).first->second;
            };

            auto forest = std::vector<parse_node_ptr>{};
            for (auto end: root_ends) {
               const auto& trees = build_rule(root_rule, 0, end);
               forest.insert(forest.end(), trees.begin(), trees.end());
            }

            return forest;
         }

         constexpr auto impossible_recovery_cost = std::numeric_limits<std::size_t>::max() / 4;

         struct recovered_tree_candidate {
            parse_node_ptr tree;
            std::size_t cost = impossible_recovery_cost;
            std::string signature;
            bool partial = false;
         };

         struct recovered_suffix_candidate {
            std::vector<parse_value> children;
            std::vector<node_damage> damage;
            std::size_t cost = impossible_recovery_cost;
            std::string signature;
            bool partial = false;
         };

         void keep_recovered_tree_candidate(std::vector<recovered_tree_candidate>& candidates,
                                            recovered_tree_candidate candidate) {
            if (candidate.tree == nullptr || candidate.cost >= impossible_recovery_cost) {
               return;
            }
            if (candidates.empty() || candidate.cost < candidates.front().cost) {
               candidates.clear();
               candidates.push_back(std::move(candidate));
            }
         }

         void keep_recovered_suffix_candidate(std::vector<recovered_suffix_candidate>& candidates,
                                              recovered_suffix_candidate candidate) {
            if (candidate.cost >= impossible_recovery_cost) {
               return;
            }
            if (candidates.empty() || candidate.cost < candidates.front().cost) {
               candidates.clear();
               candidates.push_back(std::move(candidate));
            }
         }

         auto literal_signature(const matched_string& text, bool virtual_match) -> std::string {
            return std::string{virtual_match ? "V" : "T"} + "(" + text.text + "@" +
                   std::to_string(text.range.begin.offset) + ":" + std::to_string(text.range.end.offset) + ")";
         }

         auto damage_signature(const node_damage& damage) -> std::string {
            return "D(" + std::to_string(damage.range.begin.offset) + ":" + std::to_string(damage.range.end.offset) +
                   ":" + std::string{cpf::to_string(damage.reason)} + ":" + damage.detail + ":" + damage.message +
                   ")";
         }

         auto production_children_range(std::string_view input, const std::vector<lexed_token>& tokens,
                                        std::size_t start, std::size_t end, const std::vector<parse_value>& children,
                                        const std::vector<node_damage>& damage) -> source_range {
            auto range = make_source_range(input, token_position_offset(input, tokens, start),
                                           token_position_offset(input, tokens, end));
            auto have_component = false;
            for (const auto& child: children) {
               auto child_range = range_of(child);
               if (!have_component) {
                  range = child_range;
                  have_component = true;
               } else {
                  extend_range(range, child_range);
               }
            }
            for (const auto& entry: damage) {
               if (!have_component) {
                  range = entry.range;
                  have_component = true;
               } else {
                  extend_range(range, entry.range);
               }
            }
            return range;
         }

         auto recover_full_input(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                 const prepared_parse& prepared) -> parse_forest {
            parse_forest result;
            result.error = prepared.error;
            if (!prepared.has_root_production) {
               return result;
            }

            auto rule_cache = std::unordered_map<span_key, std::vector<recovered_tree_candidate>, span_key_hash>{};
            auto step_cache = std::unordered_map<production_step_key, std::vector<recovered_suffix_candidate>,
                                                 production_step_key_hash>{};
            auto rule_in_progress = std::unordered_set<span_key, span_key_hash>{};
            auto step_in_progress = std::unordered_set<production_step_key, production_step_key_hash>{};

            std::function<const std::vector<recovered_tree_candidate>&(std::size_t, std::size_t, std::size_t)> build_rule;
            std::function<const std::vector<recovered_suffix_candidate>&(std::size_t, std::size_t, std::size_t,
                                                                         std::size_t)>
                  build_step;

            build_step = [&](std::size_t production_index, std::size_t symbol_index, std::size_t position,
                             std::size_t end) -> const std::vector<recovered_suffix_candidate>& {
               static const auto empty = std::vector<recovered_suffix_candidate>{};
               auto key = production_step_key{production_index, symbol_index, position, end};
               if (step_in_progress.contains(key)) {
                  return empty;
               }
               if (auto cache = step_cache.find(key); cache != step_cache.end()) {
                  return cache->second;
               }

               step_in_progress.insert(key);
               const auto& production = grammar.productions[production_index];
               auto candidates = std::vector<recovered_suffix_candidate>{};

               if (symbol_index == production.symbol_count) {
                    auto trailing = position;
                  if (trailing == end) {
                     keep_recovered_suffix_candidate(candidates,
                                                     recovered_suffix_candidate{{}, {}, 0, "", false});
                  }
                  if (trailing < end) {
                       const auto ignored_end = ignored_symbol_end(position, end);
                     if (ignored_end > trailing) {
                        const auto& suffixes = build_step(production_index, symbol_index, ignored_end, end);
                        for (const auto& suffix: suffixes) {
                           auto damage = make_ignored_damage(input, token_begin_offset(prepared.tokens, trailing),
                                                             token_end_offset(prepared.tokens, ignored_end - 1),
                                                             "<end of input>");
                           auto candidate = recovered_suffix_candidate{suffix.children,
                                                                       suffix.damage,
                                                                       suffix.cost + 1,
                                                                       damage_signature(damage) + suffix.signature,
                                                                       true};
                           candidate.damage.insert(candidate.damage.begin(), std::move(damage));
                           keep_recovered_suffix_candidate(candidates, std::move(candidate));
                        }
                     }
                  }

                  step_in_progress.erase(key);
                  return step_cache.emplace(key, std::move(candidates)).first->second;
               }

                auto skipped = position;
               if (skipped < end) {
                   const auto ignored_end = ignored_symbol_end(position, end);
                  if (ignored_end > skipped) {
                     const auto& suffixes = build_step(production_index, symbol_index, ignored_end, end);
                     for (const auto& suffix: suffixes) {
                         auto damage = make_ignored_damage(
                               input, token_begin_offset(prepared.tokens, skipped),
                               token_end_offset(prepared.tokens, ignored_end - 1),
                               describe_expected_symbol(production.symbols[symbol_index], grammar));
                        auto candidate = recovered_suffix_candidate{suffix.children,
                                                                    suffix.damage,
                                                                    suffix.cost + 1,
                                                                    damage_signature(damage) + suffix.signature,
                                                                    true};
                        candidate.damage.insert(candidate.damage.begin(), std::move(damage));
                        keep_recovered_suffix_candidate(candidates, std::move(candidate));
                     }
                  }
               }

               const auto& symbol = production.symbols[symbol_index];
               if (is_positive_lookahead(symbol) || is_negative_lookahead(symbol)) {
                  auto predicate_matches_cache = predicate_context{};
                  const auto matched = predicate_matches(input, prepared.tokens, grammar, symbol, position,
                                                         predicate_matches_cache);
                  if ((is_positive_lookahead(symbol) && matched) ||
                      (is_negative_lookahead(symbol) && !matched)) {
                     const auto& suffixes = build_step(production_index, symbol_index + 1, position, end);
                     for (const auto& suffix: suffixes) {
                        keep_recovered_suffix_candidate(candidates, suffix);
                     }
                  }
                  step_in_progress.erase(key);
                  return step_cache.emplace(key, std::move(candidates)).first->second;
               }
               if (symbol.kind == parser_symbol_kind::nonterminal) {
                  for (std::size_t child_end = position; child_end <= end; ++child_end) {
                     const auto& child_candidates = build_rule(symbol.value, position, child_end);
                     if (child_candidates.empty()) {
                        continue;
                     }
                     const auto& suffixes = build_step(production_index, symbol_index + 1, child_end, end);
                     if (suffixes.empty()) {
                        continue;
                     }
                     for (const auto& child: child_candidates) {
                        for (const auto& suffix: suffixes) {
                           auto candidate = recovered_suffix_candidate{suffix.children,
                                                                       suffix.damage,
                                                                       child.cost + suffix.cost,
                                                                       "N(" + child.signature + ")" + suffix.signature,
                                                                       child.partial || suffix.partial};
                           candidate.children.insert(candidate.children.begin(), child.tree);
                           keep_recovered_suffix_candidate(candidates, std::move(candidate));
                        }
                     }
                  }
               } else {
                   if (auto match = match_terminal(prepared.tokens, position, symbol, grammar); match.has_value() &&
                       match->end <= end) {
                     const auto& suffixes = build_step(production_index, symbol_index + 1, match->end, end);
                     for (const auto& suffix: suffixes) {
                        auto candidate = recovered_suffix_candidate{suffix.children,
                                                                    suffix.damage,
                                                                    suffix.cost,
                                                                    literal_signature(match->text, false) +
                                                                          suffix.signature,
                                                                    suffix.partial};
                        candidate.children.insert(candidate.children.begin(), match->text);
                        keep_recovered_suffix_candidate(candidates, std::move(candidate));
                     }
                  }

                   const auto& terminal = grammar.token_symbols[symbol.value];
                   if (terminal.kind == lexer_symbol_kind::literal) {
                     const auto& suffixes = build_step(production_index, symbol_index + 1, position, end);
                     for (const auto& suffix: suffixes) {
                         auto inserted = virtual_terminal(input, prepared.tokens, position, terminal.text);
                         auto damage = make_inserted_damage(input, inserted.range.begin.offset, quoted(terminal.text));
                        auto candidate = recovered_suffix_candidate{suffix.children,
                                                                    suffix.damage,
                                                                    suffix.cost + 1,
                                                                    literal_signature(inserted, true) +
                                                                          damage_signature(damage) + suffix.signature,
                                                                    true};
                        candidate.children.insert(candidate.children.begin(), inserted);
                        candidate.damage.insert(candidate.damage.begin(), std::move(damage));
                        keep_recovered_suffix_candidate(candidates, std::move(candidate));
                     }
                  }
               }

               step_in_progress.erase(key);
               return step_cache.emplace(key, std::move(candidates)).first->second;
            };

            build_rule = [&](std::size_t rule, std::size_t start, std::size_t end)
                  -> const std::vector<recovered_tree_candidate>& {
               static const auto empty = std::vector<recovered_tree_candidate>{};
               auto key = span_key{rule, start, end};
               if (rule_in_progress.contains(key)) {
                  return empty;
               }
               if (auto cache = rule_cache.find(key); cache != rule_cache.end()) {
                  return cache->second;
               }

               rule_in_progress.insert(key);
               auto nodes = std::vector<recovered_tree_candidate>{};
               const auto rule_begin = grammar.rule_production_offsets[rule];
               const auto rule_count = grammar.rule_production_counts[rule];
               for (std::size_t offset = 0; offset < rule_count; ++offset) {
                  const auto production_index = grammar.rule_production_indices[rule_begin + offset];
                  const auto& suffixes = build_step(production_index, 0, start, end);
                  for (const auto& suffix: suffixes) {
                      auto range = production_children_range(input, prepared.tokens, start, end, suffix.children,
                                                             suffix.damage);
                     auto partial = suffix.partial || !suffix.damage.empty();
                     auto tree = std::make_shared<parse_node>(parse_node{rule,
                                                                         production_index,
                                                                         start,
                                                                         end,
                                                                         range,
                                                                         suffix.children,
                                                                         suffix.damage,
                                                                         partial});
                     keep_recovered_tree_candidate(nodes, recovered_tree_candidate{tree,
                                                                                   suffix.cost,
                                                                                   std::to_string(production_index) +
                                                                                         "{" + suffix.signature + "}",
                                                                                   partial});
                  }
               }

               rule_in_progress.erase(key);
               return rule_cache.emplace(key, std::move(nodes)).first->second;
            };

            const auto& recovered = build_rule(root_rule, 0, prepared.tokens.size());
            if (recovered.empty() || recovered.front().cost == 0) {
               return result;
            }

            result.success = true;
            result.partial = true;
            result.forest.reserve(recovered.size());
            result.tree_partial.reserve(recovered.size());
            result.tree_damage.resize(recovered.size());
            for (const auto& candidate: recovered) {
               result.forest.push_back(candidate.tree);
               result.tree_partial.push_back(candidate.partial);
            }
            return result;
         }
      } // namespace

      void error_tracker::record(std::size_t position, std::string expected, std::string note) {
         if (position > furthest_) {
            furthest_ = position;
            expected_.clear();
            notes_.clear();
         }
         if (position == furthest_ && !expected.empty()) {
            expected_.insert(std::move(expected));
         }
         if (position == furthest_ && !note.empty()) {
            notes_.insert(std::move(note));
         }
      }

      auto error_tracker::build(std::string_view input) const -> parse_error {
         parse_error error;
         auto location = locate(input, furthest_);
         error.position = location;
         error.expected.assign(expected_.begin(), expected_.end());
         error.found = found_token(input, furthest_);
         error.notes.assign(notes_.begin(), notes_.end());
         finalize(error);
         return error;
      }

      void error_tracker::finalize(parse_error& error) {
         error.message = "Parse error at line " + std::to_string(error.position.line) + ", column " +
                         std::to_string(error.position.column) + ": expected " + join_expected(error.expected) +
                         " but found " + describe_found(error.found);
         if (!error.notes.empty()) {
            error.message += "\nNotes:";
            for (const auto& note: error.notes) {
               error.message += "\n  - " + note;
            }
         }
      }

      auto quoted(std::string_view value) -> std::string { return std::string{"\""} + escape_string(value) + "\""; }

      void append_unique(std::vector<std::string>& values, std::string value) {
         if (value.empty()) {
            return;
         }
         if (std::find(values.begin(), values.end(), value) == values.end()) {
            values.push_back(std::move(value));
         }
      }

      void merge_parse_error(parse_error& target, const parse_error& candidate) {
         if (candidate.position.line > target.position.line ||
             (candidate.position.line == target.position.line && candidate.position.column > target.position.column)) {
            target = candidate;
            return;
         }
         if (candidate.position.line < target.position.line ||
             (candidate.position.line == target.position.line && candidate.position.column < target.position.column)) {
            return;
         }

         for (const auto& expected: candidate.expected) {
            append_unique(target.expected, expected);
         }
         for (const auto& note: candidate.notes) {
            append_unique(target.notes, note);
         }
         error_tracker::finalize(target);
      }

      auto make_ambiguity_error(std::string_view rule_name) -> parse_error {
         parse_error error;
         error.expected.emplace_back("unambiguous parse");
         error.found.kind = parse_error_found_kind::ambiguous_parse;
         error.notes.push_back("multiple valid derivations were detected while parsing rule '" +
                               std::string{rule_name} + "'");
         error_tracker::finalize(error);
         return error;
      }

      auto node_child_at(const parse_node_ptr& tree, std::size_t index) -> parse_node_ptr {
         if (index >= tree->children.size()) {
            throw std::runtime_error{"Parse tree missing node child"};
         }
         const auto* child = std::get_if<parse_node_ptr>(&tree->children[index]);
         if (child == nullptr || *child == nullptr) {
            throw std::runtime_error{"Parse tree child is not a node"};
         }
         return *child;
      }

      auto matched_child_at(const parse_node_ptr& tree, std::size_t index) -> matched_string {
         if (index >= tree->children.size()) {
            throw std::runtime_error{"Parse tree missing terminal child"};
         }
         const auto* child = std::get_if<matched_string>(&tree->children[index]);
         if (child == nullptr) {
            throw std::runtime_error{"Parse tree child is not a terminal"};
         }
         return *child;
      }

      void append_matched_tree_text(const parse_node_ptr& tree, std::string& text) {
         for (const auto& child: tree->children) {
            if (const auto* matched = std::get_if<matched_string>(&child); matched != nullptr) {
               text += matched->text;
               continue;
            }
            const auto* node = std::get_if<parse_node_ptr>(&child);
            if (node == nullptr || *node == nullptr) {
               throw std::runtime_error{"Parse tree child is not materializable as token text"};
            }
            append_matched_tree_text(*node, text);
         }
      }

      auto matched_tree_at(const parse_node_ptr& tree) -> matched_string {
         matched_string matched;
         matched.range = tree->range;
         append_matched_tree_text(tree, matched.text);
         return matched;
      }

      auto production_metadata_of(const parse_node_ptr& tree, const model_spec& model) -> const production_model_metadata* {
         if (tree->production >= model.production_metadata_count || model.production_metadata == nullptr) {
            return nullptr;
         }
         return &model.production_metadata[tree->production];
      }

      auto production_definition_of(const parse_node_ptr& tree, const model_spec& model) -> std::size_t {
         if (const auto* metadata = production_metadata_of(tree, model); metadata != nullptr) {
            return metadata->definition;
         }
         return 0;
      }

      auto parse_tree_is_synthetic(const parse_node_ptr& tree, const model_spec& model) -> bool {
         if (const auto* metadata = production_metadata_of(tree, model); metadata != nullptr) {
            return metadata->synthetic;
         }
         return false;
      }

      auto parse_tree_rule_id(const parse_node_ptr& tree, const model_spec& model) -> std::size_t {
         const auto* metadata = production_metadata_of(tree, model);
         if (metadata == nullptr || !metadata->has_source_rule) {
            throw std::runtime_error{"Unknown parse production rule id"};
         }
         return metadata->rule_id;
      }

      auto parse_tree_rule_name(const parse_node_ptr& tree, const model_spec& model) -> std::string_view {
         const auto* metadata = production_metadata_of(tree, model);
         if (metadata == nullptr || !metadata->has_source_rule) {
            throw std::runtime_error{"Unknown parse production rule name"};
         }
         return metadata->rule_name;
      }

      auto precedence_of_tree(const parse_node_ptr& tree, const model_spec& model) -> int {
         const auto* metadata = production_metadata_of(tree, model);
         if (metadata == nullptr) {
            return 0;
         }
         if (metadata->precedence_passthrough) {
            if (tree->children.empty()) {
               return 0;
            }
            return precedence_of_tree(node_child_at(tree, 0), model);
         }
         return metadata->precedence;
      }

      auto validate_child_tree(const parse_node_ptr& child, int precedence, bool left_associative, bool is_left_child,
                               const model_spec& model) -> bool {
         const auto child_precedence = precedence_of_tree(child, model);
         if (child_precedence == 0) {
            return true;
         }
         if (child_precedence < precedence) {
            return false;
         }
         if (child_precedence > precedence) {
            return true;
         }
         return is_left_child ? left_associative : !left_associative;
      }

      auto validate_parse_tree(const parse_node_ptr& tree, const model_spec& model) -> bool {
         for (const auto& child: tree->children) {
            if (const auto* node = std::get_if<parse_node_ptr>(&child); node != nullptr && *node != nullptr &&
                !validate_parse_tree(*node, model)) {
               return false;
            }
         }

         const auto* metadata = production_metadata_of(tree, model);
         if (metadata == nullptr || metadata->validation_constraints == nullptr ||
             metadata->validation_constraint_count == 0) {
            return true;
         }
         for (std::size_t index = 0; index < metadata->validation_constraint_count; ++index) {
            const auto& constraint = metadata->validation_constraints[index];
            if (!validate_child_tree(node_child_at(tree, constraint.left_child_index), constraint.precedence,
                                     constraint.left_associative, true, model)) {
               return false;
            }
            if (!validate_child_tree(node_child_at(tree, constraint.right_child_index), constraint.precedence,
                                     constraint.left_associative, false, model)) {
               return false;
            }
         }
         return true;
      }

      auto requires_tree_validation(const model_spec& model) -> bool {
         for (std::size_t index = 0; index < model.production_metadata_count; ++index) {
            if (model.production_metadata[index].validation_constraint_count != 0) {
               return true;
            }
         }
         return false;
      }

      void append_cst_children(const parse_node_ptr& tree, const model_spec& model, std::vector<cst_child>& children) {
         for (const auto& child: tree->children) {
            if (const auto* matched = std::get_if<matched_string>(&child); matched != nullptr) {
               children.emplace_back(*matched);
               continue;
            }
            const auto* node = std::get_if<parse_node_ptr>(&child);
            if (node == nullptr || *node == nullptr) {
               throw std::runtime_error{"Parse tree child is not a node"};
            }
            if (parse_tree_is_synthetic(*node, model)) {
               append_cst_children(*node, model, children);
               continue;
            }
            children.emplace_back(build_cst_node(*node, model));
         }
      }

      auto build_cst_node(const parse_node_ptr& tree, const model_spec& model) -> std::unique_ptr<cst_node> {
         auto node = std::make_unique<cst_node>();
         node->production_index = production_definition_of(tree, model);
         node->range = tree->range;
         node->rule = parse_tree_rule_id(tree, model);
         node->rule_name = std::string{parse_tree_rule_name(tree, model)};
         for (const auto& damage: tree->damage) {
            add_damage(*node, damage);
         }
         append_cst_children(tree, model, node->children);
         return node;
      }

      auto filtered_parse_error(std::string_view rule_name) -> parse_error {
         auto error = parse_error{};
         error.expected.push_back("valid parse tree");
         error.found.kind = parse_error_found_kind::filtered_parse;
         error.notes.push_back(std::string{R"(completed Earley parses for rule ')"} + std::string{rule_name} +
                               R"(' were rejected by precedence/associativity constraints)");
         error_tracker::finalize(error);
         return error;
      }

      auto repaired_input_of(const parse_node_ptr& tree, std::string_view input, const grammar_spec& grammar)
            -> std::optional<std::string> {
         auto plan = repaired_input_plan{};
         if (!collect_repaired_input_plan(tree, input, plan) || !plan.saw_damage) {
            return std::nullopt;
         }
          if (!validate_repaired_input_plan(input, plan, grammar)) {
            return std::nullopt;
         }
         return apply_repaired_input_plan(input, std::move(plan));
      }

      auto lex_input(std::string_view input, const grammar_spec& grammar) -> token_sequence {
         return tokenize_input(input, grammar);
      }

       auto earley_parse(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                         bool allow_partial, forest_span_order span_order) -> parse_forest {
          return finish_earley_parse(input, grammar, root_rule, prepare_earley_parse(input, grammar, root_rule),
                                     allow_partial, span_order);
      }

       auto earley_parse(const token_sequence& tokens, const grammar_spec& grammar, std::size_t root_rule,
                         bool allow_partial, forest_span_order span_order) -> parse_forest {
         auto predicate_matches_cache = predicate_context{};
         return finish_earley_parse(tokens.input, grammar, root_rule,
                                    prepare_earley_parse(tokens.input, tokens.tokens, grammar, root_rule, 0,
                                                         predicate_matches_cache),
                                     allow_partial, span_order);
      }

      auto earley_recognize(std::string_view input, const grammar_spec& grammar, std::size_t root_rule)
            -> recognize_result {
         auto tokenized = tokenize_input(input, grammar);
         auto predicate_matches_cache = predicate_context{};
         if (recognize_fast(tokenized.input, tokenized.tokens, grammar, root_rule, 0, predicate_matches_cache)) {
            auto result = recognize_result{};
            result.success = true;
            return result;
         }
         return finish_earley_recognize(root_rule, prepare_earley_parse(input, grammar, root_rule, true));
      }

      auto earley_recognize(const token_sequence& tokens, const grammar_spec& grammar,
                            std::size_t root_rule) -> recognize_result {
         auto predicate_matches_cache = predicate_context{};
         if (recognize_fast(tokens.input, tokens.tokens, grammar, root_rule, 0, predicate_matches_cache)) {
            auto result = recognize_result{};
            result.success = true;
            return result;
         }

         predicate_matches_cache = {};
         return finish_earley_recognize(root_rule,
                                        prepare_earley_parse(tokens.input, tokens.tokens, grammar, root_rule, 0,
                                                             predicate_matches_cache, true));
      }

      auto earley_inspect(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                          std::size_t ambiguity_limit) -> inspect_result {
         inspect_result result;
         auto prepared = prepare_earley_parse(input, grammar, root_rule);
         if (!prepared.has_root_production) {
            result.error = prepared.error;
            return result;
         }

         std::size_t accepted_end = prepared.tokens.size() + 1;
         for (std::size_t end = 0; end < prepared.chart_size; ++end) {
             if (end != prepared.tokens.size()) {
               continue;
            }
            if (rule_completed_at(prepared, root_rule, 0, end)) {
               accepted_end = end;
               result.success = true;
               break;
            }
         }

         if (!result.success) {
            result.error = prepared.error;
            return result;
         }

         std::unordered_map<span_key, std::size_t, span_key_hash> rule_count_cache;
         std::unordered_map<span_key, std::size_t, span_key_hash> production_count_cache;
          std::unordered_set<span_key, span_key_hash> rule_in_progress;
         std::unordered_set<span_key, span_key_hash> production_in_progress;

         std::function<std::size_t(std::size_t, std::size_t, std::size_t)> count_rule;
         std::function<std::size_t(std::size_t, std::size_t, std::size_t)> count_production;

         count_rule = [&](std::size_t rule, std::size_t start, std::size_t end) -> std::size_t {
            auto key = span_key{rule, start, end};
             if (rule_in_progress.contains(key)) {
                return 0;
             }
            if (auto cache = rule_count_cache.find(key); cache != rule_count_cache.end()) {
               return cache->second;
            }

             rule_in_progress.insert(key);
            auto total = std::size_t{0};
            if (auto completed = prepared.completed_rules.find(key); completed != prepared.completed_rules.end()) {
               for (const auto production_index: completed->second) {
                  total = capped_add(total, count_production(production_index, start, end), ambiguity_limit);
                  if (total >= ambiguity_limit) {
                     break;
                  }
               }
            }
             rule_in_progress.erase(key);
            rule_count_cache.emplace(key, total);
            return total;
         };

         count_production = [&](std::size_t production_index, std::size_t start, std::size_t end) -> std::size_t {
            auto key = span_key{production_index, start, end};
            if (production_in_progress.contains(key) || !prepared.completed_productions.contains(key)) {
               return 0;
            }
            if (auto cache = production_count_cache.find(key); cache != production_count_cache.end()) {
               return cache->second;
            }

            production_in_progress.insert(key);
            const auto& production = grammar.productions[production_index];
            std::function<std::size_t(std::size_t, std::size_t)> enumerate = [&](std::size_t symbol_index,
                                                                                 std::size_t position) -> std::size_t {
               if (symbol_index == production.symbol_count) {
                  return position == end ? 1 : 0;
               }

               const auto& symbol = production.symbols[symbol_index];
               if (is_positive_lookahead(symbol) || is_negative_lookahead(symbol)) {
                  auto predicate_matches_cache = predicate_context{};
                  const auto matched = predicate_matches(input, prepared.tokens, grammar, symbol, position,
                                                         predicate_matches_cache);
                  if ((is_positive_lookahead(symbol) && matched) ||
                      (is_negative_lookahead(symbol) && !matched)) {
                     return enumerate(symbol_index + 1, position);
                  }
                  return 0;
               }
               if (symbol.kind == parser_symbol_kind::nonterminal) {
                  const auto& child_ends = rule_end_list(prepared, symbol.value, position);
                  if (child_ends.empty()) {
                     return 0;
                  }

                  auto total = std::size_t{0};
                  for (const auto child_end: child_ends) {
                     if (child_end > end) {
                        break;
                     }
                     auto left = count_rule(symbol.value, position, child_end);
                     if (left == 0) {
                        continue;
                     }
                     auto right = enumerate(symbol_index + 1, child_end);
                     total = capped_add(total, capped_mul(left, right, ambiguity_limit), ambiguity_limit);
                     if (total >= ambiguity_limit) {
                        break;
                     }
                  }
                  return total;
               }

                auto match = match_terminal(prepared.tokens, position, symbol, grammar);
               if (!match.has_value() || match->end > end) {
                  return 0;
               }
               return enumerate(symbol_index + 1, match->end);
            };

            auto total = enumerate(0, start);
            production_in_progress.erase(key);
            production_count_cache.emplace(key, total);
            return total;
         };

         auto derivation_count = count_rule(root_rule, 0, accepted_end);
         result.ambiguous = derivation_count >= ambiguity_limit;
         return result;
      }
   } // namespace detail
} // namespace cpf



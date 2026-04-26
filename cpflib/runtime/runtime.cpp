#include "runtime.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace cpf {
   namespace detail {
      namespace {
         using source_location = source_position;

         auto locate(std::string_view input, std::size_t offset) -> source_location {
            source_location location;
            location.offset = std::min(offset, input.size());
            auto limit = std::min(offset, input.size());
            for (std::size_t i = 0; i < limit; ++i) {
               if (input[i] == '\n') {
                  ++location.line;
                  location.column = 1;
               } else {
                  ++location.column;
               }
            }
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
            for (std::size_t index = 0; index < grammar.skip_symbol_count; ++index) {
               auto next = position;
               std::string capture;
               if (!try_lexer_symbol_at(input, next, grammar.skip_symbols[index], &capture) || next == position) {
                  continue;
               }
               auto candidate = raw_lexer_match{index, next, grammar.skip_symbols[index].precedence,
                                                grammar.skip_symbols[index].kind,
                                                std::move(capture), true};
               if (better_raw_lexer_match(candidate, best, position)) {
                  best = std::move(candidate);
               }
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

         struct terminal_match {
            std::size_t end = 0;
            matched_string text;
         };

         struct predicate_context {
            std::unordered_map<predicate_query_key, bool, predicate_query_key_hash> matches;
            std::unordered_set<predicate_query_key, predicate_query_key_hash> in_progress;
         };

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

            for (std::size_t index = 0; index < grammar.token_symbol_count; ++index) {
               consider(false, index, grammar.token_symbols[index]);
            }
            for (std::size_t index = 0; index < grammar.skip_symbol_count; ++index) {
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
            std::vector<chart_item_key> items;
            std::unordered_set<chart_item_key, chart_item_key_hash> indices;

            auto add(std::size_t production, std::size_t dot, std::size_t start) -> bool {
               auto key = chart_item_key{production, dot, start};
               if (!indices.insert(key).second) {
                  return false;
               }
               items.push_back(key);
               return true;
            }
         };

         struct prepared_parse {
            bool has_root_production = false;
            std::vector<lexed_token> tokens;
            std::vector<chart_column> chart;
            std::unordered_map<span_key, std::vector<std::size_t>, span_key_hash> completed_rules;
            std::unordered_set<span_key, span_key_hash> completed_productions;
            std::unordered_map<rule_start_key, std::vector<std::size_t>, rule_start_key_hash> rule_ends;
            parse_error error;
         };

         auto predicate_matches(std::string_view input, const std::vector<lexed_token>& tokens,
                                const grammar_spec& grammar, const parser_symbol& symbol, std::size_t position,
                                predicate_context& context) -> bool;

         auto collect_root_ends(std::string_view input, const prepared_parse& prepared, const grammar_spec& grammar,
                                std::size_t root_rule, bool require_full_input) -> std::vector<std::size_t>;
         auto build_root_forest(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                const prepared_parse& prepared, const std::vector<std::size_t>& root_ends)
               -> std::vector<parse_node_ptr>;
         auto recover_full_input(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                 const prepared_parse& prepared) -> parse_forest;

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
                                   predicate_context& predicate_matches_cache) -> prepared_parse {
            prepared_parse prepared;
            error_tracker tracker;
            prepared.tokens = std::move(tokens);
            prepared.chart.resize(prepared.tokens.size() + 1);

            const auto root_begin = grammar.rule_production_offsets[root_rule];
            const auto root_count = grammar.rule_production_counts[root_rule];
            for (std::size_t offset = 0; offset < root_count; ++offset) {
               prepared.chart[start_position].add(grammar.rule_production_indices[root_begin + offset], 0, start_position);
               prepared.has_root_production = true;
            }

            if (!prepared.has_root_production) {
               prepared.error.message = "Parse error: root rule has no productions";
               return prepared;
            }

            for (std::size_t position = start_position; position < prepared.chart.size(); ++position) {
               for (std::size_t index = 0; index < prepared.chart[position].items.size(); ++index) {
                  const auto [production_index, dot, start] = prepared.chart[position].items[index];
                  const auto& production = grammar.productions[production_index];

                  if (dot == production.symbol_count) {
                     for (std::size_t parent_item_index = 0; parent_item_index < prepared.chart[start].items.size();
                          ++parent_item_index) {
                        const auto [parent_index, parent_dot, parent_start] = prepared.chart[start].items[parent_item_index];
                        const auto& parent = grammar.productions[parent_index];
                        if (parent_dot >= parent.symbol_count) {
                           continue;
                        }
                        const auto& next = parent.symbols[parent_dot];
                        if (next.kind == parser_symbol_kind::nonterminal && next.value == production.lhs) {
                           prepared.chart[position].add(parent_index, parent_dot + 1, parent_start);
                        }
                     }
                     continue;
                  }

                  const auto& next = production.symbols[dot];
                  if (next.kind == parser_symbol_kind::nonterminal) {
                     for (std::size_t candidate = 0; candidate < grammar.production_count; ++candidate) {
                        if (grammar.productions[candidate].lhs == next.value) {
                           prepared.chart[position].add(candidate, 0, position);
                        }
                     }
                     continue;
                  }

                  if (is_positive_lookahead(next) || is_negative_lookahead(next)) {
                     const auto matched = predicate_matches(input, prepared.tokens, grammar, next, position,
                                                            predicate_matches_cache);
                     const auto success = is_positive_lookahead(next) ? matched : !matched;
                     if (!success) {
                        tracker.record(token_position_offset(input, prepared.tokens, position),
                                       describe_expected_symbol(next, grammar), describe_progress(production, dot));
                        continue;
                     }
                     prepared.chart[position].add(production_index, dot + 1, start);
                     continue;
                  }

                  auto match = match_terminal(prepared.tokens, position, next, grammar);
                  if (!match.has_value()) {
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
                  prepared.chart[match->end].add(production_index, dot + 1, start);
               }
            }

            for (std::size_t end = 0; end < prepared.chart.size(); ++end) {
               for (const auto& item: prepared.chart[end].items) {
                  const auto [production_index, dot, start] = item;
                  const auto& production = grammar.productions[production_index];
                  if (dot != production.symbol_count) {
                     continue;
                  }
                  prepared.completed_productions.insert(span_key{production_index, start, end});
                  prepared.completed_rules[span_key{production.lhs, start, end}].push_back(production_index);
                  prepared.rule_ends[rule_start_key{production.lhs, start}].push_back(end);
               }
            }

            for (auto& [key, ends]: prepared.rule_ends) {
               std::sort(ends.begin(), ends.end());
               ends.erase(std::unique(ends.begin(), ends.end()), ends.end());
               (void) key;
            }

             auto success = false;
             for (std::size_t end = start_position; end < prepared.chart.size(); ++end) {
                if (end != prepared.tokens.size()) {
                  continue;
               }
                if (prepared.completed_rules.contains(span_key{root_rule, start_position, end})) {
                  success = true;
                  break;
               }
            }

            if (!success) {
                for (std::size_t end = start_position; end < prepared.chart.size(); ++end) {
                   auto completed = prepared.completed_rules.find(span_key{root_rule, start_position, end});
                  if (completed != prepared.completed_rules.end()) {
                     auto failure_position = token_position_offset(input, prepared.tokens, end);
                     tracker.record(failure_position, "<end of input>",
                                    "after completing rule '" +
                                          std::string{grammar.productions[completed->second.front()].lhs_name} + "'");
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

            auto prepared = prepare_earley_parse(input, tokens, grammar, target.value, position, context);
            auto matched = false;
            for (std::size_t end = position; end < prepared.chart.size(); ++end) {
               if (prepared.completed_rules.contains(span_key{target.value, position, end})) {
                  matched = true;
                  break;
               }
            }

            context.in_progress.erase(key);
            context.matches.emplace(key, matched);
            return matched;
         }

         auto prepare_earley_parse(std::string_view input, const grammar_spec& grammar, std::size_t root_rule)
               -> prepared_parse {
            auto predicate_matches_cache = predicate_context{};
            return prepare_earley_parse(input, tokenize_input(input, grammar).tokens, grammar, root_rule, 0,
                                        predicate_matches_cache);
         }

         auto finish_earley_parse(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                  prepared_parse prepared, bool allow_partial) -> parse_forest {
            if (!prepared.has_root_production) {
               parse_forest result;
               result.error = prepared.error;
               return result;
            }

            auto full_ends = collect_root_ends(input, prepared, grammar, root_rule, true);
            if (!full_ends.empty()) {
               parse_forest result;
               result.success = true;
               result.forest = build_root_forest(input, grammar, root_rule, prepared, full_ends);
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

            for (std::size_t end = 0; end < prepared.chart.size(); ++end) {
               if (end != prepared.tokens.size()) {
                  continue;
               }

               if (prepared.completed_rules.contains(span_key{root_rule, 0, end})) {
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
            for (std::size_t end = 0; end < prepared.chart.size(); ++end) {
                if (require_full_input && end != prepared.tokens.size()) {
                  continue;
               }
               if (prepared.completed_rules.contains(span_key{root_rule, 0, end})) {
                  ends.push_back(end);
               }
            }
            return ends;
         }

         auto build_root_forest(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                const prepared_parse& prepared, const std::vector<std::size_t>& root_ends)
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
                     auto end_it = prepared.rule_ends.find(rule_start_key{symbol.value, position});
                     if (end_it == prepared.rule_ends.end()) {
                        return;
                     }

                     for (auto child_end: end_it->second) {
                        if (child_end > end) {
                           break;
                        }
                        const auto& child_nodes = build_rule(symbol.value, position, child_end);
                        for (const auto& child: child_nodes) {
                           children.emplace_back(child);
                           enumerate(symbol_index + 1, child_end);
                           children.pop_back();
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
                        bool allow_partial) -> parse_forest {
         return finish_earley_parse(input, grammar, root_rule, prepare_earley_parse(input, grammar, root_rule),
                                    allow_partial);
      }

      auto earley_parse(const token_sequence& tokens, const grammar_spec& grammar, std::size_t root_rule,
                        bool allow_partial) -> parse_forest {
         auto predicate_matches_cache = predicate_context{};
         return finish_earley_parse(tokens.input, grammar, root_rule,
                                    prepare_earley_parse(tokens.input, tokens.tokens, grammar, root_rule, 0,
                                                         predicate_matches_cache),
                                    allow_partial);
      }

      auto earley_recognize(std::string_view input, const grammar_spec& grammar, std::size_t root_rule)
            -> recognize_result {
         return finish_earley_recognize(root_rule, prepare_earley_parse(input, grammar, root_rule));
      }

      auto earley_recognize(const token_sequence& tokens, const grammar_spec& grammar,
                            std::size_t root_rule) -> recognize_result {
         auto predicate_matches_cache = predicate_context{};
         return finish_earley_recognize(root_rule,
                                        prepare_earley_parse(tokens.input, tokens.tokens, grammar, root_rule, 0,
                                                             predicate_matches_cache));
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
         for (std::size_t end = 0; end < prepared.chart.size(); ++end) {
             if (end != prepared.tokens.size()) {
               continue;
            }
            if (prepared.completed_rules.contains(span_key{root_rule, 0, end})) {
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
                  auto end_it = prepared.rule_ends.find(rule_start_key{symbol.value, position});
                  if (end_it == prepared.rule_ends.end()) {
                     return 0;
                  }

                  auto total = std::size_t{0};
                  for (const auto child_end: end_it->second) {
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



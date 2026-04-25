#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <span>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <typeinfo>
#include <map>
#include <variant>
#include <vector>

namespace cpf {
   /// @brief Describes the furthest parse failure that occurred while parsing input.
   struct parse_error {
      /// @brief One-based line number of the parse error.
      std::size_t line = 1;
      /// @brief One-based column number of the parse error.
      std::size_t column = 1;
      /// @brief Tokens or grammar elements that would have been accepted.
      std::vector<std::string> expected;
      /// @brief Token that was found at the point of failure.
      std::string found = "<end of input>";
      /// @brief Additional contextual notes explaining why a token or rule was expected.
      std::vector<std::string> notes;
      /// @brief Human-readable error message.
      std::string message = "Parse error";
   };

   /// @brief Result of parsing an input string into a forest of nodes.
   /// @tparam T Root node type produced by the parse entry point.
   template<typename T>
   struct parse_result {
      /// @brief True when parsing consumed the full input successfully.
      bool success = false;
      /// @brief All parse trees produced for the input.
      std::vector<std::unique_ptr<T>> forest;
      /// @brief Error details when @ref success is false.
      parse_error error;
   };

   /// @brief One position within an input source.
   struct source_position {
      /// @brief Zero-based byte offset in the original input.
      std::size_t offset = 0;
      /// @brief One-based line number.
      std::size_t line = 1;
      /// @brief One-based column number.
      std::size_t column = 1;
   };

   /// @brief Half-open source range covering a parsed match.
   struct source_range {
      source_position begin;
      source_position end;
   };

   /// @brief Captured terminal text together with the source range it matched.
   struct matched_string {
      std::string text;
      source_range range;
   };

   /// @brief Base class for all generated model nodes.
   struct node {
      /// @brief Zero-based production index within the rule that created this node.
      std::size_t definition = 0;
      /// @brief Source range that produced this node.
      source_range range;

      virtual ~node() = default;

      /// @brief Returns the dynamic type of the concrete node.
      /// @return The type information of the concrete node instance.
      [[nodiscard]] virtual const std::type_info& type() const = 0;

   protected:
      /// @brief Clones the concrete node through the base interface.
      /// @return A newly allocated deep copy of the node.
      [[nodiscard]] virtual std::unique_ptr<node> clone_node() const = 0;
   };

   namespace detail {
      using source_location = source_position;

      inline source_location locate(std::string_view input, std::size_t offset) {
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

      inline source_range make_source_range(std::string_view input, std::size_t begin_offset, std::size_t end_offset) {
         return source_range{locate(input, begin_offset), locate(input, end_offset)};
      }

      inline std::string escape_string(std::string_view value) {
         std::string escaped;
         escaped.reserve(value.size() + 4);
         for (char ch : value) {
            switch (ch) {
               case '\\': escaped += "\\\\"; break;
               case '\n': escaped += "\\n"; break;
               case '\r': escaped += "\\r"; break;
               case '\t': escaped += "\\t"; break;
               case '"': escaped += "\\\""; break;
               default: escaped += ch; break;
            }
         }
         return escaped;
      }

      inline std::string quoted(std::string_view value) {
         return std::string{"\""} + escape_string(value) + "\"";
      }

      inline std::string found_token(std::string_view input, std::size_t offset) {
         if (offset >= input.size()) {
            return "<end of input>";
         }
         auto end = offset;
         if (std::isspace(static_cast<unsigned char>(input[end])) != 0) {
            return quoted(std::string_view{input.data() + end, 1});
         }
         while (end < input.size() && std::isspace(static_cast<unsigned char>(input[end])) == 0) {
            ++end;
         }
         return quoted(input.substr(offset, std::min<std::size_t>(end - offset, 16)));
      }

      inline std::string join_expected(const std::vector<std::string>& expected) {
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

      inline void skip_space(std::string_view input, std::size_t& position) {
         while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position])) != 0) {
            ++position;
         }
      }

      class error_tracker {
      public:
         void record(std::size_t position, std::string expected, std::string note = {}) {
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

         [[nodiscard]] parse_error build(std::string_view input) const {
            parse_error error;
            auto location = locate(input, furthest_);
            error.line = location.line;
            error.column = location.column;
            error.expected.assign(expected_.begin(), expected_.end());
            error.found = found_token(input, furthest_);
            error.notes.assign(notes_.begin(), notes_.end());
            finalize(error);
            return error;
         }

         static void finalize(parse_error& error) {
            error.message = "Parse error at line " + std::to_string(error.line)
                          + ", column " + std::to_string(error.column)
                          + ": expected " + join_expected(error.expected)
                          + " but found " + error.found;
            if (!error.notes.empty()) {
               error.message += "\nNotes:";
               for (const auto& note : error.notes) {
                  error.message += "\n  - " + note;
               }
            }
         }

      private:
         std::size_t furthest_ = 0;
         std::set<std::string> expected_;
         std::set<std::string> notes_;
      };

      inline void append_unique(std::vector<std::string>& values, std::string value) {
         if (value.empty()) {
            return;
         }
         if (std::find(values.begin(), values.end(), value) == values.end()) {
            values.push_back(std::move(value));
         }
      }

      inline void merge_parse_error(parse_error& target, const parse_error& candidate) {
         if (candidate.line > target.line || (candidate.line == target.line && candidate.column > target.column)) {
            target = candidate;
            return;
         }
         if (candidate.line < target.line || (candidate.line == target.line && candidate.column < target.column)) {
            return;
         }

         for (const auto& expected : candidate.expected) {
            append_unique(target.expected, expected);
         }
         for (const auto& note : candidate.notes) {
            append_unique(target.notes, note);
         }
         error_tracker::finalize(target);
      }

      inline bool starts_with(std::string_view input, std::size_t position, std::string_view literal) {
         return input.substr(position, literal.size()) == literal;
      }

      inline bool try_literal(std::string_view input, std::size_t& position, std::string_view literal, std::string* capture = nullptr) {
         auto checkpoint = position;
         skip_space(input, checkpoint);
         if (!starts_with(input, checkpoint, literal)) {
            return false;
         }
         position = checkpoint + literal.size();
         if (capture != nullptr) {
            *capture = std::string{literal};
         }
         return true;
      }

      inline bool try_regex(std::string_view input, std::size_t& position, const std::regex& regex, std::string* capture = nullptr) {
         auto checkpoint = position;
         skip_space(input, checkpoint);
         auto begin = input.data() + checkpoint;
         auto end = input.data() + input.size();
         std::match_results<const char*> match;
         if (!std::regex_search(begin, end, match, regex, std::regex_constants::match_continuous)) {
            return false;
         }
         position = checkpoint + static_cast<std::size_t>(match.length());
         if (capture != nullptr) {
            *capture = match.str();
         }
         return true;
      }

      enum class parser_symbol_kind {
         nonterminal,
         literal,
         regex
      };

      struct parser_symbol {
         parser_symbol_kind kind = parser_symbol_kind::literal;
         std::size_t value = 0;
         std::string_view text;
      };

      struct production_spec {
         std::size_t lhs = 0;
         std::string_view lhs_name;
         std::string_view debug_text;
         const parser_symbol* symbols = nullptr;
         std::size_t symbol_count = 0;
      };

      struct grammar_spec {
         const production_spec* productions = nullptr;
         std::size_t production_count = 0;
         std::size_t rule_count = 0;
      };

      struct parse_node;
      using parse_node_ptr = std::shared_ptr<const parse_node>;
      using parse_value = std::variant<matched_string, parse_node_ptr>;

      struct parse_node {
         std::size_t rule = 0;
         std::size_t production = 0;
         std::size_t start = 0;
         std::size_t end = 0;
         source_range range;
         std::vector<parse_value> children;
      };

      struct parse_forest {
         bool success = false;
         std::vector<parse_node_ptr> forest;
         parse_error error;
      };

      struct terminal_match {
         std::size_t end = 0;
         matched_string text;
      };

      inline source_range range_of(const parse_value& value) {
         if (std::holds_alternative<matched_string>(value)) {
            return std::get<matched_string>(value).range;
         }
         return std::get<parse_node_ptr>(value)->range;
      }

      inline std::size_t skip_space_position(std::string_view input, std::size_t position) {
         skip_space(input, position);
         return position;
      }

      inline std::string describe_expected_symbol(const parser_symbol& symbol) {
         if (symbol.kind == parser_symbol_kind::literal) {
            return quoted(symbol.text);
         }
         if (symbol.kind == parser_symbol_kind::regex) {
            return "pattern " + std::string{symbol.text};
         }
         return "rule '" + std::string{symbol.text} + "'";
      }

      inline std::string describe_progress(const production_spec& production, std::size_t dot) {
         return "while parsing rule '" + std::string{production.lhs_name}
              + "' via " + std::string{production.debug_text}
              + " (after symbol " + std::to_string(dot)
              + " of " + std::to_string(production.symbol_count) + ")";
      }

      inline std::optional<terminal_match> match_terminal(std::string_view input, std::size_t position, const parser_symbol& symbol) {
         terminal_match match;
         auto token_start = position;
         skip_space(input, token_start);
         auto current = position;
         if (symbol.kind == parser_symbol_kind::literal) {
            std::string matched_text;
            if (!try_literal(input, current, symbol.text, &matched_text)) {
               return std::nullopt;
            }
            match.end = current;
            match.text.text = std::move(matched_text);
            match.text.range = make_source_range(input, token_start, current);
            return match;
         }
         if (symbol.kind == parser_symbol_kind::regex) {
            const std::regex regex{std::string{symbol.text}};
            std::string matched_text;
            if (!try_regex(input, current, regex, &matched_text)) {
               return std::nullopt;
            }
            match.end = current;
            match.text.text = std::move(matched_text);
            match.text.range = make_source_range(input, token_start, current);
            return match;
         }
         return std::nullopt;
      }

      inline parse_forest earley_parse(std::string_view input, const grammar_spec& grammar, std::size_t root_rule) {
         parse_forest result;
         error_tracker tracker;

         struct chart_column {
            std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> items;
            std::map<std::tuple<std::size_t, std::size_t, std::size_t>, std::size_t> indices;

            bool add(std::size_t production, std::size_t dot, std::size_t start) {
               auto key = std::make_tuple(production, dot, start);
               if (indices.contains(key)) {
                  return false;
               }
               indices.emplace(key, items.size());
               items.push_back(key);
               return true;
            }
         };

         std::vector<chart_column> chart(input.size() + 1);
         auto has_root_production = false;
         for (std::size_t production = 0; production < grammar.production_count; ++production) {
            if (grammar.productions[production].lhs == root_rule) {
               chart.front().add(production, 0, 0);
               has_root_production = true;
            }
         }

         if (!has_root_production) {
            result.error.message = "Parse error: root rule has no productions";
            return result;
         }

         for (std::size_t position = 0; position < chart.size(); ++position) {
            for (std::size_t index = 0; index < chart[position].items.size(); ++index) {
               const auto [production_index, dot, start] = chart[position].items[index];
               const auto& production = grammar.productions[production_index];

               if (dot == production.symbol_count) {
                  for (const auto& parent_item : chart[start].items) {
                     const auto [parent_index, parent_dot, parent_start] = parent_item;
                     const auto& parent = grammar.productions[parent_index];
                     if (parent_dot >= parent.symbol_count) {
                        continue;
                     }
                     const auto& next = parent.symbols[parent_dot];
                     if (next.kind == parser_symbol_kind::nonterminal && next.value == production.lhs) {
                        chart[position].add(parent_index, parent_dot + 1, parent_start);
                     }
                  }
                  continue;
               }

               const auto& next = production.symbols[dot];
               if (next.kind == parser_symbol_kind::nonterminal) {
                  for (std::size_t candidate = 0; candidate < grammar.production_count; ++candidate) {
                     if (grammar.productions[candidate].lhs == next.value) {
                        chart[position].add(candidate, 0, position);
                     }
                  }
                  continue;
               }

               auto match = match_terminal(input, position, next);
               if (!match.has_value()) {
                  tracker.record(skip_space_position(input, position), describe_expected_symbol(next), describe_progress(production, dot));
                  continue;
               }
               chart[match->end].add(production_index, dot + 1, start);
            }
         }

         std::map<std::tuple<std::size_t, std::size_t, std::size_t>, std::vector<std::size_t>> completed_rules;
         std::map<std::tuple<std::size_t, std::size_t, std::size_t>, bool> completed_productions;
         std::map<std::tuple<std::size_t, std::size_t>, std::vector<std::size_t>> rule_ends;

         for (std::size_t end = 0; end < chart.size(); ++end) {
            for (const auto& item : chart[end].items) {
               const auto [production_index, dot, start] = item;
               const auto& production = grammar.productions[production_index];
               if (dot != production.symbol_count) {
                  continue;
               }
               completed_productions[std::make_tuple(production_index, start, end)] = true;
               completed_rules[std::make_tuple(production.lhs, start, end)].push_back(production_index);
               rule_ends[std::make_tuple(production.lhs, start)].push_back(end);
            }
         }

         for (auto& [key, ends] : rule_ends) {
            std::sort(ends.begin(), ends.end());
            ends.erase(std::unique(ends.begin(), ends.end()), ends.end());
         }

         std::map<std::tuple<std::size_t, std::size_t, std::size_t>, std::vector<parse_node_ptr>> rule_cache;
         std::map<std::tuple<std::size_t, std::size_t, std::size_t>, std::vector<parse_node_ptr>> production_cache;
         std::set<std::tuple<std::size_t, std::size_t, std::size_t>> rule_in_progress;
         std::set<std::tuple<std::size_t, std::size_t, std::size_t>> production_in_progress;

         std::function<std::vector<parse_node_ptr>(std::size_t, std::size_t, std::size_t)> build_rule;
         std::function<std::vector<parse_node_ptr>(std::size_t, std::size_t, std::size_t)> build_production;

         build_rule = [&](std::size_t rule, std::size_t start, std::size_t end) -> std::vector<parse_node_ptr> {
            auto key = std::make_tuple(rule, start, end);
            if (auto cache = rule_cache.find(key); cache != rule_cache.end()) {
               return cache->second;
            }
            if (rule_in_progress.contains(key)) {
               return {};
            }

            rule_in_progress.insert(key);
            std::vector<parse_node_ptr> nodes;
            if (auto completed = completed_rules.find(key); completed != completed_rules.end()) {
               for (auto production_index : completed->second) {
                  auto produced = build_production(production_index, start, end);
                  nodes.insert(nodes.end(), produced.begin(), produced.end());
               }
            }
            rule_in_progress.erase(key);
            rule_cache.emplace(key, nodes);
            return nodes;
         };

         build_production = [&](std::size_t production_index, std::size_t start, std::size_t end) -> std::vector<parse_node_ptr> {
            auto key = std::make_tuple(production_index, start, end);
            if (auto cache = production_cache.find(key); cache != production_cache.end()) {
               return cache->second;
            }
            if (production_in_progress.contains(key) || !completed_productions.contains(key)) {
               return {};
            }

            production_in_progress.insert(key);
            const auto& production = grammar.productions[production_index];
            std::vector<parse_node_ptr> nodes;
            std::vector<parse_value> children;

            std::function<void(std::size_t, std::size_t)> enumerate = [&](std::size_t symbol_index, std::size_t position) {
               if (symbol_index == production.symbol_count) {
                  if (position == end) {
                     auto range = make_source_range(input, start, end);
                     if (!children.empty()) {
                        range.begin = range_of(children.front()).begin;
                        range.end = range_of(children.back()).end;
                     }
                     nodes.push_back(std::make_shared<parse_node>(parse_node{production.lhs, production_index, start, end, range, children}));
                  }
                  return;
               }

               const auto& symbol = production.symbols[symbol_index];
               if (symbol.kind == parser_symbol_kind::nonterminal) {
                  auto end_it = rule_ends.find(std::make_tuple(symbol.value, position));
                  if (end_it == rule_ends.end()) {
                     return;
                  }

                  for (auto child_end : end_it->second) {
                     if (child_end > end) {
                        break;
                     }
                     auto child_nodes = build_rule(symbol.value, position, child_end);
                     for (const auto& child : child_nodes) {
                        children.emplace_back(child);
                        enumerate(symbol_index + 1, child_end);
                        children.pop_back();
                     }
                  }
                  return;
               }

               auto match = match_terminal(input, position, symbol);
               if (!match.has_value() || match->end > end) {
                  return;
               }
               children.emplace_back(match->text);
               enumerate(symbol_index + 1, match->end);
               children.pop_back();
            };

            enumerate(0, start);
            production_in_progress.erase(key);
            production_cache.emplace(key, nodes);
            return nodes;
         };

         for (std::size_t end = 0; end < chart.size(); ++end) {
            if (skip_space_position(input, end) != input.size()) {
               continue;
            }
            auto trees = build_rule(root_rule, 0, end);
            result.forest.insert(result.forest.end(), trees.begin(), trees.end());
         }

         if (!result.forest.empty()) {
            result.success = true;
            return result;
         }

         for (std::size_t end = 0; end < chart.size(); ++end) {
            auto completed = completed_rules.find(std::make_tuple(root_rule, 0, end));
            if (completed != completed_rules.end()) {
               auto failure_position = skip_space_position(input, end);
               tracker.record(failure_position, "<end of input>", "after completing rule '" + std::string{grammar.productions[completed->second.front()].lhs_name} + "'");
            }
         }

         result.error = tracker.build(input);
         return result;
      }
   } // namespace detail
} // namespace cpf


#pragma once

#include "api.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cpf {
   namespace detail {
      struct parse_node;
      struct grammar_spec;
      using parse_node_ptr = std::shared_ptr<const parse_node>;

      [[nodiscard]] auto repaired_input_of(const parse_node_ptr& tree, std::string_view input,
                                           const grammar_spec& grammar)
            -> std::optional<std::string>;

      template<typename T> [[nodiscard]] auto opaque_tree_of(const parse_tree<T>& tree) -> std::shared_ptr<const void> {
         return tree.m_state->opaque_tree;
      }

      template<typename T>
      [[nodiscard]] auto pending_damage_of(const parse_tree<T>& tree) -> const std::vector<node_damage>& {
         return tree.m_state->pending_damage;
      }

      class error_tracker {
      public:
         void record(std::size_t position, std::string expected, std::string note = {});
         [[nodiscard]] auto build(std::string_view input) const -> parse_error;
         static void finalize(parse_error& error);

      private:
         std::size_t furthest_ = 0;
         std::set<std::string> expected_;
         std::set<std::string> notes_;
      };

      [[nodiscard]] auto quoted(std::string_view value) -> std::string;
      void append_unique(std::vector<std::string>& values, std::string value);
      void add_damage(node& target, node_damage damage);
      void merge_parse_error(parse_error& target, const parse_error& candidate);
      [[nodiscard]] auto make_ambiguity_error(std::string_view rule_name) -> parse_error;

      enum class parser_symbol_kind { nonterminal, terminal };

      enum class lexer_symbol_kind { literal, regex };

      struct parser_symbol {
         parser_symbol_kind kind = parser_symbol_kind::terminal;
         std::size_t value = 0;
         std::string_view text;
      };

      struct lexer_symbol_spec {
         lexer_symbol_kind kind = lexer_symbol_kind::literal;
         std::string_view text;
         const std::regex* compiled_regex = nullptr;
         std::size_t precedence = 0;
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
         const std::string_view* rule_expected_labels = nullptr;
         const std::size_t* rule_production_indices = nullptr;
         const std::size_t* rule_production_offsets = nullptr;
         const std::size_t* rule_production_counts = nullptr;
         const lexer_symbol_spec* token_symbols = nullptr;
         std::size_t token_symbol_count = 0;
         const lexer_symbol_spec* skip_symbols = nullptr;
         std::size_t skip_symbol_count = 0;
         bool use_default_whitespace = true;
      };

      using parse_value = std::variant<matched_string, parse_node_ptr>;

      struct parse_node {
         std::size_t rule = 0;
         std::size_t production = 0;
         std::size_t start = 0;
         std::size_t end = 0;
         source_range range;
         std::vector<parse_value> children;
         std::vector<node_damage> damage;
         bool partial = false;
      };

      struct parse_forest {
         bool success = false;
         bool partial = false;
         std::vector<parse_node_ptr> forest;
         std::vector<bool> tree_partial;
         std::vector<std::vector<node_damage>> tree_damage;
         parse_error error;
      };

      struct inspect_result {
         bool success = false;
         bool ambiguous = false;
         parse_error error;
      };

      [[nodiscard]] auto lex_input(std::string_view input, const grammar_spec& grammar) -> token_sequence;
      [[nodiscard]] auto earley_parse(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                      bool allow_partial = false) -> parse_forest;
      [[nodiscard]] auto earley_parse(const token_sequence& tokens, const grammar_spec& grammar, std::size_t root_rule,
                                      bool allow_partial = false) -> parse_forest;
      [[nodiscard]] auto earley_recognize(std::string_view input, const grammar_spec& grammar, std::size_t root_rule)
            -> recognize_result;
      [[nodiscard]] auto earley_recognize(const token_sequence& tokens, const grammar_spec& grammar,
                                          std::size_t root_rule) -> recognize_result;
      [[nodiscard]] auto earley_inspect(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                        std::size_t ambiguity_limit = 2) -> inspect_result;
   } // namespace detail
} // namespace cpf

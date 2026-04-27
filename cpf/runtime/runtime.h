#pragma once

#include "api.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
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

      enum class parser_symbol_kind {
         nonterminal,
         terminal,
         positive_nonterminal,
         positive_terminal,
         negative_nonterminal,
         negative_terminal
      };

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

      struct validation_constraint_spec {
         int precedence = 0;
         bool left_associative = true;
         std::size_t left_child_index = 0;
         std::size_t right_child_index = 0;
      };

      struct production_model_metadata {
         bool has_source_rule = false;
         bool synthetic = false;
         std::size_t rule_id = 0;
         std::string_view rule_name;
         std::size_t definition = 0;
         int precedence = 0;
         bool precedence_passthrough = false;
         const validation_constraint_spec* validation_constraints = nullptr;
         std::size_t validation_constraint_count = 0;
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

      struct model_spec {
         grammar_spec grammar;
         const production_model_metadata* production_metadata = nullptr;
         std::size_t production_metadata_count = 0;
         const std::string_view* rule_names = nullptr;
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

      enum class forest_span_order { ascending, descending };

      struct inspect_result {
         bool success = false;
         bool ambiguous = false;
         parse_error error;
      };

      [[nodiscard]] auto node_child_at(const parse_node_ptr& tree, std::size_t index) -> parse_node_ptr;
      [[nodiscard]] auto matched_child_at(const parse_node_ptr& tree, std::size_t index) -> matched_string;
      void append_matched_tree_text(const parse_node_ptr& tree, std::string& text);
      [[nodiscard]] auto matched_tree_at(const parse_node_ptr& tree) -> matched_string;
      [[nodiscard]] auto production_metadata_of(const parse_node_ptr& tree, const model_spec& model)
            -> const production_model_metadata*;
      [[nodiscard]] auto production_definition_of(const parse_node_ptr& tree, const model_spec& model) -> std::size_t;
      [[nodiscard]] auto parse_tree_is_synthetic(const parse_node_ptr& tree, const model_spec& model) -> bool;
      [[nodiscard]] auto parse_tree_rule_id(const parse_node_ptr& tree, const model_spec& model) -> std::size_t;
      [[nodiscard]] auto parse_tree_rule_name(const parse_node_ptr& tree, const model_spec& model) -> std::string_view;
      [[nodiscard]] auto precedence_of_tree(const parse_node_ptr& tree, const model_spec& model) -> int;
      [[nodiscard]] auto validate_child_tree(const parse_node_ptr& child, int precedence, bool left_associative,
                                             bool is_left_child, const model_spec& model) -> bool;
      [[nodiscard]] auto validate_parse_tree(const parse_node_ptr& tree, const model_spec& model) -> bool;
      [[nodiscard]] auto requires_tree_validation(const model_spec& model) -> bool;
      void append_cst_children(const parse_node_ptr& tree, const model_spec& model, std::vector<cst_child>& children);
      [[nodiscard]] auto build_cst_node(const parse_node_ptr& tree, const model_spec& model) -> std::unique_ptr<cst_node>;
      [[nodiscard]] auto filtered_parse_error(std::string_view rule_name) -> parse_error;

      template<typename T, typename ValidateTree, typename MaterializeTree, typename DamageIndexer>
      [[nodiscard]] auto populate_parse_result(const parse_forest& forest, std::string_view root_rule_name,
                                              const model_spec& model, const parse_options& options,
                                              ValidateTree&& validate_tree, MaterializeTree&& materialize_tree,
                                              DamageIndexer&& damage_indexer) -> parse_result<T> {
         auto result = parse_result<T>{};
         result.partial = forest.partial;
         if (result.partial) {
            result.error = forest.error;
         }

         auto valid_tree_count = std::size_t{0};
         for (std::size_t tree_index = 0; tree_index < forest.forest.size(); ++tree_index) {
            const auto& tree = forest.forest[tree_index];
            if (!validate_tree(tree)) {
               continue;
            }
            ++valid_tree_count;
            if (options.error_on_ambiguity && valid_tree_count > 1) {
               result.status = parse_status::failure;
               result.success = false;
               result.partial = false;
               result.forest.clear();
               result.error = make_ambiguity_error(root_rule_name);
               return result;
            }
            result.status = result.partial ? parse_status::partial_success : parse_status::success;
            result.success = true;
            if (!options.build_ast) {
               continue;
            }
            result.forest.emplace_back(
                  tree, production_definition_of(tree, model), tree->range,
                  [materialize = materialize_tree, tree]() mutable { return materialize(tree); }, forest.tree_damage[tree_index],
                  forest.tree_partial[tree_index],
                  [damage_indexer = damage_indexer](const T& root, std::vector<const node*>& damaged_nodes) mutable {
                     damage_indexer(root, damaged_nodes);
                  },
                  [tree, grammar = model.grammar](std::string_view repaired_input) {
                     return repaired_input_of(tree, repaired_input, grammar);
                  });
         }

         return result;
      }

      template<typename T, typename RecognizeFactory, typename ParseForestFactory, typename ValidateTree, typename MaterializeTree,
               typename DamageIndexer>
      [[nodiscard]] auto parse_shared_forest(RecognizeFactory&& recognize, ParseForestFactory&& parse_forest, const model_spec& model,
                                             std::size_t root_rule, const parse_options& options,
                                             ValidateTree&& validate_tree, MaterializeTree&& materialize_tree,
                                             DamageIndexer&& damage_indexer) -> parse_result<T> {
         if (model.rule_names == nullptr || root_rule >= model.grammar.rule_count) {
            throw std::runtime_error{"Unknown parse root rule"};
         }

         auto fetch_forest = std::forward<ParseForestFactory>(parse_forest);
          auto recognize_only = std::forward<RecognizeFactory>(recognize);
         auto validate = std::forward<ValidateTree>(validate_tree);
         auto materialize = std::forward<MaterializeTree>(materialize_tree);
         auto index_damage = std::forward<DamageIndexer>(damage_indexer);
         const auto root_rule_name = model.rule_names[root_rule];

          if (!options.build_ast && !options.allow_partial && !options.error_on_ambiguity &&
              !requires_tree_validation(model)) {
             auto result = parse_result<T>{};
             auto recognized = recognize_only();
             result.success = recognized.success;
             result.status = recognized.success ? parse_status::success : parse_status::failure;
             if (!recognized.success) {
                result.error = std::move(recognized.error);
             }
             return result;
          }

         auto forest = fetch_forest(forest_span_order::ascending);
         if (!forest.success) {
            auto result = parse_result<T>{};
            result.error = std::move(forest.error);
            return result;
         }

         auto result = populate_parse_result<T>(forest, root_rule_name, model, options, validate, materialize,
                                                index_damage);
         if (result.success || (result.error.has_value() &&
                                result.error->found.kind == parse_error_found_kind::ambiguous_parse)) {
            return result;
         }

         auto alternate_forest = fetch_forest(forest_span_order::descending);
         if (alternate_forest.success) {
            auto alternate = populate_parse_result<T>(alternate_forest, root_rule_name, model, options, validate,
                                                      materialize, index_damage);
            if (alternate.success || (alternate.error.has_value() &&
                                      alternate.error->found.kind == parse_error_found_kind::ambiguous_parse)) {
               return alternate;
            }
         }

         result.error = filtered_parse_error(root_rule_name);
         return result;
      }

      [[nodiscard]] auto lex_input(std::string_view input, const grammar_spec& grammar) -> token_sequence;
      [[nodiscard]] auto earley_parse(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                      bool allow_partial = false,
                                      forest_span_order span_order = forest_span_order::ascending) -> parse_forest;
      [[nodiscard]] auto earley_parse(const token_sequence& tokens, const grammar_spec& grammar, std::size_t root_rule,
                                      bool allow_partial = false,
                                      forest_span_order span_order = forest_span_order::ascending) -> parse_forest;
      [[nodiscard]] auto earley_recognize(std::string_view input, const grammar_spec& grammar, std::size_t root_rule)
            -> recognize_result;
      [[nodiscard]] auto earley_recognize(const token_sequence& tokens, const grammar_spec& grammar,
                                          std::size_t root_rule) -> recognize_result;
      [[nodiscard]] auto earley_inspect(std::string_view input, const grammar_spec& grammar, std::size_t root_rule,
                                        std::size_t ambiguity_limit = 2) -> inspect_result;
   } // namespace detail
} // namespace cpf

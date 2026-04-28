#include "dynamic.h"

#include "runtime.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cpf {
   namespace {
      enum class field_shape {
         terminal_scalar,
         terminal_optional,
         terminal_vector,
         node_scalar,
         node_vector,
         capture_variant
      };

      struct variant_alternative_info {
         std::string type;
         std::string resolved_rule;
         bool node = false;
         bool lexical = false;
      };

      struct field_info {
         std::string name;
         std::string type;
         std::string resolved_rule;
         field_shape shape = field_shape::terminal_scalar;
         std::vector<variant_alternative_info> variant_alternatives;
      };

      struct class_info {
         std::string name;
         std::string base;
         bool base_rule = false;
         std::vector<field_info> fields;
      };

      struct synthetic_capture_info {
         std::size_t id = 0;
         std::string rule_name;
         field_info field;
         std::vector<std::size_t> production_indices;
         std::vector<variant_alternative_info> production_alternatives;
         std::unordered_map<std::size_t, std::size_t> production_offsets;
      };

      enum class helper_kind { optional, zero_or_more, one_or_more, exact };

      struct helper_info {
         std::size_t id = 0;
         std::size_t rule_index = 0;
         std::string rule_name;
         symbol base_symbol;
         std::string owner_rule_name;
         std::size_t owner_definition = 0;
         std::size_t lexer_precedence = 0;
         helper_kind kind = helper_kind::optional;
         std::size_t exact_count = 0;
         std::vector<std::size_t> production_indices;
      };

      struct lowered_symbol {
         const symbol* source = nullptr;
         std::optional<std::size_t> helper_id;
      };

      struct emitted_symbol {
         symbol_kind kind = symbol_kind::reference;
         std::size_t value = 0;
         std::string text;
         lookahead_kind lookahead = lookahead_kind::none;
      };

      struct emitted_production_info {
         std::size_t lhs = 0;
         std::string lhs_name;
         std::string debug_text;
         std::size_t lexer_precedence = 0;
         std::vector<emitted_symbol> symbols;
         const rule* source_rule = nullptr;
         const production* source_production = nullptr;
         std::vector<lowered_symbol> lowered_symbols;
         std::vector<std::optional<std::size_t>> child_indices;
      };

      struct emitted_lexer_symbol_info {
         symbol_kind kind = symbol_kind::literal;
         std::string text;
         std::size_t precedence = 0;
      };

      struct infix_definition_info {
         const production* source_production = nullptr;
         std::string child;
         std::size_t definition = 0;
         std::string group;
         std::string associativity;
         std::string left_label;
         std::string right_label;
         int precedence = 0;
      };

      struct family_info {
         std::string name;
         bool expression_family = false;
         std::vector<std::string> direct_children;
         std::vector<std::string> primary_children;
         std::vector<infix_definition_info> infix_definitions;
      };

      struct precedence_rank {
         bool explicit_value = false;
         int value = 0;
         std::size_t line = 0;
      };

      [[nodiscard]] auto is_infix_production(const class_info& info, const production& production) -> bool {
         if (info.base.empty()) {
            return false;
         }
         const auto& symbols = production.symbols;
         return symbols.size() == 3 && symbols[0].is_single() && symbols[1].is_single() && symbols[2].is_single() &&
                symbols[0].kind == symbol_kind::reference && symbols[2].kind == symbol_kind::reference &&
                symbols[0].value == info.base && symbols[2].value == info.base &&
                symbols[1].kind != symbol_kind::reference;
      }

      [[nodiscard]] auto default_precedence_alias(std::string_view rule_name, const production& production,
                                                  bool repeated_definition) -> std::string {
         if (!repeated_definition) {
            return std::string{rule_name};
         }
         return std::string{rule_name} + "#" + std::to_string(production.definition);
      }

      [[nodiscard]] auto precedence_label(std::string_view rule_name, const production& production,
                                          bool repeated_definition) -> std::string {
         if (auto attribute = production.find_attribute("prec");
             attribute.has_value() && attribute->operation == attribute_operator::assign && !attribute->numeric) {
            return attribute->value;
         }
         if (auto attribute = production.find_attribute("lbl"); attribute.has_value()) {
            return attribute->value;
         }
         return default_precedence_alias(rule_name, production, repeated_definition);
      }

      [[nodiscard]] auto precedence_alias(std::string_view rule_name, const production& production,
                                          bool repeated_definition) -> std::string {
         if (auto attribute = production.find_attribute("lbl"); attribute.has_value()) {
            return attribute->value;
         }
         return default_precedence_alias(rule_name, production, repeated_definition);
      }

      [[nodiscard]] auto associativity_of(const production& production) -> std::string {
         if (auto attribute = production.find_attribute("dir"); attribute.has_value()) {
            return attribute->value;
         }
         return "left";
      }

      [[nodiscard]] auto has_absolute_precedence_rank(const production& production) -> bool {
         if (auto attribute = production.find_attribute("prec"); attribute.has_value()) {
            return attribute->numeric;
         }
         return true;
      }

      [[nodiscard]] auto precedence_rank_of(const production& production) -> precedence_rank {
         precedence_rank rank;
         rank.line = production.line;
         rank.value = static_cast<int>(production.line);
         if (auto attribute = production.find_attribute("prec"); attribute.has_value() && attribute->numeric) {
            rank.explicit_value = true;
            rank.value = std::stoi(attribute->value);
         }
         return rank;
      }

      [[nodiscard]] auto is_node_field(const field_info& field) -> bool {
         return field.shape == field_shape::node_scalar || field.shape == field_shape::node_vector;
      }

      [[nodiscard]] auto is_variant_field(const field_info& field) -> bool {
         return field.shape == field_shape::capture_variant;
      }

      [[nodiscard]] auto inline_requested_for(const symbol& symbol, const rule* referenced_rule) -> bool {
         return symbol.inline_requested || (referenced_rule != nullptr && referenced_rule->inline_requested);
      }

      [[nodiscard]] auto inline_target_field(const symbol& symbol,
                                            const std::unordered_map<std::string, const rule*>& rules_by_name,
                                            const std::unordered_set<std::string>& lexical_rules,
                                            const std::unordered_map<std::string, class_info>& classes)
            -> const field_info* {
         if (symbol.kind != symbol_kind::reference || lexical_rules.contains(symbol.value)) {
            return nullptr;
         }

         auto rule_it = rules_by_name.find(symbol.value);
         if (rule_it == rules_by_name.end() || !inline_requested_for(symbol, rule_it->second)) {
            return nullptr;
         }

         auto class_it = classes.find(symbol.value);
         if (class_it == classes.end() || class_it->second.base_rule || class_it->second.fields.size() != 1) {
            return nullptr;
         }

         return &class_it->second.fields.front();
      }

      [[nodiscard]] auto uses_helper_rule(const symbol& symbol) -> bool { return !symbol.is_single(); }

      [[nodiscard]] auto is_lexical_reference(const symbol& symbol,
                                             const std::unordered_set<std::string>& lexical_rules) -> bool {
         return symbol.kind == symbol_kind::reference && lexical_rules.contains(symbol.value);
      }

      [[nodiscard]] auto production_is_lexical(const production& production,
                                               const std::unordered_set<std::string>& lexical_rules) -> bool {
         if (!production.attributes.empty()) {
            return false;
         }
         return std::all_of(production.symbols.begin(), production.symbols.end(), [&](const auto& symbol) {
            if (symbol.is_zero_width()) {
               return false;
            }
            return symbol.kind != symbol_kind::reference || lexical_rules.contains(symbol.value);
         });
      }

      [[nodiscard]] auto field_from_symbol(const symbol& symbol, const std::unordered_set<std::string>& lexical_rules,
                                           const std::unordered_map<std::string, field_info>& synthetic_capture_fields = {})
            -> field_info {
         field_info field;
         field.name = symbol.label;
         if (symbol.is_zero_width()) {
            return field;
         }
         if (symbol.kind == symbol_kind::reference) {
            if (symbol.has_label()) {
               if (auto synthetic_it = synthetic_capture_fields.find(symbol.value);
                   synthetic_it != synthetic_capture_fields.end()) {
                  field = synthetic_it->second;
                  field.name = symbol.label;
                  return field;
               }
            }
            if (lexical_rules.contains(symbol.value)) {
               if (symbol.is_repeated()) {
                  field.shape = field_shape::terminal_vector;
                  field.type = "std::vector<cpf::matched_string>";
               } else if (symbol.is_optional()) {
                  field.shape = field_shape::terminal_optional;
                  field.type = "std::optional<cpf::matched_string>";
               } else {
                  field.shape = field_shape::terminal_scalar;
                  field.type = "cpf::matched_string";
               }
               field.resolved_rule = symbol.value;
               return field;
            }
            field.resolved_rule = symbol.value;
            if (symbol.is_repeated()) {
               field.shape = field_shape::node_vector;
               field.type = "std::vector<std::unique_ptr<" + symbol.value + ">>";
            } else {
               field.shape = field_shape::node_scalar;
               field.type = "std::unique_ptr<" + symbol.value + ">";
            }
            return field;
         }

         if (symbol.is_repeated()) {
            field.shape = field_shape::terminal_vector;
            field.type = "std::vector<cpf::matched_string>";
         } else if (symbol.is_optional()) {
            field.shape = field_shape::terminal_optional;
            field.type = "std::optional<cpf::matched_string>";
         } else {
            field.shape = field_shape::terminal_scalar;
            field.type = "cpf::matched_string";
         }
         return field;
      }

      [[nodiscard]] auto field_shapes_compatible(field_shape existing, field_shape candidate) -> bool {
         if (existing == candidate) {
            return true;
         }
         const auto existing_terminal = existing == field_shape::terminal_scalar || existing == field_shape::terminal_optional ||
                                       existing == field_shape::terminal_vector;
         const auto candidate_terminal = candidate == field_shape::terminal_scalar ||
                                        candidate == field_shape::terminal_optional ||
                                        candidate == field_shape::terminal_vector;
         if (existing_terminal && candidate_terminal) {
            return true;
         }
         const auto existing_node = existing == field_shape::node_scalar || existing == field_shape::node_vector;
         const auto candidate_node = candidate == field_shape::node_scalar || candidate == field_shape::node_vector;
         if (existing_node && candidate_node) {
            return true;
         }
         return false;
      }

      [[nodiscard]] auto merged_field_type(const field_info& field) -> std::string {
         switch (field.shape) {
            case field_shape::terminal_scalar:
               return "cpf::matched_string";
            case field_shape::terminal_optional:
               return "std::optional<cpf::matched_string>";
            case field_shape::terminal_vector:
               return "std::vector<cpf::matched_string>";
            case field_shape::node_scalar:
               return "std::unique_ptr<" + field.resolved_rule + ">";
            case field_shape::node_vector:
               return "std::vector<std::unique_ptr<" + field.resolved_rule + ">>";
            case field_shape::capture_variant:
               return field.type;
         }
         return {};
      }

      [[nodiscard]] auto describe_field_type(const field_info& field) -> std::string {
         switch (field.shape) {
            case field_shape::terminal_scalar:
               return "cpf::matched_string";
            case field_shape::terminal_optional:
               return "std::optional<cpf::matched_string>";
            case field_shape::terminal_vector:
               return "std::vector<cpf::matched_string>";
            case field_shape::node_scalar:
               return field.resolved_rule;
            case field_shape::node_vector:
               return "std::vector<" + field.resolved_rule + ">";
            case field_shape::capture_variant:
               return field.type;
         }
         return {};
      }

      [[nodiscard]] auto field_layout_equal(const field_info& left, const field_info& right) -> bool {
         if (left.name != right.name || left.type != right.type || left.resolved_rule != right.resolved_rule ||
             left.shape != right.shape || left.variant_alternatives.size() != right.variant_alternatives.size()) {
            return false;
         }

         for (std::size_t i = 0; i < left.variant_alternatives.size(); ++i) {
            const auto& left_alternative = left.variant_alternatives[i];
            const auto& right_alternative = right.variant_alternatives[i];
            if (left_alternative.type != right_alternative.type ||
                left_alternative.resolved_rule != right_alternative.resolved_rule ||
                left_alternative.node != right_alternative.node ||
                left_alternative.lexical != right_alternative.lexical) {
               return false;
            }
         }

         return true;
      }

      [[nodiscard]] auto apply_symbol_quantifier_to_field(std::string_view owner_rule, field_info field,
                                                          const symbol& symbol) -> field_info {
         if (symbol.is_single()) {
            return field;
         }

         if (field.shape == field_shape::capture_variant) {
            throw std::runtime_error{"Rule '" + std::string{owner_rule} +
                                     "' cannot flatten quantified helper capture '" + field.name +
                                     "' because variant members cannot be repeated or made optional implicitly"};
         }

         if (symbol.is_repeated()) {
            if (field.shape == field_shape::terminal_scalar || field.shape == field_shape::terminal_optional) {
               field.shape = field_shape::terminal_vector;
            } else if (field.shape == field_shape::node_scalar) {
               field.shape = field_shape::node_vector;
            }
            field.type = merged_field_type(field);
            return field;
         }

         if (symbol.is_optional() && field.shape == field_shape::terminal_scalar) {
            field.shape = field_shape::terminal_optional;
            field.type = merged_field_type(field);
         }

         return field;
      }

      [[nodiscard]] auto common_rule_base(std::string_view left, std::string_view right,
                                          const std::unordered_map<std::string, std::string>& bases)
            -> std::optional<std::string> {
         std::set<std::string> left_ancestors;
         auto current = std::string{left};
         while (!current.empty()) {
            left_ancestors.insert(current);
            auto base_it = bases.find(current);
            if (base_it == bases.end()) {
               break;
            }
            current = base_it->second;
         }

         current = std::string{right};
         while (!current.empty()) {
            if (left_ancestors.contains(current)) {
               return current;
            }
            auto base_it = bases.find(current);
            if (base_it == bases.end()) {
               break;
            }
            current = base_it->second;
         }

         return std::nullopt;
      }

      void merge_field_resolution(std::string_view owner_rule, field_info& existing, const field_info& candidate,
                                  const std::unordered_map<std::string, std::string>& bases,
                                  bool repeated_assignment = false) {
         if (is_variant_field(existing) || is_variant_field(candidate)) {
            if (repeated_assignment) {
               throw std::runtime_error{"Rule '" + std::string{owner_rule} + "' label '" + existing.name +
                                        "' cannot be repeated because variant members are not supported in repeated captures"};
            }
            if (existing.type != candidate.type || existing.shape != candidate.shape) {
               throw std::runtime_error{"Rule '" + std::string{owner_rule} + "' label '" + existing.name +
                                        "' has conflicting member types '" + describe_field_type(existing) + "' and '" +
                                        describe_field_type(candidate) + "'"};
            }
            existing.variant_alternatives = candidate.variant_alternatives;
            existing.type = candidate.type;
            return;
         }

         auto existing_is_node = is_node_field(existing);
         auto candidate_is_node = is_node_field(candidate);
         if (existing_is_node != candidate_is_node) {
            throw std::runtime_error{"Rule '" + std::string{owner_rule} + "' label '" + existing.name +
                                     "' has conflicting member types '" + describe_field_type(existing) + "' and '" +
                                     describe_field_type(candidate) + "'"};
         }

         if (!field_shapes_compatible(existing.shape, candidate.shape)) {
            throw std::runtime_error{"Rule '" + std::string{owner_rule} + "' label '" + existing.name +
                                     "' has conflicting member types '" + describe_field_type(existing) + "' and '" +
                                     describe_field_type(candidate) + "'"};
         }

         if (!existing_is_node) {
            if (repeated_assignment || existing.shape == field_shape::terminal_vector ||
                candidate.shape == field_shape::terminal_vector) {
               existing.shape = field_shape::terminal_vector;
            } else if ((existing.shape == field_shape::terminal_scalar && candidate.shape == field_shape::terminal_optional) ||
                       (existing.shape == field_shape::terminal_optional && candidate.shape == field_shape::terminal_scalar)) {
               existing.shape = field_shape::terminal_optional;
            }
            existing.type = merged_field_type(existing);
            if (existing.resolved_rule.empty()) {
               existing.resolved_rule = candidate.resolved_rule;
            }
            return;
         }

         auto common_base = common_rule_base(existing.resolved_rule, candidate.resolved_rule, bases);
         if (!common_base.has_value()) {
            throw std::runtime_error{"Rule '" + std::string{owner_rule} + "' label '" + existing.name +
                                     "' cannot resolve a common member type for rules '" + existing.resolved_rule +
                                     "' and '" + candidate.resolved_rule + "'"};
         }

         existing.resolved_rule = *common_base;
         if (repeated_assignment || existing.shape == field_shape::node_vector ||
             candidate.shape == field_shape::node_vector) {
            existing.shape = field_shape::node_vector;
         }
         existing.type = merged_field_type(existing);
      }

      void register_group_alias(std::unordered_map<std::string, std::string>& alias_to_group, std::string_view alias,
                                std::string_view group, std::string_view family_name) {
         if (alias.empty()) {
            return;
         }

         auto alias_text = std::string{alias};
         auto group_text = std::string{group};
         if (auto alias_it = alias_to_group.find(alias_text);
             alias_it != alias_to_group.end() && alias_it->second != group_text) {
            throw std::runtime_error{"Expression family '" + std::string{family_name} +
                                     "' cannot use precedence alias '" + alias_text + "' for both '" +
                                     alias_it->second + "' and '" + group_text + "'"};
         }
         alias_to_group[alias_text] = std::move(group_text);
      }

      [[nodiscard]] auto render_symbol_debug(const symbol& symbol) -> std::string {
         std::string text;
         if (symbol.kind == symbol_kind::reference) {
            text = symbol.value;
         } else if (symbol.kind == symbol_kind::literal) {
            text = "'" + symbol.value + "'";
         } else {
            text = "r'" + symbol.value + "'";
         }

         if (symbol.lookahead == lookahead_kind::positive) {
            text = "&" + text;
         } else if (symbol.lookahead == lookahead_kind::negative) {
            text = "!" + text;
         }

         switch (symbol.quantifier) {
            case symbol_quantifier::one:
               break;
            case symbol_quantifier::optional:
               text += "?";
               break;
            case symbol_quantifier::zero_or_more:
               text += "*";
               break;
            case symbol_quantifier::one_or_more:
               text += "+";
               break;
            case symbol_quantifier::exact:
               text += "{" + std::to_string(symbol.exact_repetition) + "}";
               break;
         }

         if (symbol.has_label()) {
            text += ":" + symbol.label;
            if (symbol.inline_requested) {
               text += "[inline]";
            }
         }
         return text;
      }

      [[nodiscard]] auto render_production_debug(std::string_view rule_name, const production& production) -> std::string {
         std::string text = std::string{rule_name} + " ->";
         for (const auto& symbol: production.symbols) {
            text += " " + render_symbol_debug(symbol);
         }
         return text;
      }

      [[nodiscard]] auto to_dynamic_field_shape(field_shape shape) -> dynamic_field_shape {
         switch (shape) {
            case field_shape::terminal_scalar:
               return dynamic_field_shape::terminal_scalar;
            case field_shape::terminal_optional:
               return dynamic_field_shape::terminal_optional;
            case field_shape::terminal_vector:
               return dynamic_field_shape::terminal_vector;
            case field_shape::node_scalar:
               return dynamic_field_shape::node_scalar;
            case field_shape::node_vector:
               return dynamic_field_shape::node_vector;
            case field_shape::capture_variant:
               return dynamic_field_shape::capture_variant;
         }
         return dynamic_field_shape::terminal_scalar;
      }

      [[nodiscard]] auto declared_type_name_of(const field_info& field) -> std::string {
         switch (field.shape) {
            case field_shape::terminal_scalar:
            case field_shape::terminal_optional:
            case field_shape::terminal_vector:
               return field.resolved_rule.empty() ? "cpf::matched_string" : field.resolved_rule;
            case field_shape::node_scalar:
            case field_shape::node_vector:
               return field.resolved_rule;
            case field_shape::capture_variant:
               return "variant";
         }
         return {};
      }

      [[nodiscard]] auto alternative_type_name_of(const variant_alternative_info& alternative) -> std::string {
         if (alternative.node) {
            return alternative.resolved_rule;
         }
         if (!alternative.resolved_rule.empty()) {
            return alternative.resolved_rule;
         }
         return "cpf::matched_string";
      }

      void write_indent(std::ostream& os, std::size_t indent) {
         for (std::size_t i = 0; i < indent; ++i) {
            os << "  ";
         }
      }

      void write_range(std::ostream& os, const source_range& range) {
         os << range.begin.offset << ".." << range.end.offset << " (" << range.begin.line << ':'
            << range.begin.column << '-' << range.end.line << ':' << range.end.column << ')';
      }

      void write_dynamic_field(std::ostream& os, const dynamic_field& field, std::size_t indent);

      void write_dynamic_node(std::ostream& os, const dynamic_node& node, std::size_t indent) {
         write_indent(os, indent);
         os << node.rule_name << " [production=" << node.production_index << ", range=";
         write_range(os, node.range);
         os << ']';
         if (node.fields.empty()) {
            return;
         }

         os << "(\n";
         auto index = std::size_t{0};
         for (const auto& [field_name, field]: node.fields) {
            (void) field_name;
            write_dynamic_field(os, field, indent + 1);
            if (index + 1 < node.fields.size()) {
               os << '\n';
            }
            ++index;
         }
         os << '\n';
         write_indent(os, indent);
         os << ')';
      }

      void write_dynamic_field(std::ostream& os, const dynamic_field& field, std::size_t indent) {
         write_indent(os, indent);
         os << field.name << " = ";
         switch (field.value_kind) {
            case dynamic_field_value_kind::empty:
               os << "null";
               return;
            case dynamic_field_value_kind::token:
               os << "terminal(";
               if (field.value_type_name.empty()) {
                  os << "cpf::matched_string, ";
               } else {
                  os << field.value_type_name << ", ";
               }
               os << std::quoted(field.token.has_value() ? field.token->text : std::string{}) << ')';
               return;
            case dynamic_field_value_kind::node:
               if (field.node != nullptr) {
                  os << '\n';
                  write_dynamic_node(os, *field.node, indent + 1);
               } else {
                  os << "null";
               }
               return;
            case dynamic_field_value_kind::token_list:
               os << '[';
               for (std::size_t index = 0; index < field.tokens.size(); ++index) {
                  if (index != 0) {
                     os << ", ";
                  }
                  os << std::quoted(field.tokens[index].text);
               }
               os << ']';
               return;
            case dynamic_field_value_kind::node_list:
               if (field.nodes.empty()) {
                  os << "[]";
                  return;
               }
               os << "[\n";
               for (std::size_t index = 0; index < field.nodes.size(); ++index) {
                  if (field.nodes[index] != nullptr) {
                     write_dynamic_node(os, *field.nodes[index], indent + 1);
                  } else {
                     write_indent(os, indent + 1);
                     os << "null";
                  }
                  if (index + 1 < field.nodes.size()) {
                     os << '\n';
                  }
               }
               os << '\n';
               write_indent(os, indent);
               os << ']';
               return;
         }
      }
   } // namespace

   struct parser::impl {
      grammar source_grammar;
      grammar_analysis analysis_result;
      std::vector<dynamic_rule_info> public_rules;
      std::unordered_map<std::string, std::size_t> public_rule_lookup;
      std::unordered_map<std::string, const rule*> rules_by_name;
      std::unordered_set<std::string> lexical_rules;
      std::unordered_map<std::string, class_info> classes;
      std::unordered_map<std::string, std::unordered_map<std::string, field_info>> fields_by_rule;
      std::unordered_set<std::string> structured_synthetic_rules;
      std::unordered_map<std::string, family_info> families;
      std::unordered_map<std::string, std::size_t> rule_indices;
      std::unordered_map<std::string, std::size_t> synthetic_capture_by_rule;
      std::vector<std::string> emitted_rule_names;
      std::vector<helper_info> helpers;
      std::vector<synthetic_capture_info> synthetic_captures;
      std::vector<emitted_production_info> emitted_productions;
      std::vector<detail::production_model_metadata> production_metadata;
      std::vector<std::vector<detail::validation_constraint_spec>> production_validation_constraints;

      std::vector<std::vector<detail::parser_symbol>> production_symbols;
      std::vector<std::vector<std::string>> production_symbol_texts;
      std::vector<std::string> production_lhs_names;
      std::vector<std::string> production_debug_texts;
      std::vector<detail::production_spec> production_specs;
      std::vector<std::size_t> grammar_rule_production_indices;
      std::vector<std::size_t> grammar_rule_production_offsets;
      std::vector<std::size_t> grammar_rule_production_counts;
      std::vector<std::string> grammar_rule_expected_labels_storage;
      std::vector<std::string_view> grammar_rule_expected_labels;
      std::vector<std::string> token_symbol_texts;
      std::vector<std::string> skip_symbol_texts;
      std::vector<std::regex> token_regexes;
      std::vector<std::regex> skip_regexes;
      std::vector<detail::lexer_symbol_spec> token_symbols;
      std::vector<detail::lexer_symbol_spec> skip_symbols;
      std::vector<std::string_view> grammar_rule_names;
      detail::grammar_spec grammar_spec;
      detail::model_spec shared_model;

      [[nodiscard]] auto resolve_root_rule(std::string_view root_rule) const -> std::size_t {
         auto rule_name = std::string{root_rule};
         if (rule_name.empty()) {
            rule_name = analysis_result.summary.primary_entry_rule;
         }
         if (rule_name.empty()) {
            throw std::runtime_error{"Compiled grammar does not expose a primary entry rule"};
         }
         auto found = public_rule_lookup.find(rule_name);
         if (found == public_rule_lookup.end()) {
            throw std::invalid_argument{"Unknown public rule '" + rule_name + "'"};
         }
         return public_rules[found->second].id;
      }

      [[nodiscard]] auto rule_info(std::string_view name) const -> const dynamic_rule_info* {
         auto found = public_rule_lookup.find(std::string{name});
         if (found == public_rule_lookup.end()) {
            return nullptr;
         }
         return &public_rules[found->second];
      }

      [[nodiscard]] auto node_child_at(const detail::parse_node_ptr& tree, std::size_t index) const
            -> detail::parse_node_ptr {
         return detail::node_child_at(tree, index);
      }

      [[nodiscard]] auto matched_child_at(const detail::parse_node_ptr& tree, std::size_t index) const -> matched_string {
         return detail::matched_child_at(tree, index);
      }

      void append_matched_tree_text(const detail::parse_node_ptr& tree, std::string& text) const {
         detail::append_matched_tree_text(tree, text);
      }

      [[nodiscard]] auto matched_tree_at(const detail::parse_node_ptr& tree) const -> matched_string {
         return detail::matched_tree_at(tree);
      }

      [[nodiscard]] auto generated_tree_is_synthetic(const detail::parse_node_ptr& tree) const -> bool {
         return detail::parse_tree_is_synthetic(tree, shared_model);
      }

      [[nodiscard]] auto generated_tree_rule_id(const detail::parse_node_ptr& tree) const -> std::size_t {
         return detail::parse_tree_rule_id(tree, shared_model);
      }

      [[nodiscard]] auto generated_tree_rule_name(const detail::parse_node_ptr& tree) const -> std::string_view {
         return detail::parse_tree_rule_name(tree, shared_model);
      }

      [[nodiscard]] auto definition_of_generated_tree(const detail::parse_node_ptr& tree) const -> std::size_t {
         return detail::production_definition_of(tree, shared_model);
      }

      void append_cst_children(const detail::parse_node_ptr& tree, std::vector<cst_child>& children) const {
         detail::append_cst_children(tree, shared_model, children);
      }

      [[nodiscard]] auto build_cst_node(const detail::parse_node_ptr& tree) const -> std::unique_ptr<cst_node> {
         return detail::build_cst_node(tree, shared_model);
      }

      [[nodiscard]] auto make_field(const field_info& info) const -> dynamic_field {
         dynamic_field field;
         field.name = info.name;
         field.shape = to_dynamic_field_shape(info.shape);
         field.declared_type_name = declared_type_name_of(info);
         for (const auto& alternative: info.variant_alternatives) {
            field.alternative_type_names.push_back(alternative_type_name_of(alternative));
         }
         if (info.shape == field_shape::terminal_vector) {
            field.value_kind = dynamic_field_value_kind::token_list;
         } else if (info.shape == field_shape::node_vector) {
            field.value_kind = dynamic_field_value_kind::node_list;
         }
         return field;
      }

      void merge_dynamic_field(dynamic_field& target, dynamic_field source) const {
         switch (target.shape) {
            case dynamic_field_shape::terminal_scalar:
            case dynamic_field_shape::terminal_optional:
            case dynamic_field_shape::capture_variant:
               target.value_kind = source.value_kind;
               target.value_type_name = std::move(source.value_type_name);
               target.token = std::move(source.token);
               target.node = std::move(source.node);
               target.tokens = std::move(source.tokens);
               target.nodes = std::move(source.nodes);
               return;
            case dynamic_field_shape::terminal_vector:
               target.value_kind = dynamic_field_value_kind::token_list;
               if (target.value_type_name.empty()) {
                  target.value_type_name = target.declared_type_name;
               }
               if (source.value_kind == dynamic_field_value_kind::token && source.token.has_value()) {
                  target.tokens.push_back(std::move(*source.token));
               } else if (source.value_kind == dynamic_field_value_kind::token_list) {
                  for (auto& token: source.tokens) {
                     target.tokens.push_back(std::move(token));
                  }
               }
               return;
            case dynamic_field_shape::node_scalar:
               target.value_kind = source.value_kind;
               target.value_type_name = std::move(source.value_type_name);
               target.node = std::move(source.node);
               return;
            case dynamic_field_shape::node_vector:
               target.value_kind = dynamic_field_value_kind::node_list;
               if (target.value_type_name.empty()) {
                  target.value_type_name = target.declared_type_name;
               }
               if (source.value_kind == dynamic_field_value_kind::node && source.node != nullptr) {
                  target.nodes.push_back(std::move(source.node));
               } else if (source.value_kind == dynamic_field_value_kind::node_list) {
                  for (auto& node: source.nodes) {
                     target.nodes.push_back(std::move(node));
                  }
               }
               return;
         }
      }

      [[nodiscard]] auto field_named(dynamic_node& node, std::string_view name) const -> dynamic_field& {
         if (auto found = node.fields.find(std::string{name}); found != node.fields.end()) {
            return found->second;
         }
         throw std::runtime_error{"Dynamic parse tree is missing field '" + std::string{name} + "'"};
      }

      [[nodiscard]] auto field_named(const std::string& rule_name, std::string_view name) const -> const field_info& {
         return fields_by_rule.at(rule_name).at(std::string{name});
      }

      [[nodiscard]] auto rule_has_inline_target(std::string_view rule_name) const -> bool {
         if (lexical_rules.contains(std::string{rule_name})) {
            return false;
         }

         auto class_it = classes.find(std::string{rule_name});
         return class_it != classes.end() && !class_it->second.base_rule && class_it->second.fields.size() == 1;
      }

      [[nodiscard]] auto inline_target_field_for(const symbol& symbol) const -> const field_info* {
         return inline_target_field(symbol, rules_by_name, lexical_rules, classes);
      }

      [[nodiscard]] auto helper_named(std::size_t helper_id) const -> const helper_info& { return helpers.at(helper_id); }

      void merge_inline_child_field(dynamic_node& node, const symbol& symbol, std::unique_ptr<dynamic_node> child) const {
         if (child == nullptr) {
            return;
         }

         const auto& nested_field = classes.at(symbol.value).fields.front();
         const auto parent_field_name = symbol.has_label() ? symbol.label : nested_field.name;
         merge_dynamic_field(field_named(node, parent_field_name), std::move(field_named(*child, nested_field.name)));
      }

      void collect_helper_tokens(const helper_info& helper, const detail::parse_node_ptr& tree,
                                 std::vector<matched_string>& values, bool lexical_reference) const {
         switch (helper.kind) {
            case helper_kind::optional:
               if (tree->production == helper.production_indices[0]) {
                  return;
               }
               if (tree->production == helper.production_indices[1]) {
                  values.push_back(lexical_reference ? matched_tree_at(node_child_at(tree, 0)) : matched_child_at(tree, 0));
                  return;
               }
               break;
            case helper_kind::zero_or_more:
               if (tree->production == helper.production_indices[0]) {
                  return;
               }
               if (tree->production == helper.production_indices[1]) {
                  values.push_back(lexical_reference ? matched_tree_at(node_child_at(tree, 0)) : matched_child_at(tree, 0));
                  collect_helper_tokens(helper, node_child_at(tree, 1), values, lexical_reference);
                  return;
               }
               break;
            case helper_kind::one_or_more:
               if (tree->production == helper.production_indices[0]) {
                  values.push_back(lexical_reference ? matched_tree_at(node_child_at(tree, 0)) : matched_child_at(tree, 0));
                  return;
               }
               if (tree->production == helper.production_indices[1]) {
                  values.push_back(lexical_reference ? matched_tree_at(node_child_at(tree, 0)) : matched_child_at(tree, 0));
                  collect_helper_tokens(helper, node_child_at(tree, 1), values, lexical_reference);
                  return;
               }
               break;
            case helper_kind::exact:
               if (tree->production == helper.production_indices[0]) {
                  for (std::size_t index = 0; index < helper.exact_count; ++index) {
                     values.push_back(lexical_reference ? matched_tree_at(node_child_at(tree, index))
                                                        : matched_child_at(tree, index));
                  }
                  return;
               }
               break;
         }
         throw std::runtime_error{"Unknown quantified helper production"};
      }

      void collect_helper_nodes(const helper_info& helper, const detail::parse_node_ptr& tree,
                                std::vector<std::unique_ptr<dynamic_node>>& values) const {
         switch (helper.kind) {
            case helper_kind::optional:
               if (tree->production == helper.production_indices[0]) {
                  return;
               }
               if (tree->production == helper.production_indices[1]) {
                  values.push_back(build_dynamic_node(node_child_at(tree, 0)));
                  return;
               }
               break;
            case helper_kind::zero_or_more:
               if (tree->production == helper.production_indices[0]) {
                  return;
               }
               if (tree->production == helper.production_indices[1]) {
                  values.push_back(build_dynamic_node(node_child_at(tree, 0)));
                  collect_helper_nodes(helper, node_child_at(tree, 1), values);
                  return;
               }
               break;
            case helper_kind::one_or_more:
               if (tree->production == helper.production_indices[0]) {
                  values.push_back(build_dynamic_node(node_child_at(tree, 0)));
                  return;
               }
               if (tree->production == helper.production_indices[1]) {
                  values.push_back(build_dynamic_node(node_child_at(tree, 0)));
                  collect_helper_nodes(helper, node_child_at(tree, 1), values);
                  return;
               }
               break;
            case helper_kind::exact:
               if (tree->production == helper.production_indices[0]) {
                  for (std::size_t index = 0; index < helper.exact_count; ++index) {
                     values.push_back(build_dynamic_node(node_child_at(tree, index)));
                  }
                  return;
               }
               break;
         }
         throw std::runtime_error{"Unknown quantified helper production"};
      }

      void assign_helper_field(dynamic_field& field, const helper_info& helper, const detail::parse_node_ptr& tree) const {
         if (field.shape == dynamic_field_shape::terminal_optional || field.shape == dynamic_field_shape::terminal_vector) {
            field.value_type_name = field.declared_type_name;
         }
         if (field.shape == dynamic_field_shape::node_scalar || field.shape == dynamic_field_shape::node_vector) {
            field.value_type_name = field.declared_type_name;
         }

         if (helper.base_symbol.kind == symbol_kind::reference && lexical_rules.contains(helper.base_symbol.value)) {
            if (field.shape == dynamic_field_shape::terminal_optional) {
               if (tree->production == helper.production_indices[1]) {
                  field.value_kind = dynamic_field_value_kind::token;
                  field.token = matched_tree_at(node_child_at(tree, 0));
               }
               return;
            }
            field.value_kind = dynamic_field_value_kind::token_list;
            collect_helper_tokens(helper, tree, field.tokens, true);
            return;
         }

         if (helper.base_symbol.kind == symbol_kind::reference) {
            if (field.shape == dynamic_field_shape::node_scalar) {
               if (tree->production == helper.production_indices[1]) {
                  field.value_kind = dynamic_field_value_kind::node;
                  field.node = build_dynamic_node(node_child_at(tree, 0));
                  field.value_type_name = field.node != nullptr ? field.node->rule_name : field.declared_type_name;
               }
               return;
            }
            field.value_kind = dynamic_field_value_kind::node_list;
            collect_helper_nodes(helper, tree, field.nodes);
            return;
         }

         if (field.shape == dynamic_field_shape::terminal_optional) {
            if (tree->production == helper.production_indices[1]) {
               field.value_kind = dynamic_field_value_kind::token;
               field.token = matched_child_at(tree, 0);
               field.value_type_name = "cpf::matched_string";
            }
            return;
         }

         field.value_kind = dynamic_field_value_kind::token_list;
         collect_helper_tokens(helper, tree, field.tokens, false);
         field.value_type_name = "cpf::matched_string";
      }

      void assign_synthetic_capture_field(dynamic_field& field, const synthetic_capture_info& capture,
                                          const detail::parse_node_ptr& tree) const {
         auto alternative_offset = capture.production_offsets.find(tree->production);
         if (alternative_offset == capture.production_offsets.end()) {
            throw std::runtime_error{"Unknown labeled group production"};
         }
         const auto& alternative = capture.production_alternatives[alternative_offset->second];

         if (field.shape == dynamic_field_shape::terminal_scalar || field.shape == dynamic_field_shape::terminal_optional ||
             field.shape == dynamic_field_shape::terminal_vector) {
            field.value_kind = dynamic_field_value_kind::token;
            field.token = alternative.lexical ? matched_tree_at(node_child_at(tree, 0)) : matched_child_at(tree, 0);
            field.value_type_name = alternative_type_name_of(alternative);
            return;
         }

         if (field.shape == dynamic_field_shape::node_scalar || field.shape == dynamic_field_shape::node_vector) {
            field.value_kind = dynamic_field_value_kind::node;
            field.node = build_dynamic_node(node_child_at(tree, 0));
            field.value_type_name = field.node != nullptr ? field.node->rule_name : alternative.resolved_rule;
            return;
         }

         if (field.shape == dynamic_field_shape::capture_variant) {
            if (alternative.node) {
               field.value_kind = dynamic_field_value_kind::node;
               field.node = build_dynamic_node(node_child_at(tree, 0));
               field.value_type_name = field.node != nullptr ? field.node->rule_name : alternative.resolved_rule;
            } else {
               field.value_kind = dynamic_field_value_kind::token;
               field.token = alternative.lexical ? matched_tree_at(node_child_at(tree, 0)) : matched_child_at(tree, 0);
               field.value_type_name = alternative_type_name_of(alternative);
            }
            return;
         }

         throw std::runtime_error{"Unsupported synthetic capture field shape"};
      }

      [[nodiscard]] auto build_dynamic_node(const detail::parse_node_ptr& tree) const -> std::unique_ptr<dynamic_node> {
         if (tree->production >= emitted_productions.size()) {
            throw std::runtime_error{"Unknown dynamic parse production"};
         }

         const auto& emitted_production = emitted_productions[tree->production];
         if (emitted_production.source_rule == nullptr || emitted_production.source_production == nullptr) {
            throw std::runtime_error{"Unknown dynamic parse production"};
         }

         const auto& rule = *emitted_production.source_rule;
         const auto& production = *emitted_production.source_production;
         const auto& info = classes.at(rule.identifier);
         if (info.base_rule) {
            return build_dynamic_node(node_child_at(tree, 0));
         }

         auto node = std::make_unique<dynamic_node>();
         node->production_index = production.definition;
         node->range = tree->range;
         node->rule = rule_indices.at(info.name);
         node->rule_name = info.name;
         for (const auto& damage: tree->damage) {
            detail::add_damage(*node, damage);
         }
         for (const auto& field: info.fields) {
            node->fields.emplace(field.name, make_field(field));
         }

         for (std::size_t index = 0; index < emitted_production.lowered_symbols.size(); ++index) {
            const auto& lowered = emitted_production.lowered_symbols[index];
            const auto& source_symbol = *lowered.source;
            const auto child_index = emitted_production.child_indices[index];
            if (source_symbol.is_zero_width()) {
               continue;
            }

            if (lowered.helper_id.has_value() && inline_target_field_for(source_symbol) != nullptr) {
               std::vector<std::unique_ptr<dynamic_node>> helper_nodes;
               collect_helper_nodes(helper_named(*lowered.helper_id), node_child_at(tree, *child_index), helper_nodes);
               for (auto& helper_node: helper_nodes) {
                  merge_inline_child_field(*node, source_symbol, std::move(helper_node));
               }
               continue;
            }

            if (lowered.helper_id.has_value() && !source_symbol.has_label()) {
               if (source_symbol.kind == symbol_kind::reference &&
                   structured_synthetic_rules.contains(source_symbol.value)) {
                  std::vector<std::unique_ptr<dynamic_node>> helper_nodes;
                  collect_helper_nodes(helper_named(*lowered.helper_id), node_child_at(tree, *child_index), helper_nodes);
                  for (auto& helper_node: helper_nodes) {
                     if (helper_node == nullptr) {
                        continue;
                     }
                     for (auto& [field_name, nested_field]: helper_node->fields) {
                        merge_dynamic_field(field_named(*node, field_name), std::move(nested_field));
                     }
                  }
               }
               continue;
            }

            if (inline_target_field_for(source_symbol) != nullptr) {
               merge_inline_child_field(*node, source_symbol, build_dynamic_node(node_child_at(tree, *child_index)));
               continue;
            }

            if (!source_symbol.has_label()) {
               if (source_symbol.kind == symbol_kind::reference &&
                   structured_synthetic_rules.contains(source_symbol.value)) {
                  auto structured_child = build_dynamic_node(node_child_at(tree, *child_index));
                  if (structured_child != nullptr) {
                     for (auto& [field_name, nested_field]: structured_child->fields) {
                        merge_dynamic_field(field_named(*node, field_name), std::move(nested_field));
                     }
                  }
               }
               continue;
            }

            auto& field = field_named(*node, source_symbol.label);
            const auto& declared_field = field_named(info.name, source_symbol.label);
            auto assigned_value = make_field(declared_field);

            if (lowered.helper_id.has_value()) {
               assign_helper_field(assigned_value, helper_named(*lowered.helper_id), node_child_at(tree, *child_index));
               merge_dynamic_field(field, std::move(assigned_value));
               continue;
            }

            if (source_symbol.kind == symbol_kind::reference) {
               if (auto capture_it = synthetic_capture_by_rule.find(source_symbol.value);
                   capture_it != synthetic_capture_by_rule.end()) {
                  assign_synthetic_capture_field(assigned_value, synthetic_captures[capture_it->second],
                                                 node_child_at(tree, *child_index));
               } else if (lexical_rules.contains(source_symbol.value)) {
                  assigned_value.value_kind = dynamic_field_value_kind::token;
                  assigned_value.token = matched_tree_at(node_child_at(tree, *child_index));
                  assigned_value.value_type_name = source_symbol.value;
               } else {
                  assigned_value.value_kind = dynamic_field_value_kind::node;
                  assigned_value.node = build_dynamic_node(node_child_at(tree, *child_index));
                  assigned_value.value_type_name =
                        assigned_value.node != nullptr ? assigned_value.node->rule_name : declared_field.resolved_rule;
               }
               merge_dynamic_field(field, std::move(assigned_value));
               continue;
            }

            assigned_value.value_kind = dynamic_field_value_kind::token;
            assigned_value.token = matched_child_at(tree, *child_index);
            auto declared_type_name = declared_type_name_of(declared_field);
            assigned_value.value_type_name = declared_type_name.empty() ? "cpf::matched_string" : declared_type_name;
            merge_dynamic_field(field, std::move(assigned_value));
         }

         return node;
      }

      [[nodiscard]] auto validate_generated_tree(const detail::parse_node_ptr& tree) const -> bool {
         return detail::validate_parse_tree(tree, shared_model);
      }

      [[nodiscard]] auto recognize(std::size_t root_rule, std::string_view input) const -> recognize_result {
         recognize_result result;
         auto recognized = detail::earley_recognize(input, grammar_spec, root_rule);
         result.success = recognized.success;
         if (!recognized.success) {
            result.error = std::move(recognized.error);
         }
         return result;
      }

      [[nodiscard]] auto recognize(std::size_t root_rule, const token_sequence& tokens) const -> recognize_result {
         recognize_result result;
         auto recognized = detail::earley_recognize(tokens, grammar_spec, root_rule);
         result.success = recognized.success;
         if (!recognized.success) {
            result.error = std::move(recognized.error);
         }
         return result;
      }

      [[nodiscard]] auto parse_dynamic(const std::shared_ptr<const impl>& self, std::size_t root_rule,
                                       const token_sequence& tokens,
                                       const parse_options& options) const -> parse_result<dynamic_node> {
         return detail::parse_shared_forest<dynamic_node>(
                [&]() { return detail::earley_recognize(tokens, grammar_spec, root_rule); },
               [&](detail::forest_span_order order) {
                  return detail::earley_parse(tokens, grammar_spec, root_rule, options.allow_partial, order);
               },
               shared_model, root_rule, options, [this](const detail::parse_node_ptr& tree) {
                  return validate_generated_tree(tree);
               },
               [state = self](const detail::parse_node_ptr& tree) { return state->build_dynamic_node(tree); },
               [](const dynamic_node& root, std::vector<const node*>& damaged_nodes) {
                  visit_dynamic_recursive(root, [&](const dynamic_node& current) {
                     if (current.is_damaged()) {
                        damaged_nodes.push_back(&current);
                     }
                  });
               });
      }

      [[nodiscard]] auto parse_dynamic(const std::shared_ptr<const impl>& self, std::size_t root_rule,
                                       std::string_view input,
                                       const parse_options& options) const -> parse_result<dynamic_node> {
         auto tokens = detail::lex_input(input, grammar_spec);
         return parse_dynamic(self, root_rule, tokens, options);
      }

      [[nodiscard]] auto parse_cst(const std::shared_ptr<const impl>& self, std::size_t root_rule,
                                   const token_sequence& tokens,
                                   const parse_options& options) const -> parse_result<cst_node> {
         return detail::parse_shared_forest<cst_node>(
                [&]() { return detail::earley_recognize(tokens, grammar_spec, root_rule); },
               [&](detail::forest_span_order order) {
                  return detail::earley_parse(tokens, grammar_spec, root_rule, options.allow_partial, order);
               },
               shared_model, root_rule, options, [this](const detail::parse_node_ptr& tree) {
                  return validate_generated_tree(tree);
               },
               [state = self](const detail::parse_node_ptr& tree) { return state->build_cst_node(tree); },
               [](const cst_node& root, std::vector<const node*>& damaged_nodes) {
                  visit_cst_recursive(root, [&](const cst_node& current) {
                     if (current.is_damaged()) {
                        damaged_nodes.push_back(&current);
                     }
                  });
               });
      }

      [[nodiscard]] auto parse_cst(const std::shared_ptr<const impl>& self, std::size_t root_rule,
                                   std::string_view input,
                                   const parse_options& options) const -> parse_result<cst_node> {
         auto tokens = detail::lex_input(input, grammar_spec);
         return parse_cst(self, root_rule, tokens, options);
      }

      static auto build(const grammar& grammar_model) -> std::shared_ptr<const impl>;
   };

   [[nodiscard]] auto clone_dynamic_field(const dynamic_field& field) -> dynamic_field {
      dynamic_field copy;
      copy.name = field.name;
      copy.shape = field.shape;
      copy.value_kind = field.value_kind;
      copy.declared_type_name = field.declared_type_name;
      copy.value_type_name = field.value_type_name;
      copy.alternative_type_names = field.alternative_type_names;
      copy.token = field.token;
      copy.tokens = field.tokens;
      if (field.node != nullptr) {
         copy.node = field.node->clone();
      }
      for (const auto& child: field.nodes) {
         copy.nodes.push_back(child != nullptr ? child->clone() : nullptr);
      }
      return copy;
   }

   [[nodiscard]] auto clone_dynamic_node(const dynamic_node& node) -> std::unique_ptr<dynamic_node> {
      auto copy = std::make_unique<dynamic_node>();
      copy->production_index = node.production_index;
      copy->range = node.range;
      copy->rule = node.rule;
      copy->rule_name = node.rule_name;
      for (const auto& damage: node.damage()) {
         detail::add_damage(*copy, damage);
      }
      for (const auto& [field_name, field]: node.fields) {
         copy->fields.emplace(field_name, clone_dynamic_field(field));
      }
      return copy;
   }

   auto dynamic_field::clone() const -> dynamic_field { return clone_dynamic_field(*this); }

   auto dynamic_node::clone() const -> std::unique_ptr<dynamic_node> { return clone_dynamic_node(*this); }

   auto dynamic_node::get_field(std::string_view name) -> dynamic_field* {
      if (auto found = fields.find(std::string{name}); found != fields.end()) {
         return &found->second;
      }
      return nullptr;
   }

   auto dynamic_node::get_field(std::string_view name) const -> const dynamic_field* {
      if (auto found = fields.find(std::string{name}); found != fields.end()) {
         return &found->second;
      }
      return nullptr;
   }

   auto dynamic_node::get_token(std::string_view name) -> matched_string& {
      return get_field(name)->token.value();
   }

   auto dynamic_node::get_token(std::string_view name) const -> const matched_string& {
      return get_field(name)->token.value();
   }

   auto dynamic_node::get_tokens(std::string_view name) -> std::vector<matched_string>& {
      return get_field(name)->tokens;
   }

   auto dynamic_node::get_tokens(std::string_view name) const -> const std::vector<matched_string>& {
      return get_field(name)->tokens;
   }

   auto dynamic_node::get_node(std::string_view name) -> const dynamic_node& {
      return *get_field(name)->node;
   }

   auto dynamic_node::get_node(std::string_view name) const -> const dynamic_node& {
      return *get_field(name)->node;
   }

   auto dynamic_node::get_nodes(std::string_view name) -> std::vector<std::reference_wrapper<dynamic_node>> {
      auto& field = *get_field(name);
      std::vector<std::reference_wrapper<dynamic_node>> nodes;
      for (auto& node_ptr: field.nodes) {
         if (node_ptr != nullptr) {
            nodes.push_back(*node_ptr);
         }
      }
      return nodes;
   }

   auto dynamic_node::get_nodes(std::string_view name) const -> std::vector<std::reference_wrapper<const dynamic_node>> {
      const auto& field = *get_field(name);
      std::vector<std::reference_wrapper<const dynamic_node>> nodes;
      for (const auto& node_ptr: field.nodes) {
         if (node_ptr != nullptr) {
            nodes.push_back(*node_ptr);
         }
      }
      return nodes;
   }

   auto dynamic_node::source_text(std::string_view input) const -> std::string {
      if (range.begin.offset > range.end.offset || range.end.offset > input.size()) {
         throw std::out_of_range{"Dynamic node source range is outside the input"};
      }
      return std::string{input.substr(range.begin.offset, range.end.offset - range.begin.offset)};
   }

   auto dynamic_node::clone_node() const -> std::unique_ptr<node> { return clone_dynamic_node(*this); }

   auto operator<<(std::ostream& os, const dynamic_node& node) -> std::ostream& {
      write_dynamic_node(os, node, 0);
      return os;
   }

   auto parser::impl::build(const grammar& grammar_model) -> std::shared_ptr<const impl> {
      auto compiled = std::make_shared<impl>();
      compiled->source_grammar = grammar_model;
      compiled->analysis_result = analyze_grammar(compiled->source_grammar);

      std::unordered_map<std::string, std::vector<std::string>> children;
      std::unordered_map<std::string, std::string> bases;

      for (const auto& rule: compiled->source_grammar.rules) {
         compiled->rules_by_name[rule.identifier] = &rule;
      }

      for (const auto& rule: compiled->source_grammar.rules) {
         if (rule.synthetic) {
            continue;
         }
         compiled->public_rule_lookup.emplace(rule.identifier, compiled->public_rules.size());
         compiled->public_rules.push_back(dynamic_rule_info{compiled->public_rules.size(), rule.identifier,
                                                            rule.declared_as_token, false,
                                                            rule.productions.size()});
         if (!rule.is_choice_rule()) {
            continue;
         }
         compiled->public_rules.back().base_rule = true;
         for (const auto& production: rule.productions) {
            auto child = production.symbols.front().value;
            if (auto base_it = bases.find(child); base_it != bases.end() && base_it->second != rule.identifier) {
               throw std::runtime_error{"Rule '" + child + "' cannot inherit from both '" + base_it->second +
                                        "' and '" + rule.identifier + "'"};
            }
            bases[child] = rule.identifier;
            children[rule.identifier].push_back(child);
         }
      }

      std::unordered_set<std::string> protected_parser_rules;
      for (const auto& rule: compiled->source_grammar.rules) {
         if (rule.synthetic) {
            continue;
         }
         if (rule.is_choice_rule()) {
            protected_parser_rules.insert(rule.identifier);
         }
      }
      for (const auto& [child, base]: bases) {
         protected_parser_rules.insert(child);
         protected_parser_rules.insert(base);
      }

      std::unordered_set<std::string> token_reachable_rules;
      std::function<void(std::string_view)> mark_token_reachable = [&](std::string_view rule_name) {
         auto owned_rule_name = std::string{rule_name};
         if (!token_reachable_rules.insert(owned_rule_name).second) {
            return;
         }
         const auto* current_rule = compiled->rules_by_name.at(owned_rule_name);
         for (const auto& production: current_rule->productions) {
            for (const auto& symbol: production.symbols) {
               if (symbol.kind != symbol_kind::reference || !compiled->rules_by_name.contains(symbol.value)) {
                  continue;
               }
               mark_token_reachable(symbol.value);
            }
         }
      };
      for (const auto& rule: compiled->source_grammar.rules) {
         if (rule.declared_as_token) {
            mark_token_reachable(rule.identifier);
         }
      }

      auto changed = true;
      while (changed) {
         changed = false;
         for (const auto& rule: compiled->source_grammar.rules) {
            if (compiled->lexical_rules.contains(rule.identifier)) {
               continue;
            }
            if (!rule.declared_as_token && !token_reachable_rules.contains(rule.identifier)) {
               continue;
            }
            if (!rule.declared_as_token && !rule.synthetic && protected_parser_rules.contains(rule.identifier)) {
               continue;
            }
            if (rule.productions.empty()) {
               continue;
            }
            if (!std::all_of(rule.productions.begin(), rule.productions.end(), [&](const auto& production) {
                   return production_is_lexical(production, compiled->lexical_rules);
                })) {
               continue;
            }
            compiled->lexical_rules.insert(rule.identifier);
            changed = true;
         }
      }

      for (const auto& rule: compiled->source_grammar.rules) {
         if (!rule.declared_as_token) {
            continue;
         }
         if (protected_parser_rules.contains(rule.identifier)) {
            throw std::runtime_error{"Token rule '" + rule.identifier +
                                     "' cannot participate in choice-style inheritance"};
         }
         if (!compiled->lexical_rules.contains(rule.identifier)) {
            throw std::runtime_error{"Token rule '" + rule.identifier +
                                     "' must lower only to terminals or lexical rules"};
         }
      }

      for (const auto& rule: compiled->source_grammar.rules) {
         if (!rule.is_choice_rule()) {
            continue;
         }
         for (const auto& production: rule.productions) {
            const auto& child = production.symbols.front().value;
            if (compiled->lexical_rules.contains(child)) {
               throw std::runtime_error{"Choice rule '" + rule.identifier +
                                        "' cannot reference token-like rule '" + child + "'"};
            }
         }
      }

      std::vector<std::string> ordered_referenced_synthetic_rules;
      std::set<std::string> referenced_synthetic_rules;
      std::vector<std::string> ordered_labeled_synthetic_rules;
      std::set<std::string> labeled_synthetic_rules;
      for (const auto& rule: compiled->source_grammar.rules) {
         for (const auto& production: rule.productions) {
            for (const auto& symbol: production.symbols) {
               if (symbol.kind != symbol_kind::reference) {
                  continue;
               }
               if (auto rule_it = compiled->rules_by_name.find(symbol.value);
                   rule_it != compiled->rules_by_name.end() && rule_it->second->synthetic) {
                  if (referenced_synthetic_rules.insert(symbol.value).second) {
                     ordered_referenced_synthetic_rules.push_back(symbol.value);
                  }
                  if (symbol.has_label() && labeled_synthetic_rules.insert(symbol.value).second) {
                     ordered_labeled_synthetic_rules.push_back(symbol.value);
                  }
               }
            }
         }
      }

      std::set<std::string> captureful_synthetic_rules;
      auto changed_captureful_synthetic_rules = true;
      while (changed_captureful_synthetic_rules) {
         changed_captureful_synthetic_rules = false;
         for (const auto& rule_name: ordered_referenced_synthetic_rules) {
            if (captureful_synthetic_rules.contains(rule_name)) {
               continue;
            }

            const auto& synthetic_rule = *compiled->rules_by_name.at(rule_name);
            auto captureful = false;
            for (const auto& production: synthetic_rule.productions) {
               for (const auto& symbol: production.symbols) {
                  if (symbol.has_label() ||
                      (symbol.kind == symbol_kind::reference &&
                       captureful_synthetic_rules.contains(symbol.value))) {
                     captureful = true;
                     break;
                  }
               }
               if (captureful) {
                  break;
               }
            }

            if (captureful) {
               captureful_synthetic_rules.insert(rule_name);
               changed_captureful_synthetic_rules = true;
            }
         }
      }

      std::unordered_map<std::string, field_info> synthetic_capture_fields;
      std::unordered_map<std::string, std::vector<variant_alternative_info>> synthetic_capture_production_alternatives;
      std::set<std::string> structured_synthetic_rules;
      for (const auto& rule_name: ordered_referenced_synthetic_rules) {
         const auto& synthetic_rule = *compiled->rules_by_name.at(rule_name);
         auto simple_capture_rule = true;
         for (const auto& production: synthetic_rule.productions) {
            if (production.symbols.size() != 1 || !production.symbols.front().is_single()) {
               simple_capture_rule = false;
               break;
            }

            const auto& capture_symbol = production.symbols.front();
            if (capture_symbol.has_label()) {
               simple_capture_rule = false;
               break;
            }
            if (capture_symbol.kind == symbol_kind::reference) {
               auto rule_it = compiled->rules_by_name.find(capture_symbol.value);
               if (rule_it != compiled->rules_by_name.end() && rule_it->second->synthetic) {
                  simple_capture_rule = false;
                  break;
               }
            }
         }

         if (!simple_capture_rule &&
             (labeled_synthetic_rules.contains(rule_name) || captureful_synthetic_rules.contains(rule_name))) {
            structured_synthetic_rules.insert(rule_name);
            continue;
         }

         if (!labeled_synthetic_rules.contains(rule_name)) {
            continue;
         }

         std::vector<variant_alternative_info> alternatives;
         std::vector<variant_alternative_info> production_alternatives;
         std::set<std::string> seen_types;
         for (const auto& production: synthetic_rule.productions) {
            const auto& capture_symbol = production.symbols.front();
            auto field = field_from_symbol(capture_symbol, compiled->lexical_rules);
            variant_alternative_info alternative;
            alternative.node = is_node_field(field);
            alternative.lexical = is_lexical_reference(capture_symbol, compiled->lexical_rules);
            alternative.resolved_rule = field.resolved_rule;
            alternative.type = merged_field_type(field);
            production_alternatives.push_back(alternative);
            if (seen_types.insert(alternative.type).second) {
               alternatives.push_back(alternative);
            }
         }

         field_info merged_field;
         merged_field.name = "value";
         if (alternatives.size() == 1) {
            merged_field.variant_alternatives = alternatives;
            merged_field.type = alternatives.front().type;
            merged_field.resolved_rule = alternatives.front().resolved_rule;
            merged_field.shape = alternatives.front().node ? field_shape::node_scalar : field_shape::terminal_scalar;
         } else {
            merged_field.shape = field_shape::capture_variant;
            merged_field.variant_alternatives = alternatives;
            merged_field.type = "variant";
         }

         synthetic_capture_fields.emplace(rule_name, std::move(merged_field));
         synthetic_capture_production_alternatives.emplace(rule_name, std::move(production_alternatives));
      }

      for (const auto& rule_name: ordered_labeled_synthetic_rules) {
         if (!synthetic_capture_fields.contains(rule_name)) {
            continue;
         }
         synthetic_capture_info capture;
         capture.id = compiled->synthetic_captures.size();
         capture.rule_name = rule_name;
         capture.field = synthetic_capture_fields.at(rule_name);
         capture.production_alternatives = synthetic_capture_production_alternatives.at(rule_name);
         compiled->synthetic_capture_by_rule.emplace(rule_name, capture.id);
         compiled->synthetic_captures.push_back(std::move(capture));
      }
      compiled->structured_synthetic_rules.insert(structured_synthetic_rules.begin(), structured_synthetic_rules.end());

      for (const auto& rule: compiled->source_grammar.rules) {
         if (rule.synthetic && !structured_synthetic_rules.contains(rule.identifier)) {
            continue;
         }
         class_info info;
         info.name = rule.identifier;
         const auto has_choice_definitions = std::any_of(rule.productions.begin(), rule.productions.end(), [](const auto& production) {
            return production.is_direct_reference_choice();
         });
         const auto has_concrete_definitions = std::any_of(rule.productions.begin(), rule.productions.end(), [](const auto& production) {
            return !production.is_direct_reference_choice();
         });
         if (has_choice_definitions && has_concrete_definitions) {
            throw std::runtime_error{"Rule '" + rule.identifier + "' mixes choice-style and concrete definitions"};
         }

         info.base_rule = has_choice_definitions;
         if (auto base_it = bases.find(rule.identifier); base_it != bases.end()) {
            info.base = base_it->second;
         }
         compiled->classes.emplace(info.name, std::move(info));
      }

      const auto collect_rule_fields = [&](const rule& rule) {
         auto fields = std::vector<field_info>{};
         auto class_it = compiled->classes.find(rule.identifier);
         if (class_it == compiled->classes.end() || class_it->second.base_rule) {
            return fields;
         }

         std::unordered_map<std::string, field_info> resolved_fields;
         std::vector<std::string> field_order;
         std::vector<std::set<std::string>> labels_by_definition;

         const auto collect_symbol_fields = [&](const symbol& symbol) {
            auto collected = std::vector<field_info>{};
             if (const auto* nested_field = inline_target_field(symbol, compiled->rules_by_name,
                                                                compiled->lexical_rules, compiled->classes);
                 nested_field != nullptr) {
                auto field = apply_symbol_quantifier_to_field(rule.identifier, *nested_field, symbol);
                field.name = symbol.has_label() ? symbol.label : nested_field->name;
                collected.push_back(std::move(field));
                return collected;
             }

            if (symbol.has_label()) {
               collected.push_back(field_from_symbol(symbol, compiled->lexical_rules, synthetic_capture_fields));
               return collected;
            }

            if (symbol.kind != symbol_kind::reference || !structured_synthetic_rules.contains(symbol.value)) {
               return collected;
            }

            auto nested_class_it = compiled->classes.find(symbol.value);
            if (nested_class_it == compiled->classes.end() || nested_class_it->second.base_rule) {
               return collected;
            }

            collected.reserve(nested_class_it->second.fields.size());
            for (const auto& nested_field: nested_class_it->second.fields) {
               collected.push_back(apply_symbol_quantifier_to_field(rule.identifier, nested_field, symbol));
            }
            return collected;
         };

         const auto merge_field = [&](field_info field, bool repeated_assignment) {
            auto existing_it = resolved_fields.find(field.name);
            if (existing_it == resolved_fields.end()) {
               field_order.push_back(field.name);
               field.type = merged_field_type(field);
               resolved_fields.emplace(field.name, std::move(field));
               return;
            }
            auto& existing = existing_it->second;
            merge_field_resolution(rule.identifier, existing, field, bases, repeated_assignment);
         };

         for (const auto& production: rule.productions) {
            std::unordered_map<std::string, std::size_t> field_counts;
            std::set<std::string> labels_in_definition;
            for (const auto& symbol: production.symbols) {
               if (symbol.kind == symbol_kind::reference && !compiled->rules_by_name.contains(symbol.value)) {
                  throw std::runtime_error{"Rule '" + rule.identifier + "' references unknown rule '" + symbol.value + "'"};
               }

               auto symbol_fields = collect_symbol_fields(symbol);
               for (auto& field: symbol_fields) {
                  if (field.name == "user_data") {
                     throw std::runtime_error{"Rule '" + rule.identifier +
                                              "' cannot expose label 'user_data' because generated node templates reserve that member name"};
                  }
                  labels_in_definition.insert(field.name);
                  const auto occurrence = ++field_counts[field.name];
                  merge_field(std::move(field), occurrence > 1);
               }
            }
            labels_by_definition.push_back(std::move(labels_in_definition));
         }

         for (const auto& field_name: field_order) {
            auto& field = resolved_fields.at(field_name);
            auto missing_from_definition = false;
            for (const auto& labels_in_definition: labels_by_definition) {
               if (!labels_in_definition.contains(field_name)) {
                  missing_from_definition = true;
                  break;
               }
            }
            if (missing_from_definition && field.shape == field_shape::terminal_scalar) {
               field.shape = field_shape::terminal_optional;
               field.type = merged_field_type(field);
            }
         }

         fields.reserve(field_order.size());
         for (const auto& field_name: field_order) {
            fields.push_back(resolved_fields.at(field_name));
         }
         return fields;
      };

      auto changed_fields = true;
      while (changed_fields) {
         changed_fields = false;
         for (const auto& rule: compiled->source_grammar.rules) {
            auto class_it = compiled->classes.find(rule.identifier);
            if (class_it == compiled->classes.end() || class_it->second.base_rule) {
               continue;
            }

            auto next_fields = collect_rule_fields(rule);
            const auto same_layout = class_it->second.fields.size() == next_fields.size() &&
                                     std::equal(class_it->second.fields.begin(), class_it->second.fields.end(),
                                                next_fields.begin(), next_fields.end(),
                                                [](const auto& left, const auto& right) {
                                                   return field_layout_equal(left, right);
                                                });
            if (!same_layout) {
               class_it->second.fields = std::move(next_fields);
               changed_fields = true;
            }
         }
      }

      for (const auto& [name, info]: compiled->classes) {
         std::unordered_map<std::string, field_info> fields;
         for (const auto& field: info.fields) {
            fields.emplace(field.name, field);
         }
         compiled->fields_by_rule.emplace(name, std::move(fields));
      }

      for (const auto& rule: compiled->source_grammar.rules) {
         if (!rule.inline_requested || compiled->rule_has_inline_target(rule.identifier)) {
            continue;
         }

         auto diagnostic = grammar_diagnostic{};
         diagnostic.severity = grammar_diagnostic_severity::warning;
         diagnostic.code = grammar_diagnostic_code::ignored_inline_request;
         diagnostic.rule = rule.identifier;
         diagnostic.line = rule.inline_line;
         diagnostic.message = "Rule '" + rule.identifier +
                              "' requests [inline] but does not resolve to exactly one member; continuing without automatic inlining";
         compiled->analysis_result.diagnostics.push_back(std::move(diagnostic));
      }

      auto seen_inline_member_diagnostics = std::set<std::string>{};
      for (const auto& rule: compiled->source_grammar.rules) {
         for (const auto& production: rule.productions) {
            for (const auto& symbol: production.symbols) {
               if (!symbol.inline_requested) {
                  continue;
               }

               auto key = rule.identifier + ":" + std::to_string(production.line) + ":" + symbol.label + ":" +
                          std::to_string(static_cast<int>(symbol.kind)) + ":" + symbol.value;
               if (!seen_inline_member_diagnostics.insert(key).second) {
                  continue;
               }

               const auto valid_inline_target =
                     symbol.kind == symbol_kind::reference && compiled->rule_has_inline_target(symbol.value);
               if (valid_inline_target) {
                  continue;
               }

               auto diagnostic = grammar_diagnostic{};
               diagnostic.severity = grammar_diagnostic_severity::warning;
               diagnostic.code = grammar_diagnostic_code::ignored_inline_request;
               diagnostic.rule = rule.identifier;
               diagnostic.line = production.line;
               diagnostic.message = "Member '" + symbol.label + "' in rule '" + rule.identifier +
                                    "' requests [inline] for '" + render_symbol_debug(symbol) +
                                    "' but the referenced target does not resolve to exactly one member; continuing without inlining";
               compiled->analysis_result.diagnostics.push_back(std::move(diagnostic));
            }
         }
      }

      std::ranges::sort(compiled->analysis_result.diagnostics, [](const auto& lhs, const auto& rhs) {
         if (lhs.line != rhs.line) {
            return lhs.line < rhs.line;
         }
         if (lhs.severity != rhs.severity) {
            return lhs.severity == grammar_diagnostic_severity::error;
         }
         return lhs.message < rhs.message;
      });

      for (std::size_t index = 0; index < compiled->source_grammar.rules.size(); ++index) {
         compiled->rule_indices.emplace(compiled->source_grammar.rules[index].identifier, index);
         compiled->emitted_rule_names.push_back(compiled->source_grammar.rules[index].identifier);
      }

      for (const auto& rule: compiled->source_grammar.rules) {
         if (rule.synthetic) {
            continue;
         }
         if (compiled->public_rule_lookup.contains(rule.identifier)) {
            compiled->public_rules[compiled->public_rule_lookup.at(rule.identifier)].id =
                  compiled->rule_indices.at(rule.identifier);
         }
         if (!compiled->classes.contains(rule.identifier)) {
            continue;
         }
         if (compiled->public_rule_lookup.contains(rule.identifier)) {
            compiled->public_rules[compiled->public_rule_lookup.at(rule.identifier)].base_rule =
                  compiled->classes.at(rule.identifier).base_rule;
         }
      }

      for (const auto& rule: compiled->source_grammar.rules) {
         if (rule.synthetic) {
            continue;
         }
         if (!compiled->classes.at(rule.identifier).base_rule) {
            continue;
         }

         family_info family;
         family.name = rule.identifier;
         family.direct_children = children[rule.identifier];

         std::unordered_map<std::string, std::string> alias_to_group;
         std::unordered_map<std::string, std::size_t> group_line;
         std::unordered_map<std::string, precedence_rank> group_rank;
         std::unordered_map<std::string, std::set<std::string>> edges;

         for (const auto& child: family.direct_children) {
            const auto& child_info = compiled->classes.at(child);
            if (child_info.base_rule) {
               family.primary_children.push_back(child);
               continue;
            }

            auto has_primary_definition = false;
            const auto repeated_child_definitions = compiled->rules_by_name.at(child)->productions.size() > 1;
            for (const auto& production: compiled->rules_by_name.at(child)->productions) {
               if (is_infix_production(child_info, production)) {
                  family.expression_family = true;
                  infix_definition_info infix_definition;
                  infix_definition.source_production = &production;
                  infix_definition.child = child;
                  infix_definition.definition = production.definition;
                  infix_definition.group = precedence_label(child_info.name, production, repeated_child_definitions);
                  infix_definition.associativity = associativity_of(production);
                  infix_definition.left_label = production.symbols[0].label.empty() ? "left" : production.symbols[0].label;
                  infix_definition.right_label = production.symbols[2].label.empty() ? "right" : production.symbols[2].label;
                  family.infix_definitions.push_back(infix_definition);

                  register_group_alias(alias_to_group,
                                       precedence_alias(child_info.name, production, repeated_child_definitions),
                                       infix_definition.group, family.name);
                  group_line[infix_definition.group] =
                        std::min(group_line.contains(infix_definition.group) ? group_line[infix_definition.group]
                                                                             : production.line,
                                 production.line);
                  if (has_absolute_precedence_rank(production)) {
                     auto rank = precedence_rank_of(production);
                     if (!group_rank.contains(infix_definition.group) ||
                         (rank.explicit_value && !group_rank[infix_definition.group].explicit_value) ||
                         (rank.explicit_value == group_rank[infix_definition.group].explicit_value &&
                          (rank.value < group_rank[infix_definition.group].value ||
                           (rank.value == group_rank[infix_definition.group].value &&
                            rank.line < group_rank[infix_definition.group].line)))) {
                        group_rank[infix_definition.group] = rank;
                     }
                  }
               } else {
                  has_primary_definition = true;
               }
            }

            if (has_primary_definition) {
               family.primary_children.push_back(child);
            }
         }

         for (const auto& infix_definition: family.infix_definitions) {
            if (auto attribute = infix_definition.source_production->find_attribute("prec");
                attribute.has_value() && !attribute->numeric) {
               if (attribute->operation == attribute_operator::less_than) {
                  edges[infix_definition.group].insert(attribute->value);
               } else if (attribute->operation == attribute_operator::greater_than) {
                  edges[attribute->value].insert(infix_definition.group);
               }
            }
         }

         std::set<std::string> all_groups;
         for (const auto& infix_definition: family.infix_definitions) {
            all_groups.insert(infix_definition.group);
         }
         for (const auto& edge: edges) {
            all_groups.insert(edge.first);
            all_groups.insert(edge.second.begin(), edge.second.end());
         }

         std::unordered_map<std::string, std::set<std::string>> normalized_edges;
         for (const auto& group: all_groups) {
            normalized_edges[group];
         }
         for (const auto& [from, to_set]: edges) {
            auto normalized_from = alias_to_group.contains(from) ? alias_to_group[from] : from;
            for (const auto& raw_to: to_set) {
               auto normalized_to = alias_to_group.contains(raw_to) ? alias_to_group[raw_to] : raw_to;
               if (normalized_from != normalized_to) {
                  normalized_edges[normalized_from].insert(normalized_to);
               }
            }
         }

         std::vector<std::string> ranked_groups{all_groups.begin(), all_groups.end()};
         for (std::size_t i = 0; i < ranked_groups.size(); ++i) {
            for (std::size_t j = i + 1; j < ranked_groups.size(); ++j) {
               const auto& left = ranked_groups[i];
               const auto& right = ranked_groups[j];
               if (!group_rank.contains(left) || !group_rank.contains(right)) {
                  continue;
               }
               if (group_rank[left].value < group_rank[right].value) {
                  normalized_edges[left].insert(right);
               } else if (group_rank[right].value < group_rank[left].value) {
                  normalized_edges[right].insert(left);
               }
            }
         }

         std::unordered_map<std::string, int> indegree;
         for (const auto& group: all_groups) {
            indegree[group] = 0;
         }
         for (const auto& [from, to_set]: normalized_edges) {
            for (const auto& to: to_set) {
               ++indegree[to];
            }
         }

         std::vector<std::string> ordered_groups;
         while (ordered_groups.size() < all_groups.size()) {
            std::vector<std::string> ready;
            for (const auto& group: all_groups) {
               if (indegree[group] == 0 &&
                   std::find(ordered_groups.begin(), ordered_groups.end(), group) == ordered_groups.end()) {
                  ready.push_back(group);
               }
            }
            if (ready.empty()) {
               throw std::runtime_error{"Precedence cycle detected for family '" + family.name + "'"};
            }
            std::sort(ready.begin(), ready.end(), [&](const auto& left, const auto& right) {
               auto left_line = group_line.contains(left) ? group_line[left] : 0;
               auto right_line = group_line.contains(right) ? group_line[right] : 0;
               if (left_line != right_line) {
                  return left_line < right_line;
               }
               return left < right;
            });
            auto next = ready.front();
            ordered_groups.push_back(next);
            indegree[next] = -1;
            for (const auto& target: normalized_edges[next]) {
               --indegree[target];
            }
         }

         for (std::size_t index = 0; index < ordered_groups.size(); ++index) {
            for (auto& infix_definition: family.infix_definitions) {
               if (infix_definition.group == ordered_groups[index]) {
                  infix_definition.precedence = static_cast<int>(index + 1);
               }
            }
         }

         compiled->families.emplace(family.name, std::move(family));
      }

      const auto make_emitted_symbol = [&](const symbol& source_symbol) {
         emitted_symbol lowered_symbol;
         lowered_symbol.kind = source_symbol.kind;
         lowered_symbol.lookahead = source_symbol.lookahead;
         if (source_symbol.kind == symbol_kind::reference) {
            lowered_symbol.value = compiled->rule_indices.at(source_symbol.value);
            lowered_symbol.text = source_symbol.value;
         } else {
            lowered_symbol.text = source_symbol.value;
         }
         return lowered_symbol;
      };

      const auto create_helper = [&](const symbol& source_symbol, std::string_view owner_rule_name,
                                     std::size_t owner_definition, std::size_t lexer_precedence) {
         helper_info helper;
         helper.id = compiled->helpers.size();
         helper.rule_index = compiled->emitted_rule_names.size();
         helper.rule_name = "__cpf_internal_" + std::to_string(helper.id);
         helper.base_symbol = source_symbol;
         helper.owner_rule_name = std::string{owner_rule_name};
         helper.owner_definition = owner_definition;
         helper.lexer_precedence = lexer_precedence;
         helper.exact_count = source_symbol.exact_repetition;
         switch (source_symbol.quantifier) {
            case symbol_quantifier::optional:
               helper.kind = helper_kind::optional;
               break;
            case symbol_quantifier::zero_or_more:
               helper.kind = helper_kind::zero_or_more;
               break;
            case symbol_quantifier::one_or_more:
               helper.kind = helper_kind::one_or_more;
               break;
            case symbol_quantifier::exact:
               helper.kind = helper_kind::exact;
               break;
            case symbol_quantifier::one:
               break;
         }
         compiled->emitted_rule_names.push_back(helper.rule_name);
         compiled->helpers.push_back(helper);
         return helper.id;
      };

      for (const auto& rule: compiled->source_grammar.rules) {
         for (const auto& production: rule.productions) {
            emitted_production_info emitted_production;
            auto next_child_index = std::size_t{0};
            emitted_production.lhs = compiled->rule_indices.at(rule.identifier);
            emitted_production.lhs_name = rule.identifier;
            emitted_production.debug_text = render_production_debug(rule.identifier, production);
            emitted_production.lexer_precedence = production.line;
            if (!rule.synthetic || compiled->classes.contains(rule.identifier)) {
               emitted_production.source_rule = &rule;
               emitted_production.source_production = &production;
            }
            for (const auto& source_symbol: production.symbols) {
               lowered_symbol lowered;
               lowered.source = &source_symbol;
               if (uses_helper_rule(source_symbol)) {
                  auto helper_id = create_helper(source_symbol, rule.identifier, production.definition, production.line);
                  lowered.helper_id = helper_id;
                  emitted_production.symbols.push_back(
                        emitted_symbol{symbol_kind::reference, compiled->helpers[helper_id].rule_index,
                                       compiled->helpers[helper_id].rule_name, source_symbol.lookahead});
               } else {
                  emitted_production.symbols.push_back(make_emitted_symbol(source_symbol));
               }
               if (source_symbol.is_zero_width()) {
                  emitted_production.child_indices.push_back(std::nullopt);
               } else {
                  emitted_production.child_indices.push_back(next_child_index++);
               }
               emitted_production.lowered_symbols.push_back(lowered);
            }
            compiled->emitted_productions.push_back(std::move(emitted_production));
         }
      }

      for (auto& helper: compiled->helpers) {
         auto append_helper_production = [&](std::vector<emitted_symbol> symbols, std::string text) {
            helper.production_indices.push_back(compiled->emitted_productions.size());
            emitted_production_info emitted_production;
            emitted_production.lhs = helper.rule_index;
            emitted_production.lhs_name = helper.owner_rule_name;
            emitted_production.debug_text = std::move(text);
            emitted_production.lexer_precedence = helper.lexer_precedence;
            emitted_production.symbols = std::move(symbols);
            compiled->emitted_productions.push_back(std::move(emitted_production));
         };

         auto base_symbol = make_emitted_symbol(helper.base_symbol);
         auto helper_reference = emitted_symbol{symbol_kind::reference, helper.rule_index, helper.rule_name};
         auto helper_text = render_symbol_debug(helper.base_symbol);

         switch (helper.kind) {
            case helper_kind::optional:
               append_helper_production({}, helper.owner_rule_name + " -> /* optional empty */");
               append_helper_production({base_symbol}, helper.owner_rule_name + " -> " + helper_text + "?");
               break;
            case helper_kind::zero_or_more:
               append_helper_production({}, helper.owner_rule_name + " -> /* repetition empty */");
               append_helper_production({base_symbol, helper_reference}, helper.owner_rule_name + " -> " + helper_text + "*");
               break;
            case helper_kind::one_or_more:
               append_helper_production({base_symbol}, helper.owner_rule_name + " -> " + helper_text + "+");
               append_helper_production({base_symbol, helper_reference}, helper.owner_rule_name + " -> " + helper_text + "+");
               break;
            case helper_kind::exact: {
               std::vector<emitted_symbol> symbols;
               symbols.reserve(helper.exact_count);
               for (std::size_t index = 0; index < helper.exact_count; ++index) {
                  symbols.push_back(base_symbol);
               }
               append_helper_production(std::move(symbols), helper.owner_rule_name + " -> " + helper_text);
               break;
            }
         }
      }

      for (std::size_t emitted_index = 0; emitted_index < compiled->emitted_productions.size(); ++emitted_index) {
         const auto& emitted_production = compiled->emitted_productions[emitted_index];
         if (auto capture_it = compiled->synthetic_capture_by_rule.find(emitted_production.lhs_name);
             capture_it != compiled->synthetic_capture_by_rule.end()) {
            auto& capture = compiled->synthetic_captures[capture_it->second];
            capture.production_offsets.emplace(emitted_index, capture.production_indices.size());
            capture.production_indices.push_back(emitted_index);
         }
      }

      const auto lexer_symbol_key = [](symbol_kind kind, std::string_view text) {
         return std::to_string(static_cast<int>(kind)) + ":" + std::string{text};
      };

      std::vector<emitted_lexer_symbol_info> grammar_token_symbols;
      std::unordered_map<std::string, std::size_t> grammar_token_symbol_indices;
      std::vector<emitted_lexer_symbol_info> grammar_skip_symbols;
      std::unordered_map<std::string, std::size_t> grammar_skip_symbol_indices;

      const auto register_lexer_symbol = [&](std::vector<emitted_lexer_symbol_info>& symbols,
                                             std::unordered_map<std::string, std::size_t>& indices,
                                             symbol_kind kind, std::string_view text,
                                             std::size_t precedence) -> std::size_t {
         auto key = lexer_symbol_key(kind, text);
         if (auto existing = indices.find(key); existing != indices.end()) {
            symbols[existing->second].precedence = std::min(symbols[existing->second].precedence, precedence);
            return existing->second;
         }
         auto index = symbols.size();
         indices.emplace(std::move(key), index);
         symbols.push_back(emitted_lexer_symbol_info{kind, std::string{text}, precedence});
         return index;
      };

      for (const auto& production: compiled->emitted_productions) {
         for (const auto& symbol: production.symbols) {
            if (symbol.kind != symbol_kind::reference) {
               (void) register_lexer_symbol(grammar_token_symbols, grammar_token_symbol_indices, symbol.kind,
                                            symbol.text, production.lexer_precedence);
            }
         }
      }
      for (const auto& skip_rule: compiled->source_grammar.skip_rules) {
         (void) register_lexer_symbol(grammar_skip_symbols, grammar_skip_symbol_indices, skip_rule.kind,
                                      skip_rule.value, skip_rule.line);
      }

      std::vector<std::vector<std::size_t>> productions_by_rule(compiled->emitted_rule_names.size());
      for (std::size_t emitted_index = 0; emitted_index < compiled->emitted_productions.size(); ++emitted_index) {
         productions_by_rule[compiled->emitted_productions[emitted_index].lhs].push_back(emitted_index);
      }

      compiled->grammar_rule_production_offsets.resize(compiled->emitted_rule_names.size());
      compiled->grammar_rule_production_counts.resize(compiled->emitted_rule_names.size());
      compiled->grammar_rule_expected_labels_storage.resize(compiled->emitted_rule_names.size());
      for (std::size_t rule_index = 0; rule_index < productions_by_rule.size(); ++rule_index) {
         compiled->grammar_rule_production_offsets[rule_index] = compiled->grammar_rule_production_indices.size();
         compiled->grammar_rule_production_counts[rule_index] = productions_by_rule[rule_index].size();
         compiled->grammar_rule_production_indices.insert(compiled->grammar_rule_production_indices.end(),
                                                          productions_by_rule[rule_index].begin(),
                                                          productions_by_rule[rule_index].end());
      }
      for (const auto& rule: compiled->source_grammar.rules) {
         std::optional<std::string> expected_label;
         for (const auto& production: rule.productions) {
            auto attribute = production.find_attribute("error");
            if (!attribute.has_value()) {
               continue;
            }
            if (attribute->operation != attribute_operator::assign || attribute->numeric) {
               throw std::runtime_error{"Rule '" + rule.identifier + "' uses unsupported error annotation syntax"};
            }
            if (expected_label.has_value() && *expected_label != attribute->value) {
               throw std::runtime_error{"Rule '" + rule.identifier +
                                        "' uses conflicting error annotations across productions"};
            }
            expected_label = attribute->value;
         }
         if (expected_label.has_value()) {
            compiled->grammar_rule_expected_labels_storage[compiled->rule_indices.at(rule.identifier)] = *expected_label;
         }
      }
      compiled->grammar_rule_expected_labels.reserve(compiled->grammar_rule_expected_labels_storage.size());
      for (const auto& label: compiled->grammar_rule_expected_labels_storage) {
         compiled->grammar_rule_expected_labels.push_back(label);
      }

      compiled->token_symbol_texts.reserve(grammar_token_symbols.size());
      compiled->token_symbols.reserve(grammar_token_symbols.size());
      compiled->token_regexes.reserve(grammar_token_symbols.size());
      for (const auto& symbol: grammar_token_symbols) {
         compiled->token_symbol_texts.push_back(symbol.text);
         const auto* compiled_regex = static_cast<const std::regex*>(nullptr);
         if (symbol.kind == symbol_kind::regex) {
            compiled->token_regexes.emplace_back(symbol.text);
            compiled_regex = &compiled->token_regexes.back();
         }
         compiled->token_symbols.push_back(detail::lexer_symbol_spec{
               symbol.kind == symbol_kind::literal ? detail::lexer_symbol_kind::literal : detail::lexer_symbol_kind::regex,
               compiled->token_symbol_texts.back(), compiled_regex, symbol.precedence});
      }

      compiled->skip_symbol_texts.reserve(grammar_skip_symbols.size());
      compiled->skip_symbols.reserve(grammar_skip_symbols.size());
      compiled->skip_regexes.reserve(grammar_skip_symbols.size());
      for (const auto& symbol: grammar_skip_symbols) {
         compiled->skip_symbol_texts.push_back(symbol.text);
         const auto* compiled_regex = static_cast<const std::regex*>(nullptr);
         if (symbol.kind == symbol_kind::regex) {
            compiled->skip_regexes.emplace_back(symbol.text);
            compiled_regex = &compiled->skip_regexes.back();
         }
         compiled->skip_symbols.push_back(detail::lexer_symbol_spec{
               symbol.kind == symbol_kind::literal ? detail::lexer_symbol_kind::literal : detail::lexer_symbol_kind::regex,
               compiled->skip_symbol_texts.back(), compiled_regex, symbol.precedence});
      }

      compiled->production_symbols.reserve(compiled->emitted_productions.size());
      compiled->production_symbol_texts.reserve(compiled->emitted_productions.size());
      compiled->production_lhs_names.reserve(compiled->emitted_productions.size());
      compiled->production_debug_texts.reserve(compiled->emitted_productions.size());
      compiled->production_specs.reserve(compiled->emitted_productions.size());
      compiled->production_metadata.resize(compiled->emitted_productions.size());
      compiled->production_validation_constraints.resize(compiled->emitted_productions.size());

      for (std::size_t emitted_index = 0; emitted_index < compiled->emitted_productions.size(); ++emitted_index) {
         const auto& production = compiled->emitted_productions[emitted_index];
         auto& metadata = compiled->production_metadata[emitted_index];
         compiled->production_lhs_names.push_back(production.lhs_name);
         compiled->production_debug_texts.push_back(production.debug_text);
         compiled->production_symbol_texts.emplace_back();
         compiled->production_symbol_texts.back().reserve(production.symbols.size());
         compiled->production_symbols.emplace_back();
         compiled->production_symbols.back().reserve(production.symbols.size());

         for (const auto& symbol: production.symbols) {
            compiled->production_symbol_texts.back().push_back(symbol.text);
         }
         for (std::size_t symbol_index = 0; symbol_index < production.symbols.size(); ++symbol_index) {
            const auto& symbol = production.symbols[symbol_index];
            auto kind = detail::parser_symbol_kind::terminal;
            if (symbol.kind == symbol_kind::reference) {
               kind = detail::parser_symbol_kind::nonterminal;
            }
            if (symbol.lookahead == lookahead_kind::positive) {
               kind = symbol.kind == symbol_kind::reference ? detail::parser_symbol_kind::positive_nonterminal
                                                            : detail::parser_symbol_kind::positive_terminal;
            } else if (symbol.lookahead == lookahead_kind::negative) {
               kind = symbol.kind == symbol_kind::reference ? detail::parser_symbol_kind::negative_nonterminal
                                                            : detail::parser_symbol_kind::negative_terminal;
            }

            auto value = symbol.value;
            if (symbol.kind != symbol_kind::reference) {
               const auto& indices = symbol.kind == symbol_kind::literal || symbol.kind == symbol_kind::regex
                                           ? grammar_token_symbol_indices
                                           : grammar_token_symbol_indices;
               value = indices.at(lexer_symbol_key(symbol.kind, symbol.text));
            }
            compiled->production_symbols.back().push_back(
                  detail::parser_symbol{kind, value, compiled->production_symbol_texts.back()[symbol_index]});
         }

         if (production.source_rule != nullptr) {
            metadata.has_source_rule = true;
            metadata.synthetic = production.source_rule->synthetic;
            metadata.rule_name = production.source_rule->identifier;
            metadata.rule_id = compiled->rule_indices.at(production.source_rule->identifier);
         }
         if (production.source_production != nullptr) {
            metadata.definition = production.source_production->definition;
         }
         if (production.source_rule != nullptr && production.source_production != nullptr &&
             compiled->classes.contains(production.source_rule->identifier)) {
            const auto& info = compiled->classes.at(production.source_rule->identifier);
            if (info.base_rule) {
               metadata.precedence_passthrough = true;
            }
            if (!info.base.empty()) {
               if (auto family_it = compiled->families.find(info.base); family_it != compiled->families.end()) {
                  for (const auto& infix_definition: family_it->second.infix_definitions) {
                     if (infix_definition.child != info.name ||
                         infix_definition.definition != production.source_production->definition) {
                        continue;
                     }
                     metadata.precedence = infix_definition.precedence;
                     compiled->production_validation_constraints[emitted_index].push_back(
                           detail::validation_constraint_spec{infix_definition.precedence,
                                                              infix_definition.associativity == "left", 0, 2});
                  }
               }
            }
         }
         metadata.validation_constraints = compiled->production_validation_constraints[emitted_index].data();
         metadata.validation_constraint_count = compiled->production_validation_constraints[emitted_index].size();

         compiled->production_specs.push_back(detail::production_spec{production.lhs, compiled->production_lhs_names.back(),
                                                                      compiled->production_debug_texts.back(),
                                                                      compiled->production_symbols.back().data(),
                                                                      compiled->production_symbols.back().size()});
      }

      compiled->grammar_spec.productions = compiled->production_specs.data();
      compiled->grammar_spec.production_count = compiled->production_specs.size();
      compiled->grammar_spec.rule_count = compiled->emitted_rule_names.size();
      compiled->grammar_spec.rule_expected_labels = compiled->grammar_rule_expected_labels.data();
      compiled->grammar_spec.rule_production_indices = compiled->grammar_rule_production_indices.data();
      compiled->grammar_spec.rule_production_offsets = compiled->grammar_rule_production_offsets.data();
      compiled->grammar_spec.rule_production_counts = compiled->grammar_rule_production_counts.data();
      compiled->grammar_spec.token_symbols = compiled->token_symbols.data();
      compiled->grammar_spec.token_symbol_count = compiled->token_symbols.size();
      compiled->grammar_spec.skip_symbols = compiled->skip_symbols.data();
      compiled->grammar_spec.skip_symbol_count = compiled->skip_symbols.size();
      compiled->grammar_spec.use_default_whitespace = !compiled->source_grammar.whitespace_rule.has_value();
      compiled->grammar_rule_names.reserve(compiled->emitted_rule_names.size());
      for (const auto& rule_name: compiled->emitted_rule_names) {
         compiled->grammar_rule_names.push_back(rule_name);
      }
      compiled->shared_model.grammar = compiled->grammar_spec;
      compiled->shared_model.production_metadata = compiled->production_metadata.data();
      compiled->shared_model.production_metadata_count = compiled->production_metadata.size();
      compiled->shared_model.rule_names = compiled->grammar_rule_names.data();

      return compiled;
   }

   parser::parser(std::shared_ptr<const impl> impl) : m_impl{std::move(impl)} {}

   parser::~parser() = default;

   auto parser::implementation() const -> const impl& {
      if (m_impl == nullptr) {
         throw std::runtime_error{"Compiled grammar is empty"};
      }
      return *m_impl;
   }

   auto parser::empty() const -> bool { return m_impl == nullptr; }

   auto parser::source_grammar() const -> const grammar& { return implementation().source_grammar; }

   auto parser::analysis() const -> const grammar_analysis& { return implementation().analysis_result; }

   auto parser::primary_entry_rule() const -> std::string_view {
      return implementation().analysis_result.summary.primary_entry_rule;
   }

   auto parser::rules() const -> std::span<const dynamic_rule_info> {
      if (m_impl == nullptr) {
         return {};
      }
      return {m_impl->public_rules.data(), m_impl->public_rules.size()};
   }

   auto parser::find_rule(std::string_view name) const -> const dynamic_rule_info* {
      if (m_impl == nullptr) {
         return nullptr;
      }
      return m_impl->rule_info(name);
   }

   auto parser::lex(std::string_view input) const -> token_sequence {
      return detail::lex_input(input, implementation().grammar_spec);
   }

   auto parser::recognize(std::string_view input) const -> recognize_result {
      const auto& state = implementation();
      return state.recognize(state.resolve_root_rule({}), input);
   }

   auto parser::recognize(const token_sequence& tokens) const -> recognize_result {
      const auto& state = implementation();
      return state.recognize(state.resolve_root_rule({}), tokens);
   }

   auto parser::recognize(std::string_view root_rule, std::string_view input) const -> recognize_result {
      const auto& state = implementation();
      return state.recognize(state.resolve_root_rule(root_rule), input);
   }

   auto parser::recognize(std::string_view root_rule, const token_sequence& tokens) const -> recognize_result {
      const auto& state = implementation();
      return state.recognize(state.resolve_root_rule(root_rule), tokens);
   }

   auto parser::parse(std::string_view input, const parse_options& options) const -> dynamic_parse_result {
      const auto& state = implementation();
      return state.parse_dynamic(m_impl, state.resolve_root_rule({}), input, options);
   }

   auto parser::parse(const token_sequence& tokens, const parse_options& options) const -> dynamic_parse_result {
      const auto& state = implementation();
      return state.parse_dynamic(m_impl, state.resolve_root_rule({}), tokens, options);
   }

   auto parser::parse(std::string_view root_rule, std::string_view input,
                                const parse_options& options) const -> dynamic_parse_result {
      const auto& state = implementation();
      return state.parse_dynamic(m_impl, state.resolve_root_rule(root_rule), input, options);
   }

   auto parser::parse(std::string_view root_rule, const token_sequence& tokens,
                                const parse_options& options) const -> dynamic_parse_result {
      const auto& state = implementation();
      return state.parse_dynamic(m_impl, state.resolve_root_rule(root_rule), tokens, options);
   }

   auto parser::parse_cst(std::string_view input, const parse_options& options) const -> cst_parse_result {
      const auto& state = implementation();
      return state.parse_cst(m_impl, state.resolve_root_rule({}), input, options);
   }

   auto parser::parse_cst(const token_sequence& tokens, const parse_options& options) const -> cst_parse_result {
      const auto& state = implementation();
      return state.parse_cst(m_impl, state.resolve_root_rule({}), tokens, options);
   }

   auto parser::parse_cst(std::string_view root_rule, std::string_view input,
                                    const parse_options& options) const -> cst_parse_result {
      const auto& state = implementation();
      return state.parse_cst(m_impl, state.resolve_root_rule(root_rule), input, options);
   }

   auto parser::parse_cst(std::string_view root_rule, const token_sequence& tokens,
                                    const parse_options& options) const -> cst_parse_result {
      const auto& state = implementation();
      return state.parse_cst(m_impl, state.resolve_root_rule(root_rule), tokens, options);
   }

   auto compile_grammar(const grammar& grammar) -> parser { return parser{parser::impl::build(grammar)}; }

   auto compile_grammar(std::string_view source) -> parser { return compile_grammar(parse_grammar(source)); }

   auto compile_grammar(const loaded_grammar& grammar) -> parser { return compile_grammar(grammar.parsed_grammar); }

   auto compile_grammar_file(const std::filesystem::path& path) -> parser {
      return compile_grammar(load_grammar_file(path));
   }
} // namespace cpf









#include "code_generator.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace cpf {
   namespace {
      enum class field_shape {
         terminal_scalar,
         terminal_optional,
         terminal_vector,
         node_scalar,
         node_vector
      };

      struct field_info {
         std::string name;
         std::string type;
         std::string resolved_rule;
         field_shape shape = field_shape::terminal_scalar;
      };

      struct class_info {
         std::string name;
         std::string base;
         bool base_rule = false;
         std::vector<field_info> fields;
      };

      enum class helper_kind {
         optional,
         zero_or_more,
         one_or_more,
         exact
      };

      struct helper_info {
         std::size_t id = 0;
         std::size_t rule_index = 0;
         std::string rule_name;
         symbol base_symbol;
         std::string owner_rule_name;
         std::size_t owner_definition = 0;
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
      };

      struct emitted_production_info {
         std::size_t lhs = 0;
         std::string lhs_name;
         std::string debug_text;
         std::vector<emitted_symbol> symbols;
         const rule* source_rule = nullptr;
         const production* source_production = nullptr;
         std::vector<lowered_symbol> lowered_symbols;
      };

      struct infix_definition_info {
         const production* production = nullptr;
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

      [[nodiscard]] std::string cpp_string_literal(std::string_view value) {
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
         return std::string{"\""} + escaped + "\"";
      }

      void line(std::ostringstream& stream, int indent, const std::string& text = {}) {
         stream << std::string(static_cast<std::size_t>(indent) * 3, ' ') << text << '\n';
      }

      [[nodiscard]] bool is_valid_cpp_identifier(std::string_view value) {
         if (value.empty()) {
            return false;
         }
         if (std::isalpha(static_cast<unsigned char>(value.front())) == 0 && value.front() != '_') {
            return false;
         }
         for (std::size_t i = 1; i < value.size(); ++i) {
            const auto ch = value[i];
            if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_') {
               return false;
            }
         }
         return true;
      }

      [[nodiscard]] bool is_valid_cpp_namespace(std::string_view value) {
         if (value.empty()) {
            return true;
         }

         std::size_t offset = 0;
         while (offset < value.size()) {
            const auto separator = value.find("::", offset);
            const auto segment_length = separator == std::string_view::npos ? value.size() - offset : separator - offset;
            if (!is_valid_cpp_identifier(value.substr(offset, segment_length))) {
               return false;
            }
            if (separator == std::string_view::npos) {
               return true;
            }
            offset = separator + 2;
            if (offset == value.size()) {
               return false;
            }
         }
         return true;
      }

      void validate_cpp_namespace(std::string_view value) {
         if (is_valid_cpp_namespace(value)) {
            return;
         }
         throw std::runtime_error{"Invalid C++ namespace '" + std::string{value} + "'"};
      }

      [[nodiscard]] bool is_infix_production(const class_info& info, const production& production) {
         if (info.base.empty()) {
            return false;
         }
         const auto& symbols = production.symbols;
         return symbols.size() == 3
             && symbols[0].is_single()
             && symbols[1].is_single()
             && symbols[2].is_single()
             && symbols[0].kind == symbol_kind::reference
             && symbols[2].kind == symbol_kind::reference
             && symbols[0].value == info.base
             && symbols[2].value == info.base
             && symbols[1].kind != symbol_kind::reference;
      }

      [[nodiscard]] std::string default_precedence_alias(std::string_view rule_name, const production& production, bool repeated_definition) {
         if (!repeated_definition) {
            return std::string{rule_name};
         }
         return std::string{rule_name} + "#" + std::to_string(production.definition);
      }

      [[nodiscard]] std::string precedence_label(std::string_view rule_name, const production& production, bool repeated_definition) {
         if (auto attribute = production.find_attribute("prec"); attribute.has_value() && attribute->operation == attribute_operator::assign && !attribute->numeric) {
            return attribute->value;
         }
         if (auto attribute = production.find_attribute("lbl"); attribute.has_value()) {
            return attribute->value;
         }
         return default_precedence_alias(rule_name, production, repeated_definition);
      }

      [[nodiscard]] std::string precedence_alias(std::string_view rule_name, const production& production, bool repeated_definition) {
         if (auto attribute = production.find_attribute("lbl"); attribute.has_value()) {
            return attribute->value;
         }
         return default_precedence_alias(rule_name, production, repeated_definition);
      }

      [[nodiscard]] std::string associativity_of(const production& production) {
         if (auto attribute = production.find_attribute("dir"); attribute.has_value()) {
            return attribute->value;
         }
         return "left";
      }

      [[nodiscard]] bool has_absolute_precedence_rank(const production& production) {
         if (auto attribute = production.find_attribute("prec"); attribute.has_value()) {
            return attribute->numeric;
         }
         return true;
      }

      [[nodiscard]] precedence_rank precedence_rank_of(const production& production) {
         precedence_rank rank;
         rank.line = production.line;
         rank.value = static_cast<int>(production.line);
         if (auto attribute = production.find_attribute("prec"); attribute.has_value() && attribute->numeric) {
            rank.explicit_value = true;
            rank.value = std::stoi(attribute->value);
         }
         return rank;
      }

      [[nodiscard]] std::vector<std::string> collect_concrete_descendants(
         std::string_view base,
         const std::unordered_map<std::string, std::vector<std::string>>& children,
         const std::unordered_map<std::string, class_info>& classes) {
         std::vector<std::string> result;
         std::function<void(std::string_view)> visit = [&](std::string_view current) {
            auto child_it = children.find(std::string{current});
            if (child_it == children.end()) {
               return;
            }
            for (const auto& child : child_it->second) {
               auto class_it = classes.find(child);
               if (class_it == classes.end()) {
                  continue;
               }
               if (class_it->second.base_rule) {
                  visit(child);
               } else {
                  result.push_back(child);
               }
            }
         };
         visit(base);
         return result;
      }

      [[nodiscard]] std::string render_field_declaration(const field_info& field) {
         return field.type + " " + field.name + ";";
      }

      [[nodiscard]] std::string render_child_clone_expression(const field_info& field) {
         return "copy->" + field.name + " = " + field.name + " ? " + field.name + "->clone() : nullptr;";
      }

      [[nodiscard]] bool is_node_field(const field_info& field) {
         return field.shape == field_shape::node_scalar || field.shape == field_shape::node_vector;
      }

      [[nodiscard]] bool uses_helper_rule(const symbol& symbol) {
         return !symbol.is_single();
      }

      [[nodiscard]] field_info field_from_symbol(const symbol& symbol) {
         field_info field;
         field.name = symbol.label;
         if (symbol.kind == symbol_kind::reference) {
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


      [[nodiscard]] bool field_shapes_compatible(field_shape existing, field_shape candidate) {
         if (existing == candidate) {
            return true;
         }
         if ((existing == field_shape::terminal_scalar && candidate == field_shape::terminal_optional)
          || (existing == field_shape::terminal_optional && candidate == field_shape::terminal_scalar)) {
            return true;
         }
         if ((existing == field_shape::node_scalar && candidate == field_shape::node_scalar)
          || (existing == field_shape::node_vector && candidate == field_shape::node_vector)) {
            return true;
         }
         return false;
      }

      [[nodiscard]] std::string merged_field_type(const field_info& field) {
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
         }
         return {};
      }

      [[nodiscard]] std::string describe_field_type(const field_info& field) {
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
         }
         return {};
      }

      [[nodiscard]] std::optional<std::string> common_rule_base(
         std::string_view left,
         std::string_view right,
         const std::unordered_map<std::string, std::string>& bases) {
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

      void register_group_alias(
         std::unordered_map<std::string, std::string>& alias_to_group,
         std::string_view alias,
         std::string_view group,
         std::string_view family_name) {
         if (alias.empty()) {
            return;
         }

         auto alias_text = std::string{alias};
         auto group_text = std::string{group};
         if (auto alias_it = alias_to_group.find(alias_text); alias_it != alias_to_group.end() && alias_it->second != group_text) {
            throw std::runtime_error{"Expression family '" + std::string{family_name} + "' cannot use precedence alias '" + alias_text + "' for both '" + alias_it->second + "' and '" + group_text + "'"};
         }
         alias_to_group[alias_text] = std::move(group_text);
      }

      [[nodiscard]] std::string render_symbol_debug(const symbol& symbol) {
         std::string text;
         if (symbol.kind == symbol_kind::reference) {
            text = symbol.value;
         } else if (symbol.kind == symbol_kind::literal) {
            text = "'" + symbol.value + "'";
         } else {
            text = "r'" + symbol.value + "'";
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
         }
         return text;
      }

      [[nodiscard]] std::string render_production_debug(std::string_view rule_name, const production& production) {
         std::string text = std::string{rule_name} + " ->";
         for (const auto& symbol : production.symbols) {
            text += " " + render_symbol_debug(symbol);
         }
         return text;
      }

      generated_code generate_code_impl(const grammar& grammar, const std::string& base_name, std::string_view code_namespace) {
         validate_cpp_namespace(code_namespace);

         std::unordered_map<std::string, const rule*> rules_by_name;
         std::unordered_map<std::string, std::vector<std::string>> children;
         std::unordered_map<std::string, std::string> bases;

         for (const auto& rule : grammar.rules) {
            rules_by_name[rule.identifier] = &rule;
         }

         for (const auto& rule : grammar.rules) {
            if (rule.synthetic) {
               continue;
            }
            if (!rule.is_choice_rule()) {
               continue;
            }
            for (const auto& production : rule.productions) {
               auto child = production.symbols.front().value;
               if (auto base_it = bases.find(child); base_it != bases.end() && base_it->second != rule.identifier) {
                  throw std::runtime_error{"Rule '" + child + "' cannot inherit from both '" + base_it->second + "' and '" + rule.identifier + "'"};
               }
               bases[child] = rule.identifier;
               children[rule.identifier].push_back(child);
            }
         }

         std::unordered_map<std::string, class_info> classes;
         for (const auto& rule : grammar.rules) {
            if (rule.synthetic) {
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

            if (!info.base_rule) {
               std::unordered_map<std::string, field_info> resolved_fields;
               std::vector<std::string> field_order;

               auto merge_field = [&](field_info field) {
                  auto existing_it = resolved_fields.find(field.name);
                  if (existing_it == resolved_fields.end()) {
                     field_order.push_back(field.name);
                     field.type = merged_field_type(field);
                     resolved_fields.emplace(field.name, std::move(field));
                     return;
                  }

                  auto& existing = existing_it->second;
                  auto existing_is_node = is_node_field(existing);
                  auto field_is_node = is_node_field(field);
                  if (existing_is_node != field_is_node) {
                     throw std::runtime_error{"Rule '" + rule.identifier + "' label '" + field.name + "' has conflicting member types '" + describe_field_type(existing) + "' and '" + describe_field_type(field) + "'"};
                  }

                  if (!field_shapes_compatible(existing.shape, field.shape)) {
                     throw std::runtime_error{"Rule '" + rule.identifier + "' label '" + field.name + "' has conflicting member types '" + describe_field_type(existing) + "' and '" + describe_field_type(field) + "'"};
                  }

                  if (!existing_is_node) {
                     if ((existing.shape == field_shape::terminal_scalar && field.shape == field_shape::terminal_optional)
                      || (existing.shape == field_shape::terminal_optional && field.shape == field_shape::terminal_scalar)) {
                        existing.shape = field_shape::terminal_optional;
                     }
                     existing.type = merged_field_type(existing);
                     return;
                  }

                  auto common_base = common_rule_base(existing.resolved_rule, field.resolved_rule, bases);
                  if (!common_base.has_value()) {
                     throw std::runtime_error{"Rule '" + rule.identifier + "' label '" + field.name + "' cannot resolve a common member type for rules '" + existing.resolved_rule + "' and '" + field.resolved_rule + "'"};
                  }

                  existing.resolved_rule = *common_base;
                  existing.type = merged_field_type(existing);
               };

               for (const auto& production : rule.productions) {
                  std::set<std::string> labels_in_definition;
                  for (const auto& symbol : production.symbols) {
                     if (symbol.kind == symbol_kind::reference && !rules_by_name.contains(symbol.value)) {
                        throw std::runtime_error{"Rule '" + rule.identifier + "' references unknown rule '" + symbol.value + "'"};
                     }
                     if (!symbol.has_label()) {
                        continue;
                     }
                     if (!labels_in_definition.insert(symbol.label).second) {
                        throw std::runtime_error{"Rule '" + rule.identifier + "' uses label '" + symbol.label + "' more than once in definition " + std::to_string(production.definition)};
                     }

                     auto field = field_from_symbol(symbol);
                     merge_field(std::move(field));
                  }

               }

               for (const auto& field_name : field_order) {
                  info.fields.push_back(resolved_fields.at(field_name));
               }
            }
            classes.emplace(info.name, std::move(info));
         }

         std::unordered_map<std::string, std::unordered_map<std::string, field_info>> fields_by_rule;
         for (const auto& [name, info] : classes) {
            std::unordered_map<std::string, field_info> fields;
            for (const auto& field : info.fields) {
               fields.emplace(field.name, field);
            }
            fields_by_rule.emplace(name, std::move(fields));
         }

         for (const auto& [name, info] : classes) {
            for (const auto& production : rules_by_name.at(name)->productions) {
               for (const auto& symbol : production.symbols) {
                  if (symbol.kind == symbol_kind::reference && !rules_by_name.contains(symbol.value)) {
                     throw std::runtime_error{"Rule '" + name + "' references unknown rule '" + symbol.value + "'"};
                  }
               }
            }
         }

         std::vector<std::string> ordered_public_rule_names;
         std::set<std::string> ordered_public_seen;
         std::function<void(std::string_view)> order_public_rule = [&](std::string_view name) {
            auto name_text = std::string{name};
            if (ordered_public_seen.contains(name_text) || !classes.contains(name_text)) {
               return;
            }
            const auto& info = classes.at(name_text);
            if (!info.base.empty()) {
               order_public_rule(info.base);
            }
            ordered_public_seen.insert(name_text);
            ordered_public_rule_names.push_back(name_text);
         };
         for (const auto& rule : grammar.rules) {
            if (rule.synthetic) {
               continue;
            }
            order_public_rule(rule.identifier);
         }

         std::unordered_map<std::string, family_info> families;
         for (const auto& rule : grammar.rules) {
            if (rule.synthetic) {
               continue;
            }
            if (!classes.at(rule.identifier).base_rule) {
               continue;
            }
            family_info family;
            family.name = rule.identifier;
            family.direct_children = children[rule.identifier];

            std::unordered_map<std::string, std::string> alias_to_group;
            std::unordered_map<std::string, std::size_t> group_line;
            std::unordered_map<std::string, precedence_rank> group_rank;
            std::unordered_map<std::string, std::set<std::string>> edges;

            for (const auto& child : family.direct_children) {
               const auto& child_info = classes.at(child);
               if (child_info.base_rule) {
                  family.primary_children.push_back(child);
                  continue;
               }

               auto has_primary_definition = false;
               const auto repeated_child_definitions = rules_by_name.at(child)->productions.size() > 1;
               for (const auto& production : rules_by_name.at(child)->productions) {
                  if (is_infix_production(child_info, production)) {
                     family.expression_family = true;

                     infix_definition_info infix_definition;
                     infix_definition.production = &production;
                     infix_definition.child = child;
                     infix_definition.definition = production.definition;
                     infix_definition.group = precedence_label(child_info.name, production, repeated_child_definitions);
                     infix_definition.associativity = associativity_of(production);
                     infix_definition.left_label = production.symbols[0].label.empty() ? "left" : production.symbols[0].label;
                     infix_definition.right_label = production.symbols[2].label.empty() ? "right" : production.symbols[2].label;
                     family.infix_definitions.push_back(infix_definition);

                     register_group_alias(alias_to_group, precedence_alias(child_info.name, production, repeated_child_definitions), infix_definition.group, family.name);
                     group_line[infix_definition.group] = std::min(group_line.contains(infix_definition.group) ? group_line[infix_definition.group] : production.line, production.line);
                     if (has_absolute_precedence_rank(production)) {
                        auto rank = precedence_rank_of(production);
                        if (!group_rank.contains(infix_definition.group)
                         || (rank.explicit_value && !group_rank[infix_definition.group].explicit_value)
                         || (rank.explicit_value == group_rank[infix_definition.group].explicit_value
                          && (rank.value < group_rank[infix_definition.group].value
                           || (rank.value == group_rank[infix_definition.group].value && rank.line < group_rank[infix_definition.group].line)))) {
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

            for (const auto& infix_definition : family.infix_definitions) {
               if (auto attribute = infix_definition.production->find_attribute("prec"); attribute.has_value() && !attribute->numeric) {
                  if (attribute->operation == attribute_operator::less_than) {
                     edges[infix_definition.group].insert(attribute->value);
                  } else if (attribute->operation == attribute_operator::greater_than) {
                     edges[attribute->value].insert(infix_definition.group);
                  }
               }
            }

            std::set<std::string> all_groups;
            for (const auto& infix_definition : family.infix_definitions) {
               all_groups.insert(infix_definition.group);
            }
            for (const auto& edge : edges) {
               all_groups.insert(edge.first);
               all_groups.insert(edge.second.begin(), edge.second.end());
            }

            std::unordered_map<std::string, std::set<std::string>> normalized_edges;
            for (const auto& group : all_groups) {
               normalized_edges[group];
            }
            for (const auto& [from, to_set] : edges) {
               auto normalized_from = alias_to_group.contains(from) ? alias_to_group[from] : from;
               for (const auto& raw_to : to_set) {
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
            for (const auto& group : all_groups) {
               indegree[group] = 0;
            }
            for (const auto& [from, to_set] : normalized_edges) {
               for (const auto& to : to_set) {
                  ++indegree[to];
               }
            }

            std::vector<std::string> ordered_groups;
            while (ordered_groups.size() < all_groups.size()) {
               std::vector<std::string> ready;
               for (const auto& group : all_groups) {
                  if (indegree[group] == 0 && std::find(ordered_groups.begin(), ordered_groups.end(), group) == ordered_groups.end()) {
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
               for (const auto& target : normalized_edges[next]) {
                  --indegree[target];
               }
            }

            for (std::size_t i = 0; i < ordered_groups.size(); ++i) {
               for (auto& infix_definition : family.infix_definitions) {
                  if (infix_definition.group == ordered_groups[i]) {
                     infix_definition.precedence = static_cast<int>(i + 1);
                  }
               }
            }

            families.emplace(family.name, std::move(family));
         }

         std::ostringstream header;
         line(header, 0, "#pragma once");
         line(header, 0);
         line(header, 0, "#include <cpflib>");
         line(header, 0, "#include <iosfwd>");
         line(header, 0, "#include <memory>");
         line(header, 0, "#include <optional>");
         line(header, 0, "#include <string>");
         line(header, 0, "#include <string_view>");
         line(header, 0, "#include <typeinfo>");
         line(header, 0, "#include <utility>");
         line(header, 0, "#include <vector>");
         line(header, 0);
         if (!code_namespace.empty()) {
            line(header, 0, "namespace " + std::string{code_namespace} + " {");
            line(header, 0);
         }

         for (const auto& rule : grammar.rules) {
            if (rule.synthetic) {
               continue;
            }
            line(header, 0, "struct " + rule.identifier + ";");
         }
         line(header, 0);

         for (const auto& rule_name : ordered_public_rule_names) {
            const auto& info = classes.at(rule_name);
            auto base = info.base.empty() ? "cpf::node" : info.base;
            line(header, 0, "struct " + info.name + " : " + base + " {");
            line(header, 1, "using parse_result = cpf::parse_result<" + info.name + ">;");
            for (const auto& field : info.fields) {
               line(header, 1, render_field_declaration(field));
            }
            if (!info.fields.empty()) {
               line(header, 0);
            }
            line(header, 1, "~" + info.name + "() override = default;");
            line(header, 1, "static parse_result parse(std::string_view input);");
            line(header, 1, "const std::type_info& type() const override;");
            line(header, 1, "std::unique_ptr<" + info.name + "> clone();");
            if (!info.base_rule) {
               line(header, 0);
               line(header, 1, "protected:");
               line(header, 1, "   std::unique_ptr<cpf::node> clone_node() const override;");
            }
            line(header, 0, "};");
            line(header, 0, "std::ostream& operator<<(std::ostream& os, const " + info.name + "& node);");
            line(header, 0);
         }

         for (const auto& rule : grammar.rules) {
            if (rule.synthetic) {
               continue;
            }
            const auto& info = classes.at(rule.identifier);
            if (info.base_rule) {
               auto concrete_descendants = collect_concrete_descendants(info.name, children, classes);
               line(header, 0, "template<typename Visitor>");
               line(header, 0, "auto visit(const " + info.name + "& node, Visitor&& visitor) {");
               for (const auto& descendant : concrete_descendants) {
                  line(header, 1, "if (auto* value = dynamic_cast<const " + descendant + "*>(&node)) {");
                  line(header, 2, "return visit(*value, visitor);");
                  line(header, 1, "}");
               }
               line(header, 1, "throw std::bad_cast{};");
               line(header, 0, "}");
               line(header, 0);

               line(header, 0, "template<typename Visitor>");
               line(header, 0, "void visit_recursive(const " + info.name + "& node, Visitor&& visitor) {");
               for (const auto& descendant : concrete_descendants) {
                  line(header, 1, "if (auto* value = dynamic_cast<const " + descendant + "*>(&node)) {");
                  line(header, 2, "visit_recursive(*value, visitor);");
                  line(header, 2, "return;");
                  line(header, 1, "}");
               }
               line(header, 1, "throw std::bad_cast{};");
               line(header, 0, "}");
               line(header, 0);
               continue;
            }

            line(header, 0, "template<typename Visitor>");
            line(header, 0, "auto visit(const " + info.name + "& node, Visitor&& visitor) {");
            line(header, 1, "return std::forward<Visitor>(visitor)(node);");
            line(header, 0, "}");
            line(header, 0);
            line(header, 0, "template<typename Visitor>");
            line(header, 0, "void visit_recursive(const " + info.name + "& node, Visitor&& visitor) {");
            line(header, 1, "std::forward<Visitor>(visitor)(node);");
            for (const auto& field : info.fields) {
               if (field.shape == field_shape::node_scalar) {
                  line(header, 1, "if (node." + field.name + ") {");
                  line(header, 2, "visit_recursive(*node." + field.name + ", visitor);");
                  line(header, 1, "}");
               } else if (field.shape == field_shape::node_vector) {
                  line(header, 1, "for (const auto& child : node." + field.name + ") {");
                  line(header, 2, "if (child) {");
                  line(header, 3, "visit_recursive(*child, visitor);");
                  line(header, 2, "}");
                  line(header, 1, "}");
               }
            }
            line(header, 0, "}");
            line(header, 0);
         }

         if (!code_namespace.empty()) {
            line(header, 0, "} // namespace " + std::string{code_namespace});
            line(header, 0);
         }

         std::unordered_map<std::string, std::size_t> rule_indices;
         for (std::size_t i = 0; i < grammar.rules.size(); ++i) {
             rule_indices.emplace(grammar.rules[i].identifier, i);
         }

         std::vector<std::string> emitted_rule_names;
         emitted_rule_names.reserve(grammar.rules.size());
         for (const auto& rule : grammar.rules) {
            emitted_rule_names.push_back(rule.identifier);
         }

         std::vector<helper_info> helpers;
         std::vector<emitted_production_info> emitted_productions;

         auto make_emitted_symbol = [&](const symbol& source_symbol) {
            emitted_symbol lowered_symbol;
            lowered_symbol.kind = source_symbol.kind;
            if (source_symbol.kind == symbol_kind::reference) {
               lowered_symbol.value = rule_indices.at(source_symbol.value);
               lowered_symbol.text = source_symbol.value;
            } else {
               lowered_symbol.text = source_symbol.value;
            }
            return lowered_symbol;
         };

         auto create_helper = [&](const symbol& source_symbol, std::string_view owner_rule_name, std::size_t owner_definition) {
            helper_info helper;
            helper.id = helpers.size();
            helper.rule_index = emitted_rule_names.size();
            helper.rule_name = "__cpf_internal_" + std::to_string(helper.id);
            helper.base_symbol = source_symbol;
            helper.owner_rule_name = std::string{owner_rule_name};
            helper.owner_definition = owner_definition;
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
            emitted_rule_names.push_back(helper.rule_name);
            helpers.push_back(helper);
            return helper.id;
         };

         for (const auto& rule : grammar.rules) {
            for (const auto& production : rule.productions) {
               emitted_production_info emitted_production;
               emitted_production.lhs = rule_indices.at(rule.identifier);
               emitted_production.lhs_name = rule.identifier;
               emitted_production.debug_text = render_production_debug(rule.identifier, production);
               if (!rule.synthetic) {
                  emitted_production.source_rule = &rule;
                  emitted_production.source_production = &production;
               }
               for (const auto& source_symbol : production.symbols) {
                  lowered_symbol lowered;
                  lowered.source = &source_symbol;
                  if (uses_helper_rule(source_symbol)) {
                     auto helper_id = create_helper(source_symbol, rule.identifier, production.definition);
                     lowered.helper_id = helper_id;
                     emitted_production.symbols.push_back(emitted_symbol{symbol_kind::reference, helpers[helper_id].rule_index, helpers[helper_id].rule_name});
                  } else {
                     emitted_production.symbols.push_back(make_emitted_symbol(source_symbol));
                  }
                  emitted_production.lowered_symbols.push_back(lowered);
               }
               emitted_productions.push_back(std::move(emitted_production));
            }
         }

         for (auto& helper : helpers) {
            auto append_helper_production = [&](std::vector<emitted_symbol> symbols, std::string text) {
               helper.production_indices.push_back(emitted_productions.size());
               emitted_production_info emitted_production;
               emitted_production.lhs = helper.rule_index;
               emitted_production.lhs_name = helper.owner_rule_name;
               emitted_production.debug_text = std::move(text);
               emitted_production.symbols = std::move(symbols);
               emitted_productions.push_back(std::move(emitted_production));
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
                  for (std::size_t i = 0; i < helper.exact_count; ++i) {
                     symbols.push_back(base_symbol);
                  }
                  append_helper_production(std::move(symbols), helper.owner_rule_name + " -> " + helper_text);
                  break;
               }
            }
         }

         std::ostringstream source;
         line(source, 0, "#include \"" + base_name + ".h\"");
         line(source, 0);
         line(source, 0, "#include <array>");
         line(source, 0, "#include <ostream>");
         line(source, 0, "#include <stdexcept>");
         line(source, 0, "#include <utility>");
         line(source, 0);
         if (!code_namespace.empty()) {
            line(source, 0, "namespace " + std::string{code_namespace} + " {");
            line(source, 0);
         }
         line(source, 0, "namespace {");
         line(source, 1, "using parse_node_ptr = cpf::detail::parse_node_ptr;");
         line(source, 0, "} // namespace");
         line(source, 0);

         auto production_index = 0;
         for (const auto& production : emitted_productions) {
            line(source, 0, "namespace {");
            line(source, 1, "constexpr std::array<cpf::detail::parser_symbol, " + std::to_string(production.symbols.size()) + "> production_" + std::to_string(production_index) + "_symbols{{");
            for (std::size_t i = 0; i < production.symbols.size(); ++i) {
               const auto& symbol = production.symbols[i];
               std::string rendered_symbol;
               if (symbol.kind == symbol_kind::reference) {
                  rendered_symbol = "{cpf::detail::parser_symbol_kind::nonterminal, " + std::to_string(symbol.value) + ", " + cpp_string_literal(symbol.text) + "}";
               } else if (symbol.kind == symbol_kind::literal) {
                  rendered_symbol = "{cpf::detail::parser_symbol_kind::literal, 0, " + cpp_string_literal(symbol.text) + "}";
               } else {
                  rendered_symbol = "{cpf::detail::parser_symbol_kind::regex, 0, " + cpp_string_literal(symbol.text) + "}";
               }
               if (i + 1 != production.symbols.size()) {
                  rendered_symbol += ",";
               }
               line(source, 2, rendered_symbol);
            }
            line(source, 1, "}};");
            line(source, 0, "} // namespace");
            line(source, 0);
            ++production_index;
         }

         const auto production_count = production_index;

         line(source, 0, "namespace {");
         line(source, 1, "constexpr std::array<cpf::detail::production_spec, " + std::to_string(production_count) + "> grammar_productions{{");
         production_index = 0;
         for (const auto& production : emitted_productions) {
            auto rendered_production = "{" + std::to_string(production.lhs)
               + ", " + cpp_string_literal(production.lhs_name)
               + ", " + cpp_string_literal(production.debug_text)
               + ", production_" + std::to_string(production_index) + "_symbols.data(), production_" + std::to_string(production_index) + "_symbols.size()}";
            if (production_index + 1 != production_count) {
               rendered_production += ",";
            }
            line(source, 2, rendered_production);
            ++production_index;
         }
         line(source, 1, "}};");
         line(source, 1, "constexpr cpf::detail::grammar_spec grammar_spec{grammar_productions.data(), grammar_productions.size(), " + std::to_string(emitted_rule_names.size()) + "};");
         line(source, 1, "constexpr std::array<std::string_view, " + std::to_string(emitted_rule_names.size()) + "> grammar_rule_names{{");
         for (std::size_t i = 0; i < emitted_rule_names.size(); ++i) {
            auto rendered_name = cpp_string_literal(emitted_rule_names[i]);
            if (i + 1 != emitted_rule_names.size()) {
               rendered_name += ",";
            }
            line(source, 2, rendered_name);
         }
         line(source, 1, "}};");
         line(source, 1, "std::unique_ptr<cpf::node> build_node(const parse_node_ptr& tree);");
         line(source, 0);

         for (const auto& helper : helpers) {
            auto helper_name = "helper_" + std::to_string(helper.id);
            if (helper.base_symbol.kind == symbol_kind::reference) {
               if (helper.kind == helper_kind::optional) {
                  line(source, 1, "template<typename T>");
                  line(source, 1, "std::unique_ptr<T> extract_" + helper_name + "(const parse_node_ptr& tree) {");
                  line(source, 2, "switch (tree->production) {");
                  line(source, 3, "case " + std::to_string(helper.production_indices[0]) + ":");
                  line(source, 4, "return nullptr;");
                  line(source, 3, "case " + std::to_string(helper.production_indices[1]) + ": {");
                  line(source, 4, "auto built = build_node(std::get<parse_node_ptr>(tree->children.front()));");
                  line(source, 4, "return std::unique_ptr<T>{static_cast<T*>(built.release())};");
                  line(source, 3, "}");
                  line(source, 3, "default:");
                  line(source, 4, "throw std::runtime_error{\"Unknown quantified helper production\"};");
                  line(source, 2, "}");
                  line(source, 1, "}");
               } else {
                  line(source, 1, "template<typename T>");
                  line(source, 1, "void collect_" + helper_name + "(const parse_node_ptr& tree, std::vector<std::unique_ptr<T>>& values) {");
                  line(source, 2, "switch (tree->production) {");
                  if (helper.kind == helper_kind::zero_or_more) {
                     line(source, 3, "case " + std::to_string(helper.production_indices[0]) + ":");
                     line(source, 4, "return;");
                     line(source, 3, "case " + std::to_string(helper.production_indices[1]) + ": {");
                     line(source, 4, "auto built = build_node(std::get<parse_node_ptr>(tree->children[0]));");
                     line(source, 4, "values.push_back(std::unique_ptr<T>{static_cast<T*>(built.release())});");
                     line(source, 4, "collect_" + helper_name + "(std::get<parse_node_ptr>(tree->children[1]), values);");
                     line(source, 4, "return;");
                     line(source, 3, "}");
                  } else if (helper.kind == helper_kind::one_or_more) {
                     line(source, 3, "case " + std::to_string(helper.production_indices[0]) + ": {");
                     line(source, 4, "auto built = build_node(std::get<parse_node_ptr>(tree->children[0]));");
                     line(source, 4, "values.push_back(std::unique_ptr<T>{static_cast<T*>(built.release())});");
                     line(source, 4, "return;");
                     line(source, 3, "}");
                     line(source, 3, "case " + std::to_string(helper.production_indices[1]) + ": {");
                     line(source, 4, "auto built = build_node(std::get<parse_node_ptr>(tree->children[0]));");
                     line(source, 4, "values.push_back(std::unique_ptr<T>{static_cast<T*>(built.release())});");
                     line(source, 4, "collect_" + helper_name + "(std::get<parse_node_ptr>(tree->children[1]), values);");
                     line(source, 4, "return;");
                     line(source, 3, "}");
                  } else {
                     line(source, 3, "case " + std::to_string(helper.production_indices[0]) + ":");
                     for (std::size_t i = 0; i < helper.exact_count; ++i) {
                        line(source, 4, "{ auto built = build_node(std::get<parse_node_ptr>(tree->children[" + std::to_string(i) + "])); values.push_back(std::unique_ptr<T>{static_cast<T*>(built.release())}); }");
                     }
                     line(source, 4, "return;");
                  }
                  line(source, 3, "default:");
                  line(source, 4, "throw std::runtime_error{\"Unknown quantified helper production\"};");
                  line(source, 2, "}");
                  line(source, 1, "}");
                  line(source, 1, "template<typename T>");
                  line(source, 1, "std::vector<std::unique_ptr<T>> extract_" + helper_name + "(const parse_node_ptr& tree) {");
                  line(source, 2, "std::vector<std::unique_ptr<T>> values;");
                  line(source, 2, "collect_" + helper_name + "(tree, values);");
                  line(source, 2, "return values;");
                  line(source, 1, "}");
               }
            } else if (helper.kind == helper_kind::optional) {
               line(source, 1, "std::optional<cpf::matched_string> extract_" + helper_name + "(const parse_node_ptr& tree) {");
               line(source, 2, "switch (tree->production) {");
               line(source, 3, "case " + std::to_string(helper.production_indices[0]) + ":");
               line(source, 4, "return std::nullopt;");
               line(source, 3, "case " + std::to_string(helper.production_indices[1]) + ":");
               line(source, 4, "return std::get<cpf::matched_string>(tree->children.front());");
               line(source, 3, "default:");
               line(source, 4, "throw std::runtime_error{\"Unknown quantified helper production\"};");
               line(source, 2, "}");
               line(source, 1, "}");
            } else {
               line(source, 1, "void collect_" + helper_name + "(const parse_node_ptr& tree, std::vector<cpf::matched_string>& values) {");
               line(source, 2, "switch (tree->production) {");
               if (helper.kind == helper_kind::zero_or_more) {
                  line(source, 3, "case " + std::to_string(helper.production_indices[0]) + ":");
                  line(source, 4, "return;");
                  line(source, 3, "case " + std::to_string(helper.production_indices[1]) + ":");
                  line(source, 4, "values.push_back(std::get<cpf::matched_string>(tree->children[0]));");
                  line(source, 4, "collect_" + helper_name + "(std::get<parse_node_ptr>(tree->children[1]), values);");
                  line(source, 4, "return;");
               } else if (helper.kind == helper_kind::one_or_more) {
                  line(source, 3, "case " + std::to_string(helper.production_indices[0]) + ":");
                  line(source, 4, "values.push_back(std::get<cpf::matched_string>(tree->children[0]));");
                  line(source, 4, "return;");
                  line(source, 3, "case " + std::to_string(helper.production_indices[1]) + ":");
                  line(source, 4, "values.push_back(std::get<cpf::matched_string>(tree->children[0]));");
                  line(source, 4, "collect_" + helper_name + "(std::get<parse_node_ptr>(tree->children[1]), values);");
                  line(source, 4, "return;");
               } else {
                  line(source, 3, "case " + std::to_string(helper.production_indices[0]) + ":");
                  for (std::size_t i = 0; i < helper.exact_count; ++i) {
                     line(source, 4, "values.push_back(std::get<cpf::matched_string>(tree->children[" + std::to_string(i) + "]));");
                  }
                  line(source, 4, "return;");
               }
               line(source, 3, "default:");
               line(source, 4, "throw std::runtime_error{\"Unknown quantified helper production\"};");
               line(source, 2, "}");
               line(source, 1, "}");
               line(source, 1, "std::vector<cpf::matched_string> extract_" + helper_name + "(const parse_node_ptr& tree) {");
               line(source, 2, "std::vector<cpf::matched_string> values;");
               line(source, 2, "collect_" + helper_name + "(tree, values);");
               line(source, 2, "return values;");
               line(source, 1, "}");
            }
            line(source, 0);
         }

         line(source, 0);

         for (const auto& rule : grammar.rules) {
            if (rule.synthetic) {
               continue;
            }
            const auto& info = classes.at(rule.identifier);
            auto family_it = families.find(rule.identifier);
            if (!info.base_rule || family_it == families.end() || !family_it->second.expression_family) {
               continue;
            }

            const auto& family = family_it->second;
            line(source, 1, "int precedence_of_" + family.name + "(const " + family.name + "& node) {");
             for (const auto& child : family.direct_children) {
                std::vector<const infix_definition_info*> child_definitions;
                for (const auto& infix_definition : family.infix_definitions) {
                   if (infix_definition.child == child) {
                      child_definitions.push_back(&infix_definition);
                   }
                }
                if (child_definitions.empty()) {
                   continue;
                }

                line(source, 2, "if (auto* value = dynamic_cast<const " + child + "*>(&node)) {");
                line(source, 3, "switch (value->definition) {");
                for (const auto* infix_definition : child_definitions) {
                   line(source, 4, "case " + std::to_string(infix_definition->definition) + ":");
                   line(source, 5, "return " + std::to_string(infix_definition->precedence) + ";");
                }
                line(source, 4, "default:");
                line(source, 5, "return 0;");
               line(source, 2, "}");
                line(source, 2, "}");
            }
            line(source, 2, "return 0;");
            line(source, 1, "}");
            line(source, 0);
            line(source, 1, "bool validate_" + family.name + "_child(const " + family.name + "& child, int precedence, bool left_associative, bool is_left_child) {");
            line(source, 2, "auto child_precedence = precedence_of_" + family.name + "(child);");
            line(source, 2, "if (child_precedence == 0) {");
            line(source, 3, "return true;");
            line(source, 2, "}");
            line(source, 2, "if (child_precedence < precedence) {");
            line(source, 3, "return false;");
            line(source, 2, "}");
            line(source, 2, "if (child_precedence > precedence) {");
            line(source, 3, "return true;");
            line(source, 2, "}");
            line(source, 2, "return is_left_child ? left_associative : !left_associative;");
            line(source, 1, "}");
            line(source, 0);
         }

         line(source, 1, "bool validate_generated_node(const cpf::node& node) {");
         for (const auto& rule : grammar.rules) {
            if (rule.synthetic) {
               continue;
            }
            const auto& info = classes.at(rule.identifier);
            if (info.base_rule) {
               continue;
            }
             std::vector<const infix_definition_info*> infix_definitions;
             if (!info.base.empty()) {
                if (auto family_it = families.find(info.base); family_it != families.end()) {
                   for (const auto& infix_definition : family_it->second.infix_definitions) {
                      if (infix_definition.child == info.name) {
                         infix_definitions.push_back(&infix_definition);
                      }
                   }
                }
             }

             auto needs_value = !infix_definitions.empty();
            for (const auto& field : info.fields) {
               if (is_node_field(field)) {
                  needs_value = true;
                  break;
               }
            }

            if (needs_value) {
               line(source, 2, "if (auto* value = dynamic_cast<const " + info.name + "*>(&node)) {");
            } else {
               line(source, 2, "if (dynamic_cast<const " + info.name + "*>(&node) != nullptr) {");
            }
            for (const auto& field : info.fields) {
               if (field.shape == field_shape::node_scalar) {
                  line(source, 3, "if (value->" + field.name + " && !validate_generated_node(*value->" + field.name + ")) {");
                  line(source, 4, "return false;");
                  line(source, 3, "}");
               } else if (field.shape == field_shape::node_vector) {
                  line(source, 3, "for (const auto& child : value->" + field.name + ") {");
                  line(source, 4, "if (child && !validate_generated_node(*child)) {");
                  line(source, 5, "return false;");
                  line(source, 4, "}");
                  line(source, 3, "}");
               }
            }

             if (!infix_definitions.empty()) {
                line(source, 3, "switch (value->definition) {");
                for (const auto* infix_definition : infix_definitions) {
                   const auto precedence = std::to_string(infix_definition->precedence);
                   const auto left_associative = infix_definition->associativity == "left" ? "true" : "false";

                   line(source, 4, "case " + std::to_string(infix_definition->definition) + ":");
                   line(source, 5, "if (value->" + infix_definition->left_label + " && !validate_" + info.base + "_child(*value->" + infix_definition->left_label + ", " + precedence + ", " + left_associative + ", true)) {");
                   line(source, 6, "return false;");
                   line(source, 5, "}");
                   line(source, 5, "if (value->" + infix_definition->right_label + " && !validate_" + info.base + "_child(*value->" + infix_definition->right_label + ", " + precedence + ", " + left_associative + ", false)) {");
                   line(source, 6, "return false;");
                   line(source, 5, "}");
                   line(source, 5, "break;");
                }
                line(source, 4, "default:");
                line(source, 5, "break;");
                line(source, 3, "}");
            }

            line(source, 3, "return true;");
            line(source, 2, "}");
         }
         line(source, 2, "return true;");
         line(source, 1, "}");
         line(source, 0);

         line(source, 1, "std::unique_ptr<cpf::node> build_node(const parse_node_ptr& tree) {");
         line(source, 2, "switch (tree->production) {");
         for (std::size_t emitted_index = 0; emitted_index < emitted_productions.size(); ++emitted_index) {
            const auto& emitted_production = emitted_productions[emitted_index];
            if (emitted_production.source_rule == nullptr || emitted_production.source_production == nullptr) {
               continue;
            }

            const auto& rule = *emitted_production.source_rule;
            const auto& production = *emitted_production.source_production;
            const auto& info = classes.at(rule.identifier);
            line(source, 3, "case " + std::to_string(emitted_index) + ": {");
            if (info.base_rule) {
               line(source, 4, "return build_node(std::get<parse_node_ptr>(tree->children.front()));");
            } else {
               line(source, 4, "auto node = std::make_unique<" + info.name + ">();");
               line(source, 4, "node->definition = " + std::to_string(production.definition) + ";");
               line(source, 4, "node->range = tree->range;");
               for (std::size_t i = 0; i < emitted_production.lowered_symbols.size(); ++i) {
                  const auto& lowered_symbol = emitted_production.lowered_symbols[i];
                  const auto& symbol = *lowered_symbol.source;
                  if (lowered_symbol.helper_id.has_value()) {
                     if (!symbol.has_label()) {
                        continue;
                     }
                     const auto& field = fields_by_rule.at(info.name).at(symbol.label);
                     const auto helper_name = "helper_" + std::to_string(*lowered_symbol.helper_id);
                     if (field.shape == field_shape::terminal_optional || field.shape == field_shape::terminal_vector) {
                        line(source, 4, "node->" + symbol.label + " = extract_" + helper_name + "(std::get<parse_node_ptr>(tree->children[" + std::to_string(i) + "]));");
                     } else if (field.shape == field_shape::node_scalar || field.shape == field_shape::node_vector) {
                        line(source, 4, "node->" + symbol.label + " = extract_" + helper_name + "<" + field.resolved_rule + ">(std::get<parse_node_ptr>(tree->children[" + std::to_string(i) + "]));");
                     }
                     continue;
                  }

                  if (symbol.kind == symbol_kind::reference) {
                     if (symbol.has_label()) {
                        const auto& field = fields_by_rule.at(info.name).at(symbol.label);
                        line(source, 4, "auto child_" + std::to_string(i) + " = build_node(std::get<parse_node_ptr>(tree->children[" + std::to_string(i) + "]));");
                        line(source, 4, "node->" + symbol.label + " = std::unique_ptr<" + field.resolved_rule + ">{static_cast<" + field.resolved_rule + "*>(child_" + std::to_string(i) + ".release())};");
                     }
                  } else if (symbol.has_label()) {
                     line(source, 4, "node->" + symbol.label + " = std::get<cpf::matched_string>(tree->children[" + std::to_string(i) + "]); ");
                  }
               }
               line(source, 4, "return node;");
            }
            line(source, 3, "}");
         }
         line(source, 3, "default:");
         line(source, 4, "throw std::runtime_error{\"Unknown parse production\"};");
         line(source, 2, "}");
         line(source, 1, "}");
         line(source, 0);

         line(source, 1, "template<typename T>");
         line(source, 1, "cpf::parse_result<T> parse_generated(std::string_view input, std::size_t root_rule) {");
         line(source, 2, "cpf::parse_result<T> result;");
         line(source, 2, "auto forest = cpf::detail::earley_parse(input, grammar_spec, root_rule);");
         line(source, 2, "if (!forest.success) {");
         line(source, 3, "result.error = std::move(forest.error);");
         line(source, 3, "return result;");
         line(source, 2, "}");
         line(source, 2, "for (const auto& tree : forest.forest) {");
         line(source, 3, "auto built = build_node(tree);");
         line(source, 3, "if (!validate_generated_node(*built)) {");
         line(source, 4, "continue;");
         line(source, 3, "}");
         line(source, 3, "if (dynamic_cast<T*>(built.get()) == nullptr) {");
         line(source, 4, "continue;");
         line(source, 3, "}");
         line(source, 3, "result.forest.push_back(std::unique_ptr<T>{static_cast<T*>(built.release())});");
         line(source, 2, "}");
         line(source, 2, "if (!result.forest.empty()) {");
         line(source, 3, "result.success = true;");
         line(source, 3, "return result;");
         line(source, 2, "}");
         line(source, 2, "result.error.expected.push_back(\"valid parse tree\");");
         line(source, 2, "result.error.found = \"<filtered parse>\";");
         line(source, 2, "result.error.notes.push_back(std::string{R\"(completed Earley parses for rule ')\"} + std::string{grammar_rule_names[root_rule]} + R\"(' were rejected by precedence/associativity constraints)\");");
         line(source, 2, "cpf::detail::error_tracker::finalize(result.error);");
         line(source, 2, "return result;");
         line(source, 1, "}");
         line(source, 0, "} // namespace");
         line(source, 0);

         for (const auto& rule : grammar.rules) {
            if (rule.synthetic) {
               continue;
            }
            const auto& info = classes.at(rule.identifier);
            line(source, 0, info.name + "::parse_result " + info.name + "::parse(std::string_view input) {");
            if (info.base_rule) {
               line(source, 1, "parse_result result;");
               line(source, 1, "cpf::parse_error best_error;");
               line(source, 1, "auto have_error = false;");
               for (const auto& child : families.at(info.name).direct_children) {
                  line(source, 1, "{");
                  line(source, 2, "auto child_result = " + child + "::parse(input);");
                  line(source, 2, "if (child_result.success) {");
                  line(source, 3, "result.success = true;");
                  line(source, 3, "for (auto& tree : child_result.forest) {");
                  line(source, 4, "result.forest.push_back(std::unique_ptr<" + info.name + ">{static_cast<" + info.name + "*>(tree.release())});");
                  line(source, 3, "}");
                  line(source, 2, "} else if (!have_error) {");
                  line(source, 3, "best_error = child_result.error;");
                  line(source, 3, "have_error = true;");
                  line(source, 2, "} else {");
                  line(source, 3, "cpf::detail::merge_parse_error(best_error, child_result.error);");
                  line(source, 2, "}");
                  line(source, 1, "}");
               }
               line(source, 1, "if (!result.forest.empty()) {");
               line(source, 2, "return result;");
               line(source, 1, "}");
               line(source, 1, "if (have_error) {");
               line(source, 2, "cpf::detail::append_unique(best_error.notes, " + cpp_string_literal("while matching base rule '" + info.name + "'") + ");");
               line(source, 2, "cpf::detail::error_tracker::finalize(best_error);");
               line(source, 2, "result.error = best_error;");
               line(source, 1, "}");
               line(source, 1, "return result;");
            } else {
               line(source, 1, "return parse_generated<" + info.name + ">(input, " + std::to_string(rule_indices.at(info.name)) + ");");
            }
            line(source, 0, "}");
            line(source, 0);
            line(source, 0, "const std::type_info& " + info.name + "::type() const {");
            line(source, 1, "return typeid(" + info.name + ");");
            line(source, 0, "}");
            line(source, 0);
            line(source, 0, "std::unique_ptr<" + info.name + "> " + info.name + "::clone() {");
            line(source, 1, "auto cloned = this->clone_node();");
            line(source, 1, "return std::unique_ptr<" + info.name + ">{static_cast<" + info.name + "*>(cloned.release())};");
            line(source, 0, "}");
            line(source, 0);

            if (!info.base_rule) {
               line(source, 0, "std::unique_ptr<cpf::node> " + info.name + "::clone_node() const {");
               line(source, 1, "auto copy = std::make_unique<" + info.name + ">();");
               line(source, 1, "copy->definition = definition;");
               line(source, 1, "copy->range = range;");
               for (const auto& field : info.fields) {
                  if (field.shape == field_shape::node_scalar) {
                     line(source, 1, render_child_clone_expression(field));
                  } else if (field.shape == field_shape::node_vector) {
                     line(source, 1, "for (const auto& child : " + field.name + ") {");
                     line(source, 2, "copy->" + field.name + ".push_back(child ? child->clone() : nullptr);");
                     line(source, 1, "}");
                  } else {
                     line(source, 1, "copy->" + field.name + " = " + field.name + ";");
                  }
               }
               line(source, 1, "return copy;");
               line(source, 0, "}");
               line(source, 0);
            }
         }

         for (const auto& rule : grammar.rules) {
            if (rule.synthetic) {
               continue;
            }
            const auto& info = classes.at(rule.identifier);
            line(source, 0, "std::ostream& operator<<(std::ostream& os, const " + info.name + "& node) {");
            if (info.base_rule) {
               auto concrete_descendants = collect_concrete_descendants(info.name, children, classes);
               for (const auto& descendant : concrete_descendants) {
                  line(source, 1, "if (auto* value = dynamic_cast<const " + descendant + "*>(&node)) {");
                  line(source, 2, "return os << *value;");
                  line(source, 1, "}");
               }
               line(source, 1, "return os << \"" + info.name + "()\";");
            } else {
               if (info.fields.empty()) {
                  line(source, 1, "(void)node;");
               }
               line(source, 1, "os << \"" + info.name + "(\";");
               for (std::size_t i = 0; i < info.fields.size(); ++i) {
                  const auto& field = info.fields[i];
                  if (i != 0) {
                     line(source, 1, "os << \", \";");
                  }
                  line(source, 1, "os << \"" + field.name + "=\";");
                  if (field.shape == field_shape::node_scalar) {
                     line(source, 1, "if (node." + field.name + ") {");
                     line(source, 2, "os << *node." + field.name + ";");
                     line(source, 1, "} else {");
                     line(source, 2, "os << \"null\";");
                     line(source, 1, "}");
                  } else if (field.shape == field_shape::node_vector) {
                     line(source, 1, "os << \"[\";");
                     line(source, 1, "for (std::size_t item_index = 0; item_index < node." + field.name + ".size(); ++item_index) {");
                     line(source, 2, "if (item_index != 0) {");
                     line(source, 3, "os << \", \";");
                     line(source, 2, "}");
                     line(source, 2, "if (node." + field.name + "[item_index]) {");
                     line(source, 3, "os << *node." + field.name + "[item_index];");
                     line(source, 2, "} else {");
                     line(source, 3, "os << \"null\";");
                     line(source, 2, "}");
                     line(source, 1, "}");
                     line(source, 1, "os << \"]\";");
                  } else if (field.shape == field_shape::terminal_optional) {
                     line(source, 1, "if (node." + field.name + ") {");
                     line(source, 2, "os << cpf::detail::quoted(node." + field.name + "->text);");
                     line(source, 1, "} else {");
                     line(source, 2, "os << \"null\";");
                     line(source, 1, "}");
                  } else if (field.shape == field_shape::terminal_vector) {
                     line(source, 1, "os << \"[\";");
                     line(source, 1, "for (std::size_t item_index = 0; item_index < node." + field.name + ".size(); ++item_index) {");
                     line(source, 2, "if (item_index != 0) {");
                     line(source, 3, "os << \", \";");
                     line(source, 2, "}");
                     line(source, 2, "os << cpf::detail::quoted(node." + field.name + "[item_index].text);");
                     line(source, 1, "}");
                     line(source, 1, "os << \"]\";");
                  } else {
                     line(source, 1, "os << cpf::detail::quoted(node." + field.name + ".text);");
                  }
               }
               line(source, 1, "os << \")\";");
               line(source, 1, "return os;");
            }
            line(source, 0, "}");
            line(source, 0);
         }

         if (!code_namespace.empty()) {
            line(source, 0, "} // namespace " + std::string{code_namespace});
            line(source, 0);
         }

         return generated_code{header.str(), source.str()};
      }
   } // namespace

   generated_code generate_code(const grammar& grammar, const std::string& base_name, std::string_view code_namespace) {
      auto code = generate_code_impl(grammar, base_name, code_namespace);
      return code;
   }
} // namespace cpf


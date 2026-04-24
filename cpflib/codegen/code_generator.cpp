#include "code_generator.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace cpf {
   namespace {
      struct field_info {
         std::string name;
         std::string type;
         bool node_pointer = false;
      };

      struct class_info {
         const production* primary_production = nullptr;
         std::string name;
         std::string base;
         bool base_rule = false;
         bool implicit_value = false;
         std::vector<field_info> fields;
      };

      struct family_info {
         std::string name;
         bool expression_family = false;
         std::vector<std::string> direct_children;
         std::vector<std::string> primary_children;
         std::vector<std::string> infix_children;
         std::map<std::string, int> precedence;
         std::map<std::string, std::string> associativity;
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

      [[nodiscard]] bool is_infix_rule(const class_info& info) {
         if (info.base.empty() || info.primary_production == nullptr) {
            return false;
         }
         const auto& symbols = info.primary_production->symbols;
         return symbols.size() == 3
             && symbols[0].kind == symbol_kind::reference
             && symbols[2].kind == symbol_kind::reference
             && symbols[0].value == info.base
             && symbols[2].value == info.base
             && symbols[1].kind != symbol_kind::reference;
      }

      [[nodiscard]] std::string precedence_label(const class_info& info) {
         if (info.primary_production == nullptr) {
            return info.name;
         }
         if (auto attribute = info.primary_production->find_attribute("prec"); attribute.has_value() && attribute->operation == attribute_operator::assign && !attribute->numeric) {
            return attribute->value;
         }
         if (auto attribute = info.primary_production->find_attribute("lbl"); attribute.has_value()) {
            return attribute->value;
         }
         return info.name;
      }

      [[nodiscard]] std::string precedence_alias(const class_info& info) {
         if (info.primary_production == nullptr) {
            return info.name;
         }
         if (auto attribute = info.primary_production->find_attribute("lbl"); attribute.has_value()) {
            return attribute->value;
         }
         return info.name;
      }

      [[nodiscard]] std::string associativity_of(const class_info& info) {
         if (info.primary_production == nullptr) {
            return "left";
         }
         if (auto attribute = info.primary_production->find_attribute("dir"); attribute.has_value()) {
            return attribute->value;
         }
         return "left";
      }

      [[nodiscard]] bool has_absolute_precedence_rank(const class_info& info) {
         if (info.primary_production == nullptr) {
            return false;
         }
         if (auto attribute = info.primary_production->find_attribute("prec"); attribute.has_value()) {
            return attribute->numeric;
         }
         return true;
      }

      [[nodiscard]] precedence_rank precedence_rank_of(const class_info& info) {
         precedence_rank rank;
         if (info.primary_production == nullptr) {
            return rank;
         }

         rank.line = info.primary_production->line;
         rank.value = static_cast<int>(info.primary_production->line);
         if (auto attribute = info.primary_production->find_attribute("prec"); attribute.has_value() && attribute->numeric) {
            rank.explicit_value = true;
            rank.value = std::stoi(attribute->value);
         }
         return rank;
      }

      [[nodiscard]] bool is_single_unlabeled_terminal(const production& production) {
         return production.symbols.size() == 1
             && production.symbols.front().kind != symbol_kind::reference
             && !production.symbols.front().has_label();
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

      [[nodiscard]] std::string render_symbol_debug(const symbol& symbol) {
         std::string text;
         if (symbol.kind == symbol_kind::reference) {
            text = symbol.value;
         } else if (symbol.kind == symbol_kind::literal) {
            text = "'" + symbol.value + "'";
         } else {
            text = "r'" + symbol.value + "'";
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

      generated_code generate_code_impl(const grammar& grammar, const std::string& base_name) {
         std::unordered_map<std::string, const rule*> rules_by_name;
         std::unordered_map<std::string, std::vector<std::string>> children;
         std::unordered_map<std::string, std::string> bases;

         for (const auto& rule : grammar.rules) {
            if (rules_by_name.contains(rule.identifier)) {
               throw std::runtime_error{"Duplicate rule '" + rule.identifier + "'"};
            }
            rules_by_name[rule.identifier] = &rule;
         }

         for (const auto& rule : grammar.rules) {
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
            if (!rule.is_choice_rule() && rule.productions.size() != 1) {
               throw std::runtime_error{"Rule '" + rule.identifier + "' must have exactly one production unless it is a pure choice rule"};
            }

            class_info info;
            info.primary_production = rule.productions.empty() ? nullptr : &rule.productions.front();
            info.name = rule.identifier;
            info.base_rule = rule.is_choice_rule();
            if (auto base_it = bases.find(rule.identifier); base_it != bases.end()) {
               info.base = base_it->second;
            }
            if (!info.base_rule && info.primary_production != nullptr) {
               info.implicit_value = is_single_unlabeled_terminal(*info.primary_production);
               for (const auto& symbol : info.primary_production->symbols) {
                  if (!symbol.has_label()) {
                     continue;
                  }
                  field_info field;
                  field.name = symbol.label;
                  field.node_pointer = symbol.kind == symbol_kind::reference;
                  field.type = field.node_pointer ? "std::unique_ptr<" + symbol.value + ">" : "std::string";
                  info.fields.push_back(std::move(field));
               }
               if (info.implicit_value) {
                  info.fields.push_back(field_info{"value", "std::string", false});
               }
            }
            classes.emplace(info.name, std::move(info));
         }

         for (const auto& [name, info] : classes) {
            if (info.primary_production == nullptr) {
               continue;
            }
            for (const auto& symbol : info.primary_production->symbols) {
               if (symbol.kind == symbol_kind::reference && !rules_by_name.contains(symbol.value)) {
                  throw std::runtime_error{"Rule '" + name + "' references unknown rule '" + symbol.value + "'"};
               }
            }
         }

         std::unordered_map<std::string, family_info> families;
         for (const auto& rule : grammar.rules) {
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
               if (is_infix_rule(child_info)) {
                  family.expression_family = true;
                  family.infix_children.push_back(child);
                  auto group = precedence_label(child_info);
                  alias_to_group[child] = group;
                  alias_to_group[precedence_alias(child_info)] = group;
                  group_line[group] = std::min(group_line.contains(group) ? group_line[group] : child_info.primary_production->line, child_info.primary_production->line);
                  if (has_absolute_precedence_rank(child_info)) {
                     auto rank = precedence_rank_of(child_info);
                     if (!group_rank.contains(group)
                      || (rank.explicit_value && !group_rank[group].explicit_value)
                      || (rank.explicit_value == group_rank[group].explicit_value
                       && (rank.value < group_rank[group].value
                        || (rank.value == group_rank[group].value && rank.line < group_rank[group].line)))) {
                        group_rank[group] = rank;
                     }
                  }
                  family.associativity[child] = associativity_of(child_info);
               } else {
                  family.primary_children.push_back(child);
               }
            }

            for (const auto& child : family.infix_children) {
               const auto& child_info = classes.at(child);
               auto group = precedence_label(child_info);
               if (auto attribute = child_info.primary_production->find_attribute("prec"); attribute.has_value() && !attribute->numeric) {
                  if (attribute->operation == attribute_operator::less_than) {
                     edges[group].insert(attribute->value);
                  } else if (attribute->operation == attribute_operator::greater_than) {
                     edges[attribute->value].insert(group);
                  }
               }
            }

            std::set<std::string> all_groups;
            for (const auto& child : family.infix_children) {
               all_groups.insert(precedence_label(classes.at(child)));
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
               for (const auto& child : family.infix_children) {
                  if (precedence_label(classes.at(child)) == ordered_groups[i]) {
                     family.precedence[child] = static_cast<int>(i + 1);
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
         line(header, 0, "#include <string>");
         line(header, 0, "#include <string_view>");
         line(header, 0, "#include <typeinfo>");
         line(header, 0, "#include <utility>");
         line(header, 0);

         for (const auto& rule : grammar.rules) {
            line(header, 0, "struct " + rule.identifier + ";");
         }
         line(header, 0);

         for (const auto& rule : grammar.rules) {
            const auto& info = classes.at(rule.identifier);
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
               if (field.node_pointer) {
                  line(header, 1, "if (node." + field.name + ") {");
                  line(header, 2, "visit_recursive(*node." + field.name + ", visitor);");
                  line(header, 1, "}");
               }
            }
            line(header, 0, "}");
            line(header, 0);
         }

         std::unordered_map<std::string, std::size_t> rule_indices;
         for (std::size_t i = 0; i < grammar.rules.size(); ++i) {
             rule_indices.emplace(grammar.rules[i].identifier, i);
         }

         std::ostringstream source;
         line(source, 0, "#include \"" + base_name + ".h\"");
         line(source, 0);
         line(source, 0, "#include <array>");
         line(source, 0, "#include <ostream>");
         line(source, 0, "#include <stdexcept>");
         line(source, 0, "#include <utility>");
         line(source, 0);
         line(source, 0, "namespace {");
         line(source, 1, "using parse_node_ptr = cpf::detail::parse_node_ptr;");
         line(source, 0, "} // namespace");
         line(source, 0);

         auto production_index = 0;
         for (const auto& rule : grammar.rules) {
            for (const auto& production : rule.productions) {
               line(source, 0, "namespace {");
               line(source, 1, "constexpr std::array<cpf::detail::parser_symbol, " + std::to_string(production.symbols.size()) + "> production_" + std::to_string(production_index) + "_symbols{{");
               for (std::size_t i = 0; i < production.symbols.size(); ++i) {
                  const auto& symbol = production.symbols[i];
                  std::string rendered_symbol;
                  if (symbol.kind == symbol_kind::reference) {
                     rendered_symbol = "{cpf::detail::parser_symbol_kind::nonterminal, " + std::to_string(rule_indices.at(symbol.value)) + ", " + cpp_string_literal(symbol.value) + "}";
                  } else if (symbol.kind == symbol_kind::literal) {
                     rendered_symbol = "{cpf::detail::parser_symbol_kind::literal, 0, " + cpp_string_literal(symbol.value) + "}";
                  } else {
                     rendered_symbol = "{cpf::detail::parser_symbol_kind::regex, 0, " + cpp_string_literal(symbol.value) + "}";
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
         }

         const auto production_count = production_index;

         line(source, 0, "namespace {");
         line(source, 1, "constexpr std::array<cpf::detail::production_spec, " + std::to_string(production_count) + "> grammar_productions{{");
         production_index = 0;
         for (const auto& rule : grammar.rules) {
            for (std::size_t production_offset = 0; production_offset < rule.productions.size(); ++production_offset) {
               const auto& production = rule.productions[production_offset];
               auto rendered_production = "{" + std::to_string(rule_indices.at(rule.identifier))
                  + ", " + cpp_string_literal(rule.identifier)
                  + ", " + cpp_string_literal(render_production_debug(rule.identifier, production))
                  + ", production_" + std::to_string(production_index) + "_symbols.data(), production_" + std::to_string(production_index) + "_symbols.size()}";
               if (production_index + 1 != production_count) {
                  rendered_production += ",";
               }
               line(source, 2, rendered_production);
               ++production_index;
            }
         }
         line(source, 1, "}};");
         line(source, 1, "constexpr cpf::detail::grammar_spec grammar_spec{grammar_productions.data(), grammar_productions.size(), " + std::to_string(grammar.rules.size()) + "};");
         line(source, 0);

         for (const auto& rule : grammar.rules) {
            const auto& info = classes.at(rule.identifier);
            auto family_it = families.find(rule.identifier);
            if (!info.base_rule || family_it == families.end() || !family_it->second.expression_family) {
               continue;
            }

            const auto& family = family_it->second;
            line(source, 1, "int precedence_of_" + family.name + "(const " + family.name + "& node) {");
            for (const auto& child : family.infix_children) {
               line(source, 2, "if (dynamic_cast<const " + child + "*>(&node) != nullptr) {");
               line(source, 3, "return " + std::to_string(family.precedence.at(child)) + ";");
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
            const auto& info = classes.at(rule.identifier);
            if (info.base_rule) {
               continue;
            }
            auto needs_value = is_infix_rule(info);
            for (const auto& field : info.fields) {
               if (field.node_pointer) {
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
               if (!field.node_pointer) {
                  continue;
               }
               line(source, 3, "if (value->" + field.name + " && !validate_generated_node(*value->" + field.name + ")) {");
               line(source, 4, "return false;");
               line(source, 3, "}");
            }

            if (is_infix_rule(info)) {
               const auto& symbols = info.primary_production->symbols;
               const auto left_label = symbols[0].label.empty() ? "left" : symbols[0].label;
               const auto right_label = symbols[2].label.empty() ? "right" : symbols[2].label;
               const auto& family = families.at(info.base);
               const auto precedence = std::to_string(family.precedence.at(info.name));
               const auto left_associative = family.associativity.at(info.name) == "left" ? "true" : "false";

               line(source, 3, "if (value->" + left_label + " && !validate_" + info.base + "_child(*value->" + left_label + ", " + precedence + ", " + left_associative + ", true)) {");
               line(source, 4, "return false;");
               line(source, 3, "}");
               line(source, 3, "if (value->" + right_label + " && !validate_" + info.base + "_child(*value->" + right_label + ", " + precedence + ", " + left_associative + ", false)) {");
               line(source, 4, "return false;");
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
         production_index = 0;
         for (const auto& rule : grammar.rules) {
            const auto& info = classes.at(rule.identifier);
            for (const auto& production : rule.productions) {
               line(source, 3, "case " + std::to_string(production_index) + ": {");
               if (info.base_rule) {
                  line(source, 4, "return build_node(std::get<parse_node_ptr>(tree->children.front()));");
               } else {
                  line(source, 4, "auto node = std::make_unique<" + info.name + ">();");
                  for (std::size_t i = 0; i < production.symbols.size(); ++i) {
                     const auto& symbol = production.symbols[i];
                     if (symbol.kind == symbol_kind::reference) {
                        if (symbol.has_label()) {
                           line(source, 4, "auto child_" + std::to_string(i) + " = build_node(std::get<parse_node_ptr>(tree->children[" + std::to_string(i) + "]));");
                           line(source, 4, "node->" + symbol.label + " = std::unique_ptr<" + symbol.value + ">{static_cast<" + symbol.value + "*>(child_" + std::to_string(i) + ".release())};");
                        }
                     } else if (symbol.has_label()) {
                        line(source, 4, "node->" + symbol.label + " = std::get<std::string>(tree->children[" + std::to_string(i) + "]); ");
                     } else if (info.implicit_value) {
                        line(source, 4, "node->value = std::get<std::string>(tree->children[" + std::to_string(i) + "]); ");
                     }
                  }
                  line(source, 4, "return node;");
               }
               line(source, 3, "}");
               ++production_index;
            }
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
         line(source, 2, "result.error.notes.push_back(std::string{\"completed Earley parses for rule '\"} + std::string{grammar_spec.productions[root_rule].lhs_name} + \"' were rejected by precedence/associativity constraints\");");
         line(source, 2, "cpf::detail::error_tracker::finalize(result.error);");
         line(source, 2, "return result;");
         line(source, 1, "}");
         line(source, 0, "} // namespace");
         line(source, 0);

         for (const auto& rule : grammar.rules) {
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
               for (const auto& field : info.fields) {
                  if (field.node_pointer) {
                     line(source, 1, render_child_clone_expression(field));
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
               line(source, 1, "os << \"" + info.name + "(\";");
               for (std::size_t i = 0; i < info.fields.size(); ++i) {
                  const auto& field = info.fields[i];
                  if (i != 0) {
                     line(source, 1, "os << \", \";");
                  }
                  line(source, 1, "os << \"" + field.name + "=\";");
                  if (field.node_pointer) {
                     line(source, 1, "if (node." + field.name + ") {");
                     line(source, 2, "os << *node." + field.name + ";");
                     line(source, 1, "} else {");
                     line(source, 2, "os << \"null\";");
                     line(source, 1, "}");
                  } else {
                     line(source, 1, "os << cpf::detail::quoted(node." + field.name + ");");
                  }
               }
               line(source, 1, "os << \")\";");
               line(source, 1, "return os;");
            }
            line(source, 0, "}");
            line(source, 0);
         }

         return generated_code{header.str(), source.str()};
      }
   } // namespace

   generated_code generate_code(const grammar& grammar, const std::string& base_name) {
      auto code = generate_code_impl(grammar, base_name);
      return code;
   }
} // namespace cpf


#include "grammar_analysis.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cpf {
   namespace {
      [[nodiscard]] std::size_t first_line_of(const rule& rule) {
         if (rule.productions.empty()) {
            return 1;
         }
         return rule.productions.front().line;
      }

      [[nodiscard]] bool is_nullable_quantifier(symbol_quantifier quantifier, std::size_t exact_repetition) {
         switch (quantifier) {
            case symbol_quantifier::optional:
            case symbol_quantifier::zero_or_more:
               return true;
            case symbol_quantifier::exact:
               return exact_repetition == 0;
            case symbol_quantifier::one:
            case symbol_quantifier::one_or_more:
               return false;
         }
         return false;
      }

      [[nodiscard]] bool base_symbol_nullable(const symbol& symbol,
                                              const std::unordered_map<std::string, bool>& nullable_rules) {
         if (symbol.is_zero_width()) {
            return true;
         }
         if (symbol.kind == symbol_kind::literal) {
            return symbol.value.empty();
         }
         if (symbol.kind == symbol_kind::regex) {
            return false;
         }
         if (auto nullable = nullable_rules.find(symbol.value); nullable != nullable_rules.end()) {
            return nullable->second;
         }
         return false;
      }

      [[nodiscard]] bool symbol_nullable(const symbol& symbol,
                                         const std::unordered_map<std::string, bool>& nullable_rules) {
         if (is_nullable_quantifier(symbol.quantifier, symbol.exact_repetition)) {
            return true;
         }
         return base_symbol_nullable(symbol, nullable_rules);
      }

      [[nodiscard]] bool production_nullable(const production& production,
                                             const std::unordered_map<std::string, bool>& nullable_rules) {
         return std::ranges::all_of(production.symbols, [&](const auto& symbol) {
            return symbol_nullable(symbol, nullable_rules);
         });
      }

      [[nodiscard]] std::vector<std::string>
      related_public_rules(const std::vector<std::size_t>& component, const std::vector<const rule*>& rules) {
         auto related = std::vector<std::string>{};
         for (const auto index: component) {
            if (rules[index]->synthetic) {
               continue;
            }
            related.push_back(rules[index]->identifier);
         }
         std::ranges::sort(related);
         related.erase(std::unique(related.begin(), related.end()), related.end());
         return related;
      }

      [[nodiscard]] std::string rule_kind_name(const rule& rule) {
         return rule.declared_as_token ? "Token rule" : "Rule";
      }

      void tarjan_visit(std::size_t node, const std::vector<std::vector<std::size_t>>& adjacency,
                        std::vector<int>& indices, std::vector<int>& lowlinks, std::vector<bool>& on_stack,
                        std::vector<std::size_t>& stack, int& next_index,
                        std::vector<std::vector<std::size_t>>& components) {
         indices[node] = next_index;
         lowlinks[node] = next_index;
         ++next_index;
         stack.push_back(node);
         on_stack[node] = true;

         for (const auto target: adjacency[node]) {
            if (indices[target] < 0) {
               tarjan_visit(target, adjacency, indices, lowlinks, on_stack, stack, next_index, components);
               lowlinks[node] = std::min(lowlinks[node], lowlinks[target]);
            } else if (on_stack[target]) {
               lowlinks[node] = std::min(lowlinks[node], indices[target]);
            }
         }

         if (lowlinks[node] != indices[node]) {
            return;
         }

         auto component = std::vector<std::size_t>{};
         while (!stack.empty()) {
            const auto current = stack.back();
            stack.pop_back();
            on_stack[current] = false;
            component.push_back(current);
            if (current == node) {
               break;
            }
         }
         components.push_back(std::move(component));
      }
   } // namespace

   bool grammar_analysis::has_warnings() const {
      return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
         return diagnostic.severity == grammar_diagnostic_severity::warning;
      });
   }

   bool grammar_analysis::has_errors() const {
      return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
         return diagnostic.severity == grammar_diagnostic_severity::error;
      });
   }

   std::string grammar_analysis::render_summary() const {
      auto warning_count = std::size_t{0};
      auto error_count = std::size_t{0};
      for (const auto& diagnostic: diagnostics) {
         if (diagnostic.severity == grammar_diagnostic_severity::warning) {
            ++warning_count;
         } else {
            ++error_count;
         }
      }

      auto stream = std::ostringstream{};
      stream << "grammar analysis: parser rules=" << summary.parser_rule_count
             << ", token rules=" << summary.token_rule_count
             << ", reachable rules=" << summary.reachable_rule_count;
      if (!summary.primary_entry_rule.empty()) {
         stream << ", primary entry='" << summary.primary_entry_rule << "'";
      }
      stream << ", nullable rules=" << summary.nullable_rule_count
             << ", warnings=" << warning_count
             << ", errors=" << error_count;
      return stream.str();
   }

   grammar_analysis analyze_grammar(const grammar& grammar) {
      auto analysis = grammar_analysis{};
      auto rules = std::vector<const rule*>{};
      rules.reserve(grammar.rules.size());
      auto indices = std::unordered_map<std::string, std::size_t>{};
      for (const auto& rule: grammar.rules) {
         indices.emplace(rule.identifier, rules.size());
         rules.push_back(&rule);
         if (rule.synthetic) {
            continue;
         }
         if (rule.declared_as_token) {
            ++analysis.summary.token_rule_count;
         } else {
            ++analysis.summary.parser_rule_count;
            if (analysis.summary.primary_entry_rule.empty()) {
               analysis.summary.primary_entry_rule = rule.identifier;
            }
         }
      }

      auto adjacency = std::vector<std::vector<std::size_t>>(rules.size());
      auto incoming_references = std::vector<std::size_t>(rules.size(), 0);
      for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
         auto seen_targets = std::unordered_set<std::size_t>{};
         for (const auto& production: rules[rule_index]->productions) {
            for (const auto& symbol: production.symbols) {
               if (symbol.kind != symbol_kind::reference) {
                  continue;
               }
               auto target = indices.find(symbol.value);
               if (target == indices.end()) {
                  continue;
               }
               if (seen_targets.insert(target->second).second) {
                  adjacency[rule_index].push_back(target->second);
               }
               ++incoming_references[target->second];
            }
         }
      }

      auto reachable = std::vector<bool>(rules.size(), false);
      if (!analysis.summary.primary_entry_rule.empty()) {
         auto stack = std::vector<std::size_t>{indices.at(analysis.summary.primary_entry_rule)};
         while (!stack.empty()) {
            const auto current = stack.back();
            stack.pop_back();
            if (reachable[current]) {
               continue;
            }
            reachable[current] = true;
            for (const auto target: adjacency[current]) {
               if (!reachable[target]) {
                  stack.push_back(target);
               }
            }
         }
      }

      auto nullable_rules = std::unordered_map<std::string, bool>{};
      for (const auto* rule: rules) {
         nullable_rules.emplace(rule->identifier, false);
      }
      auto changed = true;
      while (changed) {
         changed = false;
         for (const auto* rule: rules) {
            if (nullable_rules.at(rule->identifier)) {
               continue;
            }
            for (const auto& production: rule->productions) {
               if (!production_nullable(production, nullable_rules)) {
                  continue;
               }
               nullable_rules[rule->identifier] = true;
               changed = true;
               break;
            }
         }
      }

      for (const auto& [identifier, nullable]: nullable_rules) {
         if (!nullable) {
            continue;
         }
         if (auto rule = grammar.find_rule(identifier); rule != nullptr && !rule->synthetic) {
            ++analysis.summary.nullable_rule_count;
         }
      }

      auto nullable_adjacency = std::vector<std::vector<std::size_t>>(rules.size());
      auto self_nullable_edge = std::vector<bool>(rules.size(), false);
      for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
         if (!nullable_rules.at(rules[rule_index]->identifier)) {
            continue;
         }
         auto targets = std::unordered_set<std::size_t>{};
         for (const auto& production: rules[rule_index]->productions) {
            if (!production_nullable(production, nullable_rules)) {
               continue;
            }
            for (const auto& symbol: production.symbols) {
               if (symbol.kind != symbol_kind::reference) {
                  continue;
               }
               auto target = indices.find(symbol.value);
               if (target == indices.end() || !nullable_rules.at(symbol.value)) {
                  continue;
               }
               targets.insert(target->second);
               if (target->second == rule_index) {
                  self_nullable_edge[rule_index] = true;
               }
            }
         }
         nullable_adjacency[rule_index].assign(targets.begin(), targets.end());
      }

      auto tarjan_indices = std::vector<int>(rules.size(), -1);
      auto tarjan_lowlinks = std::vector<int>(rules.size(), -1);
      auto tarjan_stack = std::vector<std::size_t>{};
      auto tarjan_on_stack = std::vector<bool>(rules.size(), false);
      auto tarjan_components = std::vector<std::vector<std::size_t>>{};
      auto tarjan_next_index = 0;
      for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
         if (!nullable_rules.at(rules[rule_index]->identifier) || tarjan_indices[rule_index] >= 0) {
            continue;
         }
         tarjan_visit(rule_index, nullable_adjacency, tarjan_indices, tarjan_lowlinks, tarjan_on_stack,
                      tarjan_stack, tarjan_next_index, tarjan_components);
      }

      for (const auto& component: tarjan_components) {
         auto has_cycle = component.size() > 1;
         if (!has_cycle && component.size() == 1) {
            has_cycle = self_nullable_edge[component.front()];
         }
         if (!has_cycle) {
            continue;
         }

         auto related = related_public_rules(component, rules);
         if (related.empty()) {
            continue;
         }

         auto diagnostic = grammar_diagnostic{};
         diagnostic.severity = grammar_diagnostic_severity::warning;
         diagnostic.code = grammar_diagnostic_code::nullable_cycle;
         diagnostic.rule = related.front();
         diagnostic.related_rules = related;
         diagnostic.line = first_line_of(*grammar.find_rule(diagnostic.rule));
         diagnostic.message = "Nullable cycle allows these rules to recurse without consuming input: ";
         for (std::size_t i = 0; i < related.size(); ++i) {
            if (i != 0) {
               diagnostic.message += ", ";
            }
            diagnostic.message += "'" + related[i] + "'";
         }
         analysis.diagnostics.push_back(std::move(diagnostic));
         ++analysis.summary.nullable_cycle_count;
      }

      for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
         const auto& rule = *rules[rule_index];
         if (rule.synthetic) {
            continue;
         }

         if (rule.identifier == analysis.summary.primary_entry_rule) {
            continue;
         }

         if (incoming_references[rule_index] == 0) {
            auto diagnostic = grammar_diagnostic{};
            diagnostic.severity = grammar_diagnostic_severity::warning;
            diagnostic.code = grammar_diagnostic_code::unused_rule;
            diagnostic.rule = rule.identifier;
            diagnostic.line = first_line_of(rule);
            diagnostic.message = rule.declared_as_token ? "Token rule '" + rule.identifier + "' is never referenced"
                                                        : "Rule '" + rule.identifier +
                                                              "' is never referenced by any other rule";
            analysis.diagnostics.push_back(std::move(diagnostic));
            ++analysis.summary.unused_rule_count;
            continue;
         }

         if (!analysis.summary.primary_entry_rule.empty() && !reachable[rule_index]) {
            auto diagnostic = grammar_diagnostic{};
            diagnostic.severity = grammar_diagnostic_severity::warning;
            diagnostic.code = grammar_diagnostic_code::unreachable_rule;
            diagnostic.rule = rule.identifier;
            diagnostic.line = first_line_of(rule);
            diagnostic.message = rule_kind_name(rule) + " '" + rule.identifier +
                                 "' is not reachable from the primary entry rule '" +
                                 analysis.summary.primary_entry_rule + "'";
            analysis.diagnostics.push_back(std::move(diagnostic));
            ++analysis.summary.unreachable_rule_count;
            continue;
         }
      }

      for (const auto& rule: grammar.rules) {
         if (rule.synthetic) {
            continue;
         }
         for (const auto& production: rule.productions) {
            auto suspicious = false;
            for (std::size_t index = 0; index < production.symbols.size(); ++index) {
               const auto& symbol = production.symbols[index];
               if (symbol.kind != symbol_kind::reference || symbol.value != rule.identifier) {
                  continue;
               }
               suspicious = true;
               for (std::size_t other = 0; other < production.symbols.size(); ++other) {
                  if (other == index) {
                     continue;
                  }
                  if (!symbol_nullable(production.symbols[other], nullable_rules)) {
                     suspicious = false;
                     break;
                  }
               }
               if (suspicious) {
                  break;
               }
            }
            if (!suspicious) {
               continue;
            }

            auto diagnostic = grammar_diagnostic{};
            diagnostic.severity = grammar_diagnostic_severity::warning;
            diagnostic.code = grammar_diagnostic_code::suspicious_recursive_pattern;
            diagnostic.rule = rule.identifier;
            diagnostic.line = production.line;
            diagnostic.message = "Rule '" + rule.identifier +
                                 "' contains a self-recursive production that can loop without guaranteed token consumption";
            analysis.diagnostics.push_back(std::move(diagnostic));
            ++analysis.summary.suspicious_recursive_pattern_count;
         }
      }

      std::ranges::sort(analysis.diagnostics, [](const auto& lhs, const auto& rhs) {
         if (lhs.line != rhs.line) {
            return lhs.line < rhs.line;
         }
         if (lhs.severity != rhs.severity) {
            return lhs.severity == grammar_diagnostic_severity::error;
         }
         return lhs.message < rhs.message;
      });

      for (std::size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
         if (rules[rule_index]->synthetic || !reachable[rule_index]) {
            continue;
         }
         ++analysis.summary.reachable_rule_count;
      }

      return analysis;
   }
} // namespace cpf




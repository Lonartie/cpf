#include "rule_complexity.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace cpf {
   namespace {
      class sample_planner {
      public:
         explicit sample_planner(const grammar& grammar)
            : grammar_{grammar} {
            for (const auto& rule : grammar_.rules) {
               rules_by_name_.emplace(rule.identifier, &rule);
            }
            compute_minimal_rule_samples();
         }

         [[nodiscard]] auto build() -> rule_complexity_samples {
            rule_complexity_samples samples;
            for (const auto& rule : grammar_.rules) {
               if (rule.synthetic) {
                  continue;
               }
               samples.inputs_by_rule.emplace(rule.identifier, build_rule_samples(rule));
            }
            return samples;
         }

      private:
         [[nodiscard]] auto regex_sample(std::string_view pattern, std::size_t tier) const -> std::string {
            auto top_level_pipe = std::optional<std::size_t>{};
            auto in_class = false;
            for (std::size_t index = 0; index < pattern.size(); ++index) {
               auto current = pattern[index];
               if (current == '\\') {
                  ++index;
                  continue;
               }
               if (current == '[') {
                  in_class = true;
                  continue;
               }
               if (current == ']' && in_class) {
                  in_class = false;
                  continue;
               }
               if (!in_class && current == '|') {
                  top_level_pipe = index;
                  break;
               }
            }
            if (top_level_pipe.has_value()) {
               return regex_sample(pattern.substr(0, *top_level_pipe), tier);
            }

            auto class_sample = [](std::string_view char_class) -> std::string {
               if (char_class.find("0-9") != std::string_view::npos) {
                  return "0";
               }
               if (char_class.find("A-Z") != std::string_view::npos) {
                  return "A";
               }
               if (char_class.find("a-z") != std::string_view::npos) {
                  return "a";
               }
               if (char_class.find('_') != std::string_view::npos) {
                  return "_";
               }
               for (char current : char_class) {
                  if (std::isalnum(static_cast<unsigned char>(current)) != 0) {
                     return std::string{1, current};
                  }
               }
               return "x";
            };

            auto escape_sample = [](char escaped) -> std::string {
               switch (escaped) {
                  case 'd': return "0";
                  case 'w': return "w";
                  case 's': return " ";
                  case 'n': return "\n";
                  case 't': return "\t";
                  default: return std::string{1, escaped};
               }
            };

            std::string result;
            for (std::size_t index = 0; index < pattern.size();) {
               std::string atom;
               auto current = pattern[index];
               if (current == '[') {
                  auto end = pattern.find(']', index + 1);
                  if (end == std::string_view::npos) {
                     atom = "x";
                     ++index;
                  } else {
                     atom = class_sample(pattern.substr(index + 1, end - index - 1));
                     index = end + 1;
                  }
               } else if (current == '\\') {
                  if (index + 1 >= pattern.size()) {
                     atom = "x";
                     ++index;
                  } else {
                     atom = escape_sample(pattern[index + 1]);
                     index += 2;
                  }
               } else if (current == '.') {
                  atom = "x";
                  ++index;
               } else {
                  atom = std::string{1, current};
                  ++index;
               }

               auto repetitions = std::size_t{1};
               if (index < pattern.size()) {
                  auto quantifier = pattern[index];
                  if (quantifier == '?') {
                     repetitions = tier == 0 ? 0 : 1;
                     ++index;
                  } else if (quantifier == '*') {
                     repetitions = tier;
                     ++index;
                  } else if (quantifier == '+') {
                     repetitions = 1 + tier;
                     ++index;
                  } else if (quantifier == '{') {
                     auto end = pattern.find('}', index + 1);
                     if (end != std::string_view::npos) {
                        repetitions = static_cast<std::size_t>(std::stoull(std::string{pattern.substr(index + 1, end - index - 1)}));
                        index = end + 1;
                     }
                  }
               }

               for (std::size_t repeat = 0; repeat < repetitions; ++repeat) {
                  result += atom;
               }
            }

            return result.empty() ? std::string{"x"} : result;
         }

         [[nodiscard]] auto join_parts(const std::vector<std::string>& parts) const -> std::string {
            std::string joined;
            for (const auto& part : parts) {
               if (part.empty()) {
                  continue;
               }
               if (!joined.empty()) {
                  joined += ' ';
               }
               joined += part;
            }
            return joined;
         }

         [[nodiscard]] auto repetition_count(const symbol& symbol, std::size_t tier) const -> std::size_t {
            switch (symbol.quantifier) {
               case symbol_quantifier::one:
                  return 1;
               case symbol_quantifier::optional:
                  return tier == 0 ? 0 : 1;
               case symbol_quantifier::zero_or_more:
                  return tier;
               case symbol_quantifier::one_or_more:
                  return 1 + tier;
               case symbol_quantifier::exact:
                  return symbol.exact_repetition;
            }
            return 1;
         }

         [[nodiscard]] auto minimal_reference_sample(std::string_view rule_name) const -> std::optional<std::string> {
            auto sample = minimal_rule_samples_.find(std::string{rule_name});
            if (sample == minimal_rule_samples_.end() || !sample->second.has_value()) {
               return std::nullopt;
            }
            return *sample->second;
         }

         [[nodiscard]] auto minimal_symbol_sample(const symbol& symbol) const -> std::optional<std::string> {
            if (symbol.kind == symbol_kind::literal) {
               return symbol.value;
            }
            if (symbol.kind == symbol_kind::regex) {
               return regex_sample(symbol.value, 0);
            }

            auto repetitions = repetition_count(symbol, 0);
            std::vector<std::string> pieces;
            pieces.reserve(repetitions);
            for (std::size_t repeat = 0; repeat < repetitions; ++repeat) {
               auto reference = minimal_reference_sample(symbol.value);
               if (!reference.has_value()) {
                  return std::nullopt;
               }
               if (!reference->empty()) {
                  pieces.push_back(*reference);
               }
            }
            return join_parts(pieces);
         }

         [[nodiscard]] auto minimal_production_sample(const production& production) const -> std::optional<std::string> {
            std::vector<std::string> parts;
            parts.reserve(production.symbols.size());
            for (const auto& symbol : production.symbols) {
               auto sample = minimal_symbol_sample(symbol);
               if (!sample.has_value()) {
                  return std::nullopt;
               }
               if (!sample->empty()) {
                  parts.push_back(*sample);
               }
            }
            return join_parts(parts);
         }

         void compute_minimal_rule_samples() {
            for (const auto& rule : grammar_.rules) {
               minimal_rule_samples_.emplace(rule.identifier, std::nullopt);
            }

            auto changed = true;
            while (changed) {
               changed = false;
               for (const auto& rule : grammar_.rules) {
                  std::optional<std::string> best;
                  for (const auto& production : rule.productions) {
                     auto sample = minimal_production_sample(production);
                     if (!sample.has_value()) {
                        continue;
                     }
                     if (!best.has_value() || sample->size() < best->size()) {
                        best = std::move(sample);
                     }
                  }

                  if (!best.has_value()) {
                     continue;
                  }

                  auto& cached = minimal_rule_samples_.at(rule.identifier);
                  if (!cached.has_value() || best->size() < cached->size()) {
                     cached = *best;
                     changed = true;
                  }
               }
            }

            for (const auto& rule : grammar_.rules) {
               if (!minimal_rule_samples_.at(rule.identifier).has_value()) {
                  throw std::runtime_error{"Unable to generate minimal complexity samples for every grammar rule"};
               }
            }
         }

         [[nodiscard]] auto build_symbol_piece(const symbol& symbol, std::size_t tier) -> std::optional<std::string> {
            if (symbol.kind == symbol_kind::literal) {
               return symbol.value;
            }
            if (symbol.kind == symbol_kind::regex) {
               return regex_sample(symbol.value, tier);
            }

            auto requested_tier = tier;
            if (auto active = active_rule_counts_.find(symbol.value); active != active_rule_counts_.end() && active->second > 0) {
               requested_tier = requested_tier == 0 ? 0 : requested_tier - 1;
            }
            return build_rule_sample(symbol.value, requested_tier);
         }

         [[nodiscard]] auto build_symbol_sample(const symbol& symbol, std::size_t tier) -> std::optional<std::string> {
            auto repetitions = repetition_count(symbol, tier);
            std::vector<std::string> parts;
            parts.reserve(repetitions);
            for (std::size_t repeat = 0; repeat < repetitions; ++repeat) {
               auto piece_tier = symbol.is_repeated() && tier > 0 ? tier - 1 : tier;
               auto piece = build_symbol_piece(symbol, piece_tier);
               if (!piece.has_value()) {
                  if (repetitions == 0) {
                     return std::string{};
                  }
                  return std::nullopt;
               }
               if (!piece->empty()) {
                  parts.push_back(*piece);
               }
            }
            return join_parts(parts);
         }

         [[nodiscard]] auto build_production_sample(const production& production, std::size_t tier) -> std::optional<std::string> {
            std::vector<std::string> parts;
            parts.reserve(production.symbols.size());
            for (const auto& symbol : production.symbols) {
               auto sample = build_symbol_sample(symbol, tier);
               if (!sample.has_value()) {
                  return std::nullopt;
               }
               if (!sample->empty()) {
                  parts.push_back(*sample);
               }
            }
            return join_parts(parts);
         }

         [[nodiscard]] auto build_rule_sample(std::string_view rule_name, std::size_t tier) -> std::optional<std::string> {
            auto cache_key = std::string{rule_name} + "#" + std::to_string(tier);
            if (auto cache = rule_sample_cache_.find(cache_key); cache != rule_sample_cache_.end()) {
               return cache->second;
            }

            if (tier == 0) {
               auto minimal = minimal_reference_sample(rule_name);
               rule_sample_cache_.emplace(cache_key, minimal);
               return minimal;
            }

            auto rule_it = rules_by_name_.find(std::string{rule_name});
            if (rule_it == rules_by_name_.end()) {
               throw std::runtime_error{"Unable to generate complexity sample for unknown rule '" + std::string{rule_name} + "'"};
            }

            ++active_rule_counts_[std::string{rule_name}];
            std::optional<std::string> best;
            for (const auto& production : rule_it->second->productions) {
               auto sample = build_production_sample(production, tier);
               if (!sample.has_value()) {
                  continue;
               }
               if (!best.has_value() || sample->size() > best->size()) {
                  best = std::move(sample);
               }
            }
            --active_rule_counts_[std::string{rule_name}];

            if (!best.has_value()) {
               best = minimal_reference_sample(rule_name);
            }

            rule_sample_cache_.emplace(cache_key, best);
            return best;
         }

         [[nodiscard]] auto build_reduction_samples(std::string_view rule_name, const production& production) -> std::vector<std::string> {
            std::vector<std::string> samples;
            samples.reserve(4);
            for (auto tier = std::size_t{0}; tier < 4; ++tier) {
               auto sample = tier == 0 ? minimal_production_sample(production) : build_production_sample(production, tier);
               if (!sample.has_value()) {
                  throw std::runtime_error{"Unable to generate complexity samples for rule '" + std::string{rule_name} + "' definition " + std::to_string(production.definition)};
               }
               samples.push_back(*sample);
            }
            if (samples.size() < 2) {
               samples.push_back(samples.front());
            }
            return samples;
         }

         [[nodiscard]] auto build_rule_samples(const rule& rule) -> std::vector<std::vector<std::string>> {
            std::vector<std::vector<std::string>> samples_by_definition;
            samples_by_definition.reserve(rule.productions.size());
            for (const auto& production : rule.productions) {
               samples_by_definition.push_back(build_reduction_samples(rule.identifier, production));
            }
            return samples_by_definition;
         }

         const grammar& grammar_;
         std::unordered_map<std::string, const rule*> rules_by_name_;
         std::unordered_map<std::string, std::optional<std::string>> minimal_rule_samples_;
         std::unordered_map<std::string, std::optional<std::string>> rule_sample_cache_;
         std::unordered_map<std::string, std::size_t> active_rule_counts_;
      };
   } // namespace

   rule_complexity_samples generate_rule_complexity_samples(const grammar& grammar) {
      return sample_planner{grammar}.build();
   }
} // namespace cpf






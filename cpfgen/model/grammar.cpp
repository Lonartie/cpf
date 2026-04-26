#include "grammar.h"

#include <algorithm>

namespace cpf {
   bool symbol::has_label() const { return !label.empty(); }

   bool symbol::is_optional() const {
      return quantifier == symbol_quantifier::optional ||
             (quantifier == symbol_quantifier::exact && exact_repetition == 0);
   }

   bool symbol::is_repeated() const {
      return quantifier == symbol_quantifier::zero_or_more || quantifier == symbol_quantifier::one_or_more ||
             (quantifier == symbol_quantifier::exact && exact_repetition != 1);
   }

   bool symbol::is_single() const {
      return quantifier == symbol_quantifier::one || (quantifier == symbol_quantifier::exact && exact_repetition == 1);
   }

   std::optional<attribute> production::find_attribute(std::string_view name) const {
      auto it = std::find_if(attributes.begin(), attributes.end(),
                             [&](const auto& attribute) { return attribute.name == name; });
      if (it == attributes.end()) {
         return std::nullopt;
      }
      return *it;
   }

   bool production::is_direct_reference_choice() const {
      return attributes.empty() && symbols.size() == 1 && symbols.front().is_single() &&
             symbols.front().kind == symbol_kind::reference && !symbols.front().has_label();
   }

   bool rule::is_choice_rule() const {
      return !productions.empty() && std::all_of(productions.begin(), productions.end(), [](const auto& production) {
         return production.is_direct_reference_choice();
      });
   }

   const skip_rule* grammar::find_skip_rule(std::string_view identifier) const {
      auto it = std::find_if(skip_rules.begin(), skip_rules.end(),
                             [&](const auto& rule) { return rule.identifier == identifier; });
      return it == skip_rules.end() ? nullptr : &*it;
   }

   skip_rule* grammar::find_skip_rule(std::string_view identifier) {
      auto it = std::find_if(skip_rules.begin(), skip_rules.end(),
                             [&](const auto& rule) { return rule.identifier == identifier; });
      return it == skip_rules.end() ? nullptr : &*it;
   }

   const rule* grammar::find_rule(std::string_view identifier) const {
      auto it =
            std::find_if(rules.begin(), rules.end(), [&](const auto& rule) { return rule.identifier == identifier; });
      return it == rules.end() ? nullptr : &*it;
   }

   rule* grammar::find_rule(std::string_view identifier) {
      auto it =
            std::find_if(rules.begin(), rules.end(), [&](const auto& rule) { return rule.identifier == identifier; });
      return it == rules.end() ? nullptr : &*it;
   }
} // namespace cpf

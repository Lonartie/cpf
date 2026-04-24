#include "grammar.h"

#include <algorithm>

namespace cpf {
   bool symbol::has_label() const {
	  return !label.empty();
   }

   std::optional<attribute> production::find_attribute(std::string_view name) const {
	  auto it = std::find_if(attributes.begin(), attributes.end(), [&](const auto& attribute) {
		 return attribute.name == name;
	  });
	  if (it == attributes.end()) {
		 return std::nullopt;
	  }
	  return *it;
   }

   bool production::is_direct_reference_choice() const {
	  return attributes.empty()
		  && symbols.size() == 1
		  && symbols.front().kind == symbol_kind::reference
		  && !symbols.front().has_label();
   }

   bool rule::is_choice_rule() const {
	  return !productions.empty()
		  && std::all_of(productions.begin(), productions.end(), [](const auto& production) {
			   return production.is_direct_reference_choice();
			 });
   }

   const rule* grammar::find_rule(std::string_view identifier) const {
	  auto it = std::find_if(rules.begin(), rules.end(), [&](const auto& rule) {
		 return rule.identifier == identifier;
	  });
	  return it == rules.end() ? nullptr : &*it;
   }

   rule* grammar::find_rule(std::string_view identifier) {
	  auto it = std::find_if(rules.begin(), rules.end(), [&](const auto& rule) {
		 return rule.identifier == identifier;
	  });
	  return it == rules.end() ? nullptr : &*it;
   }
} // namespace cpf


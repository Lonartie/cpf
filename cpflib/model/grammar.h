#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cpf {
   /// @brief Describes the kind of grammar symbol used in a production.
   enum class symbol_kind {
      reference,
      literal,
      regex
   };

   /// @brief Operator used by a grammar attribute assignment.
   enum class attribute_operator {
      assign,
      less_than,
      greater_than
   };

   /// @brief Parsed representation of a rule attribute.
   struct attribute {
      std::string name;
      attribute_operator operation = attribute_operator::assign;
      std::string value;
      bool numeric = false;
   };

   /// @brief Parsed grammar symbol within a production.
   struct symbol {
      symbol_kind kind = symbol_kind::reference;
      std::string value;
      std::string label;

      /// @brief Checks whether the symbol exposes a generated member label.
      /// @return True when the symbol was annotated with a label.
      [[nodiscard]] bool has_label() const;
   };

   /// @brief One production alternative for a rule.
   struct production {
      std::vector<symbol> symbols;
      std::vector<attribute> attributes;
      std::size_t line = 1;

      /// @brief Finds an attribute by name.
      /// @param name Attribute name to search for.
      /// @return The matching attribute when present.
      [[nodiscard]] std::optional<attribute> find_attribute(std::string_view name) const;

      /// @brief Checks whether the production is a plain base-to-derived choice.
      /// @return True when the production is a single unlabeled rule reference.
      [[nodiscard]] bool is_direct_reference_choice() const;
   };

   /// @brief Parsed grammar rule.
   struct rule {
      std::string identifier;
      std::vector<production> productions;

      /// @brief Checks whether all productions are direct reference choices.
      /// @return True when the rule behaves as an inheritance/choice rule.
      [[nodiscard]] bool is_choice_rule() const;
   };

   /// @brief Parsed grammar document.
   struct grammar {
      std::vector<rule> rules;

      /// @brief Looks up a rule by identifier.
      /// @param identifier Rule name to search for.
      /// @return Pointer to the matching rule or nullptr when absent.
      [[nodiscard]] const rule* find_rule(std::string_view identifier) const;

      /// @brief Looks up a mutable rule by identifier.
      /// @param identifier Rule name to search for.
      /// @return Pointer to the matching rule or nullptr when absent.
      [[nodiscard]] rule* find_rule(std::string_view identifier);
   };
} // namespace cpf


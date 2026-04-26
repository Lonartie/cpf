#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cpf {
   /// @brief Describes the kind of grammar symbol used in a production.
   enum class symbol_kind { reference, literal, regex };

   /// @brief Postfix repetition applied to a grammar symbol.
   enum class symbol_quantifier { one, optional, zero_or_more, one_or_more, exact };

   /// @brief Zero-width lookahead behavior attached to a grammar symbol.
   enum class lookahead_kind { none, positive, negative };

   /// @brief Operator used by a grammar attribute assignment.
   enum class attribute_operator { assign, less_than, greater_than };

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
      symbol_quantifier quantifier = symbol_quantifier::one;
      std::size_t exact_repetition = 1;
      lookahead_kind lookahead = lookahead_kind::none;

      /// @brief Checks whether the symbol exposes a generated member label.
      /// @return True when the symbol was annotated with a label.
      [[nodiscard]] bool has_label() const;

      /// @brief Checks whether the symbol can match zero times.
      /// @return True when the symbol is optional.
      [[nodiscard]] bool is_optional() const;

      /// @brief Checks whether the symbol can match more than once.
      /// @return True when the symbol is repeated into a list.
      [[nodiscard]] bool is_repeated() const;

      /// @brief Checks whether the symbol behaves like a single required occurrence.
      /// @return True when the symbol lowers to exactly one occurrence.
      [[nodiscard]] bool is_single() const;

      /// @brief Checks whether the symbol performs a zero-width lookahead test.
      /// @return True when the symbol matches without consuming input.
      [[nodiscard]] bool is_zero_width() const;
   };

   /// @brief One production alternative for a rule.
   struct production {
      std::vector<symbol> symbols;
      std::vector<attribute> attributes;
      /// @brief Zero-based production index after merging repeated rule declarations.
      std::size_t definition = 0;
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
      bool declared_as_token = false;
      bool synthetic = false;
      std::vector<production> productions;

      /// @brief Checks whether all productions are direct reference choices.
      /// @return True when the rule behaves as an inheritance/choice rule.
      [[nodiscard]] bool is_choice_rule() const;
   };

   /// @brief Parsed skip-rule declaration used for ignorable input trivia.
   struct skip_rule {
      std::string identifier;
      symbol_kind kind = symbol_kind::regex;
      std::string value;
      std::size_t line = 1;
   };

   /// @brief Parsed grammar document.
   struct grammar {
      std::optional<std::string> whitespace_rule;
      std::size_t whitespace_rule_line = 1;
      std::vector<skip_rule> skip_rules;
      std::vector<rule> rules;

      /// @brief Looks up a skip rule by identifier.
      /// @param identifier Skip-rule name to search for.
      /// @return Pointer to the matching skip rule or nullptr when absent.
      [[nodiscard]] const skip_rule* find_skip_rule(std::string_view identifier) const;

      /// @brief Looks up a mutable skip rule by identifier.
      /// @param identifier Skip-rule name to search for.
      /// @return Pointer to the matching skip rule or nullptr when absent.
      [[nodiscard]] skip_rule* find_skip_rule(std::string_view identifier);

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

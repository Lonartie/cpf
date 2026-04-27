#pragma once

#include "model/grammar.h"

#include <string>
#include <vector>

namespace cpf {
   /// @brief Severity level for static grammar diagnostics.
   enum class grammar_diagnostic_severity { warning, error };

   /// @brief Stable diagnostic identifiers for grammar analysis findings.
   enum class grammar_diagnostic_code {
      unreachable_rule,
      unused_rule,
      nullable_cycle,
      suspicious_recursive_pattern
   };

   /// @brief One static diagnostic produced while analyzing a grammar.
   struct grammar_diagnostic {
      grammar_diagnostic_severity severity = grammar_diagnostic_severity::warning;
      grammar_diagnostic_code code = grammar_diagnostic_code::unused_rule;
      std::string message;
      std::string rule;
      std::vector<std::string> related_rules;
      std::size_t line = 1;
   };

   /// @brief Aggregate counts describing one analyzed grammar.
   struct grammar_analysis_summary {
      std::string primary_entry_rule;
      std::size_t parser_rule_count = 0;
      std::size_t token_rule_count = 0;
      std::size_t reachable_rule_count = 0;
      std::size_t unreachable_rule_count = 0;
      std::size_t unused_rule_count = 0;
      std::size_t nullable_rule_count = 0;
      std::size_t nullable_cycle_count = 0;
      std::size_t suspicious_recursive_pattern_count = 0;
   };

   /// @brief Structured result of running static grammar diagnostics.
   struct grammar_analysis {
      grammar_analysis_summary summary;
      std::vector<grammar_diagnostic> diagnostics;

      /// @brief Checks whether any warning diagnostics were produced.
      /// @return True when at least one warning is present.
      [[nodiscard]] bool has_warnings() const;

      /// @brief Checks whether any error diagnostics were produced.
      /// @return True when at least one error is present.
      [[nodiscard]] bool has_errors() const;

      /// @brief Renders a short human-readable summary string.
      /// @return Summary text suitable for logs or UIs.
      [[nodiscard]] std::string render_summary() const;
   };

   /// @brief Performs static diagnostics and linting on a parsed grammar.
   /// @param grammar Parsed grammar description.
   /// @return Structured analysis summary and diagnostics.
   [[nodiscard]] grammar_analysis analyze_grammar(const grammar& grammar);
} // namespace cpf


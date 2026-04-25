#pragma once

#include "model/grammar.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace cpf {
   /// @brief Generated sample inputs used to measure the complexity of each public grammar reduction.
   struct rule_complexity_samples {
      std::unordered_map<std::string, std::vector<std::vector<std::string>>> inputs_by_rule;
   };

   /// @brief Synthesizes valid sample inputs for each public production definition in a grammar.
   /// @param grammar Parsed grammar description.
   /// @return Deterministic per-reduction sample inputs used for complexity fitting.
   rule_complexity_samples generate_rule_complexity_samples(const grammar& grammar);
} // namespace cpf


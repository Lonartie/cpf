#include "expression_autofix.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
   auto require(bool condition, std::string_view message) -> bool {
      if (!condition) {
         std::cerr << message << std::endl;
      }
      return condition;
   }

   struct evaluator {
      auto value(const auto& node) -> double { return source_repair::visit(*node, *this); }
      auto operator()(const source_repair::addition& node) -> double { return value(node.lhs) + value(node.rhs); }
      auto operator()(const source_repair::multiplication& node) -> double { return value(node.lhs) * value(node.rhs); }
      auto operator()(const source_repair::grouping& node) -> double { return value(node.inner); }
      auto operator()(const source_repair::number& node) -> double { return std::stod(node.value.text); }
      auto operator()(const source_repair::call& node) -> double {
         if (node.name.text == "sum") {
            auto total = 0.0;
            for (const auto& argument: node.args) {
               total += value(argument);
            }
            return total;
         }
         throw std::runtime_error{"Unknown function"};
      }
   };
}

int main() {
   constexpr auto input = "(1 + sum(2, 3)";

   cpf::parse_options options;
   options.allow_partial = true;

   auto partial_result = source_repair::expression::parse(input, options);
   if (!require(partial_result.success, "broken expression should recover into one tree")) {
      return 1;
   }
   if (!require(partial_result.partial, "broken expression should report partial success")) {
      return 1;
   }
   if (!require(partial_result.status == cpf::parse_status::partial_success,
                "broken expression should set partial_success")) {
      return 1;
   }
   if (!require(partial_result.error.has_value(), "broken expression should return a recovery diagnostic")) {
      return 1;
   }
   if (!require(partial_result.forest.size() == 1, "broken expression should keep one recovered tree")) {
      return 1;
   }
   if (!require(partial_result.forest.front().is_partial(), "recovered expression tree should be partial")) {
      return 1;
   }

   auto repaired = partial_result.forest.front().try_repair_input(input);
   if (!require(repaired.has_value(), "recovered expression tree should rebuild repaired input")) {
      return 1;
   }
   if (!require(*repaired != input, "repaired expression should differ from the broken input")) {
      return 1;
   }
   std::cout << "Repaired expression: " << *repaired << std::endl;

   auto clean_result = source_repair::expression::parse(*repaired);
   if (!require(clean_result.success, "repaired expression should parse cleanly")) {
      return 1;
   }
   if (!require(!clean_result.partial, "repaired expression should not stay partial")) {
      return 1;
   }
   if (!require(clean_result.forest.size() == 1, "repaired expression should produce one tree")) {
      return 1;
   }

   const auto partial_value = source_repair::visit(*partial_result.forest.front(), evaluator{});
   const auto clean_value = source_repair::visit(*clean_result.forest.front(), evaluator{});
   if (!require(std::abs(partial_value - 6.0) < 1e-9, "recovered expression should evaluate to 6")) {
      return 1;
   }
   if (!require(std::abs(clean_value - 6.0) < 1e-9, "repaired expression should evaluate to 6")) {
      return 1;
   }

   std::cout << "Source repair example checks passed." << std::endl;
}


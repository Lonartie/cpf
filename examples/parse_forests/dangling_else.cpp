#include "dangling_else.h"
#include <iostream>
#include <string_view>

namespace {
   auto require(bool condition, std::string_view message) -> bool {
      if (!condition) {
         std::cerr << message << std::endl;
      }
      return condition;
   }
}

int main() {
   constexpr auto input = "if outer then if inner then accept() else reject()";

   auto result = parse_forests::statement::parse(input);
   if (!require(result.success, "dangling-else input should parse successfully")) {
      return 1;
   }
   if (!require(!result.partial, "dangling-else input should not need recovery")) {
      return 1;
   }
   if (!require(result.forest.size() == 2, "dangling-else input should return two parse trees")) {
      return 1;
   }

   auto saw_else_on_inner_if = false;
   auto saw_else_on_outer_if = false;
   for (const auto& tree: result.forest) {
      std::cout << *tree << std::endl;

      if (const auto* outer_without_else = dynamic_cast<const parse_forests::if_then*>(tree.get());
          outer_without_else != nullptr) {
         if (!require(dynamic_cast<const parse_forests::if_then_else*>(outer_without_else->then_branch.get()) != nullptr,
                      "one forest entry should attach else to the inner if")) {
            return 1;
         }
         saw_else_on_inner_if = true;
         continue;
      }

      if (const auto* outer_with_else = dynamic_cast<const parse_forests::if_then_else*>(tree.get());
          outer_with_else != nullptr) {
         if (!require(dynamic_cast<const parse_forests::if_then*>(outer_with_else->then_branch.get()) != nullptr,
                      "one forest entry should keep the inner if without else")) {
            return 1;
         }
         if (!require(dynamic_cast<const parse_forests::call*>(outer_with_else->else_branch.get()) != nullptr,
                      "the outer else branch should still be the reject() call")) {
            return 1;
         }
         saw_else_on_outer_if = true;
         continue;
      }

      std::cerr << "Unexpected tree type in parse forest" << std::endl;
      return 1;
   }

   if (!require(saw_else_on_inner_if, "parse forest should contain the inner-else interpretation")) {
      return 1;
   }
   if (!require(saw_else_on_outer_if, "parse forest should contain the outer-else interpretation")) {
      return 1;
   }

   cpf::parse_options ambiguity_options;
   ambiguity_options.error_on_ambiguity = true;
   auto ambiguity_error = parse_forests::statement::parse(input, ambiguity_options);
   if (!require(!ambiguity_error.success, "error_on_ambiguity should reject the dangling-else input")) {
      return 1;
   }
   if (!require(ambiguity_error.forest.empty(), "ambiguous parse failure should not return forest entries")) {
      return 1;
   }
   if (!require(ambiguity_error.error.has_value(), "ambiguous parse failure should report diagnostics")) {
      return 1;
   }
   if (!require(ambiguity_error.error->found.kind == cpf::parse_error_found_kind::ambiguous_parse,
                "ambiguous parse failure should classify the found kind correctly")) {
      return 1;
   }

   std::cout << "Parse forest example checks passed." << std::endl;
}


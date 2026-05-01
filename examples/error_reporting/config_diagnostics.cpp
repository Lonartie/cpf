#include "config_diagnostics.h"
#include <iostream>
#include <string_view>

namespace {
   auto require(bool condition, std::string_view message) -> bool {
      if (!condition) {
         std::cerr << message << std::endl;
      }
      return condition;
   }

   auto contains(const std::vector<std::string>& values, std::string_view expected) -> bool {
      for (const auto& value: values) {
         if (value == expected) {
            return true;
         }
      }
      return false;
   }

   void print_error(std::string_view title, const cpf::parse_error& error) {
      std::cout << title << "\n"
                << "  line: " << error.position.line << ", column: " << error.position.column << "\n"
                << "  found kind: " << cpf::to_string(error.found.kind) << "\n"
                << "  found text: " << error.found.text << "\n"
                << "  message: " << error.message << std::endl;
   }

   auto check_invalid_value_example() -> bool {
      constexpr auto input =
            "# Feature flags\n"
            "[server]\n"
            "port = 8080\n"
            "enabled = maybe\n";

      auto result = error_reporting::document::parse(input);
      if (!require(!result.success, "invalid value input should fail")) {
         return false;
      }
      if (!require(result.status == cpf::parse_status::failure, "invalid value input should report failure")) {
         return false;
      }
      if (!require(result.forest.empty(), "failed parse should not return trees")) {
         return false;
      }
      if (!require(result.error.has_value(), "failed parse should return diagnostics")) {
         return false;
      }

      const auto& error = *result.error;
      print_error("invalid value diagnostic", error);
      return require(error.position.line == 4 && error.position.column == 11,
                     "invalid value diagnostic should point at 'maybe'") &&
             require(error.found.kind == cpf::parse_error_found_kind::token,
                     "invalid value diagnostic should report a token") &&
             require(contains(error.expected, "expected quoted string"),
                     "invalid value diagnostic should mention quoted strings") &&
             require(contains(error.expected, "expected integer literal"),
                     "invalid value diagnostic should mention integers") &&
             require(contains(error.expected, "expected boolean literal"),
                     "invalid value diagnostic should mention booleans") &&
             require(error.notes.size() >= 3,
                     "invalid value diagnostic should merge contextual notes from every value branch") &&
             require(error.message.find("expected expected boolean literal") != std::string::npos,
                     "invalid value diagnostic should describe the merged expectations");
   }

   auto check_invalid_key_example() -> bool {
      constexpr auto input =
            "[server]\n"
            "9port = true\n";

      auto result = error_reporting::document::parse(input);
      if (!require(!result.success, "invalid key input should fail")) {
         return false;
      }
      if (!require(result.error.has_value(), "invalid key input should return diagnostics")) {
         return false;
      }

      const auto& error = *result.error;
      print_error("invalid key diagnostic", error);
      return require(error.position.line == 2 && error.position.column == 1,
                     "invalid key diagnostic should point at the bad key") &&
             require(contains(error.expected, "expected setting name"),
                     "invalid key diagnostic should use the custom identifier label") &&
             require(error.message.find("expected setting name") != std::string::npos,
                     "invalid key diagnostic should mention the custom label");
   }
}

int main() {
   if (!check_invalid_value_example()) {
      return 1;
   }
   if (!check_invalid_key_example()) {
      return 1;
   }

   std::cout << "Error reporting example checks passed." << std::endl;
}


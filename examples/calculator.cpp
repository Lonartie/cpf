#include "calculator.h"
#include <iostream>
#include <cmath>

/// @brief A calculator interpreter that evaluates the expression represented by the AST.
struct calculator {
   bool require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); return true; }
   double value(auto& node) { return gen::visit(*node, *this); }
   double operator()(const gen::addition& n) { return value(n.lhs) + value(n.rhs); }
   double operator()(const gen::subtraction& n) { return value(n.lhs) - value(n.rhs); }
   double operator()(const gen::multiplication& n) { return value(n.lhs) * value(n.rhs); }
   double operator()(const gen::division& n) { return value(n.lhs) / value(n.rhs); }
   double operator()(const gen::power& n) { return std::pow(value(n.lhs), value(n.rhs)); }
   double operator()(const gen::grouping& g) { return value(g.inner); }
   double operator()(const gen::number& n) { return std::stod(n.value.text); }
   double operator()(const gen::method_call& n) {
      if (n.id->value.text == "sin" && require(n.args->args.size() == 1, "sin requires exactly one argument")) {
         return std::sin(value(n.args->args.front()));
      }
      if (n.id->value.text == "cos" && require(n.args->args.size() == 1, "cos requires exactly one argument")) {
         return std::cos(value(n.args->args.front()));
      }
      if (n.id->value.text == "tan" && require(n.args->args.size() == 1, "tan requires exactly one argument")) {
         return std::tan(value(n.args->args.front()));
      }
      if (n.id->value.text == "sum" && require(n.args->args.size() >= 1, "sum requires at least one argument")) {
         double sum = 0;
         for (auto& arg: n.args->args)
            sum += value(arg);
         return sum;
      }
      throw std::runtime_error(std::string("Unknown function: ") + n.id->value.text);
   }
};

int main() {
   // Parsing some input values
   auto result = gen::expression::parse("(1 + 2) * 3 - 4 / 5 ^ 6 + tan(0.5) - sum(1, 2, 3, 3)");

   // Checking that the parsing was successful and that we got exactly one AST
   if (!result.success || result.partial || result.forest.size() != 1) {
      std::cerr << "Parsing failed" << std::endl;
      return 1;
   }

   // Checking the return value
   auto& ast = result.forest.front();
   auto result_value = gen::visit(*ast, calculator {});
   if (std::abs(result_value - 0.546046) > 1e-6) {
      std::cerr << "Unexpected result: " << result_value << std::endl;
      return 1;
   }

   std::cout << "Result: " << result_value << std::endl;
}

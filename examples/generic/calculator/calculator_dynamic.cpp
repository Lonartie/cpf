#include <cpflib>
#include <iostream>
#include <cmath>

static constexpr auto grammar = R"(
expression                       ->  addition        | subtraction
                                  |  multiplication  | division
                                  |  power           | method_call
                                  |  grouping        | number;

addition         [prec = 'sub']  ->  expression:lhs '+' expression:rhs;
subtraction      [lbl = 'sub']   ->  expression:lhs '-' expression:rhs;
multiplication   [prec = 'div']  ->  expression:lhs '*' expression:rhs;
division         [lbl = 'div']   ->  expression:lhs '/' expression:rhs;
power            [dir = right]   ->  expression:lhs '^' expression:rhs;
grouping                         ->  '(' expression:inner ')';
method_call                      ->  identifier:id '(' arguments?:args[inline] ')';
number                           ->  r'[0-9]+(\.[0-9]+)?':value;
token identifier                 ->  r'[a-zA-Z_][a-zA-Z0-9_]*';
arguments                        ->  expression:args (',' expression:args)*;)";

/// @brief A calculator interpreter that evaluates the expression represented by the AST.
struct calculator {
   bool require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); return true; }
   double value(auto&& node) { return cpf::visit(std::forward<decltype(node)>(node), *this); }
   double operator()(const cpf::dynamic_node& n) {
      if (n.rule_name == "addition") { return value(n.get_node("lhs")) + value(n.get_node("rhs")); }
      if (n.rule_name == "subtraction") { return value(n.get_node("lhs")) - value(n.get_node("rhs")); }
      if (n.rule_name == "multiplication") { return value(n.get_node("lhs")) * value(n.get_node("rhs")); }
      if (n.rule_name == "division") { return value(n.get_node("lhs")) / value(n.get_node("rhs")); }
      if (n.rule_name == "power") { return std::pow(value(n.get_node("lhs")), value(n.get_node("rhs"))); }
      if (n.rule_name == "grouping") { return value(n.get_node("inner")); }
      if (n.rule_name == "number") { return std::stod(n.get_token("value").text); }
      if (n.rule_name == "method_call") {
         auto args = n.get_nodes("args");
         const auto& identifier = n.get_token("id").text;
         if (identifier == "sin" && require(args.size() == 1, "sin requires exactly one argument")) {
            return std::sin(value(args.front()));
         }
         if (identifier == "cos" && require(args.size() == 1, "cos requires exactly one argument")) {
            return std::cos(value(args.front()));
         }
         if (identifier == "tan" && require(args.size() == 1, "tan requires exactly one argument")) {
            return std::tan(value(args.front()));
         }
         if (identifier == "sum" && require(args.size() >= 1, "sum requires at least one argument")) {
            double sum = 0;
            for (auto& arg: args)
               sum += value(arg);
            return sum;
         }
         throw std::runtime_error(std::string("Unknown function: ") + identifier);
      }
      throw std::runtime_error(std::string("Unknown rule: ") + n.rule_name);
   }
};

int main() {
   auto parser = cpf::compile_grammar(grammar);

   // Parsing some input values
   auto result = parser.parse("(1 + 2) * 3 - 4 / 5 ^ 6 + tan(0.5) - sum(1, 2, 3, 3)");

   // Checking that the parsing was successful and that we got exactly one AST
   if (!result.success || result.partial || result.forest.size() != 1) {
      std::cerr << "Parsing failed" << std::endl;
      return 1;
   }

   // Checking the return value
   auto& ast = result.forest.front();
   auto result_value = cpf::visit(*ast, calculator {});
   if (std::abs(result_value - 0.546046) > 1e-6) {
      std::cerr << "Unexpected result: " << result_value << std::endl;
      return 1;
   }

   std::cout << "Result: " << result_value << std::endl;
}
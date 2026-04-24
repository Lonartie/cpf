#include <cpflib>

#include "support/doctest.h"

TEST_SUITE("cpflib.code_generator") {
   TEST_CASE("calculator grammar generates the documented public surface") {
      auto grammar = cpf::parse_grammar(R"(
         expression -> addition | subtraction | multiplication | division | number;
         addition        [prec = 'sub']              -> expression:left '+':op expression:right;
         subtraction     [prec < 'div', lbl = 'sub'] -> expression:left '-':op expression:right;
         multiplication  [prec = 'div']              -> expression:left '*':op expression:right;
         division        [prec < 'num', lbl = 'div'] -> expression:left '/':op expression:right;
         number          [lbl = 'num']               -> r'[0-9]+';
      )");

      auto generated = cpf::generate_code(grammar, "calculator");

      SUBCASE("header output exposes node types and visitors") {
         CHECK(generated.header.find("struct expression : cpf::node") != std::string::npos);
         CHECK(generated.header.find("struct number : expression") != std::string::npos);
         CHECK(generated.header.find("std::string value;") != std::string::npos);
         CHECK(generated.header.find("std::unique_ptr<expression> left;") != std::string::npos);
         CHECK(generated.header.find("template<typename Visitor>") != std::string::npos);
         CHECK(generated.header.find("auto visit(const expression& node, Visitor&& visitor)") != std::string::npos);
      }

      SUBCASE("source output wires parser, errors, and cloning") {
         CHECK(generated.source.find("grammar_productions{{") != std::string::npos);
         CHECK(generated.source.find("cpf::detail::earley_parse(input, grammar_spec,") != std::string::npos);
         CHECK(generated.source.find("expression -> addition") != std::string::npos);
         CHECK(generated.source.find("std::unique_ptr<cpf::node> build_node(const parse_node_ptr& tree)") != std::string::npos);
         CHECK(generated.source.find("bool validate_generated_node(const cpf::node& node)") != std::string::npos);
         CHECK(generated.source.find("rejected by precedence/associativity constraints") != std::string::npos);
         CHECK(generated.source.find("result.forest.push_back(std::unique_ptr<T>{static_cast<T*>(built.release())});") != std::string::npos);
         CHECK(generated.source.find("std::unique_ptr<cpf::node> number::clone_node() const") != std::string::npos);
      }
   }

   TEST_CASE("omitted rule attributes fall back to documented defaults") {
      auto grammar = cpf::parse_grammar(R"(
         default_expr -> default_add | default_subtract | default_multiply | default_number;
         default_add -> default_expr:left '+':op default_expr:right;
         default_subtract -> default_expr:left '-':op default_expr:right;
         default_multiply -> default_expr:left '*':op default_expr:right;
         default_number -> r'[0-9]+';
      )");

      auto generated = cpf::generate_code(grammar, "default_attrs");

      SUBCASE("default precedence follows source order for infix rules") {
         CHECK(generated.source.find("int precedence_of_default_expr(const default_expr& node)") != std::string::npos);
         CHECK(generated.source.find("dynamic_cast<const default_add*>(&node) != nullptr") != std::string::npos);
         CHECK(generated.source.find("dynamic_cast<const default_subtract*>(&node) != nullptr") != std::string::npos);
         CHECK(generated.source.find("dynamic_cast<const default_multiply*>(&node) != nullptr") != std::string::npos);
         CHECK(generated.source.find("validate_default_expr_child(*value->left, 1, true, true)") != std::string::npos);
         CHECK(generated.source.find("validate_default_expr_child(*value->right, 3, true, false)") != std::string::npos);
      }

      SUBCASE("default associativity is left and unlabeled terminals become value fields") {
         CHECK(generated.source.find("validate_default_expr_child(*value->right, 2, true, false)") != std::string::npos);
         CHECK(generated.header.find("struct default_number : default_expr") != std::string::npos);
         CHECK(generated.header.find("std::string value;") != std::string::npos);
      }
   }

   TEST_CASE("default labels fall back to rule identifiers for relative precedence references") {
      auto grammar = cpf::parse_grammar(R"(
         default_label_expr -> default_label_add | default_label_multiply | default_label_number;
         default_label_add [prec < default_label_multiply] -> default_label_expr:left '+':op default_label_expr:right;
         default_label_multiply -> default_label_expr:left '*':op default_label_expr:right;
         default_label_number -> r'[0-9]+';
      )");

      auto generated = cpf::generate_code(grammar, "default_labels");

      CHECK(generated.source.find("int precedence_of_default_label_expr(const default_label_expr& node)") != std::string::npos);
      CHECK(generated.source.find("dynamic_cast<const default_label_add*>(&node) != nullptr") != std::string::npos);
      CHECK(generated.source.find("dynamic_cast<const default_label_multiply*>(&node) != nullptr") != std::string::npos);
      CHECK(generated.source.find("validate_default_label_expr_child(*value->right, 1, true, false)") != std::string::npos);
      CHECK(generated.source.find("validate_default_label_expr_child(*value->right, 2, true, false)") != std::string::npos);
   }

   TEST_CASE("ambiguous concrete grammars are rejected during code generation") {
      auto grammar = cpf::parse_grammar(R"(
         ambiguous_node -> 'x':first | 'x':second;
      )");

      CHECK_THROWS_WITH_AS(
         cpf::generate_code(grammar, "ambiguous_node"),
         doctest::Contains("must have exactly one production unless it is a pure choice rule"),
         std::runtime_error
      );
   }
}


#include <cpflib>

#include "support/doctest.h"

TEST_SUITE("cpflib.grammar_parser") {
   TEST_CASE("calculator grammar is parsed into the expected rule model") {
      auto grammar = cpf::parse_grammar(R"(
         // Calculator grammar file
         expression -> addition | subtraction | multiplication | division | number;
         addition        [prec = 'sub']              -> expression:left '+':op expression:right;
         subtraction     [prec < 'div', lbl = 'sub'] -> expression:left '-':op expression:right;
         multiplication  [prec = 'div']              -> expression:left '*':op expression:right;
         division        [prec < 'num', lbl = 'div'] -> expression:left '/':op expression:right;
         number          [lbl = 'num']               -> r'[0-9]+';
      )");

      REQUIRE(grammar.rules.size() == 6);

      auto* expression = grammar.find_rule("expression");
      auto* addition = grammar.find_rule("addition");
      auto* division = grammar.find_rule("division");
      auto* number = grammar.find_rule("number");

      REQUIRE(expression != nullptr);
      REQUIRE(addition != nullptr);
      REQUIRE(division != nullptr);
      REQUIRE(number != nullptr);
      REQUIRE(addition->productions.size() == 1);

      SUBCASE("choice rules are preserved") {
         CHECK(expression->is_choice_rule());
         CHECK(expression->productions.size() == 5);
         CHECK(expression->productions[0].symbols[0].value == "addition");
         CHECK(expression->productions[4].symbols[0].value == "number");
      }

      SUBCASE("labeled infix productions keep symbols and attributes") {
         CHECK(addition->productions[0].symbols.size() == 3);
         CHECK(addition->productions[0].symbols[0].label == "left");
         CHECK(addition->productions[0].symbols[1].kind == cpf::symbol_kind::literal);
         CHECK(addition->productions[0].symbols[1].value == "+");
         CHECK(addition->productions[0].symbols[1].label == "op");
         REQUIRE(addition->productions[0].find_attribute("prec").has_value());
         CHECK(addition->productions[0].find_attribute("prec")->value == "sub");

         REQUIRE(division->productions[0].find_attribute("lbl").has_value());
         CHECK(division->productions[0].find_attribute("lbl")->value == "div");
         CHECK(division->productions[0].find_attribute("prec")->operation == cpf::attribute_operator::less_than);
      }

      SUBCASE("regex terminals are preserved") {
         CHECK(number->productions[0].symbols[0].kind == cpf::symbol_kind::regex);
         CHECK(number->productions[0].symbols[0].value == "[0-9]+");
      }
   }

   TEST_CASE("attribute variants are preserved across parsed productions") {
      auto grammar = cpf::parse_grammar(R"(
         expr -> add | sub | pow | num;
         add [prec = 10, lbl = 'sum', dir = left] -> expr:left '+':op expr:right;
         sub [prec = 'sum'] -> expr:left '-':op expr:right;
         pow [prec > 'sum', dir = right, lbl = 'power'] -> expr:left '^':op expr:right;
         num [lbl = 'atom'] -> r'[0-9]+';
      )");

      auto* add = grammar.find_rule("add");
      auto* sub = grammar.find_rule("sub");
      auto* pow = grammar.find_rule("pow");
      auto* num = grammar.find_rule("num");

      REQUIRE(add != nullptr);
      REQUIRE(sub != nullptr);
      REQUIRE(pow != nullptr);
      REQUIRE(num != nullptr);

      SUBCASE("numeric precedence and rule labels are preserved") {
         REQUIRE(add->productions[0].find_attribute("prec").has_value());
         CHECK(add->productions[0].find_attribute("prec")->numeric);
         CHECK(add->productions[0].find_attribute("prec")->value == "10");
         CHECK(add->productions[0].find_attribute("dir")->value == "left");
         CHECK(add->productions[0].find_attribute("lbl")->value == "sum");
      }

      SUBCASE("relative precedence assignments are preserved") {
         REQUIRE(sub->productions[0].find_attribute("prec").has_value());
         CHECK_FALSE(sub->productions[0].find_attribute("prec")->numeric);
         CHECK(sub->productions[0].find_attribute("prec")->operation == cpf::attribute_operator::assign);
         CHECK(sub->productions[0].find_attribute("prec")->value == "sum");

         REQUIRE(pow->productions[0].find_attribute("prec").has_value());
         CHECK(pow->productions[0].find_attribute("prec")->operation == cpf::attribute_operator::greater_than);
         CHECK(pow->productions[0].find_attribute("prec")->value == "sum");
      }

      SUBCASE("associativity and terminal labels are preserved") {
         CHECK(pow->productions[0].find_attribute("dir")->value == "right");
         CHECK(pow->productions[0].find_attribute("lbl")->value == "power");
         REQUIRE(num->productions[0].find_attribute("lbl").has_value());
         CHECK(num->productions[0].find_attribute("lbl")->value == "atom");
      }
   }

   TEST_CASE("identifier-valued precedence references and omitted attributes are preserved") {
      auto grammar = cpf::parse_grammar(R"(
         expr -> add | multiply | number;
         add [prec < multiply] -> expr:left '+':op expr:right;
         multiply -> expr:left '*':op expr:right;
         number -> r'[0-9]+';
      )");

      auto* add = grammar.find_rule("add");
      auto* multiply = grammar.find_rule("multiply");
      auto* number = grammar.find_rule("number");

      REQUIRE(add != nullptr);
      REQUIRE(multiply != nullptr);
      REQUIRE(number != nullptr);

      REQUIRE(add->productions[0].find_attribute("prec").has_value());
      CHECK(add->productions[0].find_attribute("prec")->operation == cpf::attribute_operator::less_than);
      CHECK(add->productions[0].find_attribute("prec")->value == "multiply");
      CHECK_FALSE(add->productions[0].find_attribute("prec")->numeric);

      CHECK_FALSE(multiply->productions[0].find_attribute("prec").has_value());
      CHECK_FALSE(multiply->productions[0].find_attribute("dir").has_value());
      CHECK_FALSE(multiply->productions[0].find_attribute("lbl").has_value());
      CHECK_FALSE(number->productions[0].find_attribute("lbl").has_value());
   }

   TEST_CASE("duplicate choice-rule declarations preserve ambiguous alternatives in the grammar model") {
      auto grammar = cpf::parse_grammar(R"(
         ambiguous_expr -> ambiguous_first;
         ambiguous_expr -> ambiguous_second;
         ambiguous_first -> 'x':text;
         ambiguous_second -> 'x':text;
      )");

      REQUIRE(grammar.rules.size() == 3);

      auto* ambiguous_expr = grammar.find_rule("ambiguous_expr");
      REQUIRE(ambiguous_expr != nullptr);
      CHECK(ambiguous_expr->is_choice_rule());
      REQUIRE(ambiguous_expr->productions.size() == 2);
      CHECK(ambiguous_expr->productions[0].symbols[0].value == "ambiguous_first");
      CHECK(ambiguous_expr->productions[1].symbols[0].value == "ambiguous_second");
   }

   TEST_CASE("duplicate concrete rule declarations keep merged production definition indices") {
      auto grammar = cpf::parse_grammar(R"(
         merged_value -> 'x':text;
         merged_value -> r'x':text;
         merged_value -> 'y':text | 'z':text;
      )");

      REQUIRE(grammar.rules.size() == 1);

      auto* merged_value = grammar.find_rule("merged_value");
      REQUIRE(merged_value != nullptr);
      CHECK_FALSE(merged_value->is_choice_rule());
      REQUIRE(merged_value->productions.size() == 4);

      CHECK(merged_value->productions[0].definition == 0);
      CHECK(merged_value->productions[1].definition == 1);
      CHECK(merged_value->productions[2].definition == 2);
      CHECK(merged_value->productions[3].definition == 3);
    }

   TEST_CASE("grammar parser reports precise and expressive source errors") {
      auto capture_error = [](std::string_view source) -> std::string {
         try {
            static_cast<void>(cpf::parse_grammar(source));
         } catch (const std::runtime_error& error) {
            return error.what();
         }
         return std::string{};
      };

      SUBCASE("missing arrows point at the broken token and rule") {
         auto message = capture_error(R"(
            expr => number;
            number -> r'[0-9]+';
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("line 2") != std::string::npos);
         CHECK(message.find("column") != std::string::npos);
         CHECK(message.find("Expected '->'") != std::string::npos);
         CHECK(message.find("found \"=>\"") != std::string::npos);
         CHECK(message.find("while parsing rule 'expr'") != std::string::npos);
      }

      SUBCASE("unterminated quoted strings report end-of-input in rule context") {
         auto message = capture_error(R"(
            expr -> 'unterminated;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Unterminated quoted string") != std::string::npos);
         CHECK(message.find("<end of input>") != std::string::npos);
         CHECK(message.find("while parsing rule 'expr'") != std::string::npos);
      }

      SUBCASE("attribute operator mistakes mention the offending token") {
         auto message = capture_error(R"(
            expr [prec ! 10] -> number;
            number -> r'[0-9]+';
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Expected attribute operator") != std::string::npos);
         CHECK(message.find("found \"!\"") != std::string::npos);
         CHECK(message.find("while parsing rule 'expr'") != std::string::npos);
      }

      SUBCASE("empty productions report the exact source location") {
         auto message = capture_error(R"(
            expr -> ;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Expected at least one symbol in production") != std::string::npos);
         CHECK(message.find("line 2") != std::string::npos);
         CHECK(message.find("while parsing rule 'expr'") != std::string::npos);
      }
   }
}


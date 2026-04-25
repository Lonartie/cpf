#include <cpflib>

#include "support/doctest.h"

#include <filesystem>
#include <fstream>

namespace {
   void write_file(const std::filesystem::path& path, std::string_view content) {
      std::filesystem::create_directories(path.parent_path());
      std::ofstream stream{path};
      REQUIRE(stream.good());
      stream << content;
   }
}

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

   TEST_CASE("quantified symbol suffixes are preserved in the grammar model") {
      auto grammar = cpf::parse_grammar(R"(
         child -> 'c':text;
         quantified -> 'x'?:maybe_x child*:children r'[0-9]'+:digits child:fixed{3};
      )");

      auto* quantified = grammar.find_rule("quantified");
      REQUIRE(quantified != nullptr);
      REQUIRE(quantified->productions.size() == 1);
      REQUIRE(quantified->productions.front().symbols.size() == 4);

      const auto& maybe_x = quantified->productions.front().symbols[0];
      const auto& children = quantified->productions.front().symbols[1];
      const auto& digits = quantified->productions.front().symbols[2];
      const auto& fixed = quantified->productions.front().symbols[3];

      CHECK(maybe_x.quantifier == cpf::symbol_quantifier::optional);
      CHECK(children.quantifier == cpf::symbol_quantifier::zero_or_more);
      CHECK(digits.quantifier == cpf::symbol_quantifier::one_or_more);
      CHECK(fixed.quantifier == cpf::symbol_quantifier::exact);
      CHECK(fixed.exact_repetition == 3);
   }

   TEST_CASE("malformed repetition suffixes report expressive parser errors") {
      auto capture_error = [](std::string_view source) -> std::string {
         try {
            static_cast<void>(cpf::parse_grammar(source));
         } catch (const std::runtime_error& error) {
            return error.what();
         }
         return std::string{};
      };

      SUBCASE("missing repetition counts are rejected") {
         auto message = capture_error(R"(
            expr -> 'x'{ };
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Expected repetition count inside '{...}'") != std::string::npos);
      }

      SUBCASE("double repetition suffixes are rejected") {
         auto message = capture_error(R"(
            expr -> 'x'*:value?;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("only have one repetition suffix") != std::string::npos);
      }
   }

   TEST_CASE("grouped alternatives are lowered into flat productions") {
      auto grammar = cpf::parse_grammar(R"(
         grouped -> 'a' ('b' | 'c') 'd' | ('e' | 'f');
      )");

      REQUIRE(grammar.rules.size() == 1);
      auto* grouped = grammar.find_rule("grouped");
      REQUIRE(grouped != nullptr);
      REQUIRE(grouped->productions.size() == 4);

      CHECK(grouped->productions[0].symbols.size() == 3);
      CHECK(grouped->productions[0].symbols[0].value == "a");
      CHECK(grouped->productions[0].symbols[1].value == "b");
      CHECK(grouped->productions[1].symbols[1].value == "c");
      CHECK(grouped->productions[2].symbols[0].value == "e");
      CHECK(grouped->productions[3].symbols[0].value == "f");
   }

   TEST_CASE("quantified groups lower to synthetic helper rules") {
      auto grammar = cpf::parse_grammar(R"(
         grouped_many -> ('a' | 'b')+;
      )");

      REQUIRE(grammar.rules.size() == 2);

      auto* grouped_many = grammar.find_rule("grouped_many");
      REQUIRE(grouped_many != nullptr);
      REQUIRE(grouped_many->productions.size() == 1);
      REQUIRE(grouped_many->productions[0].symbols.size() == 1);
      CHECK(grouped_many->productions[0].symbols[0].kind == cpf::symbol_kind::reference);
      CHECK(grouped_many->productions[0].symbols[0].quantifier == cpf::symbol_quantifier::one_or_more);

      auto synthetic_seen = false;
      for (const auto& rule : grammar.rules) {
         if (!rule.synthetic) {
            continue;
         }
         synthetic_seen = true;
         REQUIRE(rule.productions.size() == 2);
         CHECK(rule.productions[0].symbols[0].value == "a");
         CHECK(rule.productions[1].symbols[0].value == "b");
      }
      CHECK(synthetic_seen);
   }

   TEST_CASE("labeled single-symbol groups lower through synthetic helper rules") {
      auto grammar = cpf::parse_grammar(R"(
         grouped_value -> ('x' | 'y'):value;
      )");

      REQUIRE(grammar.rules.size() == 2);

      auto* grouped_value = grammar.find_rule("grouped_value");
      REQUIRE(grouped_value != nullptr);
      REQUIRE(grouped_value->productions.size() == 1);
      REQUIRE(grouped_value->productions.front().symbols.size() == 1);
      CHECK(grouped_value->productions.front().symbols.front().label == "value");
      CHECK(grouped_value->productions.front().symbols.front().kind == cpf::symbol_kind::reference);
      CHECK(grouped_value->productions.front().symbols.front().value.find("$cpf_group_") == 0);

      auto synthetic_seen = false;
      for (const auto& rule : grammar.rules) {
         if (!rule.synthetic) {
            continue;
         }
         synthetic_seen = true;
         REQUIRE(rule.productions.size() == 2);
         REQUIRE(rule.productions[0].symbols.size() == 1);
         REQUIRE(rule.productions[1].symbols.size() == 1);
         CHECK(rule.productions[0].symbols[0].value == "x");
         CHECK(rule.productions[1].symbols[0].value == "y");
      }
      CHECK(synthetic_seen);
   }

   TEST_CASE("unsupported labeled group captures report expressive parser errors") {
      auto capture_error = [](std::string_view source) -> std::string {
         try {
            static_cast<void>(cpf::parse_grammar(source));
         } catch (const std::runtime_error& error) {
            return error.what();
         }
         return std::string{};
      };

      SUBCASE("labeled groups cannot also contain inner labeled captures") {
         auto message = capture_error(R"(
            expr -> ('x':inner | 'y':inner):value;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Labeled groups cannot contain labeled captures") != std::string::npos);
      }

      SUBCASE("quantified labeled groups are still rejected") {
         auto message = capture_error(R"(
            expr -> ('x' | 'y')+:value;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Quantified labeled groups are not supported") != std::string::npos);
      }

      SUBCASE("labeled groups must lower to a single symbol per alternative") {
         auto message = capture_error(R"(
            expr -> ('x' 'y' | 'z'):value;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Labeled groups must lower to exactly one symbol per alternative") != std::string::npos);
      }

      SUBCASE("quantified groups cannot contain labeled captures") {
         auto message = capture_error(R"(
            expr -> ('x':value | 'y':value)+;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Quantified groups cannot contain labeled captures") != std::string::npos);
      }
   }

   TEST_CASE("grammar loader expands imports across multiple files") {
      auto test_directory = std::filesystem::temp_directory_path() / "cpf_import_loader_tests";
      std::filesystem::remove_all(test_directory);

      SUBCASE("relative imports are loaded depth-first and merged") {
         write_file(test_directory / "parts" / "numbers.cpf", R"(
            imported_number -> r'[0-9]+':value;
         )");
         write_file(test_directory / "parts" / "expr.cpf", R"(
            import 'numbers.cpf';
            imported_expr -> imported_number;
         )");
         write_file(test_directory / "root.cpf", R"(
            import 'parts/expr.cpf';
            imported_root -> imported_expr;
         )");

         auto loaded = cpf::load_grammar_file(test_directory / "root.cpf");
         CHECK(loaded.dependencies.size() == 3);
         CHECK(loaded.parsed_grammar.find_rule("imported_root") != nullptr);
         CHECK(loaded.parsed_grammar.find_rule("imported_expr") != nullptr);
         CHECK(loaded.parsed_grammar.find_rule("imported_number") != nullptr);
      }

      SUBCASE("rules declared in different imported files merge like repeated in-file definitions") {
         write_file(test_directory / "first.cpf", R"(
            shared_expr -> shared_number;
            shared_number -> '1':value;
         )");
         write_file(test_directory / "second.cpf", R"(
            shared_expr -> shared_other;
            shared_other -> '2':value;
         )");
         write_file(test_directory / "root.cpf", R"(
            import 'first.cpf';
            import 'second.cpf';
         )");

         auto grammar = cpf::parse_grammar_file(test_directory / "root.cpf");
         auto* shared_expr = grammar.find_rule("shared_expr");
         REQUIRE(shared_expr != nullptr);
         REQUIRE(shared_expr->productions.size() == 2);
         CHECK(shared_expr->productions[0].definition == 0);
         CHECK(shared_expr->productions[1].definition == 1);
      }

      SUBCASE("duplicate imports are loaded only once") {
         write_file(test_directory / "leaf.cpf", R"(
            imported_leaf -> 'x':value;
         )");
         write_file(test_directory / "left.cpf", R"(
            import 'leaf.cpf';
            imported_left -> imported_leaf;
         )");
         write_file(test_directory / "right.cpf", R"(
            import 'leaf.cpf';
            imported_right -> imported_leaf;
         )");
         write_file(test_directory / "root.cpf", R"(
            import 'left.cpf';
            import 'right.cpf';
         )");

         auto loaded = cpf::load_grammar_file(test_directory / "root.cpf");
         CHECK(loaded.dependencies.size() == 4);
         CHECK(loaded.parsed_grammar.find_rule("imported_leaf") != nullptr);
      }

      SUBCASE("import cycles report a clear loader error") {
         write_file(test_directory / "first.cpf", "import 'second.cpf';\ncycle_first -> 'a':value;\n");
         write_file(test_directory / "second.cpf", "import 'first.cpf';\ncycle_second -> 'b':value;\n");

         CHECK_THROWS_WITH_AS(
            cpf::load_grammar_file(test_directory / "first.cpf"),
            doctest::Contains("Grammar import cycle detected"),
            std::runtime_error
         );
      }
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


#include <cpfgenlib>

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
} // namespace

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

   TEST_CASE("double-quoted grammar strings are parsed like single-quoted ones") {
      auto grammar = cpf::parse_grammar(R"(
         expr -> value | pattern;
         value [lbl = "atom", alias = 'token'] -> "\"quoted\"":text;
         pattern [prec = "atom"] -> r"[A-Z]+":caps;
      )");

      auto* value = grammar.find_rule("value");
      auto* pattern = grammar.find_rule("pattern");

      REQUIRE(value != nullptr);
      REQUIRE(pattern != nullptr);
      REQUIRE(value->productions.size() == 1);
      REQUIRE(pattern->productions.size() == 1);

      CHECK(value->productions[0].symbols[0].kind == cpf::symbol_kind::literal);
      CHECK(value->productions[0].symbols[0].value == "\"quoted\"");
      REQUIRE(value->productions[0].find_attribute("lbl").has_value());
      CHECK(value->productions[0].find_attribute("lbl")->value == "atom");
      REQUIRE(value->productions[0].find_attribute("alias").has_value());
      CHECK(value->productions[0].find_attribute("alias")->value == "token");

      CHECK(pattern->productions[0].symbols[0].kind == cpf::symbol_kind::regex);
      CHECK(pattern->productions[0].symbols[0].value == "[A-Z]+");
      REQUIRE(pattern->productions[0].find_attribute("prec").has_value());
      CHECK(pattern->productions[0].find_attribute("prec")->value == "atom");
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

   TEST_CASE("skip declarations and @whitespace are preserved separately from grammar rules") {
      auto grammar = cpf::parse_grammar(R"(
         @whitespace ws;
         skip ws -> r'[ \t\r\n]+';
         skip line_comment -> r'//[^\n]*';

         expr -> number;
         number -> r'[0-9]+':value;
      )");

      REQUIRE(grammar.whitespace_rule.has_value());
      CHECK(*grammar.whitespace_rule == "ws");
      REQUIRE(grammar.skip_rules.size() == 2);
      REQUIRE(grammar.rules.size() == 2);

      auto* whitespace = grammar.find_skip_rule("ws");
      auto* comment = grammar.find_skip_rule("line_comment");
      REQUIRE(whitespace != nullptr);
      REQUIRE(comment != nullptr);
      CHECK(whitespace->kind == cpf::symbol_kind::regex);
      CHECK(whitespace->value == "[ \t\r\n]+");
      CHECK(comment->kind == cpf::symbol_kind::regex);
      CHECK(comment->value == "//[^\n]*");
      CHECK(grammar.find_rule("expr") != nullptr);
      CHECK(grammar.find_rule("number") != nullptr);
   }

   TEST_CASE("token declarations are preserved as explicit token rules") {
      auto grammar = cpf::parse_grammar(R"(
         token identifier_head -> r'[A-Za-z_]';
         token identifier_tail -> r'[A-Za-z0-9_]';
         token identifier -> identifier_head identifier_tail*;

         variable -> identifier:name;
      )");

      auto* identifier = grammar.find_rule("identifier");
      auto* variable = grammar.find_rule("variable");

      REQUIRE(identifier != nullptr);
      REQUIRE(variable != nullptr);
      CHECK(identifier->declared_as_token);
      CHECK_FALSE(variable->declared_as_token);
      REQUIRE(identifier->productions.size() == 1);
      REQUIRE(identifier->productions.front().symbols.size() == 2);
      CHECK(identifier->productions.front().symbols[0].value == "identifier_head");
      CHECK(identifier->productions.front().symbols[1].value == "identifier_tail");
      CHECK(identifier->productions.front().symbols[1].quantifier == cpf::symbol_quantifier::zero_or_more);
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

      SUBCASE("@whitespace must reference an existing skip rule") {
         auto message = capture_error(R"(
            @whitespace ws;
            expr -> 'x':value;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("@whitespace references unknown skip rule 'ws'") != std::string::npos);
      }

      SUBCASE("skip rules must lower directly to terminals") {
         auto message = capture_error(R"(
            token -> 'x';
            skip ws -> token;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Skip rules must lower directly to a literal or regex terminal") != std::string::npos);
      }

      SUBCASE("@namespace remains a generation-time concern") {
         auto message = capture_error(R"(
            @namespace generated::fixtures;
            expr -> 'x':value;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Grammar directive '@namespace' is not supported") != std::string::npos);
      }

      SUBCASE("token declarations do not accept rule attributes") {
         auto message = capture_error(R"(
            token identifier [lbl = 'atom'] -> r'[A-Za-z_]+';
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Token declarations do not support rule attributes") != std::string::npos);
      }

      SUBCASE("rules cannot mix token and non-token declarations") {
         auto message = capture_error(R"(
            token identifier -> r'[A-Za-z_]+';
            identifier -> 'x';
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("cannot be declared as both token and non-token") != std::string::npos);
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
      for (const auto& rule: grammar.rules) {
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
      CHECK(grouped_value->productions.front().symbols.front().value.find("cpf_group_") == 0);

      auto synthetic_seen = false;
      for (const auto& rule: grammar.rules) {
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

   TEST_CASE("full labeled groups lower through synthetic helper rules") {
      auto grammar = cpf::parse_grammar(R"(
         expr -> number;
         number -> r'[0-9]+':value;
         grouped_pair -> ('x':first 'y':second | 'z':first 'w':second):value;
         grouped_quantified -> ('a':text 'b':suffix)+:pairs;
         grouped_nested -> ('-':sign number:value | number:value):payload;
      )");

      auto* grouped_pair = grammar.find_rule("grouped_pair");
      auto* grouped_quantified = grammar.find_rule("grouped_quantified");
      auto* grouped_nested = grammar.find_rule("grouped_nested");

      REQUIRE(grouped_pair != nullptr);
      REQUIRE(grouped_quantified != nullptr);
      REQUIRE(grouped_nested != nullptr);

      SUBCASE("multi-symbol labeled groups lower to one labeled helper reference") {
         REQUIRE(grouped_pair->productions.size() == 1);
         REQUIRE(grouped_pair->productions.front().symbols.size() == 1);
         const auto& symbol = grouped_pair->productions.front().symbols.front();
         CHECK(symbol.label == "value");
         CHECK(symbol.kind == cpf::symbol_kind::reference);
         CHECK(symbol.value.find("cpf_group_") == 0);
         CHECK(symbol.quantifier == cpf::symbol_quantifier::one);
      }

      SUBCASE("quantified labeled groups preserve the helper quantifier") {
         REQUIRE(grouped_quantified->productions.size() == 1);
         REQUIRE(grouped_quantified->productions.front().symbols.size() == 1);
         const auto& symbol = grouped_quantified->productions.front().symbols.front();
         CHECK(symbol.label == "pairs");
         CHECK(symbol.kind == cpf::symbol_kind::reference);
         CHECK(symbol.value.find("cpf_group_") == 0);
         CHECK(symbol.quantifier == cpf::symbol_quantifier::one_or_more);
      }

      SUBCASE("labeled groups may contain inner labeled captures") {
         REQUIRE(grouped_nested->productions.size() == 1);
         REQUIRE(grouped_nested->productions.front().symbols.size() == 1);
         const auto& symbol = grouped_nested->productions.front().symbols.front();
         CHECK(symbol.label == "payload");
         CHECK(symbol.kind == cpf::symbol_kind::reference);
         CHECK(symbol.value.find("cpf_group_") == 0);

         auto synthetic_seen = false;
         for (const auto& rule: grammar.rules) {
            if (!rule.synthetic || rule.identifier != symbol.value) {
               continue;
            }
            synthetic_seen = true;
            REQUIRE(rule.productions.size() == 2);
            CHECK(rule.productions[0].symbols[0].label == "sign");
            CHECK(rule.productions[0].symbols[1].label == "value");
            CHECK(rule.productions[1].symbols[0].label == "value");
         }
         CHECK(synthetic_seen);
      }
   }

   TEST_CASE("lookahead predicates are preserved as zero-width grammar symbols") {
      auto grammar = cpf::parse_grammar(R"(
         keyword -> 'if' | 'else';
         identifier -> !keyword r'[A-Za-z_][A-Za-z0-9_]*':value;
         call -> identifier:name &'(' '(':open ')':close;
      )");

      auto* identifier = grammar.find_rule("identifier");
      auto* call = grammar.find_rule("call");

      REQUIRE(identifier != nullptr);
      REQUIRE(call != nullptr);
      REQUIRE(identifier->productions.size() == 1);
      REQUIRE(call->productions.size() == 1);

      SUBCASE("negative lookahead keeps the referenced symbol and zero-width marker") {
         REQUIRE(identifier->productions.front().symbols.size() == 2);
         const auto& guard = identifier->productions.front().symbols[0];
         CHECK(guard.kind == cpf::symbol_kind::reference);
         CHECK(guard.value == "keyword");
         CHECK(guard.lookahead == cpf::lookahead_kind::negative);
         CHECK_FALSE(guard.has_label());
      }

      SUBCASE("positive lookahead on terminals stays separate from the consuming terminal") {
         REQUIRE(call->productions.front().symbols.size() == 4);
         const auto& guard = call->productions.front().symbols[1];
         const auto& open = call->productions.front().symbols[2];
         CHECK(guard.kind == cpf::symbol_kind::literal);
         CHECK(guard.value == "(");
         CHECK(guard.lookahead == cpf::lookahead_kind::positive);
         CHECK(open.kind == cpf::symbol_kind::literal);
         CHECK(open.value == "(");
         CHECK(open.lookahead == cpf::lookahead_kind::none);
         CHECK(open.label == "open");
      }
   }

   TEST_CASE("cut markers lower later alternatives behind negative lookahead guards") {
      auto grammar = cpf::parse_grammar(R"(
         statement -> 'if':keyword !> '(':open identifier:condition ')':close identifier:body | identifier:name;
      )");

      auto* statement = grammar.find_rule("statement");
      REQUIRE(statement != nullptr);
      REQUIRE(statement->productions.size() == 2);

      SUBCASE("the committed alternative keeps its original consuming symbols") {
         const auto& committed = statement->productions[0].symbols;
         REQUIRE(committed.size() == 5);
         CHECK(committed[0].value == "if");
         CHECK(committed[1].value == "(");
         CHECK(committed[2].value == "identifier");
      }

      SUBCASE("later alternatives are guarded by a synthetic negative lookahead helper") {
         const auto& fallback = statement->productions[1].symbols;
         REQUIRE(fallback.size() == 2);
         CHECK(fallback[0].kind == cpf::symbol_kind::reference);
         CHECK(fallback[0].lookahead == cpf::lookahead_kind::negative);
         CHECK(fallback[0].value.find("cpf_group_") == 0);
         CHECK(fallback[1].value == "identifier");
         CHECK(fallback[1].label == "name");
      }
   }

   TEST_CASE("template declarations instantiate into synthetic helper rules") {
      auto grammar = cpf::parse_grammar(R"(
         template surrounded<Open, Inner, Close> -> Open:open Inner:value Close:close;
         template keyword_value<Keyword, Value> -> Keyword:keyword Value:value;
         template specialized_surrounded<Open, InnerTempl, Close> -> Open:open InnerTempl<'spec'>:value Close:close;
         template prepend<Prep> -> Prep:prep '_value':suffix;
         token identifier_head -> r'[A-Za-z_]';
         token identifier_tail -> r'[A-Za-z0-9_]';
         token identifier -> identifier_head identifier_tail*;
         paren_identifier -> surrounded<'(', identifier, ')'>:body;
         paren_returned_identifier -> surrounded<'(', keyword_value<'return', identifier>, ')'>:body;
         paren_specialized_identifier -> specialized_surrounded<'(', prepend, ')'>:body;
      )");

      CHECK(grammar.find_rule("surrounded") == nullptr);

      auto* paren_identifier = grammar.find_rule("paren_identifier");
      REQUIRE(paren_identifier != nullptr);
      REQUIRE(paren_identifier->productions.size() == 1);
      REQUIRE(paren_identifier->productions.front().symbols.size() == 1);

      const auto& invocation = paren_identifier->productions.front().symbols.front();
      CHECK(invocation.kind == cpf::symbol_kind::reference);
      CHECK(invocation.label == "body");
      CHECK(invocation.value.find("cpf_template_surrounded_") == 0);

      auto* paren_returned_identifier = grammar.find_rule("paren_returned_identifier");
      REQUIRE(paren_returned_identifier != nullptr);
      REQUIRE(paren_returned_identifier->productions.size() == 1);
      REQUIRE(paren_returned_identifier->productions.front().symbols.size() == 1);
      const auto& nested_invocation = paren_returned_identifier->productions.front().symbols.front();
      CHECK(nested_invocation.kind == cpf::symbol_kind::reference);
      CHECK(nested_invocation.label == "body");
      CHECK(nested_invocation.value.find("cpf_template_surrounded_") == 0);

      auto* paren_specialized_identifier = grammar.find_rule("paren_specialized_identifier");
      REQUIRE(paren_specialized_identifier != nullptr);
      REQUIRE(paren_specialized_identifier->productions.size() == 1);
      REQUIRE(paren_specialized_identifier->productions.front().symbols.size() == 1);
      const auto& higher_order_invocation = paren_specialized_identifier->productions.front().symbols.front();
      CHECK(higher_order_invocation.kind == cpf::symbol_kind::reference);
      CHECK(higher_order_invocation.label == "body");
      CHECK(higher_order_invocation.value.find("cpf_template_specialized_surrounded_") == 0);

      auto instantiated = false;
      auto nested_instantiated = false;
      auto nested_payload_instantiated = false;
      auto higher_order_instantiated = false;
      auto higher_order_payload_instantiated = false;
      for (const auto& rule: grammar.rules) {
         if (!rule.synthetic || rule.identifier != invocation.value) {
            if (rule.synthetic && rule.identifier == nested_invocation.value) {
               nested_instantiated = true;
               REQUIRE(rule.productions.size() == 1);
               REQUIRE(rule.productions.front().symbols.size() == 3);
               CHECK(rule.productions.front().symbols[0].kind == cpf::symbol_kind::literal);
               CHECK(rule.productions.front().symbols[0].value == "(");
               CHECK(rule.productions.front().symbols[1].kind == cpf::symbol_kind::reference);
               CHECK(rule.productions.front().symbols[1].value.find("cpf_template_keyword_value_") == 0);
               CHECK(rule.productions.front().symbols[1].label == "value");
               CHECK(rule.productions.front().symbols[2].kind == cpf::symbol_kind::literal);
               CHECK(rule.productions.front().symbols[2].value == ")");
               continue;
            }
            if (rule.synthetic && rule.identifier == higher_order_invocation.value) {
               higher_order_instantiated = true;
               REQUIRE(rule.productions.size() == 1);
               REQUIRE(rule.productions.front().symbols.size() == 3);
               CHECK(rule.productions.front().symbols[0].kind == cpf::symbol_kind::literal);
               CHECK(rule.productions.front().symbols[0].value == "(");
               CHECK(rule.productions.front().symbols[1].kind == cpf::symbol_kind::reference);
               CHECK(rule.productions.front().symbols[1].value.find("cpf_template_prepend_") == 0);
               CHECK(rule.productions.front().symbols[1].label == "value");
               CHECK(rule.productions.front().symbols[2].kind == cpf::symbol_kind::literal);
               CHECK(rule.productions.front().symbols[2].value == ")");
               continue;
            }
            if (rule.synthetic && !rule.identifier.empty() && rule.identifier.find("cpf_template_keyword_value_") == 0) {
               nested_payload_instantiated = true;
               REQUIRE(rule.productions.size() == 1);
               REQUIRE(rule.productions.front().symbols.size() == 2);
               CHECK(rule.productions.front().symbols[0].kind == cpf::symbol_kind::literal);
               CHECK(rule.productions.front().symbols[0].value == "return");
               CHECK(rule.productions.front().symbols[0].label == "keyword");
               CHECK(rule.productions.front().symbols[1].kind == cpf::symbol_kind::reference);
               CHECK(rule.productions.front().symbols[1].value == "identifier");
               CHECK(rule.productions.front().symbols[1].label == "value");
               continue;
            }
            if (rule.synthetic && !rule.identifier.empty() && rule.identifier.find("cpf_template_prepend_") == 0) {
               higher_order_payload_instantiated = true;
               REQUIRE(rule.productions.size() == 1);
               REQUIRE(rule.productions.front().symbols.size() == 2);
               CHECK(rule.productions.front().symbols[0].kind == cpf::symbol_kind::literal);
               CHECK(rule.productions.front().symbols[0].value == "spec");
               CHECK(rule.productions.front().symbols[0].label == "prep");
               CHECK(rule.productions.front().symbols[1].kind == cpf::symbol_kind::literal);
               CHECK(rule.productions.front().symbols[1].value == "_value");
               CHECK(rule.productions.front().symbols[1].label == "suffix");
               continue;
            }
            continue;
         }
         instantiated = true;
         REQUIRE(rule.productions.size() == 1);
         REQUIRE(rule.productions.front().symbols.size() == 3);
         CHECK(rule.productions.front().symbols[0].kind == cpf::symbol_kind::literal);
         CHECK(rule.productions.front().symbols[0].value == "(");
         CHECK(rule.productions.front().symbols[0].label == "open");
         CHECK(rule.productions.front().symbols[1].kind == cpf::symbol_kind::reference);
         CHECK(rule.productions.front().symbols[1].value == "identifier");
         CHECK(rule.productions.front().symbols[1].label == "value");
         CHECK(rule.productions.front().symbols[2].kind == cpf::symbol_kind::literal);
         CHECK(rule.productions.front().symbols[2].value == ")");
         CHECK(rule.productions.front().symbols[2].label == "close");
      }
      CHECK(instantiated);
      CHECK(nested_instantiated);
      CHECK(nested_payload_instantiated);
      CHECK(higher_order_instantiated);
      CHECK(higher_order_payload_instantiated);
   }

   TEST_CASE("unsupported unlabeled quantified group captures report expressive parser errors") {
      auto capture_error = [](std::string_view source) -> std::string {
         try {
            static_cast<void>(cpf::parse_grammar(source));
         } catch (const std::runtime_error& error) {
            return error.what();
         }
         return std::string{};
      };


      SUBCASE("quantified groups cannot contain labeled captures") {
         auto message = capture_error(R"(
            expr -> ('x':value | 'y':value)+;
         )");

         REQUIRE_FALSE(message.empty());
         CHECK(message.find("Quantified groups cannot contain labeled captures") != std::string::npos);
      }
   }

   TEST_CASE("grammar loader preprocesses @import directives across multiple files") {
      auto test_directory = std::filesystem::temp_directory_path() / "cpf_import_loader_tests";
      std::filesystem::remove_all(test_directory);

      SUBCASE("relative @import directives are expanded recursively before parsing") {
         write_file(test_directory / "parts" / "numbers.cpf", R"(
            imported_number -> r'[0-9]+':value;
         )");
         write_file(test_directory / "parts" / "expr.cpf", R"(
            @import "numbers.cpf";
            imported_expr -> imported_number;
         )");
         write_file(test_directory / "root.cpf", R"(
            @import 'parts/expr.cpf';
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
            @import 'first.cpf';
            @import 'second.cpf';
         )");

         auto grammar = cpf::parse_grammar_file(test_directory / "root.cpf");
         auto* shared_expr = grammar.find_rule("shared_expr");
         REQUIRE(shared_expr != nullptr);
         REQUIRE(shared_expr->productions.size() == 2);
         CHECK(shared_expr->productions[0].definition == 0);
         CHECK(shared_expr->productions[1].definition == 1);
      }

      SUBCASE("duplicate @import expansions behave like textual inclusion") {
         write_file(test_directory / "leaf.cpf", R"(
            imported_leaf -> 'x':value;
         )");
         write_file(test_directory / "left.cpf", R"(
            @import 'leaf.cpf';
            imported_left -> imported_leaf;
         )");
         write_file(test_directory / "right.cpf", R"(
            @import 'leaf.cpf';
            imported_right -> imported_leaf;
         )");
         write_file(test_directory / "root.cpf", R"(
            @import 'left.cpf';
            @import 'right.cpf';
         )");

         auto loaded = cpf::load_grammar_file(test_directory / "root.cpf");
         CHECK(loaded.dependencies.size() == 4);
          auto* imported_leaf = loaded.parsed_grammar.find_rule("imported_leaf");
          REQUIRE(imported_leaf != nullptr);
          CHECK(imported_leaf->productions.size() == 2);
      }

      SUBCASE("@import cycles report a clear loader error") {
         write_file(test_directory / "first.cpf", "@import 'second.cpf';\ncycle_first -> 'a':value;\n");
         write_file(test_directory / "second.cpf", "@import 'first.cpf';\ncycle_second -> 'b':value;\n");

         CHECK_THROWS_WITH_AS(cpf::load_grammar_file(test_directory / "first.cpf"),
                              doctest::Contains("Grammar import cycle detected"), std::runtime_error);
      }

      SUBCASE("double-quoted @import paths preserve escaped quotes and relative resolution") {
         auto quoted_directory = test_directory / "quoted imports";
#if defined(_WIN32)
         auto child_path = quoted_directory / "child_grammar.cpf";
          auto root_source = std::string{"@import \"child_grammar.cpf\";\nquoted_root -> quoted_child;\n"};
#else
         auto child_path = quoted_directory / "child\"grammar.cpf";
          auto root_source = std::string{"@import \"child\\\"grammar.cpf\";\nquoted_root -> quoted_child;\n"};
#endif
         write_file(child_path, "quoted_child -> \"x\":value;\n");
         write_file(quoted_directory / "root.cpf", root_source);

         auto loaded = cpf::load_grammar_file(quoted_directory / "root.cpf");
         CHECK(loaded.dependencies.size() == 2);
         CHECK(loaded.parsed_grammar.find_rule("quoted_root") != nullptr);
         CHECK(loaded.parsed_grammar.find_rule("quoted_child") != nullptr);
      }

      SUBCASE("imported token declarations cannot conflict with parser rules") {
         write_file(test_directory / "tokens.cpf", R"(
            token identifier -> r'[A-Za-z_]+';
         )");
         write_file(test_directory / "rules.cpf", R"(
            identifier -> 'x';
         )");
         write_file(test_directory / "root.cpf", R"(
            @import 'tokens.cpf';
            @import 'rules.cpf';
         )");

         CHECK_THROWS_WITH_AS(cpf::load_grammar_file(test_directory / "root.cpf"),
                              doctest::Contains("cannot be declared as both token and non-token"),
                              std::runtime_error);
      }

      SUBCASE("@import is preprocessed before directives and rules are parsed") {
         write_file(test_directory / "trivia.cpf", R"(
            @whitespace ws;
            skip ws -> r'[ \t\r\n]+';
         )");
         write_file(test_directory / "root.cpf", R"(
            @import 'trivia.cpf';
            expr -> 'x':value;
         )");

         auto grammar = cpf::parse_grammar_file(test_directory / "root.cpf");
         REQUIRE(grammar.whitespace_rule.has_value());
         CHECK(*grammar.whitespace_rule == "ws");
         CHECK(grammar.find_skip_rule("ws") != nullptr);
         CHECK(grammar.find_rule("expr") != nullptr);
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

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
         number          [lbl = 'num']               -> r'[0-9]+':value;
      )");

      auto generated = cpf::generate_code(grammar, "calculator");

      SUBCASE("header output exposes node types and visitors") {
         CHECK(generated.header.find("struct expression : cpf::node") != std::string::npos);
         CHECK(generated.header.find("struct number : expression") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string value;") != std::string::npos);
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
         default_number -> r'[0-9]+':value;
      )");

      auto generated = cpf::generate_code(grammar, "default_attrs");

      SUBCASE("default precedence follows source order for infix rules") {
         CHECK(generated.source.find("int precedence_of_default_expr(const default_expr& node)") != std::string::npos);
         CHECK(generated.source.find("if (auto* value = dynamic_cast<const default_add*>(&node))") != std::string::npos);
         CHECK(generated.source.find("if (auto* value = dynamic_cast<const default_subtract*>(&node))") != std::string::npos);
         CHECK(generated.source.find("if (auto* value = dynamic_cast<const default_multiply*>(&node))") != std::string::npos);
         CHECK(generated.source.find("validate_default_expr_child(*value->left, 1, true, true)") != std::string::npos);
         CHECK(generated.source.find("validate_default_expr_child(*value->right, 3, true, false)") != std::string::npos);
      }

      SUBCASE("default associativity is left and explicit labels generate matched_string fields") {
         CHECK(generated.source.find("validate_default_expr_child(*value->right, 2, true, false)") != std::string::npos);
         CHECK(generated.header.find("struct default_number : default_expr") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string value;") != std::string::npos);
      }
   }

   TEST_CASE("default labels fall back to rule identifiers for relative precedence references") {
      auto grammar = cpf::parse_grammar(R"(
         default_label_expr -> default_label_add | default_label_multiply | default_label_number;
         default_label_add [prec < default_label_multiply] -> default_label_expr:left '+':op default_label_expr:right;
         default_label_multiply -> default_label_expr:left '*':op default_label_expr:right;
         default_label_number -> r'[0-9]+':value;
      )");

      auto generated = cpf::generate_code(grammar, "default_labels");

      CHECK(generated.source.find("int precedence_of_default_label_expr(const default_label_expr& node)") != std::string::npos);
      CHECK(generated.source.find("if (auto* value = dynamic_cast<const default_label_add*>(&node))") != std::string::npos);
      CHECK(generated.source.find("if (auto* value = dynamic_cast<const default_label_multiply*>(&node))") != std::string::npos);
      CHECK(generated.source.find("validate_default_label_expr_child(*value->right, 1, true, false)") != std::string::npos);
      CHECK(generated.source.find("validate_default_label_expr_child(*value->right, 2, true, false)") != std::string::npos);
   }

   TEST_CASE("duplicate concrete rule declarations generate one merged node type") {
      auto grammar = cpf::parse_grammar(R"(
         merged_message -> merged_greeting | merged_farewell;
         merged_greeting -> 'hello':text;
         merged_farewell -> 'bye':text;

         merged_wrapper -> merged_message:payload '!':suffix;
         merged_wrapper -> merged_greeting:payload '?':suffix;

         merged_expr -> merged_binary | merged_number;
         merged_binary -> merged_expr:left '+':op merged_expr:right;
         merged_binary -> merged_expr:left '*':op merged_expr:right;
         merged_number -> r'[0-9]+':value;
      )");

      auto generated = cpf::generate_code(grammar, "merged_defs");

      SUBCASE("merged fields resolve to a common node base") {
         CHECK(generated.header.find("struct merged_wrapper : cpf::node") != std::string::npos);
         CHECK(generated.header.find("std::unique_ptr<merged_message> payload;") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string suffix;") != std::string::npos);
      }

      SUBCASE("generated source stamps and preserves matched definitions") {
         CHECK(generated.source.find("node->definition = 0;") != std::string::npos);
         CHECK(generated.source.find("node->definition = 1;") != std::string::npos);
         CHECK(generated.source.find("copy->definition = definition;") != std::string::npos);
         CHECK(generated.source.find("switch (value->definition)") != std::string::npos);
         CHECK(generated.source.find("std::unique_ptr<merged_message>{static_cast<merged_message*>(child_0.release())}") != std::string::npos);
      }
   }

   TEST_CASE("conflicting merged member resolutions are rejected with expressive errors") {
      auto capture_error = [](const cpf::grammar& grammar) {
         try {
            static_cast<void>(cpf::generate_code(grammar, "conflicting_rule"));
         } catch (const std::runtime_error& error) {
            return std::string{error.what()};
         }
         return std::string{};
      };

      SUBCASE("terminal and node labels cannot be merged") {
         auto grammar = cpf::parse_grammar(R"(
            number -> r'[0-9]+';
            conflicting_value -> 'x':value;
            conflicting_value -> number:value;
         )");

         auto message = capture_error(grammar);
         REQUIRE_FALSE(message.empty());
         CHECK(message.find("label 'value'") != std::string::npos);
         CHECK(message.find("conflicting member types") != std::string::npos);
      }

      SUBCASE("unrelated node families report the common-type failure") {
         auto grammar = cpf::parse_grammar(R"(
            first -> 'a':text;
            second -> 'b':text;
            conflicting_node -> first:value;
            conflicting_node -> second:value;
         )");

         auto message = capture_error(grammar);
         REQUIRE_FALSE(message.empty());
         CHECK(message.find("label 'value'") != std::string::npos);
         CHECK(message.find("cannot resolve a common member type") != std::string::npos);
      }
   }

   TEST_CASE("quantified grammar syntax generates optional and vector members") {
      auto grammar = cpf::parse_grammar(R"(
         quant_choice -> quant_alpha | quant_digit;
         quant_alpha -> 'a':text;
         quant_digit -> r'[0-9]':text;

         maybe_choice -> quant_choice?:value;
         star_choice -> quant_choice*:values;
         plus_choice -> quant_choice+:values;
         exact_digit_triplet -> r'[0-9]'{3}:digits;
         optional_terminal -> 'go'?:marker;
      )");

      auto generated = cpf::generate_code(grammar, "quantified");

      SUBCASE("header output exposes the expected quantified member types") {
         CHECK(generated.header.find("std::unique_ptr<quant_choice> value;") != std::string::npos);
         CHECK(generated.header.find("std::vector<std::unique_ptr<quant_choice>> values;") != std::string::npos);
         CHECK(generated.header.find("std::vector<cpf::matched_string> digits;") != std::string::npos);
         CHECK(generated.header.find("std::optional<cpf::matched_string> marker;") != std::string::npos);
      }

      SUBCASE("unlabeled terminals are not captured into implicit fields") {
         auto silent_grammar = cpf::parse_grammar(R"(
            silent -> 'x';
         )");

         auto silent_generated = cpf::generate_code(silent_grammar, "silent");
         CHECK(silent_generated.header.find("struct silent : cpf::node") != std::string::npos);
         CHECK(silent_generated.header.find("value;") == std::string::npos);
      }

      SUBCASE("source output lowers quantified syntax through helper extractors") {
         CHECK(generated.source.find("extract_helper_") != std::string::npos);
         CHECK(generated.source.find("Unknown quantified helper production") != std::string::npos);
         CHECK(generated.source.find("node->value = extract_helper_") != std::string::npos);
         CHECK(generated.source.find("node->values = extract_helper_") != std::string::npos);
         CHECK(generated.source.find("node->digits = extract_helper_") != std::string::npos);
         CHECK(generated.source.find("node->marker = extract_helper_") != std::string::npos);
      }
   }
}


#include "ambiguous_choice.h"
#include "calculator.h"
#include "error_choice.h"
#include "grouped.h"
#include "imported_bundle.h"
#include "message.h"
#include "merged_definitions.h"
#include "namespaced_calculator.h"
#include "quantified.h"

#include "support/doctest.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {
   namespace namespaced = generated::fixtures;

   struct calculator_visitor {
      auto visit(auto& node) const { return ::visit(node, *this); }
      int operator()(const addition& node) const { return visit(*node.left) + visit(*node.right); }
      int operator()(const subtraction& node) const { return visit(*node.left) - visit(*node.right); }
      int operator()(const multiplication& node) const { return visit(*node.left) * visit(*node.right); }
      int operator()(const division& node) const { return visit(*node.left) / visit(*node.right); }
      int operator()(const number& node) const { return std::stoi(node.value.text); }
   };

   struct merged_expr_visitor {
      auto visit(auto& node) const { return ::visit(node, *this); }

      int operator()(const merged_binary& node) const {
         auto left = visit(*node.left);
         auto right = visit(*node.right);
         if (node.op.text == "+") {
            return left + right;
         }
         return left * right;
      }

      int operator()(const merged_number& node) const {
         return std::stoi(node.value.text);
      }
   };

   struct quant_choice_visitor {
      std::string operator()(const quant_alpha& node) const { return node.text.text; }
      std::string operator()(const quant_digit& node) const { return node.text.text; }
   };

   struct imported_bundle_visitor {
      std::string operator()(const imported_bundle_word& node) const {
         return node.value.text;
      }

      std::string operator()(const imported_bundle_greeting& node) const {
         return visit(*node.text, *this) + node.suffix->value.text;
      }

      std::string operator()(const imported_bundle_parting& node) const {
         return node.text.text;
      }
   };

   struct namespaced_calculator_visitor {
      auto visit(auto& node) const { return namespaced::visit(node, *this); }
      int operator()(const namespaced::addition& node) const { return visit(*node.left) + visit(*node.right); }
      int operator()(const namespaced::number& node) const { return std::stoi(node.value.text); }
   };

   int evaluate(std::string_view input) {
      auto result = expression::parse(input);
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);
      return visit(*result.forest.front(), calculator_visitor{});
   }

   int evaluate_merged(std::string_view input) {
      auto result = merged_expr::parse(input);
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);
      return visit(*result.forest.front(), merged_expr_visitor{});
   }

   int evaluate_namespaced(std::string_view input) {
      auto result = namespaced::expression::parse(input);
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);
      return namespaced::visit(*result.forest.front(), namespaced_calculator_visitor{});
   }

   std::vector<std::string> collect_quant_values(const std::vector<std::unique_ptr<quant_choice>>& values) {
      std::vector<std::string> result;
      for (const auto& value : values) {
         REQUIRE(value != nullptr);
         result.emplace_back(visit(*value, quant_choice_visitor{}));
      }
      return result;
   }
}

TEST_SUITE("generated.runtime") {
   TEST_CASE("calculator grammar matches the expected generated runtime behavior") {
      SUBCASE("README flow parses, evaluates, and streams") {
         auto result = expression::parse("1 + 2 * 3");

         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         auto& tree = result.forest.front();
         CHECK(visit(*tree, calculator_visitor{}) == 7);

         std::ostringstream stream;
         stream << *tree;
         CHECK(stream.str().find("addition(") != std::string::npos);
         CHECK(stream.str().find("multiplication(") != std::string::npos);
      }

      SUBCASE("left associativity and explicit terminal captures are preserved") {
         CHECK(evaluate("8 / 2 / 2") == 2);
         CHECK(evaluate("10 - 3 - 2") == 5);
         CHECK(evaluate(" 6 + 4 ") == 10);

          CHECK(expression::complexity_inputs(0).size() >= 2);
          CHECK(number::complexity_inputs(0).size() >= 2);

          const auto& expression_recomputed = expression::recompute_complexity(0);
          const auto& recomputed = number::recompute_complexity(0);
          CHECK(&expression_recomputed == &expression::Complexity[0]);
          CHECK(&recomputed == &number::Complexity[0]);
          CHECK_FALSE(expression::Complexity[0].summary.empty());
          CHECK_FALSE(number::Complexity[0].big_o.empty());
          CHECK_FALSE(expression_recomputed.summary.empty());
          CHECK_FALSE(recomputed.summary.empty());
          CHECK(recomputed.estimate(8.0) > 0.0);

         auto number_result = number::parse("42");
         REQUIRE(number_result.success);
         REQUIRE(number_result.forest.size() == 1);
         CHECK(number_result.forest.front()->value.text == "42");
      }

      SUBCASE("nodes and terminal members expose their matched source ranges") {
         auto result = expression::parse(" 1 + 23 ");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         auto* root = dynamic_cast<const addition*>(result.forest.front().get());
         REQUIRE(root != nullptr);
         CHECK(root->range.begin.offset == 1);
         CHECK(root->range.begin.column == 2);
         CHECK(root->range.end.offset == 7);
         CHECK(root->range.end.column == 8);
         CHECK(root->op.text == "+");
         CHECK(root->op.range.begin.offset == 3);
         CHECK(root->op.range.begin.column == 4);
         CHECK(root->op.range.end.offset == 4);
         CHECK(root->op.range.end.column == 5);

         REQUIRE(root->right != nullptr);
         auto* rhs = dynamic_cast<const number*>(root->right.get());
         REQUIRE(rhs != nullptr);
         CHECK(rhs->value.text == "23");
         CHECK(rhs->value.range.begin.offset == 5);
         CHECK(rhs->value.range.end.offset == 7);
         CHECK(rhs->range.begin.offset == 5);
         CHECK(rhs->range.end.offset == 7);
      }

      SUBCASE("cloning and recursive visiting traverse the full tree") {
         auto result = expression::parse("1 + 2 * 3");
         REQUIRE(result.success);
         auto cloned = result.forest.front()->clone();
         REQUIRE(cloned != nullptr);
         CHECK(visit(*cloned, calculator_visitor{}) == 7);

         std::vector<std::string> visited;
         visit_recursive(*cloned, [&](const auto& node) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, addition>) {
               visited.push_back("addition");
            } else if constexpr (std::is_same_v<node_t, multiplication>) {
               visited.push_back("multiplication");
            } else if constexpr (std::is_same_v<node_t, number>) {
               visited.push_back("number");
            }
         });

         CHECK(visited == std::vector<std::string>{"addition", "number", "multiplication", "number", "number"});
      }

      SUBCASE("parse failures expose structured error details") {
         auto result = expression::parse("1 +");

         CHECK_FALSE(result.success);
         CHECK(result.error.line == 1);
         CHECK(result.error.column >= 3);
         CHECK_FALSE(result.error.expected.empty());
         CHECK(result.error.message.find("expected") != std::string::npos);
         CHECK(result.error.message.find("found") != std::string::npos);
         CHECK_FALSE(result.error.notes.empty());
         CHECK(result.error.message.find("while parsing rule 'number'") != std::string::npos);
      }

      SUBCASE("operator-sequence failures point at the exact offending token") {
         auto result = expression::parse("1 + * 2");

         CHECK_FALSE(result.success);
         CHECK(result.error.line == 1);
         CHECK(result.error.column == 5);
         CHECK(result.error.found == "\"*\"");
         CHECK(result.error.message.find("pattern [0-9]+") != std::string::npos);
         CHECK(result.error.message.find("while parsing rule 'number'") != std::string::npos);
      }

      SUBCASE("multiline failures stay close to the actual broken line") {
         auto result = expression::parse("1 +\n* 2");

         CHECK_FALSE(result.success);
         CHECK(result.error.line == 2);
         CHECK(result.error.column == 1);
         CHECK(result.error.found == "\"*\"");
         CHECK(result.error.message.find("line 2, column 1") != std::string::npos);
      }
   }

   TEST_CASE("namespaced generated grammars remain fully usable through the CMake helper") {
      SUBCASE("parse entry points, visitors, and streaming stay available inside the namespace") {
         auto result = namespaced::expression::parse("1 + 2 + 3");

         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);
         CHECK(namespaced::visit(*result.forest.front(), namespaced_calculator_visitor{}) == 6);
         CHECK(evaluate_namespaced("4 + 5") == 9);

         std::ostringstream stream;
         stream << *result.forest.front();
         CHECK(stream.str().find("addition(") != std::string::npos);
         CHECK(stream.str().find("number(") != std::string::npos);
      }

      SUBCASE("recursive visiting stays reachable through the generated namespace") {
         auto result = namespaced::expression::parse("7 + 8");

         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         std::vector<std::string> visited;
         namespaced::visit_recursive(*result.forest.front(), [&](const auto& node) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, namespaced::addition>) {
               visited.push_back("addition");
            } else if constexpr (std::is_same_v<node_t, namespaced::number>) {
               visited.push_back("number");
            }
         });

         CHECK(visited == std::vector<std::string>{"addition", "number", "number"});
      }
   }

   TEST_CASE("choice-only inheritance grammars stay visitable and printable") {
      SUBCASE("concrete rule parsing keeps concrete fields") {
         auto greeting_result = greeting::parse("hello");
         REQUIRE(greeting_result.success);
         REQUIRE(greeting_result.forest.size() == 1);
         CHECK(greeting_result.forest.front()->text.text == "hello");
         const auto& recomputed = greeting::recompute_complexity(0);
         CHECK(&recomputed == &greeting::Complexity[0]);
         CHECK_FALSE(greeting::Complexity[0].summary.empty());
         CHECK(recomputed.estimate(5.0) > 0.0);
      }

      SUBCASE("base rule parsing dispatches to the derived node") {
         auto message_result = message::parse("hello");
         REQUIRE(message_result.success);
         REQUIRE(message_result.forest.size() == 1);

         auto text = visit(*message_result.forest.front(), [](const greeting& node) {
            return node.text.text;
         });
         CHECK(text == "hello");

         std::ostringstream stream;
         stream << *message_result.forest.front();
         CHECK(stream.str() == "greeting(text=\"hello\")");
      }
   }

   TEST_CASE("ambiguous choice grammars return a forest with multiple matching derivations") {
      auto first_result = ambiguous_first::parse("x");
      REQUIRE(first_result.success);
      REQUIRE(first_result.forest.size() == 1);
      CHECK(first_result.forest.front()->text.text == "x");

      auto second_result = ambiguous_second::parse("x");
      REQUIRE(second_result.success);
      REQUIRE(second_result.forest.size() == 1);
      CHECK(second_result.forest.front()->text.text == "x");

      auto ambiguous_result = ambiguous_expr::parse("x");
      REQUIRE(ambiguous_result.success);
      REQUIRE(ambiguous_result.forest.size() == 2);

      auto first_seen = false;
      auto second_seen = false;
      for (const auto& tree : ambiguous_result.forest) {
         if (dynamic_cast<const ambiguous_first*>(tree.get()) != nullptr) {
            first_seen = true;
         }
         if (dynamic_cast<const ambiguous_second*>(tree.get()) != nullptr) {
            second_seen = true;
         }
      }

      CHECK(first_seen);
      CHECK(second_seen);
   }

   TEST_CASE("choice-rule failures merge expectations from every matching branch") {
      auto result = choice_message::parse("help");

      CHECK_FALSE(result.success);
      CHECK(result.error.line == 1);
      CHECK(result.error.column == 1);
      CHECK(result.error.found == "\"help\"");
      CHECK(result.error.message.find("\"hello\"") != std::string::npos);
      CHECK(result.error.message.find("\"world\"") != std::string::npos);
      CHECK(result.error.message.find("while parsing rule 'say_hello'") != std::string::npos);
      CHECK(result.error.message.find("while parsing rule 'say_world'") != std::string::npos);
      CHECK(result.error.message.find("while matching base rule 'choice_message'") != std::string::npos);
   }

   TEST_CASE("merged concrete rule definitions keep one generated type and mark the matched definition") {
      SUBCASE("ambiguous same-type parses remain distinguishable through node::definition") {
         auto result = repeated_token::parse("x");

         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 2);

         std::vector<std::size_t> definitions;
         for (const auto& tree : result.forest) {
            definitions.push_back(tree->definition);
            CHECK(tree->text.text == "x");
         }

         CHECK(definitions == std::vector<std::size_t>{0, 1});
      }

      SUBCASE("shared labeled references resolve to the nearest common generated base") {
         auto exclamation = merged_wrapper::parse("bye!");
         REQUIRE(exclamation.success);
         REQUIRE(exclamation.forest.size() == 1);
         CHECK(exclamation.forest.front()->definition == 0);
         CHECK(exclamation.forest.front()->suffix.text == "!");
         REQUIRE(exclamation.forest.front()->payload != nullptr);
         CHECK(dynamic_cast<const merged_farewell*>(exclamation.forest.front()->payload.get()) != nullptr);

         auto question = merged_wrapper::parse("hello?");
         REQUIRE(question.success);
         REQUIRE(question.forest.size() == 1);
         CHECK(question.forest.front()->definition == 1);
         CHECK(question.forest.front()->suffix.text == "?");
         REQUIRE(question.forest.front()->payload != nullptr);
         CHECK(dynamic_cast<const merged_greeting*>(question.forest.front()->payload.get()) != nullptr);

         auto cloned = question.forest.front()->clone();
         REQUIRE(cloned != nullptr);
         CHECK(cloned->definition == 1);
         REQUIRE(cloned->payload != nullptr);
         CHECK(dynamic_cast<const merged_greeting*>(cloned->payload.get()) != nullptr);
      }

      SUBCASE("default precedence is still evaluated per merged definition") {
         auto result = merged_expr::parse("1 + 2 * 3");

         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);
         CHECK(visit(*result.forest.front(), merged_expr_visitor{}) == 7);
         CHECK(evaluate_merged("4 * 5 + 6") == 26);

         auto* root = dynamic_cast<const merged_binary*>(result.forest.front().get());
         REQUIRE(root != nullptr);
         CHECK(root->definition == 0);
         REQUIRE(root->right != nullptr);

         auto* right = dynamic_cast<const merged_binary*>(root->right.get());
         REQUIRE(right != nullptr);
         CHECK(right->definition == 1);
      }

      SUBCASE("complexity samples and stored estimates are tracked per merged definition") {
         REQUIRE(merged_binary::Complexity.size() == 2);
         auto plus_inputs = merged_binary::complexity_inputs(0);
         auto multiply_inputs = merged_binary::complexity_inputs(1);
         REQUIRE(plus_inputs.size() >= 2);
         REQUIRE(multiply_inputs.size() >= 2);
         CHECK(std::string{plus_inputs.back()}.find('+') != std::string::npos);
         CHECK(std::string{multiply_inputs.back()}.find('*') != std::string::npos);

         const auto& recomputed_plus = merged_binary::recompute_complexity(0);
         const auto& recomputed = merged_binary::recompute_complexity(1);
         CHECK(&recomputed_plus == &merged_binary::Complexity[0]);
         CHECK(&recomputed == &merged_binary::Complexity[1]);
         CHECK_FALSE(merged_binary::Complexity[0].summary.empty());
         CHECK_FALSE(merged_binary::Complexity[1].summary.empty());
         CHECK_FALSE(recomputed.summary.empty());
         CHECK(recomputed.estimate(8.0) > 0.0);

         CHECK_THROWS_AS(merged_binary::complexity_inputs(2), std::out_of_range);
         CHECK_THROWS_AS(merged_binary::recompute_complexity(2), std::out_of_range);
      }
   }

   TEST_CASE("quantified grammar syntax lowers to the existing Earley runtime") {
      SUBCASE("optional references can be absent or present") {
         auto empty = maybe_choice::parse("");
         REQUIRE(empty.success);
         REQUIRE(empty.forest.size() == 1);
         CHECK(empty.forest.front()->value == nullptr);

         auto present = maybe_choice::parse("a");
         REQUIRE(present.success);
         REQUIRE(present.forest.size() == 1);
         REQUIRE(present.forest.front()->value != nullptr);
         CHECK(visit(*present.forest.front()->value, quant_choice_visitor{}) == "a");
      }

      SUBCASE("zero-or-more references produce an empty-or-filled vector") {
         auto empty = star_choice::parse("");
         REQUIRE(empty.success);
         REQUIRE(empty.forest.size() == 1);
         CHECK(empty.forest.front()->values.empty());

         auto repeated = star_choice::parse("a7a");
         REQUIRE(repeated.success);
         REQUIRE(repeated.forest.size() == 1);
         CHECK(collect_quant_values(repeated.forest.front()->values) == std::vector<std::string>{"a", "7", "a"});
      }

      SUBCASE("one-or-more references require at least one match") {
         auto missing = plus_choice::parse("");
         CHECK_FALSE(missing.success);

         auto repeated = plus_choice::parse("7a");
         REQUIRE(repeated.success);
         REQUIRE(repeated.forest.size() == 1);
         CHECK(collect_quant_values(repeated.forest.front()->values) == std::vector<std::string>{"7", "a"});
      }

      SUBCASE("exact repetitions and optional terminals map to generated STL fields") {
         auto digits = exact_digit_triplet::parse("123");
         REQUIRE(digits.success);
         REQUIRE(digits.forest.size() == 1);
         REQUIRE(digits.forest.front()->digits.size() == 3);
         CHECK(digits.forest.front()->digits[0].text == "1");
         CHECK(digits.forest.front()->digits[1].text == "2");
         CHECK(digits.forest.front()->digits[2].text == "3");

         auto missing_terminal = optional_terminal::parse("");
         REQUIRE(missing_terminal.success);
         REQUIRE(missing_terminal.forest.size() == 1);
         CHECK_FALSE(missing_terminal.forest.front()->marker.has_value());

         auto present_terminal = optional_terminal::parse("go");
         REQUIRE(present_terminal.success);
         REQUIRE(present_terminal.forest.size() == 1);
         REQUIRE(present_terminal.forest.front()->marker.has_value());
         CHECK(present_terminal.forest.front()->marker->text == "go");
      }
   }

   TEST_CASE("grouped grammar syntax widens the frontend while still using the Earley backend") {
      SUBCASE("groups with inner alternatives flatten into the expected captures") {
         auto x_result = grouped_value::parse("x");
         REQUIRE(x_result.success);
         REQUIRE(x_result.forest.size() == 1);
         CHECK(x_result.forest.front()->text.text == "x");

         auto y_result = grouped_value::parse("y");
         REQUIRE(y_result.success);
         REQUIRE(y_result.forest.size() == 1);
         CHECK(y_result.forest.front()->text.text == "y");
      }

      SUBCASE("alternation can appear both inside and outside grouped sequences") {
         auto hello = grouped_sentence::parse("(hi)");
         REQUIRE(hello.success);
         REQUIRE(hello.forest.size() == 1);
         CHECK(hello.forest.front()->open.text == "(");
         CHECK(hello.forest.front()->text.text == "hi");
         CHECK(hello.forest.front()->close.text == ")");

         auto bye = grouped_sentence::parse("(bye)");
         REQUIRE(bye.success);
         REQUIRE(bye.forest.size() == 1);
         CHECK(bye.forest.front()->text.text == "bye");
      }

      SUBCASE("single-symbol grouped choices can be captured through a group label") {
         auto x = grouped_choice_value::parse("x");
         REQUIRE(x.success);
         REQUIRE(x.forest.size() == 1);
         CHECK(x.forest.front()->value.text == "x");

         auto y = grouped_choice_value::parse("y");
         REQUIRE(y.success);
         REQUIRE(y.forest.size() == 1);
         CHECK(y.forest.front()->value.text == "y");
      }

      SUBCASE("group labels on rule choices resolve to the nearest generated base type") {
         auto hello = grouped_choice_payload::parse("hello");
         REQUIRE(hello.success);
         REQUIRE(hello.forest.size() == 1);
         CHECK(std::holds_alternative<std::unique_ptr<grouped_choice_greeting>>(hello.forest.front()->payload));
         const auto& greeting = std::get<std::unique_ptr<grouped_choice_greeting>>(hello.forest.front()->payload);
         REQUIRE(greeting != nullptr);
         CHECK(greeting->definition == 0);

         auto bye = grouped_choice_payload::parse("bye");
         REQUIRE(bye.success);
         REQUIRE(bye.forest.size() == 1);
         CHECK(std::holds_alternative<std::unique_ptr<grouped_choice_farewell>>(bye.forest.front()->payload));
         const auto& farewell = std::get<std::unique_ptr<grouped_choice_farewell>>(bye.forest.front()->payload);
         REQUIRE(farewell != nullptr);
      }

      SUBCASE("quantifiers apply to groups as parse-shaping constructs") {
         auto repeated = grouped_repeat::parse("abba");
         REQUIRE(repeated.success);
         REQUIRE(repeated.forest.size() == 1);
         CHECK(repeated.forest.front()->range.begin.offset == 0);
         CHECK(repeated.forest.front()->range.end.offset == 4);

         auto missing = grouped_repeat::parse("");
         CHECK_FALSE(missing.success);

         auto exact = grouped_exact::parse("hahaha");
         REQUIRE(exact.success);
         REQUIRE(exact.forest.size() == 1);

         auto too_short = grouped_exact::parse("haha");
         CHECK_FALSE(too_short.success);
      }
   }

   TEST_CASE("imported grammars behave like one logical grammar file") {
      SUBCASE("transitive relative imports contribute generated rules") {
         auto hello = imported_bundle_message::parse("hello!");
         REQUIRE(hello.success);
         REQUIRE(hello.forest.size() == 1);
         CHECK(visit(*hello.forest.front(), imported_bundle_visitor{}) == "hello!");

         auto hi = imported_bundle_message::parse("hi!");
         REQUIRE(hi.success);
         REQUIRE(hi.forest.size() == 1);
         CHECK(visit(*hi.forest.front(), imported_bundle_visitor{}) == "hi!");
      }

      SUBCASE("imported sibling files can define additional concrete alternatives") {
         auto bye = imported_bundle_message::parse("bye");
         REQUIRE(bye.success);
         REQUIRE(bye.forest.size() == 1);
         CHECK(visit(*bye.forest.front(), imported_bundle_visitor{}) == "bye");
      }
   }
}



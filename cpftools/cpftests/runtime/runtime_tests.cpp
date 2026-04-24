#include "ambiguous_choice.h"
#include "calculator.h"
#include "error_choice.h"
#include "message.h"

#include "support/doctest.h"

#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace {
   struct calculator_visitor {
      int operator()(const addition& node) const { return visit(*node.left, *this) + visit(*node.right, *this); }
      int operator()(const subtraction& node) const { return visit(*node.left, *this) - visit(*node.right, *this); }
      int operator()(const multiplication& node) const { return visit(*node.left, *this) * visit(*node.right, *this); }
      int operator()(const division& node) const { return visit(*node.left, *this) / visit(*node.right, *this); }
      int operator()(const number& node) const { return std::stoi(node.value); }
   };

   int evaluate(std::string_view input) {
      auto result = expression::parse(input);
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);
      return visit(*result.forest.front(), calculator_visitor{});
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

      SUBCASE("left associativity and implicit values are preserved") {
         CHECK(evaluate("8 / 2 / 2") == 2);
         CHECK(evaluate("10 - 3 - 2") == 5);
         CHECK(evaluate(" 6 + 4 ") == 10);

         auto number_result = number::parse("42");
         REQUIRE(number_result.success);
         REQUIRE(number_result.forest.size() == 1);
         CHECK(number_result.forest.front()->value == "42");
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

   TEST_CASE("choice-only inheritance grammars stay visitable and printable") {
      SUBCASE("concrete rule parsing keeps concrete fields") {
         auto greeting_result = greeting::parse("hello");
         REQUIRE(greeting_result.success);
         REQUIRE(greeting_result.forest.size() == 1);
         CHECK(greeting_result.forest.front()->text == "hello");
      }

      SUBCASE("base rule parsing dispatches to the derived node") {
         auto message_result = message::parse("hello");
         REQUIRE(message_result.success);
         REQUIRE(message_result.forest.size() == 1);

         auto text = visit(*message_result.forest.front(), [](const greeting& node) {
            return node.text;
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
      CHECK(first_result.forest.front()->text == "x");

      auto second_result = ambiguous_second::parse("x");
      REQUIRE(second_result.success);
      REQUIRE(second_result.forest.size() == 1);
      CHECK(second_result.forest.front()->text == "x");

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
}



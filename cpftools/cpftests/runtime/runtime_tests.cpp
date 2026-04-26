#include "ambiguous_choice.h"
#include "calculator.h"
#include "custom_errors.h"
#include "error_choice.h"
#include "grouped.h"
#include "imported_bundle.h"
#include "lookahead.h"
#include "lexer_priority.h"
#include "merged_definitions.h"
#include "message.h"
#include "namespaced_calculator.h"
#include "quantified.h"
#include "templates.h"
#include "tokens.h"

#include "support/doctest.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {
   namespace namespaced = generated::fixtures;

   template<typename T>
   concept has_user_data_member = requires(T value) { value.user_data; };

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

      int operator()(const merged_number& node) const { return std::stoi(node.value.text); }
   };

   struct quant_choice_visitor {
      std::string operator()(const quant_alpha& node) const { return node.text.text; }
      std::string operator()(const quant_digit& node) const { return node.text.text; }
   };

   struct imported_bundle_visitor {
      std::string operator()(const imported_bundle_word& node) const { return node.value.text; }

      std::string operator()(const imported_bundle_greeting& node) const {
         return visit(*node.text, *this) + node.suffix->value.text;
      }

      std::string operator()(const imported_bundle_parting& node) const { return node.text.text; }
   };

   struct namespaced_calculator_visitor {
      auto visit(auto& node) const { return namespaced::visit(node, *this); }
      int operator()(const namespaced::addition& node) const { return visit(*node.left) + visit(*node.right); }
      int operator()(const namespaced::number& node) const { return std::stoi(node.value.text); }
   };

   struct exact_parent_visitor {
      addition* root = nullptr;
      multiplication* multiplication_node = nullptr;

      void operator()(multiplication& node, addition* parent) {
         CHECK(parent == root);
         multiplication_node = &node;
      }

      void operator()(number& node, multiplication* parent) {
         CHECK(parent == multiplication_node);
         node.value.text = "4";
      }

      template<typename Node, typename Parent>
      void operator()(Node&, Parent*) {}

      template<typename Node>
      void operator()(Node&) {}
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
      for (const auto& value: values) {
         REQUIRE(value != nullptr);
         result.emplace_back(visit(*value, quant_choice_visitor{}));
      }
      return result;
   }
} // namespace

TEST_SUITE("generated.runtime") {
   TEST_CASE("generated clone-bearing node types are move-only") {
      CHECK(std::is_move_constructible_v<expression>);
      CHECK(std::is_move_assignable_v<expression>);
      CHECK_FALSE(std::is_copy_constructible_v<expression>);
      CHECK_FALSE(std::is_copy_assignable_v<expression>);

      CHECK(std::is_move_constructible_v<number>);
      CHECK(std::is_move_assignable_v<number>);
      CHECK_FALSE(std::is_copy_constructible_v<number>);
      CHECK_FALSE(std::is_copy_assignable_v<number>);

      CHECK(std::is_move_constructible_v<namespaced::expression>);
      CHECK(std::is_move_assignable_v<namespaced::expression>);
      CHECK_FALSE(std::is_copy_constructible_v<namespaced::expression>);
      CHECK_FALSE(std::is_copy_assignable_v<namespaced::expression>);
   }

   TEST_CASE("calculator grammar matches the expected generated runtime behavior") {
      SUBCASE("README flow parses, evaluates, and streams") {
         auto result = expression::parse("1 + 2 * 3");

         REQUIRE(result.success);
         CHECK(result.status == cpf::parse_status::success);
         CHECK_FALSE(result.error.has_value());
         REQUIRE(result.forest.size() == 1);
         CHECK_FALSE(result.forest.front().has_materialized());

         auto& tree = result.forest.front();
         CHECK(visit(*tree, calculator_visitor{}) == 7);
         CHECK(tree.has_materialized());

         std::ostringstream stream;
         stream << *tree;
         CHECK(stream.str().find("addition(\n") != std::string::npos);
         CHECK(stream.str().find("  left =") != std::string::npos);
         CHECK(stream.str().find("    multiplication(\n") != std::string::npos);
      }

      SUBCASE("left associativity and explicit terminal captures are preserved") {
         CHECK(evaluate("8 / 2 / 2") == 2);
         CHECK(evaluate("10 - 3 - 2") == 5);
         CHECK(evaluate(" 6 + 4 ") == 10);

         CHECK(expression::complexity_inputs(0).size() >= 2);
         CHECK(number::complexity_inputs(0).size() >= 2);

         const auto& expression_recomputed = expression::recompute_complexity(0);
         const auto& recomputed = number::recompute_complexity(0);
         CHECK(&expression_recomputed == &expression::complexity(0));
         CHECK(&recomputed == &number::complexity(0));
         CHECK_FALSE(expression::complexity(0).summary.empty());
         CHECK_FALSE(number::complexity(0).big_o.empty());
         CHECK_FALSE(expression_recomputed.summary.empty());
         CHECK_FALSE(recomputed.summary.empty());
         CHECK(recomputed.estimate(8.0) >= 0.0);

         auto number_result = number::parse("42");
         REQUIRE(number_result.success);
         REQUIRE(number_result.forest.size() == 1);
         CHECK(number_result.forest.front()->value.text == "42");
      }

      SUBCASE("parse options can validate success without materializing the AST") {
         cpf::parse_options options;
         options.build_ast = false;
         auto result = expression::parse("1 + 2 * 3", options);
         REQUIRE(result.success);
         CHECK(result.status == cpf::parse_status::success);
         CHECK_FALSE(result.error.has_value());
         CHECK(result.forest.empty());
      }

      SUBCASE("recognize can validate syntax without building a parse forest") {
         auto success = expression::recognize("1 + 2 * 3");
         CHECK(success.success);
         CHECK_FALSE(success.error.has_value());

         auto failure = expression::recognize("1 +");
         CHECK_FALSE(failure.success);
         REQUIRE(failure.error.has_value());
         CHECK(failure.error->message.find("expected") != std::string::npos);
      }

      SUBCASE("generated lexers expose reusable token sequences for parse and recognize") {
         auto input = std::string{"1 + 2 * 3"};
         auto tokens = expression::lex(input);

         REQUIRE(tokens.size() == 5);
         CHECK(tokens.input == input);
         CHECK(tokens[0].text.text == "1");
         CHECK(tokens[1].text.text == "+");
         CHECK(tokens[2].text.text == "2");
         CHECK(tokens[3].text.text == "*");
         CHECK(tokens[4].text.text == "3");

         std::ostringstream token_stream;
         token_stream << tokens;
         CHECK(token_stream.str().find("token_sequence(\n") != std::string::npos);
         CHECK(token_stream.str().find("input = \"1 + 2 * 3\"") != std::string::npos);
         CHECK(token_stream.str().find("[3] { symbol = ") != std::string::npos);
         CHECK(token_stream.str().find("text = \"*\"") != std::string::npos);

         auto recognized = expression::recognize(tokens);
         CHECK(recognized.success);
         CHECK_FALSE(recognized.error.has_value());

         cpf::parse_options validate_only;
         validate_only.build_ast = false;
         auto validated = expression::parse(tokens, validate_only);
         REQUIRE(validated.success);
         CHECK(validated.status == cpf::parse_status::success);
         CHECK(validated.forest.empty());

         auto parsed = expression::parse(tokens);
         REQUIRE(parsed.success);
         REQUIRE(parsed.forest.size() == 1);
         CHECK(visit(*parsed.forest.front(), calculator_visitor{}) == 7);
      }

      SUBCASE("parse options can reject ambiguity before AST construction") {
         cpf::parse_options options;
         options.error_on_ambiguity = true;
         auto result = ambiguous_expr::parse("x", options);
         CHECK_FALSE(result.success);
         REQUIRE(result.error.has_value());
         CHECK(result.forest.empty());
         CHECK(result.status == cpf::parse_status::failure);
         CHECK(result.error->found.kind == cpf::parse_error_found_kind::ambiguous_parse);
         CHECK(result.error->message.find("unambiguous parse") != std::string::npos);
          CHECK(result.error->message.find("ambiguous_expr") != std::string::npos);
      }

      SUBCASE("allow_partial keeps ignored invalid input inside one recovered tree") {
         cpf::parse_options options;
         options.allow_partial = true;

         auto result = expression::parse("1 + * 2 + 3", options);

         REQUIRE(result.success);
         CHECK(result.partial);
         CHECK(result.status == cpf::parse_status::partial_success);
         REQUIRE(result.error.has_value());
         REQUIRE(result.forest.size() == 1);
         CHECK(result.forest.front().is_partial());
         CHECK(visit(*result.forest.front(), calculator_visitor{}) == 6);

         auto visited_numbers = std::size_t{0};
         auto visited_additions = std::size_t{0};
         visit_recursive(*result.forest.front(), [&](const auto& node) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, number>) {
               ++visited_numbers;
            } else if constexpr (std::is_same_v<node_t, addition>) {
               ++visited_additions;
            }
         });
         CHECK(visited_numbers == 3);
         CHECK(visited_additions >= 1);

         auto saw_ignored = false;
         auto saw_ignored_message = false;
         auto damage_count = std::size_t{0};
         for (const auto* damaged_node: result.forest.front().damaged_nodes()) {
            REQUIRE(damaged_node != nullptr);
            for (const auto& damage: damaged_node->damage()) {
               ++damage_count;
               if (damage.reason == cpf::node_damage_reason::ignored_invalid_input &&
                   damage.range.begin.offset == 4 && damage.range.end.offset == 5) {
                  saw_ignored = true;
                  if (damage.message.find("could not match") != std::string::npos) {
                     saw_ignored_message = true;
                  }
               }
            }
         }
         CHECK(damage_count >= 1);
         CHECK(saw_ignored);
         CHECK(saw_ignored_message);
         auto repaired = result.forest.front().try_repair_input("1 + * 2 + 3");
         REQUIRE(repaired.has_value());
         CHECK(*repaired == "1 +  2 + 3");
         CHECK(result.forest.front().try_repair_input("1 + 2 + 3") == std::nullopt);

         auto cloned = result.forest.front()->clone();
         REQUIRE(cloned != nullptr);
         auto cloned_damage_count = std::size_t{0};
         visit_recursive(*cloned, [&](const auto& node) {
            if (node.is_damaged()) {
               cloned_damage_count += node.damage().size();
            }
         });
         CHECK(cloned_damage_count == damage_count);
      }

      SUBCASE("allow_partial can insert virtual literals inside one recovered tree") {
         cpf::parse_options options;
         options.allow_partial = true;

         auto result = grouped_sentence::parse("(hi", options);

         REQUIRE(result.success);
         CHECK(result.partial);
         CHECK(result.status == cpf::parse_status::partial_success);
         REQUIRE(result.error.has_value());
         REQUIRE(result.forest.size() == 1);
         CHECK(result.forest.front().is_partial());
         CHECK(result.forest.front()->open.text == "(");
         CHECK(result.forest.front()->text.text == "hi");
         CHECK(result.forest.front()->close.text == ")");
         CHECK(result.forest.front()->close.range.begin.offset == 3);
         CHECK(result.forest.front()->close.range.end.offset == 3);

         std::vector<std::string> visited;
         visit_recursive(*result.forest.front(), [&](const auto& node) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, grouped_sentence>) {
               visited.emplace_back("grouped_sentence");
            }
         });
         CHECK(visited == std::vector<std::string>{"grouped_sentence"});

         auto saw_inserted = false;
         auto saw_inserted_message = false;
         for (const auto* damaged_node: result.forest.front().damaged_nodes()) {
            REQUIRE(damaged_node != nullptr);
            for (const auto& damage: damaged_node->damage()) {
               if (damage.reason == cpf::node_damage_reason::inserted_virtual_token) {
                  saw_inserted = true;
                  if (damage.message.find("available tokens") != std::string::npos) {
                     saw_inserted_message = true;
                  }
               }
            }
         }
         CHECK(saw_inserted);
         CHECK(saw_inserted_message);
         auto repaired = result.forest.front().try_repair_input("(hi");
         REQUIRE(repaired.has_value());
         CHECK(*repaired == "(hi)");
         CHECK(result.forest.front().try_repair_input("(bye") == std::nullopt);
      }

      SUBCASE("allow_partial supports multiple damaged regions inside one recovered tree") {
         cpf::parse_options options;
         options.allow_partial = true;

         auto result = expression::parse("1 + * 2 + ) 3", options);

         REQUIRE(result.success);
         CHECK(result.partial);
         CHECK(result.status == cpf::parse_status::partial_success);
         REQUIRE(result.error.has_value());
         REQUIRE(result.forest.size() == 1);
         CHECK(result.forest.front().is_partial());
         CHECK(visit(*result.forest.front(), calculator_visitor{}) == 6);

         auto saw_star = false;
         auto saw_paren = false;
         auto damage_count = std::size_t{0};
         for (const auto* damaged_node: result.forest.front().damaged_nodes()) {
            REQUIRE(damaged_node != nullptr);
            for (const auto& damage: damaged_node->damage()) {
               ++damage_count;
               if (damage.reason == cpf::node_damage_reason::ignored_invalid_input && damage.range.begin.offset == 4) {
                  saw_star = true;
               }
               if (damage.reason == cpf::node_damage_reason::ignored_invalid_input && damage.range.begin.offset == 10) {
                  saw_paren = true;
               }
            }
         }
         CHECK(damage_count >= 2);
         CHECK(saw_star);
         CHECK(saw_paren);
      }

      SUBCASE("allow_partial can validate partial recovery without building AST nodes") {
         cpf::parse_options options;
         options.allow_partial = true;
         options.build_ast = false;

         auto result = expression::parse("1 + * 2 + 3", options);

         REQUIRE(result.success);
         CHECK(result.partial);
         CHECK(result.status == cpf::parse_status::partial_success);
         REQUIRE(result.error.has_value());
         CHECK(result.forest.empty());
      }

      SUBCASE("repaired_input returns the original input when the tree has no damages") {
         auto result = expression::parse(" 1 + 2 * 3 ");

         REQUIRE(result.success);
         CHECK(result.status == cpf::parse_status::success);
         CHECK_FALSE(result.error.has_value());
         REQUIRE(result.forest.size() == 1);
         CHECK_FALSE(result.forest.front().is_partial());
         auto repaired = result.forest.front().try_repair_input(" 1 + 2 * 3 ");
         REQUIRE(repaired.has_value());
         CHECK(*repaired == " 1 + 2 * 3 ");
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
         const auto& root = *result.forest.front();
         auto cloned = root.clone();
         REQUIRE(cloned != nullptr);
         CHECK(visit(*cloned, calculator_visitor{}) == 7);

         std::vector<std::string> visited;
         visit_recursive(*cloned, [&](const auto& node) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, addition>) {
               visited.emplace_back("addition");
            } else if constexpr (std::is_same_v<node_t, multiplication>) {
               visited.emplace_back("multiplication");
            } else if constexpr (std::is_same_v<node_t, number>) {
               visited.emplace_back("number");
            }
         });

         CHECK(visited == std::vector<std::string>{"addition", "number", "multiplication", "number", "number"});
      }

      SUBCASE("recursive visiting can expose parent context for scalar children") {
         auto result = expression::parse("1 + 2 * 3");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         std::vector<std::string> visited;
         visit_recursive(*result.forest.front(), [&](const auto& node, const auto* parent) {
            auto parent_name = std::string{"<root>"};
            if (dynamic_cast<const addition*>(parent) != nullptr) {
               parent_name = "addition";
            } else if (dynamic_cast<const multiplication*>(parent) != nullptr) {
               parent_name = "multiplication";
            } else if (dynamic_cast<const number*>(parent) != nullptr) {
               parent_name = "number";
            }

            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, addition>) {
               visited.emplace_back("addition<-" + parent_name);
            } else if constexpr (std::is_same_v<node_t, multiplication>) {
               visited.emplace_back("multiplication<-" + parent_name);
            } else if constexpr (std::is_same_v<node_t, number>) {
               visited.emplace_back("number<-" + parent_name);
            }
         });

         CHECK(visited == std::vector<std::string>{
                            "addition<-<root>",
                            "number<-addition",
                            "multiplication<-addition",
                            "number<-multiplication",
                            "number<-multiplication"
         });
      }

      SUBCASE("mutable recursive visiting can rewrite the AST in place") {
         auto result = expression::parse("1 + 2 * 3");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         visit_recursive(*result.forest.front(), [](auto& node) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, number>) {
               node.value.text = "1";
            }
         });

         CHECK(visit(*result.forest.front(), calculator_visitor{}) == 2);
      }

      SUBCASE("mutable recursive visiting can seed an explicit parent for subtree traversals") {
         auto result = expression::parse("1 + 2 * 3");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         auto* root = dynamic_cast<addition*>(result.forest.front().get());
         REQUIRE(root != nullptr);
         REQUIRE(root->right != nullptr);

         auto visitor = exact_parent_visitor{.root = root};
         visit_recursive(*root->right, visitor, root);

         CHECK(visit(*result.forest.front(), calculator_visitor{}) == 17);
      }

      SUBCASE("mutable visit dispatch can rewrite the selected concrete node in place") {
         auto result = expression::parse("1 + 2");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         visit(*result.forest.front(), [](auto& node) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, addition>) {
               auto replacement = std::make_unique<number>();
               replacement->value.text = "10";
               node.left = std::move(replacement);
            }
         });

         CHECK(visit(*result.forest.front(), calculator_visitor{}) == 12);
      }

      SUBCASE("parse failures expose structured error details") {
         auto result = expression::parse("1 +");

         CHECK_FALSE(result.success);
         CHECK(result.status == cpf::parse_status::failure);
         REQUIRE(result.error.has_value());
         CHECK(result.error->position.line == 1);
         CHECK(result.error->position.column >= 3);
         CHECK_FALSE(result.error->expected.empty());
         CHECK(result.error->message.find("expected") != std::string::npos);
         CHECK(result.error->message.find("found") != std::string::npos);
         CHECK_FALSE(result.error->notes.empty());
         CHECK(result.error->message.find("while parsing rule 'number'") != std::string::npos);
      }

      SUBCASE("operator-sequence failures point at the exact offending token") {
         auto result = expression::parse("1 + * 2");

         CHECK_FALSE(result.success);
         CHECK(result.status == cpf::parse_status::failure);
         REQUIRE(result.error.has_value());
         CHECK(result.error->position.line == 1);
         CHECK(result.error->position.column == 5);
         CHECK(result.error->found.kind == cpf::parse_error_found_kind::token);
         CHECK(result.error->found.text == "*");
         CHECK(result.error->message.find("pattern [0-9]+") != std::string::npos);
         CHECK(result.error->message.find("while parsing rule 'number'") != std::string::npos);
      }

      SUBCASE("multiline failures stay close to the actual broken line") {
         auto result = expression::parse("1 +\n* 2");

         CHECK_FALSE(result.success);
         CHECK(result.status == cpf::parse_status::failure);
         REQUIRE(result.error.has_value());
         CHECK(result.error->position.line == 2);
         CHECK(result.error->position.column == 1);
         CHECK(result.error->found.kind == cpf::parse_error_found_kind::token);
         CHECK(result.error->found.text == "*");
         CHECK(result.error->message.find("line 2, column 1") != std::string::npos);
      }
   }

   TEST_CASE("rule-level custom error annotations override generated nonterminal expectations") {
      auto result = custom_assignment::parse("=value");

      CHECK_FALSE(result.success);
      REQUIRE(result.error.has_value());
      CHECK(result.error->message.find("expected identifier") != std::string::npos);
      CHECK(std::find(result.error->expected.begin(), result.error->expected.end(), "expected identifier") !=
            result.error->expected.end());
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
         CHECK(stream.str().find("addition(\n") != std::string::npos);
         CHECK(stream.str().find("  left =") != std::string::npos);
         CHECK(stream.str().find("    number(\n") != std::string::npos);
      }

      SUBCASE("custom whitespace and comment skip rules work inside the generated namespace") {
         auto result = namespaced::expression::parse("\n 7 // keep skipping trivia\n + 8 \n");

         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);
         CHECK(namespaced::visit(*result.forest.front(), namespaced_calculator_visitor{}) == 15);
         CHECK(evaluate_namespaced("\t4 // comment\n + 5") == 9);
      }

      SUBCASE("recursive visiting stays reachable through the generated namespace") {
         auto result = namespaced::expression::parse("7 + 8");

         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         std::vector<std::string> visited;
         namespaced::visit_recursive(*result.forest.front(), [&](const auto& node) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, namespaced::addition>) {
               visited.emplace_back("addition");
            } else if constexpr (std::is_same_v<node_t, namespaced::number>) {
               visited.emplace_back("number");
            }
         });

         CHECK(visited == std::vector<std::string>{"addition", "number", "number"});
      }

      SUBCASE("recursive visiting can expose parent context inside the generated namespace") {
         auto result = namespaced::expression::parse("7 + 8");

         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         std::vector<std::string> visited;
         namespaced::visit_recursive(*result.forest.front(), [&](const auto& node, const auto* parent) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, namespaced::addition>) {
               CHECK(parent == nullptr);
               visited.emplace_back("addition");
            } else if constexpr (std::is_same_v<node_t, namespaced::number>) {
               CHECK(dynamic_cast<const namespaced::addition*>(parent) != nullptr);
               visited.emplace_back("number");
            }
         });

         CHECK(visited == std::vector<std::string>{"addition", "number", "number"});
      }
   }

   TEST_CASE("generated node templates default to no user data and can opt into caller payloads") {
      CHECK((std::is_same_v<expression::user_data_type, void>));
      CHECK_FALSE(has_user_data_member<expression>);
      CHECK((std::is_same_v<expression_node<std::string>::user_data_type, std::string>));
      CHECK(has_user_data_member<expression_node<std::string>>);

      auto default_result = expression::parse("42");
      REQUIRE(default_result.success);
      REQUIRE(default_result.forest.size() == 1);

      auto custom_result = expression_node<std::string>::parse("42");
      REQUIRE(custom_result.success);
      REQUIRE(custom_result.forest.size() == 1);

      auto& tree = custom_result.forest.front();
      CHECK(tree->user_data.empty());

      auto stored = visit(*tree, [](auto& node) {
         using node_t = std::decay_t<decltype(node)>;
         if constexpr (std::is_same_v<node_t, number_node<std::string>>) {
            node.user_data = std::string{"token:"} + node.value.text;
         } else {
            node.user_data = "non-leaf";
         }
         return node.user_data;
      });

      CHECK(stored == "token:42");
      CHECK(tree->user_data == "token:42");

      auto cloned = tree->clone();
      REQUIRE(cloned != nullptr);
      CHECK(cloned->user_data == "token:42");
   }

   TEST_CASE("token declarations and inferred lexical helpers collapse to matched_string captures") {
      auto result = binding::parse("let foo.bar:int = baz;");

      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);

      const auto& tree = result.forest.front();
      CHECK(tree->keyword.text == "let");
      CHECK(tree->name.text == "foo.bar");
      CHECK(tree->type.text == "int");
      CHECK(tree->value.text == "baz");
      CHECK(tree->name.range.begin.offset == 4);
      CHECK(tree->name.range.end.offset == 11);
      CHECK(tree->type.range.begin.offset == 12);
      CHECK(tree->type.range.end.offset == 15);
      CHECK(tree->value.range.begin.offset == 18);
      CHECK(tree->value.range.end.offset == 21);

      std::ostringstream stream;
      stream << *tree;
      CHECK(stream.str().find("name = \"foo.bar\"") != std::string::npos);
      CHECK(stream.str().find("type = \"int\"") != std::string::npos);
      CHECK(stream.str().find("value = \"baz\"") != std::string::npos);
   }

   TEST_CASE("generated lexers prefer earlier equal-length tokens and longer shared-prefix tokens") {
      auto keyword = chosen_word::parse("if");
      REQUIRE(keyword.success);
      REQUIRE(keyword.forest.size() == 1);
      CHECK(keyword.forest.front()->production_index == 0);
      CHECK(keyword.forest.front()->text.text == "if");

      auto identifier = chosen_word::parse("iff");
      REQUIRE(identifier.success);
      REQUIRE(identifier.forest.size() == 1);
      CHECK(identifier.forest.front()->production_index == 1);
      CHECK(identifier.forest.front()->text.text == "iff");

      auto equals_equals = comparison_op::parse("==");
      REQUIRE(equals_equals.success);
      REQUIRE(equals_equals.forest.size() == 1);
      CHECK(equals_equals.forest.front()->production_index == 0);
      CHECK(equals_equals.forest.front()->text.text == "==");

      auto equals = comparison_op::parse("=");
      REQUIRE(equals.success);
      REQUIRE(equals.forest.size() == 1);
      CHECK(equals.forest.front()->production_index == 1);
      CHECK(equals.forest.front()->text.text == "=");
   }

   TEST_CASE("lookahead predicates and cut markers affect generated runtime behavior") {
      SUBCASE("negative lookahead excludes reserved words from identifiers") {
         auto value = lookahead_identifier::parse("name_1");
         REQUIRE(value.success);
         REQUIRE(value.forest.size() == 1);
         CHECK(value.forest.front()->value.text == "name_1");

         auto keyword = lookahead_identifier::parse("if");
         CHECK_FALSE(keyword.success);
      }

      SUBCASE("positive lookahead requires the following delimiter without capturing it twice") {
         auto call = lookahead_call::parse("foo()");
         REQUIRE(call.success);
         REQUIRE(call.forest.size() == 1);
         CHECK(call.forest.front()->name->value.text == "foo");
         CHECK(call.forest.front()->open.text == "(");
         CHECK(call.forest.front()->close.text == ")");

         auto missing = lookahead_call::parse("foo");
         CHECK_FALSE(missing.success);
      }

      SUBCASE("cut markers commit to the earlier branch once its prefix matches") {
         auto identifier = lookahead_statement::parse("value");
         REQUIRE(identifier.success);
         REQUIRE(identifier.forest.size() == 1);
         CHECK(identifier.forest.front()->name->value.text == "value");

         auto committed = lookahead_statement::parse("if(x)y");
         REQUIRE(committed.success);
         REQUIRE(committed.forest.size() == 1);
         REQUIRE(committed.forest.front()->keyword.has_value());
         CHECK(committed.forest.front()->keyword->text == "if");
         CHECK(committed.forest.front()->condition->value.text == "x");
         CHECK(committed.forest.front()->body->value.text == "y");

         auto missing_paren = lookahead_statement::parse("if value");
         CHECK_FALSE(missing_paren.success);
      }
   }

   TEST_CASE("template instantiations generate reusable structured helper nodes") {
      SUBCASE("single template invocations expose the substituted captures") {
         auto result = template_paren_identifier::parse("(name)");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);
         REQUIRE(result.forest.front()->body != nullptr);
         CHECK(result.forest.front()->body->open.text == "(");
         CHECK(result.forest.front()->body->value.text == "name");
         CHECK(result.forest.front()->body->close.text == ")");
      }

      SUBCASE("template arguments may carry quantifiers into substituted captures") {
         auto result = template_brace_identifiers::parse("{foo bar}");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);
         REQUIRE(result.forest.front()->body != nullptr);
         REQUIRE(result.forest.front()->body->value.size() == 2);
         CHECK(result.forest.front()->body->value[0].text == "foo");
         CHECK(result.forest.front()->body->value[1].text == "bar");
      }

      SUBCASE("multiple template families can shape different helper payloads consistently") {
         auto result = template_returned_identifier::parse("return value");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);
         REQUIRE(result.forest.front()->payload != nullptr);
         CHECK(result.forest.front()->payload->keyword.text == "return");
         CHECK(result.forest.front()->payload->value.text == "value");
      }

      SUBCASE("template arguments may themselves be template invocations") {
         auto result = template_nested_returned_identifier::parse("(return value)");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);
         REQUIRE(result.forest.front()->body != nullptr);
         CHECK(result.forest.front()->body->open.text == "(");
         CHECK(result.forest.front()->body->close.text == ")");
         REQUIRE(result.forest.front()->body->value != nullptr);
         CHECK(result.forest.front()->body->value->keyword.text == "return");
         CHECK(result.forest.front()->body->value->value.text == "value");
      }

      SUBCASE("template parameters may themselves name template families that are specialized in place") {
         auto result = template_specialized_identifier::parse("(@spec::value)");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);
         REQUIRE(result.forest.front()->body != nullptr);
         CHECK(result.forest.front()->body->open.text == "(");
         CHECK(result.forest.front()->body->close.text == ")");
         REQUIRE(result.forest.front()->body->value != nullptr);
         CHECK(result.forest.front()->body->value->prep.text == "@spec");
         CHECK(result.forest.front()->body->value->suffix.text == "::value");
      }
   }

   TEST_CASE("choice-only inheritance grammars stay visitable and printable") {
      SUBCASE("concrete rule parsing keeps concrete fields") {
         auto greeting_result = greeting::parse("hello");
         REQUIRE(greeting_result.success);
         REQUIRE(greeting_result.forest.size() == 1);
         CHECK(greeting_result.forest.front()->text.text == "hello");
         const auto& recomputed = greeting::recompute_complexity(0);
         CHECK(&recomputed == &greeting::complexity(0));
         CHECK_FALSE(greeting::complexity(0).summary.empty());
         CHECK(recomputed.estimate(5.0) >= 0.0);
      }

      SUBCASE("base rule parsing dispatches to the derived node") {
         auto message_result = message::parse("hello");
         REQUIRE(message_result.success);
         REQUIRE(message_result.forest.size() == 1);

         auto text = visit(*message_result.forest.front(), [](const greeting& node) { return node.text.text; });
         CHECK(text == "hello");

         std::ostringstream stream;
         stream << *message_result.forest.front();
         CHECK(stream.str() == "greeting(\n  text = \"hello\"\n)");
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
      for (const auto& tree: ambiguous_result.forest) {
         if (dynamic_cast<const ambiguous_first*>(tree.get()) != nullptr) {
            first_seen = true;
         }
         if (dynamic_cast<const ambiguous_second*>(tree.get()) != nullptr) {
            second_seen = true;
         }
      }

      CHECK(first_seen);
      CHECK(second_seen);

      cpf::parse_options ambiguity_options;
      ambiguity_options.error_on_ambiguity = true;
      auto ambiguity_error = ambiguous_expr::parse("x", ambiguity_options);
      CHECK_FALSE(ambiguity_error.success);
      CHECK(ambiguity_error.forest.empty());
      CHECK(ambiguity_error.status == cpf::parse_status::failure);
      REQUIRE(ambiguity_error.error.has_value());
      CHECK(ambiguity_error.error->found.kind == cpf::parse_error_found_kind::ambiguous_parse);
   }

   TEST_CASE("choice-rule failures merge expectations from every matching branch") {
      auto result = choice_message::parse("help");

      CHECK_FALSE(result.success);
      CHECK(result.status == cpf::parse_status::failure);
      REQUIRE(result.error.has_value());
      CHECK(result.error->position.line == 1);
      CHECK(result.error->position.column == 1);
      CHECK(result.error->found.kind == cpf::parse_error_found_kind::token);
      CHECK(result.error->found.text == "help");
      CHECK(result.error->message.find("\"hello\"") != std::string::npos);
      CHECK(result.error->message.find("\"world\"") != std::string::npos);
      CHECK(result.error->message.find("while parsing rule 'say_hello'") != std::string::npos);
      CHECK(result.error->message.find("while parsing rule 'say_world'") != std::string::npos);
      CHECK(result.error->message.find("while matching base rule 'choice_message'") != std::string::npos);
   }

   TEST_CASE("merged concrete rule definitions keep one generated type and mark the matched definition") {
      SUBCASE("overlapping terminal definitions follow lexer precedence and keep the surviving definition index") {
         auto result = repeated_token::parse("x");

         REQUIRE(result.success);
          REQUIRE(result.forest.size() == 1);

         std::vector<std::size_t> production_indices;
         for (const auto& tree: result.forest) {
            production_indices.push_back(tree->production_index);
            CHECK(tree->text.text == "x");
         }

          CHECK(production_indices == std::vector<std::size_t>{0});
      }

      SUBCASE("shared labeled references resolve to the nearest common generated base") {
         auto exclamation = merged_wrapper::parse("bye!");
         REQUIRE(exclamation.success);
         REQUIRE(exclamation.forest.size() == 1);
         CHECK(exclamation.forest.front()->production_index == 0);
         CHECK(exclamation.forest.front()->suffix.text == "!");
         REQUIRE(exclamation.forest.front()->payload != nullptr);
         CHECK(dynamic_cast<const merged_farewell*>(exclamation.forest.front()->payload.get()) != nullptr);

         auto question = merged_wrapper::parse("hello?");
         REQUIRE(question.success);
         REQUIRE(question.forest.size() == 1);
         CHECK(question.forest.front()->production_index == 1);
         CHECK(question.forest.front()->suffix.text == "?");
         REQUIRE(question.forest.front()->payload != nullptr);
         CHECK(dynamic_cast<const merged_greeting*>(question.forest.front()->payload.get()) != nullptr);

         auto cloned = question.forest.front()->clone();
         REQUIRE(cloned != nullptr);
         CHECK(cloned->production_index == 1);
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
         CHECK(root->production_index == 0);
         REQUIRE(root->right != nullptr);

         auto* right = dynamic_cast<const merged_binary*>(root->right.get());
         REQUIRE(right != nullptr);
         CHECK(right->production_index == 1);
      }

      SUBCASE("complexity samples and stored estimates are tracked per merged definition") {
         auto plus_inputs = merged_binary::complexity_inputs(0);
         auto multiply_inputs = merged_binary::complexity_inputs(1);
         REQUIRE(plus_inputs.size() >= 2);
         REQUIRE(multiply_inputs.size() >= 2);
         CHECK(std::string{plus_inputs.back()}.find('+') != std::string::npos);
         CHECK(std::string{multiply_inputs.back()}.find('*') != std::string::npos);

         const auto& recomputed_plus = merged_binary::recompute_complexity(0);
         const auto& recomputed = merged_binary::recompute_complexity(1);
         CHECK(&recomputed_plus == &merged_binary::complexity(0));
         CHECK(&recomputed == &merged_binary::complexity(1));
         CHECK_FALSE(merged_binary::complexity(0).summary.empty());
         CHECK_FALSE(merged_binary::complexity(1).summary.empty());
         CHECK_FALSE(recomputed.summary.empty());
         CHECK(recomputed.estimate(8.0) >= 0.0);

         CHECK_THROWS_AS(merged_binary::complexity(2), std::out_of_range);
         CHECK_THROWS_AS(merged_binary::complexity_inputs(2), std::out_of_range);
         CHECK_THROWS_AS(merged_binary::recompute_complexity(2), std::out_of_range);
      }
   }

   TEST_CASE("quantified grammar syntax lowers to the existing Earley runtime") {
      SUBCASE("optional references can be absent or present") {
         auto empty = maybe_choice::parse("");
         REQUIRE(empty.success);
         REQUIRE(empty.forest.size() == 1);
         auto& empty_tree = empty.forest.front();
         CHECK_FALSE(empty_tree.has_materialized());
         CHECK(empty_tree->value == nullptr);
         CHECK(empty_tree.has_materialized());

         auto present = maybe_choice::parse("a");
         REQUIRE(present.success);
         REQUIRE(present.forest.size() == 1);
         auto& present_tree = present.forest.front();
         CHECK_FALSE(present_tree.has_materialized());
         REQUIRE(present_tree->value != nullptr);
         CHECK(present_tree.has_materialized());
         CHECK(visit(*present_tree->value, quant_choice_visitor{}) == "a");
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

      SUBCASE("recursive visiting passes the parent for repeated child vectors") {
         auto repeated = star_choice::parse("a7a");
         REQUIRE(repeated.success);
         REQUIRE(repeated.forest.size() == 1);

         std::vector<std::string> visited;
         visit_recursive(*repeated.forest.front(), [&](const auto& node, const auto* parent) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, star_choice>) {
               CHECK(parent == nullptr);
               visited.emplace_back("star_choice");
            } else if constexpr (std::is_same_v<node_t, quant_alpha>) {
               CHECK(dynamic_cast<const star_choice*>(parent) != nullptr);
               visited.emplace_back("quant_alpha");
            } else if constexpr (std::is_same_v<node_t, quant_digit>) {
               CHECK(dynamic_cast<const star_choice*>(parent) != nullptr);
               visited.emplace_back("quant_digit");
            }
         });

         CHECK(visited == std::vector<std::string>{"star_choice", "quant_alpha", "quant_digit", "quant_alpha"});
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
         CHECK(greeting->production_index == 0);
         CHECK(visit_payload(*hello.forest.front(), [](const auto& node) { return node.text.text; }) == "hello");

         auto bye = grouped_choice_payload::parse("bye");
         REQUIRE(bye.success);
         REQUIRE(bye.forest.size() == 1);
         CHECK(std::holds_alternative<std::unique_ptr<grouped_choice_farewell>>(bye.forest.front()->payload));
         const auto& farewell = std::get<std::unique_ptr<grouped_choice_farewell>>(bye.forest.front()->payload);
         REQUIRE(farewell != nullptr);
         CHECK(visit_payload(*bye.forest.front(), [](const auto& node) { return node.text.text; }) == "bye");
      }

      SUBCASE("mutable variant visitors can rewrite grouped capture payloads in place") {
         auto hello = grouped_choice_payload::parse("hello");
         REQUIRE(hello.success);
         REQUIRE(hello.forest.size() == 1);

         CHECK(visit_payload(*hello.forest.front(), [](auto& node) {
            node.text.text = "hola";
            return node.text.text;
         }) == "hola");

         const auto& greeting = std::get<std::unique_ptr<grouped_choice_greeting>>(hello.forest.front()->payload);
         REQUIRE(greeting != nullptr);
         CHECK(greeting->text.text == "hola");
      }

      SUBCASE("recursive visiting passes the parent through grouped variant captures") {
         auto hello = grouped_choice_payload::parse("hello");
         REQUIRE(hello.success);
         REQUIRE(hello.forest.size() == 1);

         std::vector<std::string> visited;
         visit_recursive(*hello.forest.front(), [&](const auto& node, const auto* parent) {
            using node_t = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<node_t, grouped_choice_payload>) {
               CHECK(parent == nullptr);
               visited.emplace_back("grouped_choice_payload");
            } else if constexpr (std::is_same_v<node_t, grouped_choice_greeting>) {
               CHECK(dynamic_cast<const grouped_choice_payload*>(parent) != nullptr);
               visited.emplace_back("grouped_choice_greeting");
            } else if constexpr (std::is_same_v<node_t, grouped_choice_farewell>) {
               CHECK(dynamic_cast<const grouped_choice_payload*>(parent) != nullptr);
               visited.emplace_back("grouped_choice_farewell");
            }
         });

         CHECK(visited == std::vector<std::string>{"grouped_choice_payload", "grouped_choice_greeting"});
      }

      SUBCASE("multi-symbol labeled groups materialize helper nodes with their inner captures") {
         auto xy = grouped_pair::parse("xy");
         REQUIRE(xy.success);
         REQUIRE(xy.forest.size() == 1);
         REQUIRE(xy.forest.front()->value != nullptr);
         CHECK(xy.forest.front()->value->first.text == "x");
         CHECK(xy.forest.front()->value->second.text == "y");

         auto zw = grouped_pair::parse("zw");
         REQUIRE(zw.success);
         REQUIRE(zw.forest.size() == 1);
         REQUIRE(zw.forest.front()->value != nullptr);
         CHECK(zw.forest.front()->value->first.text == "z");
         CHECK(zw.forest.front()->value->second.text == "w");
      }

      SUBCASE("quantified labeled groups materialize repeated helper nodes") {
         auto repeated = grouped_pairs::parse("abab");
         REQUIRE(repeated.success);
         REQUIRE(repeated.forest.size() == 1);
         REQUIRE(repeated.forest.front()->pairs.size() == 2);
         REQUIRE(repeated.forest.front()->pairs[0] != nullptr);
         REQUIRE(repeated.forest.front()->pairs[1] != nullptr);
         CHECK(repeated.forest.front()->pairs[0]->text.text == "a");
         CHECK(repeated.forest.front()->pairs[0]->suffix.text == "b");
         CHECK(repeated.forest.front()->pairs[1]->text.text == "a");
         CHECK(repeated.forest.front()->pairs[1]->suffix.text == "b");
      }

      SUBCASE("labeled groups may keep inner labeled captures optional across alternatives") {
         auto negative = grouped_signed_number::parse("-12");
         REQUIRE(negative.success);
         REQUIRE(negative.forest.size() == 1);
         REQUIRE(negative.forest.front()->payload != nullptr);
         REQUIRE(negative.forest.front()->payload->sign.has_value());
         CHECK(negative.forest.front()->payload->sign->text == "-");
         REQUIRE(negative.forest.front()->payload->value != nullptr);
         CHECK(negative.forest.front()->payload->value->value.text == "12");

         auto positive = grouped_signed_number::parse("12");
         REQUIRE(positive.success);
         REQUIRE(positive.forest.size() == 1);
         REQUIRE(positive.forest.front()->payload != nullptr);
         CHECK_FALSE(positive.forest.front()->payload->sign.has_value());
         REQUIRE(positive.forest.front()->payload->value != nullptr);
         CHECK(positive.forest.front()->payload->value->value.text == "12");
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

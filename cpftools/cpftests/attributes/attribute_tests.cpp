#include "attr_dir.h"
#include "attr_prec_labels.h"
#include "attr_prec_numeric.h"
#include "default_attrs.h"
#include "default_labels.h"

#include "support/doctest.h"

#include <string_view>

namespace {
   struct numeric_visitor {
      int operator()(const numeric_plus& node) const { return visit(*node.left, *this) + visit(*node.right, *this); }
      int operator()(const numeric_times& node) const { return visit(*node.left, *this) * visit(*node.right, *this); }
      int operator()(const numeric_number& node) const { return std::stoi(node.value.text); }
   };

   struct label_visitor {
      int operator()(const label_plus& node) const { return visit(*node.left, *this) + visit(*node.right, *this); }
      int operator()(const label_minus& node) const { return visit(*node.left, *this) - visit(*node.right, *this); }
      int operator()(const label_times& node) const { return visit(*node.left, *this) * visit(*node.right, *this); }
      int operator()(const label_divide& node) const { return visit(*node.left, *this) / visit(*node.right, *this); }
      int operator()(const label_number& node) const { return std::stoi(node.value.text); }
   };

   struct assoc_visitor {
      int operator()(const assoc_power& node) const {
         auto base = visit(*node.left, *this);
         auto exponent = visit(*node.right, *this);
         auto result = 1;
         for (auto i = 0; i < exponent; ++i) {
            result *= base;
         }
         return result;
      }
      int operator()(const assoc_subtract& node) const { return visit(*node.left, *this) - visit(*node.right, *this); }
      int operator()(const assoc_number& node) const { return std::stoi(node.value.text); }
   };

   struct default_visitor {
      int operator()(const default_add& node) const { return visit(*node.left, *this) + visit(*node.right, *this); }
      int operator()(const default_subtract& node) const {
         return visit(*node.left, *this) - visit(*node.right, *this);
      }
      int operator()(const default_multiply& node) const {
         return visit(*node.left, *this) * visit(*node.right, *this);
      }
      int operator()(const default_number& node) const { return std::stoi(node.value.text); }
   };

   struct default_label_visitor {
      int operator()(const default_label_add& node) const {
         return visit(*node.left, *this) + visit(*node.right, *this);
      }
      int operator()(const default_label_multiply& node) const {
         return visit(*node.left, *this) * visit(*node.right, *this);
      }
      int operator()(const default_label_number& node) const { return std::stoi(node.value.text); }
   };

   int evaluate_numeric(std::string_view input) {
      auto result = numeric_expr::parse(input);
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);
      return visit(*result.forest.front(), numeric_visitor{});
   }

   int evaluate_label(std::string_view input) {
      auto result = label_expr::parse(input);
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);
      return visit(*result.forest.front(), label_visitor{});
   }

   int evaluate_assoc(std::string_view input) {
      auto result = assoc_expr::parse(input);
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);
      return visit(*result.forest.front(), assoc_visitor{});
   }

   int evaluate_default(std::string_view input) {
      auto result = default_expr::parse(input);
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);
      return visit(*result.forest.front(), default_visitor{});
   }

   int evaluate_default_label(std::string_view input) {
      auto result = default_label_expr::parse(input);
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);
      return visit(*result.forest.front(), default_label_visitor{});
   }
} // namespace

TEST_SUITE("generated.attributes") {
   TEST_CASE("precedence attributes shape the generated parse tree") {
      SUBCASE("numeric absolute precedence chooses the tighter operator") {
         CHECK(evaluate_numeric("1 + 2 * 3") == 9);

         auto result = numeric_expr::parse("1 + 2 * 3");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);
         CHECK(dynamic_cast<const numeric_times*>(result.forest.front().get()) != nullptr);
      }

      SUBCASE("label-based and relative precedence groups remain stable") {
         CHECK(evaluate_label("1 + 2 * 3") == 7);
         CHECK(evaluate_label("20 * 5 / 2") == 50);
         CHECK(evaluate_label("10 - 3 + 8 / 2") == 11);

         auto result = label_expr::parse("20 * 5 / 2");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);
         CHECK(dynamic_cast<const label_divide*>(result.forest.front().get()) != nullptr);
      }
   }

   TEST_CASE("associativity attributes control recursive binding") {
      SUBCASE("left and right associativity are both honored") {
         CHECK(evaluate_assoc("8 - 3 - 2") == 3);
         CHECK(evaluate_assoc("2 ^ 3 ^ 2") == 512);

         auto result = assoc_expr::parse("2 ^ 3 ^ 2");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         auto* root = dynamic_cast<const assoc_power*>(result.forest.front().get());
         REQUIRE(root != nullptr);
         REQUIRE(root->right != nullptr);
         CHECK(dynamic_cast<const assoc_power*>(root->right.get()) != nullptr);
      }
   }

   TEST_CASE("attribute-heavy grammars still report expressive source-near errors") {
      auto result = assoc_expr::parse("2 ^");

      CHECK_FALSE(result.success);
      CHECK(result.error.line == 1);
      CHECK(result.error.column == 4);
      CHECK(result.error.found == "<end of input>");
      CHECK_FALSE(result.error.expected.empty());
      CHECK(result.error.message.find("pattern [0-9]+") != std::string::npos);
      CHECK(result.error.message.find("while parsing rule 'assoc_number'") != std::string::npos);
   }

   TEST_CASE("default rule attribute values are honored when attributes are omitted") {
      SUBCASE("default precedence follows infix rule declaration order") {
         CHECK(evaluate_default("1 + 2 * 3") == 7);

         auto result = default_expr::parse("1 + 2 * 3");
         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         auto* root = dynamic_cast<const default_add*>(result.forest.front().get());
         REQUIRE(root != nullptr);
         CHECK(dynamic_cast<const default_multiply*>(root->right.get()) != nullptr);
      }

      SUBCASE("default associativity is left and unlabeled terminals use value") {
         CHECK(evaluate_default("8 - 3 - 2") == 3);

         auto number_result = default_number::parse("42");
         REQUIRE(number_result.success);
         REQUIRE(number_result.forest.size() == 1);
         CHECK(number_result.forest.front()->value.text == "42");

         auto expr_result = default_expr::parse("8 - 3 - 2");
         REQUIRE(expr_result.success);
         REQUIRE(expr_result.forest.size() == 1);

         auto* root = dynamic_cast<const default_subtract*>(expr_result.forest.front().get());
         REQUIRE(root != nullptr);
         CHECK(dynamic_cast<const default_number*>(root->right.get()) != nullptr);
      }
   }

   TEST_CASE("relative precedence can reference the default label derived from a rule name") {
      CHECK(evaluate_default_label("1 + 2 * 3") == 7);

      auto result = default_label_expr::parse("1 + 2 * 3");
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);
      CHECK(dynamic_cast<const default_label_add*>(result.forest.front().get()) != nullptr);
   }
}

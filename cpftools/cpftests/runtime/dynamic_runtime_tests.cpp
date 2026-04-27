#include <cpflib>

#include "support/doctest.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
   constexpr auto dynamic_calculator_grammar = R"(
      expression -> addition | subtraction | multiplication | division | number;
      addition [prec = 'sub'] -> expression:left '+':op expression:right;
      subtraction [prec < 'div', lbl = 'sub'] -> expression:left '-':op expression:right;
      multiplication [prec = 'div'] -> expression:left '*':op expression:right;
      division [prec < 'num', lbl = 'div'] -> expression:left '/':op expression:right;
      number [lbl = 'num'] -> r'[0-9]+':value;
   )";

   void write_file(const std::filesystem::path& path, std::string_view content) {
      std::filesystem::create_directories(path.parent_path());
      std::ofstream stream{path};
      REQUIRE(stream.good());
      stream << content;
   }

   auto make_dynamic_calculator() -> cpf::compiled_grammar { return cpf::compile_grammar(dynamic_calculator_grammar); }

   auto required_field(cpf::dynamic_node& node, std::string_view name) -> cpf::dynamic_field& {
      auto* field = node.get_field(name);
      CHECK(field != nullptr);
      if (field == nullptr) {
         throw std::runtime_error{"Missing dynamic field"};
      }
      return *field;
   }

   auto required_field(const cpf::dynamic_node& node, std::string_view name) -> const cpf::dynamic_field& {
      auto* field = node.get_field(name);
      CHECK(field != nullptr);
      if (field == nullptr) {
         throw std::runtime_error{"Missing dynamic field"};
      }
      return *field;
   }

   auto required_child(cpf::dynamic_node& node, std::string_view name) -> cpf::dynamic_node& {
      auto& field = required_field(node, name);
      CHECK(field.node != nullptr);
      if (field.node == nullptr) {
         throw std::runtime_error{"Missing dynamic child node"};
      }
      return *field.node;
   }

   auto required_token(cpf::dynamic_node& node, std::string_view name) -> cpf::matched_string& {
      auto& field = required_field(node, name);
      CHECK(field.token.has_value());
      if (!field.token.has_value()) {
         throw std::runtime_error{"Missing dynamic token"};
      }
      return *field.token;
   }

   auto required_token(const cpf::dynamic_node& node, std::string_view name) -> const cpf::matched_string& {
      const auto& field = required_field(node, name);
      CHECK(field.token.has_value());
      if (!field.token.has_value()) {
         throw std::runtime_error{"Missing dynamic token"};
      }
      return *field.token;
   }

   struct dynamic_calculator_visitor {
      auto operator()(const cpf::dynamic_node& node) const -> int {
         const auto child_value = [&](std::string_view field_name) {
            const auto& field = required_field(node, field_name);
            CHECK(field.node != nullptr);
            if (field.node == nullptr) {
               throw std::runtime_error{"Missing dynamic child node"};
            }
            return cpf::visit(*field.node, *this);
         };

         if (node.rule_name == "addition") {
            return child_value("left") + child_value("right");
         }
         if (node.rule_name == "subtraction") {
            return child_value("left") - child_value("right");
         }
         if (node.rule_name == "multiplication") {
            return child_value("left") * child_value("right");
         }
         if (node.rule_name == "division") {
            return child_value("left") / child_value("right");
         }
         if (node.rule_name == "number") {
            return std::stoi(required_token(node, "value").text);
         }
         throw std::runtime_error{"Unexpected dynamic calculator rule"};
      }
   };

   auto evaluate_dynamic_expression(std::string_view input) -> int {
      auto parser = make_dynamic_calculator();
      auto result = parser.parse(input);
      CHECK(result.success);
      CHECK(result.forest.size() == 1);
      if (!result.success || result.forest.size() != 1) {
         throw std::runtime_error{"Dynamic calculator parse failed"};
      }
      return cpf::visit(*result.forest.front(), dynamic_calculator_visitor{});
   }
}

TEST_SUITE("cpflib.dynamic_runtime") {
   TEST_CASE("compile_grammar parses the primary entry rule into a generic dynamic AST") {
      auto parser = make_dynamic_calculator();

      CHECK(parser.primary_entry_rule() == "expression");
      REQUIRE(parser.find_rule("expression") != nullptr);
      REQUIRE(parser.find_rule("number") != nullptr);

      auto tokens = parser.lex("1 + 2 * 3");
      REQUIRE(tokens.size() == 5);
      CHECK(tokens[1].text.text == "+");
      CHECK(tokens[3].text.text == "*");

      auto result = parser.parse(tokens);
      REQUIRE(result.success);
      CHECK(result.status == cpf::parse_status::success);
      REQUIRE(result.forest.size() == 1);
      CHECK_FALSE(result.forest.front().has_materialized());

      auto& root = *result.forest.front();
      CHECK(root.rule_name == "addition");
      CHECK(root.source_text(tokens.input) == "1 + 2 * 3");

      const auto& op = required_field(root, "op");
      REQUIRE(op.token.has_value());
      CHECK(op.token->text == "+");
      CHECK(op.value_type_name == "cpf::matched_string");

      const auto& left = required_field(root, "left");
      REQUIRE(left.node != nullptr);
      CHECK(left.node->rule_name == "number");
      CHECK(required_field(*left.node, "value").token->text == "1");

      const auto& right = required_field(root, "right");
      REQUIRE(right.node != nullptr);
      CHECK(right.node->rule_name == "multiplication");
      CHECK(required_field(*right.node, "op").token->text == "*");
      CHECK(required_field(*required_field(*right.node, "right").node, "value").token->text == "3");
   }

   TEST_CASE("dynamic visit dispatch can interpret calculator expressions") {
      CHECK(evaluate_dynamic_expression("1 + 2 * 3") == 7);
      CHECK(evaluate_dynamic_expression("9 - 6 / 3") == 7);
      CHECK(evaluate_dynamic_expression("1 * 2 * 3") == 6);
      CHECK(evaluate_dynamic_expression("1 * 2 * 3 + 1 * 2 * 3 + 1 * 2 * 3") == 18);
   }

   TEST_CASE("dynamic const visit dispatch exposes the selected node") {
      auto parser = make_dynamic_calculator();
      auto result = parser.parse("1 + 2 * 3");

      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);

      const auto& root = *result.forest.front();
      CHECK(cpf::visit(root, [](const cpf::dynamic_node& node) {
         return node.rule_name + ":" + required_token(node, "op").text;
      }) == "addition:+");
   }

   TEST_CASE("dynamic recursive visiting supports const traversal and parent context") {
      auto parser = make_dynamic_calculator();
      auto result = parser.parse("1 + 2 * 3");

      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);

      const auto& root = *result.forest.front();
      auto visited = std::vector<std::string>{};
      cpf::visit_recursive(root, [&](const cpf::dynamic_node& node, const cpf::dynamic_node* parent) {
         visited.push_back(node.rule_name + "<-" + (parent != nullptr ? parent->rule_name : std::string{"<root>"}));
      });

      CHECK(visited == std::vector<std::string>{
                           "addition<-<root>",
                           "number<-addition",
                           "multiplication<-addition",
                           "number<-multiplication",
                           "number<-multiplication"
      });
   }

   TEST_CASE("dynamic mutable recursive visiting can rewrite the tree in place") {
      auto parser = make_dynamic_calculator();
      auto result = parser.parse("1 + 2 * 3");

      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);

      auto& root = *result.forest.front();
      cpf::visit_recursive(root, [](cpf::dynamic_node& node) {
         if (node.rule_name == "number") {
            required_token(node, "value").text = "1";
         }
      });

      CHECK(cpf::visit(root, dynamic_calculator_visitor{}) == 2);
   }

   TEST_CASE("dynamic mutable recursive visiting can seed an explicit parent for subtree traversals") {
      auto parser = make_dynamic_calculator();
      auto result = parser.parse("1 + 2 * 3");

      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);

      auto& root = *result.forest.front();
      auto& right = required_child(root, "right");
      auto visited = std::vector<std::string>{};

      cpf::visit_recursive(right, [&](cpf::dynamic_node& node, cpf::dynamic_node* parent) {
         visited.push_back(node.rule_name + "<-" + (parent != nullptr ? parent->rule_name : std::string{"<root>"}));
         if (node.rule_name == "number") {
            required_token(node, "value").text = "4";
         }
      }, &root);

      CHECK(visited == std::vector<std::string>{
                           "multiplication<-addition",
                           "number<-multiplication",
                           "number<-multiplication"
      });
      CHECK(cpf::visit(root, dynamic_calculator_visitor{}) == 17);
   }

   TEST_CASE("dynamic mutable visit dispatch can rewrite the selected node in place") {
      auto parser = make_dynamic_calculator();
      auto result = parser.parse("1 + 2");

      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);

      auto& root = *result.forest.front();
      cpf::visit(root, [](cpf::dynamic_node& node) {
         required_token(required_child(node, "left"), "value").text = "10";
      });

      CHECK(cpf::visit(root, dynamic_calculator_visitor{}) == 12);
   }

   TEST_CASE("compile_grammar exposes grouped concrete syntax through parse_cst") {
      auto parser = cpf::compile_grammar(R"(
         grouped_sentence -> ('(' 'hi' ')'):body;
      )");

      auto result = parser.parse_cst("(hi)");
      REQUIRE(result.success);
      REQUIRE(result.forest.size() == 1);

      const auto& root = *result.forest.front();
      CHECK(root.rule_name == "grouped_sentence");
      REQUIRE(root.children.size() == 3);
      CHECK(std::get<cpf::matched_string>(root.children[0]).text == "(");
      CHECK(std::get<cpf::matched_string>(root.children[1]).text == "hi");
      CHECK(std::get<cpf::matched_string>(root.children[2]).text == ")");
   }

   TEST_CASE("dynamic runtime AST preserves partial recovery damage") {
      auto parser = make_dynamic_calculator();

      cpf::parse_options options;
      options.allow_partial = true;

      auto result = parser.parse("1 + * 2 + 3", options);
      REQUIRE(result.success);
      CHECK(result.partial);
      REQUIRE(result.error.has_value());
      REQUIRE(result.forest.size() == 1);

      const auto& root = *result.forest.front();
      auto damaged_nodes = std::size_t{0};
      cpf::visit_recursive(root, [&](const cpf::dynamic_node& node) {
         if (node.is_damaged()) {
            ++damaged_nodes;
         }
      });
      CHECK(damaged_nodes >= 1);
      CHECK_FALSE(result.forest.front().damaged_nodes().empty());
   }

   TEST_CASE("compile_grammar supports lookahead, imported grammars, and named roots") {
      auto parser = cpf::compile_grammar(R"(
         lookahead_keyword -> 'if' | 'else' | 'while';
         lookahead_identifier -> !lookahead_keyword r'[A-Za-z_][A-Za-z0-9_]*':value;
         lookahead_call -> lookahead_identifier:name &'(' '(':open ')':close;
      )");

      auto call = parser.parse("lookahead_call", "print()");
      REQUIRE(call.success);
      REQUIRE(call.forest.size() == 1);
      CHECK((*call.forest.front()).rule_name == "lookahead_call");
      CHECK(required_field(*required_field(*call.forest.front(), "name").node, "value").token->text == "print");

      auto rejected = parser.recognize("lookahead_call", "if()");
      CHECK_FALSE(rejected.success);
      REQUIRE(rejected.error.has_value());

      auto temp_root = std::filesystem::temp_directory_path() / "cpf_dynamic_runtime_tests";
      std::filesystem::remove_all(temp_root);
      write_file(temp_root / "tokens.cpf", "token imported_identifier -> r'[A-Za-z_]+';\n");
      write_file(temp_root / "root.cpf", R"(@import "tokens.cpf";
imported_assignment -> imported_identifier:name '=':assign imported_identifier:value;
)");

      auto imported = cpf::compile_grammar_file(temp_root / "root.cpf");
      auto parsed = imported.parse("imported_assignment", "left=right");
      REQUIRE(parsed.success);
      REQUIRE(parsed.forest.size() == 1);
      const auto& assignment = *parsed.forest.front();
      CHECK(assignment.rule_name == "imported_assignment");
      CHECK(required_field(assignment, "name").token->text == "left");
      CHECK(required_field(assignment, "value").token->text == "right");

      std::filesystem::remove_all(temp_root);
   }
}



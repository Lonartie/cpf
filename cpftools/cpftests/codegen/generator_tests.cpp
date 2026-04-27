#include <cpfgenlib>

#include "support/doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
   void write_file(const std::filesystem::path& path, std::string_view content) {
      std::filesystem::create_directories(path.parent_path());
      std::ofstream stream{path};
      REQUIRE(stream.good());
      stream << content;
   }

   int precedence_of_generated_rule(std::string_view generated_source, std::string_view rule_name) {
      auto rule_case = std::string{"case "} + std::string{rule_name} + "::RuleId:";
      auto rule_position = generated_source.find(rule_case);
      REQUIRE(rule_position != std::string_view::npos);
      auto production_case = generated_source.find("case 0:", rule_position);
      REQUIRE(production_case != std::string_view::npos);
      auto return_position = generated_source.find("return ", production_case);
      REQUIRE(return_position != std::string_view::npos);
      return_position += std::string_view{"return "}.size();
      auto return_end = generated_source.find(';', return_position);
      REQUIRE(return_end != std::string_view::npos);
      return std::stoi(std::string{generated_source.substr(return_position, return_end - return_position)});
   }

   [[nodiscard]] std::string read_file(const std::filesystem::path& path) {
      auto stream = std::ifstream{path};
      REQUIRE(stream.good());
      return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
   }

   struct golden_fixture_spec {
      std::string grammar_name;
      std::string snapshot_stem;
      std::string code_namespace;
   };

   [[nodiscard]] auto repository_root() -> std::filesystem::path {
      return std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path().parent_path();
   }

   [[nodiscard]] auto grammar_fixture_path(std::string_view grammar_name) -> std::filesystem::path {
      return repository_root() / "cpftools" / "cpftests" / "fixtures" / "grammars" /
             (std::string{grammar_name} + ".cpf");
   }

   [[nodiscard]] auto snapshot_path(std::string_view stem, std::string_view extension) -> std::filesystem::path {
      return std::filesystem::path{__FILE__}.parent_path() / "snapshots" /
             (std::string{stem} + "." + std::string{extension});
   }

   void check_snapshot(std::string_view actual, const std::filesystem::path& path) {
      const auto expected = read_file(path);
      CHECK(actual == expected);
   }
} // namespace

TEST_SUITE("cpflib.code_generator") {
   TEST_CASE("representative grammars keep stable golden generated outputs") {
      const auto fixtures = std::vector<golden_fixture_spec>{
            {"calculator", "calculator", ""},
            {"default_attrs", "default_attrs", ""},
            {"imported_bundle", "imported_bundle", ""},
            {"namespaced_calculator", "namespaced_calculator", "generated::fixtures"},
      };

      for (const auto& fixture: fixtures) {
         CAPTURE(fixture.grammar_name);
         auto grammar = cpf::parse_grammar_file(grammar_fixture_path(fixture.grammar_name));
         auto generated = cpf::generate_code(grammar, fixture.grammar_name, fixture.code_namespace);

         check_snapshot(generated.header, snapshot_path(fixture.snapshot_stem, "h.snap"));
         check_snapshot(generated.source, snapshot_path(fixture.snapshot_stem, "cpp.snap"));
      }
   }

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
         CHECK(generated.header.find("Generated parser API for grammar 'calculator'.") != std::string::npos);
         CHECK(generated.header.find("Every generated parse entry point uses the shared Earley runtime") !=
               std::string::npos);
         CHECK(generated.header.find("Generated AST node for grammar rule 'number'.") != std::string::npos);
         CHECK(generated.header.find("this rule participates in a recursive rule cycle") != std::string::npos);
         CHECK(generated.header.find("Exclusive node storage is O(1).") != std::string::npos);
         CHECK(generated.header.find("using expression = expression_node<>;") != std::string::npos);
         CHECK(generated.header.find("struct expression_node : cpf::node_with_user_data<UserData>") != std::string::npos);
         CHECK(generated.header.find("struct number_node : expression_node<UserData>") != std::string::npos);
         CHECK(generated.header.find("#include <sstream>") != std::string::npos);
         CHECK(generated.header.find("static constexpr std::size_t RuleId = ") != std::string::npos);
         CHECK(generated.header.find("static constexpr std::size_t ProductionCount = 5;") != std::string::npos);
         CHECK(generated.header.find("static auto complexity(std::size_t production_index) -> const cpf::complexity&;") !=
               std::string::npos);
         CHECK(generated.header.find(
                     "static auto lex(std::string_view input) -> cpf::token_sequence;") !=
                std::string::npos);
          CHECK(generated.header.find(
                     "static parse_result parse(std::string_view input, const cpf::parse_options& options = {});") !=
               std::string::npos);
          CHECK(generated.header.find(
                        "static parse_result parse(const cpf::token_sequence& tokens, const cpf::parse_options& options = {});") !=
                std::string::npos);
          CHECK(generated.header.find(
                      "using cst_parse_result = cpf::parse_result<cpf::cst_node>;") != std::string::npos);
          CHECK(generated.header.find(
                      "static cst_parse_result parse_cst(std::string_view input, const cpf::parse_options& options = {});") !=
                std::string::npos);
          CHECK(generated.header.find(
                      "static cst_parse_result parse_cst(const cpf::token_sequence& tokens, const cpf::parse_options& options = {});") !=
                std::string::npos);
         CHECK(generated.header.find("static cpf::recognize_result recognize(std::string_view input);") !=
               std::string::npos);
          CHECK(generated.header.find(
                        "static cpf::recognize_result recognize(const cpf::token_sequence& tokens);") !=
                std::string::npos);
         CHECK(generated.header.find(
                     "static auto complexity_inputs(std::size_t production_index) -> std::span<const std::string_view>;") !=
               std::string::npos);
         CHECK(generated.header.find(
                     "static auto recompute_complexity(std::size_t production_index) -> const cpf::complexity&;") !=
               std::string::npos);
          CHECK(generated.header.find("expression_node() = default;") != std::string::npos);
          CHECK(generated.header.find("expression_node(expression_node&&) = default;") != std::string::npos);
          CHECK(generated.header.find("auto operator=(expression_node&&) -> expression_node& = default;") !=
                std::string::npos);
          CHECK(generated.header.find("expression_node(const expression_node&) = delete;") != std::string::npos);
          CHECK(generated.header.find("auto operator=(const expression_node&) -> expression_node& = delete;") !=
                std::string::npos);
         CHECK(generated.header.find("std::unique_ptr<expression_node<UserData>> clone() const;") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string value;") != std::string::npos);
         CHECK(generated.header.find("std::unique_ptr<expression_node<UserData>> left;") != std::string::npos);
         CHECK(generated.header.find("template<typename UserData, typename Visitor>") != std::string::npos);
         CHECK(generated.header.find("switch (node.rule_id())") != std::string::npos);
         CHECK(generated.header.find("auto visit(expression_node<UserData>& node, Visitor&& visitor)") != std::string::npos);
         CHECK(generated.header.find("void visit_recursive(expression_node<UserData>& node, Visitor&& visitor)") != std::string::npos);
         CHECK(generated.header.find("void invoke_calculator_recursive_visitor(Node& node, Parent* parent, Visitor&& visitor)") !=
               std::string::npos);
         CHECK(generated.header.find(
                     "void visit_recursive(expression_node<UserData>& node, Visitor&& visitor, Parent* parent)") !=
               std::string::npos);
         CHECK(generated.header.find(
                     "detail::invoke_calculator_recursive_visitor(node, parent, std::forward<Visitor>(visitor));") !=
               std::string::npos);
         CHECK(generated.header.find("inline void write_calculator_debug_indent(std::ostream& os, std::size_t indent)") !=
               std::string::npos);
         CHECK(generated.header.find("inline void write_calculator_debug_block(std::ostream& os, std::string block, std::size_t indent)") !=
               std::string::npos);
         CHECK(generated.header.find("visit_recursive(*node.left, visitor, &node);") != std::string::npos);
      }

      SUBCASE("source output wires parser, errors, and cloning") {
         CHECK(generated.source.find("#include <cpflib>") != std::string::npos);
         CHECK(generated.source.find("#include <runtime/runtime.h>") == std::string::npos);
         CHECK(generated.source.find("grammar_productions{{") != std::string::npos);
         CHECK(generated.source.find("grammar_rule_production_indices{{") != std::string::npos);
         CHECK(generated.source.find("expression_complexity_inputs_0{{") != std::string::npos);
         CHECK(generated.source.find("compute_generated_rule_complexity<expression>(expression_complexity_inputs_0") !=
               std::string::npos);
         CHECK(generated.source.find("std::array<cpf::complexity, 5> expression_complexity_cache{};") != std::string::npos);
         CHECK(generated.source.find("cpf::recognize_result recognize_generated(std::string_view input, std::size_t root_rule)") !=
               std::string::npos);
          CHECK(generated.source.find(
                      "cpf::recognize_result recognize_generated(const cpf::token_sequence& tokens, std::size_t root_rule)") !=
                std::string::npos);
         CHECK(generated.source.find("cpf::parse_result<T> parse_generated(std::string_view input, std::size_t "
                                     "root_rule, const cpf::parse_options& options)") != std::string::npos);
          CHECK(generated.source.find(
                      "cpf::parse_result<T> parse_generated(const cpf::token_sequence& tokens, std::size_t root_rule, const cpf::parse_options& options)") !=
                std::string::npos);
          CHECK(generated.source.find("production_model_metadata{{") != std::string::npos);
          CHECK(generated.source.find(
                      "grammar_model{grammar_spec, production_model_metadata.data(), production_model_metadata.size(), grammar_rule_names.data()}") !=
                std::string::npos);
          CHECK(generated.source.find("options.allow_partial") != std::string::npos);
          CHECK(generated.source.find("cpf::detail::parse_shared_forest<T>(") != std::string::npos);
          CHECK(generated.source.find("cpf::detail::parse_shared_forest<cpf::cst_node>(") != std::string::npos);
          CHECK(generated.source.find("cpf::detail::validate_parse_tree(tree, grammar_model)") != std::string::npos);
          CHECK(generated.source.find("cpf::detail::build_cst_node(tree, grammar_model)") != std::string::npos);
          CHECK(generated.source.find("for (const auto& damage : tree->damage)") != std::string::npos);
          CHECK(generated.source.find("cpf::detail::add_damage(*node, damage);") != std::string::npos);
         CHECK(generated.source.find(
                     "auto detail::complexity_inputs_expression_default(std::size_t production_index) -> std::span<const std::string_view>") !=
               std::string::npos);
         CHECK(generated.source.find(
                     "auto detail::recompute_complexity_expression_default(std::size_t production_index) -> const cpf::complexity&") !=
               std::string::npos);
          CHECK(generated.source.find("auto tokens = cpf::detail::lex_input(input, grammar_spec);") !=
                std::string::npos);
           CHECK(generated.source.find("cpf::detail::earley_parse(tokens, grammar_spec, root_rule, options.allow_partial, order)") !=
                std::string::npos);
           CHECK(generated.source.find("cpf::detail::earley_recognize(tokens, grammar_spec, root_rule)") !=
                std::string::npos);
           CHECK(generated.source.find("auto detail::lex_calculator_generated(std::string_view input) -> cpf::token_sequence") !=
                std::string::npos);
         CHECK(generated.source.find("expression -> addition") != std::string::npos);
         CHECK(generated.source.find("const std::regex regex_0{") != std::string::npos);
         CHECK(generated.source.find("std::unique_ptr<cpf::node> build_node(const parse_node_ptr& tree)") !=
               std::string::npos);
         CHECK(generated.source.find("std::unique_ptr<T> release_built_node_as(std::unique_ptr<cpf::node> built)") !=
               std::string::npos);
         CHECK(generated.source.find("bool validate_generated_node(const cpf::node& node)") != std::string::npos);
          CHECK(generated.source.find("bool validate_generated_tree(const parse_node_ptr& tree)") == std::string::npos);
          CHECK(generated.source.find("std::unique_ptr<cpf::cst_node> build_cst_node(const parse_node_ptr& tree)") ==
                std::string::npos);
          CHECK(generated.source.find("void append_cst_children(const parse_node_ptr& tree, std::vector<cpf::cst_child>& children)") ==
                std::string::npos);
          CHECK(generated.source.find("cpf::visit_cst_recursive(root, [&](const cpf::cst_node& current)") !=
                std::string::npos);
          CHECK(generated.source.find("cpf::detail::node_child_at(tree, 0)") != std::string::npos);
         CHECK(generated.source.find("auto detail::recognize_expression_default(std::string_view input) -> cpf::recognize_result") !=
               std::string::npos);
         CHECK(generated.header.find("for (const auto& damage : node.damage()) {") != std::string::npos);
          CHECK(generated.source.find("successful_children += child_result.forest.size();") == std::string::npos);
          CHECK(generated.source.find("auto filtered_error = cpf::parse_error{};") == std::string::npos);
          CHECK(generated.source.find("partial_candidates") == std::string::npos);
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
         CHECK(generated.source.find("switch (node.rule_id())") != std::string::npos);
         CHECK(generated.source.find("case default_add::RuleId:") != std::string::npos);
         CHECK(generated.source.find("case default_subtract::RuleId:") != std::string::npos);
         CHECK(generated.source.find("case default_multiply::RuleId:") != std::string::npos);
         CHECK(generated.source.find("validate_default_expr_child(*value.left, 1, true, true)") != std::string::npos);
         CHECK(generated.source.find("validate_default_expr_child(*value.right, 3, true, false)") != std::string::npos);
      }

      SUBCASE("default associativity is left and explicit labels generate matched_string fields") {
         CHECK(generated.source.find("validate_default_expr_child(*value.right, 2, true, false)") != std::string::npos);
         CHECK(generated.header.find("struct default_number_node : default_expr_node<UserData>") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string value;") != std::string::npos);
      }
   }

   TEST_CASE("generated source emits lexer symbol tables and terminal-id parser symbols") {
      auto grammar = cpf::parse_grammar(R"(
         token kw_if -> 'if';
         token bare_word -> r'[A-Za-z_]+';
         chosen_word -> kw_if:text;
         chosen_word -> bare_word:text;
         comparison_op -> '==':text;
         comparison_op -> '=':text;
      )");

      auto generated = cpf::generate_code(grammar, "lexer_tables");

      CHECK(generated.source.find("grammar_token_symbols{{") != std::string::npos);
      CHECK(generated.source.find("cpf::detail::lexer_symbol_spec") != std::string::npos);
      CHECK(generated.source.find("cpf::detail::lexer_symbol_kind::literal, \"if\"") != std::string::npos);
      CHECK(generated.source.find("cpf::detail::lexer_symbol_kind::regex, \"[A-Za-z_]+\"") != std::string::npos);
      CHECK(generated.source.find("cpf::detail::parser_symbol_kind::terminal") != std::string::npos);
      CHECK(generated.source.find("\"==\"") != std::string::npos);
      CHECK(generated.source.find("grammar_skip_symbols") != std::string::npos);
   }

   TEST_CASE("@import location affects default precedence through textual inclusion order") {
      auto test_directory = std::filesystem::temp_directory_path() / "cpf_import_precedence_codegen_tests";
      std::filesystem::remove_all(test_directory);

      write_file(test_directory / "multiply_rule.cpf", R"(
         import_precedence_multiply -> import_precedence_expr:left '*':op import_precedence_expr:right;
      )");

      write_file(test_directory / "import_after_add.cpf", R"(
         import_precedence_expr -> import_precedence_add | import_precedence_multiply | import_precedence_number;
         import_precedence_add -> import_precedence_expr:left '+':op import_precedence_expr:right;
         @import 'multiply_rule.cpf';
         import_precedence_number -> r'[0-9]+':value;
      )");

      write_file(test_directory / "import_before_add.cpf", R"(
         import_precedence_expr -> import_precedence_add | import_precedence_multiply | import_precedence_number;
         @import 'multiply_rule.cpf';
         import_precedence_add -> import_precedence_expr:left '+':op import_precedence_expr:right;
         import_precedence_number -> r'[0-9]+':value;
      )");

      auto import_after_add = cpf::generate_code(cpf::parse_grammar_file(test_directory / "import_after_add.cpf"),
                                                 "import_after_add");
      auto import_before_add = cpf::generate_code(cpf::parse_grammar_file(test_directory / "import_before_add.cpf"),
                                                  "import_before_add");

      CHECK(precedence_of_generated_rule(import_after_add.source, "import_precedence_add") == 1);
      CHECK(precedence_of_generated_rule(import_after_add.source, "import_precedence_multiply") == 2);
      CHECK(precedence_of_generated_rule(import_before_add.source, "import_precedence_multiply") == 1);
      CHECK(precedence_of_generated_rule(import_before_add.source, "import_precedence_add") == 2);
   }

   TEST_CASE("default labels fall back to rule identifiers for relative precedence references") {
      auto grammar = cpf::parse_grammar(R"(
         default_label_expr -> default_label_add | default_label_multiply | default_label_number;
         default_label_add [prec < default_label_multiply] -> default_label_expr:left '+':op default_label_expr:right;
         default_label_multiply -> default_label_expr:left '*':op default_label_expr:right;
         default_label_number -> r'[0-9]+':value;
      )");

      auto generated = cpf::generate_code(grammar, "default_labels");

      CHECK(generated.source.find("int precedence_of_default_label_expr(const default_label_expr& node)") !=
            std::string::npos);
      CHECK(generated.source.find("case default_label_add::RuleId:") != std::string::npos);
      CHECK(generated.source.find("case default_label_multiply::RuleId:") != std::string::npos);
      CHECK(generated.source.find("validate_default_label_expr_child(*value.right, 1, true, false)") !=
            std::string::npos);
      CHECK(generated.source.find("validate_default_label_expr_child(*value.right, 2, true, false)") !=
            std::string::npos);
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
         CHECK(generated.header.find("struct merged_wrapper_node : cpf::node_with_user_data<UserData>") != std::string::npos);
         CHECK(generated.header.find("std::unique_ptr<merged_message_node<UserData>> payload;") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string suffix;") != std::string::npos);
      }

      SUBCASE("generated source stamps and preserves matched definitions") {
         CHECK(generated.header.find("static constexpr std::size_t ProductionCount = 2;") != std::string::npos);
         CHECK(generated.header.find("static auto complexity(std::size_t production_index) -> const cpf::complexity&;") !=
               std::string::npos);
         CHECK(generated.source.find("node->production_index = 0;") != std::string::npos);
         CHECK(generated.source.find("node->production_index = 1;") != std::string::npos);
         CHECK(generated.header.find("copy->production_index = node.production_index;") != std::string::npos);
         CHECK(generated.source.find("switch (value.production_index)") != std::string::npos);
         CHECK(generated.source.find(
                     "release_built_node_as<merged_message>(std::move(child_0))") !=
               std::string::npos);
         CHECK(generated.source.find("merged_binary_complexity_inputs_0{{") != std::string::npos);
         CHECK(generated.source.find("merged_binary_complexity_inputs_1{{") != std::string::npos);
         CHECK(generated.source.find(
                     "compute_generated_rule_complexity<merged_binary>(merged_binary_complexity_inputs_1") !=
               std::string::npos);
         CHECK(generated.source.find("std::array<cpf::complexity, 2> merged_binary_complexity_cache{};") !=
               std::string::npos);
         CHECK(generated.source.find("auto child_result = detail::parse_merged_greeting_default(tokens, options);") ==
               std::string::npos);
         CHECK(generated.source.find("cpf::detail::parse_shared_forest<T>(") != std::string::npos);
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
         CHECK(generated.header.find("Exclusive node storage is O(r) across repeated members `values`") !=
               std::string::npos);
         CHECK(generated.header.find("std::unique_ptr<quant_choice_node<UserData>> value;") != std::string::npos);
         CHECK(generated.header.find("std::vector<std::unique_ptr<quant_choice_node<UserData>>> values;") != std::string::npos);
         CHECK(generated.header.find("std::vector<cpf::matched_string> digits;") != std::string::npos);
         CHECK(generated.header.find("std::optional<cpf::matched_string> marker;") != std::string::npos);
      }

      SUBCASE("unlabeled terminals are not captured into implicit fields") {
         auto silent_grammar = cpf::parse_grammar(R"(
            silent -> 'x';
         )");

         auto silent_generated = cpf::generate_code(silent_grammar, "silent");
         CHECK(silent_generated.header.find("struct silent_node : cpf::node_with_user_data<UserData>") != std::string::npos);
         CHECK(silent_generated.header.find("value;") == std::string::npos);
      }

      SUBCASE("source output lowers quantified syntax through helper extractors") {
         CHECK(generated.source.find("extract_helper_") != std::string::npos);
         CHECK(generated.source.find("Unknown quantified helper production") != std::string::npos);
         CHECK(generated.source.find("release_built_node_as<T>(std::move(built))") != std::string::npos);
         CHECK(generated.source.find("parse_node_ptr node_child_at(const parse_node_ptr& tree, std::size_t index)") ==
               std::string::npos);
         CHECK(generated.source.find("cpf::matched_string matched_child_at(const parse_node_ptr& tree, std::size_t index)") ==
               std::string::npos);
         CHECK(generated.source.find("Generated parse tree missing node child") == std::string::npos);
         CHECK(generated.source.find("Generated parse tree child is not a terminal") == std::string::npos);
         CHECK(generated.source.find("build_node(cpf::detail::node_child_at(tree, 0))") != std::string::npos);
         CHECK(generated.source.find("cpf::detail::matched_child_at(tree, 0)") != std::string::npos);
         CHECK(generated.source.find("tree->children.front()") == std::string::npos);
         CHECK(generated.source.find("node->value = extract_helper_") != std::string::npos);
         CHECK(generated.source.find("node->values = extract_helper_") != std::string::npos);
         CHECK(generated.source.find("node->digits = extract_helper_") != std::string::npos);
         CHECK(generated.source.find("node->marker = extract_helper_") != std::string::npos);
      }
   }

   TEST_CASE("grouped grammar syntax lowers without leaking helper rules into the public API") {
      auto grammar = cpf::parse_grammar(R"(
         grouped_value -> ('x':text | 'y':text);
         grouped_sentence -> '(':open ('hi':text | 'bye':text) ')':close;
         grouped_repeat -> ('a' | 'b')+;
      )");

      auto generated = cpf::generate_code(grammar, "grouped");

      SUBCASE("header output exposes only public rules and flattened captures") {
         CHECK(generated.header.find("struct grouped_value_node : cpf::node_with_user_data<UserData>") != std::string::npos);
         CHECK(generated.header.find("struct grouped_sentence_node : cpf::node_with_user_data<UserData>") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string text;") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string open;") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string close;") != std::string::npos);
         CHECK(generated.header.find("$cpf_group_") == std::string::npos);
      }

      SUBCASE("source output still emits Earley productions for lowered groups") {
         CHECK(generated.source.find("grouped_value -> 'x':text") != std::string::npos);
         CHECK(generated.source.find("grouped_value -> 'y':text") != std::string::npos);
         CHECK(generated.source.find("grouped_sentence -> '(':open 'hi':text ')':close") != std::string::npos);
         CHECK(generated.source.find("grouped_sentence -> '(':open 'bye':text ')':close") != std::string::npos);
         CHECK(generated.source.find("extract_helper_") != std::string::npos);
      }
   }

   TEST_CASE("labeled groups generate public members without exposing helper rule types") {
      auto grammar = cpf::parse_grammar(R"(
         grouped_choice_message -> grouped_choice_greeting | grouped_choice_farewell;
         grouped_choice_greeting -> 'hello':text;
         grouped_choice_farewell -> 'bye':text;
         grouped_choice_value -> ('x' | 'y'):value;
         grouped_choice_payload -> (grouped_choice_greeting | grouped_choice_farewell):payload;
      )");

      auto generated = cpf::generate_code(grammar, "grouped_choice");

      CHECK(generated.header.find("cpf::matched_string value;") != std::string::npos);
      CHECK(generated.header.find("std::variant<std::unique_ptr<grouped_choice_greeting_node<UserData>>, "
                                  "std::unique_ptr<grouped_choice_farewell_node<UserData>>> payload;") != std::string::npos);
      CHECK(generated.header.find("auto visit_payload(const grouped_choice_payload_node<UserData>& node, Visitor&& visitor)") !=
            std::string::npos);
      CHECK(generated.header.find("auto visit_payload(grouped_choice_payload_node<UserData>& node, Visitor&& visitor)") !=
             std::string::npos);
      CHECK(generated.header.find("$cpf_group_") == std::string::npos);

      CHECK(generated.source.find("extract_group_capture_") != std::string::npos);
      CHECK(generated.source.find("node->value = extract_group_capture_") != std::string::npos);
      CHECK(generated.source.find("node->payload = extract_group_capture_") != std::string::npos);
      CHECK(generated.header.find("else if constexpr (std::is_same_v<value_t, std::unique_ptr<grouped_choice_farewell_node<UserData>>>)") !=
            std::string::npos);
   }

   TEST_CASE("lookahead predicates emit zero-width parser symbol kinds in generated source") {
      auto grammar = cpf::parse_grammar(R"(
         keyword -> 'if' | 'else';
         identifier -> !keyword r'[A-Za-z_][A-Za-z0-9_]*':value;
         call -> identifier:name &'(' '(':open ')':close;
      )");

      auto generated = cpf::generate_code(grammar, "lookahead_codegen");

      CHECK(generated.source.find("cpf::detail::parser_symbol_kind::negative_nonterminal") != std::string::npos);
      CHECK(generated.source.find("cpf::detail::parser_symbol_kind::positive_terminal") != std::string::npos);
      CHECK(generated.source.find("identifier -> !keyword") != std::string::npos);
      CHECK(generated.source.find("call -> identifier:name &'('") != std::string::npos);
   }

   TEST_CASE("template invocations generate hidden helper nodes with substituted captures") {
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

      auto generated = cpf::generate_code(grammar, "templates_codegen");

      CHECK(generated.header.find("std::unique_ptr<cpf_template_surrounded_") != std::string::npos);
      CHECK(generated.header.find(" body;") != std::string::npos);
      CHECK(generated.header.find("cpf::matched_string open;") != std::string::npos);
      CHECK(generated.header.find("cpf::matched_string value;") != std::string::npos);
      CHECK(generated.header.find("cpf::matched_string close;") != std::string::npos);
      CHECK(generated.source.find("paren_identifier -> cpf_template_surrounded_") != std::string::npos);
      CHECK(generated.source.find("paren_returned_identifier -> cpf_template_surrounded_") != std::string::npos);
      CHECK(generated.source.find("paren_specialized_identifier -> cpf_template_specialized_surrounded_") != std::string::npos);
      CHECK(generated.header.find("std::unique_ptr<cpf_template_keyword_value_") != std::string::npos);
      CHECK(generated.header.find("cpf::matched_string keyword;") != std::string::npos);
      CHECK(generated.source.find("cpf_template_keyword_value_") != std::string::npos);
      CHECK(generated.header.find("std::unique_ptr<cpf_template_prepend_") != std::string::npos);
      CHECK(generated.header.find("cpf::matched_string prep;") != std::string::npos);
      CHECK(generated.header.find("cpf::matched_string suffix;") != std::string::npos);
      CHECK(generated.source.find("cpf_template_prepend_") != std::string::npos);
   }

   TEST_CASE("lifted template literals inherit lexer precedence from their declaring rules") {
      auto grammar = cpf::parse_grammar(
            "template specialized<InnerTempl> -> InnerTempl<'spec'>:value;\n"
            "template prepend<Prep> -> Prep:prep '::value':suffix;\n"
            "token identifier -> r'[A-Za-z_][A-Za-z0-9_]*';\n"
            "specialized_identifier -> specialized<prepend>:body;\n");

      auto generated = cpf::generate_code(grammar, "template_precedence_codegen");

      CHECK(generated.source.find("{cpf::detail::lexer_symbol_kind::literal, \"spec\", nullptr, 2}") !=
            std::string::npos);
      CHECK(generated.source.find("{cpf::detail::lexer_symbol_kind::literal, \"::value\", nullptr, 2}") !=
            std::string::npos);
      CHECK(generated.source.find(
                  "{cpf::detail::lexer_symbol_kind::regex, \"[A-Za-z_][A-Za-z0-9_]*\", &regex_0, 3}") !=
            std::string::npos);
   }

   TEST_CASE("code generation surfaces grammar analysis alongside emitted source") {
      auto grammar = cpf::parse_grammar(R"(
         entry -> used;
         used -> 'x':value;
         detached -> helper;
         helper -> 'y':value;
      )");

      auto generated = cpf::generate_code(grammar, "diagnostics_codegen");

      CHECK(generated.analysis.summary.primary_entry_rule == "entry");
      CHECK(generated.analysis.summary.unused_rule_count == 1);
      CHECK(generated.analysis.summary.unreachable_rule_count == 1);
      CHECK(generated.analysis.has_warnings());
      CHECK_FALSE(generated.analysis.has_errors());
      CHECK(generated.analysis.render_summary().find("warnings=2") != std::string::npos);

      const auto has_unused = std::ranges::any_of(generated.analysis.diagnostics, [](const auto& diagnostic) {
         return diagnostic.code == cpf::grammar_diagnostic_code::unused_rule && diagnostic.rule == "detached";
      });
      const auto has_unreachable = std::ranges::any_of(generated.analysis.diagnostics, [](const auto& diagnostic) {
         return diagnostic.code == cpf::grammar_diagnostic_code::unreachable_rule && diagnostic.rule == "helper";
      });

      CHECK(has_unused);
      CHECK(has_unreachable);
      CHECK(generated.header.find("#pragma once") != std::string::npos);
      CHECK(generated.source.find("diagnostics_codegen.h") != std::string::npos);
   }

   TEST_CASE("generated code can be wrapped in an explicit C++ namespace") {
      auto grammar = cpf::parse_grammar(R"(
         @whitespace ws;
         skip ws -> r'[ \t\r\n]+';
         skip line_comment -> r'//[^\n]*';

         expression -> addition | number;
         addition -> expression:left '+':op expression:right;
         number -> r'[0-9]+':value;
      )");

      auto generated = cpf::generate_code(grammar, "namespaced_calculator", "generated::fixtures");

      CHECK(generated.header.find("namespace generated::fixtures {") != std::string::npos);
      CHECK(generated.header.find("template<typename UserData = void> struct expression_node;") != std::string::npos);
      CHECK(generated.header.find("using expression = expression_node<>;") != std::string::npos);
      CHECK(generated.header.find("struct expression_node : cpf::node_with_user_data<UserData>") != std::string::npos);
      CHECK(generated.header.find("auto visit(const expression_node<UserData>& node, Visitor&& visitor)") != std::string::npos);
      CHECK(generated.header.find("auto visit(expression_node<UserData>& node, Visitor&& visitor)") != std::string::npos);
      CHECK(generated.header.find("} // namespace generated::fixtures") != std::string::npos);

      CHECK(generated.source.find("namespace generated::fixtures {") != std::string::npos);
       CHECK(generated.source.find("grammar_skip_symbols") != std::string::npos);
      CHECK(generated.source.find("auto detail::parse_expression_default(std::string_view input, const cpf::parse_options& options) -> cpf::parse_result<expression>") !=
            std::string::npos);
      CHECK(generated.source.find("} // namespace generated::fixtures") != std::string::npos);
   }

   TEST_CASE("token declarations and inferred lexical helpers generate terminal captures") {
      auto grammar = cpf::parse_grammar(R"(
         token identifier -> identifier_head identifier_tail*;
         identifier_head -> r'[A-Za-z_]';
         identifier_tail -> r'[A-Za-z0-9_]';

         token qualified_identifier -> identifier ('.' identifier)*;
         token value_type -> 'int' | 'void';

         binding -> 'let':keyword qualified_identifier:name ':':colon value_type:type '=':assign identifier:value ';':semi;
      )");

      auto generated = cpf::generate_code(grammar, "tokens");

      SUBCASE("header output lowers token-like references to matched_string members") {
         CHECK(generated.header.find("cpf::matched_string name;") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string type;") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string value;") != std::string::npos);
         CHECK(generated.header.find("std::unique_ptr<qualified_identifier_node<UserData>> name;") == std::string::npos);
         CHECK(generated.header.find("std::unique_ptr<value_type_node<UserData>> type;") == std::string::npos);
      }

      SUBCASE("source output collapses lexical helper subtrees into token text") {
         CHECK(generated.source.find("cpf::matched_string matched_tree_at(const parse_node_ptr& tree)") == std::string::npos);
         CHECK(generated.source.find("append_matched_tree_text") == std::string::npos);
         CHECK(generated.source.find("node->name = cpf::detail::matched_tree_at(") != std::string::npos);
         CHECK(generated.source.find("node->type = cpf::detail::matched_tree_at(") != std::string::npos);
         CHECK(generated.source.find("node->value = cpf::detail::matched_tree_at(") != std::string::npos);
      }
   }

   TEST_CASE("invalid token declarations are rejected during code generation") {
      auto grammar = cpf::parse_grammar(R"(
         token bad_token -> expression '!';
         expression -> number:value;
         number [lbl = 'atom'] -> r'[0-9]+':value;
      )");

      CHECK_THROWS_WITH_AS(cpf::generate_code(grammar, "bad_token"),
                           doctest::Contains("Token rule 'bad_token' must lower only to terminals or lexical rules"),
                           std::runtime_error);
   }

   TEST_CASE("generated code emits node templates with default void user data") {
      auto grammar = cpf::parse_grammar(R"(
         user_value -> user_number;
         user_number -> r'[0-9]+':value;
      )");

      auto generated = cpf::generate_code(grammar, "user_data");

      CHECK(generated.header.find("template<typename UserData = void> struct user_value_node;") != std::string::npos);
      CHECK(generated.header.find("using user_value = user_value_node<>;") != std::string::npos);
      CHECK(generated.header.find("struct user_value_node : cpf::node_with_user_data<UserData>") != std::string::npos);
      CHECK(generated.header.find("struct user_number_node : user_value_node<UserData>") != std::string::npos);
      CHECK(generated.header.find("copy->user_data = node.user_data;") != std::string::npos);
      CHECK(generated.header.find("auto user_value_node<UserData>::parse") != std::string::npos);
   }

   TEST_CASE("generated node templates reject conflicting user_data labels") {
      auto grammar = cpf::parse_grammar(R"(
         conflicting_user_data -> 'x':user_data;
      )");

      CHECK_THROWS_WITH_AS(cpf::generate_code(grammar, "conflicting_user_data"),
                           doctest::Contains("cannot expose label 'user_data'"), std::runtime_error);
   }

   TEST_CASE("invalid generated namespaces are rejected") {
      auto grammar = cpf::parse_grammar("value -> 'x':text;");

      CHECK_THROWS_WITH_AS(cpf::generate_code(grammar, "invalid_namespace", "generated::1broken"),
                           doctest::Contains("Invalid C++ namespace 'generated::1broken'"), std::runtime_error);
   }
}

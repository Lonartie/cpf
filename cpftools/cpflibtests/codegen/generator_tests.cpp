#include <cpfgenlib>

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
         CHECK(generated.header.find("Generated parser API for grammar 'calculator'.") != std::string::npos);
         CHECK(generated.header.find("Every generated parse entry point uses the shared Earley runtime") !=
               std::string::npos);
         CHECK(generated.header.find("Generated AST node for grammar rule 'number'.") != std::string::npos);
         CHECK(generated.header.find("this rule participates in a recursive rule cycle") != std::string::npos);
         CHECK(generated.header.find("Exclusive node storage is O(1).") != std::string::npos);
         CHECK(generated.header.find("using expression = expression_node<>;") != std::string::npos);
         CHECK(generated.header.find("struct expression_node : cpf::node_with_user_data<UserData>") != std::string::npos);
         CHECK(generated.header.find("struct number_node : expression_node<UserData>") != std::string::npos);
         CHECK(generated.header.find("static constexpr std::size_t RuleId = ") != std::string::npos);
         CHECK(generated.header.find("static constexpr std::size_t ProductionCount = 5;") != std::string::npos);
         CHECK(generated.header.find("static auto complexity(std::size_t production_index) -> const cpf::complexity&;") !=
               std::string::npos);
         CHECK(generated.header.find(
                     "static parse_result parse(std::string_view input, const cpf::parse_options& options = {});") !=
               std::string::npos);
         CHECK(generated.header.find("static cpf::recognize_result recognize(std::string_view input);") !=
               std::string::npos);
         CHECK(generated.header.find(
                     "static auto complexity_inputs(std::size_t production_index) -> std::span<const std::string_view>;") !=
               std::string::npos);
         CHECK(generated.header.find(
                     "static auto recompute_complexity(std::size_t production_index) -> const cpf::complexity&;") !=
               std::string::npos);
         CHECK(generated.header.find("std::unique_ptr<expression_node<UserData>> clone() const;") != std::string::npos);
         CHECK(generated.header.find("cpf::matched_string value;") != std::string::npos);
         CHECK(generated.header.find("std::unique_ptr<expression_node<UserData>> left;") != std::string::npos);
         CHECK(generated.header.find("template<typename UserData, typename Visitor>") != std::string::npos);
         CHECK(generated.header.find("switch (node.rule_id())") != std::string::npos);
         CHECK(generated.header.find("auto visit(expression_node<UserData>& node, Visitor&& visitor)") != std::string::npos);
         CHECK(generated.header.find("void visit_recursive(expression_node<UserData>& node, Visitor&& visitor)") != std::string::npos);
      }

      SUBCASE("source output wires parser, errors, and cloning") {
         CHECK(generated.source.find("grammar_productions{{") != std::string::npos);
         CHECK(generated.source.find("grammar_rule_production_indices{{") != std::string::npos);
         CHECK(generated.source.find("expression_complexity_inputs_0{{") != std::string::npos);
         CHECK(generated.source.find("compute_generated_rule_complexity<expression>(expression_complexity_inputs_0") !=
               std::string::npos);
         CHECK(generated.source.find("std::array<cpf::complexity, 5> expression_complexity_cache{};") != std::string::npos);
         CHECK(generated.source.find("cpf::recognize_result recognize_generated(std::string_view input, std::size_t root_rule)") !=
               std::string::npos);
         CHECK(generated.source.find("cpf::parse_result<T> parse_generated(std::string_view input, std::size_t "
                                     "root_rule, const cpf::parse_options& options)") != std::string::npos);
          CHECK(generated.source.find("options.allow_partial") != std::string::npos);
         CHECK(generated.source.find("auto valid_tree_count = std::size_t{0};") != std::string::npos);
         CHECK(generated.source.find("if (!validate_generated_tree(tree))") != std::string::npos);
         CHECK(generated.source.find(
                     "result.error = cpf::detail::make_ambiguity_error(grammar_rule_names[root_rule]);") !=
               std::string::npos);
         CHECK(generated.source.find(
                     "result.status = result.partial ? cpf::parse_status::partial_success : cpf::parse_status::success;") !=
               std::string::npos);
         CHECK(generated.source.find("result.error.reset();") != std::string::npos);
         CHECK(generated.source.find(
                     "result.forest.emplace_back(tree, definition_of_generated_tree(tree), tree->range") !=
               std::string::npos);
          CHECK(generated.source.find("for (const auto& damage : tree->damage)") != std::string::npos);
          CHECK(generated.source.find("cpf::detail::add_damage(*node, damage);") != std::string::npos);
         CHECK(generated.source.find(
                     "auto detail::complexity_inputs_expression_default(std::size_t production_index) -> std::span<const std::string_view>") !=
               std::string::npos);
         CHECK(generated.source.find(
                     "auto detail::recompute_complexity_expression_default(std::size_t production_index) -> const cpf::complexity&") !=
               std::string::npos);
          CHECK(generated.source.find("cpf::detail::earley_parse(input, grammar_spec, root_rule, options.allow_partial)") !=
                std::string::npos);
         CHECK(generated.source.find("expression -> addition") != std::string::npos);
         CHECK(generated.source.find("const std::regex regex_0{") != std::string::npos);
         CHECK(generated.source.find("std::unique_ptr<cpf::node> build_node(const parse_node_ptr& tree)") !=
               std::string::npos);
         CHECK(generated.source.find("std::unique_ptr<T> release_built_node_as(std::unique_ptr<cpf::node> built)") !=
               std::string::npos);
         CHECK(generated.source.find("bool validate_generated_node(const cpf::node& node)") != std::string::npos);
         CHECK(generated.source.find("bool validate_generated_tree(const parse_node_ptr& tree)") != std::string::npos);
         CHECK(generated.source.find("rejected by precedence/associativity constraints") != std::string::npos);
         CHECK(generated.source.find("definition_of_generated_tree(const parse_node_ptr& tree)") != std::string::npos);
         CHECK(generated.source.find("auto detail::recognize_expression_default(std::string_view input) -> cpf::recognize_result") !=
               std::string::npos);
         CHECK(generated.header.find("for (const auto& damage : node.damage()) {") != std::string::npos);
          CHECK(generated.source.find("successful_children += child_result.forest.size();") != std::string::npos);
          CHECK(generated.source.find("auto filtered_error = cpf::parse_error{};") != std::string::npos);
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
         CHECK(generated.source.find("auto child_result = detail::parse_merged_greeting_default(input, options);") !=
               std::string::npos);
         CHECK(generated.source.find("auto opaque = std::static_pointer_cast<const cpf::detail::parse_node>(cpf::detail::opaque_tree_of(tree));") != std::string::npos);
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
         CHECK(generated.source.find("parse_node_ptr node_child_at(const parse_node_ptr& tree, std::size_t index)") !=
               std::string::npos);
         CHECK(generated.source.find("cpf::matched_string matched_child_at(const parse_node_ptr& tree, std::size_t index)") !=
               std::string::npos);
         CHECK(generated.source.find("Generated parse tree missing node child") != std::string::npos);
         CHECK(generated.source.find("Generated parse tree child is not a terminal") != std::string::npos);
         CHECK(generated.source.find("build_node(node_child_at(tree, 0))") != std::string::npos);
         CHECK(generated.source.find("matched_child_at(tree, 0)") != std::string::npos);
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

   TEST_CASE("generated code can be wrapped in an explicit C++ namespace") {
      auto grammar = cpf::parse_grammar(R"(
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
      CHECK(generated.source.find("auto detail::parse_expression_default(std::string_view input, const cpf::parse_options& options) -> cpf::parse_result<expression>") !=
            std::string::npos);
      CHECK(generated.source.find("} // namespace generated::fixtures") != std::string::npos);
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

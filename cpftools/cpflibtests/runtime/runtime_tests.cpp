#include <cpflib>
#include <runtime/runtime.h>

#include "support/doctest.h"

#include <array>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace {
   const std::regex precedence_word_regex{"[A-Za-z_]+", std::regex_constants::optimize};

   constexpr std::array<cpf::detail::lexer_symbol_spec, 2> recover_token_symbols{{
         {cpf::detail::lexer_symbol_kind::literal, "a", nullptr, 0},
         {cpf::detail::lexer_symbol_kind::literal, "b", nullptr, 1}
   }};
   constexpr std::array<cpf::detail::parser_symbol, 2> recover_ab_symbols{{
         {cpf::detail::parser_symbol_kind::terminal, 0, "a"},
         {cpf::detail::parser_symbol_kind::terminal, 1, "b"}
   }};
   constexpr std::array<cpf::detail::production_spec, 1> recover_grammar_productions{{
         {0, "start", "start -> 'a' 'b'", recover_ab_symbols.data(), recover_ab_symbols.size()}
   }};
   constexpr std::array<std::size_t, 1> recover_grammar_rule_production_indices{{0}};
   constexpr std::array<std::size_t, 1> recover_grammar_rule_production_offsets{{0}};
   constexpr std::array<std::size_t, 1> recover_grammar_rule_production_counts{{1}};
   constexpr std::array<std::string_view, 1> recover_grammar_rule_expected_labels{{""}};
   constexpr cpf::detail::grammar_spec recover_grammar_spec{
         recover_grammar_productions.data(),
         recover_grammar_productions.size(),
         1,
         recover_grammar_rule_expected_labels.data(),
         recover_grammar_rule_production_indices.data(),
         recover_grammar_rule_production_offsets.data(),
         recover_grammar_rule_production_counts.data(),
         recover_token_symbols.data(),
         recover_token_symbols.size(),
         nullptr,
         0,
         true};

   constexpr std::array<cpf::detail::parser_symbol, 1> optional_start_symbols{{
         {cpf::detail::parser_symbol_kind::nonterminal, 1, "opt_a"}
   }};
   constexpr std::array<cpf::detail::lexer_symbol_spec, 1> optional_token_symbols{{
         {cpf::detail::lexer_symbol_kind::literal, "a", nullptr, 0}
   }};
   constexpr std::array<cpf::detail::parser_symbol, 0> optional_empty_symbols{{}};
   constexpr std::array<cpf::detail::parser_symbol, 1> optional_value_symbols{{
         {cpf::detail::parser_symbol_kind::terminal, 0, "a"}
   }};
   constexpr std::array<cpf::detail::production_spec, 3> optional_grammar_productions{{
         {0, "start", "start -> opt_a", optional_start_symbols.data(), optional_start_symbols.size()},
         {1, "opt_a", "opt_a -> /* empty */", optional_empty_symbols.data(), optional_empty_symbols.size()},
         {1, "opt_a", "opt_a -> 'a'", optional_value_symbols.data(), optional_value_symbols.size()}
   }};
   constexpr std::array<std::size_t, 3> optional_grammar_rule_production_indices{{0, 1, 2}};
   constexpr std::array<std::size_t, 2> optional_grammar_rule_production_offsets{{0, 1}};
   constexpr std::array<std::size_t, 2> optional_grammar_rule_production_counts{{1, 2}};
   constexpr std::array<std::string_view, 2> optional_grammar_rule_expected_labels{{"", ""}};
   constexpr cpf::detail::grammar_spec optional_grammar_spec{
         optional_grammar_productions.data(),
         optional_grammar_productions.size(),
         2,
         optional_grammar_rule_expected_labels.data(),
         optional_grammar_rule_production_indices.data(),
         optional_grammar_rule_production_offsets.data(),
         optional_grammar_rule_production_counts.data(),
         optional_token_symbols.data(),
         optional_token_symbols.size(),
         nullptr,
         0,
         true};

   constexpr std::array<cpf::detail::lexer_symbol_spec, 4> precedence_token_symbols{{
         {cpf::detail::lexer_symbol_kind::literal, "if", nullptr, 0},
         {cpf::detail::lexer_symbol_kind::regex, "[A-Za-z_]+", &precedence_word_regex, 1},
         {cpf::detail::lexer_symbol_kind::literal, "==", nullptr, 2},
         {cpf::detail::lexer_symbol_kind::literal, "=", nullptr, 3}
   }};
   constexpr std::array<cpf::detail::parser_symbol, 1> precedence_keyword_symbols{{
         {cpf::detail::parser_symbol_kind::terminal, 0, "if"}
   }};
   constexpr std::array<cpf::detail::parser_symbol, 1> precedence_word_symbols{{
         {cpf::detail::parser_symbol_kind::terminal, 1, "[A-Za-z_]+"}
   }};
   constexpr std::array<cpf::detail::parser_symbol, 1> precedence_equals_equals_symbols{{
         {cpf::detail::parser_symbol_kind::terminal, 2, "=="}
   }};
   constexpr std::array<cpf::detail::parser_symbol, 1> precedence_equals_symbols{{
         {cpf::detail::parser_symbol_kind::terminal, 3, "="}
   }};
   constexpr std::array<cpf::detail::production_spec, 4> precedence_grammar_productions{{
         {0, "chosen_word", "chosen_word -> 'if'", precedence_keyword_symbols.data(), precedence_keyword_symbols.size()},
         {0, "chosen_word", "chosen_word -> r'[A-Za-z_]+'", precedence_word_symbols.data(), precedence_word_symbols.size()},
         {1, "comparison_op", "comparison_op -> '=='", precedence_equals_equals_symbols.data(), precedence_equals_equals_symbols.size()},
         {1, "comparison_op", "comparison_op -> '='", precedence_equals_symbols.data(), precedence_equals_symbols.size()}
   }};
   constexpr std::array<std::size_t, 4> precedence_grammar_rule_production_indices{{0, 1, 2, 3}};
   constexpr std::array<std::size_t, 2> precedence_grammar_rule_production_offsets{{0, 2}};
   constexpr std::array<std::size_t, 2> precedence_grammar_rule_production_counts{{2, 2}};
   constexpr std::array<std::string_view, 2> precedence_grammar_rule_expected_labels{{"", ""}};
   constexpr cpf::detail::grammar_spec precedence_grammar_spec{
         precedence_grammar_productions.data(),
         precedence_grammar_productions.size(),
         2,
         precedence_grammar_rule_expected_labels.data(),
         precedence_grammar_rule_production_indices.data(),
         precedence_grammar_rule_production_offsets.data(),
         precedence_grammar_rule_production_counts.data(),
         precedence_token_symbols.data(),
         precedence_token_symbols.size(),
         nullptr,
         0,
         true};

   struct fake_node final : cpf::node {
      static constexpr std::size_t RuleId = 7;

      explicit fake_node(std::string value) : value{std::move(value)} {}
      fake_node(fake_node&&) = default;
      auto operator=(fake_node&&) -> fake_node& = default;
      fake_node(const fake_node&) = delete;
      auto operator=(const fake_node&) -> fake_node& = delete;

      std::string value;

      [[nodiscard]] std::size_t rule_id() const override { return RuleId; }


   protected:
      [[nodiscard]] std::unique_ptr<cpf::node> clone_node() const override {
         auto clone = std::make_unique<fake_node>(value);
         clone->production_index = production_index;
         clone->range = range;
         for (const auto& damage: damage()) {
            cpf::detail::add_damage(*clone, damage);
         }
         return clone;
      }
   };
} // namespace

TEST_SUITE("cpflib.runtime") {
   TEST_CASE("parse results can represent ambiguous parses as multiple trees") {
      cpf::parse_result<fake_node> result;
      result.status = cpf::parse_status::success;
      result.success = true;
      result.forest.emplace_back(std::make_unique<fake_node>("first"));
      result.forest.emplace_back(std::make_unique<fake_node>("second"));

      CHECK(result.status == cpf::parse_status::success);
      CHECK_FALSE(result.error.has_value());
      REQUIRE(result.forest.size() == 2);
      CHECK(result.forest[0]->value == "first");
      CHECK(result.forest[1]->value == "second");
      CHECK(result.forest[0]->rule_id() == fake_node::RuleId);
   }

   TEST_CASE("const parse-tree handles expose read-only materialized nodes") {
      cpf::parse_tree<fake_node> tree{std::make_unique<fake_node>("first")};
      const auto& const_tree = tree;

      CHECK(std::is_same_v<decltype(tree.get()), fake_node*>);
      CHECK(std::is_same_v<decltype(const_tree.get()), const fake_node*>);
      CHECK(std::is_same_v<decltype(tree.operator->()), fake_node*>);
      CHECK(std::is_same_v<decltype(const_tree.operator->()), const fake_node*>);
      CHECK(std::is_same_v<decltype((*tree)), fake_node&>);
      CHECK(std::is_same_v<decltype((*const_tree)), const fake_node&>);

      tree->value = "updated";

      CHECK(const_tree->value == "updated");
      CHECK((&*const_tree) == static_cast<const fake_node*>(tree.get()));
   }

   TEST_CASE("clone-bearing runtime types are move-only") {
      CHECK(std::is_move_constructible_v<fake_node>);
      CHECK(std::is_move_assignable_v<fake_node>);
      CHECK_FALSE(std::is_copy_constructible_v<fake_node>);
      CHECK_FALSE(std::is_copy_assignable_v<fake_node>);
      CHECK(std::is_move_constructible_v<cpf::parse_tree<fake_node>>);
      CHECK(std::is_move_assignable_v<cpf::parse_tree<fake_node>>);
      CHECK_FALSE(std::is_copy_constructible_v<cpf::parse_tree<fake_node>>);
      CHECK_FALSE(std::is_copy_assignable_v<cpf::parse_tree<fake_node>>);
   }

   TEST_CASE("moved parse-tree handles preserve lazy materialization state") {
      auto materialize_count = std::size_t{0};
      cpf::parse_tree<fake_node> original{{}, 3, {}, [&]() {
                                           ++materialize_count;
                                           return std::make_unique<fake_node>("lazy");
                                        }};
      auto moved = std::move(original);

      CHECK_FALSE(moved.has_materialized());

      auto* materialized = moved.get();

      REQUIRE(materialized != nullptr);
      CHECK(materialize_count == 1);
      CHECK(moved.has_materialized());

      cpf::parse_tree<fake_node> assigned;
      assigned = std::move(moved);

      CHECK(assigned.has_materialized());
      CHECK(assigned.get() == materialized);
   }

   TEST_CASE("cloned parse-tree handles rematerialize lazily without mutating the original handle") {
      auto materialize_count = std::size_t{0};
      cpf::parse_tree<fake_node> original{{}, 3, {}, [&]() {
                                           ++materialize_count;
                                           return std::make_unique<fake_node>("lazy");
                                        }};
      auto clone = original.clone();

      CHECK_FALSE(original.has_materialized());
      CHECK_FALSE(clone.has_materialized());

      auto* clone_materialized = clone.get();

      REQUIRE(clone_materialized != nullptr);
      CHECK(materialize_count == 1);
      CHECK_FALSE(original.has_materialized());
      CHECK(clone.has_materialized());

      auto* original_materialized = original.get();

      REQUIRE(original_materialized != nullptr);
      CHECK(materialize_count == 2);
      CHECK(original.has_materialized());
      CHECK(original_materialized != clone_materialized);
   }

   TEST_CASE("matched strings keep both text and source ranges") {
      cpf::matched_string match;
      match.text = "hello";
      match.range.begin.offset = 3;
      match.range.begin.line = 2;
      match.range.begin.column = 4;
      match.range.end.offset = 8;
      match.range.end.line = 2;
      match.range.end.column = 9;

      CHECK(match.text == "hello");
      CHECK(match.range.begin.offset == 3);
      CHECK(match.range.begin.line == 2);
      CHECK(match.range.begin.column == 4);
      CHECK(match.range.end.offset == 8);
      CHECK(match.range.end.column == 9);
   }

   TEST_CASE("token sequences stream as readable multiline debug output") {
      auto tokens = cpf::token_sequence{};
      tokens.input = "if!";
      tokens.tokens.push_back(cpf::lexed_token{0, cpf::matched_string{"if", {{0, 1, 1}, {2, 1, 3}}}, false});
      tokens.tokens.push_back(cpf::lexed_token{0, cpf::matched_string{"!", {{2, 1, 3}, {3, 1, 4}}}, true});

      auto stream = std::ostringstream{};
      stream << tokens;

      CHECK(stream.str().find("token_sequence(\n") != std::string::npos);
      CHECK(stream.str().find("input = \"if!\"") != std::string::npos);
      CHECK(stream.str().find("[0] { symbol = 0, text = \"if\"") != std::string::npos);
      CHECK(stream.str().find("[1] { invalid = true, text = \"!\"") != std::string::npos);
      CHECK(stream.str().find("range = 0..2 (1:1-1:3)") != std::string::npos);
   }

   TEST_CASE("error tracker reports furthest failures with context notes") {
      cpf::detail::error_tracker tracker;
      tracker.record(4, "pattern [0-9]+", "while parsing rule 'number' via number -> r'[0-9]+' (after symbol 0 of 1)");
      tracker.record(4, "\"(\"", "while parsing rule 'group' via group -> '(' expr ')' (after symbol 0 of 3)");

      auto error = tracker.build("1 + * 2");

      CHECK(error.position.offset == 4);
      CHECK(error.position.line == 1);
      CHECK(error.position.column == 5);
      CHECK(error.found.kind == cpf::parse_error_found_kind::token);
      CHECK(error.found.text == "*");
      CHECK(error.expected.size() == 2);
      CHECK(error.message.find("pattern [0-9]+") != std::string::npos);
      CHECK(error.message.find("\"(\"") != std::string::npos);
      CHECK(error.message.find("Notes:") != std::string::npos);
      CHECK(error.message.find("while parsing rule 'number'") != std::string::npos);
      CHECK(error.message.find("while parsing rule 'group'") != std::string::npos);
   }

   TEST_CASE("parse errors at the same position merge expectations and notes") {
      cpf::parse_error merged;
      merged.position.line = 2;
      merged.position.column = 4;
      merged.expected = {"\"hello\""};
      merged.found.kind = cpf::parse_error_found_kind::token;
      merged.found.text = "help";
      merged.notes = {"while parsing rule 'say_hello'"};
      cpf::detail::error_tracker::finalize(merged);

      cpf::parse_error candidate;
      candidate.position.line = 2;
      candidate.position.column = 4;
      candidate.expected = {"\"world\""};
      candidate.found.kind = cpf::parse_error_found_kind::token;
      candidate.found.text = "help";
      candidate.notes = {"while parsing rule 'say_world'"};
      cpf::detail::error_tracker::finalize(candidate);

      cpf::detail::merge_parse_error(merged, candidate);

      CHECK(merged.expected.size() == 2);
      CHECK(merged.notes.size() == 2);
      CHECK(merged.message.find("\"hello\"") != std::string::npos);
      CHECK(merged.message.find("\"world\"") != std::string::npos);
      CHECK(merged.message.find("while parsing rule 'say_hello'") != std::string::npos);
      CHECK(merged.message.find("while parsing rule 'say_world'") != std::string::npos);
   }

   TEST_CASE("earley_parse keeps optional helper productions stable for empty and present inputs") {
      for (auto input: {std::string_view{}, std::string_view{"a"}}) {
         auto result = cpf::detail::earley_parse(input, optional_grammar_spec, 0);

         REQUIRE(result.success);
         REQUIRE(result.forest.size() == 1);

         const auto& root = result.forest.front();
         REQUIRE(root != nullptr);
         CHECK(root->production == 0);
         REQUIRE(root->children.size() == 1);

         const auto& helper = std::get<cpf::detail::parse_node_ptr>(root->children.front());
         REQUIRE(helper != nullptr);

         if (input.empty()) {
            CHECK(helper->production == 1);
            CHECK(helper->children.empty());
         } else {
            CHECK(helper->production == 2);
            REQUIRE(helper->children.size() == 1);
            CHECK(std::get<cpf::matched_string>(helper->children.front()).text == "a");
         }
      }
   }

   TEST_CASE("earley_parse keeps ignored invalid input inside one recovered tree") {
      auto result = cpf::detail::earley_parse("a!b", recover_grammar_spec, 0, true);

      REQUIRE(result.success);
      CHECK(result.partial);
      REQUIRE(result.forest.size() == 1);
      REQUIRE(result.tree_partial.size() == 1);
      CHECK(result.tree_partial.front());

      const auto& root = result.forest.front();
      REQUIRE(root != nullptr);
      CHECK(root->partial);
      CHECK(root->children.size() == 2);
      REQUIRE(root->damage.size() == 1);
      CHECK(root->damage.front().range.begin.offset == 1);
      CHECK(root->damage.front().range.end.offset == 2);
      CHECK(root->damage.front().reason == cpf::node_damage_reason::ignored_invalid_input);
      CHECK(root->damage.front().detail == "\"!\"");
      CHECK(root->damage.front().message.find("could not match \"b\"") != std::string::npos);
      CHECK(std::get<cpf::matched_string>(root->children[0]).text == "a");
      CHECK(std::get<cpf::matched_string>(root->children[1]).text == "b");
   }

   TEST_CASE("earley_parse can insert a virtual literal inside one recovered tree") {
      auto result = cpf::detail::earley_parse("a", recover_grammar_spec, 0, true);

      REQUIRE(result.success);
      CHECK(result.partial);
      REQUIRE(result.forest.size() == 1);

      const auto& root = result.forest.front();
      REQUIRE(root != nullptr);
      CHECK(root->partial);
      REQUIRE(root->children.size() == 2);
      REQUIRE(root->damage.size() == 1);
      CHECK(root->damage.front().reason == cpf::node_damage_reason::inserted_virtual_token);
      CHECK(root->damage.front().detail == "\"b\"");
      CHECK(root->damage.front().message.find("available tokens") != std::string::npos);
      CHECK(root->damage.front().message.find("\"b\"") != std::string::npos);

      const auto inserted = std::get<cpf::matched_string>(root->children[1]);
      CHECK(inserted.text == "b");
      CHECK(inserted.range.begin.offset == 1);
      CHECK(inserted.range.end.offset == 1);
   }

   TEST_CASE("earley_parse tokenizes with precedence for equal lengths and longest match for shared prefixes") {
      auto keyword = cpf::detail::earley_parse("if", precedence_grammar_spec, 0);
      REQUIRE(keyword.success);
      REQUIRE(keyword.forest.size() == 1);
      CHECK(keyword.forest.front()->production == 0);
      CHECK(std::get<cpf::matched_string>(keyword.forest.front()->children.front()).text == "if");

      auto identifier = cpf::detail::earley_parse("iff", precedence_grammar_spec, 0);
      REQUIRE(identifier.success);
      REQUIRE(identifier.forest.size() == 1);
      CHECK(identifier.forest.front()->production == 1);
      CHECK(std::get<cpf::matched_string>(identifier.forest.front()->children.front()).text == "iff");

      auto double_equals = cpf::detail::earley_parse("==", precedence_grammar_spec, 1);
      REQUIRE(double_equals.success);
      REQUIRE(double_equals.forest.size() == 1);
      CHECK(double_equals.forest.front()->production == 2);

      auto equals = cpf::detail::earley_parse("=", precedence_grammar_spec, 1);
      REQUIRE(equals.success);
      REQUIRE(equals.forest.size() == 1);
      CHECK(equals.forest.front()->production == 3);
   }

   TEST_CASE("earley runtime can reuse caller-provided token sequences") {
      auto tokens = cpf::detail::lex_input("if", precedence_grammar_spec);

      REQUIRE(tokens.size() == 1);
      CHECK_FALSE(tokens.front().invalid);
      CHECK(tokens.front().symbol == 0);
      CHECK(tokens.front().text.text == "if");
      CHECK(tokens.front().text.range.begin.offset == 0);
      CHECK(tokens.front().text.range.end.offset == 2);

      CHECK(tokens.input == "if");

      auto parsed = cpf::detail::earley_parse(tokens, precedence_grammar_spec, 0);
      REQUIRE(parsed.success);
      REQUIRE(parsed.forest.size() == 1);
      CHECK(parsed.forest.front()->production == 0);

      auto recognized = cpf::detail::earley_recognize(tokens, precedence_grammar_spec, 0);
      CHECK(recognized.success);
      CHECK_FALSE(recognized.error.has_value());
   }
}

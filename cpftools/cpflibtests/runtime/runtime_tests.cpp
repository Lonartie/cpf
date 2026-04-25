#include <cpflib>

#include "support/doctest.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>

namespace {
   constexpr std::array<cpf::detail::parser_symbol, 2> recover_ab_symbols{{
         {cpf::detail::parser_symbol_kind::literal, 0, "a", nullptr},
         {cpf::detail::parser_symbol_kind::literal, 0, "b", nullptr}
   }};
   constexpr std::array<cpf::detail::production_spec, 1> recover_grammar_productions{{
         {0, "start", "start -> 'a' 'b'", recover_ab_symbols.data(), recover_ab_symbols.size()}
   }};
   constexpr std::array<std::size_t, 1> recover_grammar_rule_production_indices{{0}};
   constexpr std::array<std::size_t, 1> recover_grammar_rule_production_offsets{{0}};
   constexpr std::array<std::size_t, 1> recover_grammar_rule_production_counts{{1}};
   constexpr cpf::detail::grammar_spec recover_grammar_spec{
         recover_grammar_productions.data(),
         recover_grammar_productions.size(),
         1,
         recover_grammar_rule_production_indices.data(),
         recover_grammar_rule_production_offsets.data(),
         recover_grammar_rule_production_counts.data()};

   constexpr std::array<cpf::detail::parser_symbol, 1> optional_start_symbols{{
         {cpf::detail::parser_symbol_kind::nonterminal, 1, "opt_a", nullptr}
   }};
   constexpr std::array<cpf::detail::parser_symbol, 0> optional_empty_symbols{{}};
   constexpr std::array<cpf::detail::parser_symbol, 1> optional_value_symbols{{
         {cpf::detail::parser_symbol_kind::literal, 0, "a", nullptr}
   }};
   constexpr std::array<cpf::detail::production_spec, 3> optional_grammar_productions{{
         {0, "start", "start -> opt_a", optional_start_symbols.data(), optional_start_symbols.size()},
         {1, "opt_a", "opt_a -> /* empty */", optional_empty_symbols.data(), optional_empty_symbols.size()},
         {1, "opt_a", "opt_a -> 'a'", optional_value_symbols.data(), optional_value_symbols.size()}
   }};
   constexpr std::array<std::size_t, 3> optional_grammar_rule_production_indices{{0, 1, 2}};
   constexpr std::array<std::size_t, 2> optional_grammar_rule_production_offsets{{0, 1}};
   constexpr std::array<std::size_t, 2> optional_grammar_rule_production_counts{{1, 2}};
   constexpr cpf::detail::grammar_spec optional_grammar_spec{
         optional_grammar_productions.data(),
         optional_grammar_productions.size(),
         2,
         optional_grammar_rule_production_indices.data(),
         optional_grammar_rule_production_offsets.data(),
         optional_grammar_rule_production_counts.data()};

   struct fake_node final : cpf::node {
      static constexpr std::size_t RuleId = 7;

      explicit fake_node(std::string value) : value{std::move(value)} {}

      std::string value;

      [[nodiscard]] std::size_t rule_id() const override { return RuleId; }

      [[nodiscard]] const std::type_info& type() const override { return typeid(fake_node); }

   protected:
      [[nodiscard]] std::unique_ptr<cpf::node> clone_node() const override {
         return std::make_unique<fake_node>(*this);
      }
   };
} // namespace

TEST_SUITE("cpflib.runtime") {
   TEST_CASE("parse results can represent ambiguous parses as multiple trees") {
      cpf::parse_result<fake_node> result;
      result.success = true;
      result.forest.emplace_back(std::make_unique<fake_node>("first"));
      result.forest.emplace_back(std::make_unique<fake_node>("second"));

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

   TEST_CASE("error tracker reports furthest failures with context notes") {
      cpf::detail::error_tracker tracker;
      tracker.record(4, "pattern [0-9]+", "while parsing rule 'number' via number -> r'[0-9]+' (after symbol 0 of 1)");
      tracker.record(4, "\"(\"", "while parsing rule 'group' via group -> '(' expr ')' (after symbol 0 of 3)");

      auto error = tracker.build("1 + * 2");

      CHECK(error.offset == 4);
      CHECK(error.line == 1);
      CHECK(error.column == 5);
      CHECK(error.found == "\"*\"");
      CHECK(error.expected.size() == 2);
      CHECK(error.message.find("pattern [0-9]+") != std::string::npos);
      CHECK(error.message.find("\"(\"") != std::string::npos);
      CHECK(error.message.find("Notes:") != std::string::npos);
      CHECK(error.message.find("while parsing rule 'number'") != std::string::npos);
      CHECK(error.message.find("while parsing rule 'group'") != std::string::npos);
   }

   TEST_CASE("parse errors at the same position merge expectations and notes") {
      cpf::parse_error merged;
      merged.line = 2;
      merged.column = 4;
      merged.expected = {"\"hello\""};
      merged.found = "\"help\"";
      merged.notes = {"while parsing rule 'say_hello'"};
      cpf::detail::error_tracker::finalize(merged);

      cpf::parse_error candidate;
      candidate.line = 2;
      candidate.column = 4;
      candidate.expected = {"\"world\""};
      candidate.found = "\"help\"";
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
}

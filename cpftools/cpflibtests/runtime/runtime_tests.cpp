#include <cpflib>

#include "support/doctest.h"

#include <memory>
#include <string>
#include <typeinfo>

namespace {
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
      result.forest.push_back(std::make_unique<fake_node>("first"));
      result.forest.push_back(std::make_unique<fake_node>("second"));

      REQUIRE(result.forest.size() == 2);
      CHECK(result.forest[0]->value == "first");
      CHECK(result.forest[1]->value == "second");
      CHECK(result.forest[0]->rule_id() == fake_node::RuleId);
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
}

#include "benchmark_support.h"

#include "calculator.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
   namespace calculator = cpfbench::calculator;

   struct benchmark_case {
      const char* label = "";
      std::string input;
      cpf::token_sequence compiletime_tokens;
      cpf::token_sequence runtime_tokens;
      cpf::parse_tree<calculator::expression> compiletime_tree;
      cpf::parse_tree<cpf::dynamic_node> runtime_tree;
      std::unique_ptr<calculator::expression> compiletime_root;
      std::unique_ptr<cpf::dynamic_node> runtime_root;
   };

   struct compiletime_calculator_visitor {
      [[nodiscard]] auto visit(const calculator::expression& node) const -> int { return calculator::visit(node, *this); }
      [[nodiscard]] auto operator()(const calculator::addition& node) const -> int {
         return visit(*node.left) + visit(*node.right);
      }
      [[nodiscard]] auto operator()(const calculator::subtraction& node) const -> int {
         return visit(*node.left) - visit(*node.right);
      }
      [[nodiscard]] auto operator()(const calculator::multiplication& node) const -> int {
         return visit(*node.left) * visit(*node.right);
      }
      [[nodiscard]] auto operator()(const calculator::division& node) const -> int {
         return visit(*node.left) / visit(*node.right);
      }
      [[nodiscard]] auto operator()(const calculator::number& node) const -> int { return std::stoi(node.value.text); }
   };

   [[nodiscard]] auto required_dynamic_field(const cpf::dynamic_node& node, std::string_view name)
         -> const cpf::dynamic_field& {
      const auto* field = node.get_field(name);
      if (field == nullptr) {
         throw std::runtime_error{"Missing dynamic field"};
      }
      return *field;
   }

   [[nodiscard]] auto required_dynamic_child(const cpf::dynamic_node& node, std::string_view name)
         -> const cpf::dynamic_node& {
      const auto& field = required_dynamic_field(node, name);
      if (field.node == nullptr) {
         throw std::runtime_error{"Missing dynamic child node"};
      }
      return *field.node;
   }

   [[nodiscard]] auto required_dynamic_token(const cpf::dynamic_node& node, std::string_view name)
         -> const cpf::matched_string& {
      const auto& field = required_dynamic_field(node, name);
      if (!field.token.has_value()) {
         throw std::runtime_error{"Missing dynamic token"};
      }
      return *field.token;
   }

   struct runtime_calculator_visitor {
      [[nodiscard]] auto operator()(const cpf::dynamic_node& node) const -> int {
         const auto child_value = [&](std::string_view field_name) {
            return cpf::visit(required_dynamic_child(node, field_name), *this);
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
            return std::stoi(required_dynamic_token(node, "value").text);
         }

         throw std::runtime_error{"Unexpected dynamic calculator rule"};
      }
   };

   template<typename T>
   [[nodiscard]] auto parse_single_tree(cpf::parse_result<T>&& result, std::string_view benchmark_name)
         -> cpf::parse_tree<T> {
      if (!result.success || result.forest.size() != 1) {
         auto message = std::string{"Failed to prepare benchmark '"} + std::string{benchmark_name} + "': ";
         if (!result.success) {
            message += cpfbench::detail::parse_error_message(result);
         } else {
            message += "expected exactly one parse tree";
         }
         throw std::runtime_error{message};
      }

      return std::move(result.forest.front());
   }

   [[nodiscard]] std::string join_terms(std::size_t count, const char* separator) {
      std::string expression;
      expression.reserve(count * 8);
      for (std::size_t i = 0; i < count; ++i) {
         if (!expression.empty()) {
            expression += separator;
         }
         expression += std::to_string((i % 9) + 1);
      }
      return expression;
   }

   [[nodiscard]] std::string make_layered_expression(std::size_t additions, std::size_t multiplications) {
      std::string expression;
      expression.reserve((additions + 1) * (multiplications + 8));
      for (std::size_t i = 0; i < additions; ++i) {
         if (!expression.empty()) {
            expression += " + ";
         }
         expression += join_terms(multiplications, " * ");
      }
      return expression;
   }

   [[nodiscard]] const std::string& small_expression() {
      static const auto expression = std::string{"1 + 2 * 3"};
      return expression;
   }

   [[nodiscard]] const std::string& medium_expression() {
      static const auto expression = std::string{"1 + 2 * 3 - 4 / 2 + 5 * 6"};
      return expression;
   }

   [[nodiscard]] const std::string& large_expression() {
      static const auto expression = make_layered_expression(3, 3);
      return expression;
   }

   [[nodiscard]] auto runtime_calculator_parser() -> const cpf::compiled_grammar& {
      static const auto parser = cpf::compile_grammar_file(std::filesystem::path{CPF_BENCH_CALCULATOR_GRAMMAR_PATH});
      return parser;
   }

   [[nodiscard]] auto benchmark_cases() -> const std::vector<benchmark_case>& {
      static const auto cases = [] {
         auto built_cases = std::vector<benchmark_case>{};
         built_cases.reserve(3);

         const auto& runtime_parser = runtime_calculator_parser();
         for (const auto& [label, input]: std::initializer_list<std::pair<const char*, std::string>>{
                {"small", small_expression()},
                {"medium", medium_expression()},
                {"large", large_expression()},
         }) {
            benchmark_case current_case;
            current_case.label = label;
            current_case.input = input;
            current_case.compiletime_tokens = calculator::expression::lex(current_case.input);
            current_case.runtime_tokens = runtime_parser.lex(current_case.input);
            current_case.compiletime_tree = parse_single_tree(calculator::expression::parse(current_case.compiletime_tokens),
                                                             "calculator.compiletime_materialize_ast");
            auto compiletime_materialized = current_case.compiletime_tree.clone();
            current_case.compiletime_root = compiletime_materialized->clone();

            current_case.runtime_tree = parse_single_tree(runtime_parser.parse(current_case.runtime_tokens),
                                                          "calculator.runtime_materialize_ast");

            auto runtime_materialized = current_case.runtime_tree.clone();
            current_case.runtime_root = runtime_materialized->clone();
            built_cases.push_back(std::move(current_case));
         }

         return built_cases;
      }();
      return cases;
   }

   [[nodiscard]] auto calculator_case(std::int64_t characters) -> const benchmark_case* {
      for (const auto& current_case: benchmark_cases()) {
         if (static_cast<std::int64_t>(current_case.input.size()) == characters) {
            return &current_case;
         }
      }

      benchmark::DoNotOptimize(characters);
      return nullptr;
   }

   void report_unknown_case(benchmark::State& state, std::string_view benchmark_name, std::int64_t characters) {
      auto error = std::string{"Benchmark '"} + std::string{benchmark_name} + "' received an unsupported input size";
      benchmark::DoNotOptimize(characters);
      state.SkipWithError(error);
   }

   void calculator_compiletime_lex_input(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.compiletime_lex_input", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_lex_input(state, selected_case->input, "calculator.compiletime_lex_input",
                                    [](std::string_view input) { return calculator::expression::lex(input); });
   }

   void calculator_runtime_lex_input(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.runtime_lex_input", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_lex_input(state, selected_case->input, "calculator.runtime_lex_input",
                                    [](std::string_view input) { return runtime_calculator_parser().lex(input); });
   }

   void calculator_compiletime_parse_tokens(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.compiletime_parse_tokens", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_parse_tokens(state, selected_case->compiletime_tokens,
                                       "calculator.compiletime_parse_tokens",
                                       [](const cpf::token_sequence& tokens) { return calculator::expression::parse(tokens); });
   }

   void calculator_runtime_parse_tokens(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.runtime_parse_tokens", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_parse_tokens(state, selected_case->runtime_tokens, "calculator.runtime_parse_tokens",
                                       [](const cpf::token_sequence& tokens) {
                                          return runtime_calculator_parser().parse(tokens);
                                       });
   }

   void calculator_compiletime_materialize_ast(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.compiletime_materialize_ast", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_materialize_ast(state, selected_case->input.size(), "calculator.compiletime_materialize_ast",
                                          [selected_case] { return selected_case->compiletime_tree.clone(); });
   }

   void calculator_runtime_materialize_ast(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.runtime_materialize_ast", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_materialize_ast(state, selected_case->input.size(), "calculator.runtime_materialize_ast",
                                          [selected_case] { return selected_case->runtime_tree.clone(); });
   }

   void calculator_compiletime_interpret_result(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.compiletime_interpret_result", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_interpret_result(
            state, *selected_case->compiletime_root, selected_case->input.size(),
            [](const calculator::expression& root) { return compiletime_calculator_visitor{}.visit(root); });
   }

   void calculator_runtime_interpret_result(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.runtime_interpret_result", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_interpret_result(
            state, *selected_case->runtime_root, selected_case->input.size(),
            [](const cpf::dynamic_node& root) { return cpf::visit(root, runtime_calculator_visitor{}); });
   }
} // namespace

BENCHMARK(calculator_compiletime_lex_input)
      ->Name("calculator/compiletime_lex_input")
      ->ArgName("chars")
      ->Arg(static_cast<std::int64_t>(small_expression().size()))
      ->Arg(static_cast<std::int64_t>(medium_expression().size()))
      ->Arg(static_cast<std::int64_t>(large_expression().size()))
      ->Repetitions(8)
      ->MinWarmUpTime(0.1)
      ->ReportAggregatesOnly()
      ->ComputeStatistics("min", &cpfbench::detail::minimum)
      ->ComputeStatistics("max", &cpfbench::detail::maximum)
      ->Complexity()
      ->Unit(benchmark::kMicrosecond);

BENCHMARK(calculator_runtime_lex_input)
      ->Name("calculator/runtime_lex_input")
      ->ArgName("chars")
      ->Arg(static_cast<std::int64_t>(small_expression().size()))
      ->Arg(static_cast<std::int64_t>(medium_expression().size()))
      ->Arg(static_cast<std::int64_t>(large_expression().size()))
      ->Repetitions(8)
      ->MinWarmUpTime(0.1)
      ->ReportAggregatesOnly()
      ->ComputeStatistics("min", &cpfbench::detail::minimum)
      ->ComputeStatistics("max", &cpfbench::detail::maximum)
      ->Complexity()
      ->Unit(benchmark::kMicrosecond);

BENCHMARK(calculator_compiletime_parse_tokens)
      ->Name("calculator/compiletime_parse_tokens")
      ->ArgName("chars")
      ->Arg(static_cast<std::int64_t>(small_expression().size()))
      ->Arg(static_cast<std::int64_t>(medium_expression().size()))
      ->Arg(static_cast<std::int64_t>(large_expression().size()))
      ->Repetitions(8)
      ->MinWarmUpTime(0.1)
      ->ReportAggregatesOnly()
      ->ComputeStatistics("min", &cpfbench::detail::minimum)
      ->ComputeStatistics("max", &cpfbench::detail::maximum)
      ->Complexity()
      ->Unit(benchmark::kMicrosecond);

BENCHMARK(calculator_runtime_parse_tokens)
      ->Name("calculator/runtime_parse_tokens")
      ->ArgName("chars")
      ->Arg(static_cast<std::int64_t>(small_expression().size()))
      ->Arg(static_cast<std::int64_t>(medium_expression().size()))
      ->Arg(static_cast<std::int64_t>(large_expression().size()))
      ->Repetitions(8)
      ->MinWarmUpTime(0.1)
      ->ReportAggregatesOnly()
      ->ComputeStatistics("min", &cpfbench::detail::minimum)
      ->ComputeStatistics("max", &cpfbench::detail::maximum)
      ->Complexity()
      ->Unit(benchmark::kMicrosecond);

BENCHMARK(calculator_compiletime_materialize_ast)
      ->Name("calculator/compiletime_materialize_ast")
      ->ArgName("chars")
      ->Arg(static_cast<std::int64_t>(small_expression().size()))
      ->Arg(static_cast<std::int64_t>(medium_expression().size()))
      ->Arg(static_cast<std::int64_t>(large_expression().size()))
      ->Repetitions(8)
      ->MinWarmUpTime(0.1)
      ->ReportAggregatesOnly()
      ->ComputeStatistics("min", &cpfbench::detail::minimum)
      ->ComputeStatistics("max", &cpfbench::detail::maximum)
      ->Complexity()
      ->Unit(benchmark::kMicrosecond);

BENCHMARK(calculator_runtime_materialize_ast)
      ->Name("calculator/runtime_materialize_ast")
      ->ArgName("chars")
      ->Arg(static_cast<std::int64_t>(small_expression().size()))
      ->Arg(static_cast<std::int64_t>(medium_expression().size()))
      ->Arg(static_cast<std::int64_t>(large_expression().size()))
      ->Repetitions(8)
      ->MinWarmUpTime(0.1)
      ->ReportAggregatesOnly()
      ->ComputeStatistics("min", &cpfbench::detail::minimum)
      ->ComputeStatistics("max", &cpfbench::detail::maximum)
      ->Complexity()
      ->Unit(benchmark::kMicrosecond);

BENCHMARK(calculator_compiletime_interpret_result)
      ->Name("calculator/compiletime_interpret_result")
      ->ArgName("chars")
      ->Arg(static_cast<std::int64_t>(small_expression().size()))
      ->Arg(static_cast<std::int64_t>(medium_expression().size()))
      ->Arg(static_cast<std::int64_t>(large_expression().size()))
      ->Repetitions(8)
      ->MinWarmUpTime(0.1)
      ->ReportAggregatesOnly()
      ->ComputeStatistics("min", &cpfbench::detail::minimum)
      ->ComputeStatistics("max", &cpfbench::detail::maximum)
      ->Complexity()
      ->Unit(benchmark::kMicrosecond);

BENCHMARK(calculator_runtime_interpret_result)
      ->Name("calculator/runtime_interpret_result")
      ->ArgName("chars")
      ->Arg(static_cast<std::int64_t>(small_expression().size()))
      ->Arg(static_cast<std::int64_t>(medium_expression().size()))
      ->Arg(static_cast<std::int64_t>(large_expression().size()))
      ->Repetitions(8)
      ->MinWarmUpTime(0.1)
      ->ReportAggregatesOnly()
      ->ComputeStatistics("min", &cpfbench::detail::minimum)
      ->ComputeStatistics("max", &cpfbench::detail::maximum)
      ->Complexity()
      ->Unit(benchmark::kMicrosecond);


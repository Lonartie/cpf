#include "benchmark_support.h"

#include "calculator.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

namespace {
   namespace calculator = cpfbench::calculator;

   struct benchmark_case {
      const char* label;
      const std::string* input;
   };

   struct calculator_visitor {
      auto visit(auto& node) const { return calculator::visit(node, *this); }

      int operator()(const calculator::addition& node) const { return visit(*node.left) + visit(*node.right); }

      int operator()(const calculator::subtraction& node) const { return visit(*node.left) - visit(*node.right); }

      int operator()(const calculator::multiplication& node) const { return visit(*node.left) * visit(*node.right); }

      int operator()(const calculator::division& node) const {
         auto numerator = visit(*node.left);
         auto denominator = visit(*node.right);
         return denominator == 0 ? 0 : numerator / denominator;
      }

      int operator()(const calculator::number& node) const { return std::stoi(node.value.text); }
   };

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

   [[nodiscard]] auto calculator_case(std::int64_t characters) -> const benchmark_case* {
      static const benchmark_case cases[] = {
            {"small", &small_expression()},
            {"medium", &medium_expression()},
            {"large", &large_expression()},
      };

      for (const auto& current_case: cases) {
         if (static_cast<std::int64_t>(current_case.input->size()) == characters) {
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

   void calculator_parse_to_forest(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.parse_to_forest", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_parse_to_forest<calculator::expression>(state, *selected_case->input,
                                                                  "calculator.parse_to_forest");
   }

   void calculator_materialize_ast(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.materialize_ast", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_materialize_ast<calculator::expression>(state, *selected_case->input,
                                                                  "calculator.materialize_ast");
   }

   void calculator_parse_and_evaluate(benchmark::State& state) {
      auto* selected_case = calculator_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "calculator.parse_and_evaluate", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_parse_and_consume<calculator::expression>(
            state, *selected_case->input, "calculator.parse_and_evaluate",
            [](const calculator::expression& node) { return calculator::visit(node, calculator_visitor{}); });
   }
} // namespace

BENCHMARK(calculator_parse_to_forest)
      ->Name("calculator/parse_to_forest")
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

BENCHMARK(calculator_materialize_ast)
      ->Name("calculator/materialize_ast")
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

BENCHMARK(calculator_parse_and_evaluate)
      ->Name("calculator/parse_and_evaluate")
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

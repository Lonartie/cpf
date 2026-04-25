#pragma once

#include <benchmark/benchmark.h>

#include <cpflib>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cpfbench {
   namespace detail {
      inline auto parse_error_message(const auto& result) -> std::string {
         if (result.error.has_value()) {
            return result.error->message;
         }
         return "unknown parse failure";
      }

      inline auto minimum(const std::vector<double>& samples) -> double {
         return *std::min_element(samples.begin(), samples.end());
      }

      inline auto maximum(const std::vector<double>& samples) -> double {
         return *std::max_element(samples.begin(), samples.end());
      }

      inline void report_parse_failure(benchmark::State& state, std::string_view benchmark_name,
                                       const std::string& message) {
         auto error = std::string{"Benchmark '"} + std::string{benchmark_name} + "' failed to parse input: " + message;
         state.SkipWithError(error);
      }

      inline void report_materialization_failure(benchmark::State& state, std::string_view benchmark_name,
                                                 std::string_view message) {
         auto error = std::string{"Benchmark '"} + std::string{benchmark_name} +
                      "' failed to materialize AST: " + std::string{message};
         state.SkipWithError(error);
      }

      inline void set_items_processed(benchmark::State& state) { state.SetItemsProcessed(state.iterations()); }
   } // namespace detail

   template<typename Root>
   void benchmark_parse(benchmark::State& state, std::string_view input, std::string_view benchmark_name) {
      state.SetComplexityN(static_cast<benchmark::ComplexityN>(input.size()));

      for (auto _: state) {
         auto result = Root::parse(input);
         if (!result.success || result.forest.empty()) {
            detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(result));
            return;
         }

         benchmark::DoNotOptimize(result.forest.size());
      }

      detail::set_items_processed(state);
   }

   template<typename Root>
   void benchmark_parse_to_forest(benchmark::State& state, std::string_view input, std::string_view benchmark_name) {
      state.SetComplexityN(static_cast<benchmark::ComplexityN>(input.size()));

      for (auto _: state) {
         auto result = Root::parse(input);
         if (!result.success || result.forest.empty()) {
            detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(result));
            return;
         }

         benchmark::DoNotOptimize(result.forest.size());
         benchmark::DoNotOptimize(result.forest.front().definition);
      }

      detail::set_items_processed(state);
   }

   template<typename Root>
   void benchmark_materialize_ast(benchmark::State& state, std::string_view input, std::string_view benchmark_name) {
      state.SetComplexityN(static_cast<benchmark::ComplexityN>(input.size()));

      auto result = Root::parse(input);
      if (!result.success || result.forest.empty()) {
         detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(result));
         return;
      }

      const auto tree_template = result.forest.front();
      if (tree_template.has_materialized()) {
         detail::report_materialization_failure(state, benchmark_name,
                                                "forest handle was already materialized before benchmarking");
         return;
      }

      for (auto _: state) {
         auto tree = tree_template;
         if (tree.has_materialized()) {
            detail::report_materialization_failure(state, benchmark_name, "forest handle was already materialized");
            return;
         }

         auto* materialized = tree.get();
         if (materialized == nullptr) {
            detail::report_materialization_failure(state, benchmark_name, "materializer returned null");
            return;
         }

         benchmark::DoNotOptimize(materialized);
      }

      detail::set_items_processed(state);
   }

   template<typename Root, typename Consumer>
   void benchmark_parse_and_consume(benchmark::State& state, std::string_view input, std::string_view benchmark_name,
                                    Consumer&& consume) {
      state.SetComplexityN(static_cast<benchmark::ComplexityN>(input.size()));

      for (auto _: state) {
         auto result = Root::parse(input);
         if (!result.success || result.forest.empty()) {
            detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(result));
            return;
         }

         auto consumed = consume(*result.forest.front());
         benchmark::DoNotOptimize(consumed);
      }

      detail::set_items_processed(state);
   }
} // namespace cpfbench

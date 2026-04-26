#pragma once

#include <benchmark/benchmark.h>

#include <cpflib>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cpfbench {
   namespace detail {
      inline constexpr auto heap_counter_name = "heap_bytes";

      inline auto parse_error_message(const auto& result) -> std::string {
         if (result.error.has_value()) {
            return result.error->message;
         }
         return "unknown parse failure";
      }

      inline auto minimum(const std::vector<double>& samples) -> double {
         if (samples.empty()) {
            return std::numeric_limits<double>::quiet_NaN();
         }
         return *std::min_element(samples.begin(), samples.end());
      }

      inline auto maximum(const std::vector<double>& samples) -> double {
         if (samples.empty()) {
            return std::numeric_limits<double>::quiet_NaN();
         }
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

      void reset_heap_footprint_baseline();
      [[nodiscard]] auto current_heap_footprint_bytes() -> std::size_t;

      inline void begin_heap_footprint_measurement() { reset_heap_footprint_baseline(); }

      inline auto end_heap_footprint_measurement() -> double {
         return static_cast<double>(current_heap_footprint_bytes());
      }

      inline void set_heap_footprint_bytes(benchmark::State& state, double heap_footprint_bytes) {
         state.counters[heap_counter_name] = heap_footprint_bytes;
      }

      inline void set_items_processed(benchmark::State& state) { state.SetItemsProcessed(state.iterations()); }
   } // namespace detail

   template<typename Root>
   void benchmark_parse(benchmark::State& state, std::string_view input, std::string_view benchmark_name) {
      state.SetComplexityN(static_cast<benchmark::ComplexityN>(input.size()));

      detail::begin_heap_footprint_measurement();
      {
         auto result = Root::parse(input);
         if (!result.success || result.forest.empty()) {
            detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(result));
            return;
         }

         detail::set_heap_footprint_bytes(state, detail::end_heap_footprint_measurement());
         benchmark::DoNotOptimize(result.forest.size());
      }

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

      detail::begin_heap_footprint_measurement();
      {
         auto result = Root::parse(input);
         if (!result.success || result.forest.empty()) {
            detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(result));
            return;
         }

         detail::set_heap_footprint_bytes(state, detail::end_heap_footprint_measurement());
         benchmark::DoNotOptimize(result.forest.size());
         benchmark::DoNotOptimize(result.forest.front().production_index());
      }

      for (auto _: state) {
         auto result = Root::parse(input);
         if (!result.success || result.forest.empty()) {
            detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(result));
            return;
         }

         benchmark::DoNotOptimize(result.forest.size());
         benchmark::DoNotOptimize(result.forest.front().production_index());
      }

      detail::set_items_processed(state);
   }

   template<typename Root>
   void benchmark_materialize_ast(benchmark::State& state, std::string_view input, std::string_view benchmark_name) {
      state.SetComplexityN(static_cast<benchmark::ComplexityN>(input.size()));

      detail::begin_heap_footprint_measurement();
      {
         auto footprint_result = Root::parse(input);
         if (!footprint_result.success || footprint_result.forest.empty()) {
            detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(footprint_result));
            return;
         }

         auto& footprint_tree = footprint_result.forest.front();
         auto* footprint_materialized = footprint_tree.get();
         if (footprint_materialized == nullptr) {
            detail::report_materialization_failure(state, benchmark_name, "materializer returned null");
            return;
         }

         detail::set_heap_footprint_bytes(state, detail::end_heap_footprint_measurement());
         benchmark::DoNotOptimize(footprint_materialized);
      }

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
         auto tree = tree_template.clone();
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

      detail::begin_heap_footprint_measurement();
      {
         auto result = Root::parse(input);
         if (!result.success || result.forest.empty()) {
            detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(result));
            return;
         }

         auto consumed = consume(*result.forest.front());
         detail::set_heap_footprint_bytes(state, detail::end_heap_footprint_measurement());
         benchmark::DoNotOptimize(consumed);
      }

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

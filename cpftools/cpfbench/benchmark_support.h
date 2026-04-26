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

   template<typename Lexer>
   void benchmark_lex_input(benchmark::State& state, std::string_view input, std::string_view benchmark_name,
                            Lexer&& lex) {
      state.SetComplexityN(static_cast<benchmark::ComplexityN>(input.size()));

      detail::begin_heap_footprint_measurement();
      {
         auto tokens = lex(input);
         detail::set_heap_footprint_bytes(state, detail::end_heap_footprint_measurement());
         benchmark::DoNotOptimize(tokens.size());
         benchmark::DoNotOptimize(tokens.input.size());
      }

      for (auto _: state) {
         auto tokens = lex(input);
         benchmark::DoNotOptimize(tokens.size());
         benchmark::DoNotOptimize(tokens.input.size());
      }

      detail::set_items_processed(state);
   }

   template<typename Parser>
   void benchmark_parse_tokens(benchmark::State& state, const cpf::token_sequence& tokens, std::string_view benchmark_name,
                               Parser&& parse_tokens) {
      state.SetComplexityN(static_cast<benchmark::ComplexityN>(tokens.input.size()));

      detail::begin_heap_footprint_measurement();
      {
         auto result = parse_tokens(tokens);
         if (!result.success || result.forest.empty()) {
            detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(result));
            return;
         }

         detail::set_heap_footprint_bytes(state, detail::end_heap_footprint_measurement());
         benchmark::DoNotOptimize(result.forest.size());
         benchmark::DoNotOptimize(result.forest.front().production_index());
      }

      for (auto _: state) {
         auto result = parse_tokens(tokens);
         if (!result.success || result.forest.empty()) {
            detail::report_parse_failure(state, benchmark_name, detail::parse_error_message(result));
            return;
         }

         benchmark::DoNotOptimize(result.forest.size());
         benchmark::DoNotOptimize(result.forest.front().production_index());
      }


      detail::set_items_processed(state);
   }

   template<typename TreeFactory>
   void benchmark_materialize_ast(benchmark::State& state, std::size_t input_size, std::string_view benchmark_name,
                                  TreeFactory&& make_tree) {
      state.SetComplexityN(static_cast<benchmark::ComplexityN>(input_size));

      detail::begin_heap_footprint_measurement();
      {
         auto tree = make_tree();
         auto* root = tree.get();
         if (root == nullptr) {
            detail::report_materialization_failure(state, benchmark_name, "empty parse tree");
            return;
         }

         detail::set_heap_footprint_bytes(state, detail::end_heap_footprint_measurement());
         benchmark::DoNotOptimize(root);
         benchmark::DoNotOptimize(tree.production_index());
      }

      for (auto _: state) {
         auto tree = make_tree();
         auto* root = tree.get();
         if (root == nullptr) {
            detail::report_materialization_failure(state, benchmark_name, "empty parse tree");
            return;
         }

         benchmark::DoNotOptimize(root);
         benchmark::DoNotOptimize(tree.production_index());
      }

      detail::set_items_processed(state);
   }

   template<typename Node, typename Interpreter>
   void benchmark_interpret_result(benchmark::State& state, const Node& root, std::size_t input_size,
                                   Interpreter&& interpret) {
      state.SetComplexityN(static_cast<benchmark::ComplexityN>(input_size));

      detail::begin_heap_footprint_measurement();
      {
         auto value = interpret(root);
         detail::set_heap_footprint_bytes(state, detail::end_heap_footprint_measurement());
         benchmark::DoNotOptimize(value);
      }

      for (auto _: state) {
         auto value = interpret(root);
         benchmark::DoNotOptimize(value);
      }

      detail::set_items_processed(state);
   }
} // namespace cpfbench

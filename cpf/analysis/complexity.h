#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace cpf {
   /// @brief Estimated time-complexity model derived from observed measurements.
   struct complexity {
      /// @brief Big-O family selected for the fitted model.
      std::string big_o;
      /// @brief Human-readable fitted expression whose result is measured in seconds.
      std::string expression;
      /// @brief Complete human-readable summary of the fitted complexity.
      std::string summary;
      /// @brief Estimates execution time in seconds for an input size.
      std::function<double(double)> estimator;
      /// @brief Relative root-mean-square fit error for the chosen model.
      double relative_root_mean_square_error = std::numeric_limits<double>::infinity();
      /// @brief Coefficients used by the fitted expression.
      std::vector<double> coefficients;
      /// @brief Observed argument sizes used for fitting.
      std::vector<double> arg_sizes;
      /// @brief Observed median timings in seconds used for fitting.
      std::vector<double> sample_times_s;

      /// @brief Evaluates the fitted model for a specific input size.
      /// @param input_size User-defined size metric of the input.
      /// @return Estimated runtime in seconds.
      [[nodiscard]] auto estimate(double input_size) const -> double;
   };

   auto operator<<(std::ostream& stream, const complexity& value) -> std::ostream&;

   namespace detail {
      inline constexpr auto complexity_trial_count = std::size_t{5};
      inline constexpr auto complexity_min_iterations = std::size_t{1};
      inline constexpr auto complexity_min_trial_duration = std::chrono::microseconds{250};
      inline constexpr auto complexity_max_terms_per_model = std::size_t{4};
      inline constexpr auto complexity_solver_epsilon = 1e-12;

      template<typename T> inline void do_not_optimize(T& value) {
         std::atomic_signal_fence(std::memory_order_seq_cst);
         auto* volatile sink = &value;
         (void) sink;
         std::atomic_signal_fence(std::memory_order_seq_cst);
      }

      template<typename T> inline void do_not_optimize(const T& value) {
         std::atomic_signal_fence(std::memory_order_seq_cst);
         auto const* volatile sink = &value;
         (void) sink;
         std::atomic_signal_fence(std::memory_order_seq_cst);
      }

      inline void clobber_memory() { std::atomic_signal_fence(std::memory_order_seq_cst); }

      inline auto median(std::vector<double> values) -> double {
         if (values.empty()) {
            return 0.0;
         }

         std::sort(values.begin(), values.end());
         auto middle = values.size() / 2;
         if ((values.size() % 2U) != 0U) {
            return values[middle];
         }
         return (values[middle - 1] + values[middle]) / 2.0;
      }

      auto analyze_complexity_samples(std::vector<double> arg_sizes, std::vector<double> sample_times_s)
            -> complexity;

      template<typename Func, typename Tuple> auto invoke_complexity_sample(Func& func, Tuple& arguments) -> void {
         using tuple_type = std::remove_reference_t<Tuple>;
         constexpr auto arity = std::tuple_size_v<tuple_type>;
         using result_type = decltype(std::apply(
               [&](auto&... unpacked) -> decltype(auto) { return std::invoke(func, unpacked...); }, arguments));

         if constexpr (std::is_void_v<result_type>) {
            std::apply([&](auto&... unpacked) { std::invoke(func, unpacked...); }, arguments);
            clobber_memory();
         } else {
            auto result = std::apply(
                  [&](auto&... unpacked) -> decltype(auto) { return std::invoke(func, unpacked...); }, arguments);
            do_not_optimize(result);
            clobber_memory();
         }

         (void) arity;
      }

      template<typename Func, typename Tuple> auto measure_complexity_sample_s(Func& func, Tuple& arguments) -> double {
         std::vector<double> trials;
         trials.reserve(complexity_trial_count);

         for (std::size_t trial = 0; trial < complexity_trial_count; ++trial) {
            auto iterations = std::size_t{0};
            auto start = std::chrono::steady_clock::now();
            auto end = start;
            do {
               invoke_complexity_sample(func, arguments);
               ++iterations;
               end = std::chrono::steady_clock::now();
            } while (iterations < complexity_min_iterations || (end - start) < complexity_min_trial_duration);

            auto elapsed = std::chrono::duration<double>{end - start}.count();
            trials.push_back(elapsed / static_cast<double>(iterations));
         }

         return median(std::move(trials));
      }
   } // namespace detail

   /// @brief Measures a callable on multiple input sizes and fits a practical time-complexity model.
   /// @tparam Func Callable type.
   /// @tparam Args Tuple element types matching the callable arguments.
   /// @param func Callable to execute.
   /// @param args Argument tuples to pass to the callable.
   /// @param arg_sizes User-defined sizes corresponding to each argument tuple.
   /// @return Fitted complexity model whose estimator returns seconds.
   template<typename Func, typename... Args>
   auto complexity_of(Func&& func, std::vector<std::tuple<Args...>> args, std::vector<double> arg_sizes) -> complexity {
      if (args.size() != arg_sizes.size()) {
         throw std::invalid_argument{
               "cpf::complexity_of requires the same number of argument tuples and argument sizes"};
      }

      auto callable = std::forward<Func>(func);
      std::vector<double> sample_times_s;
      sample_times_s.reserve(args.size());
      for (auto& argument_tuple: args) {
         sample_times_s.push_back(detail::measure_complexity_sample_s(callable, argument_tuple));
      }

      return detail::analyze_complexity_samples(std::move(arg_sizes), std::move(sample_times_s));
   }
} // namespace cpf

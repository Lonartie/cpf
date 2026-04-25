#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#   include <intrin.h>
#endif

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
      [[nodiscard]] auto estimate(double input_size) const -> double {
         return estimator ? estimator(input_size) : 0.0;
      }
   };

   inline auto operator<<(std::ostream& stream, const complexity& value) -> std::ostream& {
      stream << value.summary;
      return stream;
   }

   namespace detail {
      inline constexpr auto complexity_trial_count = std::size_t{5};
      inline constexpr auto complexity_min_iterations = std::size_t{1};
      inline constexpr auto complexity_min_trial_duration = std::chrono::microseconds{250};
      inline constexpr auto complexity_max_terms_per_model = std::size_t{4};
      inline constexpr auto complexity_solver_epsilon = 1e-12;

      inline auto sanitize_measurement_size(double value) -> double {
         return std::max(value, 0.0);
      }

      inline auto sanitize_log_size(double value) -> double {
         return std::max(sanitize_measurement_size(value), 1.0);
      }

      inline void validate_complexity_inputs(const std::vector<double>& arg_sizes, const std::vector<double>& sample_times_s) {
         if (arg_sizes.size() != sample_times_s.size()) {
            throw std::invalid_argument{"cpf::complexity_of requires the same number of argument tuples and argument sizes"};
         }

         if (arg_sizes.size() < 2) {
            throw std::invalid_argument{"cpf::complexity_of requires at least two samples"};
         }

         for (const auto size : arg_sizes) {
            if (!std::isfinite(size) || size < 0.0) {
               throw std::invalid_argument{"cpf::complexity_of requires finite, non-negative argument sizes"};
            }
         }

         for (const auto duration_s : sample_times_s) {
            if (!std::isfinite(duration_s) || duration_s < 0.0) {
               throw std::invalid_argument{"cpf::complexity_of requires finite, non-negative timings"};
            }
         }
      }

      inline auto positive_scale_denominator(double value) -> double {
         return std::max(std::abs(value), std::numeric_limits<double>::epsilon());
      }

#if defined(__clang__) || defined(__GNUC__)
      template<typename T>
      inline void do_not_optimize(T& value) {
         asm volatile("" : "+r,m"(value) : : "memory");
      }

      template<typename T>
      inline void do_not_optimize(const T& value) {
         asm volatile("" : : "g"(value) : "memory");
      }

      inline void clobber_memory() {
         asm volatile("" : : : "memory");
      }
#elif defined(_MSC_VER)
      template<typename T>
      inline void do_not_optimize(T& value) {
         auto* volatile sink = &value;
         (void)sink;
         _ReadWriteBarrier();
      }

      template<typename T>
      inline void do_not_optimize(const T& value) {
         auto const* volatile sink = &value;
         (void)sink;
         _ReadWriteBarrier();
      }

      inline void clobber_memory() {
         _ReadWriteBarrier();
      }
#else
      template<typename T>
      inline void do_not_optimize(T& value) {
         std::atomic_signal_fence(std::memory_order_seq_cst);
         auto* volatile sink = &value;
         (void)sink;
         std::atomic_signal_fence(std::memory_order_seq_cst);
      }

      template<typename T>
      inline void do_not_optimize(const T& value) {
         std::atomic_signal_fence(std::memory_order_seq_cst);
         auto const* volatile sink = &value;
         (void)sink;
         std::atomic_signal_fence(std::memory_order_seq_cst);
      }

      inline void clobber_memory() {
         std::atomic_signal_fence(std::memory_order_seq_cst);
      }
#endif

      inline auto basis_constant(double) -> double {
         return 1.0;
      }

      inline auto basis_log_n(double value) -> double {
         return std::log(sanitize_log_size(value));
      }

      inline auto basis_sqrt_n(double value) -> double {
         return std::sqrt(sanitize_measurement_size(value));
      }

      inline auto basis_sqrt_n_log_n(double value) -> double {
         return basis_sqrt_n(value) * basis_log_n(value);
      }

      inline auto basis_n(double value) -> double {
         return sanitize_measurement_size(value);
      }

      inline auto basis_n_log_n(double value) -> double {
         auto size = sanitize_measurement_size(value);
         return size * std::log(sanitize_log_size(value));
      }

      inline auto basis_n_to_three_halves(double value) -> double {
         auto size = sanitize_measurement_size(value);
         return size * std::sqrt(size);
      }

      inline auto basis_n_squared(double value) -> double {
         auto size = sanitize_measurement_size(value);
         return size * size;
      }

      inline auto basis_n_squared_log_n(double value) -> double {
         auto size = sanitize_measurement_size(value);
         return size * size * basis_log_n(value);
      }

      inline auto basis_n_cubed(double value) -> double {
         auto size = sanitize_measurement_size(value);
         return size * size * size;
      }

      inline auto capped_exponential(double exponent) -> double {
         constexpr auto max_exponent = 709.0;
         if (exponent <= 0.0) {
            return 1.0;
         }
         return std::exp(std::min(exponent, max_exponent));
      }

      inline auto basis_two_to_n(double value) -> double {
         return capped_exponential(sanitize_measurement_size(value) * std::log(2.0));
      }

      inline auto basis_factorial_n(double value) -> double {
         return capped_exponential(std::lgamma(sanitize_measurement_size(value) + 1.0));
      }

      struct basis_term {
         std::string_view label;
         double (*evaluate)(double) = nullptr;
      };

      struct complexity_model {
         std::string_view big_o;
         std::size_t dominant_rank = 0;
         std::vector<double (*)(double)> basis_functions;
         std::vector<std::string_view> basis_labels;
      };

      struct dominant_complexity_family {
         std::string_view big_o;
         std::size_t dominant_rank = 0;
      };

      inline auto basis_terms() -> const std::vector<basis_term>& {
         static const auto terms = std::vector<basis_term>{
            basis_term{"1", &basis_constant},
            basis_term{"log(N)", &basis_log_n},
            basis_term{"sqrt(N)", &basis_sqrt_n},
            basis_term{"sqrt(N)*log(N)", &basis_sqrt_n_log_n},
            basis_term{"N", &basis_n},
            basis_term{"N*log(N)", &basis_n_log_n},
            basis_term{"N^(3/2)", &basis_n_to_three_halves},
            basis_term{"N^2", &basis_n_squared},
            basis_term{"N^2*log(N)", &basis_n_squared_log_n},
            basis_term{"N^3", &basis_n_cubed},
            basis_term{"2^N", &basis_two_to_n},
            basis_term{"N!", &basis_factorial_n},
         };
         return terms;
      }

      inline auto dominant_complexity_families() -> const std::vector<dominant_complexity_family>& {
         static const auto families = std::vector<dominant_complexity_family>{
            dominant_complexity_family{"O(1)", 0},
            dominant_complexity_family{"O(log N)", 1},
            dominant_complexity_family{"O(sqrt N)", 2},
            dominant_complexity_family{"O(sqrt N log N)", 3},
            dominant_complexity_family{"O(N)", 4},
            dominant_complexity_family{"O(N log N)", 5},
            dominant_complexity_family{"O(N^(3/2))", 6},
            dominant_complexity_family{"O(N^2)", 7},
            dominant_complexity_family{"O(N^2 log N)", 8},
            dominant_complexity_family{"O(N^3)", 9},
            dominant_complexity_family{"O(2^N)", 10},
            dominant_complexity_family{"O(N!)", 11},
         };
         return families;
      }

      inline void append_complexity_model(
         std::vector<complexity_model>& models,
         const dominant_complexity_family& family,
         const std::vector<std::size_t>& selected_lower_ranks) {
         complexity_model model;
         model.big_o = family.big_o;
         model.dominant_rank = family.dominant_rank;
         const auto& terms = basis_terms();
         model.basis_functions.push_back(terms[family.dominant_rank].evaluate);
         model.basis_labels.push_back(terms[family.dominant_rank].label);

         auto ordered_lower_ranks = selected_lower_ranks;
         std::sort(ordered_lower_ranks.begin(), ordered_lower_ranks.end(), std::greater<>{});
         for (const auto lower_rank : ordered_lower_ranks) {
            model.basis_functions.push_back(terms[lower_rank].evaluate);
            model.basis_labels.push_back(terms[lower_rank].label);
         }
         models.push_back(std::move(model));
      }

      inline void append_complexity_model_combinations(
         std::vector<complexity_model>& models,
         const dominant_complexity_family& family,
         std::size_t next_lower_rank,
         std::vector<std::size_t>& selected_lower_ranks,
         std::size_t remaining_lower_terms) {
         if (remaining_lower_terms == 0 || next_lower_rank >= family.dominant_rank) {
            return;
         }

         for (std::size_t lower_rank = next_lower_rank; lower_rank < family.dominant_rank; ++lower_rank) {
            selected_lower_ranks.push_back(lower_rank);
            append_complexity_model(models, family, selected_lower_ranks);
            append_complexity_model_combinations(models, family, lower_rank + 1, selected_lower_ranks, remaining_lower_terms - 1);
            selected_lower_ranks.pop_back();
         }
      }

      inline auto complexity_models() -> const std::vector<complexity_model>& {
         static const auto models = [] {
            std::vector<complexity_model> generated_models;
            generated_models.reserve(512);
            for (const auto& family : dominant_complexity_families()) {
               std::vector<std::size_t> selected_lower_ranks;
               append_complexity_model(generated_models, family, selected_lower_ranks);
               append_complexity_model_combinations(
                  generated_models,
                  family,
                  0,
                  selected_lower_ranks,
                  complexity_max_terms_per_model - 1);
            }
            return generated_models;
         }();
         return models;
      }

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

      inline auto format_scalar(double value) -> std::string {
         std::ostringstream stream;
         stream << std::setprecision(6) << value;
         return stream.str();
      }

      inline auto format_percentage(double value) -> std::string {
         std::ostringstream stream;
         stream << std::fixed << std::setprecision(2) << (value * 100.0);
         return stream.str();
      }

      inline auto clamp_non_negative_finite(long double value) -> double {
         if (!std::isfinite(value) || value <= 0.0L) {
            return value > 0.0L ? std::numeric_limits<double>::max() : 0.0;
         }

         auto max_value = static_cast<long double>(std::numeric_limits<double>::max());
         if (value >= max_value) {
            return std::numeric_limits<double>::max();
         }
         return static_cast<double>(value);
      }

      inline auto evaluate_model(
         const complexity_model& model,
         const std::vector<double>& coefficients,
         double input_size) -> double {
         auto estimate = 0.0L;
         for (std::size_t index = 0; index < coefficients.size(); ++index) {
            auto basis_value = model.basis_functions[index](input_size);
            if (!std::isfinite(basis_value)) {
               return std::numeric_limits<double>::max();
            }
            estimate += static_cast<long double>(coefficients[index]) * static_cast<long double>(basis_value);
         }
         return clamp_non_negative_finite(estimate);
      }

      inline auto render_expression(const complexity_model& model, const std::vector<double>& coefficients) -> std::string {
         std::ostringstream stream;
         auto emitted_term = false;
         for (std::size_t index = 0; index < coefficients.size(); ++index) {
            auto coefficient = coefficients[index];
            if (std::abs(coefficient) < 1e-9) {
               continue;
            }

            if (emitted_term) {
               stream << (coefficient >= 0.0 ? " + " : " - ");
            } else if (coefficient < 0.0) {
               stream << '-';
            }

            auto absolute = std::abs(coefficient);
            if (model.basis_labels[index] == std::string_view{"1"}) {
               stream << format_scalar(absolute);
            } else {
               stream << format_scalar(absolute) << " * " << model.basis_labels[index];
            }
            emitted_term = true;
         }

         if (!emitted_term) {
            return "0";
         }

         return stream.str();
      }

      inline auto solve_linear_system(
         std::vector<std::vector<double>> matrix,
         std::vector<double> right_hand_side) -> std::optional<std::vector<double>> {
         auto dimension = matrix.size();
         for (std::size_t row = 0; row < dimension; ++row) {
            matrix[row].push_back(right_hand_side[row]);
         }

         for (std::size_t pivot = 0; pivot < dimension; ++pivot) {
            auto best_row = pivot;
            auto best_value = std::abs(matrix[pivot][pivot]);
            for (std::size_t candidate = pivot + 1; candidate < dimension; ++candidate) {
               auto candidate_value = std::abs(matrix[candidate][pivot]);
               if (candidate_value > best_value) {
                  best_row = candidate;
                  best_value = candidate_value;
               }
            }

            if (best_value <= complexity_solver_epsilon) {
               return std::nullopt;
            }

            if (best_row != pivot) {
               std::swap(matrix[pivot], matrix[best_row]);
            }

            auto divisor = matrix[pivot][pivot];
            for (std::size_t column = pivot; column <= dimension; ++column) {
               matrix[pivot][column] /= divisor;
            }

            for (std::size_t row = 0; row < dimension; ++row) {
               if (row == pivot) {
                  continue;
               }

               auto factor = matrix[row][pivot];
               if (std::abs(factor) <= complexity_solver_epsilon) {
                  continue;
               }
               for (std::size_t column = pivot; column <= dimension; ++column) {
                  matrix[row][column] -= factor * matrix[pivot][column];
               }
            }
         }

         std::vector<double> solution(dimension);
         for (std::size_t row = 0; row < dimension; ++row) {
            solution[row] = matrix[row][dimension];
         }
         return solution;
      }

      struct complexity_fit {
         const complexity_model* model = nullptr;
         std::vector<double> coefficients;
         double relative_root_mean_square_error = std::numeric_limits<double>::infinity();
         double score = std::numeric_limits<double>::infinity();
      };

      inline auto fit_complexity_model(
         const complexity_model& model,
         const std::vector<double>& arg_sizes,
         const std::vector<double>& sample_times_s) -> std::optional<complexity_fit> {
         auto parameter_count = model.basis_functions.size();
         if (arg_sizes.size() < parameter_count) {
            return std::nullopt;
         }

         std::vector<std::vector<double>> scaled_design(arg_sizes.size(), std::vector<double>(parameter_count, 0.0));
         std::vector<double> column_scales(parameter_count, 1.0);
         for (std::size_t sample_index = 0; sample_index < arg_sizes.size(); ++sample_index) {
            for (std::size_t parameter_index = 0; parameter_index < parameter_count; ++parameter_index) {
               auto basis_value = model.basis_functions[parameter_index](arg_sizes[sample_index]);
               if (!std::isfinite(basis_value)) {
                  return std::nullopt;
               }
               scaled_design[sample_index][parameter_index] = basis_value;
               column_scales[parameter_index] = std::max(column_scales[parameter_index], std::abs(basis_value));
            }
         }

         for (std::size_t sample_index = 0; sample_index < scaled_design.size(); ++sample_index) {
            for (std::size_t parameter_index = 0; parameter_index < parameter_count; ++parameter_index) {
               scaled_design[sample_index][parameter_index] /= column_scales[parameter_index];
            }
         }

         std::vector<std::vector<double>> normal_matrix(parameter_count, std::vector<double>(parameter_count, 0.0));
         std::vector<double> normal_vector(parameter_count, 0.0);

         for (std::size_t sample_index = 0; sample_index < arg_sizes.size(); ++sample_index) {
            for (std::size_t row = 0; row < parameter_count; ++row) {
               normal_vector[row] += scaled_design[sample_index][row] * sample_times_s[sample_index];
               for (std::size_t column = 0; column < parameter_count; ++column) {
                  normal_matrix[row][column] += scaled_design[sample_index][row] * scaled_design[sample_index][column];
               }
            }
         }

         auto scaled_coefficients = solve_linear_system(normal_matrix, normal_vector);
         if (!scaled_coefficients.has_value()) {
            return std::nullopt;
         }

         std::vector<double> coefficients(parameter_count, 0.0);
         for (std::size_t parameter_index = 0; parameter_index < parameter_count; ++parameter_index) {
            coefficients[parameter_index] = (*scaled_coefficients)[parameter_index] / column_scales[parameter_index];
            if (!std::isfinite(coefficients[parameter_index])) {
               return std::nullopt;
            }
         }

         auto squared_error = 0.0;
         auto mean_time = std::accumulate(sample_times_s.begin(), sample_times_s.end(), 0.0) / static_cast<double>(sample_times_s.size());
         auto valid_prediction = true;
         for (std::size_t sample_index = 0; sample_index < arg_sizes.size(); ++sample_index) {
            auto predicted = evaluate_model(model, coefficients, arg_sizes[sample_index]);
            if (!std::isfinite(predicted)) {
               valid_prediction = false;
               break;
            }
            auto residual = predicted - sample_times_s[sample_index];
            squared_error += residual * residual;
         }

         if (!valid_prediction) {
            return std::nullopt;
         }

         auto relative_root_mean_square_error = std::sqrt(squared_error / static_cast<double>(sample_times_s.size()))
            / positive_scale_denominator(mean_time);

         auto largest_input = *std::max_element(arg_sizes.begin(), arg_sizes.end());
         auto fitted_at_largest_input = evaluate_model(model, coefficients, largest_input);
         auto dominant_component = coefficients.front() * model.basis_functions.front()(largest_input);
         auto dominant_share = std::abs(dominant_component) / positive_scale_denominator(fitted_at_largest_input);

         auto score = (relative_root_mean_square_error * 5.0)
            + 0.004 * static_cast<double>(parameter_count - 1U)
            + 0.0005 * static_cast<double>(model.dominant_rank);
         if (coefficients.front() < 0.0) {
            score += 0.25;
         }
         if (dominant_share < 0.05) {
            score += (0.05 - dominant_share) * 2.0;
         }

         complexity_fit fit;
         fit.model = &model;
         fit.coefficients = std::move(coefficients);
         fit.relative_root_mean_square_error = relative_root_mean_square_error;
         fit.score = score;
         return fit;
      }

      inline auto analyze_complexity_samples(
         std::vector<double> arg_sizes,
         std::vector<double> sample_times_s) -> complexity {
         validate_complexity_inputs(arg_sizes, sample_times_s);

         std::optional<complexity_fit> best_fit;
         for (const auto& model : complexity_models()) {
            auto fit = fit_complexity_model(model, arg_sizes, sample_times_s);
            if (!fit.has_value()) {
               continue;
            }

            if (!best_fit.has_value()
             || fit->score < best_fit->score
             || (std::abs(fit->score - best_fit->score) <= 1e-9
              && fit->coefficients.size() < best_fit->coefficients.size())) {
               best_fit = std::move(fit);
            }
         }

         if (!best_fit.has_value() || best_fit->model == nullptr) {
            throw std::runtime_error{"cpf::complexity_of failed to fit any supported complexity model"};
         }

         complexity result;
         result.big_o = std::string{best_fit->model->big_o};
         result.expression = render_expression(*best_fit->model, best_fit->coefficients);
         result.summary = result.big_o + ": time(s) ~= " + result.expression
            + " (relative RMSE " + format_percentage(best_fit->relative_root_mean_square_error) + "%)";
         result.relative_root_mean_square_error = best_fit->relative_root_mean_square_error;
         result.coefficients = best_fit->coefficients;
         result.arg_sizes = std::move(arg_sizes);
         result.sample_times_s = std::move(sample_times_s);

         auto model = *best_fit->model;
         auto coefficients = result.coefficients;
         result.estimator = [model = std::move(model), coefficients = std::move(coefficients)](double input_size) {
            return evaluate_model(model, coefficients, input_size);
         };
         return result;
      }

      template<typename Func, typename Tuple>
      auto invoke_complexity_sample(Func& func, Tuple& arguments) -> void {
         using tuple_type = std::remove_reference_t<Tuple>;
         constexpr auto arity = std::tuple_size_v<tuple_type>;
         using result_type = decltype(std::apply(
            [&](auto&... unpacked) -> decltype(auto) {
               return std::invoke(func, unpacked...);
            },
            arguments));

         if constexpr (std::is_void_v<result_type>) {
            std::apply(
               [&](auto&... unpacked) {
                  std::invoke(func, unpacked...);
               },
               arguments);
            clobber_memory();
         } else {
            auto result = std::apply(
               [&](auto&... unpacked) -> decltype(auto) {
                  return std::invoke(func, unpacked...);
               },
               arguments);
            do_not_optimize(result);
            clobber_memory();
         }

         (void)arity;
      }

      template<typename Func, typename Tuple>
      auto measure_complexity_sample_s(Func& func, Tuple& arguments) -> double {
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
   auto complexity_of(
      Func&& func,
      std::vector<std::tuple<Args...>> args,
      std::vector<double> arg_sizes) -> complexity {
      if (args.size() != arg_sizes.size()) {
         throw std::invalid_argument{"cpf::complexity_of requires the same number of argument tuples and argument sizes"};
      }

      auto callable = std::forward<Func>(func);
      std::vector<double> sample_times_s;
      sample_times_s.reserve(args.size());
      for (auto& argument_tuple : args) {
         sample_times_s.push_back(detail::measure_complexity_sample_s(callable, argument_tuple));
      }

      return detail::analyze_complexity_samples(std::move(arg_sizes), std::move(sample_times_s));
   }
} // namespace cpf


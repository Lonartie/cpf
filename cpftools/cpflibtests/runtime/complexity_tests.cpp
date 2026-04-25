#include <cpflib>

#include "support/doctest.h"

#include <cmath>
#include <string>
#include <tuple>
#include <vector>

TEST_SUITE("cpflib.complexity") {
   TEST_CASE("synthetic linear samples fit an O(N) model with practical coefficients") {
      auto result =
            cpf::detail::analyze_complexity_samples({1.0, 2.0, 4.0, 8.0, 16.0}, {15.0, 25.0, 45.0, 85.0, 165.0});

      CHECK(result.big_o == "O(N)");
      CHECK(result.summary.find("O(N)") != std::string::npos);
      CHECK(result.expression.find("N") != std::string::npos);
      CHECK(result.relative_root_mean_square_error < 0.02);
      CHECK(result.estimate(32.0) > result.estimate(16.0));
   }

   TEST_CASE("synthetic linearithmic samples fit an O(N log N) model") {
      std::vector<double> sizes{2.0, 4.0, 8.0, 16.0, 32.0, 64.0};
      std::vector<double> samples;
      samples.reserve(sizes.size());
      for (const auto size: sizes) {
         samples.push_back((5.0e-7 * size * std::log(size)) + (2.0e-7 * size) + 7.0e-8);
      }

      auto result = cpf::detail::analyze_complexity_samples(sizes, samples);

      CHECK(result.big_o == "O(N log N)");
      CHECK(result.summary.find("time(s) ~= ") != std::string::npos);
      CHECK(result.expression.find("N*log(N)") != std::string::npos);
      CHECK(result.estimate(128.0) > result.estimate(64.0));
      CHECK(result.relative_root_mean_square_error < 0.02);
   }

   TEST_CASE("synthetic square-root samples fit an O(sqrt N) model") {
      std::vector<double> sizes{1.0, 4.0, 9.0, 16.0, 25.0, 36.0};
      std::vector<double> samples;
      samples.reserve(sizes.size());
      for (const auto size: sizes) {
         samples.push_back((11.0 * std::sqrt(size)) + (1.5 * std::log(std::max(size, 1.0))) + 3.0);
      }

      auto result = cpf::detail::analyze_complexity_samples(sizes, samples);

      CHECK(result.big_o == "O(sqrt N)");
      CHECK(result.expression.find("sqrt(N)") != std::string::npos);
      CHECK(result.estimate(49.0) > result.estimate(36.0));
   }

   TEST_CASE("synthetic quadratic samples fit an O(N^2) model") {
      auto result = cpf::detail::analyze_complexity_samples({1.0, 2.0, 3.0, 4.0, 5.0}, {12.0, 23.0, 40.0, 63.0, 92.0});

      CHECK(result.big_o == "O(N^2)");
      CHECK(result.expression.find("N^2") != std::string::npos);
      CHECK(result.relative_root_mean_square_error < 0.02);
      CHECK(result.estimate(6.0) > result.estimate(5.0));
   }

   TEST_CASE("synthetic quadratic-logarithmic samples fit an O(N^2 log N) model") {
      std::vector<double> sizes{2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0};
      std::vector<double> samples;
      samples.reserve(sizes.size());
      for (const auto size: sizes) {
         samples.push_back((2.5 * size * size * std::log(size)) + (3.0 * size * size) + (4.0 * std::sqrt(size)) + 9.0);
      }

      auto result = cpf::detail::analyze_complexity_samples(sizes, samples);

      CHECK(result.big_o == "O(N^2 log N)");
      CHECK(result.expression.find("N^2*log(N)") != std::string::npos);
      CHECK(result.estimate(20.0) > result.estimate(16.0));
   }

   TEST_CASE("synthetic exponential samples fit an O(2^N) model") {
      std::vector<double> sizes{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
      std::vector<double> samples;
      samples.reserve(sizes.size());
      for (const auto size: sizes) {
         samples.push_back((3.0 * std::exp2(size)) + (5.0 * size * size) + 11.0);
      }

      auto result = cpf::detail::analyze_complexity_samples(sizes, samples);

      CHECK(result.big_o == "O(2^N)");
      CHECK(result.expression.find("2^N") != std::string::npos);
      CHECK(result.estimate(9.0) > result.estimate(8.0));
      CHECK(std::isfinite(result.estimate(128.0)) != 0);
   }

   TEST_CASE("synthetic factorial samples fit an O(N!) model") {
      std::vector<double> sizes{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
      std::vector<double> samples;
      samples.reserve(sizes.size());
      for (const auto size: sizes) {
         samples.push_back((0.25 * std::tgamma(size + 1.0)) + (4.0 * std::exp2(size)) + 13.0);
      }

      auto result = cpf::detail::analyze_complexity_samples(sizes, samples);

      CHECK(result.big_o == "O(N!)");
      CHECK(result.expression.find("N!") != std::string::npos);
      CHECK(result.estimate(9.0) > result.estimate(8.0));
      CHECK(std::isfinite(result.estimate(256.0)) != 0);
   }

   TEST_CASE("complexity_of validates the provided input vectors") {
      auto noop = [](int value) { return value; };

      CHECK_THROWS_AS(cpf::complexity_of(noop, std::vector<std::tuple<int>>{{1}, {2}}, std::vector<double>{1.0}),
                      std::invalid_argument);

      CHECK_THROWS_AS(
            cpf::detail::analyze_complexity_samples(std::vector<double>{1.0, -1.0}, std::vector<double>{10.0, 20.0}),
            std::invalid_argument);
   }

   TEST_CASE("complexity_of measures callable runtime and returns a usable estimator") {
      auto linear_work = [](std::size_t size) {
         volatile std::size_t total = 0;
         for (std::size_t index = 0; index < size * 2000; ++index) {
            total += index & 7U;
         }
         return total;
      };

      auto result = cpf::complexity_of(linear_work, std::vector<std::tuple<std::size_t>>{{32}, {64}, {128}, {256}},
                                       std::vector<double>{32.0, 64.0, 128.0, 256.0});

      CHECK_FALSE(result.summary.empty());
      CHECK(result.summary.find("O(") != std::string::npos);
      CHECK(result.estimate(512.0) > result.estimate(64.0));
      CHECK(result.sample_times_s.size() == 4);
      CHECK(result.arg_sizes.size() == 4);
      CHECK(result.estimate(256.0) > 0.0);
   }
}

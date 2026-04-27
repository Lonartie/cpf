#include "yacc_driver.h"

#include "c_subset.h"

#include <cpflib>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
   namespace c_subset = cpfyacc::c_subset;
   constexpr auto ParserColumnWidth = 14;

   struct cli_options {
      std::size_t iterations = 50;
      bool verify_only = false;
      bool show_components = false;
   };

   struct parser_measurement {
      std::string name;
      std::size_t iterations = 0;
      double total_milliseconds = 0.0;
      double average_milliseconds = 0.0;
      double minimum_milliseconds = 0.0;
      double maximum_milliseconds = 0.0;
   };

   [[nodiscard]] auto parse_arguments(int argc, char** argv) -> cli_options {
      auto options = cli_options{};
      for (auto index = 1; index < argc; ++index) {
         auto argument = std::string_view{argv[index]};
         if (argument == "--verify-only") {
            options.verify_only = true;
            continue;
         }
         if (argument == "--show-components") {
            options.show_components = true;
            continue;
         }
         if (argument.rfind("--iterations=", 0) == 0) {
            options.iterations = static_cast<std::size_t>(std::stoull(std::string{argument.substr(13)}));
            continue;
         }
         throw std::runtime_error{"Unknown argument: " + std::string{argument}};
      }
      if (options.iterations == 0) {
         throw std::runtime_error{"--iterations must be at least 1"};
      }
      return options;
   }

   [[nodiscard]] auto read_text_file(const std::filesystem::path& path) -> std::string {
      auto input = std::ifstream{path};
      if (!input.is_open()) {
         throw std::runtime_error{"Failed to open fixture: " + path.string()};
      }
      return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
   }

   [[nodiscard]] auto line_count_of(std::string_view text) -> std::size_t {
      if (text.empty()) {
         return 0;
      }
      return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
   }

   void ensure_yacc_accepts(std::string_view input) {
      auto result = yacc_parse_source(input.data(), input.size());
      if (!result.success) {
         throw std::runtime_error{"yacc parse failed at line " + std::to_string(result.line) + ", column " +
                                  std::to_string(result.column) + ": " + result.message};
      }
   }

   void ensure_cpf_accepts(std::string_view input) {
      auto options = cpf::parse_options{};
      options.build_ast = false;
      auto result = c_subset::translation_unit::parse(input, options);
      if (!result.success) {
         auto message = result.error.has_value() ? result.error->message : std::string{"unknown CPF parse failure"};
         throw std::runtime_error{"CPF parse failed: " + message};
      }
   }

   void ensure_cpf_accepts(const cpf::token_sequence& tokens) {
      auto options = cpf::parse_options{};
      options.build_ast = false;
      auto result = c_subset::translation_unit::parse(tokens, options);
      if (!result.success) {
         auto message = result.error.has_value() ? result.error->message : std::string{"unknown CPF parse failure"};
         throw std::runtime_error{"CPF token parse failed: " + message};
      }
   }

   void ensure_cpf_dynamic_accepts(const cpf::compiled_grammar& grammar, std::string_view input) {
      auto options = cpf::parse_options{};
      options.build_ast = false;
      auto result = grammar.parse(input, options);
      if (!result.success) {
         auto message = result.error.has_value() ? result.error->message : std::string{"unknown dynamic CPF parse failure"};
         throw std::runtime_error{"dynamic CPF parse failed: " + message};
      }
   }

   void ensure_cpf_dynamic_accepts(const cpf::compiled_grammar& grammar, const cpf::token_sequence& tokens) {
      auto options = cpf::parse_options{};
      options.build_ast = false;
      auto result = grammar.parse(tokens, options);
      if (!result.success) {
         auto message = result.error.has_value() ? result.error->message : std::string{"unknown dynamic CPF token parse failure"};
         throw std::runtime_error{"dynamic CPF token parse failed: " + message};
      }
   }

   template<typename Parser>
   [[nodiscard]] auto measure_parser(std::string name, std::size_t iterations, Parser&& parse_once)
         -> parser_measurement {
      parse_once();

      auto samples = std::vector<double>{};
      samples.reserve(iterations);
      for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
         const auto started_at = std::chrono::steady_clock::now();
         parse_once();
         const auto finished_at = std::chrono::steady_clock::now();
         const auto elapsed = std::chrono::duration<double, std::milli>{finished_at - started_at}.count();
         samples.push_back(elapsed);
      }

      const auto total = std::accumulate(samples.begin(), samples.end(), 0.0);
      const auto minimum = *std::min_element(samples.begin(), samples.end());
      const auto maximum = *std::max_element(samples.begin(), samples.end());
      auto measurement = parser_measurement{};
      measurement.name = std::move(name);
      measurement.iterations = iterations;
      measurement.total_milliseconds = total;
      measurement.average_milliseconds = total / static_cast<double>(iterations);
      measurement.minimum_milliseconds = minimum;
      measurement.maximum_milliseconds = maximum;
      return measurement;
   }

   void print_measurement(const parser_measurement& measurement) {
      std::cout << std::left << std::setw(ParserColumnWidth) << measurement.name << std::right << std::setw(12)
                << measurement.iterations << std::setw(14) << std::fixed << std::setprecision(3)
                << measurement.total_milliseconds << std::setw(14) << measurement.average_milliseconds << std::setw(14)
                << measurement.minimum_milliseconds << std::setw(14) << measurement.maximum_milliseconds << '\n';
   }
} // namespace

int main(int argc, char** argv) {
   try {
      const auto options = parse_arguments(argc, argv);
      const auto fixture_path = std::filesystem::path{CPF_YACC_COMPARE_FIXTURE_PATH};
      const auto grammar_path = std::filesystem::path{CPF_YACC_COMPARE_GRAMMAR_PATH};
      const auto input = read_text_file(fixture_path);
      const auto dynamic_compile = measure_parser("cpf dyn init", 1,
                                                  [&grammar_path] { (void) cpf::compile_grammar_file(grammar_path); });
      const auto dynamic_grammar = cpf::compile_grammar_file(grammar_path);
      const auto cpf_generated_tokens = c_subset::translation_unit::lex(input);
      const auto cpf_dynamic_tokens = dynamic_grammar.lex(input);

      std::cout << "Fixture:   " << fixture_path << '\n';
      std::cout << "Bytes:     " << input.size() << '\n';
      std::cout << "Lines:     " << line_count_of(input) << '\n';

      ensure_yacc_accepts(input);
      ensure_cpf_accepts(input);
      ensure_cpf_dynamic_accepts(dynamic_grammar, input);
      std::cout << "Verified:  yacc, generated CPF, and dynamic CPF all accept the fixture" << '\n';

      if (options.verify_only) {
         return 0;
      }

      const auto yacc = measure_parser("yacc", options.iterations,
                                       [&input] { ensure_yacc_accepts(input); });
      const auto cpf_generated = measure_parser("cpf gen", options.iterations,
                                                [&input] { ensure_cpf_accepts(input); });
      const auto cpf_dynamic = measure_parser("cpf dyn", options.iterations,
                                              [&dynamic_grammar, &input] { ensure_cpf_dynamic_accepts(dynamic_grammar, input); });

      std::cout << '\n';
      std::cout << std::left << std::setw(ParserColumnWidth) << "Parser" << std::right << std::setw(12) << "Iterations"
                << std::setw(14) << "Total ms" << std::setw(14) << "Avg ms" << std::setw(14) << "Min ms"
                << std::setw(14) << "Max ms" << '\n';
      std::cout << std::string(ParserColumnWidth + 54, '-') << '\n';
      print_measurement(yacc);
      print_measurement(cpf_generated);
      print_measurement(cpf_dynamic);
      std::cout << std::string(ParserColumnWidth + 54, '-') << '\n';

      if (yacc.average_milliseconds > 0.0) {
         std::cout << "generated CPF / yacc average runtime ratio: " << std::fixed << std::setprecision(2)
                   << (cpf_generated.average_milliseconds / yacc.average_milliseconds) << "x" << '\n';
         std::cout << "dynamic CPF / yacc average runtime ratio:   " << std::fixed << std::setprecision(2)
                   << (cpf_dynamic.average_milliseconds / yacc.average_milliseconds) << "x" << '\n';
      }
      if (cpf_generated.average_milliseconds > 0.0) {
         std::cout << "yacc / generated CPF average runtime ratio: " << std::fixed << std::setprecision(2)
                   << (yacc.average_milliseconds / cpf_generated.average_milliseconds) << "x" << '\n';
      }
      if (cpf_dynamic.average_milliseconds > 0.0) {
         std::cout << "yacc / dynamic CPF average runtime ratio:   " << std::fixed << std::setprecision(2)
                   << (yacc.average_milliseconds / cpf_dynamic.average_milliseconds) << "x" << '\n';
      }

      if (options.show_components) {
         const auto cpf_generated_lex = measure_parser("cpf gen lex", options.iterations,
                                             [&input] { (void) c_subset::translation_unit::lex(input); });
         const auto cpf_generated_parse_tokens = measure_parser("cpf gen parse", options.iterations,
                                                                [&cpf_generated_tokens] { ensure_cpf_accepts(cpf_generated_tokens); });
         const auto cpf_dynamic_lex = measure_parser("cpf dyn lex", options.iterations,
                                                     [&dynamic_grammar, &input] { (void) dynamic_grammar.lex(input); });
         const auto cpf_dynamic_parse_tokens = measure_parser("cpf dyn parse", options.iterations,
                                                              [&dynamic_grammar, &cpf_dynamic_tokens] {
                                                                 ensure_cpf_dynamic_accepts(dynamic_grammar, cpf_dynamic_tokens);
                                                              });

         std::cout << '\n';
         std::cout << "CPF component breakdown" << '\n';
         std::cout << std::left << std::setw(ParserColumnWidth) << "Parser" << std::right << std::setw(12) << "Iterations"
                   << std::setw(14) << "Total ms" << std::setw(14) << "Avg ms" << std::setw(14) << "Min ms"
                   << std::setw(14) << "Max ms" << '\n';
         std::cout << std::string(ParserColumnWidth + 54, '-') << '\n';
         print_measurement(dynamic_compile);
         print_measurement(cpf_generated_lex);
         print_measurement(cpf_generated_parse_tokens);
         print_measurement(cpf_dynamic_lex);
         print_measurement(cpf_dynamic_parse_tokens);
         std::cout << std::string(ParserColumnWidth + 54, '-') << '\n';
      }

      return 0;
   } catch (const std::exception& exception) {
      std::cerr << "yacc_compare failed: " << exception.what() << '\n';
      return 1;
   }
}




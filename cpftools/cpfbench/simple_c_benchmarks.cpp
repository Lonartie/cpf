#include "benchmark_support.h"

#include "simple_c.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

namespace {
   namespace simple_c = cpfbench::simple_c;

   struct benchmark_case {
      const char* label;
      const std::string* input;
   };

   [[nodiscard]] std::string make_function(std::size_t index, std::size_t statement_repetitions) {
      std::string program;
      program += "int fn" + std::to_string(index) + "() {\n";
      program += "  int total = " + std::to_string(static_cast<int>((index % 7) + 1)) + ";\n";
      program += "  int limit = " + std::to_string(static_cast<int>(statement_repetitions + 3)) + ";\n";
      program += "  while (total < limit) {\n";
      program += "    total = total + 1;\n";
      program += "    if (total == 5) {\n";
      program += "      total = total * 2;\n";
      program += "    } else {\n";
      program += "      total = total + 3;\n";
      program += "    }\n";
      program += "  }\n";
      for (std::size_t repeat = 0; repeat < statement_repetitions; ++repeat) {
         program += "  total = total + " + std::to_string(static_cast<int>((repeat % 5) + 1)) + ";\n";
         program += "  total = (total * 2) - 1;\n";
         program += "  if (total != limit) { total = total / 2; }\n";
      }
      program += "  return total;\n";
      program += "}\n";
      return program;
   }

   [[nodiscard]] std::string make_translation_unit(std::size_t function_count, std::size_t statement_repetitions) {
      std::string program;
      for (std::size_t i = 0; i < function_count; ++i) {
         program += make_function(i, statement_repetitions);
         program += "\n";
      }
      return program;
   }

   [[nodiscard]] const std::string& small_program() {
      static const auto program = make_translation_unit(1, 1);
      return program;
   }

   [[nodiscard]] const std::string& medium_program() {
      static const auto program = make_translation_unit(1, 2);
      return program;
   }

   [[nodiscard]] const std::string& large_program() {
      static const auto program = make_translation_unit(1, 3);
      return program;
   }

   [[nodiscard]] auto simple_c_case(std::int64_t characters) -> const benchmark_case* {
      static const benchmark_case cases[] = {
         {"small", &small_program()},
         {"medium", &medium_program()},
         {"large", &large_program()},
      };

      for (const auto& current_case : cases) {
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

   void simple_c_parse(benchmark::State& state) {
      auto* selected_case = simple_c_case(state.range(0));
      if (selected_case == nullptr) {
         report_unknown_case(state, "simple_c.parse", state.range(0));
         return;
      }

      state.SetLabel(selected_case->label);
      cpfbench::benchmark_parse<simple_c::translation_unit>(
         state,
         *selected_case->input,
         "simple_c.parse");
   }
}

BENCHMARK(simple_c_parse)
   ->Name("simple_c/parse")
   ->ArgName("chars")
   ->Arg(static_cast<std::int64_t>(small_program().size()))
   ->Arg(static_cast<std::int64_t>(medium_program().size()))
   ->Arg(static_cast<std::int64_t>(large_program().size()))
   ->Repetitions(8)
   ->ReportAggregatesOnly()
   ->ComputeStatistics("min", &cpfbench::detail::minimum)
   ->ComputeStatistics("max", &cpfbench::detail::maximum)
   ->Complexity()
   ->Unit(benchmark::kMicrosecond);






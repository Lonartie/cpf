#include <benchmark/benchmark.h>

#include <array>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
   struct size_summary {
	  std::optional<double> mean_cpu_time;
	  std::optional<double> min_cpu_time;
	  std::optional<double> max_cpu_time;
	  std::optional<double> items_per_second;
   };

   struct benchmark_summary {
	  std::string display_name;
	  std::map<benchmark::ComplexityN, size_summary> sizes;
	  std::optional<double> complexity_cpu_time;
	  benchmark::BigO complexity = benchmark::oNone;
	  std::optional<std::string> error;
   };

   [[nodiscard]] auto display_name_for(std::string_view family_name) -> std::string {
	  if (family_name == "calculator/parse") {
		 return "calculator parse";
	  }

	  if (family_name == "calculator/parse_and_evaluate") {
		 return "calculator parse + eval";
	  }

	  if (family_name == "simple_c/parse") {
		 return "simple_c parse";
	  }

	  return std::string{family_name};
   }

   [[nodiscard]] auto complexity_suffix(benchmark::BigO complexity) -> std::string_view {
	  switch (complexity) {
		 case benchmark::o1:
			return "O(1)";
		 case benchmark::oN:
			return "O(N)";
		 case benchmark::oNSquared:
			return "O(N^2)";
		 case benchmark::oNCubed:
			return "O(N^3)";
		 case benchmark::oLogN:
			return "O(log N)";
		 case benchmark::oNLogN:
			return "O(N log N)";
		 case benchmark::oLambda:
			return "O(lambda)";
		 case benchmark::oAuto:
		 case benchmark::oNone:
			break;
	  }

	  return "O(?)";
   }


   [[nodiscard]] auto format_number(double value, std::string_view suffix = {}) -> std::string {
	  std::ostringstream stream;
	  stream << std::fixed << std::setprecision(2) << value;
	  if (!suffix.empty()) {
		 stream << suffix;
	  }
	  return stream.str();
   }

   [[nodiscard]] auto format_optional(std::optional<double> value, std::string_view suffix = {}) -> std::string {
	  if (!value.has_value()) {
		 return "n/a";
	  }

	  return format_number(*value, suffix);
   }

   [[nodiscard]] auto find_items_per_second(const benchmark::UserCounters& counters) -> std::optional<double> {
	  auto iterator = counters.find("items_per_second");
	  if (iterator == counters.end()) {
		 return std::nullopt;
	  }

	  return iterator->second.value;
   }

   [[nodiscard]] auto parse_size_from_args(std::string_view args) -> std::optional<benchmark::ComplexityN> {
	  auto separator = args.find_last_of(':');
	  if (separator == std::string_view::npos || separator + 1 >= args.size()) {
		 return std::nullopt;
	  }

	  auto value = args.substr(separator + 1);
	  try {
		 return static_cast<benchmark::ComplexityN>(std::stoll(std::string{value}));
	  } catch (...) {
		 return std::nullopt;
	  }
	 }

   [[nodiscard]] auto benchmark_size(const benchmark::BenchmarkReporter::Run& report) -> std::optional<benchmark::ComplexityN> {
	  if (report.complexity_n > 0) {
		 return report.complexity_n;
	  }

	  return parse_size_from_args(report.run_name.args);
   }

   class compact_reporter final : public benchmark::BenchmarkReporter {
	public:
	  bool ReportContext(const Context& context) override {
		 (void)context;
		 return true;
	  }

	  void ReportRuns(const std::vector<Run>& reports) override {
		 for (const auto& report : reports) {
			auto family_name = report.run_name.function_name;
			auto& summary = m_summaries[family_name];
			if (summary.display_name.empty()) {
			   summary.display_name = display_name_for(family_name);
			}

			if (report.skipped != benchmark::internal::NotSkipped) {
			   summary.error = report.skip_message;
			   continue;
			}

			if (report.report_big_o) {
			   summary.complexity = report.complexity;
			   summary.complexity_cpu_time = report.GetAdjustedCPUTime();
			   continue;
			}

			if (report.report_rms || report.run_type != Run::RT_Aggregate) {
			   continue;
			}

			auto size = benchmark_size(report);
			if (!size.has_value()) {
			   continue;
			}

			auto cpu_time = report.GetAdjustedCPUTime();
			auto& size_metrics = summary.sizes[*size];

			if (report.aggregate_name == "min") {
			   size_metrics.min_cpu_time = cpu_time;
			   continue;
			}

			if (report.aggregate_name == "max") {
			   size_metrics.max_cpu_time = cpu_time;
			   continue;
			}

			if (report.aggregate_name == "mean") {
			   size_metrics.mean_cpu_time = cpu_time;
			   if (auto items_per_second = find_items_per_second(report.counters); items_per_second.has_value()) {
				  size_metrics.items_per_second = *items_per_second;
			   }
			}
		 }
	  }

	  void Finalize() override {
		 static constexpr auto family_order = std::array<std::string_view, 3>{
			"calculator/parse",
			"calculator/parse_and_evaluate",
			"simple_c/parse",
		 };

		 static constexpr auto benchmark_width = 28;
		 static constexpr auto metric_width = 12;
		 static constexpr auto throughput_width = 14;
		 static constexpr auto complexity_width = 22;

		 auto& output = GetOutputStream();
		 output
		   << std::left << std::setw(benchmark_width) << "Benchmark"
		   << std::right << std::setw(metric_width) << "Min"
		   << std::setw(metric_width) << "Avg"
		   << std::setw(metric_width) << "Max"
		   << std::setw(throughput_width) << "Iter/s"
		   << std::setw(complexity_width) << "Complexity"
		   << '\n';

		 output << std::string(benchmark_width + (metric_width * 3) + throughput_width + complexity_width, '-') << '\n';

		 for (auto family_name : family_order) {
			auto iterator = m_summaries.find(std::string{family_name});
			std::string benchmark_name = display_name_for(family_name);
			std::string minimum = "n/a";
			std::string average_cpu = "n/a";
			std::string maximum = "n/a";
			std::string iterations_per_second = "n/a";
			std::string complexity = "n/a";

			if (iterator == m_summaries.end()) {
			   complexity = "missing";
			} else {
			   const auto& summary = iterator->second;
			   benchmark_name = summary.display_name;

			   if (summary.error.has_value()) {
				  complexity = "error: " + *summary.error;
			   } else {
				  if (!summary.sizes.empty()) {
					 const auto& smallest_size = summary.sizes.begin()->second;
					 minimum = format_optional(smallest_size.min_cpu_time, "us");
					 average_cpu = format_optional(smallest_size.mean_cpu_time, "us");
					 maximum = format_optional(smallest_size.max_cpu_time, "us");
					 iterations_per_second = format_optional(smallest_size.items_per_second);
				  }

				  if (summary.complexity_cpu_time.has_value()) {
					 complexity = format_number(*summary.complexity_cpu_time) + " " + std::string{complexity_suffix(summary.complexity)};
				  }
			   }
			}

			output
			   << std::left << std::setw(benchmark_width) << benchmark_name
			   << std::right << std::setw(metric_width) << minimum
			   << std::setw(metric_width) << average_cpu
			   << std::setw(metric_width) << maximum
			   << std::setw(throughput_width) << iterations_per_second
			   << std::setw(complexity_width) << complexity
			   << '\n';
		 }
	  }

	private:
	  std::map<std::string, benchmark_summary, std::less<>> m_summaries;
   };
} // namespace

int main(int argc, char** argv) {
   benchmark::MaybeReenterWithoutASLR(argc, argv);

   char arg0_default[] = "benchmark";
   char* args_default = reinterpret_cast<char*>(arg0_default);
   if (argv == nullptr) {
	  argc = 1;
	  argv = &args_default;
   }

   benchmark::Initialize(&argc, argv);
   if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
	  return 1;
   }

   compact_reporter reporter;
   benchmark::RunSpecifiedBenchmarks(&reporter);
   benchmark::Shutdown();
   return 0;
}

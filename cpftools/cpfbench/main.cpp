#include "benchmark_support.h"

#include <benchmark/benchmark.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <new>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif

namespace {
   std::atomic<std::size_t> live_heap_allocated_bytes{0};
   thread_local auto heap_footprint_baseline_bytes = std::size_t{0};

#if !defined(__APPLE__) && !defined(__linux__)
   struct allocation_header {
      void* original = nullptr;
      std::size_t requested_size = 0;
   };
#endif

   void record_heap_allocation(std::size_t size) noexcept { live_heap_allocated_bytes.fetch_add(size, std::memory_order_relaxed); }

   void record_heap_release(std::size_t size) noexcept { live_heap_allocated_bytes.fetch_sub(size, std::memory_order_relaxed); }

   [[nodiscard]] auto current_live_heap_bytes() -> std::size_t {
      return live_heap_allocated_bytes.load(std::memory_order_relaxed);
   }

   [[nodiscard]] auto size_of_allocation(void* memory) noexcept -> std::size_t {
      if (memory == nullptr) {
         return 0;
      }

#if defined(__APPLE__)
      return malloc_size(memory);
#elif defined(__linux__)
      return malloc_usable_size(memory);
#else
      const auto address = reinterpret_cast<std::uintptr_t>(memory);
      const auto* header = reinterpret_cast<const allocation_header*>(address - sizeof(allocation_header));
      return header->requested_size;
#endif
   }

   [[nodiscard]] auto allocate_bytes(std::size_t size) -> void* {
      const auto allocation_size = size == 0 ? std::size_t{1} : size;

#if defined(__APPLE__) || defined(__linux__)
      auto* memory = std::malloc(allocation_size);
      if (memory == nullptr) {
         throw std::bad_alloc{};
      }

      record_heap_allocation(size_of_allocation(memory));
      return memory;
#else
      constexpr auto default_alignment = alignof(std::max_align_t);
      const auto total_size = allocation_size + default_alignment - 1 + sizeof(allocation_header);
      auto* original = std::malloc(total_size);
      if (original == nullptr) {
         throw std::bad_alloc{};
      }

      const auto base = reinterpret_cast<std::uintptr_t>(original) + sizeof(allocation_header);
      const auto aligned_address = (base + default_alignment - 1) & ~(static_cast<std::uintptr_t>(default_alignment) - 1U);
      auto* header = reinterpret_cast<allocation_header*>(aligned_address - sizeof(allocation_header));
      header->original = original;
      header->requested_size = allocation_size;
      record_heap_allocation(header->requested_size);
      return reinterpret_cast<void*>(aligned_address);
#endif
   }

#if defined(_MSC_VER)
   [[nodiscard]] auto allocate_aligned_bytes(std::size_t size, std::size_t alignment) -> void* {
      const auto allocation_size = size == 0 ? std::size_t{1} : size;
      auto* memory = _aligned_malloc(allocation_size, alignment);
      if (memory == nullptr) {
         throw std::bad_alloc{};
      }

      record_heap_allocation(allocation_size);
      return memory;
   }

   void free_bytes(void* memory) noexcept {
      if (memory == nullptr) {
         return;
      }

      record_heap_release(_msize(memory));
      std::free(memory);
   }

   void free_aligned_bytes(void* memory) noexcept {
      if (memory == nullptr) {
         return;
      }

      record_heap_release(_aligned_msize(memory, static_cast<std::size_t>(alignof(std::max_align_t)), 0));
      _aligned_free(memory);
   }
#elif defined(__APPLE__) || defined(__linux__)
   [[nodiscard]] auto allocate_aligned_bytes(std::size_t size, std::size_t alignment) -> void* {
      const auto allocation_size = size == 0 ? std::size_t{1} : size;
      void* memory = nullptr;
      if (posix_memalign(&memory, alignment, allocation_size) != 0) {
         throw std::bad_alloc{};
      }

      record_heap_allocation(size_of_allocation(memory));
      return memory;
   }

   void free_bytes(void* memory) noexcept {
      if (memory == nullptr) {
         return;
      }

      record_heap_release(size_of_allocation(memory));
      std::free(memory);
   }

   void free_aligned_bytes(void* memory) noexcept { free_bytes(memory); }
#else
   [[nodiscard]] auto allocate_aligned_bytes(std::size_t size, std::size_t alignment) -> void* {
      const auto allocation_size = size == 0 ? std::size_t{1} : size;
      const auto total_size = allocation_size + alignment - 1 + sizeof(allocation_header);
      auto* original = std::malloc(total_size);
      if (original == nullptr) {
         throw std::bad_alloc{};
      }

      const auto base = reinterpret_cast<std::uintptr_t>(original) + sizeof(allocation_header);
      const auto aligned_address = (base + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1U);
      auto* header = reinterpret_cast<allocation_header*>(aligned_address - sizeof(allocation_header));
      header->original = original;
      header->requested_size = allocation_size;
      record_heap_allocation(header->requested_size);
      return reinterpret_cast<void*>(aligned_address);
   }

   void free_bytes(void* memory) noexcept {
      if (memory == nullptr) {
         return;
      }

      const auto address = reinterpret_cast<std::uintptr_t>(memory);
      auto* header = reinterpret_cast<allocation_header*>(address - sizeof(allocation_header));
      record_heap_release(header->requested_size);
      std::free(header->original);
   }

   void free_aligned_bytes(void* memory) noexcept { free_bytes(memory); }
#endif

   struct size_summary {
      std::optional<double> mean_cpu_time;
      std::optional<double> min_cpu_time;
      std::optional<double> max_cpu_time;
      std::optional<double> items_per_second;
      std::optional<double> heap_footprint_bytes;
   };

   struct benchmark_summary {
      std::string display_name;
      std::map<benchmark::ComplexityN, size_summary> sizes;
      std::optional<double> complexity_cpu_time;
      benchmark::BigO complexity = benchmark::oNone;
      std::optional<std::string> error;
   };

   struct family_plan {
      std::string_view family_name;
      std::array<benchmark::ComplexityN, 3> sizes;
   };

   inline constexpr auto family_plans = std::array<family_plan, 8>{
         family_plan{"calculator/compiletime_lex_input", {9, 25, 33}},
         family_plan{"calculator/runtime_lex_input", {9, 25, 33}},
         family_plan{"calculator/compiletime_parse_tokens", {9, 25, 33}},
         family_plan{"calculator/runtime_parse_tokens", {9, 25, 33}},
         family_plan{"calculator/compiletime_materialize_ast", {9, 25, 33}},
         family_plan{"calculator/runtime_materialize_ast", {9, 25, 33}},
         family_plan{"calculator/compiletime_interpret_result", {9, 25, 33}},
         family_plan{"calculator/runtime_interpret_result", {9, 25, 33}},
   };

   [[nodiscard]] auto display_name_for(std::string_view family_name) -> std::string {
      if (family_name == "calculator/compiletime_lex_input") {
         return "calculator lex comptime";
      }

      if (family_name == "calculator/runtime_lex_input") {
         return "calculator lex runtime";
      }

      if (family_name == "calculator/compiletime_parse_tokens") {
         return "calculator parse tokens comptime";
      }

      if (family_name == "calculator/runtime_parse_tokens") {
         return "calculator parse tokens runtime";
      }

      if (family_name == "calculator/compiletime_materialize_ast") {
         return "calculator ast comptime";
      }

      if (family_name == "calculator/runtime_materialize_ast") {
         return "calculator ast runtime";
      }

      if (family_name == "calculator/compiletime_interpret_result") {
         return "calculator eval comptime";
      }

      if (family_name == "calculator/runtime_interpret_result") {
         return "calculator eval runtime";
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

   [[nodiscard]] auto find_heap_footprint_bytes(const benchmark::UserCounters& counters) -> std::optional<double> {
      auto iterator = counters.find(cpfbench::detail::heap_counter_name);
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

   [[nodiscard]] auto benchmark_size(const benchmark::BenchmarkReporter::Run& report)
         -> std::optional<benchmark::ComplexityN> {
      if (report.complexity_n > 0) {
         return report.complexity_n;
      }

      return parse_size_from_args(report.run_name.args);
   }

   [[nodiscard]] auto format_clock_duration(std::chrono::steady_clock::duration duration) -> std::string {
      using namespace std::chrono;
      const auto total_seconds = duration_cast<seconds>(duration).count();
      const auto hours = total_seconds / 3600;
      const auto minutes = (total_seconds % 3600) / 60;
      const auto seconds_part = total_seconds % 60;

      std::ostringstream stream;
      stream << std::setfill('0');
      if (hours > 0) {
         stream << std::setw(2) << hours << ':';
      }
      stream << std::setw(2) << minutes << ':' << std::setw(2) << seconds_part;
      return stream.str();
   }

   [[nodiscard]] auto format_heap_usage(std::optional<double> heap_footprint_bytes) -> std::string {
      if (!heap_footprint_bytes.has_value()) {
         return "n/a";
      }

      static constexpr auto units = std::array<std::string_view, 5>{"B", "KiB", "MiB", "GiB", "TiB"};

      auto heap_amount = *heap_footprint_bytes;
      auto unit_index = std::size_t{0};
      while (heap_amount >= 1024.0 && unit_index + 1 < units.size()) {
         heap_amount /= 1024.0;
         ++unit_index;
      }

      return format_number(heap_amount, units[unit_index]);
   }

   [[nodiscard]] auto milestone_key_for(const benchmark::BenchmarkReporter::Run& report) -> std::optional<std::string> {
      if (report.report_big_o) {
         return report.run_name.function_name + "#complexity";
      }

      if (report.report_rms || report.run_type != benchmark::BenchmarkReporter::Run::RT_Aggregate ||
          report.aggregate_name != "mean") {
         return std::nullopt;
      }

      auto size = benchmark_size(report);
      if (!size.has_value()) {
         return std::nullopt;
      }

      return report.run_name.function_name + "#size:" + std::to_string(*size);
   }

   [[nodiscard]] auto progress_targets_from_filter(std::string_view filter_text) -> std::set<std::string> {
      std::set<std::string> targets;
      std::optional<std::regex> filter;
      if (!filter_text.empty()) {
         try {
            filter.emplace(std::string{filter_text});
         } catch (...) {
            filter.reset();
         }
      }

      auto matches = [&](std::string_view value) {
         if (!filter.has_value()) {
            return true;
         }
         return std::regex_search(std::string{value}, *filter);
      };

      for (const auto& family: family_plans) {
         auto family_selected = false;
         for (const auto size: family.sizes) {
            auto run_name = std::string{family.family_name} + "/chars:" + std::to_string(size);
            if (!matches(run_name)) {
               continue;
            }
            targets.insert(std::string{family.family_name} + "#size:" + std::to_string(size));
            family_selected = true;
         }
         if (family_selected) {
            targets.insert(std::string{family.family_name} + "#complexity");
         }
      }

      return targets;
   }

   class compact_reporter final : public benchmark::BenchmarkReporter {
   public:
      explicit compact_reporter(std::set<std::string> expected_milestones) :
          m_expected_milestones{std::move(expected_milestones)} {}

      bool ReportContext(const Context& context) override {
         (void) context;
         m_started_at = std::chrono::steady_clock::now();
         if (!m_expected_milestones.empty()) {
            std::cerr << "[cpfbench] Progress 0/" << m_expected_milestones.size() << " (0.0%) elapsed 00:00"
                      << std::endl;
         }
         return true;
      }

      void ReportRuns(const std::vector<Run>& reports) override {
         for (const auto& report: reports) {
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
               if (auto milestone = milestone_key_for(report); milestone.has_value()) {
                  update_progress(*milestone, report.run_name.function_name, {}, std::nullopt);
               }
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
               if (auto heap_footprint_bytes = find_heap_footprint_bytes(report.counters);
                   heap_footprint_bytes.has_value()) {
                  size_metrics.heap_footprint_bytes = *heap_footprint_bytes;
               }
            }

            auto milestone = milestone_key_for(report);
            if (milestone.has_value()) {
               update_progress(*milestone, report.run_name.function_name, report.aggregate_name,
                               benchmark_size(report));
            }
         }
      }

      void Finalize() override {
         static constexpr auto family_order = std::array<std::string_view, 8>{
               "calculator/compiletime_lex_input",
               "calculator/runtime_lex_input",
               "calculator/compiletime_parse_tokens",
               "calculator/runtime_parse_tokens",
               "calculator/compiletime_materialize_ast",
               "calculator/runtime_materialize_ast",
               "calculator/compiletime_interpret_result",
               "calculator/runtime_interpret_result",
         };

         static constexpr auto benchmark_width = 28;
         static constexpr auto metric_width = 12;
         static constexpr auto throughput_width = 14;
         static constexpr auto heap_width = 12;
         static constexpr auto complexity_width = 22;

         auto& output = GetOutputStream();
         output << std::left << std::setw(benchmark_width) << "Benchmark" << std::right << std::setw(metric_width)
                << "Min" << std::setw(metric_width) << "Avg" << std::setw(metric_width) << "Max"
                << std::setw(throughput_width) << "Iter/s" << std::setw(heap_width) << "Heap"
                << std::setw(complexity_width) << "Complexity" << '\n';

         output << std::string(benchmark_width + (metric_width * 3) + throughput_width + heap_width + complexity_width,
                               '-')
                << '\n';

         for (auto family_name: family_order) {
            auto iterator = m_summaries.find(family_name);
            std::string benchmark_name = display_name_for(family_name);
            std::string minimum = "n/a";
            std::string average_cpu = "n/a";
            std::string maximum = "n/a";
            std::string iterations_per_second = "n/a";
            std::string heap_usage = "n/a";
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
                     heap_usage = format_heap_usage(smallest_size.heap_footprint_bytes);
                  }

                  if (summary.complexity_cpu_time.has_value()) {
                     complexity = format_number(*summary.complexity_cpu_time) + " " +
                                  std::string{complexity_suffix(summary.complexity)};
                  }
               }
            }

            output << std::left << std::setw(benchmark_width) << benchmark_name << std::right << std::setw(metric_width)
                   << minimum << std::setw(metric_width) << average_cpu << std::setw(metric_width) << maximum
                   << std::setw(throughput_width) << iterations_per_second << std::setw(heap_width) << heap_usage
                   << std::setw(complexity_width) << complexity << '\n';
         }

         if (!m_expected_milestones.empty()) {
            std::cerr << "[cpfbench] Progress complete " << m_completed_milestones.size() << "/"
                      << m_expected_milestones.size() << " elapsed "
                      << format_clock_duration(std::chrono::steady_clock::now() - m_started_at) << std::endl;
         }
      }

   private:
      void update_progress(const std::string& milestone, std::string_view family_name, std::string_view aggregate_name,
                           std::optional<benchmark::ComplexityN> size) {
         if (!m_expected_milestones.empty() && !m_expected_milestones.contains(milestone)) {
            return;
         }
         if (!m_completed_milestones.insert(milestone).second) {
            return;
         }

         const auto now = std::chrono::steady_clock::now();
         const auto elapsed = now - m_started_at;
         const auto completed = m_completed_milestones.size();
         const auto total = m_expected_milestones.empty() ? completed : m_expected_milestones.size();
         const auto percent =
               total == 0 ? 100.0 : (100.0 * static_cast<double>(completed) / static_cast<double>(total));

         std::ostringstream label;
         label << display_name_for(family_name);
         if (aggregate_name == "mean" && size.has_value()) {
            label << " [chars " << *size << "]";
         } else if (aggregate_name == "") {
            label << " [complexity]";
         } else if (milestone.ends_with("#complexity")) {
            label << " [complexity]";
         }

         std::cerr << "[cpfbench] Progress " << completed << "/" << total << " (" << std::fixed << std::setprecision(1)
                   << percent << "%)"
                   << " elapsed " << format_clock_duration(elapsed);

         if (completed > 0 && total > completed) {
            const auto average_seconds =
                  std::chrono::duration<double>(elapsed).count() / static_cast<double>(completed);
            const auto eta = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                  std::chrono::duration<double>{average_seconds * static_cast<double>(total - completed)});
            std::cerr << " eta " << format_clock_duration(eta);
         }

         std::cerr << " :: " << label.str() << std::endl;
      }

      std::map<std::string, benchmark_summary, std::less<>> m_summaries;
      std::set<std::string> m_expected_milestones;
      std::set<std::string> m_completed_milestones;
      std::chrono::steady_clock::time_point m_started_at{};
   };
} // namespace

namespace cpfbench::detail {
   void reset_heap_footprint_baseline() { heap_footprint_baseline_bytes = current_live_heap_bytes(); }

   auto current_heap_footprint_bytes() -> std::size_t {
      const auto live_bytes = current_live_heap_bytes();
      if (live_bytes < heap_footprint_baseline_bytes) {
         return 0;
      }

      return live_bytes - heap_footprint_baseline_bytes;
   }
} // namespace cpfbench::detail

void* operator new(std::size_t size) { return allocate_bytes(size); }

void* operator new[](std::size_t size) { return allocate_bytes(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
   try {
      return allocate_bytes(size);
   } catch (...) {
      return nullptr;
   }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
   try {
      return allocate_bytes(size);
   } catch (...) {
      return nullptr;
   }
}

void operator delete(void* memory) noexcept { free_bytes(memory); }

void operator delete[](void* memory) noexcept { free_bytes(memory); }

void operator delete(void* memory, std::size_t) noexcept { free_bytes(memory); }

void operator delete[](void* memory, std::size_t) noexcept { free_bytes(memory); }

void operator delete(void* memory, const std::nothrow_t&) noexcept { free_bytes(memory); }

void operator delete[](void* memory, const std::nothrow_t&) noexcept { free_bytes(memory); }

void* operator new(std::size_t size, std::align_val_t alignment) {
   return allocate_aligned_bytes(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
   return allocate_aligned_bytes(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
   try {
      return allocate_aligned_bytes(size, static_cast<std::size_t>(alignment));
   } catch (...) {
      return nullptr;
   }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
   try {
      return allocate_aligned_bytes(size, static_cast<std::size_t>(alignment));
   } catch (...) {
      return nullptr;
   }
}

void operator delete(void* memory, std::align_val_t) noexcept { free_aligned_bytes(memory); }

void operator delete[](void* memory, std::align_val_t) noexcept { free_aligned_bytes(memory); }

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { free_aligned_bytes(memory); }

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { free_aligned_bytes(memory); }

int main(int argc, char** argv) {
   benchmark::MaybeReenterWithoutASLR(argc, argv);

   char arg0_default[] = "benchmark";
   char* args_default = reinterpret_cast<char*>(arg0_default);
   if (argv == nullptr) {
      argc = 1;
      argv = &args_default;
   }

   std::string_view filter_text;
   for (auto index = 1; index < argc; ++index) {
      auto argument = std::string_view{argv[index]};
      if (argument.rfind("--benchmark_filter=", 0) == 0) {
         filter_text = argument.substr(std::string_view{"--benchmark_filter="}.size());
      }
   }

   benchmark::Initialize(&argc, argv);
   if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
      return 1;
   }

   compact_reporter reporter{progress_targets_from_filter(filter_text)};
   benchmark::RunSpecifiedBenchmarks(&reporter);
   benchmark::Shutdown();
   return 0;
}

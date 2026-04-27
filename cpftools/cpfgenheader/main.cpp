#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {
   struct configuration {
      std::filesystem::path source_root = std::filesystem::path{CPFGENHEADER_DEFAULT_SOURCE_ROOT};
      std::filesystem::path output_path = std::filesystem::path{CPFGENHEADER_DEFAULT_OUTPUT_PATH};
   };

   [[nodiscard]] auto print_usage(std::ostream& stream, const char* executable_name) -> int {
      stream << "Usage: " << executable_name << " [--source-root <path>] [--output <path>]" << std::endl;
      return &stream == &std::cout ? 0 : 1;
   }

   [[nodiscard]] auto read_text_file(const std::filesystem::path& path) -> std::string {
      auto stream = std::ifstream{path};
      if (!stream) {
         throw std::runtime_error{"Unable to read input file '" + path.string() + "'"};
      }
      return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
   }

   void write_text_file(const std::filesystem::path& path, const std::string& content) {
      if (!path.parent_path().empty()) {
         std::filesystem::create_directories(path.parent_path());
      }
      auto stream = std::ofstream{path};
      if (!stream) {
         throw std::runtime_error{"Unable to write output file '" + path.string() + "'"};
      }
      stream << content;
   }

   [[nodiscard]] auto trim(std::string_view text) -> std::string_view {
      auto begin = text.find_first_not_of(" \t\r");
      if (begin == std::string_view::npos) {
         return {};
      }
      auto end = text.find_last_not_of(" \t\r");
      return text.substr(begin, end - begin + 1);
   }

   [[nodiscard]] auto quoted_include_target(std::string_view line) -> std::string_view {
      constexpr auto include_prefix = std::string_view{"#include \""};
      auto normalized = trim(line);
      if (!normalized.starts_with(include_prefix)) {
         return {};
      }
      auto closing_quote = normalized.find('"', include_prefix.size());
      if (closing_quote == std::string_view::npos) {
         return {};
      }
      return normalized.substr(include_prefix.size(), closing_quote - include_prefix.size());
   }

   [[nodiscard]] auto is_system_include(std::string_view line) -> bool {
      auto normalized = trim(line);
      return normalized.starts_with("#include <") && normalized.ends_with('>');
   }

   [[nodiscard]] auto has_trailing_newline(const std::string& text) -> bool {
      return !text.empty() && text.back() == '\n';
   }

   enum class discovered_file_kind { declaration, implementation };

   [[nodiscard]] auto is_implementation_file(const std::filesystem::path& path) -> bool {
      const auto extension = path.extension().generic_string();
      return extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx";
   }

   [[nodiscard]] auto is_declaration_file(const std::filesystem::path& path) -> bool {
      const auto extension = path.extension().generic_string();
      return extension.empty() || extension == ".h" || extension == ".hh" || extension == ".hpp" || extension == ".hxx";
   }

   [[nodiscard]] auto classify_project_file(const std::filesystem::path& path) -> std::optional<discovered_file_kind> {
      if (!std::filesystem::is_regular_file(path)) {
         return std::nullopt;
      }
      if (is_declaration_file(path)) {
         return discovered_file_kind::declaration;
      }
      if (is_implementation_file(path)) {
         return discovered_file_kind::implementation;
      }
      return std::nullopt;
   }

   [[nodiscard]] auto implementation_namespace_name(const std::filesystem::path& source_root,
                                                    const std::filesystem::path& file_path) -> std::string {
      auto relative = file_path.lexically_relative(source_root).generic_string();
      std::string sanitized;
      sanitized.reserve(relative.size() + 32);
      sanitized = "cpf_amalgamated_";
      for (const auto ch: relative) {
         if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            sanitized += ch;
         } else {
            sanitized += '_';
         }
      }
      return sanitized;
   }

   [[nodiscard]] auto brace_delta(std::string_view text) -> int {
      auto delta = 0;
      for (const auto ch: text) {
         if (ch == '{') {
            ++delta;
         } else if (ch == '}') {
            --delta;
         }
      }
      return delta;
   }

   class amalgamator {
   public:
      explicit amalgamator(std::filesystem::path source_root) : source_root_{std::filesystem::weakly_canonical(source_root)} {}

      [[nodiscard]] auto build() -> std::string {
         output_.clear();
         visited_files_.clear();
         emitted_system_includes_.clear();

         append_line("#pragma once");
         append_line({});
         append_line("// Generated by cpftools/cpfgenheader. Do not edit manually.");
         append_line("//");
         append_line("// - Include this file like a normal header for declarations and templates.");
         append_line("// - Define CPF_IMPLEMENTATION in exactly one translation unit before including it");
         append_line("//   to emit the CPF runtime implementation bodies.");
         append_line({});

         append_section_comment("public declarations");
         const auto declaration_files = discover_project_files(discovered_file_kind::declaration);
         if (declaration_files.empty()) {
            throw std::runtime_error{"Unable to locate any CPF declaration files under '" +
                                     (source_root_ / "cpf").string() + "'"};
         }
         for (const auto& file: declaration_files) {
            append_file(file);
         }

         append_line({});
         append_line("#if defined(CPF_IMPLEMENTATION)");
         append_line({});
         append_section_comment("implementation");
         for (const auto& file: discover_project_files(discovered_file_kind::implementation)) {
            append_file(file);
         }
         append_line("#endif");
         return output_;
      }

   private:
      void append_line(std::string_view text) {
         output_ += text;
         output_ += '\n';
      }

      void append_section_comment(std::string_view section_name) {
         append_line(std::string{"// === "} + std::string{section_name} + " ===");
         append_line({});
      }

      [[nodiscard]] auto resolve_project_file(const std::filesystem::path& relative_path) const -> std::filesystem::path {
         const auto candidate = source_root_ / relative_path;
         if (!std::filesystem::exists(candidate)) {
            throw std::runtime_error{"Unable to locate source file '" + candidate.string() + "'"};
         }
         return std::filesystem::weakly_canonical(candidate);
      }

      [[nodiscard]] auto relative_key(const std::filesystem::path& path) const -> std::string {
         return path.lexically_relative(source_root_).generic_string();
      }

      [[nodiscard]] auto discover_project_files(discovered_file_kind kind) const -> std::vector<std::filesystem::path> {
         const auto cpf_root = resolve_project_file("cpf");
         auto discovered = std::vector<std::filesystem::path>{};
         for (const auto& entry: std::filesystem::recursive_directory_iterator{cpf_root}) {
            const auto classification = classify_project_file(entry.path());
            if (!classification.has_value() || *classification != kind) {
               continue;
            }
            discovered.push_back(std::filesystem::weakly_canonical(entry.path()));
         }

         std::sort(discovered.begin(), discovered.end(), [&](const auto& lhs, const auto& rhs) {
            return relative_key(lhs) < relative_key(rhs);
         });
         return discovered;
      }

      [[nodiscard]] auto resolve_local_include(const std::filesystem::path& current_file,
                                               std::string_view include_target) const -> std::filesystem::path {
         const auto relative_target = std::filesystem::path{include_target};
         const auto local_candidate = current_file.parent_path() / relative_target;
         if (std::filesystem::exists(local_candidate)) {
            return std::filesystem::weakly_canonical(local_candidate);
         }

         const auto cpf_root_candidate = source_root_ / "cpf" / relative_target;
         if (std::filesystem::exists(cpf_root_candidate)) {
            return std::filesystem::weakly_canonical(cpf_root_candidate);
         }

         const auto project_root_candidate = source_root_ / relative_target;
         if (std::filesystem::exists(project_root_candidate)) {
            return std::filesystem::weakly_canonical(project_root_candidate);
         }

         throw std::runtime_error{"Unable to resolve local include '" + std::string{include_target} +
                                  "' from '" + current_file.string() + "'"};
      }

      void append_file(const std::filesystem::path& path) {
         struct namespace_rewrite_state {
            int exit_depth = 0;
            std::string name;
         };

         const auto normalized_path = std::filesystem::weakly_canonical(path);
         if (!visited_files_.insert(normalized_path).second) {
            return;
         }

         const auto rewrite_anonymous_namespaces = is_implementation_file(normalized_path);
         const auto private_namespace_prefix = rewrite_anonymous_namespaces
                                                    ? implementation_namespace_name(source_root_, normalized_path)
                                                    : std::string{};
         auto private_namespace_index = std::size_t{0};
         auto brace_depth = 0;
         auto rewritten_namespaces = std::vector<namespace_rewrite_state>{};

          append_line(std::string{"// --- begin file: "} + relative_key(normalized_path));

         const auto text = read_text_file(normalized_path);
         auto stream = std::istringstream{text};
         auto line = std::string{};
         while (std::getline(stream, line)) {
            auto normalized = trim(line);
            if (normalized == "#pragma once") {
               continue;
            }

            if (const auto include_target = quoted_include_target(line); !include_target.empty()) {
               append_file(resolve_local_include(normalized_path, include_target));
               continue;
            }

            if (is_system_include(line)) {
               auto include_line = std::string{trim(line)};
               if (emitted_system_includes_.insert(include_line).second) {
                  append_line(include_line);
               }
               continue;
            }

            auto emitted_line = line;
            if (rewrite_anonymous_namespaces && normalized == "namespace {") {
               auto private_namespace = private_namespace_prefix + "_" + std::to_string(private_namespace_index++);
               emitted_line = "namespace " + private_namespace + " {";
               const auto next_depth = brace_depth + brace_delta(emitted_line);
               rewritten_namespaces.push_back(namespace_rewrite_state{next_depth - 1, private_namespace});
            }

            append_line(emitted_line);
            brace_depth += brace_delta(emitted_line);

            while (!rewritten_namespaces.empty() && brace_depth == rewritten_namespaces.back().exit_depth) {
               append_line("using namespace " + rewritten_namespaces.back().name + ";");
               rewritten_namespaces.pop_back();
            }
         }

         if (!has_trailing_newline(text)) {
            append_line({});
         }
          append_line(std::string{"// --- end file: "} + relative_key(normalized_path));
         append_line({});
      }

      std::filesystem::path source_root_;
      std::string output_;
      std::unordered_set<std::filesystem::path> visited_files_;
      std::unordered_set<std::string> emitted_system_includes_;
   };

   [[nodiscard]] auto parse_arguments(int argc, char** argv) -> configuration {
      auto config = configuration{};
      for (auto index = 1; index < argc; ++index) {
         const auto option = std::string_view{argv[index]};
         if (option == "--help" || option == "-h") {
            std::exit(print_usage(std::cout, argv[0]));
         }
         if (option == "--source-root") {
            if (index + 1 >= argc) {
               throw std::runtime_error{"Missing value for --source-root"};
            }
            config.source_root = argv[++index];
            continue;
         }
         if (option == "--output") {
            if (index + 1 >= argc) {
               throw std::runtime_error{"Missing value for --output"};
            }
            config.output_path = argv[++index];
            continue;
         }
         throw std::runtime_error{"Unknown option '" + std::string{option} + "'"};
      }
      return config;
   }

   void write_if_different(const std::filesystem::path& output_path, const std::string& content) {
      if (std::filesystem::exists(output_path) && read_text_file(output_path) == content) {
         std::cout << "cpfgenheader: up to date '" << output_path.string() << "'" << std::endl;
         return;
      }
      write_text_file(output_path, content);
      std::cout << "cpfgenheader: wrote '" << output_path.string() << "'" << std::endl;
   }
} // namespace

int main(int argc, char** argv) {
   try {
      const auto config = parse_arguments(argc, argv);
      auto amalgamator = ::amalgamator{config.source_root};
      auto content = amalgamator.build();
      write_if_different(config.output_path, content);
      return 0;
   } catch (const std::exception& exception) {
      std::cerr << exception.what() << std::endl;
      return 1;
   }
}



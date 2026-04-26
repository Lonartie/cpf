#include "support/doctest.h"

#include <cstdlib>
#include <map>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {
   struct command_result {
      int exit_code = 0;
      std::string stdout_text;
      std::string stderr_text;
      std::filesystem::path header_path;
      std::filesystem::path source_path;
   };

   void write_file(const std::filesystem::path& path, std::string_view content) {
      std::filesystem::create_directories(path.parent_path());
      std::ofstream stream{path};
      REQUIRE(stream.good());
      stream << content;
   }

   [[nodiscard]] std::string read_file(const std::filesystem::path& path) {
      auto stream = std::ifstream{path};
      REQUIRE(stream.good());
      return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
   }

   [[nodiscard]] std::string shell_quote(const std::filesystem::path& path) {
      const auto text = path.string();
#if defined(_WIN32)
      auto quoted = std::string{"\""};
      for (const auto ch: text) {
         if (ch == '\"') {
            quoted += '\\';
         }
         quoted += ch;
      }
      quoted += '\"';
      return quoted;
#else
      auto quoted = std::string{"'"};
      for (const auto ch: text) {
         if (ch == '\'') {
            quoted += "'\\''";
         } else {
            quoted += ch;
         }
      }
      quoted += '\'';
      return quoted;
#endif
   }

   [[nodiscard]] command_result run_cpfgen(std::string_view grammar_source, std::string_view grammar_name) {
      const auto root = std::filesystem::temp_directory_path() /
                        (std::string{"cpfgen_cli_tests_"} + std::string{grammar_name});
      std::filesystem::remove_all(root);
      std::filesystem::create_directories(root);

      const auto grammar_path = root / (std::string{grammar_name} + ".cpf");
      const auto output_path = root / "generated";
      const auto stdout_path = root / "stdout.txt";
      const auto stderr_path = root / "stderr.txt";
      write_file(grammar_path, grammar_source);

      auto command = shell_quote(std::filesystem::path{CPFGEN_EXECUTABLE_PATH});
      command += ' ' + shell_quote(grammar_path);
      command += ' ' + shell_quote(output_path);
      command += " >" + shell_quote(stdout_path);
      command += " 2>" + shell_quote(stderr_path);

      command_result result;
      result.exit_code = std::system(command.c_str());
      if (std::filesystem::exists(stdout_path)) {
         result.stdout_text = read_file(stdout_path);
      }
      if (std::filesystem::exists(stderr_path)) {
         result.stderr_text = read_file(stderr_path);
      }
      result.header_path = output_path / (std::string{grammar_name} + ".h");
      result.source_path = output_path / (std::string{grammar_name} + ".cpp");
      return result;
   }

   [[nodiscard]] command_result run_cpfgen(const std::map<std::filesystem::path, std::string>& files,
                                           const std::filesystem::path& root_grammar_path) {
      const auto root = std::filesystem::temp_directory_path() /
                        (std::string{"cpfgen_cli_tests_"} + root_grammar_path.stem().string() + "_files");
      std::filesystem::remove_all(root);
      std::filesystem::create_directories(root);

      for (const auto& [relative_path, content]: files) {
         write_file(root / relative_path, content);
      }

      const auto grammar_path = root / root_grammar_path;
      const auto output_path = root / "generated";
      const auto stdout_path = root / "stdout.txt";
      const auto stderr_path = root / "stderr.txt";

      auto command = shell_quote(std::filesystem::path{CPFGEN_EXECUTABLE_PATH});
      command += ' ' + shell_quote(grammar_path);
      command += ' ' + shell_quote(output_path);
      command += " >" + shell_quote(stdout_path);
      command += " 2>" + shell_quote(stderr_path);

      command_result result;
      result.exit_code = std::system(command.c_str());
      if (std::filesystem::exists(stdout_path)) {
         result.stdout_text = read_file(stdout_path);
      }
      if (std::filesystem::exists(stderr_path)) {
         result.stderr_text = read_file(stderr_path);
      }
      result.header_path = output_path / (grammar_path.stem().string() + ".h");
      result.source_path = output_path / (grammar_path.stem().string() + ".cpp");
      return result;
   }
} // namespace

TEST_SUITE("cpflib.cpfgen_cli") {
   TEST_CASE("cpfgen prints non-blocking grammar diagnostics to stdout while still writing files") {
      const auto result = run_cpfgen(R"(
         entry -> used;
         used -> 'x':value;
         detached -> helper;
         helper -> 'y':value;
      )",
                                     "warning_grammar");

      CHECK(result.exit_code == 0);
      CHECK(result.stdout_text.find("grammar analysis:") != std::string::npos);
      CHECK(result.stdout_text.find("warning_grammar.cpf:4: warning[unused_rule] rule 'detached'") != std::string::npos);
      CHECK(result.stdout_text.find("warning_grammar.cpf:5: warning[unreachable_rule] rule 'helper'") != std::string::npos);
      CHECK(result.stderr_text.empty());
      CHECK(std::filesystem::exists(result.header_path));
      CHECK(std::filesystem::exists(result.source_path));
   }

   TEST_CASE("cpfgen prints nullable-cycle diagnostics as warnings and still writes generated files") {
      const auto result = run_cpfgen(R"(
         entry -> first:value;
         first -> second:value;
         first -> '':empty;
         second -> first:value;
         second -> '':empty;
      )",
                                     "nullable_cycle_grammar");

      CHECK(result.exit_code == 0);
      CHECK(result.stdout_text.find("grammar analysis:") != std::string::npos);
      CHECK(result.stdout_text.find("nullable_cycle_grammar.cpf:3: warning[nullable_cycle] rule 'first'") !=
            std::string::npos);
      CHECK(result.stdout_text.find("warnings=1") != std::string::npos);
      CHECK(result.stdout_text.find("errors=0") != std::string::npos);
      CHECK(result.stderr_text.empty());
      CHECK(std::filesystem::exists(result.header_path));
      CHECK(std::filesystem::exists(result.source_path));
   }

   TEST_CASE("cpfgen remaps imported-rule diagnostics back to the original file") {
      const auto result = run_cpfgen({
            {"root.cpf", "entry -> 'a':value;\n@import 'child.cpf';\n"},
            {"child.cpf", "detached -> 'x':value;\n"}
      },
                                     "root.cpf");

      CHECK(result.exit_code == 0);
      CHECK(result.stdout_text.find("grammar analysis:") != std::string::npos);
      CHECK(result.stdout_text.find("child.cpf:1: warning[unused_rule] rule 'detached'") != std::string::npos);
      CHECK(result.stdout_text.find("root.cpf:2: warning[unused_rule] rule 'detached'") == std::string::npos);
      CHECK(result.stderr_text.empty());
      CHECK(std::filesystem::exists(result.header_path));
      CHECK(std::filesystem::exists(result.source_path));
   }
}


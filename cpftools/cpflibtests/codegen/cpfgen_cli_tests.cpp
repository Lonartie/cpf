#include "support/doctest.h"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

#if defined(_WIN32)
   [[nodiscard]] std::wstring windows_quote(const std::filesystem::path& path) {
      const auto text = path.native();
      if (text.empty()) {
         return L"\"\"";
      }
      if (text.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
         return text;
      }

      auto quoted = std::wstring{L"\""};
      auto backslash_count = std::size_t{0};
      for (const auto ch: text) {
         if (ch == L'\\') {
            ++backslash_count;
            continue;
         }

         if (ch == L'\"') {
            quoted.append(backslash_count * 2 + 1, L'\\');
            quoted += ch;
            backslash_count = 0;
            continue;
         }

         quoted.append(backslash_count, L'\\');
         backslash_count = 0;
         quoted += ch;
      }

      quoted.append(backslash_count * 2, L'\\');
      quoted += L'\"';
      return quoted;

   }

   [[nodiscard]] std::wstring windows_command_line(const std::filesystem::path& executable,
                                                   const std::vector<std::filesystem::path>& arguments) {
      auto command = windows_quote(executable);
      for (const auto& argument: arguments) {
         command += L' ';
         command += windows_quote(argument);
      }
      return command;
   }

   [[nodiscard]] int run_cpfgen_process(const std::filesystem::path& grammar_path,
                                        const std::filesystem::path& output_path,
                                        const std::filesystem::path& stdout_path,
                                        const std::filesystem::path& stderr_path) {
      auto security_attributes = SECURITY_ATTRIBUTES{};
      security_attributes.nLength = sizeof(security_attributes);
      security_attributes.bInheritHandle = TRUE;

      const auto stdout_handle = CreateFileW(stdout_path.c_str(), GENERIC_WRITE,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE, &security_attributes,
                                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (stdout_handle == INVALID_HANDLE_VALUE) {
         throw std::system_error{static_cast<int>(GetLastError()), std::system_category(),
                                 "Unable to open cpfgen stdout capture"};
      }

      const auto stderr_handle = CreateFileW(stderr_path.c_str(), GENERIC_WRITE,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE, &security_attributes,
                                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (stderr_handle == INVALID_HANDLE_VALUE) {
         CloseHandle(stdout_handle);
         throw std::system_error{static_cast<int>(GetLastError()), std::system_category(),
                                 "Unable to open cpfgen stderr capture"};
      }

      auto startup = STARTUPINFOW{};
      startup.cb = sizeof(startup);
      startup.dwFlags = STARTF_USESTDHANDLES;
      startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
      startup.hStdOutput = stdout_handle;
      startup.hStdError = stderr_handle;

      auto process = PROCESS_INFORMATION{};
      auto command_line = windows_command_line(std::filesystem::path{CPFGEN_EXECUTABLE_PATH}, {grammar_path, output_path});
      auto mutable_command_line = std::vector<wchar_t>{command_line.begin(), command_line.end()};
      mutable_command_line.push_back(L'\0');
      const auto launched = CreateProcessW(std::filesystem::path{CPFGEN_EXECUTABLE_PATH}.c_str(),
                                           mutable_command_line.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                           nullptr, &startup, &process);

      CloseHandle(stdout_handle);
      CloseHandle(stderr_handle);

      if (launched == FALSE) {
         throw std::system_error{static_cast<int>(GetLastError()), std::system_category(),
                                 "Unable to launch cpfgen"};
      }

      CloseHandle(process.hThread);
      WaitForSingleObject(process.hProcess, INFINITE);
      auto exit_code = DWORD{1};
      if (GetExitCodeProcess(process.hProcess, &exit_code) == FALSE) {
         const auto error = GetLastError();
         CloseHandle(process.hProcess);
         throw std::system_error{static_cast<int>(error), std::system_category(),
                                 "Unable to read cpfgen exit code"};
      }
      CloseHandle(process.hProcess);
      return static_cast<int>(exit_code);
   }
#else
   [[nodiscard]] int run_cpfgen_process(const std::filesystem::path& grammar_path,
                                        const std::filesystem::path& output_path,
                                        const std::filesystem::path& stdout_path,
                                        const std::filesystem::path& stderr_path) {
      const auto stdout_fd = ::open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (stdout_fd < 0) {
         throw std::system_error{errno, std::system_category(), "Unable to open cpfgen stdout capture"};
      }

      const auto stderr_fd = ::open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (stderr_fd < 0) {
         const auto error = errno;
         ::close(stdout_fd);
         throw std::system_error{error, std::system_category(), "Unable to open cpfgen stderr capture"};
      }

      const auto child = ::fork();
      if (child < 0) {
         const auto error = errno;
         ::close(stdout_fd);
         ::close(stderr_fd);
         throw std::system_error{error, std::system_category(), "Unable to fork cpfgen child process"};
      }

      if (child == 0) {
         if (::dup2(stdout_fd, STDOUT_FILENO) < 0 || ::dup2(stderr_fd, STDERR_FILENO) < 0) {
            _exit(127);
         }
         ::close(stdout_fd);
         ::close(stderr_fd);

         auto executable = std::filesystem::path{CPFGEN_EXECUTABLE_PATH}.string();
         auto grammar = grammar_path.string();
         auto output = output_path.string();
         auto arguments = std::vector<char*>{
               executable.data(),
               grammar.data(),
               output.data(),
               nullptr
         };
         ::execv(executable.c_str(), arguments.data());
         _exit(127);
      }

      ::close(stdout_fd);
      ::close(stderr_fd);

      auto status = int{0};
      while (::waitpid(child, &status, 0) < 0) {
         if (errno != EINTR) {
            throw std::system_error{errno, std::system_category(), "Unable to wait for cpfgen child process"};
         }
      }

      if (WIFEXITED(status)) {
         return WEXITSTATUS(status);
      }
      if (WIFSIGNALED(status)) {
         return 128 + WTERMSIG(status);
      }
      return -1;
   }
#endif

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

      command_result result;
      try {
         result.exit_code = run_cpfgen_process(grammar_path, output_path, stdout_path, stderr_path);
      } catch (const std::exception& exception) {
         result.exit_code = -1;
         result.stderr_text = exception.what();
      }
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

      command_result result;
      try {
         result.exit_code = run_cpfgen_process(grammar_path, output_path, stdout_path, stderr_path);
      } catch (const std::exception& exception) {
         result.exit_code = -1;
         result.stderr_text = exception.what();
      }
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


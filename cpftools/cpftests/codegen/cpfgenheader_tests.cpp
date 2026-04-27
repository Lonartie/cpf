#include "support/doctest.h"

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
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
   const auto cpfgenheader_executable_path = std::filesystem::path{CPFGENHEADER_EXECUTABLE_PATH};
   const auto cpf_source_root_path = std::filesystem::path{CPF_SOURCE_ROOT_PATH};
   const auto cpf_single_header_path = std::filesystem::path{CPF_SINGLE_HEADER_PATH};

   struct command_result {
      int exit_code = 0;
      std::string stdout_text;
      std::string stderr_text;
      std::filesystem::path output_path;
   };

   [[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::string {
      auto stream = std::ifstream{path};
      REQUIRE(stream.good());
      return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
   }

   [[nodiscard]] auto is_cpf_input_file(const std::filesystem::path& path) -> bool {
      const auto extension = path.extension().generic_string();
      return extension.empty() || extension == ".h" || extension == ".hh" || extension == ".hpp" ||
             extension == ".hxx" || extension == ".c" || extension == ".cc" || extension == ".cpp" ||
             extension == ".cxx";
   }

   [[nodiscard]] auto discover_cpf_input_files() -> std::vector<std::filesystem::path> {
      const auto cpf_root = cpf_source_root_path / "cpf";
      auto discovered = std::vector<std::filesystem::path>{};
      for (const auto& entry: std::filesystem::recursive_directory_iterator{cpf_root}) {
         if (!std::filesystem::is_regular_file(entry.path()) || !is_cpf_input_file(entry.path())) {
            continue;
         }
         discovered.push_back(std::filesystem::weakly_canonical(entry.path()));
      }
      std::sort(discovered.begin(), discovered.end(), [](const auto& lhs, const auto& rhs) {
         return lhs.generic_string() < rhs.generic_string();
      });
      return discovered;
   }

#if defined(_WIN32)
   [[nodiscard]] auto windows_quote(const std::filesystem::path& path) -> std::wstring {
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

   [[nodiscard]] auto windows_command_line(const std::filesystem::path& executable,
                                           const std::vector<std::filesystem::path>& arguments) -> std::wstring {
      auto command = windows_quote(executable);
      for (const auto& argument: arguments) {
         command += L' ';
         command += windows_quote(argument);
      }
      return command;
   }

   [[nodiscard]] auto run_process(const std::filesystem::path& executable, const std::vector<std::filesystem::path>& arguments,
                                  const std::filesystem::path& stdout_path,
                                  const std::filesystem::path& stderr_path) -> int {
      auto security_attributes = SECURITY_ATTRIBUTES{};
      security_attributes.nLength = sizeof(security_attributes);
      security_attributes.bInheritHandle = TRUE;

      const auto stdout_handle = CreateFileW(stdout_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                             &security_attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (stdout_handle == INVALID_HANDLE_VALUE) {
         throw std::system_error{static_cast<int>(GetLastError()), std::system_category(),
                                 "Unable to open cpfgenheader stdout capture"};
      }

      const auto stderr_handle = CreateFileW(stderr_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                             &security_attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (stderr_handle == INVALID_HANDLE_VALUE) {
         CloseHandle(stdout_handle);
         throw std::system_error{static_cast<int>(GetLastError()), std::system_category(),
                                 "Unable to open cpfgenheader stderr capture"};
      }

      auto startup = STARTUPINFOW{};
      startup.cb = sizeof(startup);
      startup.dwFlags = STARTF_USESTDHANDLES;
      startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
      startup.hStdOutput = stdout_handle;
      startup.hStdError = stderr_handle;

      auto process = PROCESS_INFORMATION{};
      auto command_line = windows_command_line(executable, arguments);
      auto mutable_command_line = std::vector<wchar_t>{command_line.begin(), command_line.end()};
      mutable_command_line.push_back(L'\0');
      const auto launched = CreateProcessW(executable.c_str(), mutable_command_line.data(), nullptr, nullptr, TRUE, 0,
                                           nullptr, nullptr, &startup, &process);

      CloseHandle(stdout_handle);
      CloseHandle(stderr_handle);

      if (launched == FALSE) {
         throw std::system_error{static_cast<int>(GetLastError()), std::system_category(),
                                 "Unable to launch cpfgenheader"};
      }

      CloseHandle(process.hThread);
      WaitForSingleObject(process.hProcess, INFINITE);
      auto exit_code = DWORD{1};
      if (GetExitCodeProcess(process.hProcess, &exit_code) == FALSE) {
         const auto error = GetLastError();
         CloseHandle(process.hProcess);
         throw std::system_error{static_cast<int>(error), std::system_category(),
                                 "Unable to read cpfgenheader exit code"};
      }
      CloseHandle(process.hProcess);
      return static_cast<int>(exit_code);
   }
#else
   [[nodiscard]] auto run_process(const std::filesystem::path& executable, const std::vector<std::filesystem::path>& arguments,
                                  const std::filesystem::path& stdout_path,
                                  const std::filesystem::path& stderr_path) -> int {
      const auto stdout_fd = ::open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (stdout_fd < 0) {
         throw std::system_error{errno, std::system_category(), "Unable to open cpfgenheader stdout capture"};
      }

      const auto stderr_fd = ::open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (stderr_fd < 0) {
         const auto error = errno;
         ::close(stdout_fd);
         throw std::system_error{error, std::system_category(), "Unable to open cpfgenheader stderr capture"};
      }

      const auto child = ::fork();
      if (child < 0) {
         const auto error = errno;
         ::close(stdout_fd);
         ::close(stderr_fd);
         throw std::system_error{error, std::system_category(), "Unable to fork cpfgenheader child process"};
      }

      if (child == 0) {
         if (::dup2(stdout_fd, STDOUT_FILENO) < 0 || ::dup2(stderr_fd, STDERR_FILENO) < 0) {
            _exit(127);
         }
         ::close(stdout_fd);
         ::close(stderr_fd);

         auto executable_text = executable.string();
         auto owned_arguments = std::vector<std::string>{};
         owned_arguments.reserve(arguments.size());
         auto argv = std::vector<char*>{};
         argv.reserve(arguments.size() + 2);
         argv.push_back(executable_text.data());
         for (const auto& argument: arguments) {
            owned_arguments.push_back(argument.string());
         }
         for (auto& argument: owned_arguments) {
            argv.push_back(argument.data());
         }
         argv.push_back(nullptr);
         ::execv(executable_text.c_str(), argv.data());
         _exit(127);
      }

      ::close(stdout_fd);
      ::close(stderr_fd);

      auto status = int{0};
      while (::waitpid(child, &status, 0) < 0) {
         if (errno != EINTR) {
            throw std::system_error{errno, std::system_category(), "Unable to wait for cpfgenheader child process"};
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

   [[nodiscard]] auto run_cpfgenheader(const std::vector<std::filesystem::path>& arguments,
                                       std::string_view scenario_name) -> command_result {
      const auto root = std::filesystem::temp_directory_path() /
                        (std::string{"cpfgenheader_tests_"} + std::string{scenario_name});
      std::filesystem::remove_all(root);
      std::filesystem::create_directories(root);

      const auto stdout_path = root / "stdout.txt";
      const auto stderr_path = root / "stderr.txt";

      command_result result;
      result.output_path = root / "cpflib";
      try {
         result.exit_code = run_process(cpfgenheader_executable_path, arguments, stdout_path, stderr_path);
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
      return result;
   }
} // namespace

TEST_SUITE("cpflib.cpfgenheader") {
   TEST_CASE("regenerated single header matches committed include/cpflib") {
      const auto temporary_root = std::filesystem::temp_directory_path() / "cpfgenheader_tests_regenerate";
      std::filesystem::remove_all(temporary_root);
      std::filesystem::create_directories(temporary_root);
      const auto generated_path = temporary_root / "cpflib";

      const auto result = run_cpfgenheader({std::filesystem::path{"--output"}, generated_path}, "regenerate");

      INFO(result.stdout_text);
      INFO(result.stderr_text);
      CHECK(result.exit_code == 0);
      REQUIRE(std::filesystem::exists(generated_path));
      REQUIRE(std::filesystem::exists(cpf_single_header_path));
      CHECK(read_file(generated_path) == read_file(cpf_single_header_path));
   }

   TEST_CASE("single header covers every recursively discovered cpf input file") {
      const auto temporary_root = std::filesystem::temp_directory_path() / "cpfgenheader_tests_recursive_discovery";
      std::filesystem::remove_all(temporary_root);
      std::filesystem::create_directories(temporary_root);
      const auto generated_path = temporary_root / "cpflib";

      const auto result = run_cpfgenheader({std::filesystem::path{"--output"}, generated_path}, "recursive_discovery");

      INFO(result.stdout_text);
      INFO(result.stderr_text);
      REQUIRE(result.exit_code == 0);
      REQUIRE(std::filesystem::exists(generated_path));

      const auto header = read_file(generated_path);
      const auto discovered = discover_cpf_input_files();
      REQUIRE(!discovered.empty());
      for (const auto& path: discovered) {
         const auto relative = path.lexically_relative(cpf_source_root_path).generic_string();
         CHECK_MESSAGE(header.find("// --- begin file: " + relative) != std::string::npos,
                       "missing bundled file marker for " << relative);
      }
   }

   TEST_CASE("single header contains the implementation gate") {
      REQUIRE(std::filesystem::exists(cpf_single_header_path));
      const auto header = read_file(cpf_single_header_path);
      CHECK(header.find("#if defined(CPF_IMPLEMENTATION)") != std::string::npos);
      CHECK(header.find("auto parse_shared_forest(") != std::string::npos);
   }
}




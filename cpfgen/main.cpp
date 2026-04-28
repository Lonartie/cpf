#include <cpfgenlib>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>

namespace {
   struct file_source_location {
      std::filesystem::path path;
      cpf::source_location location;
   };

   [[nodiscard]] std::string_view diagnostic_code_name(cpf::grammar_diagnostic_code code) {
      switch (code) {
         case cpf::grammar_diagnostic_code::unreachable_rule:
            return "unreachable_rule";
         case cpf::grammar_diagnostic_code::unused_rule:
            return "unused_rule";
         case cpf::grammar_diagnostic_code::nullable_cycle:
            return "nullable_cycle";
         case cpf::grammar_diagnostic_code::suspicious_recursive_pattern:
            return "suspicious_recursive_pattern";
         case cpf::grammar_diagnostic_code::inconsistent_inline_redefinition:
            return "inconsistent_inline_redefinition";
         case cpf::grammar_diagnostic_code::ignored_inline_request:
            return "ignored_inline_request";
      }
      return "unknown";
   }

   [[nodiscard]] std::string_view diagnostic_severity_name(cpf::grammar_diagnostic_severity severity) {
      switch (severity) {
         case cpf::grammar_diagnostic_severity::warning:
            return "warning";
         case cpf::grammar_diagnostic_severity::error:
            return "error";
      }
      return "warning";
   }

   [[nodiscard]] int print_usage() {
      std::cerr << "Usage: cpfgen <grammar-file> [output-directory] [--namespace <value>] [--depfile <path>]"
                << std::endl;
      return 1;
   }

   [[nodiscard]] cpf::source_location add_relative_location(cpf::source_location base,
                                                            const cpf::source_location& relative) {
      base.offset += relative.offset;
      base.line += relative.line - 1;
      if (relative.line == 1) {
         base.column += relative.column - 1;
      } else {
         base.column = relative.column;
      }
      return base;
   }

   [[nodiscard]] file_source_location diagnostic_location_for(const cpf::loaded_grammar& loaded,
                                                              const std::filesystem::path& grammar_path,
                                                              std::size_t generated_line) {
      const auto resolved = loaded.mapper.resolve(cpf::source_location{0, generated_line, 1}, loaded.preprocessed_source_id);
      if (resolved.has_value()) {
         if (auto origin = loaded.source_origins.find(resolved->id); origin != loaded.source_origins.end()) {
            return file_source_location{origin->second.path, add_relative_location(origin->second.begin, resolved->location)};
         }
         return file_source_location{grammar_path, resolved->location};
      }
      return file_source_location{grammar_path, {0, generated_line, 1}};
   }

   void print_diagnostics(const cpf::grammar_analysis& analysis, const cpf::loaded_grammar& loaded,
                          const std::filesystem::path& grammar_path) {
      if (analysis.diagnostics.empty()) {
         return;
      }

      std::cout << analysis.render_summary() << std::endl;
      for (const auto& diagnostic: analysis.diagnostics) {
         const auto location = diagnostic_location_for(loaded, grammar_path, diagnostic.line);
         std::cout << location.path.string() << ":" << location.location.line << ": "
                   << diagnostic_severity_name(diagnostic.severity) << '[' << diagnostic_code_name(diagnostic.code)
                   << "]";
         if (!diagnostic.rule.empty()) {
            std::cout << " rule '" << diagnostic.rule << "'";
         }
         std::cout << ": " << diagnostic.message;
         if (!diagnostic.related_rules.empty()) {
            std::cout << " (related:";
            for (const auto& related_rule: diagnostic.related_rules) {
               std::cout << " '" << related_rule << "'";
            }
            std::cout << ')';
         }
         std::cout << std::endl;
      }
   }

   void write_text_file(const std::filesystem::path& path, const std::string& content) {
      std::ofstream stream{path};
      if (!stream) {
         throw std::runtime_error{"Unable to write output file '" + path.string() + "'"};
      }

      stream << content;
   }

   [[nodiscard]] std::filesystem::path depfile_path_for(const std::filesystem::path& value,
                                                        const std::filesystem::path& working_directory) {
      auto error = std::error_code{};
      auto absolute_value = std::filesystem::absolute(value, error);
      if (error) {
         absolute_value = value;
         error.clear();
      }

      auto absolute_working_directory = std::filesystem::absolute(working_directory, error);
      if (error) {
         absolute_working_directory = working_directory;
      }

      auto normalized_value = absolute_value.lexically_normal();
      auto normalized_working_directory = absolute_working_directory.lexically_normal();
      auto relative_value = normalized_value.lexically_relative(normalized_working_directory);
      if (!relative_value.empty()) {
         return relative_value;
      }

      return normalized_value;
   }

   [[nodiscard]] std::string escape_depfile_path(const std::filesystem::path& path,
                                                 const std::filesystem::path& working_directory) {
      auto text = depfile_path_for(path, working_directory).generic_string();
      std::string escaped;
      escaped.reserve(text.size() * 2);
      for (char ch: text) {
         switch (ch) {
            case ' ':
               escaped += "\\ ";
               break;
            case '#':
               escaped += "\\#";
               break;
            case '$':
               escaped += "$$";
               break;
            case ':':
               escaped += "\\:";
               break;
            default:
               escaped += ch;
               break;
         }
      }
      return escaped;
   }

   void write_depfile(const std::filesystem::path& path, const std::filesystem::path& generated_header,
                      const std::filesystem::path& generated_source,
                      const std::vector<std::filesystem::path>& dependencies) {
      std::ofstream stream{path};
      if (!stream) {
         throw std::runtime_error{"Unable to write depfile '" + path.string() + "'"};
      }

      auto working_directory = std::filesystem::current_path();
      stream << escape_depfile_path(generated_header, working_directory) << ' '
             << escape_depfile_path(generated_source, working_directory) << ':';
      for (const auto& dependency: dependencies) {
         stream << ' ' << escape_depfile_path(dependency, working_directory);
      }
      stream << '\n';
   }
} // namespace

int main(int argc, char** argv) {
   try {
      if (argc < 2) {
         return print_usage();
      }

      auto grammar_path = std::filesystem::path{argv[1]};
      std::filesystem::path output_directory;
      std::filesystem::path depfile_path;
      std::string code_namespace;

      auto index = 2;
      if (index < argc && std::string_view{argv[index]}.rfind("--", 0) != 0) {
         output_directory = std::filesystem::path{argv[index]};
         ++index;
      }

      while (index < argc) {
         auto option = std::string_view{argv[index++]};
         if (option == "--depfile") {
            if (index >= argc) {
               return print_usage();
            }
            depfile_path = argv[index++];
         } else if (option == "--namespace") {
            if (index >= argc) {
               return print_usage();
            }
            code_namespace = argv[index++];
         } else {
            return print_usage();
         }
      }

      if (output_directory.empty()) {
         output_directory = grammar_path.parent_path();
      }

      auto base_name = grammar_path.stem().string();
      auto loaded = cpf::load_grammar_file(grammar_path);
      auto analysis = cpf::analyze_grammar(loaded.parsed_grammar);
      if (analysis.has_errors()) {
         print_diagnostics(analysis, loaded, grammar_path);
         std::cerr << "cpfgen: generation aborted because blocking grammar diagnostics were found" << std::endl;
         return 1;
      }

      auto generated = cpf::generate_code(loaded.parsed_grammar, base_name, code_namespace);
      print_diagnostics(generated.analysis, loaded, grammar_path);

      std::filesystem::create_directories(output_directory);
      auto header_path = output_directory / (base_name + ".h");
      auto source_path = output_directory / (base_name + ".cpp");
      write_text_file(header_path, generated.header);
      write_text_file(source_path, generated.source);
      if (!depfile_path.empty()) {
         write_depfile(depfile_path, header_path, source_path, loaded.dependencies);
      }
      return 0;
   } catch (const std::exception& exception) {
      std::cerr << exception.what() << std::endl;
      return 1;
   }
}

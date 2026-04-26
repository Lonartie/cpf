#include "grammar_loader.h"

#include "grammar_parser.h"

#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cpf {
   namespace {
      class grammar_loader {
      public:
         [[nodiscard]] loaded_grammar load(const std::filesystem::path& root_path) {
            loaded_grammar result;
            auto normalized_root = normalize_existing(root_path);
            auto preprocessed_text = preprocess_file(normalized_root, result);
            try {
               result.parsed_grammar = parse_grammar(preprocessed_text);
            } catch (const std::runtime_error& error) {
               throw std::runtime_error{"While loading grammar file '" + normalized_root.string() + "': " + error.what()};
            }
            return result;
         }

      private:
         [[nodiscard]] static bool is_quote(char ch) { return ch == '\'' || ch == '"'; }

         [[nodiscard]] static std::filesystem::path normalize_existing(const std::filesystem::path& path) {
            auto absolute_path = std::filesystem::absolute(path);
            if (!std::filesystem::exists(absolute_path)) {
               throw std::runtime_error{"Unable to open grammar file '" + absolute_path.lexically_normal().string() +
                                        "'"};
            }
            return std::filesystem::weakly_canonical(absolute_path);
         }

         [[nodiscard]] static std::string read_text_file(const std::filesystem::path& path) {
            std::ifstream stream{path};
            if (!stream) {
               throw std::runtime_error{"Unable to open grammar file '" + path.string() + "'"};
            }

            std::ostringstream buffer;
            buffer << stream.rdbuf();
            return buffer.str();
         }

         [[nodiscard]] std::string preprocess_file(const std::filesystem::path& path, loaded_grammar& result) {
            if (active_stack_.contains(path)) {
               throw std::runtime_error{"Grammar import cycle detected at '" + path.string() + "'"};
            }

            active_stack_.insert(path);
            if (dependency_paths_.insert(path).second) {
               result.dependencies.push_back(path);
            }

            auto text = read_text_file(path);
            auto position = std::size_t{0};
            auto line = std::size_t{1};
            auto preprocessed = std::string{};
            while (position < text.size()) {
               if (text[position] == '/' && position + 1 < text.size() && text[position + 1] == '/') {
                  auto comment_end = position + 2;
                  while (comment_end < text.size() && text[comment_end] != '\n') {
                     ++comment_end;
                  }
                  preprocessed.append(text, position, comment_end - position);
                  position = comment_end;
                  continue;
               }

               if (is_quote(text[position])) {
                  auto quote = text[position++];
                  preprocessed += quote;
                  while (position < text.size()) {
                     auto current = text[position];
                     preprocessed += current;
                     ++position;
                     if (current == '\\') {
                        if (position < text.size()) {
                           preprocessed += text[position];
                           ++position;
                        }
                        continue;
                     }
                     if (current == quote) {
                        break;
                     }
                     if (current == '\n') {
                        ++line;
                     }
                  }
                  continue;
               }

               if (starts_with_import(text, position)) {
                  auto import_path = parse_import(text, position, line, path);
                  preprocessed += preprocess_file(import_path, result);
                  continue;
               }

               if (text[position] == '\n') {
                  ++line;
               }
               preprocessed += text[position];
               ++position;
            }

            active_stack_.erase(path);
            return preprocessed;
         }

         [[nodiscard]] static bool starts_with_import(const std::string& text, std::size_t position) {
            constexpr auto keyword = std::string_view{"@import"};
            if (text.substr(position, keyword.size()) != keyword) {
               return false;
            }
            auto end = position + keyword.size();
            if (end >= text.size()) {
               return true;
            }
            auto next = static_cast<unsigned char>(text[end]);
            return std::isspace(next) != 0 || text[end] == '\'' || text[end] == '"';
         }

         [[nodiscard]] static std::filesystem::path parse_import(const std::string& text, std::size_t& position,
                                                                 std::size_t& line,
                                                                 const std::filesystem::path& importer_path) {
            position += std::string_view{"@import"}.size();
            skip_import_spacing(text, position, line);
            if (position >= text.size() || !is_quote(text[position])) {
               throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " +
                                        std::to_string(line) + ": expected quoted import path"};
            }

            auto quote = text[position];
            ++position;
            std::string import_target;
            while (position < text.size()) {
               auto current = text[position];
               if (current == '\\') {
                  ++position;
                  if (position >= text.size()) {
                     throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " +
                                              std::to_string(line) + ": unexpected end of import path"};
                  }
                  import_target += text[position];
                  ++position;
                  continue;
               }
               if (current == quote) {
                  ++position;
                  break;
               }
               if (current == '\n') {
                  throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " +
                                           std::to_string(line) + ": unterminated import path"};
               }
               import_target += current;
               ++position;
            }

            skip_import_spacing(text, position, line);
            if (position >= text.size() || text[position] != ';') {
               throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " +
                                         std::to_string(line) + ": expected ';' after @import"};
            }
            ++position;
            return normalize_existing(importer_path.parent_path() / import_target);
         }

         static void skip_import_spacing(const std::string& text, std::size_t& position, std::size_t& line) {
            while (position < text.size()) {
               if (std::isspace(static_cast<unsigned char>(text[position])) != 0) {
                  if (text[position] == '\n') {
                     ++line;
                  }
                  ++position;
                  continue;
               }
               if (text[position] == '/' && position + 1 < text.size() && text[position + 1] == '/') {
                  position += 2;
                  while (position < text.size() && text[position] != '\n') {
                     ++position;
                  }
                  continue;
               }
               break;
            }
         }

         std::set<std::filesystem::path> active_stack_;
         std::set<std::filesystem::path> dependency_paths_;
      };
   } // namespace

   loaded_grammar load_grammar_file(const std::filesystem::path& path) { return grammar_loader{}.load(path); }

   grammar parse_grammar_file(const std::filesystem::path& path) { return load_grammar_file(path).parsed_grammar; }
} // namespace cpf

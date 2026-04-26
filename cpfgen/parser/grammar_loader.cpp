#include "grammar_loader.h"

#include "grammar_parser.h"

#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cpf {
   namespace {
      struct preprocess_piece {
         std::size_t id = 0;
         std::string text;
      };

      struct preprocess_result {
         std::string text;
         std::size_t source_id = 0;
      };

      void advance_position(source_position& position, char ch) {
         ++position.offset;
         if (ch == '\n') {
            ++position.line;
            position.column = 1;
            return;
         }
         ++position.column;
      }

      void advance_position(source_position& position, std::string_view text) {
         for (const auto ch: text) {
            advance_position(position, ch);
         }
      }

      class grammar_loader {
      public:
         [[nodiscard]] loaded_grammar load(const std::filesystem::path& root_path) {
            loaded_grammar result;
            auto normalized_root = normalize_existing(root_path);
            const auto preprocessed = preprocess_file(normalized_root, result);
            result.preprocessed_source = preprocessed.text;
            result.preprocessed_source_id = preprocessed.source_id;
            try {
               result.parsed_grammar = parse_grammar(result.preprocessed_source);
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

         [[nodiscard]] preprocess_result preprocess_file(const std::filesystem::path& path, loaded_grammar& result) {
            if (active_stack_.contains(path)) {
               throw std::runtime_error{"Grammar import cycle detected at '" + path.string() + "'"};
            }

            active_stack_.insert(path);
            if (dependency_paths_.insert(path).second) {
               result.dependencies.push_back(path);
            }

            const auto text = read_text_file(path);
            auto pieces = std::vector<preprocess_piece>{};
            auto position = std::size_t{0};
            auto current = source_position{};
            auto copy_begin = current;
            auto copy_start = position;

            const auto flush_copied_text = [&]() {
               if (copy_start == position) {
                  return;
               }
               auto slice = std::string_view{text}.substr(copy_start, position - copy_start);
               const auto source_id = next_source_id_++;
               result.mapper.append_source(slice, source_id);
               result.source_origins.emplace(source_id, grammar_source_origin{path, copy_begin});
               pieces.push_back(preprocess_piece{source_id, std::string{slice}});
            };

            while (position < text.size()) {
               if (text[position] == '/' && position + 1 < text.size() && text[position + 1] == '/') {
                  while (position < text.size() && text[position] != '\n') {
                     advance_position(current, text[position]);
                     ++position;
                  }
                  continue;
               }

               if (is_quote(text[position])) {
                  auto quote = text[position++];
                  advance_position(current, quote);
                  while (position < text.size()) {
                     const auto ch = text[position];
                     advance_position(current, ch);
                     ++position;
                     if (ch == '\\') {
                        if (position < text.size()) {
                           advance_position(current, text[position]);
                           ++position;
                        }
                        continue;
                     }
                     if (ch == quote) {
                        break;
                     }
                  }
                  continue;
               }

               if (starts_with_import(text, position)) {
                  flush_copied_text();
                  auto import_path = parse_import(text, position, current, path);
                  auto imported = preprocess_file(import_path, result);
                  pieces.push_back(preprocess_piece{imported.source_id, std::move(imported.text)});
                  copy_start = position;
                  copy_begin = current;
                  continue;
               }

               advance_position(current, text[position]);
               ++position;
            }

            flush_copied_text();

            auto preprocessed_text = std::string{};
            for (const auto& piece: pieces) {
               preprocessed_text += piece.text;
            }

            const auto transformed_id = next_source_id_++;
            result.mapper.append_source(preprocessed_text, transformed_id);

            auto replacement_begin = source_position{};
            for (const auto& piece: pieces) {
               auto replacement_end = replacement_begin;
               advance_position(replacement_end, piece.text);
               result.mapper.append(piece.id, transformed_id, source_range{replacement_begin, replacement_end});
               replacement_begin = replacement_end;
            }

            active_stack_.erase(path);
            return preprocess_result{std::move(preprocessed_text), transformed_id};
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
                                                                 source_position& source, const std::filesystem::path& importer_path) {
            advance_position(source, std::string_view{"@import"});
            position += std::string_view{"@import"}.size();
            skip_import_spacing(text, position, source);
            if (position >= text.size() || !is_quote(text[position])) {
               throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " +
                                        std::to_string(source.line) + ": expected quoted import path"};
            }

            auto quote = text[position];
            advance_position(source, quote);
            ++position;
            std::string import_target;
            while (position < text.size()) {
               auto current = text[position];
               if (current == '\\') {
                  advance_position(source, current);
                  ++position;
                  if (position >= text.size()) {
                     throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " +
                                              std::to_string(source.line) + ": unexpected end of import path"};
                  }
                  import_target += text[position];
                  advance_position(source, text[position]);
                  ++position;
                  continue;
               }
               if (current == quote) {
                  advance_position(source, current);
                  ++position;
                  break;
               }
               if (current == '\n') {
                  throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " +
                                           std::to_string(source.line) + ": unterminated import path"};
               }
               import_target += current;
               advance_position(source, current);
               ++position;
            }

            skip_import_spacing(text, position, source);
            if (position >= text.size() || text[position] != ';') {
               throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " +
                                         std::to_string(source.line) + ": expected ';' after @import"};
            }
            advance_position(source, ';');
            ++position;
            return normalize_existing(importer_path.parent_path() / import_target);
         }

         static void skip_import_spacing(const std::string& text, std::size_t& position, source_position& source) {
            while (position < text.size()) {
               if (std::isspace(static_cast<unsigned char>(text[position])) != 0) {
                  advance_position(source, text[position]);
                  ++position;
                  continue;
               }
               if (text[position] == '/' && position + 1 < text.size() && text[position + 1] == '/') {
                  advance_position(source, text[position]);
                  ++position;
                  advance_position(source, text[position]);
                  ++position;
                  while (position < text.size() && text[position] != '\n') {
                     advance_position(source, text[position]);
                     ++position;
                  }
                  continue;
               }
               break;
            }
         }

         std::set<std::filesystem::path> active_stack_;
         std::set<std::filesystem::path> dependency_paths_;
         std::size_t next_source_id_ = 1;
      };
   } // namespace

   loaded_grammar load_grammar_file(const std::filesystem::path& path) { return grammar_loader{}.load(path); }

   grammar parse_grammar_file(const std::filesystem::path& path) { return load_grammar_file(path).parsed_grammar; }
} // namespace cpf

#include "grammar_loader.h"

#include "grammar_parser.h"

#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace cpf {
   namespace {
      class grammar_loader {
      public:
         [[nodiscard]] loaded_grammar load(const std::filesystem::path& root_path) {
            loaded_grammar result;
            load_file(normalize_existing(root_path), result);
            return result;
         }

      private:
         [[nodiscard]] static bool is_quote(char ch) {
            return ch == '\'' || ch == '"';
         }

         struct file_content {
            std::string text;
            std::filesystem::path normalized_path;
         };

         [[nodiscard]] static std::filesystem::path normalize_existing(const std::filesystem::path& path) {
            auto absolute_path = std::filesystem::absolute(path);
            if (!std::filesystem::exists(absolute_path)) {
               throw std::runtime_error{"Unable to open grammar file '" + absolute_path.lexically_normal().string() + "'"};
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

         static void merge_rule(grammar& target, rule incoming_rule) {
            if (incoming_rule.synthetic) {
               target.rules.push_back(std::move(incoming_rule));
               return;
            }

            if (auto* existing = target.find_rule(incoming_rule.identifier); existing != nullptr) {
               const auto definition_offset = existing->productions.size();
               for (auto& production : incoming_rule.productions) {
                  production.definition += definition_offset;
               }
               existing->productions.insert(existing->productions.end(), incoming_rule.productions.begin(), incoming_rule.productions.end());
               return;
            }

            target.rules.push_back(std::move(incoming_rule));
         }

         static void rename_synthetic_rules(grammar& parsed_grammar, std::string_view prefix) {
            std::unordered_map<std::string, std::string> renamed_rules;
            for (auto& rule : parsed_grammar.rules) {
               if (!rule.synthetic) {
                  continue;
               }
               auto renamed = std::string{prefix} + rule.identifier;
               renamed_rules.emplace(rule.identifier, renamed);
               rule.identifier = std::move(renamed);
            }

            if (renamed_rules.empty()) {
               return;
            }

            for (auto& rule : parsed_grammar.rules) {
               for (auto& production : rule.productions) {
                  for (auto& symbol : production.symbols) {
                     if (symbol.kind != symbol_kind::reference) {
                        continue;
                     }
                     if (auto renamed = renamed_rules.find(symbol.value); renamed != renamed_rules.end()) {
                        symbol.value = renamed->second;
                     }
                  }
               }
            }
         }

         void load_file(const std::filesystem::path& path, loaded_grammar& result) {
            if (loaded_paths_.contains(path)) {
               return;
            }
            if (active_stack_.contains(path)) {
               throw std::runtime_error{"Grammar import cycle detected at '" + path.string() + "'"};
            }

            active_stack_.insert(path);
            result.dependencies.push_back(path);

            auto text = read_text_file(path);
            auto position = std::size_t{0};
            auto line = std::size_t{1};
            while (true) {
               skip_ignored(text, position, line);
               if (position >= text.size()) {
                  break;
               }

               if (starts_with_import(text, position)) {
                  auto import_path = parse_import(text, position, line, path);
                  load_file(import_path, result);
                  continue;
               }

               auto rule_line = line;
               auto rule_text = extract_rule(text, position, line, path);
               auto prefixed_text = std::string(rule_line > 0 ? rule_line - 1 : 0, '\n') + rule_text;
               try {
                  auto parsed = parse_grammar(prefixed_text);
                  rename_synthetic_rules(parsed, "$cpf_import_" + std::to_string(synthetic_prefix_counter_++) + "_");
                  for (auto& rule : parsed.rules) {
                     merge_rule(result.parsed_grammar, std::move(rule));
                  }
               } catch (const std::runtime_error& error) {
                  throw std::runtime_error{"While loading grammar file '" + path.string() + "': " + error.what()};
               }
            }

            active_stack_.erase(path);
            loaded_paths_.insert(path);
         }

         static void skip_ignored(const std::string& text, std::size_t& position, std::size_t& line) {
            while (position < text.size()) {
               auto current = text[position];
               if (std::isspace(static_cast<unsigned char>(current)) != 0) {
                  if (current == '\n') {
                     ++line;
                  }
                  ++position;
                  continue;
               }
               if (position + 1 < text.size() && text[position] == '/' && text[position + 1] == '/') {
                  position += 2;
                  while (position < text.size() && text[position] != '\n') {
                     ++position;
                  }
                  continue;
               }
               break;
            }
         }

         [[nodiscard]] static bool starts_with_import(const std::string& text, std::size_t position) {
            constexpr auto keyword = std::string_view{"import"};
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

         [[nodiscard]] static std::filesystem::path parse_import(
            const std::string& text,
            std::size_t& position,
            std::size_t& line,
            const std::filesystem::path& importer_path) {
            position += std::string_view{"import"}.size();
            skip_ignored(text, position, line);
            if (position >= text.size() || !is_quote(text[position])) {
               throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " + std::to_string(line) + ": expected quoted import path"};
            }

            auto quote = text[position];
            ++position;
            std::string import_target;
            while (position < text.size()) {
               auto current = text[position];
               if (current == '\\') {
                  ++position;
                  if (position >= text.size()) {
                     throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " + std::to_string(line) + ": unexpected end of import path"};
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
                  throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " + std::to_string(line) + ": unterminated import path"};
               }
               import_target += current;
               ++position;
            }

            skip_ignored(text, position, line);
            if (position >= text.size() || text[position] != ';') {
               throw std::runtime_error{"Grammar import error in '" + importer_path.string() + "' at line " + std::to_string(line) + ": expected ';' after import"};
            }
            ++position;
            return normalize_existing(importer_path.parent_path() / import_target);
         }

         [[nodiscard]] static std::string extract_rule(
            const std::string& text,
            std::size_t& position,
            std::size_t& line,
            const std::filesystem::path& path) {
            auto start = position;
            auto quote = char{};
            auto escaped = false;
            while (position < text.size()) {
               auto current = text[position];
               if (quote != '\0') {
                  if (escaped) {
                     escaped = false;
                  } else if (current == '\\') {
                     escaped = true;
                  } else if (current == quote) {
                     quote = '\0';
                  }
                  if (current == '\n') {
                     ++line;
                  }
                  ++position;
                  continue;
               }

               if (current == '\'' || current == '"') {
                  quote = current;
                  ++position;
                  continue;
               }
               if (current == '\n') {
                  ++line;
                  ++position;
                  continue;
               }
               if (current == ';') {
                  ++position;
                  return text.substr(start, position - start);
               }
               ++position;
            }

            throw std::runtime_error{"While loading grammar file '" + path.string() + "': expected ';' to terminate rule"};
         }

         std::set<std::filesystem::path> loaded_paths_;
         std::set<std::filesystem::path> active_stack_;
         std::size_t synthetic_prefix_counter_ = 0;
      };
   } // namespace

   loaded_grammar load_grammar_file(const std::filesystem::path& path) {
      return grammar_loader{}.load(path);
   }

   grammar parse_grammar_file(const std::filesystem::path& path) {
      return load_grammar_file(path).parsed_grammar;
   }
} // namespace cpf



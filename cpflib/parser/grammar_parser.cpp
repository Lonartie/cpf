#include "grammar_parser.h"

#include <cctype>
#include <stdexcept>

namespace cpf {
   namespace {
      class grammar_parser {
      public:
         explicit grammar_parser(std::string_view text)
            : text_{text} {
         }

         grammar parse() {
            grammar result;
            skip_ignored();
            while (!eof()) {
               auto parsed_rule = parse_rule();
               if (auto* existing = result.find_rule(parsed_rule.identifier); existing != nullptr) {
                  const auto definition_offset = existing->productions.size();
                  for (auto& production : parsed_rule.productions) {
                     production.definition += definition_offset;
                  }
                  existing->productions.insert(existing->productions.end(), parsed_rule.productions.begin(), parsed_rule.productions.end());
               } else {
                  result.rules.push_back(std::move(parsed_rule));
               }
               skip_ignored();
            }
            return result;
         }

      private:
         [[nodiscard]] bool eof() const {
            return position_ >= text_.size();
         }

         [[nodiscard]] std::size_t column() const {
            auto column = std::size_t{1};
            auto current_position = position_;
            while (current_position > 0 && text_[current_position - 1] != '\n') {
               --current_position;
               ++column;
            }
            return column;
         }

         [[nodiscard]] std::string found_token() const {
            if (eof()) {
               return "<end of input>";
            }
            auto end = position_;
            if (std::isspace(static_cast<unsigned char>(text_[end])) != 0) {
               return "\" \"";
            }
            while (end < text_.size() && std::isspace(static_cast<unsigned char>(text_[end])) == 0) {
               ++end;
            }
            return "\"" + std::string{text_.substr(position_, std::min<std::size_t>(end - position_, 16))} + "\"";
         }

         [[nodiscard]] char current() const {
            return eof() ? '\0' : text_[position_];
         }

         void advance() {
            if (eof()) {
               return;
            }
            if (text_[position_] == '\n') {
               ++line_;
            }
            ++position_;
         }

         void skip_ignored() {
            while (!eof()) {
               if (std::isspace(static_cast<unsigned char>(current())) != 0) {
                  advance();
                  continue;
               }
               if (text_.substr(position_, 2) == "//") {
                  while (!eof() && current() != '\n') {
                     advance();
                  }
                  continue;
               }
               break;
            }
         }

         [[nodiscard]] bool take(std::string_view token) {
            skip_ignored();
            if (text_.substr(position_, token.size()) != token) {
               return false;
            }
            for (std::size_t i = 0; i < token.size(); ++i) {
               advance();
            }
            return true;
         }

         void expect(std::string_view token) {
            if (!take(token)) {
               throw error("Expected '" + std::string{token} + "'");
            }
         }

         [[nodiscard]] std::runtime_error error(const std::string& message) const {
            auto full_message = "Grammar parse error at line " + std::to_string(line_)
                              + ", column " + std::to_string(column()) + ": " + message
                              + " but found " + found_token();
            if (!current_rule_.empty()) {
               full_message += " while parsing rule '" + current_rule_ + "'";
            }
            return std::runtime_error{full_message};
         }

         std::string parse_identifier() {
            skip_ignored();
            if (eof() || (!std::isalpha(static_cast<unsigned char>(current())) && current() != '_')) {
               throw error("Expected identifier");
            }
            std::string identifier;
            while (!eof()) {
               auto ch = current();
               if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_') {
                  break;
               }
               identifier += ch;
               advance();
            }
            return identifier;
         }

         std::string parse_quoted() {
            skip_ignored();
            if (current() != '\'') {
               throw error("Expected quoted string");
            }
            advance();
            std::string value;
            while (!eof()) {
               auto ch = current();
               if (ch == '\\') {
                  advance();
                  if (eof()) {
                     throw error("Unexpected end of input in quoted string");
                  }
                  auto escaped = current();
                  switch (escaped) {
                     case 'n': value += '\n'; break;
                     case 'r': value += '\r'; break;
                     case 't': value += '\t'; break;
                     case '\\': value += '\\'; break;
                     case '\'': value += '\''; break;
                     default: value += escaped; break;
                  }
                  advance();
                  continue;
               }
               if (ch == '\'') {
                  advance();
                  return value;
               }
               value += ch;
               advance();
            }
            throw error("Unterminated quoted string");
         }

         attribute parse_attribute() {
            attribute parsed_attribute;
            parsed_attribute.name = parse_identifier();
            skip_ignored();
            if (take("=")) {
               parsed_attribute.operation = attribute_operator::assign;
            } else if (take("<")) {
               parsed_attribute.operation = attribute_operator::less_than;
            } else if (take(">")) {
               parsed_attribute.operation = attribute_operator::greater_than;
            } else {
               throw error("Expected attribute operator");
            }

            skip_ignored();
            if (current() == '\'') {
               parsed_attribute.value = parse_quoted();
               parsed_attribute.numeric = false;
               return parsed_attribute;
            }
            if (std::isdigit(static_cast<unsigned char>(current())) != 0 || current() == '-') {
               parsed_attribute.numeric = true;
               while (!eof() && (std::isdigit(static_cast<unsigned char>(current())) != 0 || current() == '-')) {
                  parsed_attribute.value += current();
                  advance();
               }
               return parsed_attribute;
            }
            parsed_attribute.value = parse_identifier();
            parsed_attribute.numeric = false;
            return parsed_attribute;
         }

         std::vector<attribute> parse_attributes() {
            std::vector<attribute> attributes;
            if (!take("[")) {
               return attributes;
            }
            do {
               attributes.push_back(parse_attribute());
            } while (take(","));
            expect("]");
            return attributes;
         }

         symbol parse_symbol() {
            skip_ignored();
            symbol parsed_symbol;
            if (current() == 'r' && text_.substr(position_, 2) == "r'") {
               advance();
               parsed_symbol.kind = symbol_kind::regex;
               parsed_symbol.value = parse_quoted();
            } else if (current() == '\'') {
               parsed_symbol.kind = symbol_kind::literal;
               parsed_symbol.value = parse_quoted();
            } else {
               parsed_symbol.kind = symbol_kind::reference;
               parsed_symbol.value = parse_identifier();
            }
            skip_ignored();
            if (take(":")) {
               parsed_symbol.label = parse_identifier();
            }
            return parsed_symbol;
         }

         production parse_production(const std::vector<attribute>& attributes, std::size_t line) {
            production parsed_production;
            parsed_production.attributes = attributes;
            parsed_production.line = line;
            skip_ignored();
            while (!eof() && current() != '|' && current() != ';') {
               parsed_production.symbols.push_back(parse_symbol());
               skip_ignored();
            }
            if (parsed_production.symbols.empty()) {
               throw error("Expected at least one symbol in production");
            }
            return parsed_production;
         }

         rule parse_rule() {
            auto line = line_;
            rule parsed_rule;
            parsed_rule.identifier = parse_identifier();
            current_rule_ = parsed_rule.identifier;
            auto attributes = parse_attributes();
            expect("->");
            do {
               auto parsed_production = parse_production(attributes, line);
               parsed_production.definition = parsed_rule.productions.size();
               parsed_rule.productions.push_back(std::move(parsed_production));
            } while (take("|"));
            expect(";");
            current_rule_.clear();
            return parsed_rule;
         }

         std::string_view text_;
         std::size_t position_ = 0;
         std::size_t line_ = 1;
         std::string current_rule_;
      };
   } // namespace

   grammar parse_grammar(std::string_view text) {
      return grammar_parser{text}.parse();
   }
} // namespace cpf


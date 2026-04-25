#include "grammar_parser.h"

#include <cctype>
#include <memory>
#include <stdexcept>
#include <utility>

namespace cpf {
   namespace {
      struct grouped_sequence_item;

      struct grouped_expression {
         std::vector<std::vector<grouped_sequence_item>> alternatives;
         std::string label;
         symbol_quantifier quantifier = symbol_quantifier::one;
         std::size_t exact_repetition = 1;
      };

      struct grouped_sequence_item {
         std::optional<symbol> parsed_symbol;
         std::unique_ptr<grouped_expression> group;
      };

      struct parsed_rule_bundle {
         rule main_rule;
         std::vector<rule> synthetic_rules;
      };

      class grammar_parser {
      public:
         explicit grammar_parser(std::string_view text)
            : text_{text} {
         }

         grammar parse() {
            grammar result;
            skip_ignored();
            while (!eof()) {
               auto parsed_rules = parse_rule();
               auto& parsed_rule = parsed_rules.main_rule;
               if (auto* existing = result.find_rule(parsed_rule.identifier); existing != nullptr) {
                  const auto definition_offset = existing->productions.size();
                  for (auto& production : parsed_rule.productions) {
                     production.definition += definition_offset;
                  }
                  existing->productions.insert(existing->productions.end(), parsed_rule.productions.begin(), parsed_rule.productions.end());
               } else {
                  result.rules.push_back(std::move(parsed_rule));
               }
               for (auto& synthetic_rule : parsed_rules.synthetic_rules) {
                  result.rules.push_back(std::move(synthetic_rule));
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

         void parse_quantifier(symbol_quantifier& quantifier, std::size_t& exact_repetition) {
            skip_ignored();
            if (take("?")) {
               quantifier = symbol_quantifier::optional;
               exact_repetition = 1;
               return;
            }
            if (take("*")) {
               quantifier = symbol_quantifier::zero_or_more;
               exact_repetition = 0;
               return;
            }
            if (take("+")) {
               quantifier = symbol_quantifier::one_or_more;
               exact_repetition = 1;
               return;
            }
            if (!take("{")) {
               return;
            }

            skip_ignored();
            if (eof() || std::isdigit(static_cast<unsigned char>(current())) == 0) {
               throw error("Expected repetition count inside '{...}'");
            }

            std::string digits;
            while (!eof() && std::isdigit(static_cast<unsigned char>(current())) != 0) {
               digits += current();
               advance();
            }
            skip_ignored();
            expect("}");

            quantifier = symbol_quantifier::exact;
            exact_repetition = static_cast<std::size_t>(std::stoull(digits));
         }

         void parse_item_suffix(std::string& label, symbol_quantifier& quantifier, std::size_t& exact_repetition) {
            parse_quantifier(quantifier, exact_repetition);
            skip_ignored();
            if (!take(":")) {
               return;
            }

            label = parse_identifier();
            auto original_quantifier = quantifier;
            auto original_exact_repetition = exact_repetition;
            parse_quantifier(quantifier, exact_repetition);
            if ((quantifier != original_quantifier || exact_repetition != original_exact_repetition)
             && (original_quantifier != symbol_quantifier::one || original_exact_repetition != 1)) {
               throw error("A symbol can only have one repetition suffix");
            }
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
            parse_item_suffix(parsed_symbol.label, parsed_symbol.quantifier, parsed_symbol.exact_repetition);
            return parsed_symbol;
         }

         grouped_expression parse_group() {
            grouped_expression parsed_group;
            expect("(");
            parsed_group.alternatives = parse_alternatives("group", ')');
            expect(")");
            parse_item_suffix(parsed_group.label, parsed_group.quantifier, parsed_group.exact_repetition);
            return parsed_group;
         }

         grouped_sequence_item parse_sequence_item() {
            skip_ignored();
            grouped_sequence_item item;
            if (current() == '(') {
               item.group = std::make_unique<grouped_expression>(parse_group());
            } else {
               item.parsed_symbol = parse_symbol();
            }
            return item;
         }

         std::vector<grouped_sequence_item> parse_sequence(std::string_view context, char terminator) {
            std::vector<grouped_sequence_item> sequence;
            skip_ignored();
            while (!eof() && current() != '|' && current() != terminator) {
               sequence.push_back(parse_sequence_item());
               skip_ignored();
            }
            if (sequence.empty()) {
               throw error("Expected at least one symbol in " + std::string{context});
            }
            return sequence;
         }

         std::vector<std::vector<grouped_sequence_item>> parse_alternatives(std::string_view context, char terminator) {
            std::vector<std::vector<grouped_sequence_item>> alternatives;
            do {
               alternatives.push_back(parse_sequence(context, terminator));
            } while (take("|"));
            return alternatives;
         }

         [[nodiscard]] bool group_contains_nested_labeled_capture(const grouped_expression& group) const {
            for (const auto& alternative : group.alternatives) {
               for (const auto& item : alternative) {
                  if (item.parsed_symbol.has_value()) {
                     if (item.parsed_symbol->has_label()) {
                        return true;
                     }
                     continue;
                  }
                  if (!item.group->label.empty() || group_contains_nested_labeled_capture(*item.group)) {
                     return true;
                  }
               }
            }
            return false;
         }

         [[nodiscard]] static bool is_group_single(const grouped_expression& group) {
            return group.quantifier == symbol_quantifier::one;
         }

         [[nodiscard]] std::string make_group_rule_name() {
            return "$cpf_group_" + std::to_string(synthetic_rule_counter_++);
         }

         std::vector<std::vector<symbol>> lower_alternatives(
            const std::vector<std::vector<grouped_sequence_item>>& alternatives,
            std::size_t line,
            bool capture_allowed,
            std::vector<rule>& synthetic_rules) {
            std::vector<std::vector<symbol>> lowered;
            for (const auto& alternative : alternatives) {
               auto lowered_alternative = lower_sequence(alternative, line, capture_allowed, synthetic_rules);
               lowered.insert(lowered.end(), lowered_alternative.begin(), lowered_alternative.end());
            }
            return lowered;
         }

         std::vector<std::vector<symbol>> lower_sequence(
            const std::vector<grouped_sequence_item>& sequence,
            std::size_t line,
            bool capture_allowed,
            std::vector<rule>& synthetic_rules) {
            std::vector<std::vector<symbol>> lowered_sequences(1);
            for (const auto& item : sequence) {
               std::vector<std::vector<symbol>> lowered_item;
               if (item.parsed_symbol.has_value()) {
                  if (item.parsed_symbol->has_label() && !capture_allowed) {
                     throw error("Quantified groups cannot contain labeled captures");
                  }
                  lowered_item.push_back(std::vector<symbol>{*item.parsed_symbol});
               } else {
                  const auto& group = *item.group;
                  if (!group.label.empty() && !is_group_single(group)) {
                     throw error("Quantified labeled groups are not supported");
                  }

                  if (!is_group_single(group) || !group.label.empty()) {
                     if (group_contains_nested_labeled_capture(group)) {
                        if (!group.label.empty()) {
                           throw error("Labeled groups cannot contain labeled captures");
                        }
                        throw error("Quantified groups cannot contain labeled captures");
                     }

                     rule synthetic_rule;
                     synthetic_rule.identifier = make_group_rule_name();
                     synthetic_rule.synthetic = true;

                     auto lowered_group = lower_alternatives(group.alternatives, line, false, synthetic_rules);
                     if (!group.label.empty()) {
                        for (const auto& lowered_production : lowered_group) {
                           if (lowered_production.size() != 1 || !lowered_production.front().is_single()) {
                              throw error("Labeled groups must lower to exactly one symbol per alternative");
                           }
                           if (lowered_production.front().kind == symbol_kind::reference
                            && lowered_production.front().value.starts_with("$cpf_group_")) {
                              throw error("Labeled groups must lower directly to terminals or public rules");
                           }
                        }
                     }
                     synthetic_rule.productions.reserve(lowered_group.size());
                     for (const auto& lowered_production : lowered_group) {
                        production parsed_production;
                        parsed_production.symbols = lowered_production;
                        parsed_production.line = line;
                        parsed_production.definition = synthetic_rule.productions.size();
                        synthetic_rule.productions.push_back(std::move(parsed_production));
                     }
                     synthetic_rules.push_back(synthetic_rule);

                     symbol helper_symbol;
                     helper_symbol.kind = symbol_kind::reference;
                     helper_symbol.value = synthetic_rules.back().identifier;
                     helper_symbol.label = group.label;
                     helper_symbol.quantifier = group.quantifier;
                     helper_symbol.exact_repetition = group.exact_repetition;
                     lowered_item.push_back(std::vector<symbol>{std::move(helper_symbol)});
                  } else {
                     lowered_item = lower_alternatives(group.alternatives, line, capture_allowed, synthetic_rules);
                  }
               }

               std::vector<std::vector<symbol>> next_sequences;
               for (const auto& prefix : lowered_sequences) {
                  for (const auto& suffix : lowered_item) {
                     auto combined = prefix;
                     combined.insert(combined.end(), suffix.begin(), suffix.end());
                     next_sequences.push_back(std::move(combined));
                  }
               }
               lowered_sequences = std::move(next_sequences);
            }
            return lowered_sequences;
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

         parsed_rule_bundle parse_rule() {
            auto line = line_;
            parsed_rule_bundle parsed_rules;
            parsed_rules.main_rule.identifier = parse_identifier();
            current_rule_ = parsed_rules.main_rule.identifier;
            auto attributes = parse_attributes();
            expect("->");

            auto parsed_alternatives = parse_alternatives("production", ';');
            auto lowered_productions = lower_alternatives(parsed_alternatives, line, true, parsed_rules.synthetic_rules);
            for (const auto& lowered_symbols : lowered_productions) {
               production parsed_production;
               parsed_production.attributes = attributes;
               parsed_production.symbols = lowered_symbols;
               parsed_production.line = line;
               parsed_production.definition = parsed_rules.main_rule.productions.size();
               parsed_rules.main_rule.productions.push_back(std::move(parsed_production));
            }
            expect(";");
            current_rule_.clear();
            return parsed_rules;
         }

         std::string_view text_;
         std::size_t position_ = 0;
         std::size_t line_ = 1;
         std::string current_rule_;
         std::size_t synthetic_rule_counter_ = 0;
      };
   } // namespace

   grammar parse_grammar(std::string_view text) {
      return grammar_parser{text}.parse();
   }
} // namespace cpf


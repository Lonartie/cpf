#include "grammar_parser.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cpf {
   namespace {
      enum class parsed_symbol_kind { reference, literal, regex, template_invocation };

      struct grouped_expression;

      struct parsed_symbol {
         parsed_symbol_kind kind = parsed_symbol_kind::reference;
         std::string value;
         std::vector<std::string> template_arguments;
         std::string label;
         symbol_quantifier quantifier = symbol_quantifier::one;
         std::size_t exact_repetition = 1;
      };

      struct grouped_sequence_item {
         std::optional<parsed_symbol> parsed_symbol;
         std::unique_ptr<grouped_expression> group;
         lookahead_kind lookahead = lookahead_kind::none;
         bool cut = false;
      };

      struct grouped_expression {
         std::vector<std::vector<grouped_sequence_item>> alternatives;
         std::string label;
         symbol_quantifier quantifier = symbol_quantifier::one;
         std::size_t exact_repetition = 1;
      };

      struct parsed_rule_bundle {
         rule main_rule;
         std::vector<rule> synthetic_rules;
      };

      struct template_declaration {
         std::string identifier;
         std::vector<std::string> parameters;
         std::vector<std::vector<grouped_sequence_item>> alternatives;
         std::size_t line = 1;
      };

      using template_map = std::unordered_map<std::string, std::shared_ptr<const template_declaration>>;
      using parameter_bindings = std::unordered_map<std::string, std::shared_ptr<const grouped_sequence_item>>;

      [[nodiscard]] bool parsed_symbol_has_label(const parsed_symbol& symbol) { return !symbol.label.empty(); }

      [[nodiscard]] bool parsed_symbol_is_single(const parsed_symbol& symbol) {
         return symbol.quantifier == symbol_quantifier::one ||
                (symbol.quantifier == symbol_quantifier::exact && symbol.exact_repetition == 1);
      }

      [[nodiscard]] grouped_expression clone_expression(const grouped_expression& expression);

      [[nodiscard]] grouped_sequence_item clone_item(const grouped_sequence_item& item) {
         grouped_sequence_item clone;
         clone.lookahead = item.lookahead;
         clone.cut = item.cut;
         if (item.parsed_symbol.has_value()) {
            clone.parsed_symbol = *item.parsed_symbol;
         }
         if (item.group != nullptr) {
            clone.group = std::make_unique<grouped_expression>(clone_expression(*item.group));
         }
         return clone;
      }

      [[nodiscard]] grouped_expression clone_expression(const grouped_expression& expression) {
         grouped_expression clone;
         clone.label = expression.label;
         clone.quantifier = expression.quantifier;
         clone.exact_repetition = expression.exact_repetition;
         clone.alternatives.reserve(expression.alternatives.size());
         for (const auto& alternative: expression.alternatives) {
            auto cloned_alternative = std::vector<grouped_sequence_item>{};
            cloned_alternative.reserve(alternative.size());
            for (const auto& item: alternative) {
               cloned_alternative.push_back(clone_item(item));
            }
            clone.alternatives.push_back(std::move(cloned_alternative));
         }
         return clone;
      }

      void strip_labels_from_expression(grouped_expression& expression);

      void strip_labels_from_item(grouped_sequence_item& item) {
         if (item.parsed_symbol.has_value()) {
            item.parsed_symbol->label.clear();
         }
         if (item.group != nullptr) {
            strip_labels_from_expression(*item.group);
         }
      }

      void strip_labels_from_expression(grouped_expression& expression) {
         expression.label.clear();
         for (auto& alternative: expression.alternatives) {
            for (auto& item: alternative) {
               strip_labels_from_item(item);
            }
         }
      }

      [[nodiscard]] grouped_sequence_item make_negative_guard(const std::vector<grouped_sequence_item>& prefix) {
         grouped_sequence_item guard;
         guard.lookahead = lookahead_kind::negative;
         auto expression = grouped_expression{};
         auto guarded_prefix = std::vector<grouped_sequence_item>{};
         guarded_prefix.reserve(prefix.size());
         for (const auto& item: prefix) {
            guarded_prefix.push_back(clone_item(item));
         }
         expression.alternatives.push_back(std::move(guarded_prefix));
         guard.group = std::make_unique<grouped_expression>(std::move(expression));
         return guard;
      }

      [[nodiscard]] auto lower_cut_markers(const std::vector<std::vector<grouped_sequence_item>>& alternatives)
            -> std::vector<std::vector<grouped_sequence_item>> {
         auto lowered = std::vector<std::vector<grouped_sequence_item>>{};
         auto guard_prefixes = std::vector<std::vector<grouped_sequence_item>>{};
         auto committed_unconditionally = false;

         for (const auto& alternative: alternatives) {
            if (committed_unconditionally) {
               break;
            }

            auto cut_index = std::optional<std::size_t>{};
            for (std::size_t index = 0; index < alternative.size(); ++index) {
               if (!alternative[index].cut) {
                  continue;
               }
               if (cut_index.has_value()) {
                  throw std::runtime_error{"Grammar parse error: Productions may contain at most one !> cut marker"};
               }
               cut_index = index;
            }

            auto rewritten = std::vector<grouped_sequence_item>{};
            for (const auto& guard_prefix: guard_prefixes) {
               rewritten.push_back(make_negative_guard(guard_prefix));
            }
            for (std::size_t index = 0; index < alternative.size(); ++index) {
               if (cut_index.has_value() && index == *cut_index) {
                  continue;
               }
               rewritten.push_back(clone_item(alternative[index]));
            }

            if (rewritten.empty()) {
               throw std::runtime_error{"Grammar parse error: Cut markers cannot produce an empty alternative"};
            }
            lowered.push_back(std::move(rewritten));

            if (!cut_index.has_value()) {
               continue;
            }

            if (*cut_index == 0) {
               committed_unconditionally = true;
               continue;
            }

            auto prefix = std::vector<grouped_sequence_item>{};
            prefix.reserve(*cut_index);
            for (std::size_t index = 0; index < *cut_index; ++index) {
               prefix.push_back(clone_item(alternative[index]));
            }
            guard_prefixes.push_back(std::move(prefix));
         }

         return lowered;
      }

      void merge_placeholder_suffix(grouped_sequence_item& item, const parsed_symbol& placeholder) {
         auto apply_quantifier = [&](auto& target) {
            const auto placeholder_is_single = placeholder.quantifier == symbol_quantifier::one ||
                                               (placeholder.quantifier == symbol_quantifier::exact &&
                                                placeholder.exact_repetition == 1);
            const auto target_is_single = target.quantifier == symbol_quantifier::one ||
                                          (target.quantifier == symbol_quantifier::exact &&
                                           target.exact_repetition == 1);
            if (!placeholder_is_single && !target_is_single) {
               throw std::runtime_error{"Grammar parse error: Template placeholder and argument cannot both be quantified"};
            }
            if (!placeholder_is_single) {
               target.quantifier = placeholder.quantifier;
               target.exact_repetition = placeholder.exact_repetition;
            }
         };

         if (item.parsed_symbol.has_value()) {
            auto& symbol = *item.parsed_symbol;
            if (!placeholder.label.empty()) {
               if (!symbol.label.empty()) {
                  throw std::runtime_error{"Grammar parse error: Template placeholder label conflicts with an argument label"};
               }
               symbol.label = placeholder.label;
            }
            apply_quantifier(symbol);
            return;
         }

         auto& group = *item.group;
         if (!placeholder.label.empty()) {
            if (!group.label.empty()) {
               throw std::runtime_error{"Grammar parse error: Template placeholder label conflicts with an argument label"};
            }
            group.label = placeholder.label;
         }
         apply_quantifier(group);
      }

      class grammar_parser {
      public:
         explicit grammar_parser(std::string_view text) : text_{text} {}

         grammar parse() {
            grammar result;
            skip_ignored();
            while (!eof()) {
               if (take("@")) {
                  parse_directive(result);
               } else if (starts_with_declaration_keyword("skip")) {
                  auto parsed_skip_rule = parse_skip_rule();
                  if (result.find_skip_rule(parsed_skip_rule.identifier) != nullptr) {
                     throw error("Duplicate skip rule '" + parsed_skip_rule.identifier + "'");
                  }
                  result.skip_rules.push_back(std::move(parsed_skip_rule));
               } else if (starts_with_declaration_keyword("template")) {
                  auto parsed_template = parse_template_declaration();
                  if (templates_.contains(parsed_template->identifier)) {
                     throw error("Duplicate template '" + parsed_template->identifier + "'");
                  }
                  templates_.emplace(parsed_template->identifier, std::move(parsed_template));
               } else if (starts_with_declaration_keyword("token")) {
                  auto parsed_rules = parse_rule(true);
                  merge_rule_bundle(result, std::move(parsed_rules));
               } else {
                  auto parsed_rules = parse_rule(false);
                  merge_rule_bundle(result, std::move(parsed_rules));
               }
               skip_ignored();
            }
            validate_trivia(result);
            return result;
         }

      private:
         [[nodiscard]] static bool is_quote(char ch) { return ch == '\'' || ch == '"'; }

         [[nodiscard]] bool eof() const { return position_ >= text_.size(); }

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

         [[nodiscard]] char current() const { return eof() ? '\0' : text_[position_]; }

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

         [[nodiscard]] bool starts_with_keyword(std::string_view keyword) {
            skip_ignored();
            if (text_.substr(position_, keyword.size()) != keyword) {
               return false;
            }
            const auto end = position_ + keyword.size();
            if (end >= text_.size()) {
               return true;
            }
            const auto next = static_cast<unsigned char>(text_[end]);
            return std::isalnum(next) == 0 && text_[end] != '_';
         }

         [[nodiscard]] bool starts_with_declaration_keyword(std::string_view keyword) {
            if (!starts_with_keyword(keyword)) {
               return false;
            }

            auto probe = position_ + keyword.size();
            while (probe < text_.size() && std::isspace(static_cast<unsigned char>(text_[probe])) != 0) {
               ++probe;
            }
            if (probe + 1 < text_.size() && text_.substr(probe, 2) == "->") {
               return false;
            }
            return probe < text_.size() &&
                   (std::isalpha(static_cast<unsigned char>(text_[probe])) != 0 || text_[probe] == '_');
         }

         void expect_keyword(std::string_view keyword) {
            if (!starts_with_keyword(keyword)) {
               throw error("Expected keyword '" + std::string{keyword} + "'");
            }
            for (std::size_t index = 0; index < keyword.size(); ++index) {
               advance();
            }
         }

         void expect(std::string_view token) {
            if (!take(token)) {
               throw error("Expected '" + std::string{token} + "'");
            }
         }

         [[nodiscard]] std::runtime_error error(const std::string& message) const {
            auto full_message = "Grammar parse error at line " + std::to_string(line_) + ", column " +
                                std::to_string(column()) + ": " + message + " but found " + found_token();
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
            if (!is_quote(current())) {
               throw error("Expected quoted string");
            }
            auto quote = current();
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
                     case 'n':
                        value += '\n';
                        break;
                     case 'r':
                        value += '\r';
                        break;
                     case 't':
                        value += '\t';
                        break;
                     case '\\':
                        value += '\\';
                        break;
                     case '\'':
                        value += '\'';
                        break;
                     case '"':
                        value += '"';
                        break;
                     default:
                        value += escaped;
                        break;
                  }
                  advance();
                  continue;
               }
               if (ch == quote) {
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
            if (is_quote(current())) {
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
            if ((quantifier != original_quantifier || exact_repetition != original_exact_repetition) &&
                (original_quantifier != symbol_quantifier::one || original_exact_repetition != 1)) {
               throw error("A symbol can only have one repetition suffix");
            }
         }

         [[nodiscard]] auto parse_template_argument_texts() -> std::vector<std::string> {
            auto arguments = std::vector<std::string>{};
            skip_ignored();
            if (current() == '>') {
               return arguments;
            }

            while (true) {
               skip_ignored();
               const auto start = position_;
               auto argument = parse_sequence_item();
               if (argument.cut) {
                  throw error("Template arguments cannot be cut markers");
               }
               const auto end = position_;
               arguments.emplace_back(text_.substr(start, end - start));
               skip_ignored();
               if (!take(",")) {
                  break;
               }
            }
            return arguments;
         }

         parsed_symbol parse_symbol() {
            skip_ignored();
            parsed_symbol parsed_symbol;
            if (current() == 'r' && position_ + 1 < text_.size() && is_quote(text_[position_ + 1])) {
               advance();
               parsed_symbol.kind = parsed_symbol_kind::regex;
               parsed_symbol.value = parse_quoted();
            } else if (is_quote(current())) {
               parsed_symbol.kind = parsed_symbol_kind::literal;
               parsed_symbol.value = parse_quoted();
            } else {
               parsed_symbol.value = parse_identifier();
               if (take("<")) {
                  parsed_symbol.kind = parsed_symbol_kind::template_invocation;
                  parsed_symbol.template_arguments = parse_template_argument_texts();
                  expect(">");
               } else {
                  parsed_symbol.kind = parsed_symbol_kind::reference;
               }
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
            if (take("!>")) {
               item.cut = true;
               return item;
            }
            if (take("&")) {
               item.lookahead = lookahead_kind::positive;
            } else if (take("!")) {
               item.lookahead = lookahead_kind::negative;
            }
            if (current() == '(') {
               item.group = std::make_unique<grouped_expression>(parse_group());
               if (item.lookahead != lookahead_kind::none && !item.group->label.empty()) {
                  throw error("Lookahead predicates cannot expose labeled captures");
               }
            } else {
               item.parsed_symbol = parse_symbol();
               if (item.lookahead != lookahead_kind::none && parsed_symbol_has_label(*item.parsed_symbol)) {
                  throw error("Lookahead predicates cannot expose labeled captures");
               }
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
            for (const auto& alternative: group.alternatives) {
               for (const auto& item: alternative) {
                  if (item.parsed_symbol.has_value()) {
                     if (parsed_symbol_has_label(*item.parsed_symbol)) {
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

         [[nodiscard]] std::string make_group_rule_name() { return "cpf_group_" + std::to_string(synthetic_rule_counter_++); }

         [[nodiscard]] std::string make_template_rule_name(std::string_view template_name) {
            return "cpf_template_" + std::string{template_name} + "_" + std::to_string(synthetic_rule_counter_++);
         }

         [[nodiscard]] grouped_sequence_item parse_item_from_text(std::string_view text) const {
            grammar_parser nested{text};
            nested.templates_ = templates_;
            auto item = nested.parse_sequence_item();
            nested.skip_ignored();
            if (!nested.eof()) {
               throw std::runtime_error{"Grammar parse error: Template argument must contain exactly one item"};
            }
            return item;
         }

         [[nodiscard]] std::string resolve_template_name(std::string_view identifier,
                                                         const parameter_bindings* bindings) {
            auto template_name = std::string{identifier};
            if (bindings == nullptr) {
               return template_name;
            }

            auto binding = bindings->find(template_name);
            if (binding == bindings->end()) {
               return template_name;
            }

            const auto& item = *binding->second;
            if (item.cut) {
               throw error("Template parameters cannot expand to cut markers");
            }
            if (item.lookahead != lookahead_kind::none || item.group != nullptr || !item.parsed_symbol.has_value()) {
               throw error("Template parameter '" + template_name + "' must bind to a template identifier");
            }

            const auto& symbol = *item.parsed_symbol;
            if (symbol.kind != parsed_symbol_kind::reference || parsed_symbol_has_label(symbol) ||
                !parsed_symbol_is_single(symbol)) {
               throw error("Template parameter '" + template_name + "' must bind to a template identifier");
            }

            return symbol.value;
         }

         std::vector<std::vector<symbol>> lower_item(const grouped_sequence_item& item, std::size_t line,
                                                     bool capture_allowed, std::vector<rule>& synthetic_rules,
                                                     const parameter_bindings* bindings) {
            if (item.cut) {
               throw error("Cut markers must be lowered before symbol emission");
            }

            if (item.parsed_symbol.has_value() && bindings != nullptr &&
                item.parsed_symbol->kind == parsed_symbol_kind::reference &&
                item.parsed_symbol->template_arguments.empty()) {
               if (auto binding = bindings->find(item.parsed_symbol->value); binding != bindings->end()) {
                  auto substituted = clone_item(*binding->second);
                  if (substituted.cut) {
                     throw error("Template parameters cannot expand to cut markers");
                  }
                  if (item.lookahead != lookahead_kind::none) {
                     if (substituted.lookahead != lookahead_kind::none) {
                        throw error("Template placeholder and argument cannot both use lookahead");
                     }
                     substituted.lookahead = item.lookahead;
                  }
                  merge_placeholder_suffix(substituted, *item.parsed_symbol);
                  return lower_item(substituted, line, capture_allowed, synthetic_rules, nullptr);
               }
            }

            if (item.parsed_symbol.has_value()) {
               symbol lowered_symbol = materialize_symbol(*item.parsed_symbol, line, synthetic_rules, bindings);
               if (item.lookahead != lookahead_kind::none) {
                  lowered_symbol.label.clear();
                  lowered_symbol.lookahead = item.lookahead;
               }
               if (lowered_symbol.has_label() && !capture_allowed) {
                  throw error("Quantified groups cannot contain labeled captures");
               }
               return std::vector<std::vector<symbol>>{std::vector<symbol>{std::move(lowered_symbol)}};
            }

            auto group = clone_expression(*item.group);
            if (item.lookahead != lookahead_kind::none) {
               strip_labels_from_expression(group);
            }

            if (item.lookahead == lookahead_kind::none && group.label.empty() &&
                !is_group_single(group) && group_contains_nested_labeled_capture(group)) {
               throw error("Quantified groups cannot contain labeled captures");
            }

            if (item.lookahead != lookahead_kind::none || !is_group_single(group) || !group.label.empty()) {
               rule synthetic_rule;
               synthetic_rule.identifier = make_group_rule_name();
               synthetic_rule.synthetic = true;

               const auto lowered_group =
                     lower_alternatives(group.alternatives, line,
                                        item.lookahead == lookahead_kind::none && !group.label.empty(),
                                        synthetic_rules, bindings);
               synthetic_rule.productions.reserve(lowered_group.size());
               for (const auto& lowered_production: lowered_group) {
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
               helper_symbol.label = item.lookahead == lookahead_kind::none ? group.label : std::string{};
               helper_symbol.quantifier = group.quantifier;
               helper_symbol.exact_repetition = group.exact_repetition;
               helper_symbol.lookahead = item.lookahead;
               return std::vector<std::vector<symbol>>{std::vector<symbol>{std::move(helper_symbol)}};
            }

            return lower_alternatives(group.alternatives, line, capture_allowed, synthetic_rules, bindings);
         }

         std::vector<std::vector<symbol>> lower_alternatives(
               const std::vector<std::vector<grouped_sequence_item>>& alternatives, std::size_t line,
               bool capture_allowed, std::vector<rule>& synthetic_rules, const parameter_bindings* bindings = nullptr) {
            std::vector<std::vector<symbol>> lowered;
            for (const auto& alternative: alternatives) {
               auto lowered_alternative = lower_sequence(alternative, line, capture_allowed, synthetic_rules, bindings);
               lowered.insert(lowered.end(), lowered_alternative.begin(), lowered_alternative.end());
            }
            return lowered;
         }

         std::vector<std::vector<symbol>> lower_sequence(const std::vector<grouped_sequence_item>& sequence,
                                                         std::size_t line, bool capture_allowed,
                                                         std::vector<rule>& synthetic_rules,
                                                         const parameter_bindings* bindings = nullptr) {
            std::vector<std::vector<symbol>> lowered_sequences(1);
            for (const auto& item: sequence) {
               auto lowered_item = lower_item(item, line, capture_allowed, synthetic_rules, bindings);

               std::vector<std::vector<symbol>> next_sequences;
               for (const auto& prefix: lowered_sequences) {
                  for (const auto& suffix: lowered_item) {
                     auto combined = prefix;
                     combined.insert(combined.end(), suffix.begin(), suffix.end());
                     next_sequences.push_back(std::move(combined));
                  }
               }
               lowered_sequences = std::move(next_sequences);
            }
            return lowered_sequences;
         }

         symbol materialize_symbol(const parsed_symbol& parsed_symbol, std::size_t line,
                                   std::vector<rule>& synthetic_rules, const parameter_bindings* bindings) {
            symbol lowered_symbol;
            lowered_symbol.label = parsed_symbol.label;
            lowered_symbol.quantifier = parsed_symbol.quantifier;
            lowered_symbol.exact_repetition = parsed_symbol.exact_repetition;

            switch (parsed_symbol.kind) {
               case parsed_symbol_kind::reference:
                  lowered_symbol.kind = symbol_kind::reference;
                  lowered_symbol.value = parsed_symbol.value;
                  return lowered_symbol;
               case parsed_symbol_kind::literal:
                  lowered_symbol.kind = symbol_kind::literal;
                  lowered_symbol.value = parsed_symbol.value;
                  return lowered_symbol;
               case parsed_symbol_kind::regex:
                  lowered_symbol.kind = symbol_kind::regex;
                  lowered_symbol.value = parsed_symbol.value;
                  return lowered_symbol;
               case parsed_symbol_kind::template_invocation:
                  lowered_symbol.kind = symbol_kind::reference;
                  lowered_symbol.value = instantiate_template(parsed_symbol, line, synthetic_rules, bindings);
                  return lowered_symbol;
            }
            return lowered_symbol;
         }

         std::string instantiate_template(const parsed_symbol& invocation, std::size_t line,
                                          std::vector<rule>& synthetic_rules, const parameter_bindings* bindings) {
            const auto template_name = resolve_template_name(invocation.value, bindings);
            auto template_it = templates_.find(template_name);
            if (template_it == templates_.end()) {
               throw error("Unknown template '" + template_name + "'");
            }

            const auto& declaration = *template_it->second;
            if (invocation.template_arguments.size() != declaration.parameters.size()) {
               throw error("Template '" + template_name + "' expects " +
                           std::to_string(declaration.parameters.size()) + " argument(s)");
            }

            parameter_bindings template_bindings;
            for (std::size_t index = 0; index < declaration.parameters.size(); ++index) {
               const auto& argument_text = invocation.template_arguments[index];
               grouped_sequence_item argument;
               if (bindings != nullptr) {
                  if (auto binding = bindings->find(argument_text); binding != bindings->end()) {
                     argument = clone_item(*binding->second);
                  } else {
                     argument = parse_item_from_text(argument_text);
                  }
               } else {
                  argument = parse_item_from_text(argument_text);
               }
               if (argument.cut) {
                  throw error("Template arguments cannot expand to cut markers");
               }
               template_bindings.emplace(declaration.parameters[index],
                                         std::make_shared<grouped_sequence_item>(std::move(argument)));
            }

            rule synthetic_rule;
            synthetic_rule.identifier = make_template_rule_name(template_name);
            synthetic_rule.synthetic = true;

            const auto lowered_alternatives =
                  lower_alternatives(declaration.alternatives, line, true, synthetic_rules, &template_bindings);
            synthetic_rule.productions.reserve(lowered_alternatives.size());
            for (const auto& lowered_production: lowered_alternatives) {
               production parsed_production;
               parsed_production.symbols = lowered_production;
               parsed_production.line = line;
               parsed_production.definition = synthetic_rule.productions.size();
               synthetic_rule.productions.push_back(std::move(parsed_production));
            }

            synthetic_rules.push_back(std::move(synthetic_rule));
            return synthetic_rules.back().identifier;
         }

         parsed_rule_bundle parse_rule(bool declared_as_token) {
            auto line = line_;
            parsed_rule_bundle parsed_rules;
            if (declared_as_token) {
               expect_keyword("token");
               skip_ignored();
            }
            parsed_rules.main_rule.identifier = parse_identifier();
            parsed_rules.main_rule.declared_as_token = declared_as_token;
            current_rule_ = parsed_rules.main_rule.identifier;
            auto attributes = parse_attributes();
            if (declared_as_token && !attributes.empty()) {
               throw error("Token declarations do not support rule attributes");
            }
            expect("->");

            auto parsed_alternatives = parse_alternatives("production", ';');
            parsed_alternatives = lower_cut_markers(parsed_alternatives);
            auto lowered_productions = lower_alternatives(parsed_alternatives, line, true, parsed_rules.synthetic_rules);
            for (const auto& lowered_symbols: lowered_productions) {
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

         [[nodiscard]] std::shared_ptr<const template_declaration> parse_template_declaration() {
            expect_keyword("template");
            skip_ignored();

            auto declaration = std::make_shared<template_declaration>();
            declaration->line = line_;
            declaration->identifier = parse_identifier();
            current_rule_ = declaration->identifier;
            expect("<");
            skip_ignored();
            if (current() != '>') {
               while (true) {
                  declaration->parameters.push_back(parse_identifier());
                  skip_ignored();
                  if (!take(",")) {
                     break;
                  }
               }
            }
            expect(">");
            expect("->");
            declaration->alternatives = lower_cut_markers(parse_alternatives("template production", ';'));
            expect(";");
            current_rule_.clear();
            return declaration;
         }

         skip_rule parse_skip_rule() {
            expect_keyword("skip");
            skip_rule parsed_rule;
            parsed_rule.line = line_;
            parsed_rule.identifier = parse_identifier();
            current_rule_ = parsed_rule.identifier;
            expect("->");
            auto parsed_symbol = parse_symbol();
            if (parsed_symbol.kind == parsed_symbol_kind::reference ||
                parsed_symbol.kind == parsed_symbol_kind::template_invocation) {
               throw error("Skip rules must lower directly to a literal or regex terminal");
            }
            if (parsed_symbol_has_label(parsed_symbol)) {
               throw error("Skip rules cannot expose labeled captures");
            }
            if (!parsed_symbol_is_single(parsed_symbol)) {
               throw error("Skip rules cannot be quantified");
            }
            parsed_rule.kind = parsed_symbol.kind == parsed_symbol_kind::literal ? symbol_kind::literal : symbol_kind::regex;
            parsed_rule.value = parsed_symbol.value;
            expect(";");
            current_rule_.clear();
            return parsed_rule;
         }

         void parse_directive(grammar& result) {
            auto directive = parse_identifier();
            if (directive == "whitespace") {
               auto line = line_;
               auto identifier = parse_identifier();
               if (result.whitespace_rule.has_value()) {
                  throw error("Duplicate @whitespace directive");
               }
               result.whitespace_rule = std::move(identifier);
               result.whitespace_rule_line = line;
               expect(";");
               return;
            }
            if (directive == "namespace") {
               throw error("Grammar directive '@namespace' is not supported; use the existing code-generation namespace options");
            }
            throw error("Unknown grammar directive '@" + directive + "'");
         }

         void merge_rule_bundle(grammar& result, parsed_rule_bundle parsed_rules) {
            auto& parsed_rule = parsed_rules.main_rule;
            if (auto* existing = result.find_rule(parsed_rule.identifier); existing != nullptr) {
               if (existing->declared_as_token != parsed_rule.declared_as_token) {
                  throw error("Rule '" + parsed_rule.identifier + "' cannot be declared as both token and non-token");
               }
               const auto definition_offset = existing->productions.size();
               for (auto& production: parsed_rule.productions) {
                  production.definition += definition_offset;
               }
               existing->productions.insert(existing->productions.end(), parsed_rule.productions.begin(),
                                            parsed_rule.productions.end());
            } else {
               result.rules.push_back(std::move(parsed_rule));
            }
            for (auto& synthetic_rule: parsed_rules.synthetic_rules) {
               result.rules.push_back(std::move(synthetic_rule));
            }
         }

         void validate_trivia(const grammar& parsed_grammar) const {
            if (!parsed_grammar.whitespace_rule.has_value()) {
               return;
            }
            if (parsed_grammar.find_skip_rule(*parsed_grammar.whitespace_rule) != nullptr) {
               return;
            }
            throw std::runtime_error{"Grammar parse error at line " +
                                     std::to_string(parsed_grammar.whitespace_rule_line) +
                                     ", column 1: @whitespace references unknown skip rule '" +
                                     *parsed_grammar.whitespace_rule + "'"};
         }

         std::string_view text_;
         std::size_t position_ = 0;
         std::size_t line_ = 1;
         std::string current_rule_;
         std::size_t synthetic_rule_counter_ = 0;
         template_map templates_;
      };
   } // namespace

   grammar parse_grammar(std::string_view text) { return grammar_parser{text}.parse(); }
} // namespace cpf

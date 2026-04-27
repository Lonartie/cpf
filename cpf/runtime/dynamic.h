#pragma once

#include "analysis/grammar_analysis.h"
#include "model/grammar.h"
#include "parser/grammar_loader.h"
#include "parser/grammar_parser.h"
#include "runtime/api.h"

#include <filesystem>
#include <iosfwd>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cpf {
   /// @brief Describes the declared storage shape of one dynamic AST field.
   enum class dynamic_field_shape {
      terminal_scalar,
      terminal_optional,
      terminal_vector,
      node_scalar,
      node_vector,
      capture_variant
   };

   /// @brief Describes the value shape currently stored in one dynamic AST field.
   enum class dynamic_field_value_kind { empty, token, node, token_list, node_list };

   struct dynamic_node;

   /// @brief One named field captured on a dynamic runtime AST node.
   struct dynamic_field {
      /// @brief Grammar label associated with this field.
      std::string name;
      /// @brief Declared storage shape derived from the grammar.
      dynamic_field_shape shape = dynamic_field_shape::terminal_scalar;
      /// @brief Actual stored value kind for the current parse.
      dynamic_field_value_kind value_kind = dynamic_field_value_kind::empty;
      /// @brief Declared static type name derived from the grammar.
      std::string declared_type_name;
      /// @brief Actual runtime type name for the current value when available.
      std::string value_type_name;
      /// @brief Declared alternative type names for variant captures.
      std::vector<std::string> alternative_type_names;
      /// @brief Captured scalar terminal value for token-like fields.
      std::optional<matched_string> token;
      /// @brief Captured scalar node value for node-like fields.
      std::unique_ptr<dynamic_node> node;
      /// @brief Captured repeated terminal values.
      std::vector<matched_string> tokens;
      /// @brief Captured repeated node values.
      std::vector<std::unique_ptr<dynamic_node>> nodes;

      dynamic_field() = default;
      dynamic_field(dynamic_field&&) = default;
      auto operator=(dynamic_field&&) -> dynamic_field& = default;
      dynamic_field(const dynamic_field&) = delete;
      auto operator=(const dynamic_field&) -> dynamic_field& = delete;

      /// @brief Deep-copies the field and any nested dynamic nodes.
      /// @return Independent cloned field value.
      [[nodiscard]] auto clone() const -> dynamic_field;
   };

   /// @brief Generic runtime AST node produced by dynamic parser compilation.
   struct dynamic_node : node {
      /// @brief Stable rule id associated with `rule_name`.
      std::size_t rule = 0;
      /// @brief Grammar rule name that materialized this node.
      std::string rule_name;
      /// @brief Named captured fields for the selected production keyed by grammar label.
      std::map<std::string, dynamic_field> fields;

      dynamic_node() = default;
      dynamic_node(dynamic_node&&) = default;
      auto operator=(dynamic_node&&) -> dynamic_node& = default;
      dynamic_node(const dynamic_node&) = delete;
      auto operator=(const dynamic_node&) -> dynamic_node& = delete;
      ~dynamic_node() override = default;

      [[nodiscard]] auto rule_id() const -> std::size_t override { return rule; }
      [[nodiscard]] auto clone() const -> std::unique_ptr<dynamic_node>;

      /// @brief Finds one field by name.
      /// @param name Grammar label to search for.
      /// @return Matching field pointer or nullptr when absent.
      [[nodiscard]] auto get_field(std::string_view name) -> dynamic_field*;

      /// @brief Finds one field by name.
      /// @param name Grammar label to search for.
      /// @return Matching field pointer or nullptr when absent.
      [[nodiscard]] auto get_field(std::string_view name) const -> const dynamic_field*;

      /// @brief Returns the exact source slice covered by this node.
      /// @param input Original input text used to produce the node.
      /// @return Source substring spanning `range`.
      [[nodiscard]] auto source_text(std::string_view input) const -> std::string;

   protected:
      [[nodiscard]] auto clone_node() const -> std::unique_ptr<node> override;
   };

   /// @brief Streams a multiline debug representation of a dynamic AST subtree.
   std::ostream& operator<<(std::ostream& os, const dynamic_node& node);

   template<typename Visitor>
   auto visit(const dynamic_node& node, Visitor&& visitor) {
      return std::forward<Visitor>(visitor)(node);
   }

   template<typename Visitor>
   auto visit(dynamic_node& node, Visitor&& visitor) {
      return std::forward<Visitor>(visitor)(node);
   }

   namespace detail {
      template<typename Node, typename Parent, typename Visitor>
      void invoke_dynamic_recursive_visitor(Node& node, Parent* parent, Visitor&& visitor) {
         if constexpr (std::is_invocable_v<Visitor&&, Node&, Parent*>) {
            std::forward<Visitor>(visitor)(node, parent);
         } else {
            std::forward<Visitor>(visitor)(node);
         }
      }
   }

   template<typename Visitor>
   void visit_recursive(const dynamic_node& node, Visitor&& visitor) {
      visit_recursive(node, std::forward<Visitor>(visitor), static_cast<const dynamic_node*>(nullptr));
   }

   template<typename Visitor, typename Parent>
   void visit_recursive(const dynamic_node& node, Visitor&& visitor, const Parent* parent) {
      detail::invoke_dynamic_recursive_visitor(node, parent, std::forward<Visitor>(visitor));
      for (const auto& [field_name, field]: node.fields) {
         (void) field_name;
         if (field.node != nullptr) {
            visit_recursive(*field.node, visitor, &node);
         }
         for (const auto& child: field.nodes) {
            if (child != nullptr) {
               visit_recursive(*child, visitor, &node);
            }
         }
      }
   }

   template<typename Visitor>
   void visit_recursive(dynamic_node& node, Visitor&& visitor) {
      visit_recursive(node, std::forward<Visitor>(visitor), static_cast<dynamic_node*>(nullptr));
   }

   template<typename Visitor, typename Parent>
   void visit_recursive(dynamic_node& node, Visitor&& visitor, Parent* parent) {
      detail::invoke_dynamic_recursive_visitor(node, parent, std::forward<Visitor>(visitor));
      for (auto& [field_name, field]: node.fields) {
         (void) field_name;
         if (field.node != nullptr) {
            visit_recursive(*field.node, visitor, &node);
         }
         for (auto& child: field.nodes) {
            if (child != nullptr) {
               visit_recursive(*child, visitor, &node);
            }
         }
      }
   }

   template<typename Visitor>
   void visit_dynamic_recursive(const dynamic_node& node, Visitor&& visitor) {
      visit_recursive(node, std::forward<Visitor>(visitor));
   }

   template<typename Visitor>
   void visit_dynamic_recursive(dynamic_node& node, Visitor&& visitor) {
      visit_recursive(node, std::forward<Visitor>(visitor));
   }

   /// @brief Metadata describing one public rule available on a compiled runtime parser.
   struct dynamic_rule_info {
      std::size_t id = 0;
      std::string name;
      bool declared_as_token = false;
      bool base_rule = false;
      std::size_t production_count = 0;
   };

   /// @brief Runtime-compiled grammar that can lex, recognize, and parse without code generation.
   class parser {
   public:
      using dynamic_parse_result = parse_result<dynamic_node>;
      using cst_parse_result = parse_result<cst_node>;

      parser() = default;
      parser(parser&&) noexcept = default;
      auto operator=(parser&&) noexcept -> parser& = default;
      parser(const parser&) = default;
      auto operator=(const parser&) -> parser& = default;
      ~parser();

      /// @brief True when this object does not reference a compiled grammar.
      [[nodiscard]] auto empty() const -> bool;

      /// @brief Parsed source grammar used to build this runtime artifact.
      [[nodiscard]] auto source_grammar() const -> const grammar&;

      /// @brief Static diagnostics collected while compiling the grammar.
      [[nodiscard]] auto analysis() const -> const grammar_analysis&;

      /// @brief Primary entry rule inferred from the grammar analysis.
      [[nodiscard]] auto primary_entry_rule() const -> std::string_view;

      /// @brief Public non-synthetic rules exposed by this runtime parser.
      [[nodiscard]] auto rules() const -> std::span<const dynamic_rule_info>;

      /// @brief Looks up one public rule by name.
      /// @param name Rule identifier.
      /// @return Matching rule metadata or nullptr when absent.
      [[nodiscard]] auto find_rule(std::string_view name) const -> const dynamic_rule_info*;

      /// @brief Lexes one input according to the compiled grammar.
      /// @param input Source text.
      /// @return Reusable token sequence carrying original source ranges.
      [[nodiscard]] auto lex(std::string_view input) const -> token_sequence;

      /// @brief Recognizes input against the primary entry rule.
      [[nodiscard]] auto recognize(std::string_view input) const -> recognize_result;
      /// @brief Recognizes tokenized input against the primary entry rule.
      [[nodiscard]] auto recognize(const token_sequence& tokens) const -> recognize_result;
      /// @brief Recognizes input against a caller-chosen public rule.
      [[nodiscard]] auto recognize(std::string_view root_rule, std::string_view input) const -> recognize_result;
      /// @brief Recognizes tokenized input against a caller-chosen public rule.
      [[nodiscard]] auto recognize(std::string_view root_rule, const token_sequence& tokens) const -> recognize_result;

      /// @brief Parses input into the generic runtime AST for the primary entry rule.
      [[nodiscard]] auto parse(std::string_view input, const parse_options& options = {}) const -> dynamic_parse_result;
      /// @brief Parses tokenized input into the generic runtime AST for the primary entry rule.
      [[nodiscard]] auto parse(const token_sequence& tokens, const parse_options& options = {}) const -> dynamic_parse_result;
      /// @brief Parses input into the generic runtime AST for a caller-chosen public rule.
      [[nodiscard]] auto parse(std::string_view root_rule, std::string_view input,
                               const parse_options& options = {}) const -> dynamic_parse_result;
      /// @brief Parses tokenized input into the generic runtime AST for a caller-chosen public rule.
      [[nodiscard]] auto parse(std::string_view root_rule, const token_sequence& tokens,
                               const parse_options& options = {}) const -> dynamic_parse_result;

      /// @brief Parses input into the generic runtime CST for the primary entry rule.
      [[nodiscard]] auto parse_cst(std::string_view input, const parse_options& options = {}) const -> cst_parse_result;
      /// @brief Parses tokenized input into the generic runtime CST for the primary entry rule.
      [[nodiscard]] auto parse_cst(const token_sequence& tokens, const parse_options& options = {}) const -> cst_parse_result;
      /// @brief Parses input into the generic runtime CST for a caller-chosen public rule.
      [[nodiscard]] auto parse_cst(std::string_view root_rule, std::string_view input,
                                   const parse_options& options = {}) const -> cst_parse_result;
      /// @brief Parses tokenized input into the generic runtime CST for a caller-chosen public rule.
      [[nodiscard]] auto parse_cst(std::string_view root_rule, const token_sequence& tokens,
                                   const parse_options& options = {}) const -> cst_parse_result;

   private:
      struct impl;

      explicit parser(std::shared_ptr<const impl> impl);
      [[nodiscard]] auto implementation() const -> const impl&;

      std::shared_ptr<const impl> m_impl;

      friend auto compile_grammar(const grammar& grammar) -> parser;
      friend auto compile_grammar(std::string_view source) -> parser;
      friend auto compile_grammar(const loaded_grammar& grammar) -> parser;
      friend auto compile_grammar_file(const std::filesystem::path& path) -> parser;
   };

   /// @brief Compiles a parsed grammar into an in-memory runtime parser.
   [[nodiscard]] auto compile_grammar(const grammar& grammar) -> parser;

   /// @brief Parses and compiles grammar source text into an in-memory runtime parser.
   [[nodiscard]] auto compile_grammar(std::string_view source) -> parser;

   /// @brief Compiles a loaded grammar with imports already expanded.
   [[nodiscard]] auto compile_grammar(const loaded_grammar& grammar) -> parser;

   /// @brief Loads a grammar file and compiles it into an in-memory runtime parser.
   [[nodiscard]] auto compile_grammar_file(const std::filesystem::path& path) -> parser;
} // namespace cpf




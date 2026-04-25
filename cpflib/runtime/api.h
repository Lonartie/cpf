#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

namespace cpf {
   /// @brief Describes the furthest parse failure that occurred while parsing input.
   struct parse_error {
      /// @brief Zero-based byte offset of the parse error.
      std::size_t offset = 0;
      /// @brief One-based line number of the parse error.
      std::size_t line = 1;
      /// @brief One-based column number of the parse error.
      std::size_t column = 1;
      /// @brief Tokens or grammar elements that would have been accepted.
      std::vector<std::string> expected;
      /// @brief Token that was found at the point of failure.
      std::string found = "<end of input>";
      /// @brief Additional contextual notes explaining why a token or rule was expected.
      std::vector<std::string> notes;
      /// @brief Human-readable error message.
      std::string message = "Parse error";
   };

   /// @brief Options that control generated parser behavior.
   struct parse_options {
      /// @brief When false, parsing validates syntax and constraints without materializing the AST forest.
      bool build_ast = true;
      /// @brief When true, parsing fails as soon as multiple valid derivations are detected.
      bool error_on_ambiguity = false;
      /// @brief When true, parsing may return damaged AST trees recovered around syntax errors.
      bool allow_partial = false;
   };

   /// @brief Result of validating whether an input matches a generated grammar rule without building an AST.
   struct recognize_result {
      /// @brief True when the input was fully recognized.
      bool success = false;
      /// @brief Optional failure details when recognition did not succeed.
      std::optional<parse_error> error;
   };

   /// @brief One position within an input source.
   struct source_position {
      /// @brief Zero-based byte offset in the original input.
      std::size_t offset = 0;
      /// @brief One-based line number.
      std::size_t line = 1;
      /// @brief One-based column number.
      std::size_t column = 1;
   };

   /// @brief Half-open source range covering a parsed match.
   struct source_range {
      source_position begin;
      source_position end;
   };

   /// @brief Classifies how a damaged parse region was recovered.
   enum class node_damage_reason {
      ignored_invalid_input,
      inserted_virtual_token
   };

   /// @brief Returns a stable textual name for one node damage reason.
   [[nodiscard]] constexpr auto to_string(node_damage_reason reason) -> std::string_view {
      switch (reason) {
         case node_damage_reason::ignored_invalid_input:
            return "ignored invalid input";
         case node_damage_reason::inserted_virtual_token:
            return "inserted virtual token";
      }
      return "unknown";
   }

   /// @brief Captured terminal text together with the source range it matched.
   struct matched_string {
      std::string text;
      source_range range;
   };

   /// @brief Damage annotation attached to a partially recovered AST node.
   struct node_damage {
      /// @brief Source range associated with the damage.
      source_range range;
      /// @brief Structured classification of the recovery action.
      node_damage_reason reason = node_damage_reason::ignored_invalid_input;
      /// @brief Optional human-readable detail associated with the recovery action.
      std::string detail;
      /// @brief Human-readable explanation of why recovery was needed and how it was applied.
      std::string message;
   };

   /// @brief Base class for all generated model nodes.
   struct node {
      /// @brief Zero-based production index within the rule that created this node.
      std::size_t definition = 0;
      /// @brief Source range that produced this node.
      source_range range;

      virtual ~node() = default;

      /// @brief Returns the generated rule identifier of the concrete node.
      /// @return Stable rule id used for constant-time dispatch in generated visitors.
      [[nodiscard]] virtual std::size_t rule_id() const = 0;

      /// @brief Returns the dynamic type of the concrete node.
      /// @return The type information of the concrete node instance.
      [[nodiscard]] virtual const std::type_info& type() const = 0;

      /// @brief True when this node was recovered from a damaged parse.
      [[nodiscard]] auto is_damaged() const -> bool { return !m_damage.empty(); }

      /// @brief Damage annotations attached to this node.
      [[nodiscard]] auto damage() const -> const std::vector<node_damage>& { return m_damage; }

      /// @brief Adds one damage annotation to this node.
      void add_damage(node_damage damage) { m_damage.push_back(std::move(damage)); }

   protected:
      /// @brief Clones the concrete node through the base interface.
      /// @return A newly allocated deep copy of the node.
      [[nodiscard]] virtual std::unique_ptr<node> clone_node() const = 0;

      template<typename T> friend class parse_tree;

      void copy_damage_to(node& other) const { other.m_damage = m_damage; }

   private:
      std::vector<node_damage> m_damage;
   };

   template<typename T> class parse_tree;

   namespace detail {
      template<typename T> [[nodiscard]] auto opaque_tree_of(const parse_tree<T>& tree) -> std::shared_ptr<const void>;
   }

   /// @brief Lazy handle for one parse tree in a returned forest.
   /// @tparam T Root node type materialized from the opaque runtime tree.
   template<typename T> class parse_tree {
   public:
      parse_tree() = default;

      parse_tree(std::unique_ptr<T> eager_tree) :
          definition{eager_tree != nullptr ? eager_tree->definition : 0},
          range{eager_tree != nullptr ? eager_tree->range : source_range{}} {
         if (eager_tree != nullptr) {
            m_materialized = std::shared_ptr<T>{eager_tree.release()};
         }
      }

      parse_tree(std::shared_ptr<const void> opaque_tree, std::size_t tree_definition, source_range tree_range,
                 std::function<std::unique_ptr<T>()> materializer, std::vector<node_damage> pending_damage = {},
                 bool tree_partial = false,
                 std::function<void(const T&, std::vector<const node*>&)> damage_indexer = {},
                 std::function<std::optional<std::string>(std::string_view)> repaired_input = {}) :
          definition{tree_definition}, range{std::move(tree_range)}, partial{tree_partial},
          m_opaque_tree{std::move(opaque_tree)}, m_materialize{std::move(materializer)},
          m_pending_damage{std::move(pending_damage)}, m_damage_indexer{std::move(damage_indexer)},
          m_repair_input{std::move(repaired_input)} {}

      [[nodiscard]] auto get() -> T* {
         ensure_materialized();
         return m_materialized.get();
      }

      [[nodiscard]] auto get() const -> const T* {
         ensure_materialized();
         return m_materialized.get();
      }

      [[nodiscard]] auto operator->() -> T* { return get(); }

      [[nodiscard]] auto operator->() const -> const T* { return get(); }

      [[nodiscard]] auto operator*() -> T& { return *get(); }

      [[nodiscard]] auto operator*() const -> const T& { return *get(); }

      [[nodiscard]] auto has_materialized() const -> bool { return static_cast<bool>(m_materialized); }

      [[nodiscard]] auto damaged_nodes() const -> const std::vector<const node*>& {
         ensure_materialized();
         ensure_damage_indexed();
         return m_damaged_nodes;
      }

      [[nodiscard]] auto repaired_input(std::string_view input) const -> std::optional<std::string> {
         if (!partial || !m_repair_input) {
            return std::string{input};
         }
         return m_repair_input(input);
      }

      [[nodiscard]] auto root_damage() const -> const std::vector<node_damage>& { return m_pending_damage; }

      std::size_t definition = 0;
      source_range range;
      bool partial = false;

   private:
      template<typename U>
      friend auto detail::opaque_tree_of(const parse_tree<U>& tree) -> std::shared_ptr<const void>;

      void ensure_materialized() const {
         if (m_materialized != nullptr || !m_materialize) {
            return;
         }
         auto built = m_materialize();
         if (built != nullptr) {
            for (const auto& damage: m_pending_damage) {
               built->add_damage(damage);
            }
            m_materialized = std::shared_ptr<T>{built.release()};
         }
      }

      void ensure_damage_indexed() const {
         if (m_damage_indexed || m_materialized == nullptr) {
            return;
         }
         if (m_damage_indexer) {
            m_damage_indexer(*m_materialized, m_damaged_nodes);
         } else if (m_materialized->is_damaged()) {
            m_damaged_nodes.push_back(m_materialized.get());
         }
         m_damage_indexed = true;
      }

      std::shared_ptr<const void> m_opaque_tree;
      std::function<std::unique_ptr<T>()> m_materialize;
      std::vector<node_damage> m_pending_damage;
      std::function<void(const T&, std::vector<const node*>&)> m_damage_indexer;
      std::function<std::optional<std::string>(std::string_view)> m_repair_input;
      mutable std::shared_ptr<T> m_materialized;
      mutable std::vector<const node*> m_damaged_nodes;
      mutable bool m_damage_indexed = false;
   };

   /// @brief Overall outcome of one parse attempt.
   enum class parse_status { failure, success, partial_success };

   /// @brief Result of parsing an input string into a forest of lazy parse-tree handles.
   /// @tparam T Root node type produced by the parse entry point.
   template<typename T> struct parse_result {
      /// @brief Explicit parse outcome state.
      parse_status status = parse_status::failure;
      /// @brief True when parsing produced either a complete or a partially recovered forest.
      bool success = false;
      /// @brief True when at least one returned tree is partially recovered.
      bool partial = false;
      /// @brief All parse trees produced for the input.
      std::vector<parse_tree<T>> forest;
      /// @brief Optional failure or recovery diagnostics associated with this parse attempt.
      std::optional<parse_error> error;
   };
} // namespace cpf


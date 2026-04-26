#pragma once

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cpf {
   /// @brief Classifies the shape of the input observed at the furthest parse failure.
   enum class parse_error_found_kind {
      token,
      end_of_input,
      ambiguous_parse,
      filtered_parse
   };

   /// @brief Structured description of what the parser observed at the furthest failure.
   struct parse_error_found {
      parse_error_found_kind kind = parse_error_found_kind::end_of_input;
      std::string text;
   };

   [[nodiscard]] constexpr auto to_string(parse_error_found_kind kind) -> std::string_view {
      switch (kind) {
         case parse_error_found_kind::token:
            return "token";
         case parse_error_found_kind::end_of_input:
            return "end of input";
         case parse_error_found_kind::ambiguous_parse:
            return "ambiguous parse";
         case parse_error_found_kind::filtered_parse:
            return "filtered parse";
      }
      return "unknown";
   }

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

   /// @brief Source location within one logical source text.
   using source_location = source_position;

   /// @brief Resolved location paired with the logical source id it belongs to.
   struct resolved_source_location {
      source_location location;
      std::size_t id = 0;
   };

   /// @brief Generic graph-based mapper for source texts related through in-place replacements.
   class source_mapper {
   public:
      /// @brief Registers one logical source text under a caller-chosen id.
      /// @param source Source text stored for later graph transitions and coordinate resolution.
      /// @param id Stable caller-chosen source id.
      void append_source(std::string_view source, std::size_t id);

      /// @brief Registers that one source's full contents replaced a range inside another source.
      /// @param from_id Source id whose full contents appear in the transformed destination.
      /// @param to_id Destination source id that contains the replacement.
      /// @param replacement_range Range inside `to_id` occupied by the contents of `from_id`.
      void append(std::size_t from_id, std::size_t to_id, source_range replacement_range);

      /// @brief Checks whether one source id has been registered.
      /// @param id Source id to query.
      /// @return True when the mapper knows that source.
      [[nodiscard]] auto contains(std::size_t id) const -> bool;

      /// @brief Returns the stored text for one source id.
      /// @param id Source id to inspect.
      /// @return Registered source text.
      [[nodiscard]] auto source(std::size_t id) const -> std::string_view;

      /// @brief Resolves one location through a single replacement edge when one applies.
      /// @param location Location in the source identified by `id`.
      /// @param id Source id containing `location`.
      /// @return Either the directly replaced source location or the canonicalized original input location.
      [[nodiscard]] auto resolve_once(source_location location, std::size_t id) const
            -> std::optional<resolved_source_location>;

      /// @brief Resolves one location recursively back to the deepest original source location.
      /// @param location Location in the source identified by `id`.
      /// @param id Source id containing `location`.
      /// @return Deepest reachable source location.
      [[nodiscard]] auto resolve(source_location location, std::size_t id) const
            -> std::optional<resolved_source_location>;

      /// @brief Resolves one location forward into every recursively transformed destination that contains it.
      /// @param location Location in the source identified by `id`.
      /// @param id Source id containing `location`.
      /// @return Every reachable transformed location, or the source location itself when no transitions leave it.
      [[nodiscard]] auto resolve_from(source_location location, std::size_t id) const
            -> std::vector<resolved_source_location>;

   private:
      struct source_node {
         std::string text;
         std::vector<std::size_t> line_offsets;
      };

      struct replacement_edge {
         std::size_t from_id = 0;
         std::size_t to_id = 0;
         source_range replacement_range;
      };

      [[nodiscard]] auto location_for(std::size_t id, std::size_t offset) const -> std::optional<source_location>;
      [[nodiscard]] auto offset_for(std::size_t id, source_location location) const -> std::optional<std::size_t>;

      std::unordered_map<std::size_t, source_node> m_sources;
      std::unordered_map<std::size_t, std::vector<replacement_edge>> m_incoming;
      std::unordered_map<std::size_t, std::vector<replacement_edge>> m_outgoing;
   };

   /// @brief Describes the furthest parse failure that occurred while parsing input.
   struct parse_error {
      /// @brief Source position of the parse error.
      source_position position;
      /// @brief Tokens or grammar elements that would have been accepted.
      std::vector<std::string> expected;
      /// @brief Structured description of what the parser observed at the point of failure.
      parse_error_found found;
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

   /// @brief One lexed terminal produced by a generated CPF lexer.
   struct lexed_token {
      /// @brief Grammar-local terminal symbol id. Prefer tokens produced by the generated `lex(...)` helper.
      std::size_t symbol = 0;
      /// @brief Matched token text together with its source range in the original input.
      matched_string text;
      /// @brief True when the lexer could not classify this input slice as any declared token.
      bool invalid = false;
   };

   /// @brief Reusable tokenized input that carries both the token sequence and the original source text.
   struct token_sequence {
      /// @brief Original source text used to produce `tokens`.
      std::string input;
      /// @brief Lexed tokens for the source text.
      std::vector<lexed_token> tokens;

      [[nodiscard]] auto empty() const -> bool { return tokens.empty(); }
      [[nodiscard]] auto size() const -> std::size_t { return tokens.size(); }
      [[nodiscard]] auto data() -> lexed_token* { return tokens.data(); }
      [[nodiscard]] auto data() const -> const lexed_token* { return tokens.data(); }
      [[nodiscard]] auto begin() -> std::vector<lexed_token>::iterator { return tokens.begin(); }
      [[nodiscard]] auto begin() const -> std::vector<lexed_token>::const_iterator { return tokens.begin(); }
      [[nodiscard]] auto end() -> std::vector<lexed_token>::iterator { return tokens.end(); }
      [[nodiscard]] auto end() const -> std::vector<lexed_token>::const_iterator { return tokens.end(); }
      [[nodiscard]] auto front() -> lexed_token& { return tokens.front(); }
      [[nodiscard]] auto front() const -> const lexed_token& { return tokens.front(); }
      [[nodiscard]] auto back() -> lexed_token& { return tokens.back(); }
      [[nodiscard]] auto back() const -> const lexed_token& { return tokens.back(); }
      [[nodiscard]] auto span() const -> std::span<const lexed_token> { return {tokens.data(), tokens.size()}; }
      [[nodiscard]] auto operator[](std::size_t index) -> lexed_token& { return tokens[index]; }
      [[nodiscard]] auto operator[](std::size_t index) const -> const lexed_token& { return tokens[index]; }
   };

   /// @brief Streams a human-readable multiline debug view of a token sequence.
   std::ostream& operator<<(std::ostream& os, const token_sequence& sequence);

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

   struct node;
   template<typename T> class parse_tree;

   namespace detail {
      void add_damage(node& target, node_damage damage);
      template<typename T> [[nodiscard]] auto opaque_tree_of(const parse_tree<T>& tree) -> std::shared_ptr<const void>;
      template<typename T>
      [[nodiscard]] auto pending_damage_of(const parse_tree<T>& tree) -> const std::vector<node_damage>&;
   }

   /// @brief Base class for all generated model nodes.
   struct node {
      /// @brief Zero-based production index within the rule that created this node.
      std::size_t production_index = 0;
      /// @brief Source range that produced this node.
      source_range range;

      node() = default;
      node(node&&) = default;
      auto operator=(node&&) -> node& = default;
      node(const node&) = delete;
      auto operator=(const node&) -> node& = delete;

      virtual ~node();

      /// @brief Returns the generated rule identifier of the concrete node.
      /// @return Stable rule id used for constant-time dispatch in generated visitors.
      [[nodiscard]] virtual std::size_t rule_id() const = 0;

      /// @brief True when this node was recovered from a damaged parse.
      [[nodiscard]] auto is_damaged() const -> bool;

      /// @brief Damage annotations attached to this node.
      [[nodiscard]] auto damage() const -> const std::vector<node_damage>&;

   protected:
      /// @brief Adds one damage annotation to this node.
      void add_damage(node_damage damage);

      /// @brief Clones the concrete node through the base interface.
      /// @return A newly allocated deep copy of the node.
      [[nodiscard]] virtual std::unique_ptr<node> clone_node() const = 0;

      template<typename T> friend class parse_tree;
      friend void detail::add_damage(node& target, node_damage damage);

      void copy_damage_to(node& other) const;

   private:
      std::vector<node_damage> m_damage;
   };

   /// @brief Base class for generated nodes that carry caller-defined user data.
   /// @tparam UserData User-defined payload type stored on each generated AST node.
   template<typename UserData> struct node_with_user_data : node {
      using user_data_type = UserData;
      UserData user_data{};
   };

   template<> struct node_with_user_data<void> : node {
      using user_data_type = void;
   };


   /// @brief Lazy handle for one parse tree in a returned forest.
   /// @tparam T Root node type materialized from the opaque runtime tree.
   template<typename T> class parse_tree {
   public:
      parse_tree() : m_state{std::make_shared<state>()} {}
      parse_tree(parse_tree&&) = default;
      auto operator=(parse_tree&&) -> parse_tree& = default;
      parse_tree(const parse_tree&) = delete;
      auto operator=(const parse_tree&) -> parse_tree& = delete;

      parse_tree(std::unique_ptr<T> eager_tree) :
          m_state{std::make_shared<state>()} {
         m_state->production_index = eager_tree != nullptr ? eager_tree->production_index : 0;
         m_state->range = eager_tree != nullptr ? eager_tree->range : source_range{};
         if (eager_tree != nullptr) {
            m_state->materialized = std::shared_ptr<T>{eager_tree.release()};
         }
      }

      parse_tree(std::shared_ptr<const void> opaque_tree, std::size_t tree_production_index, source_range tree_range,
                 std::function<std::unique_ptr<T>()> materializer, std::vector<node_damage> pending_damage = {},
                 bool tree_partial = false,
                 std::function<void(const T&, std::vector<const node*>&)> damage_indexer = {},
                 std::function<std::optional<std::string>(std::string_view)> repaired_input = {}) :
          m_state{std::make_shared<state>()} {
         m_state->production_index = tree_production_index;
         m_state->range = std::move(tree_range);
         m_state->partial = tree_partial;
         m_state->opaque_tree = std::move(opaque_tree);
         m_state->materialize = std::move(materializer);
         m_state->pending_damage = std::move(pending_damage);
         m_state->damage_indexer = std::move(damage_indexer);
         m_state->repair_input = std::move(repaired_input);
      }

      [[nodiscard]] auto get() -> T* {
         ensure_materialized();
         return m_state->materialized.get();
      }

      [[nodiscard]] auto get() const -> const T* {
         ensure_materialized();
         return m_state->materialized.get();
      }

      [[nodiscard]] auto operator->() -> T* { return get(); }

      [[nodiscard]] auto operator->() const -> const T* { return get(); }

      [[nodiscard]] auto operator*() -> T& { return *get(); }

      [[nodiscard]] auto operator*() const -> const T& { return *get(); }

      [[nodiscard]] auto has_materialized() const -> bool { return static_cast<bool>(m_state->materialized); }

      [[nodiscard]] auto damaged_nodes() const -> const std::vector<const node*>& {
         ensure_materialized();
         ensure_damage_indexed();
         return m_state->damaged_nodes;
      }

      [[nodiscard]] auto production_index() const -> std::size_t { return m_state->production_index; }

      [[nodiscard]] auto range() const -> const source_range& { return m_state->range; }

      [[nodiscard]] auto is_partial() const -> bool { return m_state->partial; }

      /// @brief Returns a logically equivalent handle with independent lazy materialization state when possible.
      [[nodiscard]] auto clone() const -> parse_tree {
         parse_tree copy;
         copy.m_state = std::make_shared<state>(*m_state);
         if (copy.m_state->materialize) {
            copy.m_state->materialized.reset();
            copy.m_state->damaged_nodes.clear();
            copy.m_state->damage_indexed = false;
         }
         return copy;
      }

      [[nodiscard]] auto try_repair_input(std::string_view input) const -> std::optional<std::string> {
         if (!m_state->partial || !m_state->repair_input) {
            return std::string{input};
         }
         return m_state->repair_input(input);
      }

   private:
      struct state {
         std::size_t production_index = 0;
         source_range range;
         bool partial = false;
         std::shared_ptr<const void> opaque_tree;
         std::function<std::unique_ptr<T>()> materialize;
         std::vector<node_damage> pending_damage;
         std::function<void(const T&, std::vector<const node*>&)> damage_indexer;
         std::function<std::optional<std::string>(std::string_view)> repair_input;
         std::shared_ptr<T> materialized;
         std::vector<const node*> damaged_nodes;
         bool damage_indexed = false;
      };

      template<typename U>
      friend auto detail::opaque_tree_of(const parse_tree<U>& tree) -> std::shared_ptr<const void>;
      template<typename U>
      friend auto detail::pending_damage_of(const parse_tree<U>& tree) -> const std::vector<node_damage>&;

      void ensure_materialized() const {
         if (m_state->materialized != nullptr || !m_state->materialize) {
            return;
         }
         auto built = m_state->materialize();
         if (built != nullptr) {
            for (const auto& damage: m_state->pending_damage) {
               built->add_damage(damage);
            }
            m_state->materialized = std::shared_ptr<T>{built.release()};
         }
      }

      void ensure_damage_indexed() const {
         if (m_state->damage_indexed || m_state->materialized == nullptr) {
            return;
         }
         if (m_state->damage_indexer) {
            m_state->damage_indexer(*m_state->materialized, m_state->damaged_nodes);
         } else if (m_state->materialized->is_damaged()) {
            m_state->damaged_nodes.push_back(m_state->materialized.get());
         }
         m_state->damage_indexed = true;
      }

      std::shared_ptr<state> m_state;
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


# Runtime API

This page describes the generated parser API exposed by CPF and the runtime facilities provided by `cpflib`.

## Generated API shape

For a grammar root such as `expression`, CPF generates node types like:

```c++
struct expression : cpf::node {
    using parse_result = cpf::parse_result<expression>;

    static constexpr std::size_t RuleId = 0;
    static constexpr std::size_t ReductionCount = 5;
    static std::array<cpf::complexity, 5> Complexity;

    ~expression() override = default;
    static parse_result parse(std::string_view input, const cpf::parse_options& options = {});
    static auto complexity_inputs(std::size_t rule_id) -> std::span<const std::string_view>;
    static auto recompute_complexity(std::size_t rule_id) -> const cpf::complexity&;
    std::size_t rule_id() const override;
    const std::type_info& type() const override;
    std::unique_ptr<expression> clone();
};
```

Each generated rule also gets:

- `operator<<`
- `visit(...)`
- `visit_recursive(...)`

Every generated node inherits:

- `std::size_t definition`
- `cpf::source_range range`

## Parse configuration

Generated parse entry points accept an optional `cpf::parse_options` value:

```c++
struct parse_options {
    bool build_ast = true;
    bool error_on_ambiguity = false;
    bool allow_partial = false;
};
```

- `build_ast = false` validates the input and grammar constraints without materializing the AST forest
- `error_on_ambiguity = true` turns ambiguous parses into a parse failure before forest expansion
- `allow_partial = true` lets the runtime recover inside a single tree by ignoring invalid input or inserting virtual literals

## Parse results and lazy forests

Each parse entry point returns `cpf::parse_result<T>`.

```c++
template<typename T>
struct parse_result {
    bool success = false;
    bool partial = false;
    std::vector<cpf::parse_tree<T>> forest;
    cpf::parse_error error;
};
```

When `build_ast = false`, `success` still reports whether parsing succeeded, while `forest` stays empty by design.

When `allow_partial = true`, `success` may also be true for a recovered parse. In that case:

- `parse_result::partial` is true when at least one returned tree is recovered
- each `parse_tree<T>` exposes its own `partial` flag
- every ambiguity path still maps to one tree
- syntax damage is kept inside that tree instead of being split into prefix/suffix trees

When `build_ast = true`, `forest` stores lazy `cpf::parse_tree<T>` handles rather than eagerly materialized AST roots.
Each handle keeps opaque parse-tree state plus lightweight metadata such as `definition` and `range`. The actual AST
node is built on first access through `operator*`, `operator->`, or `get()`. Dereferencing a const handle still
materializes lazily, but it now returns `const T&` / `const T*` access instead of mutable node access.

Recovered trees also expose:

- `parse_tree<T>::damaged_nodes()` for fast access to damaged AST nodes after materialization
- `parse_tree<T>::repaired_input(std::string_view)` to rebuild an input string whose shape matches the recovered AST
- `cpf::node::is_damaged()`
- `cpf::node::damage()`

Damage entries are structured:

```c++
enum class node_damage_reason {
    ignored_invalid_input,
    inserted_virtual_token
};

struct node_damage {
    source_range range;
    node_damage_reason reason;
    std::string detail;
    std::string message;
};
```

- `reason` classifies the repair kind
- `detail` carries the skipped snippet or inserted token text when available
- `message` explains why recovery was needed and, for inserted virtual tokens, which token options were inserted

`parse_tree<T>::repaired_input(input)` returns `std::optional<std::string>` and works like this:

- when the tree has ignored damage, the corresponding ignored source ranges are removed from `input`
- when the tree has inserted virtual tokens, the synthesized zero-width terminals are inserted into `input`
- when the tree has no damage, the method returns `input` unchanged inside the optional
- when the supplied `input` no longer structurally matches the parse tree that was built, the method returns `std::nullopt`

The runtime never caches the original source string. The method reconstructs the repaired text from the caller-provided
`input` and the stored parse-tree metadata on demand.

That means code such as this still works:

```c++
auto result = expression::parse("1 + 2 * 3");
if (!result.success || result.forest.empty()) {
    return 1;
}

auto value = visit(*result.forest.front(), visitor{});
```

AST materialization is deferred until `result.forest.front()` is actually dereferenced.

## Captured terminals and ranges

Captured terminals are stored as `cpf::matched_string`, which includes:

- the matched text
- the exact source range it matched

Source positions are tracked as one-based line and column values together with a zero-based byte offset.

## Error handling

`cpf::parse_error` carries:

- zero-based byte offset of the furthest failure
- one-based line and column of the furthest failure
- expected tokens or grammar elements
- the found token
- contextual notes
- a human-readable message

In practice:

- `success == true` means CPF produced either a complete parse or a recovered partial parse
- `partial == true` means at least one returned tree contains recovery damage
- `forest` contains one tree per surviving ambiguity path
- `error` still describes the furthest original parse failure, even when recovery succeeded

Ambiguous grammars may legitimately return multiple forest entries unless `error_on_ambiguity` is enabled.

With `allow_partial = true`, recovery currently uses two inline repair mechanisms:

- **ignored invalid input**: unexpected source text is skipped and attached as damage to the surrounding node
- **virtual literal insertion**: a missing literal token is synthesized as a zero-width match and recorded as damage

Both repairs stay inside the same returned tree, so multiple syntax errors do not create extra forest entries by themselves.

Important AST semantics:

- ignored invalid input is **not** inserted as a child node in the AST
- inserted virtual tokens **are** preserved in the AST shape as ordinary zero-width terminal matches so the AST remains structurally healthy
- both repair kinds are discoverable by traversing `parse_tree<T>::damaged_nodes()` and then inspecting each node's `damage()` entries
- `repaired_input(...)` removes ignored ranges even though they are not AST children, and inserts virtual tokens because they are part of the AST shape

## Complexity metadata

Each generated node also exposes:

- a stable `RuleId`
- `ReductionCount`
- a mutable `Complexity` array
- `complexity_inputs(rule_id)`
- `recompute_complexity(rule_id)`

Here `rule_id` is the zero-based production `definition` stored on parsed nodes, so merged generated node classes can
keep separate complexity estimates per reduction rule.

`Complexity[rule_id]` is intentionally populated lazily. CPF always generates the deterministic sample inputs up front,
but it only fits and stores the corresponding `cpf::complexity` object when `recompute_complexity(rule_id)` is called.

## End-to-end example

```c++
#include "calculator.h"

#include <iostream>
#include <string>

struct visitor {
    int operator()(const addition& node) const { return visit(*node.left, *this) + visit(*node.right, *this); }
    int operator()(const subtraction& node) const { return visit(*node.left, *this) - visit(*node.right, *this); }
    int operator()(const multiplication& node) const { return visit(*node.left, *this) * visit(*node.right, *this); }
    int operator()(const division& node) const { return visit(*node.left, *this) / visit(*node.right, *this); }
    int operator()(const number& node) const { return std::stoi(node.value.text); }
};

int main() {
    auto result = expression::parse("1 + 2 * 3");
    if (!result.success || result.forest.empty()) {
        return 1;
    }

    auto& tree = result.forest.front();
    std::cout << *tree << std::endl;
    std::cout << "Matched columns: " << tree->range.begin.column << "-" << tree->range.end.column << std::endl;
    std::cout << "Result: " << visit(*tree, visitor{}) << std::endl;
    return 0;
}
```

## Partial parsing example

```c++
cpf::parse_options options;
options.allow_partial = true;

auto result = grouped_sentence::parse("(hi", options);
if (!result.success || result.forest.empty()) {
    return 1;
}

auto& tree = result.forest.front();
if (tree.partial) {
    if (auto repaired = tree.repaired_input("(hi"); repaired.has_value()) {
        std::cout << *repaired << '\n';
    }
    for (const auto* damaged : tree.damaged_nodes()) {
        for (const auto& damage : damaged->damage()) {
            std::cout << cpf::to_string(damage.reason);
            if (!damage.detail.empty()) {
                std::cout << ": " << damage.detail;
            }
            if (!damage.message.empty()) {
                std::cout << " (" << damage.message << ")";
            }
            std::cout << " at offset " << damage.range.begin.offset << '\n';
        }
    }
}
```


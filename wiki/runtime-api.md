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
};
```

- `build_ast = false` validates the input and grammar constraints without materializing the AST forest
- `error_on_ambiguity = true` turns ambiguous parses into a parse failure before forest expansion

## Parse results and lazy forests

Each parse entry point returns `cpf::parse_result<T>`.

```c++
template<typename T>
struct parse_result {
    bool success = false;
    std::vector<cpf::parse_tree<T>> forest;
    cpf::parse_error error;
};
```

When `build_ast = false`, `success` still reports whether parsing succeeded, while `forest` stays empty by design.

When `build_ast = true`, `forest` stores lazy `cpf::parse_tree<T>` handles rather than eagerly materialized AST roots.
Each handle keeps opaque parse-tree state plus lightweight metadata such as `definition` and `range`. The actual AST
node is built on first access through `operator*`, `operator->`, or `get()`.

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

- one-based line and column of the furthest failure
- expected tokens or grammar elements
- the found token
- contextual notes
- a human-readable message

In practice:

- `success == true` means the input parsed completely
- `forest` contains every accepted parse tree
- `error` is populated when parsing fails

Ambiguous grammars may legitimately return multiple forest entries unless `error_on_ambiguity` is enabled.

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


# CPF

CPF is a small parser generation framework that reads a grammar file and produces a matching C++ header/source pair.
The generated code contains:

- model node types
- parse entry points for each rule
- stream output helpers
- visitation helpers
- clone support

The project currently targets deterministic expression-style grammars with labeled members, literals, regex terminals,
inheritance-style choice rules, precedence, and associativity.

## Grammar overview

```text
// Calculator grammar file

// Direct dependencies translate to inheritance-style choice rules.
expression -> addition | subtraction | multiplication | division | number;

// Labels generate model members.
addition        [prec = 'sub']              -> expression:left '+':op expression:right;
subtraction     [prec < 'div', lbl = 'sub'] -> expression:left '-':op expression:right;
multiplication  [prec = 'div']              -> expression:left '*':op expression:right;
division        [prec < 'num', lbl = 'div'] -> expression:left '/':op expression:right;

// Regex terminals are written as r'...'.
number          [lbl = 'num']               -> r'[0-9]+';
```

Supported rule attributes:

| Name | Values | Example | Meaning |
| ---- | ------ | ------- | ------- |
| `prec` | `= <num>` | `[prec = 10]` | Absolute precedence rank |
| `prec` | `= <str>` | `[prec = 'sum']` | Same precedence group as a label |
| `prec` | `< <str>` | `[prec < 'product']` | Lower precedence than the referenced label |
| `prec` | `> <str>` | `[prec > 'sum']` | Higher precedence than the referenced label |
| `dir` | `left`, `right` | `[dir = right]` | Operator associativity |
| `lbl` | `<str>` | `[lbl = 'number']` | Rule label used for precedence grouping |

Default attribute behavior when a rule omits them:

- `prec` defaults to an absolute precedence rank derived from the rule's source line for infix rules, so later infix rules bind tighter than earlier ones.
- `dir` defaults to `left`.
- `lbl` defaults to the rule identifier.

That means a grammar like:

```text
expr -> add | multiply | number;
add -> expr:left '+':op expr:right;
multiply -> expr:left '*':op expr:right;
number -> r'[0-9]+';
```

parses `1 + 2 * 3` as `1 + (2 * 3)` even though no explicit attributes are written.

## Generated API shape

For the calculator grammar above, CPF generates a model like this:

```c++
struct expression : cpf::node {
    using parse_result = cpf::parse_result<expression>;

    ~expression() override = default;
    static parse_result parse(std::string_view input);
    const std::type_info& type() const override;
    std::unique_ptr<expression> clone();
};

struct addition : expression {
    using parse_result = cpf::parse_result<addition>;

    std::unique_ptr<expression> left;
    std::string op;
    std::unique_ptr<expression> right;

    ~addition() override = default;
    static parse_result parse(std::string_view input);
    const std::type_info& type() const override;
    std::unique_ptr<addition> clone();
};

struct number : expression {
    using parse_result = cpf::parse_result<number>;

    std::string value;

    ~number() override = default;
    static parse_result parse(std::string_view input);
    const std::type_info& type() const override;
    std::unique_ptr<number> clone();
};
```

Each generated rule also gets:

- `operator<<`
- `visit(...)`
- `visit_recursive(...)`

## Error handling

Every parse entry point returns `cpf::parse_result<T>`.

- `success == true` means the input parsed completely
- `forest` contains the produced parse trees
- `error` contains `line`, `column`, `expected`, `found`, and `message` when parsing fails

`forest` contains every full parse tree accepted by the generated Earley parser.

- ambiguous grammars can therefore return more than one tree in `forest`
- precedence and associativity attributes are applied as disambiguation constraints over expression-family parses
- ambiguous concrete rules with multiple productions are still rejected during code generation because the generated AST layout is one concrete struct per rule

For unambiguous grammars, the current tests still expect a single parse tree per successful parse.

## Example usage

```c++
#include "calculator.h"

#include <cassert>
#include <iostream>
#include <string>

struct visitor {
    int operator()(const addition& node) const { return visit(*node.left, *this) + visit(*node.right, *this); }
    int operator()(const subtraction& node) const { return visit(*node.left, *this) - visit(*node.right, *this); }
    int operator()(const multiplication& node) const { return visit(*node.left, *this) * visit(*node.right, *this); }
    int operator()(const division& node) const { return visit(*node.left, *this) / visit(*node.right, *this); }
    int operator()(const number& node) const { return std::stoi(node.value); }
};

int main() {
    while (true) {
        std::cout << "input > ";

        std::string input;
        std::getline(std::cin, input);

        auto result = expression::parse(input);
        if (!result.success) {
            std::cout << "Parse error: " << result.error.message << std::endl;
            continue;
        }

        assert(result.forest.size() == 1);
        auto& tree = result.forest[0];
        std::cout << "Parsed successfully: " << *tree << std::endl;
        std::cout << "Result: " << visit(*tree, visitor{}) << std::endl;
    }
}
```

## Build

```zsh
cmake -S . -B build
cmake --build build
```

## Run the generator

```zsh
./build/cpfgen/cpfgen /path/to/calculator.cpf /path/to/output-directory
```

If the output directory is omitted, `cpfgen` writes the generated files next to the grammar file.

## CMake integration

When `cpflib` is added to a CMake project, it exposes the helper:

```cmake
cpf_link_grammars(<target> <grammar1.cpf> <grammar2.cpf> ...)
```

The helper:

- runs `cpfgen` for each listed grammar
- adds the generated `.cpp` files to the target sources
- adds the generated header directory to the target include paths

Example:

```cmake
add_executable(example main.cpp)
target_link_libraries(example PRIVATE cpflib)

cpf_link_grammars(example
        ${CMAKE_CURRENT_SOURCE_DIR}/calculator.cpf
        ${CMAKE_CURRENT_SOURCE_DIR}/tokens.cpf
)
```

## Tests

The repository contains:

- `cpflibtests` for grammar/runtime/code-generation unit tests
- `cpftests` for end-to-end generator integration tests using generated fixtures

Run everything with:

```zsh
ctest --test-dir build --output-on-failure
```
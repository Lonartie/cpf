# CPF

CPF is a parser generation framework that reads a `.cpf` grammar and emits a matching C++20 header/source pair.
It is intended to be a parser generator for quick iteration and prototyping. Even though efforts were made to 
keep the generated parser efficient, performance is not the primary goal.

> Disclaimer: This project makes heavy use of AI as almost all of the code is AI-generated.

Generated code includes:

- AST node types
- parse entry points for each rule
- `operator<<` helpers
- `visit(...)` and `visit_recursive(...)`
- deep-clone support
- source ranges for nodes and captured terminals

## Supported grammar features

CPF currently supports:

- literals and regex terminals
- labeled captures
- choice-style base rules
- precedence and associativity attributes
- quantified symbols (`?`, `*`, `+`, `{n}`)
- grouped alternatives
- labeled single-symbol group captures such as `('x' | 'y'):value`
- multi-file grammars via `import`
- generated-code namespaces

Grammar strings may use either single quotes or double quotes for literals, regex bodies, quoted attribute values, and import paths.

## Grammar example

```text
expression -> addition | subtraction | multiplication | division | number;

addition        [prec = 'sub']              -> expression:left '+':op expression:right;
subtraction     [prec < 'div', lbl = 'sub'] -> expression:left '-':op expression:right;
multiplication  [prec = 'div']              -> expression:left '*':op expression:right;
division        [prec < 'num', lbl = 'div'] -> expression:left '/':op expression:right;
number          [lbl = 'num']               -> r'[0-9]+':value;
```

Labels are optional. Labeled symbols become members on the generated node type; unlabeled symbols still participate in parsing but are not stored in the AST.

### Rule attributes

| Name | Values | Example | Meaning |
| ---- | ------ | ------- | ------- |
| `prec` | `= <num>` | `[prec = 10]` | Absolute precedence rank |
| `prec` | `= <str>` | `[prec = 'sum']` or `[prec = "sum"]` | Same precedence group as a label |
| `prec` | `< <str>` | `[prec < 'product']` or `[prec < "product"]` | Lower precedence than the referenced label |
| `prec` | `> <str>` | `[prec > 'sum']` or `[prec > "sum"]` | Higher precedence than the referenced label |
| `dir` | `left`, `right` | `[dir = right]` | Operator associativity |
| `lbl` | `<str>` | `[lbl = 'number']` | Rule label used for precedence grouping |

Default behavior when attributes are omitted:

- infix-rule precedence defaults to source order
- associativity defaults to `left`
- labels default to the rule identifier

## Quantifiers and groups

Supported postfix forms:

| Syntax | Meaning |
| ------ | ------- |
| `symbol?` | optional symbol |
| `symbol*` | zero or more repetitions |
| `symbol+` | one or more repetitions |
| `symbol{n}` | exactly `n` repetitions |

The same forms work for parenthesized groups:

```text
grouped_value -> ('x':text | 'y':text);
grouped_sentence -> '(':open ('hi':text | 'bye':text) ')':close;
grouped_repeat -> ('a' | 'b')+;
grouped_choice_value -> ('x' | 'y'):value;
```

Single-symbol grouped choices can also be captured into one generated member:

```text
message -> greeting | farewell;
greeting -> 'hello':text;
farewell -> 'bye':text;

payload -> (greeting | farewell):value;
token -> ('x' | 'y'):value;
```

This lowers through hidden helper rules, but the generated public API still exposes `payload::value` as `std::variant<std::unique_ptr<greeting>, std::unique_ptr<farewell>>` and `token::value` as `cpf::matched_string`.

Current limitation: labeled groups must lower to exactly one symbol per alternative, so forms such as `('x' 'y' | 'z'):value` are still rejected.

Generated member types follow the captured symbol kind:

- optional references: `std::unique_ptr<rule>`
- optional terminals: `std::optional<cpf::matched_string>`
- repeated references: `std::vector<std::unique_ptr<rule>>`
- repeated terminals: `std::vector<cpf::matched_string>`

## Multi-file grammars

Grammars can import other grammars:

```text
import 'imports/imported_expr.cpf';
import 'imports/imported_words.cpf';

imported_bundle_message -> imported_bundle_greeting | imported_bundle_parting;
```

Double-quoted imports such as `import "imports/imported_expr.cpf";` work the same way.

Imports are resolved relative to the importing file, expanded transitively, and cycle-checked.

The public loading API is:

```c++
auto loaded = cpf::load_grammar_file("/path/to/root.cpf");
auto generated = cpf::generate_code(loaded.parsed_grammar, "root");
```

`loaded.dependencies` contains the root grammar plus every imported grammar that contributed to generation.

## Generated API shape

For the calculator grammar, CPF generates node types like:

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
    cpf::matched_string op;
    std::unique_ptr<expression> right;

    ~addition() override = default;
    static parse_result parse(std::string_view input);
    const std::type_info& type() const override;
    std::unique_ptr<addition> clone();
};
```

Each generated rule also gets:

- `operator<<`
- `visit(...)`
- `visit_recursive(...)`

Every generated node inherits:

- `std::size_t definition`
- `cpf::source_range range`

Captured terminals are stored as `cpf::matched_string`, which includes both the matched text and its exact source span.

Generated headers also include Doxygen comments with conservative complexity notes derived during generation. Each header starts with a grammar-wide summary, and each generated rule documents:

- Earley parse upper bounds for `parse(...)`
- subtree cost for `clone()`, `operator<<`, and `visit_recursive(...)`
- whether the rule participates in a recursive cycle
- whether the generated node stores repeated members with linear extra storage

## Namespacing generated code

Generated code can stay in the global namespace, or it can be wrapped in a user-provided namespace to avoid collisions.

### Library API

```c++
auto generated = cpf::generate_code(grammar, "calculator", "demo::generated");
```

### Generator CLI

```zsh
./build/cpfgen/cpfgen /path/to/calculator.cpf /path/to/output --namespace demo::generated
```

### CMake integration

```cmake
cpf_link_grammars(example
        NAMESPACE demo::generated
        ${CMAKE_CURRENT_SOURCE_DIR}/calculator.cpf
)
```

The namespace value must be a valid C++ namespace such as `demo`, `demo::generated`, or `my_project::parsers`.

## Parsing behavior and complexity

CPF uses an Earley-style parser runtime.

Typical Earley complexity bounds apply:

- worst-case time: `O(n^3)`
- worst-case space: `O(n^2)`
- many unambiguous grammars run in `O(n^2)` time
- grammars close to LR behavior can approach `O(n)` time in practice

CPF returns a parse forest, so highly ambiguous grammars can also increase practical memory use and result size beyond the core chart data structure.

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
    int operator()(const number& node) const { return std::stoi(node.value.text); }
};

int main() {
    auto result = expression::parse("1 + 2 * 3");
    assert(result.success);
    assert(result.forest.size() == 1);

    auto& tree = result.forest.front();
    std::cout << *tree << std::endl;
    std::cout << "Matched columns: " << tree->range.begin.column << "-" << tree->range.end.column << std::endl;
    std::cout << "Result: " << visit(*tree, visitor{}) << std::endl;
}
```

## Error handling

Each parse entry point returns `cpf::parse_result<T>`.

- `success == true` means the input parsed completely
- `forest` contains every accepted parse tree
- `error` contains line/column information, expected tokens, the found token, and contextual notes when parsing fails

This means ambiguous grammars may legitimately return multiple trees.

## Build

```zsh
cmake -S . -B build
cmake --build build
```

Benchmarks are built by default. To skip them in constrained environments:

```zsh
cmake -S . -B build -DCPF_BUILD_BENCHMARKS=OFF
```

## CMake helper

When `cpflib` is part of a project, it exposes:

```cmake
cpf_link_grammars(<target> [NAMESPACE <cpp-namespace>] <grammar1.cpf> <grammar2.cpf> ...)
```

The helper:

- runs `cpfgen` for each listed grammar
- adds generated `.cpp` files to the target
- adds the generated header directory to the include paths
- tracks imported grammars as dependencies so regeneration happens when imported files change

## Tests

Run the full test suite with:

```zsh
ctest --test-dir build --output-on-failure
```

## Benchmarks

CPF ships a `cpfbench` executable under `cpftools/cpfbench` that measures generated parser runtime with Google Benchmark.

The benchmark target currently covers:

- the generated calculator parser
- a generated simple C-like translation-unit parser

List available benchmarks:

```zsh
./build/cpftools/cpfbench/cpfbench --benchmark_list_tests=true
```

Run the full suite and save CSV output:

```zsh
./build/cpftools/cpfbench/cpfbench --benchmark_out=./build/cpftools/cpfbench/benchmark-results.csv --benchmark_out_format=csv
```

Run only one benchmark suite:

```zsh
./build/cpftools/cpfbench/cpfbench --benchmark_filter='^calculator/.*'
./build/cpftools/cpfbench/cpfbench --benchmark_filter='^simple_c/.*'
```

The benchmark executable prints a compact table by default with three benchmark rows:

- `calculator parse`
- `calculator parse + eval`
- `simple_c parse`

The table includes `min`, `avg`, `max`, and `iter/s` from the smallest configured input size in each benchmark family, plus the fitted asymptotic complexity for the full family.

You can still override the repetition count from the command line:

```zsh
./build/cpftools/cpfbench/cpfbench --benchmark_repetitions=3
```

The benchmark names include an input-size suffix such as `/chars:25`, and each run is labeled as `small`, `medium`, or `large` in the benchmark output.

The included benchmark grammars live at:

- `cpftools/cpfbench/fixtures/grammars/calculator.cpf`
- `cpftools/cpfbench/fixtures/grammars/simple_c.cpf`

The simple C-like benchmark grammar intentionally stays small and parser-focused: functions, blocks, variable declarations, assignments, `if` / `else`, `while`, returns, identifiers, numbers, and infix expressions.

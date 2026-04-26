# Generation and integration

This page covers the APIs and tools used to load grammars, generate C++ code, and integrate generated parsers into CMake
targets.

## Library entry points

CPF exposes the grammar loader and code generator through `cpfgenlib`.

```c++
#include <cpfgenlib>

auto loaded = cpf::load_grammar_file("/path/to/root.cpf");
auto analysis = cpf::analyze_grammar(loaded.parsed_grammar);
auto generated = cpf::generate_code(loaded.parsed_grammar, "root");
```

### Loading grammars

`cpf::load_grammar_file(...)` returns:

- `parsed_grammar`: the parsed grammar model
- `dependencies`: the root grammar plus every imported grammar that contributed to generation
- `preprocessed_source`: the fully expanded grammar text
- `mapper`: a generic `cpf::source_mapper` graph describing how expanded text ranges relate to imported source fragments
- `preprocessed_source_id`: the mapper id for `preprocessed_source`
- `source_origins`: loader metadata that anchors leaf mapper ids back to concrete grammar files and source positions

If you only need the parsed grammar, `cpf::parse_grammar_file(...)` returns the `cpf::grammar` directly.

### Generating code

`cpf::generate_code(...)` returns:

- `header`: generated C++ header text
- `source`: generated C++ source text
- `analysis`: structured grammar diagnostics produced during generation

The `base_name` argument controls the generated file naming and internal include wiring.

The `analysis` field mirrors `cpf::analyze_grammar(...)` and includes:

- a summary with the primary entry rule, reachable-rule counts, nullable-rule counts, and warning/error totals
- structured diagnostics for unused rules, unreachable rules, nullable cycles, and suspicious zero-progress recursion

This lets generators, build tools, or IDE integrations surface grammar warnings without needing to scrape stderr text.
Nullable cycles stay in the warning bucket because the generated Earley parsers already support left recursion, empty
productions, and nullable recursion.

Generated headers now expose both parser and lexer entry points. A generated root type such as `expression` provides:

- `lex(std::string_view)` -> `cpf::token_sequence`
- `parse(std::string_view, ...)`
- `parse(const cpf::token_sequence&, ...)`
- `parse_cst(std::string_view, ...)`
- `parse_cst(const cpf::token_sequence&, ...)`
- `recognize(std::string_view)`
- `recognize(const cpf::token_sequence&)`

This lets callers lex once and parse the same source multiple times with different `cpf::parse_options` values without paying for
lexing again.

`parse(...)` still materializes the generated AST node family for the selected rule. `parse_cst(...)` materializes a generic
`cpf::cst_node` tree that preserves concrete terminals in source order and flattens only synthetic lowering helpers.

## `cpfgen` command-line interface

The `cpfgen` executable generates `<stem>.h` and `<stem>.cpp` from a grammar file.

```text
cpfgen <grammar-file> [output-directory] [--namespace <value>] [--depfile <path>]
```

Behavior:

- when `output-directory` is omitted, generated files are written next to the grammar file
- `--namespace` wraps the generated public API in a C++ namespace
- `--depfile` writes a make-style depfile listing imported grammar dependencies
- any grammar diagnostics found during generation are printed to `stdout`
- warning diagnostics still allow generation to continue and files to be written
- nullable-cycle diagnostics are emitted as warnings rather than blocking errors
- blocking diagnostics fail generation before the header/source pair is written

When a warning originates from text brought in through `@import`, `cpfgen` remaps the reported file and line back to the
original imported grammar instead of the post-preprocessing line in the expanded root document.

Example:

```zsh
./build/cpfgen/cpfgen /path/to/calculator.cpf /path/to/output --namespace demo::generated
```

When diagnostics are present, `cpfgen` prints the analysis summary first and then one detailed line per finding. Each
line includes the severity, stable diagnostic code, source line, rule name, and full message so shell scripts or IDEs
can surface the warnings directly.

## Namespacing generated code

Generated code can stay in the global namespace, or it can be wrapped in a user-provided namespace to avoid collisions.

### Library API

```c++
#include <cpfgenlib>

auto generated = cpf::generate_code(grammar, "calculator", "demo::generated");
```

### CLI

```zsh
./build/cpfgen/cpfgen /path/to/calculator.cpf /path/to/output --namespace demo::generated
```

The namespace value must be a valid C++ namespace such as `demo`, `demo::generated`, or `my_project::parsers`.

## CMake integration

When `cpflib` is part of a project, it exposes the helper:

```cmake
cpf_link_grammars(<target> [NAMESPACE <cpp-namespace>] <grammar1.cpf> <grammar2.cpf> ...)
```

The helper:

- runs `cpfgen` for each listed grammar
- writes generated files under the target build tree
- adds generated `.cpp` files to the target
- adds the generated header directory to the target include paths
- tracks imported grammars as dependencies so regeneration happens when imported files change

Example:

```cmake
cpf_link_grammars(example
        NAMESPACE demo::generated
        ${CMAKE_CURRENT_SOURCE_DIR}/calculator.cpf
)
```

Validation performed by `cpf_link_grammars(...)` includes:

- target existence checks
- `.cpf` extension checks
- duplicate grammar-stem detection per target
- namespace syntax validation

## Repository layout

At a high level:

- `cpflib` contains the runtime library used by generated parsers
- `cpfgenlib` contains the grammar model, parser, loader, and code generator
- `cpfgen` is the command-line front end
- `cpftools` contains tests and benchmarks


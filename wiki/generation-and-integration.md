# Generation and integration

This page covers the APIs and tools used to load grammars, generate C++ code, and integrate generated parsers into CMake
targets.

## Library entry points

CPF exposes the grammar loader and code generator through `cpfgenlib`.

```c++
#include <cpfgenlib>

auto loaded = cpf::load_grammar_file("/path/to/root.cpf");
auto generated = cpf::generate_code(loaded.parsed_grammar, "root");
```

### Loading grammars

`cpf::load_grammar_file(...)` returns:

- `parsed_grammar`: the parsed grammar model
- `dependencies`: the root grammar plus every imported grammar that contributed to generation

If you only need the parsed grammar, `cpf::parse_grammar_file(...)` returns the `cpf::grammar` directly.

### Generating code

`cpf::generate_code(...)` returns:

- `header`: generated C++ header text
- `source`: generated C++ source text

The `base_name` argument controls the generated file naming and internal include wiring.

Generated headers now expose both parser and lexer entry points. A generated root type such as `expression` provides:

- `lex(std::string_view)` -> `cpf::token_sequence`
- `parse(std::string_view, ...)`
- `parse(const cpf::token_sequence&, ...)`
- `recognize(std::string_view)`
- `recognize(const cpf::token_sequence&)`

This lets callers lex once and parse the same source multiple times with different `cpf::parse_options` values without paying for
lexing again.

## `cpfgen` command-line interface

The `cpfgen` executable generates `<stem>.h` and `<stem>.cpp` from a grammar file.

```text
cpfgen <grammar-file> [output-directory] [--namespace <value>] [--depfile <path>]
```

Behavior:

- when `output-directory` is omitted, generated files are written next to the grammar file
- `--namespace` wraps the generated public API in a C++ namespace
- `--depfile` writes a make-style depfile listing imported grammar dependencies

Example:

```zsh
./build/cpfgen/cpfgen /path/to/calculator.cpf /path/to/output --namespace demo::generated
```

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


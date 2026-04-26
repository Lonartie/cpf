# CPF

CPF is a parser generation framework that reads a `.cpf` grammar and either emits a matching C++20 header/source
pair or compiles the grammar directly into an in-memory runtime parser.
It is intended for fast parser iteration and prototyping. Generated and runtime-compiled parsers both use the Earley
algorithm and produce lazy parse forests that can be traversed and queried for syntax nodes, errors, ambiguities,
dynamic ASTs, and CSTs.

> Disclaimer: This project makes heavy use of AI as almost all of the code is AI-generated.

## Repository overview

- `cpflib`: shared grammar frontend, runtime library, and dynamic in-memory parser compiler
- `cpfgenlib`: code generator built on top of `cpflib`
- `cpfgen`: command-line front end for code generation
- `cpftools`: tests, support utilities, and benchmarks

## Quick start

Configure and build the project:

```zsh
cmake -S . -B build
cmake --build build
```

Run the test suite:

```zsh
./build/cpftools/cpflibtests/cpflibtests
./build/cpftools/cpftests/cpftests
```

Generate parser code from a grammar:

```zsh
./build/cpfgen/cpfgen /path/to/grammar.cpf /path/to/output --namespace demo::generated
```

Compile and use a parser entirely at runtime:

```c++
#include <cpflib>

auto parser = cpf::compile_grammar(R"(
   expression -> addition | number;
   addition -> expression:left '+':op expression:right;
   number -> r'[0-9]+':value;
)");

auto result = parser.parse("1 + 2 + 3");
auto cst = parser.parse_cst("1 + 2 + 3");
```

`cpfgen` prints any grammar diagnostics it finds to standard output during generation. Non-blocking warnings, including
nullable-cycle warnings for Earley-compatible grammars, still produce the generated files. Blocking diagnostics stop
generation before writing the header/source pair.

Link grammars into a CMake target:

```cmake
cpf_link_grammars(example
        NAMESPACE demo::generated
        ${CMAKE_CURRENT_SOURCE_DIR}/calculator.cpf
)
```

## Documentation

The detailed project documentation now lives under the repository `wiki` folder:

- [`wiki/README.md`](wiki/README.md): documentation index
- [`wiki/grammar-reference.md`](wiki/grammar-reference.md): grammar syntax and imports
- [`wiki/generation-and-integration.md`](wiki/generation-and-integration.md): generator APIs, CLI, and CMake integration
- [`wiki/runtime-api.md`](wiki/runtime-api.md): generated parser API, parse options, and lazy parse forests
- [`wiki/tooling.md`](wiki/tooling.md): build, test, and benchmark workflows

## Licensing and notices

Third-party dependency attribution for vendored and fetched components lives in [
`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

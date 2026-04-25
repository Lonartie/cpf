# CPF

CPF is a parser generation framework that reads a `.cpf` grammar and emits a matching C++20 header/source pair.
It targets C++20 and is intended for fast parser iteration and prototyping.

> Disclaimer: This project makes heavy use of AI as almost all of the code is AI-generated.

## Repository overview

- `cpflib`: runtime library consumed by generated parsers
- `cpfgenlib`: grammar model, parser/loader, and code generator
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
ctest --test-dir build --output-on-failure
```

Generate parser code from a grammar:

```zsh
./build/cpfgen/cpfgen /path/to/grammar.cpf /path/to/output --namespace demo::generated
```

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

Third-party dependency attribution for vendored and fetched components lives in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

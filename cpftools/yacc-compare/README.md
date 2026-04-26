# yacc-compare

This lab compares two compiled parsers on the same committed C-subset fixture:

- a yacc/bison parser with a handwritten C lexer
- a CPF grammar compiled once through `cpflib` and then reused for repeated parse runs

## Supported language subset

The grammars intentionally cover a deterministic C subset that is large enough for comparative runtime testing while still
being self-authored and easy to maintain:

- global variable declarations
- function definitions
- scalar and fixed-size array declarators
- `if` / `else`
- `while`
- `for`
- `return`
- assignments
- arithmetic, comparison, and logical expressions
- function calls and array indexing
- `//` line comments and standard ASCII identifiers / integer literals

The committed fixture `fixtures/programs/c_subset_fixture.c` is generated deterministically by `generate_fixture.py` and is
kept in the repository so benchmark runs do not depend on an extra generation step.

## Build

Enable the lab explicitly so regular builds do not require yacc/bison:

```cmake
cmake -S . -B cmake-build-debug -DCPF_BUILD_YACC_COMPARE=ON
cmake --build cmake-build-debug --target yacc_compare
```

If your default `cmake` is older than the project minimum version, use the newer CMake bundled with your IDE or toolchain.

## Run

Verification only:

```zsh
./cmake-build-debug/cpftools/yacc-compare/yacc_compare --verify-only
```

Measure three parses with each parser:

```zsh
./cmake-build-debug/cpftools/yacc-compare/yacc_compare --iterations=3
```

## Files

- `fixtures/grammars/c_subset.y`: yacc grammar
- `fixtures/grammars/c_subset.cpf`: matching CPF grammar
- `fixtures/programs/c_subset_fixture.c`: large committed fixture program
- `yacc_driver.c`: handwritten lexer and C wrapper around the generated yacc parser
- `main.cpp`: runtime comparison runner that compiles the CPF grammar once before timing parse runs



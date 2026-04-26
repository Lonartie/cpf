# Tooling

This page collects the common build, test, and benchmark workflows for CPF.

## Build

Configure and build the project:

```zsh
cmake -S . -B build
cmake --build build
```

Benchmarks are built by default. To skip them in constrained environments:

```zsh
cmake -S . -B build -DCPF_BUILD_BENCHMARKS=OFF
```

## Tests

Run the full test suite with:

```zsh
./build/cpftools/cpflibtests/cpflibtests
./build/cpftools/cpftests/cpftests
```

The test tools live under `cpftools` and use the vendored `doctest` single-header distribution. Running the
executables directly produces the underlying doctest failure output, which is usually easier to diagnose than the
aggregated CTest summary.

This is also the easiest place to inspect lexer-facing behavior while developing CPF changes:

- generated `lex(...)` entry points return `cpf::token_sequence`
- `token_sequence` has a debug `operator<<` for inspecting the produced token stream
- generated AST nodes also stream as indented multiline trees, which makes parser regressions easier to read in failing tests

## Benchmarks

CPF ships a `cpfbench` executable under `cpftools/cpfbench` that measures generated parser runtime with Google
Benchmark.

### Included benchmark families

The benchmark target currently covers:

- the generated calculator parser
- a generated simple C-like translation-unit parser

The compact benchmark table reports these benchmark rows:

- `calculator parse -> forest`
- `calculator materialize ast`
- `calculator parse + eval`
- `simple_c parse -> forest`
- `simple_c materialize ast`

The split scenarios are useful when working with CPF's lazy parse forests:

- `parse -> forest` measures parsing through opaque lazy forest handles without AST materialization
- `materialize ast` measures on-demand AST construction from an already parsed forest entry
- `parse + eval` remains an end-to-end comparison for parse plus immediate AST use

### Common commands

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

Override the repetition count:

```zsh
./build/cpftools/cpfbench/cpfbench --benchmark_repetitions=3
```

### Benchmark output notes

- benchmark names include an input-size suffix such as `/chars:25`
- runs are labeled as `small`, `medium`, or `large`
- the compact table includes `min`, `avg`, `max`, `iter/s`, and the fitted asymptotic complexity for each family
- progress information is printed while the benchmark executable runs

### Benchmark grammars

The included benchmark grammars live at:

- `cpftools/cpfbench/fixtures/grammars/calculator.cpf`
- `cpftools/cpfbench/fixtures/grammars/simple_c.cpf`

The simple C-like benchmark grammar intentionally stays small and parser-focused: functions, blocks, variable
declarations, assignments, `if` / `else`, `while`, returns, identifiers, numbers, and infix expressions.


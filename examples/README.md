# Examples

This folder contains small runnable example programs that are automatically discovered by the recursive `examples/CMakeLists.txt` build.

## Existing examples

- `calculator.cpp` / `calculator.cpf`: generated parser + AST visitor
- `calculator_dynamic.cpp`: runtime `compile_grammar(...)` API
- `json.cpp` / `json.cpf`: generated JSON AST and structural checks

## Feature-focused examples

- `error_reporting/config_diagnostics.cpp`
  - custom grammar error labels
  - skipped comments and whitespace
  - structured parse diagnostics with line and column information
- `partial_trees/markdown_partial.cpp`
  - partial recovery with `allow_partial = true`
  - CST inspection of damaged nodes
  - re-parsing repaired input
- `source_repair/expression_autofix.cpp`
  - precedence-aware expression grammar
  - `parse_tree<T>::try_repair_input(...)`
  - evaluation before and after repair
- `parse_forests/dangling_else.cpp`
  - intentionally ambiguous grammar
  - parse forest inspection
  - `error_on_ambiguity` behavior


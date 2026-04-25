# API change review for CPF runtime classes

## Scope

This document captures an assessment of the public runtime API exposed through `cpflib`, with the main focus on:

- `cpf::parse_options`
- `cpf::parse_error`
- `cpf::parse_result<T>`
- `cpf::parse_tree<T>`
- `cpf::node`
- the generated node API shape documented in `wiki/runtime-api.md`

The review is based on the current public surface in `cpflib/runtime/runtime.h`, the generated API emitted from `cpfgen/codegen/code_generator.cpp`, the runtime documentation in `wiki/runtime-api.md`, and real usage exercised by the runtime tests under `cpftools`.

## Summary

The runtime API is already powerful and practical. The main concepts are good:

- lazy `parse_tree<T>` handles are a strong idea
- returning a forest for ambiguity is sensible
- `source_range` on nodes and terminal captures is useful and consistent
- damage and recovery metadata are more useful than a plain success/failure model
- generated `visit(...)` and `visit_recursive(...)` are ergonomic

The biggest issue is not that the API is fundamentally broken. The issue is that the public surface currently feels like three layers collapsed into one:

1. consumer-facing parser API
2. generated-code plumbing API
3. parser-engine internals

That makes the API feel larger, stranger, and less deliberate than it needs to be.

## What already works well

### Lazy parse trees

`cpf::parse_tree<T>` is a good abstraction. The idea of returning lazy forest handles instead of eagerly materialized AST roots is a strong design choice.

Benefits:

- cheap parse-result inspection before paying AST materialization cost
- good match for ambiguous parses
- easy to benchmark parsing separately from materialization
- practical in consumer code

### Forest-based ambiguity model

Representing ambiguity as multiple returned trees is reasonable and easy to understand. It also aligns well with the existing generated visitor model.

### Rich source tracking

Attaching `source_range` to:

- generated nodes
- matched terminals
- node damage entries

is one of the better parts of the API. This is a clear strength.

### Recovery metadata

The partial-recovery API is surprisingly useful:

- `parse_result::partial`
- `parse_tree<T>::partial`
- `parse_tree<T>::damaged_nodes()`
- `parse_tree<T>::repaired_input(...)`
- `node::is_damaged()`
- `node::damage()`

This is a meaningful feature, not just a debugging aid.

### Generated visitors and streaming

Generated `visit(...)`, `visit_recursive(...)`, and `operator<<` fit the rest of the system well. They make the generated AST types pleasant to use.

## Biggest redesign candidates

## Consistency and usability issues

## 1. `parse_tree<T>` copy semantics are surprising

The type is copyable, but copying behaves differently depending on when it happens:

- copies made before materialization can materialize independently
- copies made after materialization share the same materialized object through `shared_ptr`

### Why this is odd

The meaning of copying a parse-tree handle changes depending on timing. That is subtle and easy to misunderstand.

### Recommendation

Choose a more explicit model:

- make the handle move-only
- or store all lazy state in shared state so copies behave uniformly

## 2. `root_damage()` looks like public plumbing, not consumer API

`parse_tree<T>::root_damage()` appears to exist for generated-code plumbing, not for ordinary users.

### Recommendation

Remove it from the public consumer API surface or make it internal-only.

## 3. `node::add_damage(...)` probably should not be public

Consumers can currently mutate parser-generated damage metadata directly.

### Why this is odd

That makes it harder to distinguish parser-produced damage from user-authored annotations.

### Recommendation

Make `add_damage(...)` protected or internal-only unless user-authored damage annotations are an intentional public feature.

## 4. `type()` is likely redundant

`cpf::node` currently requires:

- `rule_id()`
- `type()`

But the runtime already has RTTI via a polymorphic base, and the generated API mostly leans on `rule_id()` plus visitors.

### Recommendation

Consider removing `type()` unless there is a strong documented use case for it.

## 5. `parse_error` does not align well with `source_position`

`parse_error` stores:

- `offset`
- `line`
- `column`

while the rest of the runtime uses `source_position` and `source_range`.

### Recommendation

Prefer:

- `source_position position;`

This would make error positions align with the rest of the API.

## 6. `parse_error.found` is too stringly typed

Examples of values currently include:

- quoted tokens like `"*"`
- `"<end of input>"`
- `"<ambiguous parse>"`
- `"<filtered parse>"`

### Why this is odd

Consumers have to string-match sentinel values to understand the kind of failure.

### Recommendation

Introduce structured error kinds and keep `message` as the display string.

## 7. `repaired_input(...)` has a slightly misleading name

The method is useful, but it is not a simple accessor. It reconstructs a repaired form of caller-provided input and may fail if the provided text no longer structurally matches the tree.

### Recommendation

A clearer name would be something like:

- `try_repair_input(...)`
- `reconstruct_repaired_input(...)`
- `repaired_input_from(...)`

My recommendation would be `try_repair_input(...)`

## Generated API oddities

## 8. `Complexity` as public mutable static state is awkward

Generated nodes expose:

- `static std::array<cpf::complexity, N> Complexity;`

and recomputation mutates it.

### Why this is odd

This exposes global mutable state directly in the public API and likely complicates thread-safety expectations.

### Recommendation

Hide the cache and expose accessor-based APIs instead.

## 9. Group-capture `std::variant<std::unique_ptr<...>>` payloads may be awkward to consume

These are type-safe, but less ergonomic than the rest of the inheritance-and-visitor-oriented API.

### Recommendation

Consider generating helper visitor functions for such payload members, or prefer a common generated base type when possible.

## 10. `visit_recursive(...)` is read-only only

The generated traversal helpers operate on const nodes.

### Recommendation

If AST rewriting is a goal, consider adding non-const traversal helpers as an additional API.

## Overall assessment

The current runtime API is not bad. In fact, its core concepts are fairly strong. The problem is that the API feels more exposed than designed.

### Strong parts

- lazy parse-tree handles
- forest-based ambiguity
- source ranges everywhere they matter
- practical recovery metadata
- generated visitor and streaming support

### Odd parts

- complexity metadata uses confusing terminology

## Final recommendation

Do not throw the API away. Keep the core concepts, but perform a cleanup pass that:

- separates public API from runtime internals
- makes status and error reporting more explicit
- fixes constness and naming issues
- makes the generated API read like one coherent system instead of several layers exposed at once

In short: the runtime is powerful, but the public API currently feels overexposed and inconsistent in a few important places. A focused redesign pass would pay off.

## Validation

The current behavior described above was cross-checked against the runtime-focused tests, and those test suites pass in the current workspace build configuration.


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

## 1. Promote recognition to a first-class public API

The system already has internal recognition support, but the public generated API only exposes `parse(...)` plus `parse_options{ .build_ast = false }`.

### Why this is odd

"Parse but do not build AST" is really a distinct operation. It is not intuitive that `parse(...)` can succeed while intentionally returning an empty forest.

### Recommendation

Expose generated entry points such as:

- `recognize(std::string_view input)`
- optionally `inspect(std::string_view input)`

That would better align public API shape with the existing internal capabilities.

## 2. Stop exposing so much of `cpf::detail`

`cpflib/runtime/runtime.h` currently exposes a large amount of parser-engine and generated-code plumbing:

- `parser_symbol`
- `production_spec`
- `grammar_spec`
- `parse_node`
- `parse_forest`
- `error_tracker`
- `earley_parse`
- `earley_recognize`
- `earley_inspect`
- many helper utilities

### Why this is a problem

The public header currently exposes:

- consumer API
- generated parser protocol
- parser-engine implementation details

This makes the API feel overexposed and invites accidental dependency on internals.

### Recommendation

Split the runtime into layers:

- public consumer API
- generated-code support API
- parser-engine internals

Even if the project stays header-only, these layers should not all live in the same public-facing header.

## Consistency and usability issues

## 3. `definition` is too vague a public name

`cpf::node` exposes public field:

- `std::size_t definition`

This actually means something like a production index or matched alternative index.

### Why this is odd

The word `definition` is vague. In context it seems to mean the production within a rule that matched.

### Recommendation

Rename to something clearer, such as:

- `production_index`
- `alternative_index`
- `reduction_index`

`production_index` is probably the clearest.

## 4. Complexity APIs use misleading parameter names

Generated node APIs expose:

- `complexity_inputs(std::size_t rule_id)`
- `recompute_complexity(std::size_t rule_id)`

But the parameter is not a rule id like `RuleId`. It is the per-rule definition/production index.

### Why this is odd

This is one of the clearest naming mismatches in the API.

### Recommendation

Rename the parameter and probably the conceptual model:

- `complexity_inputs(std::size_t definition_index)`
- `recompute_complexity(std::size_t definition_index)`

or introduce more explicit function names like `complexity_inputs_for_definition(...)`.

## 5. `parse_tree<T>` exposes mutable metadata fields publicly

Public fields today:

- `definition`
- `range`
- `partial`

### Why this is odd

These look like metadata snapshots, but they are mutable and can drift away from the actual materialized tree state.

### Recommendation

Prefer accessor methods instead:

- `definition() const`
- `range() const`
- `is_partial() const`

The same general concern applies to public mutable metadata on `cpf::node`.

## 6. `parse_tree<T>` copy semantics are surprising

The type is copyable, but copying behaves differently depending on when it happens:

- copies made before materialization can materialize independently
- copies made after materialization share the same materialized object through `shared_ptr`

### Why this is odd

The meaning of copying a parse-tree handle changes depending on timing. That is subtle and easy to misunderstand.

### Recommendation

Choose a more explicit model:

- make the handle move-only
- or store all lazy state in shared state so copies behave uniformly

## 7. `root_damage()` looks like public plumbing, not consumer API

`parse_tree<T>::root_damage()` appears to exist for generated-code plumbing, not for ordinary users.

### Recommendation

Remove it from the public consumer API surface or make it internal-only.

## 8. `node::add_damage(...)` probably should not be public

Consumers can currently mutate parser-generated damage metadata directly.

### Why this is odd

That makes it harder to distinguish parser-produced damage from user-authored annotations.

### Recommendation

Make `add_damage(...)` protected or internal-only unless user-authored damage annotations are an intentional public feature.

## 9. `type()` is likely redundant

`cpf::node` currently requires:

- `rule_id()`
- `type()`

But the runtime already has RTTI via a polymorphic base, and the generated API mostly leans on `rule_id()` plus visitors.

### Recommendation

Consider removing `type()` unless there is a strong documented use case for it.

## 10. `parse_error` does not align well with `source_position`

`parse_error` stores:

- `offset`
- `line`
- `column`

while the rest of the runtime uses `source_position` and `source_range`.

### Recommendation

Prefer:

- `source_position position;`

This would make error positions align with the rest of the API.

## 11. `parse_error.found` is too stringly typed

Examples of values currently include:

- quoted tokens like `"*"`
- `"<end of input>"`
- `"<ambiguous parse>"`
- `"<filtered parse>"`

### Why this is odd

Consumers have to string-match sentinel values to understand the kind of failure.

### Recommendation

Introduce structured error kinds and keep `message` as the display string.

## 12. `repaired_input(...)` has a slightly misleading name

The method is useful, but it is not a simple accessor. It reconstructs a repaired form of caller-provided input and may fail if the provided text no longer structurally matches the tree.

### Recommendation

A clearer name would be something like:

- `try_repair_input(...)`
- `reconstruct_repaired_input(...)`
- `repaired_input_from(...)`

My recommendation would be `try_repair_input(...)`

## Generated API oddities

## 13. `Complexity` as public mutable static state is awkward

Generated nodes expose:

- `static std::array<cpf::complexity, N> Complexity;`

and recomputation mutates it.

### Why this is odd

This exposes global mutable state directly in the public API and likely complicates thread-safety expectations.

### Recommendation

Hide the cache and expose accessor-based APIs instead.

## 14. `RuleId`, `ReductionCount`, and `definition` do not read as one coherent naming system

These concepts are related, but the naming family is inconsistent.

### Recommendation

If a rename pass happens, make them read as one set of concepts, for example:

- `RuleId`
- `ProductionCount`
- `production_index`

or normalize them more aggressively to project-wide naming.

## 15. Group-capture `std::variant<std::unique_ptr<...>>` payloads may be awkward to consume

These are type-safe, but less ergonomic than the rest of the inheritance-and-visitor-oriented API.

### Recommendation

Consider generating helper visitor functions for such payload members, or prefer a common generated base type when possible.

## 16. `visit_recursive(...)` is read-only only

The generated traversal helpers operate on const nodes.

### Recommendation

If AST rewriting is a goal, consider adding non-const traversal helpers as an additional API.

## Structural recommendation

## 17. Split the runtime into explicit layers

If only one architectural cleanup happens, it should probably be this.

### Public consumer API

Keep only:

- `parse_options`
- `parse_error`
- `source_position`
- `source_range`
- `matched_string`
- `node_damage_reason`
- `node_damage`
- `node`
- `parse_tree<T>`
- `parse_result<T>`
- public complexity types if desired

### Generated-code support API

Move here:

- internal `parse_tree` construction support
- opaque-tree helpers
- grammar descriptor types needed by generated code

### Parser-engine internals

Move here:

- Earley parser structures
- error tracking helpers
- repair helpers
- ambiguity inspection internals

This would improve API quality even before any semantic redesign.

## If only two things change

These are the top recommendations, in order:

1. add first-class recognition APIs instead of relying on `build_ast = false`
2. hide generated-runtime plumbing and parser internals from the public runtime header

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
- public headers expose too much plumbing

## Final recommendation

Do not throw the API away. Keep the core concepts, but perform a cleanup pass that:

- separates public API from runtime internals
- makes status and error reporting more explicit
- fixes constness and naming issues
- makes the generated API read like one coherent system instead of several layers exposed at once

In short: the runtime is powerful, but the public API currently feels overexposed and inconsistent in a few important places. A focused redesign pass would pay off.

## Validation

The current behavior described above was cross-checked against the runtime-focused tests, and those test suites pass in the current workspace build configuration.


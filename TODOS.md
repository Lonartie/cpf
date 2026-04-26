# TODO's

This file is an ordered roadmap rather than a flat checklist. Items at the top are the most practical near-term investments.

## 1. Sanitizer and compiler matrix in CI

**Idea** Extend CI with sanitizer jobs and a broader compiler matrix.

**Considerations**

* Add ASan and UBSan jobs
* Build with Clang, GCC, and MSVC where possible
* Treat sanitizer findings as release blockers

**Impact** High impact, medium implementation effort.

**Value** This reduces the risk of undefined behavior, memory bugs, and platform-specific regressions in parser and runtime code.

## 2. Grammar diagnostics and linting

**Idea** Add static analysis for grammars before or during generation.

**Considerations**

* Detect unreachable and unused rules
* Report nullable cycles and suspicious recursive patterns
* Add grammar analysis summaries during generation

**Impact** High impact, medium implementation effort.

**Value** This makes grammar authoring much easier and helps users fix structural issues before runtime.

## 3. Ambiguity diagnostics and inspection tools

**Idea** Go beyond ambiguity failure and provide useful diagnostics for ambiguous parses.

**Considerations**

* Report the ambiguous source range and involved rules
* Provide capped derivation previews instead of only failing on ambiguity
* Expose a structured diagnostics API

**Impact** High impact, medium implementation effort.

**Value** This would be one of the most useful differentiators of CPF because ambiguous grammars are otherwise very hard to debug.

## 4. Grammar ergonomics: skip rules, token definitions, and lexer-like conveniences

**Idea** Improve the grammar language so larger grammars are easier to read and maintain.

**Considerations**

* Add explicit whitespace and comment skip rules
* Separate token-style definitions from parser rules more clearly
* Improve large grammar readability

**Impact** High impact, medium implementation effort.

**Value** This reduces grammar noise and makes CPF feel much more practical for real-world inputs.

## 5. Parse forest introspection API

**Idea** Expose controlled access to opaque parse forest data without forcing AST materialization.

**Considerations**

* Inspect opaque forest nodes without materializing ASTs
* Expose forest statistics, child relationships, definitions, and ranges
* Support tooling on top of the lazy parse forest

**Impact** High impact, medium implementation effort.

**Value** This builds directly on the lazy parse forest design and enables better tooling, diagnostics, and analysis.

## 7. Golden tests for generated code

**Idea** Add snapshot-style tests for representative generated outputs.

**Considerations**

* Snapshot representative generated headers and sources
* Lock in namespace, precedence, import, and lazy-forest behavior
* Catch subtle generator regressions quickly

**Impact** High impact, low-to-medium implementation effort.

**Value** This gives strong regression protection for a code generator, where small changes can have wide effects.

## 8. Benchmark regression tracking

**Idea** Turn benchmarking into an ongoing regression-detection tool instead of a manual inspection step.

**Considerations**

* Save benchmark baselines
* Compare benchmark output across changes
* Warn or fail on significant regressions

**Impact** Medium-to-high impact, medium implementation effort.

**Value** This makes the benchmark work already in the repository much more actionable.

## 9. AST and visitor ergonomics improvements

**Idea** Improve the usability of generated ASTs and visitors for downstream consumers.

**Considerations**

* Add more traversal helpers and debug dumps
* Consider JSON and Graphviz export helpers
* Improve inspection of generated trees during debugging

**Impact** Medium impact, medium implementation effort.

**Value** This improves developer experience and makes generated parsers easier to inspect and integrate.

## 10. Better grammar language expressiveness

**Idea** Lift current language limitations and add better constructs for larger grammars.

**Considerations**

* Lift current labeled-group limitations
* Add more rule visibility and metadata options
* Improve precedence-table ergonomics for larger grammars

**Impact** Medium-to-high impact, medium implementation effort.

**Value** This expands what users can express naturally without awkward grammar workarounds.

## 11. Cookbook and architecture documentation in `wiki`

**Idea** Expand the documentation from reference material into practical and contributor-oriented guides.

**Considerations**

* Add practical recipes for common grammar patterns
* Document the parser, runtime, and codegen architecture
* Keep docs aligned with tested behavior

**Impact** Medium impact, low-to-medium implementation effort.

**Value** This helps both users and contributors become productive much faster.

## 12. Code generation customization hooks

**Idea** Allow users to adapt generated code shape without forking the generator.

**Considerations**

* Allow custom base classes and selected generated helpers
* Offer more control over emitted API shape without forking codegen

**Impact** Medium impact, medium implementation effort.

**Value** This makes CPF easier to adopt in more opinionated codebases and larger systems.

## 13. Generated CST mode in addition to AST mode

**Idea** Support concrete syntax tree generation alongside the existing AST-oriented mode.

**Considerations**

* Preserve punctuation and trivia for source-to-source tooling
* Support lossless transformations and refactoring workflows

**Impact** High strategic impact, high implementation effort.

**Value** This opens the door to formatters, refactoring tools, and editor workflows that need lossless syntax preservation.

## 14. Incremental parsing and reusable parse state

**Idea** Reuse parse work across small edits instead of reparsing the full input every time.

**Considerations**

* Reuse parse work across small edits
* Target editor, LSP, and interactive tooling scenarios
* Preserve stable subtree identities where possible

**Impact** Very high strategic impact, very high implementation effort.

**Value** This would make CPF much more compelling for interactive tooling and language-server scenarios.

## 15. Alternate parsing backends or specialized fast paths

**Idea** Investigate specialized parsing strategies for grammar subsets that can run faster than the general path.

**Considerations**

* Investigate deterministic-grammar optimizations
* Consider optional packrat-like or specialized modes for suitable grammars

**Impact** High strategic impact, high design and implementation effort.

**Value** This could broaden CPF from a flexible parser generator into a more performance-adaptive platform.

## 16. Grammar packaging and multi-root workflows

**Idea** Improve support for large grammar ecosystems and reusable grammar modules.

**Considerations**

* Support exported entry points and reusable grammar modules
* Improve large-project integration

**Impact** Medium-to-high impact, medium-to-high implementation effort.

**Value** This helps CPF scale from single-grammar experiments to larger multi-module projects.

## 19. visit_recursive shall optionally pass down the parent node to the visitor

**Idea** Allow `visit_recursive` to pass down the parent node to the visitor for more context-aware traversals.

**Considerations**

* Add an optional parameter to `visit_recursive` for the parent node
* Update visitor signatures to accept the parent node when needed

**Impact** Medium impact, medium implementation effort.

**Value** This would enable more powerful visitor patterns that can make decisions based on the parent context, improving the expressiveness of tree traversals.

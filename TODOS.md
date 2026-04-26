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

## 13. Generated CST mode in addition to AST mode

**Idea** Support concrete syntax tree generation alongside the existing AST-oriented mode.

**Considerations**

* Preserve punctuation and trivia for source-to-source tooling
* Support lossless transformations and refactoring workflows

**Impact** High strategic impact, high implementation effort.

**Value** This opens the door to formatters, refactoring tools, and editor workflows that need lossless syntax preservation.

# TODO's

This file is an ordered roadmap rather than a flat checklist. Items at the top are the most practical near-term investments.

## 1. Grammar diagnostics and linting

**Idea** Add static analysis for grammars before or during generation.

**Considerations**

* Detect unreachable and unused rules
* Report nullable cycles and suspicious recursive patterns
* Add grammar analysis summaries during generation

**Impact** High impact, medium implementation effort.

**Value** This makes grammar authoring much easier and helps users fix structural issues before runtime.

## 2. Ambiguity diagnostics and inspection tools

**Idea** Go beyond ambiguity failure and provide useful diagnostics for ambiguous parses.

**Considerations**

* Report the ambiguous source range and involved rules
* Provide capped derivation previews instead of only failing on ambiguity
* Expose a structured diagnostics API

**Impact** High impact, medium implementation effort.

**Value** This would be one of the most useful differentiators of CPF because ambiguous grammars are otherwise very hard to debug.

## 3. Golden tests for generated code

**Idea** Add snapshot-style tests for representative generated outputs.

**Considerations**

* Snapshot representative generated headers and sources
* Lock in namespace, precedence, import, and lazy-forest behavior
* Catch subtle generator regressions quickly

**Impact** High impact, low-to-medium implementation effort.

**Value** This gives strong regression protection for a code generator, where small changes can have wide effects.

## 4. Generated CST mode in addition to AST mode

**Idea** Support concrete syntax tree generation alongside the existing AST-oriented mode.

**Considerations**

* Preserve punctuation and trivia for source-to-source tooling
* Support lossless transformations and refactoring workflows

**Impact** High strategic impact, high implementation effort.

**Value** This opens the door to formatters, refactoring tools, and editor workflows that need lossless syntax preservation.

## 5. Provide extensive tracking/debugging/logging/tracing APIs

**Idea** Expose rich APIs for tracking and debugging the parsing process for both grammar-code-gen and generated parsers.

**Considerations**

* Provide hooks for logging parser events
* Allow inspection of intermediate parse states
* Support configurable verbosity levels
* Make the APIs intentionally external to the model classes, they should live in a separate diagnostics namespace in the
  public API in `cpflib`
* GOAL: Every step of the parsing process should be inspectable and traceable, from tokenization to final AST construction.
  That includes the grammar generation process as well, so that users can understand how their grammar is being transformed into code
  and where potential issues may arise.

**Impact** High impact for users building tools on top of CPF, medium implementation effort.

**Value** This would make it much easier to understand, debug, and optimize parsing behavior, especially for complex grammars.


# TODO's

This file is an ordered roadmap rather than a flat checklist. Items at the top are the most practical near-term investments.

## 1. Generated CST mode in addition to AST mode

**Idea** Support concrete syntax tree generation alongside the existing AST-oriented mode.

**Considerations**

* Preserve punctuation and trivia for source-to-source tooling
* Support lossless transformations and refactoring workflows

**Impact** High strategic impact, high implementation effort.

**Value** This opens the door to formatters, refactoring tools, and editor workflows that need lossless syntax preservation.

## 2. Provide extensive tracking/debugging/logging/tracing APIs

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

## 3. Provide an API for generating parsers dynamically at runtime without generating code and relying the build pipeline

**Idea** Support dynamic parser generation in-memory without writing code to disk or relying on a build step.

**Considerations**

* It should support the same grammar specification as the code generator, but produce an in-memory parser instance instead of source code.
* Of course this doesn't allow to generate classes and structs so the API should be designed to be as generic as possible.
* No features shall be sacrificed, only the API patterns may differ. For example a node might have a map of named children instead.
* Types can be facilitated by a dynamic std::string type field.
* And so on...
* For this we should lift necessary logic from `cpfgen` into `cpflib` and make it available for both codegen and dynamic generation.

**Impact** High impact for users needing dynamic parsing capabilities, high implementation effort.

**Value** This would enable use cases like REPLs, dynamic DSLs, and runtime code analysis without needing a separate build step. 
It would also make CPF more flexible and accessible for a wider range of applications.
# TODO's

This file is an ordered roadmap rather than a flat checklist. Items at the top are the most practical near-term investments.

## Completed: Dynamic runtime parser compilation

CPF now supports parsing `.cpf` grammars directly at runtime through `cpf::compile_grammar(...)` and
`cpf::compile_grammar_file(...)` in `cpflib`.

Delivered scope:

* The runtime compiler accepts the same grammar syntax as code generation.
* No parsing features were removed: lexer reuse, named entry rules, ambiguity handling, partial recovery,
  precedence filtering, CST construction, imports, templates, groups, quantifiers, lookahead, and custom error labels
  are all available through the runtime API.
* `cpflib` now owns the shared grammar model, parser, loader, and analyzer used by both runtime compilation and
  `cpfgen`.
* Runtime parsing materializes a generic `cpf::dynamic_node` tree with named `cpf::dynamic_field` entries and dynamic
  type names instead of generated C++ classes.

# Grammar reference

CPF reads `.cpf` grammar files and emits matching C++20 parser code.

## Supported grammar features

CPF currently supports:

- grammar-level trivia via `skip` declarations and `@whitespace`
- explicit `token` declarations
- literals and regex terminals
- labeled captures
- choice-style base rules
- precedence and associativity attributes
- quantified symbols: `?`, `*`, `+`, `{n}`
- grouped alternatives
- full labeled group captures, including multi-symbol and quantified groups
- lookahead predicates: `!symbol` and `&symbol`
- commit/cut markers: `!>`
- parameterized grammar templates
- static grammar diagnostics and linting
- multi-file grammars through `@import`
- generated-code namespaces

Grammar strings may use either single quotes or double quotes for literals, regex bodies, quoted attribute values, and
import paths.

## Example grammar

```text
expression -> addition | subtraction | multiplication | division | number;

addition        [prec = 'sub']              -> expression:left '+':op expression:right;
subtraction     [prec < 'div', lbl = 'sub'] -> expression:left '-':op expression:right;
multiplication  [prec = 'div']              -> expression:left '*':op expression:right;
division        [prec < 'num', lbl = 'div'] -> expression:left '/':op expression:right;
number          [lbl = 'num']               -> r'[0-9]+':value;
```

Labels are optional. Labeled symbols become members on the generated node type. Unlabeled symbols still participate in
parsing but are not stored in the AST.

## Rule attributes

| Name   | Values          | Example              | Meaning                                     |
|--------|-----------------|----------------------|---------------------------------------------|
| `prec` | `= <num>`       | `[prec = 10]`        | Absolute precedence rank                    |
| `prec` | `= <str>`       | `[prec = 'sum']`     | Same precedence group as a label            |
| `prec` | `< <str>`       | `[prec < 'product']` | Lower precedence than the referenced label  |
| `prec` | `> <str>`       | `[prec > 'sum']`     | Higher precedence than the referenced label |
| `dir`  | `left`, `right` | `[dir = right]`      | Operator associativity                      |
| `lbl`  | `<str>`         | `[lbl = 'number']`   | Rule label used for precedence grouping     |
| `error`| `<str>`         | `[error = 'expected identifier']` | Custom expected-label used in parse errors |

Default behavior when attributes are omitted:

- infix-rule precedence defaults to source order
- associativity defaults to `left`
- labels default to the rule identifier

Custom parse-error labels can be attached to rules:

```text
identifier [error = 'expected identifier'] -> r'[A-Za-z_][A-Za-z0-9_]*':value;
assignment -> identifier:name '=':assign identifier:value;
```

When parsing fails while the generated parser is expecting `identifier`, CPF now reports `expected identifier`
instead of the default `rule 'identifier'` wording.

## Tokens and lexical helper rules

CPF grammars can declare reusable token rules explicitly:

```text
token identifier -> identifier_head identifier_tail*;
identifier_head -> r'[A-Za-z_]';
identifier_tail -> r'[A-Za-z0-9_]';
```

Parser rules can reference tokens exactly like ordinary rules:

```text
qualified_identifier -> identifier ('.' identifier)*;
binding -> 'let':keyword qualified_identifier:name ':':colon value_type:type '=':assign identifier:value ';':semi;
value_type -> 'int' | 'void';
```

Rules:

- `token <identifier> -> ...;` declares an explicitly lexical rule
- token declarations do not accept rule attributes such as `[prec = ...]`
- ordinary helper rules reachable from token declarations are inferred as lexical automatically when they lower only to terminals and other lexical rules
- references to lexical rules capture `cpf::matched_string` values instead of nested AST nodes
- direct parse entry points are still generated for these rules for compatibility with the existing API surface

The generated parser now also exposes the generated lexer publicly. For any generated root such as `binding`, callers can use:

```c++
auto tokens = binding::lex("let foo:int = bar;");
auto recognized = binding::recognize(tokens);
auto parsed = binding::parse(tokens);
```

Lexing behavior:

- explicit `token` declarations become physical lexer token definitions
- inline parser terminals such as `'let'` and `r'[0-9]+'` also participate in lexer tokenization
- inline terminals lowered through grouped, quantified, or templated helper rules keep the lexer precedence of their originating rule declarations
- the lexer emits a pure token stream; the parser no longer mixes token matching with raw character matching
- longest match wins first
- when two equal-length matches compete, literal tokens win over regex tokens
- equal-length conflicts between two literals or between two regexes are resolved by the generated lexer priority order derived from the grammar

## Quantifiers and groups

Supported postfix forms:

| Syntax      | Meaning                  |
|-------------|--------------------------|
| `symbol?`   | optional symbol          |
| `symbol*`   | zero or more repetitions |
| `symbol+`   | one or more repetitions  |
| `symbol{n}` | exactly `n` repetitions  |

The same forms work for parenthesized groups:

```text
grouped_value -> ('x':text | 'y':text);
grouped_sentence -> '(':open ('hi':text | 'bye':text) ')':close;
grouped_repeat -> ('a' | 'b')+;
grouped_choice_value -> ('x' | 'y'):value;
grouped_pair -> ('x':first 'y':second | 'z':first 'w':second):value;
grouped_signed_number -> ('-':sign number:value | number:value):payload;
grouped_pairs -> ('a':text 'b':suffix)+:pairs;
```

Single-symbol grouped choices can still be captured into one generated member:

```text
message -> greeting | farewell;
greeting -> 'hello':text;
farewell -> 'bye':text;

payload -> (greeting | farewell):value;
token -> ('x' | 'y'):value;
```

This lowers through hidden helper rules, but the generated public API still exposes `payload::value` as
`std::variant<std::unique_ptr<greeting>, std::unique_ptr<farewell>>` and `token::value` as `cpf::matched_string`.

When a labeled group needs to preserve more structure than a single captured symbol can represent, CPF generates a
dedicated helper node type for that group. This is how multi-symbol groups, quantified labeled groups, and labeled
groups with inner captures remain visible in the generated AST.

Generated member types follow the captured symbol kind:

- optional references: `std::unique_ptr<rule>`
- optional terminals: `std::optional<cpf::matched_string>`
- repeated references: `std::vector<std::unique_ptr<rule>>`
- repeated terminals: `std::vector<cpf::matched_string>`

## Lookahead predicates and cut markers

CPF supports zero-width local disambiguation markers in parser rules:

```text
lookahead_keyword -> 'if' | 'else' | 'while';
lookahead_identifier -> !lookahead_keyword r'[A-Za-z_][A-Za-z0-9_]*':value;
lookahead_call -> lookahead_identifier:name &'(' '(':open ')':close;
lookahead_statement -> 'if':keyword !> '(':open lookahead_identifier:condition ')':close lookahead_identifier:body
					| lookahead_identifier:name;
```

Rules:

- `!symbol` succeeds when `symbol` does **not** match at the current token position
- `&symbol` succeeds when `symbol` **does** match at the current token position
- predicates are zero-width: they do not consume input and they do not create AST fields
- `!>` commits to the current alternative once the production prefix before the marker has matched
- later alternatives in the same rule are suppressed automatically when an earlier cut-prefix matches

This is useful for:

- reserved-word exclusion without splitting the grammar into helper rules
- requiring a delimiter or keyword before consuming it in the main production
- preventing fallback alternatives from masking a more specific branch after a distinguishing prefix

## Parameterized templates

CPF templates capture reusable single-production or multi-production grammar shapes:

```text
template template_surrounded<Open, Inner, Close> -> Open:open Inner:value Close:close;
template template_keyword_value<Keyword, Value> -> Keyword:keyword Value:value;
template template_specialized_surrounded<Open, InnerTempl, Close> -> Open:open InnerTempl<'spec'>:value Close:close;
template template_prepend<Prep> -> Prep:prep '_value':suffix;

template_paren_identifier -> template_surrounded<'(', template_identifier, ')'>:body;
template_brace_identifiers -> template_surrounded<'{', template_identifier+, '}'>:body;
template_returned_identifier -> template_keyword_value<'return', template_identifier>:payload;
template_specialized_identifier -> template_specialized_surrounded<'(', template_prepend, ')'>:body;
```

Rules:

- `template name<Param, ...> -> ...;` declares a reusable grammar template
- template invocations appear anywhere a normal grammar symbol can appear
- arguments can be literals, regex terminals, rule references, grouped expressions, and quantified items
- template declarations are compile-time only and do not become public generated entry points
- each invocation lowers to a hidden synthetic helper rule, so labeled invocations can still materialize helper-node payloads

Template parameters may also stand in for template families. For example, an outer template can specialize a nested
template parameter in place:

```text
template template_specialized_surrounded<Open, InnerTempl, Close> -> Open:open InnerTempl<'spec'>:value Close:close;
template template_prepend<Prep> -> Prep:prep '_value':suffix;

template_specialized_identifier -> template_specialized_surrounded<'(', template_prepend, ')'>:body;
```

This matches `(`, then `spec`, then `_value`, then `)` and still materializes the nested helper node under `value`.

Template arguments inherit labels and quantifiers from the argument expression itself unless the template placeholder
adds its own suffix. This makes list-style utilities such as `template_surrounded<'{', item+, '}'>` preserve repeated
storage automatically in the generated AST.

## Grammar diagnostics and linting

CPF can analyze a parsed grammar before or during code generation:

```c++
auto grammar = cpf::parse_grammar(source_text);
auto analysis = cpf::analyze_grammar(grammar);

if (analysis.has_warnings() || analysis.has_errors()) {
   std::cerr << analysis.render_summary() << '\n';
}
```

The `cpfgen` executable runs the same analysis automatically. Any diagnostics it finds are printed to `stdout` while
generation is running so grammar authors can see suspicious structures immediately.

When diagnostics come from rules that entered the grammar through `@import`, CPF remaps the reported file and line back
to the original imported source location instead of the expanded post-import line in the preprocessed grammar text.

Current diagnostics include:

- unused rules and token rules that are never referenced by any other rule
- rules that are not reachable from the grammar's primary entry rule
- nullable cycles that can recurse without consuming input
- suspicious self-recursive productions whose surrounding symbols are all nullable

CPF's Earley runtime accepts left recursion, empty productions, and nullable recursive cycles. Nullable-cycle findings
therefore remain warnings: they highlight grammars that may be surprising, ambiguous, or harder to reason about, but
they do not block code generation on their own.

The analysis summary treats the **first non-token rule** in the grammar as the primary entry rule. Reachability lints are
reported relative to that rule so disconnected helper subgraphs are easy to spot while still leaving additional public
entry points available in the generated API.

`cpf::generate_code(...)` also returns the same analysis result alongside the generated header and source text so
generator clients can surface warnings in their own tooling without re-running analysis separately.

Only diagnostics with blocking severity prevent code generation. Warning-level findings, including nullable-cycle
diagnostics, still emit the generated header/source pair.

## Multi-file grammars

Grammars can preprocess other grammars with `@import`:

```text
@import 'imports/imported_expr.cpf';
@import 'imports/imported_words.cpf';

imported_bundle_message -> imported_bundle_greeting | imported_bundle_parting;
```

Double-quoted imports such as `@import "imports/imported_expr.cpf";` work the same way.

`@import` behaves like a preprocessing include step: CPF recursively replaces the `@import ...;` directive with the
raw contents of the referenced file before any grammar parsing happens. Paths are resolved relative to the importing
file, expanded transitively, and cycle-checked.

Consequences:

- repeated `@import` directives expand repeatedly, just like repeated textual inclusion
- imported declarations participate in parsing exactly as if their source text had been pasted into the importing file
- parse errors are reported against the final preprocessed grammar text

See [`generation-and-integration.md`](./generation-and-integration.md) for the corresponding loader and generator APIs.

## Trivia and skipped input

CPF grammars can declare ignorable trivia with top-level `skip` declarations.

```text
@whitespace ws;
skip ws -> r'[ \t\r\n]+';
skip line_comment -> r'//[^\n]*';

expression -> addition | number;
addition -> expression:left '+':op expression:right;
number -> r'[0-9]+':value;
```

Rules:

- `skip <identifier> -> <terminal>;` declares one ignorable literal or regex terminal
- `@whitespace <identifier>;` optionally selects which declared skip rule is the canonical whitespace rule
- skip rules are not exposed as generated AST nodes or parse entry points
- when no `@whitespace` directive is present, CPF keeps its existing default behavior of skipping ordinary C/C++ whitespace between tokens

Skip rules participate in generated lexing directly:

- `lex(...)` removes configured skip tokens before building the returned `cpf::token_sequence`
- skipped input still influences the source ranges stored on produced tokens and AST captures
- token-sequence `parse(...)` and `recognize(...)` reuse those already-filtered tokens instead of re-running skip handling

`@namespace` is not part of the grammar language. Generated C++ namespaces are still configured through
`cpf::generate_code(..., code_namespace)`, `cpfgen --namespace ...`, or `cpf_link_grammars(... NAMESPACE ...)`.


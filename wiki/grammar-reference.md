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
- the lexer emits a pure token stream; the parser no longer mixes token matching with raw character matching
- longest match wins first
- equal-length conflicts are resolved by the generated lexer priority order derived from the grammar

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


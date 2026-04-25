# Grammar reference

CPF reads `.cpf` grammar files and emits matching C++20 parser code.

## Supported grammar features

CPF currently supports:

- literals and regex terminals
- labeled captures
- choice-style base rules
- precedence and associativity attributes
- quantified symbols: `?`, `*`, `+`, `{n}`
- grouped alternatives
- labeled single-symbol group captures such as `('x' | 'y'):value`
- multi-file grammars through `import`
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

Default behavior when attributes are omitted:

- infix-rule precedence defaults to source order
- associativity defaults to `left`
- labels default to the rule identifier

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
```

Single-symbol grouped choices can also be captured into one generated member:

```text
message -> greeting | farewell;
greeting -> 'hello':text;
farewell -> 'bye':text;

payload -> (greeting | farewell):value;
token -> ('x' | 'y'):value;
```

This lowers through hidden helper rules, but the generated public API still exposes `payload::value` as
`std::variant<std::unique_ptr<greeting>, std::unique_ptr<farewell>>` and `token::value` as `cpf::matched_string`.

Current limitation: labeled groups must lower to exactly one symbol per alternative, so forms such as
`('x' 'y' | 'z'):value` are rejected.

Generated member types follow the captured symbol kind:

- optional references: `std::unique_ptr<rule>`
- optional terminals: `std::optional<cpf::matched_string>`
- repeated references: `std::vector<std::unique_ptr<rule>>`
- repeated terminals: `std::vector<cpf::matched_string>`

## Multi-file grammars

Grammars can import other grammars:

```text
import 'imports/imported_expr.cpf';
import 'imports/imported_words.cpf';

imported_bundle_message -> imported_bundle_greeting | imported_bundle_parting;
```

Double-quoted imports such as `import "imports/imported_expr.cpf";` work the same way.

Imports are resolved relative to the importing file, expanded transitively, and cycle-checked.

See [`generation-and-integration.md`](./generation-and-integration.md) for the corresponding loader and generator APIs.


# Grammar extensions proposal

This document collects grammar-language extensions that would make CPF grammars more expressive and more pleasant to author.

The suggestions below are based on the currently documented surface in [`wiki/grammar-reference.md`](./wiki/grammar-reference.md), the parser tests in [`cpftools/cpflibtests/parser/grammar_tests.cpp`](./cpftools/cpflibtests/parser/grammar_tests.cpp), and the existing roadmap notes in [`TODOS.md`](./TODOS.md).

## Design goals

- reduce boilerplate in larger grammars
- make token-heavy grammars easier to read
- lift current structural limitations without hiding the generated AST shape
- improve diagnostics and maintainability for real-world grammars
- stay compatible with CPF's current explicit, C++-friendly style

## Recommended near-term extensions

### 3. Operator precedence blocks and table syntax

**Problem**

The current `[prec = ...]`, `[prec < ...]`, and `[dir = ...]` attributes are flexible, but expression grammars become verbose quickly.

**Proposed syntax**

```text
precedence expression {
   left '+' '-';
   left '*' '/';
   right '^';
}
```

**Example**

```text
precedence expression {
   left '+' '-';
   left '*' '/';
}

expression -> expression:left '+':op expression:right
            | expression:left '-':op expression:right
            | expression:left '*':op expression:right
            | expression:left '/':op expression:right
            | number;

number -> r'[0-9]+':value;
```

Another, more structured form could be:

```text
expression {
   left  '+' '-' => additive;
   left  '*' '/' => multiplicative;
   atom  number;
}
```

**Why it helps**

- reduces precedence boilerplate
- makes operator grammars easier to audit
- lowers the entry cost for larger grammars like `simple_c.cpf`

**Notes**

This can be implemented as sugar over the current precedence metadata rather than a separate parsing model.

### 4. Full labeled groups, including quantified and multi-symbol groups

**Problem**

The current grammar supports labeled single-symbol groups, but rejects useful forms such as multi-symbol alternatives, quantified labeled groups, and labeled groups with inner captures.

Current documented limitation:

```text
('x' 'y' | 'z'):value
```

is rejected.

**Proposed syntax**

Keep the same syntax, but allow richer grouped captures:

```text
call -> identifier:callee '(':open (argument (',':comma argument)*):args ')':close;
value -> ('-':sign number:value | number:value):payload;
list  -> ('[':open element*:items ']':close):body;
```

**Example**

```text
argument_list -> ('(':open expression:first (',':comma expression:rest)* ')':close):args;
```

Possible generated shape:

- a dedicated synthetic node/struct for the labeled group
- or a generated variant/struct depending on the group shape

**Why it helps**

- removes an artificial limitation that appears quickly in real grammars
- makes grouped syntax usable for more than trivial single-token captures
- avoids awkward helper-rule expansion in user-written grammars

**Notes**

This is one of the clearest extensions already suggested by the current roadmap and tests.

### 5. Separated-list sugar

**Problem**

Comma-separated and delimiter-separated lists are common, but they currently require repetitive hand-written patterns.

**Proposed syntax**

Possible forms:

```text
arguments -> sep_by(expression, ','):values;
arguments -> sep_by1(expression, ','):values;
parameters -> trailing_sep_by(identifier, ','):names;
```

**Example**

```text
call -> identifier:callee '(':open sep_by(expression, ','):args ')':close;
enum_decl -> 'enum':keyword identifier:name '{':open trailing_sep_by(identifier, ','):members '}':close;
```

**Why it helps**

- eliminates one of the most repetitive grammar patterns
- makes grammars easier to scan
- gives the generator a single place to define AST shapes for list patterns

**Notes**

If function-like syntax feels too special-purpose, a dedicated infix form would also work, for example `expression % ','`.

### 6. Rule visibility, entry-point, and export metadata

**Problem**

Imported multi-file grammars are supported, but larger grammar ecosystems need a better way to distinguish public entry points from helper-only rules.

**Proposed syntax**

```text
public translation_unit -> declaration*:declarations;
public expression -> add | multiply | atom;
private additive_tail -> '+':op expression:right;
export expression;
```

**Example**

```text
public module -> declaration*:declarations;
private declaration -> function | variable_decl;
private identifier -> r'[A-Za-z_][A-Za-z0-9_]*':value;
```

**Why it helps**

- makes generated APIs smaller and easier to understand
- improves packaging for shared grammar libraries
- fits well with the roadmap item about multi-root workflows and reusable grammar modules

**Notes**

This could control which parse entry points become public and which helper rules stay implementation details.

### 7. Lookahead predicates and commit/cut markers

**Problem**

Some grammars need local disambiguation or early failure without fully rewriting the grammar shape. Today authors have to encode that indirectly.

**Proposed syntax**

```text
identifier -> !keyword r'[A-Za-z_][A-Za-z0-9_]*':value;
call -> identifier:name &('(') arguments:args;
if_stmt -> 'if' '(':open expression:condition ')':close !'else' statement:then_branch;
```

Potential cut form:

```text
if_stmt -> 'if' !> '(':open expression:condition ')':close statement:then_branch;
```

**Example**

```text
keyword -> 'if' | 'else' | 'while';
identifier -> !keyword r'[A-Za-z_][A-Za-z0-9_]*':value;
```

**Why it helps**

- improves grammar expressiveness for reserved-word exclusion and local ambiguity control
- can produce better failure locations and messages
- reduces the need for helper-rule contortions

**Notes**

This is more advanced than the earlier items, but becomes valuable once CPF targets editor and tooling scenarios.

### 8. Parameterized grammar macros or templates

**Problem**

Many grammars repeat the same structural motifs: delimited blocks, parenthesized payloads, separated lists, keyword-plus-body constructs, and so on.

**Proposed syntax**

```text
template delimited<Open, Inner, Close> -> Open:open Inner:value Close:close;
template binary_chain<Item, Op> -> Item:first (Op:ops Item:rest)*;
```

**Example**

```text
template surrounded<Open, Inner, Close> -> Open:open Inner:value Close:close;
paren_expr -> surrounded<'(', expression, ')'>:body;
block -> surrounded<'{', statement*, '}'>:body;
```

**Why it helps**

- reduces copy-paste across larger grammars
- encourages consistent AST shape conventions
- pairs naturally with imports for shared grammar utility libraries

**Notes**

This is a higher-effort extension, but it would be a strong differentiator once the base grammar surface settles.

### 9. Custom diagnostic annotations on rules and symbols

**Problem**

CPF already reports structured parse errors, but grammar authors cannot currently tailor the most user-facing parts of those messages.

**Proposed syntax**

```text
identifier [error = 'expected identifier'] -> r'[A-Za-z_][A-Za-z0-9_]*':value;
argument_list -> '(':open sep_by(expression, ','):args ')':close [error = 'expected argument list'];
```

**Example**

```text
number [error = 'expected decimal number'] -> r'[0-9]+':value;
```

**Why it helps**

- improves end-user parse errors without code changes
- makes grammar files the single source of truth for domain terminology
- complements the roadmap items on grammar diagnostics and ambiguity diagnostics

**Notes**

Even a minimal first version that only overrides one default error label would already be useful.

## Suggested priority order

### Highest value soon

1. skip rules and trivia declarations
2. named token declarations
3. full labeled groups
4. precedence blocks
5. separated-list sugar

These would materially improve day-to-day authoring of real grammars without changing CPF's core model too much.

### Good follow-up work

6. rule visibility and export metadata
7. custom diagnostic annotations
8. lookahead predicates

These improve scale, maintainability, and diagnostics.

### Longer-term bets

9. parameterized grammar macros/templates
10. lexical modes or stateful tokenization

Lexical modes are not spelled out above, but they would become interesting once skip/token support exists and CPF starts targeting more languages with string interpolation, nested comments, or preprocessor-style regions.

## Example of a more ergonomic future grammar

```text
skip ws -> r'[ \t\r\n]+';
skip comment -> r'//[^\n]*';

token identifier -> r'[A-Za-z_][A-Za-z0-9_]*';
token integer -> r'[0-9]+';

public translation_unit -> function*:functions;

precedence expression {
   left '+' '-';
   left '*' '/';
}

function -> value_type:return_type identifier:name '(':open sep_by(param, ','):params ')':close block:body;
param -> value_type:type identifier:name;

block -> ('{':open statement*:statements '}':close):body;
statement -> variable_decl | assignment | return_stmt | if_stmt | while_stmt | expr_stmt | block;

if_stmt -> 'if':keyword '(':open expression:condition ')':close statement:then_branch
           ('else':else_keyword statement:else_branch):else_part?;

expression -> expression:left '+':op expression:right
            | expression:left '*':op expression:right
            | identifier:name
            | integer:value;
```

This keeps CPF's current flavor, but removes much of the repetition and several of the awkward helper constructs that larger grammars otherwise need.


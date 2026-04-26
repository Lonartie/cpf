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

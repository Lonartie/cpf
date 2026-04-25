# CPF documentation

This directory contains the structured project documentation for CPF.

## Contents

- [`grammar-reference.md`](./grammar-reference.md)
    - Grammar syntax
    - Rule attributes
    - Quantifiers and grouped alternatives
    - Multi-file imports
- [`generation-and-integration.md`](./generation-and-integration.md)
    - Library entry points
    - `cpfgen` command-line usage
    - Generated-code namespaces
    - CMake integration through `cpf_link_grammars(...)`
- [`runtime-api.md`](./runtime-api.md)
    - Generated parser API shape
    - `cpf::parse_options`
    - Lazy parse forests via `cpf::parse_tree<T>`
    - Error handling and complexity metadata
- [`tooling.md`](./tooling.md)
    - Build commands
    - Test execution
    - Benchmark workflows and benchmark families

## Suggested reading order

1. Start with [`grammar-reference.md`](./grammar-reference.md) if you are authoring `.cpf` grammars.
2. Continue with [`generation-and-integration.md`](./generation-and-integration.md) to generate and link parsers.
3. Use [`runtime-api.md`](./runtime-api.md) when consuming generated parsers from C++.
4. Use [`tooling.md`](./tooling.md) for build, test, and benchmark workflows.

For third-party dependency attribution, see the repository-level [
`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).


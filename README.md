# CPF

CPF (Ciphers parser framework) is a parser generation framework that creates the lexer, parser and the model classes
by providing a single grammar file. It is designed to quickly prototype parsers, compilers, transpilers and
interpreters. The `cpfgen` command line tool takes a grammar file and generates a header and a source file with
the same base name as the grammar file. The generated parser is an earley parser that returns several parse trees.

## Example grammar file

```
// Calculator grammar file

// Direct dependencies like this directly translate to class inheritance
expression -> addition | subtraction | multiplication | division | number;

// Just add all the rules, optionally with labels which will generated member variables in the model classes
addition        [prec = 'sub']                      -> expression:left '+':op expression:right;
subtraction     [prec < 'div', lbl = 'sub']         -> expression:left '-':op expression:right;
multiplication  [prec = 'div']                      -> expression:left '*':op expression:right;
division        [prec < 'num', lbl = 'div']         -> expression:left '/':op expression:right;

// Regex is supported by preceding the literal with r, for example:
number          [lbl = 'num']                       -> r'[0-9]+';

// Attributes can be added to rules by adding them in square brackets as seen in the example above.
// Multiple attributes are comma separated. The following attributes are supported:
// Name | Values      | Example              | Description                              | Default
// ---- | ----------- | -------------------- | ---------------------------------------- | -----------
// prec | = <num>     | [prec = 0]           | Absolute precedence.                     | Line number 
// prec | > <str>     | [prec < 'add']       | Relatively lower precedence than label.  | Line number 
// prec | = <str>     | [prec = 'add']       | Same precedence than label.              | Line number 
// dir  | left, right | [dir = left]         | Associativity.                           | Left
// lbl  | <str>       | [lbl = 'number']     | Label for the rule.                      | None
```

The above grammar file will generate a model like this (header):

```c++
struct expression : node {
    ~expression() override = default;
    static parse_result parse(std::string_view input);
    const std::type_info& type() const override;
    std::unique_ptr<expression> clone();
};
std::ostream& operator<<(std::ostream& os, const expression& node);
auto visit(const expression& node, const auto& visitor);
void visit_recursive(const expression& node, const auto& visitor);

struct addition : expression {
    std::unique_ptr<expression> left;
    std::string op;
    std::unique_ptr<expression> right;

    ~addition() override = default;
    static parse_result parse(std::string_view input);
    const std::type_info& type() const override;
    std::unique_ptr<addition> clone();
};
std::ostream& operator<<(std::ostream& os, const addition& node);
auto visit(const addition& node, const auto& visitor);
void visit_recursive(const addition& node, const auto& visitor);

struct subtraction : expression {
    std::unique_ptr<expression> left;
    std::string op;
    std::unique_ptr<expression> right;

    ~subtraction() override = default;
    static parse_result parse(std::string_view input);
    const std::type_info& type() const override;
    std::unique_ptr<subtraction> clone();
};
std::ostream& operator<<(std::ostream& os, const subtraction& node);
auto visit(const subtraction& node, const auto& visitor);
void visit_recursive(const subtraction& node, const auto& visitor);

struct multiplication : expression {
    std::unique_ptr<expression> left;
    std::string op;
    std::unique_ptr<expression> right;

    ~multiplication() override = default;
    static parse_result parse(std::string_view input);
    const std::type_info& type() const override;
    std::unique_ptr<multiplication> clone();
};
std::ostream& operator<<(std::ostream& os, const multiplication& node);
auto visit(const multiplication& node, const auto& visitor);
void visit_recursive(const multiplication& node, const auto& visitor);

struct division : expression {
    std::unique_ptr<expression> left;
    std::string op;
    std::unique_ptr<expression> right;

    ~division() override = default;
    static parse_result parse(std::string_view input);
    const std::type_info& type() const override;
    std::unique_ptr<division> clone();
};
std::ostream& operator<<(std::ostream& os, const division& node);
auto visit(const division& node, const auto& visitor);
void visit_recursive(const division& node, const auto& visitor);

struct number : expression {
    ~number() override = default;
    static parse_result parse(std::string_view input);
    const std::type_info& type() const override;
    std::unique_ptr<number> clone();
};
std::ostream& operator<<(std::ostream& os, const number& node);
auto visit(const number& node, const auto& visitor);
void visit_recursive(const number& node, const auto& visitor);
```

where node and parse_result are defined in the cpf library. The parse result contains
the forest result and a success flag. The forest result is a vector of unique pointers to node where each
item is of the same type as the the class that is being parsed.

## Error handling

The generated parser will return a parse result with the success flag set to false if the input cannot be parsed. 
Along with it it also contains a member `error` with members `line`, `column`, `expected`, `found` and `message`.

## Usage - Continuing on the calculator example

```c++
#include "calculator.h"

struct visitor {
    auto operator()(const addition& node) { return visit(node.left, *this) + visit(node.right, *this); }
    auto operator()(const subtraction& node) { return visit(node.left, *this) - visit(node.right, *this); }
    auto operator()(const multiplication& node) { return visit(node.left, *this) * visit(node.right, *this); }
    auto operator()(const division& node) { return visit(node.left, *this) / visit(node.right, *this); }
    auto operator()(const number& node) { return std::stoi(node.value); }
};

int main() {
    while (true) {
        std::cout << "intput > ";
        std::string input;
        std::getline(std::cin, input);
        auto result = expression::parse(input);
        if (result.success) {
            assert(result.forest.size() == 1); // In this example we expect only one parse tree
            auto& tree = result.forest[0];
            std::cout << "Parsed successfully: " << *tree << std::endl;
            std::cout << "Result: " << visit(*tree, visitor{}) << std::endl;
        } else {
            std::cout << "Parse error: " << result.error.message << std::endl;
        }
    }
}
```
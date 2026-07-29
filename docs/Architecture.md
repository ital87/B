# B Compiler Architecture

## Overview

The B compiler is a multi-stage pipeline that transforms source code into executable machine code via LLVM.

```
Entry Source File (.b)
      ↓
  Module Loader (imports → one token stream)
      ↓
   Lexer (Tokenization)
      ↓
 Namespace Resolver (namespaces → flat names)
      ↓
   Parser (AST Generation + monomorphization)
      ↓
  Semantic Analysis (scopes, unused code, ownership, borrows)
      ↓
   Code Generation (LLVM IR)
      ↓
   LLVM Optimization & Codegen
      ↓
 Machine Code / Executable
```

> **Note on source layout:** the compiler that ships is the single translation
> unit `src/b_combined.cpp`. The `src/lexer/`, `src/parser/`, `src/ast/`, and
> `src/codegen/` paths referenced below describe the logical structure of that
> file's namespaces (`b::lexer`, `b::parser`, `b::ast`, `b::codegen`).

## Phase 0: Module Loader

### Purpose
Turns a multi-file project into the single token stream the parser consumes.

Starting from the entry file, each `import "path";` is resolved relative to the
importing file (falling back to the working directory, with an optional `.b`
extension). Modules are loaded depth-first, so an imported module's tokens are
emitted before those of the file that imports it. A canonical-path set guarantees
each module is loaded exactly once, which makes diamond dependencies free and
import cycles harmless. Every token is stamped with its originating file so later
errors can name it, and each module's tokens stay contiguous in the stream.

Alongside the canonical-path set the loader keeps the current import *stack*.
Re-entering a file that is still on the stack is what a cycle is, so the loader
records the chain from that file back to itself (`a.b -> b.b -> a.b`) before
returning. Cycles remain legal; `--debug` prints every chain it found.

Because the parser and code generator both resolve declarations in name-keyed
passes rather than in source order, the resulting order of modules never
constrains what a program may reference.

## Phase 1: Lexer

### Purpose
Converts raw source text into a stream of tokens (the lexical structure).

### Implementation (`src/lexer/`)

- **Token.h/cpp**: Defines `TokenType` enum and `Token` struct
  - Token structure: `type`, `lexeme`, `value`, `line`, `column`
  - Used for precise error reporting with location information

- **Lexer.h/cpp**: Main tokenization engine
  - Single-pass lexer using character-by-character scanning
  - Handles keywords, identifiers, literals, operators, and delimiters
  - Supports line (`//`) and block (`/* */`) comments
  - Escape sequence handling for strings

### Token Categories

1. **Literals**: INTEGER, FLOAT, STRING
2. **Keywords**: All language keywords (int, return, if, etc.)
3. **Operators**: Arithmetic, comparison, logical, bitwise
4. **Delimiters**: Brackets, braces, punctuation
5. **Special**: EOF, UNKNOWN

### Error Handling

Lexer throws exceptions for:
- Unterminated strings
- Invalid characters
- Incomplete tokens

Each exception includes line and column information for user-friendly error messages.

## Phase 1.5: Namespace Resolver

### Purpose
Flattens `namespace` blocks away before the parser ever runs, so that neither the
parser nor the code generator has to know namespaces exist.

### Implementation (`b::modules::NamespaceResolver`)

The resolver rewrites the token stream in place of a symbol table: a declaration
inside `namespace geometry` is renamed to `geometry__Vec2`, and every reference
to it — qualified or not — is renamed to match. `::` collapses into `__`, which
is why the resolver also rejects a program in which a handwritten `geometry__Vec2`
and a real `geometry::Vec2` would end up as the same name.

A stream that contains no `namespace`, `using`, or `::` token is returned
untouched, so programs that do not use namespaces take exactly the path they took
before.

**Pass 1 — collect.** Walks the stream tracking brace depth and a stack of open
namespace blocks. At the declaration level of each namespace it recognises
structs, enums (including their constants, which are namespace members in their
own right), function-pointer typedefs, functions, and globals, and records the
token index of every declared name together with its flattened spelling. A
declaration's name is the last identifier before `(`, `=`, `;` or `[` that is not
inside generic arguments, which is what makes `own Vec2 make(` and
`Box<T> makeBox<T>(` both land on the right token. The same pass *freezes* the
tokens that must never be renamed: struct field names, generic type parameters,
and names declared at global scope.

**Pass 2 — rewrite.** Walks the stream again, this time emitting tokens. The
`namespace` headers and their closing braces are dropped as scaffolding, and each
identifier is resolved: a recorded declaration site takes its flattened name, a
frozen token is left alone, an identifier after `.` or `->` is a struct field and
is left alone, an identifier followed by `::` starts a qualified path, and
anything else is looked up through the enclosing namespaces, then the global
scope, then the active `using namespace` declarations. Because modules arrive as
contiguous runs of tokens, a `using` is discarded as soon as its block closes or
its file ends — importing a module never imports its `using` declarations.

Names that resolve nowhere are passed through unchanged; they are locals,
parameters, or builtins, and the parser and code generator diagnose them as
before.

## Phase 1.75: Semantic Analysis

### Purpose
Resolves names against real scopes and enforces the rules the code generator has
no good place to check. Everything it finds goes through the diagnostic engine,
so one run reports every problem instead of stopping at the first.

### Implementation (`b::sema::Analyzer`)

A scope stack of locals, plus program-wide tables of globals, functions, structs
and their field types. Each local records where it was declared, whether it has
been read, whether it has been moved, and what borrows are outstanding against it.

**Unused code.** Leaving a scope is where "declared but never read" is decided. A
name starting with `_` opts out, and `pub` exempts a function or global from the
rule so a library module can expose an API nobody in that build calls.

**Moves.** An `own` value is consumed by an initializer, an assignment, an `own`
parameter, an `own` return, and an owned field initializer in `new`. Tracking is
flow-sensitive: `if`/`else` are analysed from a common snapshot and merged, a
`switch` merges across all its cases, and a move inside a loop body is reported
because the next iteration would move the same value again.

**Borrows.** `&x` and `&mut x` check the target for conflicts at the point of the
borrow. A borrow stored in a variable stays live until that variable's scope ends;
a borrow used only as an argument lasts just for the call. Lifetimes are lexical:
a borrow may not be stored in a scope outer to its target, and a borrow of a local
may not be returned.

The analyzer also stamps move sites onto the AST, which is how the code generator
knows where to clear a drop flag without re-deriving the analysis.

## Phase 2: Parser

### Purpose
Builds an Abstract Syntax Tree (AST) from the token stream.

### Implementation (`src/parser/`)

**Recursive Descent Parser**:
- Top-down parsing with backtracking
- Statement parsing: variable declarations, control flow, function calls
- Expression parsing with operator precedence

**Operator Precedence Levels**:
1. Primary: literals, identifiers, parenthesized expressions
2. Unary: negation, logical NOT, bitwise NOT
3. Multiplicative: `*`, `/`, `%`
4. Additive: `+`, `-`
5. Shift: `<<`, `>>`
6. Relational: `<`, `<=`, `>`, `>=`
7. Equality: `==`, `!=`
8. Bitwise AND: `&`
9. Bitwise XOR: `^`
10. Bitwise OR: `|`
11. Logical AND: `&&`
12. Logical OR: `||`
13. Assignment: `=` (right-associative)

**Supported Constructs**:
- Function declarations with parameters and return types
- Variable declarations with optional initialization, including fixed-size arrays
- Expression statements
- Block statements (compound statements)
- If/else conditionals
- For and while loops
- Switch statements with fall-through
- Return statements
- Function calls with argument lists
- `sizeof(type)`

**Prescan Pass**

Before the main descent, the parser sweeps the token stream once to register
every `enum` (its name, constants, and their values) and every function-pointer
`typedef`, and to lift generic declarations out of the stream into templates.
This is what makes enum and typedef names usable before the line that declares
them, in any module.

**Generics by Monomorphization**

A generic declaration such as `T maxOf<T>(T a, T b)` or `struct Box<T>` is stored
as a template: the declaration's tokens with the `<...>` parameter list removed,
plus the index of its name token. It is not type-checked or emitted on its own.

Each use site — `maxOf<int>(...)` in an expression, `Box<int>` in a type
position — mangles the type arguments into a concrete name (`maxOf__int`,
`Box__int`) and enqueues an instantiation request; identical requests collapse to
one. After the main parse, the queue is drained: for each request the template's
tokens are copied with each type parameter replaced by the argument's tokens and
the name token replaced by the mangled name, and the result is parsed as an
ordinary declaration. Since instantiating a template can request further
instantiations (a generic that uses another generic, or recurses), the loop runs
until the queue is empty.

The parser splits a `>>` token into two `>` when it closes nested type arguments,
so `Box<Box<int>>` needs no whitespace.

From code generation onward, nothing knows generics existed — only ordinary
functions and structs with mangled names, which is why a generic call costs
exactly what the hand-written equivalent costs.

### AST Node Hierarchy (`src/ast/`)

**Expression Nodes**:
- `Literal` - integer, float, string, boolean values
- `Identifier` - variable/function names
- `BinaryOp` - two-operand operations
- `UnaryOp` - one-operand operations
- `FunctionCall` - function invocations with arguments

**Statement Nodes**:
- `VariableDecl` - variable declarations with type and initializer
- `ReturnStmt` - return statements with optional value
- `ExpressionStmt` - standalone expressions
- `Block` - compound statements
- `IfStmt` - conditional statements
- `ForStmt` - for loop statements
- `WhileStmt` - while loop statements

**Declaration Nodes**:
- `FunctionDecl` - function definitions
- `Program` - top-level program container

**Type System**:
- Primitive types: int, float, double, bool, char, void
- Pointer types: any primitive with pointer levels
- Type annotations on variables and function parameters
- Char type: 8-bit integer for character/byte data

### Visitor Pattern

All AST nodes implement the visitor pattern via `ASTVisitor` base class:
- Enables extensible tree traversal
- Foundation for semantic analysis and code generation
- Supports different traversal strategies without modifying AST classes

## Phase 3: Code Generation

### Purpose
Transforms AST into LLVM Intermediate Representation (IR) and machine code.

### Implementation (`src/codegen/`)

**Core Features**:
- Variable allocation with `alloca` instructions
- Expression evaluation with type conversion
- Function declaration and definition
- Control flow with basic blocks and branches
- Scope management with stack-based scoping
- Dual type tracking (B types and LLVM types) for opaque pointer compatibility

**Type System Mapping**:
- B primitive types → LLVM types (int→i32, float→f32, double→f64, bool→i1, char→i8, void→void)
- Pointer types with proper element type tracking
- Struct types with field layout
- Function types for C ABI compliance
- Automatic type conversions in function calls and return statements
- Null pointer literal (0) handled for pointer comparisons

**Built-in Functions**:
- `printf`, `scanf`, `sprintf` - formatted I/O (variadic)
- `strlen`, `strcmp`, `strcpy`, `atoi` - string helpers

Allocation is not among them. `b_alloc` and `b_free` come from B's own runtime,
which the code generator links into every module.

### Slices

`own [T]` is a single pointer at run time; the element count lives in the second
word of the allocator header, which is otherwise only used as a free-list link
while the block is free. `b_alloc_array` writes it, `b_len` reads it back, and
`b_bounds_check` compares an index against it before every `getelementptr`. An
out-of-range access calls `b_panic`, which writes to file descriptor 2 and exits
through `exit_group` — no libc involved.

A slice whose elements are structs runs their drop glue one by one before the
block is released; a slice of owned handles releases each non-null element.

`[T; N]` carries its length in the type instead. `len` folds to a constant, a
constant index is verified during code generation and emits no check at all, and
the remaining checks compare against an immediate rather than a loaded value. The
check itself is inline IR — an `icmp` and a branch into a cold block that calls
`b_panic` — rather than a call, so LLVM's range analysis can remove it when it
can prove the index is in bounds. Length and element type are compared at
assignments, initializers and call sites; `[T; N]` converts to `[T]`, never the
other way.

### Allocator Runtime

B does not use C's allocator. The code generator parses a small LLVM IR module and
links it in, so every binary carries its own heap:

- `b_os_alloc` / `b_os_release` issue `mmap` and `munmap` through inline `syscall`
- `b_alloc` rounds a request up to a power of two of at least 32 bytes, takes the
  matching size-class free list if it is non-empty, and otherwise carves the block
  out of a 1 MiB chunk with a bump pointer; anything larger than a chunk gets its
  own mapping
- `b_free` pushes the block back onto its class list, reading the class out of the
  16-byte header that `b_alloc` wrote

A compiled B program has no undefined reference to `malloc` or `free`.

`print`, `println`, and `itoa` are not external symbols: the code generator
synthesizes them, deriving a `printf` format string from each argument's LLVM
type.

**Declaration Passes**

`visit(Program*)` runs in name-keyed passes rather than source order, which is
what frees a program from declaration order across an entire multi-module build:

1. Enum tables are recorded for switch checking.
2. Every struct is created as an *opaque* named `StructType`.
3. Struct bodies are filled in, so a field may reference any struct regardless of
   which was declared first.
4. Function-pointer typedefs are resolved.
5. Globals are emitted.
6. Every function *prototype* is created.
7. Function bodies are emitted.

Splitting 6 from 7 is what lets any function call any other function, defined
later in the file or in a module loaded afterwards. Duplicate structs, globals,
and functions are detected in these passes and reported by name.

**Semantic Checks**

There is no separate semantic pass; checks run during generation, where scope and
type information already live:

- `inferType` computes the static B type of an expression (literals, variables,
  fields, indexing, deref/address-of, casts, call results) and drives both
  lvalue addressing and the enum rules.
- Enum types are enforced on initialization, assignment, arguments, returns, and
  comparisons; an enum never implicitly becomes an `int` or another enum.
- `switch` over an enum rejects foreign case labels and warns when a constant is
  unhandled and no `default` exists.
- Call arity is checked against the declared parameter list.
- Assignment to a `const` variable is rejected.

**Addressing Model**

`addressOf` computes the address of any lvalue expression recursively —
identifiers (local or global), struct fields, array elements, and dereferences —
returning both the pointer and the B type of the pointee. Assignment, `&`, `*`,
field access, and indexing all route through it, so arbitrary chains such as
`a->b[i].c` work uniformly on either side of an assignment.

### Example Code Generation

B code:
```b
int add(int a, int b) {
    return a + b;
}

int main() {
    int x = 5;
    return add(x, 3);
}
```

Generated LLVM IR:
```llvm
define i32 @add(i32 %a, i32 %b) {
entry:
  %add = add i32 %a, %b
  ret i32 %add
}

define i32 @main() {
entry:
  %x = alloca i32
  store i32 5, i32* %x
  %x_val = load i32, i32* %x
  %call = call i32 @add(i32 %x_val, i32 3)
  ret i32 %call
}
```

### Type Conversion System

The code generator handles automatic type conversions in several contexts:

**Function Calls**: Arguments are cast to parameter types
- Integer-to-integer casts (e.g., i32 to i64 for fseek offset)
- Null pointer literals (integer 0 → null pointer of target type)

**Return Statements**: Return values are cast to function return type
- Integer-to-integer casts (e.g., i8 to i32)
- Float-to-float casts (e.g., float to double)

**Comparisons**: Pointer-to-integer comparisons are handled
- Integer 0 is converted to null pointer when comparing with pointer types
- Enables idioms like `if (file == 0)` for error checking

### Optimization

Before the module is written out, the code generator runs LLVM's standard O2
pipeline over it. That is what promotes B's allocas to registers, inlines
`b_alloc` into its callers, and folds the bounds checks that the sized-slice
types made provable.

## Phase 4: Structs & Pointers

### Purpose
Adds support for user-defined types and pointer operations.

### Implementation

**Struct Support** (`src/ast/` and `src/codegen/`):
- `StructDecl` AST node for struct definitions
- `StructField` for field definitions
- `StructType` registration in code generator
- Field name tracking for member access

**Pointer Operations**:
- `UnaryOp` with `DEREF` and `ADDRESS_OF` operators
- Safe dereference handling with element type tracking
- Pointer-to-pointer relationships preserved

**Member Access**:
- `MemberAccess` expression node with dot operator
- LLVM GEP (GetElementPtr) instruction generation
- Support for struct and pointer-to-struct access

### Example

```b
struct Point {
    int x;
    int y;
};

int main() {
    Point p;
    int* ptr = &p;
    return 42;
}
```

## Phase 5: File I/O & Dynamic Memory

### Purpose
Enables heap allocation and file system access for self-hosting capability.

### Implementation

**Array Indexing** (`src/ast/` and `src/parser/`):
- `ArrayAccess` expression node
- LBRACKET token parsing in postfix expressions
- GEP-based index calculation

**Memory Management**:
- `new T { ... }` lowers to a `b_alloc` call, a zero fill, and one store per
  named field
- Owned locals carry a drop flag; scope exits, `return`, `break` and `continue`
  emit a conditional release
- `drop.glue.T` chains a struct's own `drop` with the release of its owned fields

### Example

```b
struct Buffer { int first; int second; };

int main() {
    own Buffer b = new Buffer { first: 42 };
    b.second = b.first + 8;
    return b.second;
}
```

## Phase 4 & 5: Code Generation (Legacy Notes)

### Purpose
Transforms AST into LLVM Intermediate Representation (IR).

### Implementation (`src/codegen/`)

Uses LLVM C++ API to:
- Create LLVM modules and functions
- Generate IR for expressions and statements
- Handle type conversion and ABI compliance
- Emit debug information

### Example IR Target

For B code:
```b
int add(int a, int b) {
    return a + b;
}
```

Generates LLVM IR similar to:
```llvm
define i32 @add(i32 %a, i32 %b) {
  %1 = add i32 %a, %b
  ret i32 %1
}
```

## Design Decisions

### No Header Files
B eschews C++'s header/implementation split:
- Type information is embedded in `.b` files
- Parser reads full context in one pass
- Simplifies bootstrapping

### Ownership Instead of a Collector
Memory is managed at compile time:
- `own T` is a pointer at runtime, so all existing member-access and call
  machinery works on it unchanged
- Each owned local gets a one-bit drop flag in the entry block; it is cleared on
  a move and tested at scope exit, which is what makes a conditional move correct
  without any static case analysis in the backend
- The compiler emits a `drop.glue.T` per struct that calls the user's `drop` and
  then releases owned fields recursively
- Optionals are nullable pointers with no extra tag, and `if some` compiles to a
  null test — the type system is what makes the test unavoidable

### Namespaces Resolved Before Parsing
Namespaces are a naming concern, not a typing one, so they are settled in a
token-level pass and never reach the AST:
- The parser, the monomorphizer, and the code generator keep working on one flat
  namespace, unchanged
- Declaration order stays irrelevant, including across namespace blocks that are
  reopened in different files
- Generics inside a namespace need no special handling — a template and its
  instantiations are simply renamed like everything else

### C-Style Syntax
- Familiar to C/C++ programmers
- Minimizes learning curve
- Clear relationship to underlying LLVM IR

### Direct LLVM Backend
- Eliminates intermediate IR translations
- Leverages LLVM's mature optimization passes
- Enables easy C interoperability via C ABI

### Single-Pass Lexer
- Efficient for reasonable file sizes
- Simple implementation
- Easy to extend with new tokens

## Compiler Invocation

```bash
b input.b              # Compile to a native executable
b input.b --debug      # Same, plus the module list, any import cycles,
                       # the resolved namespaces, and an AST dump
b --version
b --update             # Re-download and rebuild the compiler in place
```

The output executable is named after the source file's stem and is written next
to the current working directory.

## Memory Model

### Current (Phase 1-2)
Simple, C-style manual memory management.

### Future Directions
1. **RAII** (C++ style): Automatic cleanup of resources
2. **Ownership** (Rust-inspired): Prevent use-after-free at compile time
3. **Reference Counting**: Automatic but with overhead
4. **Hybrid**: Zone allocation for scoped lifetimes

## Testing Strategy

1. **Unit Tests**: Lexer tokenization, parser grammar
2. **Integration Tests**: Full compilation pipeline
3. **Regression Tests**: Real-world B programs
4. **Performance Tests**: Compiler speed and output quality

## Future Optimizations

1. Parallel lexing/parsing for large files
2. Incremental compilation
3. Caching of compilation artifacts
4. JIT compilation for development
5. Profile-guided optimizations

## References

- [LLVM Language Reference Manual](https://llvm.org/docs/LangRef/)
- [LLVM C++ API](https://llvm.org/doxygen/index.html)
- [Compilers: Principles, Techniques, and Tools](https://en.wikipedia.org/wiki/Compilers:_Principles,_Techniques,_and_Tools)


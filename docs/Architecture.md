# Arc Compiler Architecture

## Overview

The Arc compiler is a multi-stage pipeline that transforms source code into executable machine code via LLVM.

```
Source Code (.arc)
      ↓
   Lexer (Tokenization)
      ↓
   Parser (AST Generation)
      ↓
  Semantic Analysis
      ↓
   Code Generation (LLVM IR)
      ↓
   LLVM Optimization & Codegen
      ↓
 Machine Code / Executable
```

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
5. Relational: `<`, `<=`, `>`, `>=`
6. Equality: `==`, `!=`
7. Bitwise AND: `&`
8. Bitwise XOR: `^`
9. Bitwise OR: `|`
10. Logical AND: `&&`
11. Logical OR: `||`
12. Assignment: `=` (right-associative)

**Supported Constructs**:
- Function declarations with parameters and return types
- Variable declarations with optional initialization
- Expression statements
- Block statements (compound statements)
- If/else conditionals
- For and while loops
- Return statements
- Function calls with argument lists

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
- Dual type tracking (Arc types and LLVM types) for opaque pointer compatibility

**Type System Mapping**:
- Arc primitive types → LLVM types (int→i32, float→f32, double→f64, bool→i1, char→i8, void→void)
- Pointer types with proper element type tracking
- Struct types with field layout
- Function types for C ABI compliance
- Automatic type conversions in function calls and return statements
- Null pointer literal (0) handled for pointer comparisons

**Built-in Functions**:
- `printf` - formatted output (variadic)
- `fopen`, `fclose` - file operations
- `fread`, `fseek`, `ftell` - file I/O
- `malloc`, `free` - memory allocation

### Example Code Generation

Arc code:
```arc
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

```arc
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
- `malloc` function for dynamic allocation
- `free` function for deallocation
- Proper pointer type tracking through allocations

**File I/O Declarations**:
- Function signatures for file operations
- C ABI compatibility for POSIX file functions

### Example

```arc
int main() {
    int* buffer = malloc(100);
    buffer[0] = 42;
    buffer[1] = buffer[0] + 8;
    free(buffer);
    return buffer[0];
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

For Arc code:
```arc
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
Arc eschews C++'s header/implementation split:
- Type information is embedded in `.arc` files
- Parser reads full context in one pass
- Simplifies bootstrapping

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
arc input.arc          # Tokenize and report tokens (Phase 1)
arc --parse input.arc  # Parse to AST (Phases 1-2)
arc --check input.arc  # Full semantic check (Phases 1-3)
arc input.arc -o out   # Compile to executable (Phases 1-4)
```

(Details to be finalized during Phase 2)

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
3. **Regression Tests**: Real-world Arc programs
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


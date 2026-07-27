# B — Systems Programming Language

B is a small, practical systems programming language compiled to native machine code via LLVM. It reads like C, compiles in milliseconds, and produces fast native binaries on Linux and Windows.

```b
int main() {
    println("Hello, B!");
    return 0;
}
```

Write one file, run one command, get one binary. No headers. No build system. No ceremony.

---

## Quick Start

### Install

**Linux / WSL:**
```bash
curl -fsSL https://raw.githubusercontent.com/ital87/B/main/get.sh | bash
```

**Windows 11 (PowerShell):**
```powershell
irm https://raw.githubusercontent.com/ital87/B/main/get.ps1 | iex
```

### Compile & Run

```bash
b hello.b
./hello
```

For a project with several files, compile the file that contains `main` — every
module it imports is pulled in automatically:

```bash
b examples/project/main.b
./main
```

---

## Project Status

### Implemented
- **Lexer:** Full tokenizer with keywords, operators, string/char/int/float literals (with escape sequences), line/block comments
- **Parser:** Recursive-descent with operator precedence
- **Types:** `int`, `float`, `double`, `bool`, `char`, `string`, `void`, `enum`, function pointers (via `typedef`), pointers (`T*`), arrays (via pointer indexing)
- **Functions:** Declarations, parameters, return values, recursion, function pointers as first-class values
- **Generics:** Generic functions and structs (`T maxOf<T>(T a, T b)`, `struct Box<T>`), compiled by monomorphization — one specialized native function per type, zero runtime cost
- **Control Flow:** `if`/`else`, `while`, `for`, `switch`/`case`/`default`, `break`, `continue` — any value is truthy-tested against zero
- **Memory:** `malloc`, `free`, `sizeof(T)`, pointers (`&`, `*`, `arr[i]`), full pointer arithmetic, fixed-size local arrays (`int buf[64];`)
- **Structs:** Declaration, nesting by value, field access (`p.x` and `p->x` both work), arbitrary chains (`a->b->c.d`)
- **Global Variables:** Top-level `int x = 5;` with optional `const` keyword
- **Const:** Local and global const variables with compile-time assignment blocking
- **I/O:** `print()`, `println()` (auto-format), `printf()`, `scanf()`, file I/O (`fopen`, `fclose`, `fread`, `fwrite`, `fprintf`, `fgets`, `fseek`, `ftell`)
- **Strings:** `strlen`, `strcmp`, `strcpy`, `atoi`, `itoa`
- **Type Casting:** C-style `(type)expr` between numeric types, `char`, enums, and pointers
- **Float/Double:** Full arithmetic and comparison with mixed int/float expressions
- **Character Literals:** `'A'`, `'\n'`, `'\t'`, `'\0'`, etc. with escape sequences
- **Enums:** Distinct types — an `enum` is not an `int`, mixing them is a compile error, and a `switch` over an enum warns about unhandled cases
- **Switch Statements:** Full support for multiple cases, default, and fall-through
- **Module System:** `import "path/to/file.b";` resolves imports relative to the importing file, loads each module exactly once, and tolerates import cycles
- **Declaration Order:** Irrelevant — types, functions, and globals may be used before they are declared, in any file
- **Native Compilation:** Linux (ELF) and Windows 11 (PE `.exe`)
- **Self-Update:** `b --update` re-downloads and rebuilds in place

### Planned
- Dependent types (memory currently manual, C-style)
- Type inference for generic call sites (today type arguments are explicit)
- Self-hosting (B compiler written in B)

---

## Language Guide

### Level 1: Basics

#### Hello World

```b
int main() {
    println("Hello, World!");
    return 0;
}
```

#### Variables and Types

```b
int main() {
    int x = 42;
    float f = 3.14;
    bool flag = true;
    char c = 'A';
    string s = "Hello";
    
    println(x);      // 42
    printf("%f\n", f);    // 3.140000
    
    return 0;
}
```

**Available types:**
- `int` — 32-bit signed integer
- `float` — 32-bit floating point
- `double` — 64-bit floating point
- `bool` — true/false
- `char` — single character (8-bit ASCII)
- `string` — text (alias for `char*`, pointer to characters)
- `void` — no value (used for functions that return nothing)

#### Functions

```b
int add(int a, int b) {
    return a + b;
}

void greet(string name) {
    printf("Hello, %s!\n", name);
}

int main() {
    int sum = add(5, 3);
    greet("Alice");
    return 0;
}
```

#### Control Flow: if/else

```b
int main() {
    int age = 25;
    
    if (age >= 18) {
        println("Adult");
    } else if (age >= 13) {
        println("Teenager");
    } else {
        println("Child");
    }
    
    return 0;
}
```

Any value can be used as a condition — it's tested against zero:

```b
int main() {
    int x = 5;
    if (x) {
        println("x is truthy (non-zero)");
    }
    
    if (!x) {
        println("x is falsy (zero)");
    }
    
    return 0;
}
```

#### Loops: while

```b
int main() {
    int i = 0;
    while (i < 10) {
        println(i);
        i = i + 1;
    }
    return 0;
}
```

#### Loops: for

```b
int main() {
    for (int i = 0; i < 10; i = i + 1) {
        println(i);
    }
    return 0;
}
```

`break` exits the loop; `continue` skips to the next iteration:

```b
int main() {
    for (int i = 0; i < 10; i = i + 1) {
        if (i == 5) {
            break;     // exit loop entirely
        }
        if (i == 2) {
            continue;  // skip to i = 3
        }
        println(i);
    }
    return 0;
}
```

#### Switch Statements

```b
int main() {
    int day = 3;
    
    switch (day) {
        case 1:
            println("Monday");
            break;
        case 2:
            println("Tuesday");
            break;
        case 3:
            println("Wednesday");
            break;
        default:
            println("Other day");
    }
    
    return 0;
}
```

`break` jumps to the end of the switch. Without it, execution continues into the next case (fall-through):

```b
int main() {
    int x = 2;
    
    switch (x) {
        case 1:
        case 2:
            println("One or two");
            break;
        case 3:
            println("Three");
            break;
        default:
            println("Something else");
    }
    
    return 0;
}
```

#### Printing Output

```b
int main() {
    print("No newline");
    println("With newline");
    
    printf("Formatted: %d, %f, %s\n", 42, 3.14, "text");
    
    return 0;
}
```

- `print(x)` — auto-detect type and print (no newline)
- `println(x)` — print with newline
- `printf(format, ...)` — C-style printf

#### Reading Input

```b
int main() {
    println("Enter your age:");
    int age = 0;
    scanf("%d", &age);
    
    printf("You are %d years old\n", age);
    
    return 0;
}
```

### Level 2: Advanced Basics

#### Global Variables

```b
int counter = 0;
const float PI = 3.14159;

int main() {
    counter = 10;
    printf("Counter: %d, PI: %f\n", counter, PI);
    return 0;
}
```

Global variables are declared at the top level. Use `const` to make them immutable:

```b
const int MAX_SIZE = 100;

int main() {
    // MAX_SIZE = 50;  // Compile error!
    println(MAX_SIZE);
    return 0;
}
```

#### Local Const Variables

```b
int main() {
    const int x = 10;
    // x = 20;  // Compile error!
    
    int y = 5;
    y = 15;  // OK
    
    return 0;
}
```

#### Floating-Point Arithmetic

```b
int main() {
    float a = 3.5;
    float b = 2.0;
    
    float sum = a + b;      // 5.5
    float diff = a - b;     // 1.5
    float prod = a * b;     // 7.0
    float quot = a / b;     // 1.75
    
    printf("Sum: %f, Quotient: %f\n", sum, quot);
    
    double d = 3.14159;
    printf("Double: %f\n", d);
    
    return 0;
}
```

Mixed int/float arithmetic works seamlessly:

```b
int main() {
    int x = 5;
    float f = 2.5;
    float result = x + f;  // 7.5 (x converted to float)
    printf("%f\n", result);
    return 0;
}
```

#### Type Casting

```b
int main() {
    int x = 10;
    float f = (float)x;
    
    float g = 3.7;
    int y = (int)g;  // truncates to 3
    
    int ascii_val = 65;
    char c = (char)ascii_val;  // 'A'
    
    printf("%f, %d, %c\n", f, y, c);
    
    return 0;
}
```

#### Pointers and Addresses

```b
int main() {
    int x = 42;
    int* p = &x;      // p points to x
    
    int y = *p;       // y = 42 (dereference p)
    
    *p = 100;         // change x through p
    printf("%d\n", x);  // 100
    
    return 0;
}
```

#### Dynamic Memory

```b
int main() {
    int* arr = malloc(5 * sizeof(int));  // allocate 5 ints
    
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    
    printf("arr[1] = %d\n", arr[1]);
    
    free(arr);  // deallocate
    
    return 0;
}
```

`sizeof(T)` takes a type — a primitive, a struct, a pointer, or a generic
instantiation — and yields its size in bytes as an `int`:

```b
struct Point {
    int x;
    int y;
};

int main() {
    printf("%d %d %d\n", sizeof(int), sizeof(Point), sizeof(Point*));
    return 0;
}
```

#### Fixed-Size Arrays

Local arrays live on the stack and behave like a pointer to their first element —
no `malloc`, no `free`:

```b
int main() {
    int numbers[5];
    for (int i = 0; i < 5; i = i + 1) {
        numbers[i] = i * i;
    }
    printf("%d\n", numbers[4]);  // 16
    
    char buffer[256];
    scanf("%255s", buffer);
    printf("You typed: %s\n", buffer);
    
    return 0;
}
```

The size must be a positive integer literal, and a fixed-size array cannot have an
initializer. For global buffers, use `malloc`.

#### Character Literals and Escape Sequences

```b
int main() {
    char a = 'A';
    char newline = '\n';
    char tab = '\t';
    char null = '\0';
    char backslash = '\\';
    
    printf("Char: %c, ASCII: %d\n", a, a);  // A, 65
    
    return 0;
}
```

### Level 3: Structured Data

#### Structs

```b
struct Point {
    int x;
    int y;
};

struct Rectangle {
    Point topLeft;
    Point bottomRight;
};

int main() {
    Point p;
    p.x = 10;
    p.y = 20;
    
    printf("Point: (%d, %d)\n", p.x, p.y);
    
    return 0;
}
```

#### Struct Pointers

```b
struct Point {
    int x;
    int y;
};

int main() {
    Point p;
    Point* ptr = &p;
    
    ptr->x = 5;      // arrow operator
    ptr->y = 10;
    
    ptr.x = 5;       // dot operator also works
    ptr.y = 10;
    
    printf("Point: (%d, %d)\n", ptr->x, ptr->y);
    
    return 0;
}
```

#### Enums

```b
enum Color {
    RED = 0,
    GREEN = 1,
    BLUE = 2
};

int main() {
    Color c = RED;
    
    if (c == RED) {
        println("It's red!");
    }
    
    printf("Color value: %d\n", c);
    
    return 0;
}
```

Enums auto-increment if you omit values:

```b
enum Status {
    INACTIVE,     // 0
    ACTIVE,       // 1
    PENDING,      // 2
    ERROR         // 3
};
```

#### Enums Are Distinct Types

An `enum` is its own type, not an alias for `int`. The compiler rejects anything
that mixes an enum with a plain number or with a different enum:

```b
enum Color { RED, GREEN, BLUE };
enum Fruit { APPLE, PEAR };

int main() {
    Color c = RED;      // fine
    
    // int n = RED;     // error: cannot use enum 'Color' for a variable of type 'int'
    // Color d = APPLE; // error: cannot use enum 'Fruit' for a variable of enum type 'Color'
    // c = 5;           // error: cannot assign a non-enum value to enum type 'Color'
    // if (c == 0) {}   // error: cannot compare enum 'Color' with a non-enum value
    // Color e = c + 1; // error: enum 'Color' supports only == and !=
    
    return 0;
}
```

The same rules apply to function arguments, return values, and struct fields.
Enums support `==` and `!=`; for anything else, cast explicitly:

```b
enum Color { RED, GREEN, BLUE };

int main() {
    Color c = BLUE;
    
    int raw = (int)c;         // enum  -> int
    Color parsed = (Color)1;  // int   -> enum
    
    printf("%d %d\n", raw, (int)parsed);
    return 0;
}
```

A `switch` over an enum only accepts case labels of that enum, and warns at
compile time when a case is missing and there is no `default`:

```b
enum Color { RED, GREEN, BLUE };

void describe(Color c) {
    switch (c) {          // Warning: does not handle BLUE and has no default case
        case RED:
            println("red");
            break;
        case GREEN:
            println("green");
            break;
    }
}
```

### Level 4: Advanced Features

#### String Operations

```b
int main() {
    string s = "Hello";
    
    int len = strlen(s);
    printf("Length: %d\n", len);
    
    string a = "test";
    string b = "test";
    if (strcmp(a, b) == 0) {
        println("Strings are equal");
    }
    
    string copy = malloc(100);
    strcpy(copy, s);
    printf("Copy: %s\n", copy);
    
    int n = atoi("42");
    printf("Parsed: %d\n", n);
    
    string str = itoa(n);
    printf("Back to string: %s\n", str);
    
    free(copy);
    free(str);
    
    return 0;
}
```

#### Function Pointers

Declare a function pointer type with `typedef`, then use it like any other type:

```b
typedef int (*Operation)(int, int);

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

int apply(int x, int y, Operation op) {
    return op(x, y);
}

int main() {
    Operation op1 = add;
    Operation op2 = multiply;
    
    printf("add(5, 3) = %d\n", apply(5, 3, op1));
    printf("mult(5, 3) = %d\n", apply(5, 3, op2));
    
    return 0;
}
```

#### File I/O

```b
int main() {
    // Write to file
    FILE* f = fopen("output.txt", "w");
    if (f != 0) {
        fprintf(f, "Hello, File!\n");
        fprintf(f, "answer = %d\n", 42);
        fclose(f);
    }
    
    // Read from file
    f = fopen("output.txt", "r");
    if (f != 0) {
        char buffer[256];
        fgets(buffer, 256, f);
        printf("Read: %s", buffer);
        fclose(f);
    }
    
    return 0;
}
```

#### Recursion

```b
int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

int main() {
    printf("5! = %d\n", factorial(5));  // 120
    return 0;
}
```

#### Complex Example: Linked List

```b
struct Node {
    int value;
    Node* next;
};

Node* createNode(int value) {
    Node* n = malloc(sizeof(Node));
    n->value = value;
    n->next = 0;
    return n;
}

void printList(Node* head) {
    Node* current = head;
    while (current != 0) {
        printf("%d -> ", current->value);
        current = current->next;
    }
    println("null");
}

int main() {
    Node* head = createNode(1);
    head->next = createNode(2);
    head->next->next = createNode(3);
    
    printList(head);
    
    return 0;
}
```

### Level 5: Generics

#### Generic Functions

Write the type parameters in angle brackets after the function name, then use
them anywhere a type is expected:

```b
T maxOf<T>(T a, T b) {
    if (a > b) {
        return a;
    }
    return b;
}

int main() {
    printf("%d\n", maxOf<int>(3, 9));        // 9
    printf("%f\n", maxOf<float>(1.5, 0.5));  // 1.500000
    return 0;
}
```

Type arguments are written explicitly at the call site: `maxOf<int>(3, 9)`.

Each combination of type arguments is compiled into its own specialized native
function (monomorphization), so a generic call costs exactly as much as the
hand-written version — no boxing, no indirection, no runtime type information.

#### Generic Structs

```b
struct Box<T> {
    T value;
};

struct Pair<A, B> {
    A first;
    B second;
};

int main() {
    Box<int> number;
    number.value = 42;
    
    Pair<int, float> pair;
    pair.first = 4;
    pair.second = 0.25;
    
    printf("%d %d %f\n", number.value, pair.first, pair.second);
    return 0;
}
```

Generic structs and functions compose, including on the heap — `sizeof` knows the
size of any instantiation:

```b
struct Stack<T> {
    T* items;
    int count;
};

Stack<T>* stackNew<T>(int capacity) {
    Stack<T>* s = malloc(sizeof(Stack<T>));
    s->items = malloc(capacity * sizeof(T));
    s->count = 0;
    return s;
}

void stackPush<T>(Stack<T>* s, T value) {
    s->items[s->count] = value;
    s->count = s->count + 1;
}

T stackTop<T>(Stack<T>* s) {
    return s->items[s->count - 1];
}

int main() {
    Stack<int>* numbers = stackNew<int>(16);
    stackPush<int>(numbers, 11);
    stackPush<int>(numbers, 22);
    printf("top: %d of %d\n", stackTop<int>(numbers), numbers->count);
    
    free(numbers->items);
    free(numbers);
    return 0;
}
```

A generic type argument can itself be a generic instantiation, a pointer, a
struct, or an enum — for example `Pair<Box<int>*, int>`.

### Level 6: Modules

#### Splitting a Project Across Files

`import` pulls another `.b` file into the compilation:

```b
import "geometry/vec.b";
import "report.b";

int main() {
    Vec2* v = vecNew(2, 5);
    report(HIGH, v);
    free(v);
    return 0;
}
```

Compile the file that contains `main`; every imported module is found, compiled,
and linked in one step:

```bash
b main.b
./main
```

**How imports resolve**

- Paths are relative to the **importing file**, so `import "geometry/vec.b";`
  inside `main.b` refers to `geometry/vec.b` next to `main.b`. If that fails, the
  path is retried relative to the current working directory.
- The `.b` extension is optional: `import "geometry/vec";` works too.
- Each module is compiled **exactly once**, no matter how many files import it,
  so diamond-shaped dependency graphs need no include guards.
- **Import cycles are allowed.** If `a.b` imports `b.b` and `b.b` imports `a.b`,
  each is still loaded once and functions may call across the cycle freely.
- `import` must appear at the top level of a file, not inside a function.

**What modules share**

Everything a module declares — functions, structs, enums, typedefs, globals, and
generics — is visible to every other module in the program. There are no headers
and no forward declarations, and **declaration order does not matter**: a
function may call another function that is defined later or in a different file,
and a struct may be used before the file that declares it is reached.

```
project/
├── main.b                 // import "report.b";  import "geometry/vec.b";
├── report.b               // import "geometry/vec.b";
└── geometry/
    ├── vec.b              // import "scalar.b";
    └── scalar.b
```

Because names are global to the program, defining the same function, struct, or
global twice is a compile error that names the duplicate.

---

## Operators and Precedence

| Operator | Type | Associativity |
|----------|------|----------------|
| `()` `[]` `.` `->` | Postfix | Left |
| `!` `~` `*` `&` `-` (unary) `sizeof` `(type)` | Unary | Right |
| `*` `/` `%` | Multiplicative | Left |
| `+` `-` | Additive | Left |
| `<<` `>>` | Bitwise Shift | Left |
| `<` `<=` `>` `>=` | Comparison | Left |
| `==` `!=` | Equality | Left |
| `&` | Bitwise AND | Left |
| `^` | Bitwise XOR | Left |
| `\|` | Bitwise OR | Left |
| `&&` | Logical AND | Left |
| `\|\|` | Logical OR | Left |
| `=` | Assignment | Right |

---

## Installation Details

### Requirements
- LLVM 14+ (LLVM 22 supported)
- C++17 compiler (GCC/Clang on Linux, Clang + MSVC linker on Windows)
- git

The installers handle everything automatically. On Linux, never requires `sudo` for personal installation.

### Manual Build

```bash
git clone https://github.com/ital87/B.git
cd B
./install.sh           # Linux/macOS
# or
powershell -ExecutionPolicy Bypass -File install.ps1  # Windows
```

---

## Project Structure

```
B/
├── src/
│   └── b_combined.cpp     # Entire compiler in one file
├── examples/              # Sample .b programs
│   ├── hello.b
│   ├── generics.b
│   ├── enums.b
│   ├── linked_list.b
│   └── project/           # Multi-file project using imports
├── docs/Architecture.md   # Compiler pipeline walkthrough
├── install.sh / get.sh    # Linux installer and bootstrap script
├── install.ps1 / get.ps1  # Windows installer and bootstrap script
├── CMakeLists.txt         # Windows build configuration
└── README.md              # This file
```

The entire compiler lives in one file, compiled directly with `g++`/`clang++` and LLVM — no intermediate build system.

---

## Tips and Best Practices

1. **Use `const` for immutable values** — helps catch bugs:
   ```b
   const int BUFFER_SIZE = 256;
   ```

2. **Check pointer validity** — pointers can be null:
   ```b
   FILE* f = fopen("file.txt", "r");
   if (f != 0) {
       // use file
       fclose(f);
   }
   ```

3. **Free allocated memory** — avoid leaks:
   ```b
   string s = malloc(100);
   // use s
   free(s);
   ```

4. **Use structs to organize data**:
   ```b
   struct Config {
       int timeout;
       string filename;
       bool verbose;
   };
   ```

5. **Separate concerns with functions** — keep `main()` clean.

---

## Common Patterns

### Safe String Handling

```b
int main() {
    string name = malloc(100);
    printf("Enter your name: ");
    scanf("%99s", name);  // Limit input to prevent overflow
    
    printf("Hello, %s!\n", name);
    free(name);
    
    return 0;
}
```

### Array Operations

```b
int main() {
    int* arr = malloc(10 * sizeof(int));  // 10 integers
    
    for (int i = 0; i < 10; i = i + 1) {
        arr[i] = i * i;
    }
    
    for (int i = 0; i < 10; i = i + 1) {
        printf("%d ", arr[i]);
    }
    println("");
    
    free(arr);
    return 0;
}
```

### State Machine with Switch

```b
enum State {
    IDLE,
    RUNNING,
    STOPPED
};

void handleState(State s) {
    switch (s) {
        case IDLE:
            println("Waiting for input...");
            break;
        case RUNNING:
            println("Process is running");
            break;
        case STOPPED:
            println("Process stopped");
            break;
        default:
            println("Unknown state");
    }
}

int main() {
    handleState(RUNNING);
    return 0;
}
```

---

## Troubleshooting

**Q: "Compilation failed" with no clear error**
- Check for missing semicolons and mismatched braces
- Verify all variable declarations before use

**Q: "Cannot find module '...' imported from ..."**
- Import paths are relative to the file that contains the `import`, not to the
  directory you run `b` from
- Check the spelling; the `.b` extension is optional but the path is not

**Q: "Duplicate definition of function ..."**
- Two modules in the same program define the same name. Names are global across
  all imported files — rename one, or move the shared definition into a module
  both files import

**Q: "Cannot use enum 'X' for ... of type 'int'"**
- Enums are distinct types. Convert explicitly with `(int)value` or `(X)number`

**Q: "Generic function 'f' needs type arguments"**
- Type arguments are explicit: write `f<int>(x)`, not `f(x)`

**Q: Memory leak errors**
- Use `free()` for every `malloc()`
- Check that pointers are not null before dereferencing

**Q: Unexpected output from arithmetic**
- Remember that integer division truncates: `5 / 2 = 2`
- Use `float` or `double` for decimal arithmetic

**Q: Segmentation fault**
- Likely a null pointer dereference or buffer overflow
- Check array bounds and pointer validity

---

## Vision

B's long-term goal is a from-scratch, low-level GUI rendering engine using HTML/CSS-like layout principles (Flexbox, Grid, Cascading), compiled straight to CPU/GPU instructions — with B itself as the systems language it's written in.

---

## Contributing

B is actively developed. Fork the repository, create a feature branch, and submit pull requests on GitHub.

---

## Resources

- [LLVM C++ API Documentation](https://llvm.org/doxygen/)
- [C Language Reference](https://en.cppreference.com/w/c/language)

---

**Happy coding!** 🚀

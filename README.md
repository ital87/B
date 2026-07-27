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

---

## Project Status

### Implemented
- **Lexer:** Full tokenizer with keywords, operators, string/char/int/float literals (with escape sequences), line/block comments
- **Parser:** Recursive-descent with operator precedence
- **Types:** `int`, `float`, `double`, `bool`, `char`, `string`, `void`, `enum`, function pointers (via `typedef`), pointers (`T*`), arrays (via pointer indexing)
- **Functions:** Declarations, parameters, return values, recursion, function pointers as first-class values
- **Control Flow:** `if`/`else`, `while`, `for`, `switch`/`case`/`default`, `break`, `continue` — any value is truthy-tested against zero
- **Memory:** `malloc`, `free`, pointers (`&`, `*`, `arr[i]`), full pointer arithmetic
- **Structs:** Declaration, field access (`p.x` and `p->x` both work)
- **Global Variables:** Top-level `int x = 5;` with optional `const` keyword
- **Const:** Local and global const variables with compile-time assignment blocking
- **I/O:** `print()`, `println()` (auto-format), `printf()`, `scanf()`, file I/O (`fopen`, `fclose`, `fread`, etc.)
- **Strings:** `strlen`, `strcmp`, `strcpy`, `atoi`, `itoa`
- **Type Casting:** C-style `(type)expr` between numeric types, `char`, and pointers
- **Float/Double:** Full arithmetic and comparison with mixed int/float expressions
- **Character Literals:** `'A'`, `'\n'`, `'\t'`, `'\0'`, etc. with escape sequences
- **Enums:** Compile-time named constants with auto-increment
- **Switch Statements:** Full support for multiple cases, default, and fall-through
- **Import System:** `import "path/to/file.b";` with cycle detection (prepared for modular code)
- **Native Compilation:** Linux (ELF) and Windows 11 (PE `.exe`)
- **Self-Update:** `b --update` re-downloads and rebuilds in place

### Known Limitations
- No generics yet
- Enums are resolved to plain `int` at compile time (no type safety)
- Import system prepared but not fully functional yet

### Planned
- Full module/import system with dependency resolution
- Generics
- Distinct `enum` type with exhaustiveness checking
- Dependent types (memory currently manual, C-style)
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
    int* arr = malloc(5 * 4);  // allocate 5 ints (4 bytes each)
    
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    
    printf("arr[1] = %d\n", arr[1]);
    
    free(arr);  // deallocate
    
    return 0;
}
```

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
        printf(f, "Hello, File!\n");
        fclose(f);
    }
    
    // Read from file
    f = fopen("output.txt", "r");
    if (f != 0) {
        char buffer[256];
        fread(buffer, 1, 256, f);
        printf("Read: %s\n", buffer);
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
    Node* n = malloc(8 + 8);  // int + pointer
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

---

## Operators and Precedence

| Operator | Type | Associativity |
|----------|------|----------------|
| `()` `[]` `.` `->` | Postfix | Left |
| `!` `~` `*` `&` `-` (unary) | Unary | Right |
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
Arc/
├── src/
│   └── b_combined.cpp     # Entire compiler in one file
├── examples/              # Sample .b programs
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
    int* arr = malloc(10 * 4);  // 10 integers
    
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

# B — Systems Programming Language

B is a small systems programming language compiled to native machine code via LLVM. It reads like C, borrows Rust's memory model, and produces fast native binaries. **Linux on x86-64 only** — see [Platform Support](#platform-support).

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

Windows is not supported at the moment; see [Platform Support](#platform-support).

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
- **Types:** `int`, `float`, `double`, `bool`, `char`, `string`, `void`, `enum`, function pointers (via `typedef`), owned heap values (`own T`), borrows (`&T`, `&mut T`), optionals (`own T?`), fixed-size local arrays
- **Functions:** Declarations, parameters, return values, recursion, function pointers as first-class values
- **Generics:** Generic functions and structs (`T maxOf<T>(T a, T b)`, `struct Box<T>`), compiled by monomorphization — one specialized native function per type, zero runtime cost
- **Control Flow:** `if`/`else`, `while`, `for`, `switch`/`case`/`default`, `break`, `continue` — any value is truthy-tested against zero
- **Memory Safety:** Ownership with move semantics, `drop` functions run automatically (RAII), shared and mutable borrows with aliasing rules, lifetimes, and no null pointers
- **Allocator:** B's own heap on top of `mmap`, not C's `malloc` — size-class free lists, no libc dependency for allocation
- **Structs:** Declaration, nesting by value, field access (`p.x` and `p->x` both work), arbitrary chains (`a->b->c.d`)
- **Global Variables:** Top-level `int x = 5;` with optional `const` keyword
- **Const:** Local and global const variables with compile-time assignment blocking
- **Slices:** `own [T]`, `&[T]`, `&mut [T]`, `new [T](n)`, `len(s)`, indexing with bounds checks
- **Sized Slices:** `[T; N]` carries the length in the type — `len` becomes a constant, in-range constant indices need no check, and a wrong length is a compile error
- **Optimizer:** the LLVM O2 pipeline runs before object emission
- **Standard Library:** `std/math.b`, `std/string.b`, `std/list.b`, `std/map.b`, `std/io.b` — written in B, no libc
- **I/O:** `print()`, `println()` (auto-format), `printf()`, `scanf()`, plus `std::io` on raw syscalls
- **Strings:** `strlen`, `strcmp`, `strcpy`, `atoi`, `itoa`
- **Type Casting:** C-style `(type)expr` between numeric types, `char`, and enums
- **Float/Double:** Full arithmetic and comparison with mixed int/float expressions
- **Character Literals:** `'A'`, `'\n'`, `'\t'`, `'\0'`, etc. with escape sequences
- **Enums:** Distinct types — an `enum` is not an `int`, mixing them is a compile error, and a `switch` over an enum warns about unhandled cases
- **Switch Statements:** Multiple cases, `default`, and real fall-through; labels must be distinct compile-time constants
- **Short-Circuit Logic:** `&&` and `||` skip their right operand once the result is decided
- **Module System:** `import "path/to/file.b";` resolves imports relative to the importing file, loads each module exactly once, and tolerates import cycles (reported with their full chain under `--debug`)
- **Namespaces:** `namespace geometry { ... }`, nestable and reopenable across files, `namespace a::b { }` shorthand, qualified access (`geometry::Vec2`), `using namespace`, and `::name` for the global scope
- **Declaration Order:** Irrelevant — types, functions, and globals may be used before they are declared, in any file
- **Native Compilation:** Linux x86-64 (ELF)
- **Self-Update:** `b --update` re-downloads and rebuilds in place

### Planned
- Functions generic over a length (`sum<int N>(&[int; N])`); today a generic
  function takes the unsized `&[T]`
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

    println(x);
    printf("%f\n", f);
    printf("%d %c %s\n", (int)flag, c, s);

    return 0;
}
```

**Available types:**
- `int` — 32-bit signed integer
- `float` — 32-bit floating point
- `double` — 64-bit floating point
- `bool` — true/false
- `char` — single character (8-bit ASCII)
- `string` — text
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
    printf("%d\n", add(5, 3));
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
    y = 15;    // OK

    printf("%d %d\n", x, y);
    return 0;
}
```

#### Floating-Point Arithmetic

```b
int main() {
    float a = 3.5;
    float b = 2.0;
    
    printf("sum  %f\n", a + b);   // 5.5
    printf("diff %f\n", a - b);   // 1.5
    printf("prod %f\n", a * b);   // 7.0
    printf("quot %f\n", a / b);   // 1.75

    double d = 3.14159;
    printf("double %f\n", d);

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

#### Heap Values

B has no raw pointers and no `malloc`. A value on the heap is created with `new`
and belongs to exactly one owner, written `own T`:

```b
struct Point {
    int x;
    int y;
};

int main() {
    own Point p = new Point { x: 3, y: 4 };
    printf("%d %d\n", p.x, p.y);
    return 0;
}
```

`new T { ... }` zero-fills the value and then assigns the fields you name, so
anything you leave out starts at zero. The memory is released automatically when
the owner goes out of scope — see [Memory Model](#memory-model).

`sizeof(T)` takes a type — a primitive, a struct, or a generic instantiation —
and yields its size in bytes as an `int`:

```b
struct Point {
    int x;
    int y;
};

int main() {
    printf("%d %d\n", sizeof(int), sizeof(Point));
    return 0;
}
```

#### Fixed-Size Arrays

Local arrays live on the stack and are indexed directly:

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
initializer. An array cannot be passed to a function or stored in a global — it
does not outlive its block.

#### Character Literals and Escape Sequences

```b
int main() {
    char a = 'A';
    char tab = '\t';
    char backslash = '\\';

    printf("Char: %c, ASCII: %d\n", a, a);   // A, 65
    printf("[%c]%c[%c]\n", tab, a, backslash);

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

void shift(&mut Point p) {
    p.x = p.x + 1;
    p.y = p.y + 1;
}

int main() {
    own Point p = new Point { x: 5, y: 10 };
    shift(&mut p);
    printf("Point: (%d, %d)\n", p.x, p.y);
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
    printf("%d\n", (int)c);

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
    
    char copy[100];
    strcpy(copy, s);
    printf("Copy: %s\n", copy);
    
    int n = atoi("42");
    printf("Parsed: %d\n", n);
    
    string str = itoa(n);
    printf("Back to string: %s\n", str);
    
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

File handling lives in `std/io.b` and is built on owned handles, so a reader
closes its file when it goes out of scope. See
[Standard Library](#standard-library).

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
    own Node? next;
};

void printList(&Node head) {
    printf("%d -> ", head.value);
    if some (rest = head.next) {
        printList(rest);
    } else {
        println("end");
    }
}

int main() {
    own Node third = new Node { value: 3, next: none };
    own Node second = new Node { value: 2, next: third };
    own Node head = new Node { value: 1, next: second };

    printList(&head);

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

Generic structs and functions compose, including on the heap:

```b
struct Box<T> {
    T value;
};

own Box<T> boxNew<T>(T value) {
    return new Box<T> { value: value };
}

T unbox<T>(&Box<T> box) {
    return box.value;
}

int main() {
    own Box<int> number = boxNew<int>(42);
    own Box<string> text = boxNew<string>("boxed");
    printf("%d %s\n", unbox<int>(&number), unbox<string>(&text));
    return 0;
}
```

A generic type argument can itself be a generic instantiation, an owned value, a
struct, or an enum — for example `Pair<Box<int>, int>`.

### Level 6: Modules

#### Splitting a Project Across Files

`import` pulls another `.b` file into the compilation:

```b
import "geometry/vec.b";
import "report.b";

int main() {
    own Vec2 v = vecNew(2, 5);
    report(HIGH, &v);
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
  each is still loaded once and functions may call across the cycle freely. The
  compiler detects every cycle and prints the chain under `--debug`:
  `Import cycle: a.b -> b.b -> a.b`.
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
global twice is a compile error that names the duplicate. To carve that global
space up, use a namespace.

#### Namespaces

A `namespace` block groups declarations under a shared name. Members are reached
from the outside with `::`:

```b
// geometry/vec.b
namespace geometry {
    const int SCALE = 3;

    struct Vec2 {
        int x;
        int y;
    };

    own Vec2 make(int x, int y) {
        return new Vec2 { x: x, y: y };
    }
}
```

```b
// main.b
import "geometry/vec.b";

int main() {
    own geometry::Vec2 v = geometry::make(2, 5);
    printf("%d %d\n", v.x, geometry::SCALE);
    return 0;
}
```

Everything a namespace can contain is a member of it: functions, structs, enums
(**and their constants**, so `report::LOW`), typedefs, globals, generic
functions, and generic structs.

**Nesting and reopening**

Namespaces nest, and `namespace a::b { }` is shorthand for two nested blocks:

```b
namespace app::math {
    int twice(int v) { return v * 2; }
}
```

The same namespace may be **reopened** in any number of blocks and any number of
files. Everything declared across all of them is one namespace:

```b
// tools.b
import "app.b";

namespace app::math {
    int quad(int v) { return twice(twice(v)); }   // sees twice from the other file
}
```

**How a name is looked up**

An unqualified name is resolved in this order, stopping at the first match:

1. the enclosing namespaces, innermost first — inside `geometry`, plain `SCALE`
   is `geometry::SCALE`, and a nested `detail::clamp(...)` is enough;
2. the global scope — so a global declaration always keeps its own meaning;
3. namespaces made visible with `using namespace`.

A qualified name like `app::math::quad` is looked up the same way: `app` is
resolved against the enclosing namespaces, then the global scope, then any
`using`. A leading `::` skips straight to the global scope, which is how you
reach a global that a namespace member of the same name would otherwise hide:

```b
namespace codes { int add(int a, int b) { return a + b; } }
int add(int a, int b) { return a + b + 1000; }

int main() {
    printf("%d\n", codes::add(3, 4));   // 7
    printf("%d\n", ::add(3, 4));        // 1007
    return 0;
}
```

**`using namespace`**

`using namespace X;` makes the members of `X` visible without qualification. It
applies from that point to the end of the enclosing block, and **never leaves the
file it is written in** — importing a module does not import its `using`
declarations:

```b
import "geometry/vec.b";

using namespace geometry;

int main() {
    own Vec2 v = make(2, 5);   // geometry::make
    return 0;
}
```

If two `using`-visible namespaces declare the same name, using it unqualified is
an error that names both namespaces; qualify it to say which one you meant.

**What namespaces do not change**

Namespaces are resolved before the program is parsed: `geometry::Vec2` becomes
the ordinary flat name `geometry__Vec2`. That means declaration order still does
not matter, struct fields are untouched (`v->x` is a field, never a namespace
member), and generics work inside namespaces exactly as outside. It also means a
handwritten name like `geometry__Vec2` would collide with the namespace member —
the compiler reports that instead of miscompiling it.

---

## Memory Model

B has no garbage collector, no `malloc`, and no null pointers. Memory is managed
by ownership, checked entirely at compile time, with no runtime cost beyond a
one-bit flag where the compiler cannot decide statically.

### Ownership

A heap value is created with `new` and has exactly one owner:

```b
struct Vec2 { int x; int y; };

own Vec2 a = new Vec2 { x: 1, y: 2 };
```

Handing that value to something else **moves** it. The old name is no longer
usable:

```b
own Vec2 b = a;
printf("%d\n", a.x);   // error: 'a' is used after it was moved
```

A move happens on initialization, assignment, passing to an `own` parameter, and
returning. Assigning a fresh value to a moved-out name makes it usable again.

The compiler follows control flow, so it also catches the cases where a move only
happens on some paths:

```b
if (flag) { take(a); }
use(a);                 // error: 'a' is used after it was moved

for (int i = 0; i < 3; i = i + 1) {
    take(a);            // error: moved inside a loop, the next round would move it again
}
```

### Drop and RAII

When an owner goes out of scope, its value is released. Give a struct a `drop`
function to run cleanup first:

```b
struct Conn { int id; };

drop Conn(&mut Conn self) {
    printf("closing %d\n", self.id);
}

int main() {
    own Conn c = new Conn { id: 1 };
    return 0;
}                       // prints "closing 1"
```

Drops run at the closing brace, on `return`, on `break` and on `continue`, in
reverse declaration order. A value that was moved away is **not** dropped by the
old owner — the new one is responsible. Owned fields are released with their
owner, so a whole tree comes down in one go.

Exactly one `drop` per struct, and it takes `&mut T`.

### Borrowing

Passing ownership is often more than you need. A borrow lends the value out:

```b
int  area(&Vec2 v)          { return v.x * v.y; }
void grow(&mut Vec2 v)      { v.x = v.x + 1; }

own Vec2 a = new Vec2 { x: 3, y: 4 };
printf("%d\n", area(&a));
grow(&mut a);
```

- `&T` is a **shared** borrow: read-only, any number at a time.
- `&mut T` is a **unique** borrow: read/write, and while it exists nothing else
  may touch the value — not another borrow, not the owner itself.

A borrow lives until the end of the block that declared it. Put it in its own
block to release it early:

```b
own Vec2 a = new Vec2 { x: 1, y: 2 };
{
    &Vec2 r = &a;
    printf("%d\n", area(r));
}
own Vec2 b = a;         // fine: the borrow ended at the brace
```

A borrow can never outlive what it points at. Returning one to a local is
rejected, and so is storing one in a longer-lived name.

### No Null

There is no null. When a value may be absent, mark the type with `?` and unwrap
it before use:

```b
struct Node {
    int value;
    own Node? next;
};

int length(&Node n) {
    if some (rest = n.next) {
        return 1 + length(rest);
    }
    return 1;
}

own Node tail = new Node { value: 2, next: none };
own Node head = new Node { value: 1, next: tail };
```

`if some (name = value)` binds `name` to the contents when there is one, and
takes the `else` branch when there is not. `if some mut (...)` binds it mutably.

Reaching through an optional without unwrapping is rejected:

```b
own V? maybe = none;
printf("%d\n", maybe.x);
// error: cannot reach 'x' through 'own V?' without unwrapping it first
// help: write 'if some (value = maybe) { ... }' and use 'value' inside
```

That is the whole of it — there is no other route into an optional, so a null
dereference cannot be written.

### The Allocator

`new` allocates from B's own heap, not from C. The runtime asks the kernel for
memory with `mmap` and manages it with size-class free lists: blocks are rounded
up to a power of two, carved out of 1 MiB chunks, and returned to a per-class
free list on release. Requests above 1 MiB get their own mapping.

A compiled B program has no undefined reference to `malloc` or `free` at all.

---

## Standard Library

The library lives in `std/` and is written in B. Import what you need; paths are
relative to the importing file.

```b
import "std/list.b";
import "std/string.b";
```

### Slices

Before the library, one piece of the language: a slice is a run of values with a
length attached.

```b
own [int] numbers = new [int](6);   // owned, zero-filled
numbers[2] = 7;
printf("%d of %d\n", numbers[2], len(numbers));
```

`&[T]` lends a slice out for reading, `&mut [T]` for writing. Every index is
checked; an out-of-range access aborts with a message rather than reading
whatever is next in memory. A slice may hold owned handles too
(`own [own Str]`), and dropping it drops every element.

When the length is known at compile time, put it in the type and most of that
checking disappears — see [Sized Slices](#sized-slices).

### `std/math.b`

`sqrt`, `sin`, `cos`, `tan`, `exp`, `ln`, `pow`, `powInt`, `hypot`, `floor`,
`ceil`, `round`, `fmod`, `abs`, `min`, `max`, `clampInt`, plus `PI`, `E`, `TAU`.

```b
printf("%.10f\n", math::sqrt(2.0));     // 1.4142135624
printf("%.10f\n", math::sin(math::PI / 6.0));  // 0.5000000000
```

Implemented in B — Newton's method for roots, range reduction with Taylor series
for the trigonometric functions. No libm.

### `std/string.b`

`text::Str` is an owned, growable string.

```b
own text::Str greeting = text::fromLiteral("  Hello, World!  ");
own text::Str trimmed = text::trim(&greeting);
printf("%s\n", text::cstr(&trimmed));
```

`fromLiteral`, `fromInt`, `toInt`, `copy`, `length`, `charAt`, `cstr`, `equals`,
`substring`, `concat`, `indexOf`, `indexOfChar`, `contains`, `startsWith`,
`endsWith`, `toUpper`, `toLower`, `trim`, `replace`, `countChar`, `split`.

`split` hands back `own [own text::Str]` — a slice that owns its pieces:

```b
own text::Str csv = text::fromLiteral("a,bb,ccc");
own [own text::Str] parts = text::split(&csv, ',');
for (int i = 0; i < len(parts); i = i + 1) {
    printf("%s\n", text::cstr(parts[i]));
}
```

### `std/list.b`

`list::List<T>` is a growable array, generic over any element type.

```b
own list::List<int> xs = list::make<int>();
list::push<int>(&mut xs, 42);
printf("%d\n", list::get<int>(&xs, 0));
```

`make`, `withCapacity`, `size`, `capacity`, `isEmpty`, `reserve`, `push`, `get`,
`set`, `pop`, `clear`, `contains`, `indexOf`, `removeAt`, `reverse`, `sort`.

### `std/map.b`

`map::Map<V>` is a hash map with `text::Str` keys, open addressing with linear
probing, growing at three-quarters full.

```b
own map::Map<int> ages = map::make<int>();
own text::Str alice = text::fromLiteral("alice");
map::put<int>(&mut ages, &alice, 30);
printf("%d\n", map::getOr<int>(&ages, &alice, 0 - 1));
```

`make`, `size`, `capacity`, `put`, `has`, `getOr`.

### `std/io.b`

Files and standard streams, on `read`/`write`/`open`/`close` syscalls.

```b
import "std/io.b";

int main() {
    own text::Str path = text::fromLiteral("input.txt");
    if some mut (reader = io::openFile(&path)) {
        while (true) {
            if some (line = io::readLine(reader)) {
                printf("%s\n", text::cstr(line));
            } else {
                return 0;
            }
        }
    }
    return 1;
}
```

`write`, `writeLine`, `writeError`, `stdin`, `fromHandle`, `openFile`,
`readLine`, `readAll`, `readFile`, `writeFile`. A `Reader` buffers 8 KiB at a
time and closes its handle in `drop`, so a reader that goes out of scope releases
the file on its own.

`examples/wordcount.b` puts strings, the map, the list and I/O together.
`examples/calc.b` is a larger one: an expression interpreter with a lexer, a
recursive-descent parser and a variable table, in about 250 lines.

---

## Sized Slices

`[T; N]` is a slice whose length is part of its type. `N` must be known at
compile time: an integer literal or a `const` global.

```b
const int SIZE = 8;

own [int; SIZE] fixed = new [int](SIZE);
```

### What the length buys

**`len` is a constant.** No runtime lookup at all:

```b
for (int i = 0; i < len(fixed); i = i + 1) {   // len(fixed) is literally 8
    fixed[i] = i;
}
```

**Constant indices are checked at compile time,** and then not at run time:

```b
own [int; 4] small = new [int](4);
small[2] = 1;    // fine, no check emitted
small[9] = 1;    // error: Index 9 is outside 'own [int; 4]'
```

**A wrong length does not compile:**

```b
int take(&[int; 4] values) { return values[0]; }

own [int; 8] wrong = new [int](8);
take(&wrong);    // error: expects '&[int; 4]' but got '&[int; 8]'
```

The element type is checked the same way, so `&[Vec2]` never reaches a `&[int]`
parameter.

### What it costs to give up

A sized slice converts to an unsized one, losing the static length and getting
runtime checks back:

```b
int sumAny(&[int] values) {            // works for any length
    int total = 0;
    for (int i = 0; i < len(values); i = i + 1) {
        total = total + values[i];
    }
    return total;
}

own [int; 5] sized = new [int](5);
sumAny(&sized);                        // fine
```

Write `&[T; N]` when the length is fixed and the code is hot; write `&[T]` when
the function should work for any length.

### The difference in the output

Two functions, same body, one sized and one not:

```b
pub int sumFixed(&[int; 64] values) { ... }
pub int sumAny(&[int] values)       { ... }
```

Compiled and disassembled, `sumFixed` is **7 instructions** and `sumAny` is
**24**. The fixed length lets LLVM see the trip count, drop the bounds check, and
vectorize the loop; the unsized version has to read the length and check each
index. Both return the same answer.

### Limits

The length must be a constant, not a runtime value, and a function cannot yet be
generic over it — there is no `sum<int N>(&[int; N])`. For code that must work at
several lengths, take `&[T]`.

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

`&&` and `\|\|` **short-circuit**: the right operand is not evaluated when the
left one already decides the result, so `if (p != 0 && p->x == 1)` is safe on a
null pointer.

---

## Language Rules

These are the rules the compiler enforces. They are checked at compile time, so a
program that violates one of them does not build.

**Values and types**

- `int` is 32-bit. An integer literal outside `-2147483648 .. 2147483647` is an
  error rather than a silent wrap.
- A floating literal is a `double`, exactly as in C. Assigning it to a `float`
  narrows it at the assignment: `double d = 3.141592653589793;` keeps all its
  digits.
- `bool` converts to a number as `0` or `1`, never as `-1`.
- A `char` printed with `print()` shows the character; use `(int)c` for its code.
- Enums are distinct types: an enum never mixes with `int` without an explicit cast.

**Names and scopes**

- Every declared name must be used. A local that is never read, a parameter that
  is ignored, a function or global nobody calls — all are errors. Prefix a name
  with `_` to say the omission is deliberate, or mark a function or global `pub`
  to make it part of a module's public surface. A value whose type has a `drop`
  is exempt: holding it *is* its purpose.
- A name declared in a block disappears at the closing brace.
- A local **shadows** a global of the same name, for both reading and writing.
- `const` is per declaration; a `const` local in one function does not constrain
  a same-named variable in another.
- Assigning to a `const` variable is an error.

**Functions**

- A non-`void` function must return a value on every path that can reach its
  closing brace.
- A call must pass exactly as many arguments as the function declares.
- An argument is converted to the parameter type if that conversion is defined
  (numeric widening or narrowing); otherwise it is an error.
- Function pointers can be called through any expression that yields one, such as
  a struct field or an array slot: `calc.run(3, 4)`, `handlers[i](x)`.

**Memory**

- There are no raw pointers and no null. See [Memory Model](#memory-model).
- An `own` value has exactly one owner; using it after it was moved is an error.
- A value may have many `&` borrows or one `&mut` borrow, never both at once.
- A borrow may not outlive the value it points at.
- An optional (`own T?`) must be unwrapped with `if some` before a field can be
  read or an element indexed. There is no way to reach through one directly.
- Indexing a slice is bounds-checked. With `[T; N]` the length is part of the
  type, so constant indices are verified at compile time instead.
- Locals are allocated once per call, not once per loop iteration, so declaring a
  buffer inside a loop does not grow the stack.
- A struct cannot contain itself by value, directly or through other structs.

**Control flow**

- A `switch` case falls through into the next one unless it ends with `break`.
- Case labels must be compile-time constants and must all be distinct.
- `switch` requires an integer, `char`, `bool` or enum value.
- A `switch` over an enum only accepts labels of that enum, and warns about
  unhandled constants when there is no `default`.

**Constants**

- Dividing by a literal zero is an error, not a crash at run time.
- A global initializer must be a compile-time constant. It may use literals,
  `sizeof`, enum constants, arithmetic on them, and previously declared `const`
  globals — but not a function call.

```b
const int BASE = 7;
int derived = BASE * 3;      // fine: BASE is const
int fromCall = compute();    // error: not a compile-time constant
```

---

## Platform Support

B targets **Linux on x86-64** and nothing else right now.

The reason is the runtime. B does not use C's allocator: `new` goes to B's own
heap, which asks the kernel for memory with `mmap` through an inline `syscall`,
and `std/io.b` reads and writes with `read`/`write`/`open`/`close` the same way.
Those syscall numbers and that calling convention are Linux on x86-64.

The compiler refuses to build for anything else rather than emitting programs
that would fault on their first allocation. Porting means giving these seven
runtime functions an implementation for the target:

| Function | Linux today | Windows would need |
|---|---|---|
| `b_os_alloc` | `mmap` | `VirtualAlloc` |
| `b_os_release` | `munmap` | `VirtualFree` |
| `b_write` / `b_read` | `write` / `read` | `WriteFile` / `ReadFile` |
| `b_open` / `b_close` | `open` / `close` | `CreateFileA` / `CloseHandle` |
| `b_panic` | `write` + `exit_group` | `WriteFile` + `ExitProcess` |

They live in one place — the LLVM IR string `kAllocatorRuntimeIR` in
`src/b_combined.cpp` — so the port is contained, but it is real work and it is
not done.

Earlier versions of B ran on Windows because they used C's `malloc` and `stdio`.
That went away with the move to an own allocator.

---

## Installation Details

### Requirements
- LLVM 14+ (LLVM 22 supported)
- C++17 compiler (GCC or Clang)
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
│   ├── wordcount.b        # Uses the standard library end to end
│   ├── calc.b             # Expression interpreter: lexer, parser, variables
│   ├── project/           # Multi-file project using imports
│   └── modules/           # Multi-file project using namespaces
├── std/                   # Standard library, written in B
│   ├── math.b
│   ├── string.b
│   ├── list.b
│   ├── map.b
│   └── io.b
├── test/run_tests.sh      # End-to-end compiler test suite (114 cases)
├── docs/Architecture.md   # Compiler pipeline walkthrough
├── install.sh / get.sh    # Linux installer and bootstrap script
├── CMakeLists.txt         # Alternative CMake build
└── README.md              # This file
```

The entire compiler lives in one file, compiled directly with `g++`/`clang++` and LLVM — no intermediate build system.

---

## Tips and Best Practices

1. **Use `const` for immutable values** — helps catch bugs:
   ```b
   const int BUFFER_SIZE = 256;
   ```

2. **Use `own T?` when a value may be absent** — the compiler then forces you to
   handle both cases:
   ```b
   if some (v = maybe) {
       // v is present here
   } else {
       // nothing to work with
   }
   ```

3. **Let ownership free things for you** — an `own` value is released at the end
   of its block, so leaks and double frees are not expressible:
   ```b
   own Buffer b = new Buffer { size: 100 };
   // no free() needed
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
    char name[100];
    printf("Enter your name: ");
    scanf("%99s", name);  // Limit input to prevent overflow

    printf("Hello, %s!\n", name);

    return 0;
}
```

### Array Operations

```b
int main() {
    int arr[10];

    for (int i = 0; i < 10; i = i + 1) {
        arr[i] = i * i;
    }

    for (int i = 0; i < 10; i = i + 1) {
        printf("%d ", arr[i]);
    }
    println("");

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
  all imported files — rename one, move the shared definition into a module both
  files import, or put each definition in its own `namespace`

**Q: "Unknown namespace 'X'"**
- `X` is not a namespace that is visible here. Check the spelling, and remember
  that a nested namespace needs its parent: `app::math`, not `math`, unless you
  are already inside `app` or wrote `using namespace app;`

**Q: "'name' is ambiguous: it is declared in ... and in ..."**
- Two `using namespace` declarations make the same name visible. Qualify the use
  (`one::f()`) to say which one you meant

**Q: "'a::x' and 'a__x' both flatten to 'a__x'"**
- A namespace member and a handwritten name collide after flattening. Rename
  either one; `__` in an identifier is what a `::` becomes

**Q: "Cannot use enum 'X' for ... of type 'int'"**
- Enums are distinct types. Convert explicitly with `(int)value` or `(X)number`

**Q: "Function 'f' must return a value on every path"**
- Some route through `f` reaches the closing brace without a `return`. Add a
  `return` at the end, or make the final `if` an `if`/`else` in which both arms
  return

**Q: "... must be a compile-time constant"**
- A global initializer or a `case` label is being computed at run time. Move the
  computation into a function, or declare the value it depends on as `const`

**Q: "Undefined variable" for something declared just above**
- The declaration is inside a block that has already closed. A name declared
  between `{` and `}` does not outlive them

**Q: "Integer literal ... does not fit in 'int'"**
- `int` is 32-bit. Split the value, or keep it in a `double` if you only need
  its magnitude

**Q: "Generic function 'f' needs type arguments"**
- Type arguments are explicit: write `f<int>(x)`, not `f(x)`

**Q: "'x' is used after it was moved"**
- An `own` value has exactly one owner. Passing it to a function or assigning it
  elsewhere hands it over. Borrow it with `&x` instead if the callee only needs
  to look at it

**Q: "cannot borrow 'x' mutably while it is borrowed"**
- A value can have many `&` borrows or one `&mut`, never both. Put the earlier
  borrow in its own block so it ends before the mutable one starts

**Q: "'r' would outlive 'x'"**
- The borrow lives longer than the value it points at. Move the value out to the
  wider scope, or keep the borrow inside the narrower one

**Q: Unexpected output from arithmetic**
- Remember that integer division truncates: `5 / 2 = 2`
- Use `float` or `double` for decimal arithmetic

**Q: "cannot reach 'x' through 'own T?' without unwrapping it first"**
- An optional may be empty, so B does not let you read through it. Wrap the
  access in `if some (value = thing) { ... }`

**Q: "'f' has no body"**
- B has no forward declarations. Delete the prototype; a function may be called
  before the line that defines it, in any file

**Q: "Two string literals side by side are not joined"**
- C's implicit concatenation does not exist in B. Write one literal, or build the
  value with `text::concat`

**Q: Segmentation fault**
- Null dereferences are no longer possible, so the usual cause is an array index
  outside its bounds. Bounds are not checked yet — that arrives with dependent
  types

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

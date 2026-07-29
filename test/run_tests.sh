#!/usr/bin/env bash

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ $# -ge 1 ]; then
    B="$1"
elif [ -x "$REPO_ROOT/build/b" ]; then
    B="$REPO_ROOT/build/b"
elif command -v b >/dev/null 2>&1; then
    B="$(command -v b)"
else
    echo "No B compiler found. Build it first or pass its path." >&2
    exit 1
fi

export B_PATH="$REPO_ROOT"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PASSED=0
FAILED=0

pass() { PASSED=$((PASSED + 1)); printf '  ok    %s\n' "$1"; }
fail() {
    FAILED=$((FAILED + 1))
    printf '  FAIL  %s\n' "$1"
    printf '        expected: %s\n' "$2"
    printf '        actual:   %s\n' "$3"
}

compile_in() {
    local entry="$1"
    shift
    (cd "$(dirname "$entry")" && "$B" "$(basename "$entry")" "$@" 2>&1)
}

run_case() {
    local name="$1" entry="$2" expected="$3"
    local out

    if ! out="$(compile_in "$entry")"; then
        fail "$name" "successful compile" "$(printf '%s' "$out" | tail -2 | tr '\n' ' ')"
        return
    fi

    local exe
    exe="$(dirname "$entry")/$(basename "${entry%.b}")"
    if ! out="$("$exe" 2>&1)"; then
        fail "$name" "successful run" "$out"
        return
    fi

    if [ "$out" = "$expected" ]; then
        pass "$name"
    else
        fail "$name" "$(printf '%s' "$expected" | tr '\n' '|')" "$(printf '%s' "$out" | tr '\n' '|')"
    fi
}

expect_error() {
    local name="$1" entry="$2" needle="$3"
    local out

    if out="$(compile_in "$entry")"; then
        fail "$name" "compile error containing '$needle'" "compiled successfully"
    elif printf '%s' "$out" | grep -qF -- "$needle"; then
        pass "$name"
    else
        fail "$name" "error containing '$needle'" "$(printf '%s' "$out" | tail -2 | tr '\n' ' ')"
    fi
}

expect_debug() {
    local name="$1" entry="$2" needle="$3"
    local out

    if ! out="$(compile_in "$entry" --debug)"; then
        fail "$name" "successful compile" "$(printf '%s' "$out" | tail -2 | tr '\n' ' ')"
        return
    fi
    if printf '%s' "$out" | grep -qF -- "$needle"; then
        pass "$name"
    else
        fail "$name" "--debug output containing '$needle'" "not found"
    fi
}

echo "Using compiler: $B"

echo
echo "Shipped examples"

for example in hello generics enums linked_list; do
    mkdir -p "$WORK/$example"
    cp "$REPO_ROOT/examples/$example.b" "$WORK/$example/"
    if out="$(compile_in "$WORK/$example/$example.b")" && "$WORK/$example/$example" >/dev/null 2>&1; then
        pass "examples/$example.b"
    else
        fail "examples/$example.b" "compiles and runs" "$(printf '%s' "$out" | tail -2 | tr '\n' ' ')"
    fi
done

cp -r "$REPO_ROOT/examples/project" "$WORK/project"
run_case project "$WORK/project/main.b" "low:  Vec2(2, 5)
high: Vec2(6, 15)"

cp -r "$REPO_ROOT/examples/modules" "$WORK/modules"
run_case modules "$WORK/modules/main.b" "low:  Vec2(2, 5)
high: Vec2(6, 15)
global SCALE = 100
geometry     = 3
clamped      = 42"

echo
echo "Imports"

mkdir -p "$WORK/nested/deep"
echo 'int leaf() { return 7; }' > "$WORK/nested/deep/leaf.b"
printf 'import "deep/leaf.b";\nint mid() { return leaf() + 1; }\n' > "$WORK/nested/mid.b"
printf 'import "mid";\nint main() { printf("%%d\\n", mid()); return 0; }\n' > "$WORK/nested/main.b"
run_case import-relative-and-extensionless "$WORK/nested/main.b" "8"

mkdir -p "$WORK/diamond"
echo 'int base() { return 1; }' > "$WORK/diamond/base.b"
printf 'import "base.b";\nint left() { return base(); }\n' > "$WORK/diamond/left.b"
printf 'import "base.b";\nint right() { return base() + 1; }\n' > "$WORK/diamond/right.b"
printf 'import "left.b";\nimport "right.b";\nint main() { printf("%%d\\n", left() + right()); return 0; }\n' \
    > "$WORK/diamond/main.b"
run_case import-diamond-loads-once "$WORK/diamond/main.b" "3"

mkdir -p "$WORK/cycle"
printf 'import "a.b";\nint main() { printf("%%d\\n", ping(3)); return 0; }\n' > "$WORK/cycle/main.b"
printf 'import "b.b";\nint ping(int n) { if (n <= 0) { return 0; } return 1 + pong(n - 1); }\n' \
    > "$WORK/cycle/a.b"
printf 'import "a.b";\nint pong(int n) { if (n <= 0) { return 0; } return 1 + ping(n - 1); }\n' \
    > "$WORK/cycle/b.b"
run_case import-cycle-compiles "$WORK/cycle/main.b" "3"
expect_debug import-cycle-reported "$WORK/cycle/main.b" "Import cycle: a.b -> b.b -> a.b"

mkdir -p "$WORK/missing"
printf 'import "nope.b";\nint main() { return 0; }\n' > "$WORK/missing/main.b"
expect_error import-missing-module "$WORK/missing/main.b" "Cannot find module 'nope.b'"

mkdir -p "$WORK/inner-import"
printf 'int main() { import "x.b"; return 0; }\n' > "$WORK/inner-import/main.b"
expect_error import-must-be-top-level "$WORK/inner-import/main.b" "import must appear at the top level"

echo
echo "Namespaces"

mkdir -p "$WORK/ns-basic"
cat > "$WORK/ns-basic/main.b" <<'CASE'
namespace outer {
    int base() { return 10; }
    namespace inner {
        int twice(int v) { return v * 2; }
    }
    int viaChild() { return inner::twice(base()); }
}
int main() {
    printf("%d %d %d\n", outer::base(), outer::inner::twice(3), outer::viaChild());
    return 0;
}
CASE
run_case ns-nested-and-sibling-lookup "$WORK/ns-basic/main.b" "10 6 20"

mkdir -p "$WORK/ns-shorthand"
cat > "$WORK/ns-shorthand/lib.b" <<'CASE'
namespace app::math {
    int twice(int v) { return v * 2; }
}
CASE
cat > "$WORK/ns-shorthand/main.b" <<'CASE'
import "lib.b";
namespace app::math {
    int quad(int v) { return twice(twice(v)); }
}
namespace app {
    int fromParent(int v) { return math::quad(v); }
}
int main() {
    printf("%d %d\n", app::math::quad(3), app::fromParent(5));
    return 0;
}
CASE
run_case ns-shorthand-and-reopening "$WORK/ns-shorthand/main.b" "12 20"

mkdir -p "$WORK/ns-shadow"
cat > "$WORK/ns-shadow/main.b" <<'CASE'
namespace codes { int add(int a, int b) { return a + b; } }
int add(int a, int b) { return a + b + 1000; }
struct Holder { int add; };
int main() {
    Holder h;
    h.add = 7;
    printf("%d %d %d %d\n", codes::add(3, 4), add(3, 4), ::add(1, 1), h.add);
    return 0;
}
CASE
run_case ns-globals-fields-stay-separate "$WORK/ns-shadow/main.b" "7 1007 1002 7"

mkdir -p "$WORK/ns-generic"
cat > "$WORK/ns-generic/main.b" <<'CASE'
namespace box {
    struct Cell<T> {
        T value;
    };
    T unwrap<T>(&Cell<T> c) { return c.value; }
    own Cell<int> intCell(int v) {
        return new Cell<int> { value: v };
    }
}
int main() {
    own box::Cell<int> c = box::intCell(41);
    printf("%d\n", box::unwrap<int>(&c));
    return 0;
}
CASE
run_case ns-generics "$WORK/ns-generic/main.b" "41"

mkdir -p "$WORK/ns-typedef"
cat > "$WORK/ns-typedef/main.b" <<'CASE'
namespace codes {
    typedef int (*Op)(int, int);
    int mul(int a, int b) { return a * b; }
    int apply(Op op, int a, int b) { return op(a, b); }
}
int main() {
    printf("%d\n", codes::apply(codes::mul, 3, 4));
    return 0;
}
CASE
run_case ns-function-pointer-typedef "$WORK/ns-typedef/main.b" "12"

mkdir -p "$WORK/ns-enum"
cat > "$WORK/ns-enum/main.b" <<'CASE'
namespace level {
    enum Kind { LOW, HIGH };
    int rank(Kind k) {
        switch (k) {
            case LOW: return 1;
            case HIGH: return 9;
        }
        return 0;
    }
}
int main() {
    printf("%d %d\n", level::rank(level::LOW), level::rank(level::HIGH));
    return 0;
}
CASE
run_case ns-enum-constants "$WORK/ns-enum/main.b" "1 9"

mkdir -p "$WORK/ns-using"
cat > "$WORK/ns-using/lib.b" <<'CASE'
namespace g { int val() { return 5; } }
CASE
cat > "$WORK/ns-using/helper.b" <<'CASE'
import "lib.b";
using namespace g;
int viaUsing() { return val(); }
CASE
cat > "$WORK/ns-using/main.b" <<'CASE'
import "helper.b";
int main() { printf("%d\n", viaUsing() + g::val()); return 0; }
CASE
run_case ns-using-is-file-scoped "$WORK/ns-using/main.b" "10"

cat > "$WORK/ns-using/leak.b" <<'CASE'
import "helper.b";
int main() { return val(); }
CASE
expect_error ns-using-does-not-leak "$WORK/ns-using/leak.b" "cannot find function 'val'"

mkdir -p "$WORK/ns-mixed"
cat > "$WORK/ns-mixed/main.b" <<'CASE'
namespace geo {
    struct Vec2 { int x; int y; };
}
namespace box {
    struct Cell<T> { T value; };
    T get<T>(&Cell<T> c) { return c.value; }
}
namespace app {
    using namespace geo;
    int total() { return helper(4, 6); }
    int helper(int a, int b) {
        Vec2 v;
        v.x = a;
        v.y = b;
        return v.x + v.y;
    }
}
int main() {
    own box::Cell<geo::Vec2> c = new box::Cell<geo::Vec2> { };
    c.value.x = 11;
    geo::Vec2 got = box::get<geo::Vec2>(&c);
    printf("%d %d\n", got.x, app::total());
    return 0;
}
CASE
run_case ns-cross-namespace-generics "$WORK/ns-mixed/main.b" "11 10"

echo
echo "Namespace diagnostics"

mkdir -p "$WORK/ns-err"
cat > "$WORK/ns-err/ambiguous.b" <<'CASE'
namespace one { int f() { return 1; } }
namespace two { int f() { return 2; } }
using namespace one;
using namespace two;
int main() { return f(); }
CASE
expect_error ns-ambiguous-using "$WORK/ns-err/ambiguous.b" "'f' is ambiguous"

cat > "$WORK/ns-err/unknown-ns.b" <<'CASE'
namespace geo { int f() { return 1; } }
int main() { return nope::f(); }
CASE
expect_error ns-unknown-namespace "$WORK/ns-err/unknown-ns.b" "Unknown namespace 'nope'"

cat > "$WORK/ns-err/unknown-member.b" <<'CASE'
namespace geo { int f() { return 1; } }
int main() { return geo::missing(); }
CASE
expect_error ns-unknown-member "$WORK/ns-err/unknown-member.b" "'missing' is not declared in namespace 'geo'"

cat > "$WORK/ns-err/unterminated.b" <<'CASE'
namespace geo { int f() { return 1; }
int main() { return geo::f(); }
CASE
expect_error ns-unterminated "$WORK/ns-err/unterminated.b" "Unterminated namespace 'geo'"

cat > "$WORK/ns-err/collision.b" <<'CASE'
namespace a { int x() { return 1; } }
int a__x() { return 2; }
int main() { return a::x() + a__x(); }
CASE
expect_error ns-flatten-collision "$WORK/ns-err/collision.b" "both flatten to 'a__x'"

cat > "$WORK/ns-err/bad-using.b" <<'CASE'
using foo;
int main() { return 0; }
CASE
expect_error ns-using-needs-namespace "$WORK/ns-err/bad-using.b" "Expected 'namespace' after 'using'"

cat > "$WORK/ns-err/no-brace.b" <<'CASE'
namespace geo;
int main() { return 0; }
CASE
expect_error ns-needs-brace "$WORK/ns-err/no-brace.b" "Expected '{' after namespace 'geo'"

echo
echo "Language semantics"

mkdir -p "$WORK/sem"

cat > "$WORK/sem/shortcircuit.b" <<'CASE'
int loud() { print("EVALUATED"); return 1; }
int guard(int n) { return 100 / n; }
int main() {
    int zero = 0;
    if (zero != 0 && guard(zero) == 1) { print("boom"); }
    if (1 == 1 || loud() == 1) { print("ok"); }
    return 0;
}
CASE
run_case sem-short-circuit "$WORK/sem/shortcircuit.b" "ok"

cat > "$WORK/sem/shadow.b" <<'CASE'
pub int x = 5;
int main() {
    int x = 10;
    x = x + 1;
    printf("%d\n", x);
    return 0;
}
CASE
run_case sem-local-shadows-global "$WORK/sem/shadow.b" "11"

cat > "$WORK/sem/blockscope.b" <<'CASE'
int main() { if (1) { int y = 1; printf("%d", y); } return y; }
CASE
expect_error sem-block-scope-ends "$WORK/sem/blockscope.b" "cannot find 'y' in this scope"

cat > "$WORK/sem/constscope.b" <<'CASE'
void f() { const int x = 1; printf("%d", x); }
int main() { f(); int x = 2; x = 3; printf("%d\n", x); return 0; }
CASE
run_case sem-const-is-per-function "$WORK/sem/constscope.b" "13"

cat > "$WORK/sem/doubles.b" <<'CASE'
void show(double d) { printf("%.15f\n", d); }
int main() {
    double d = 3.141592653589793;
    printf("%.15f\n", d);
    show(1.5);
    show(2);
    return 0;
}
CASE
run_case sem-double-precision "$WORK/sem/doubles.b" "3.141592653589793
1.500000000000000
2.000000000000000"

cat > "$WORK/sem/bools.b" <<'CASE'
int main() {
    char c = 65;
    printf("%d %d ", (int)true, (int)false);
    print(c);
    return 0;
}
CASE
run_case sem-bool-and-char "$WORK/sem/bools.b" "1 0 A"

cat > "$WORK/sem/fallthrough.b" <<'CASE'
int main() {
    for (int i = 1; i <= 3; i = i + 1) {
        switch (i) {
            case 1: print("a");
            case 2: print("b"); break;
            default: print("c"); break;
        }
    }
    print("\n");
    return 0;
}
CASE
run_case sem-switch-fallthrough "$WORK/sem/fallthrough.b" "abbc"

cat > "$WORK/sem/loopalloca.b" <<'CASE'
int main() {
    int total = 0;
    for (int i = 0; i < 500000; i = i + 1) {
        int buf[32];
        buf[0] = i;
        total = total + (buf[0] % 2);
    }
    printf("%d\n", total);
    return 0;
}
CASE
run_case sem-locals-do-not-grow-stack "$WORK/sem/loopalloca.b" "250000"

cat > "$WORK/sem/globals.b" <<'CASE'
const int BASE = 7;
int derived = BASE * 3;
string greeting = "hi";
int main() {
    printf("%d %d %s\n", BASE, derived, greeting);
    return 0;
}
CASE
run_case sem-constant-global-initializers "$WORK/sem/globals.b" "7 21 hi"

cat > "$WORK/sem/fnptr.b" <<'CASE'
typedef int (*Op)(int, int);
struct Calc { Op run; };
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
int main() {
    Calc c;
    c.run = add;
    printf("%d ", c.run(3, 4));
    c.run = mul;
    printf("%d\n", c.run(3, 4));
    return 0;
}
CASE
run_case sem-call-function-pointer-field "$WORK/sem/fnptr.b" "7 12"

echo
echo "Semantic diagnostics"

mkdir -p "$WORK/sem-err"

cat > "$WORK/sem-err/noreturn.b" <<'CASE'
int f(int x) { if (x > 0) { return 1; } }
int main() { return f(1); }
CASE
expect_error sem-missing-return "$WORK/sem-err/noreturn.b" "must return a value"

cat > "$WORK/sem-err/allpaths.b" <<'CASE'
int f(int x) { if (x > 0) { return 1; } else { return 2; } }
int main() { printf("%d %d\n", f(1), f(-1)); return 0; }
CASE
run_case sem-return-on-all-paths-is-fine "$WORK/sem-err/allpaths.b" "1 2"

cat > "$WORK/sem-err/bigint.b" <<'CASE'
int main() { return 3000000000; }
CASE
expect_error sem-integer-literal-range "$WORK/sem-err/bigint.b" "does not fit in 'int'"

cat > "$WORK/sem-err/divzero.b" <<'CASE'
int main() { return 1 / 0; }
CASE
expect_error sem-division-by-zero "$WORK/sem-err/divzero.b" "Division by zero"

cat > "$WORK/sem-err/dupcase.b" <<'CASE'
int main() { int x = 1; switch (x) { case 1: break; case 1: break; } return 0; }
CASE
expect_error sem-duplicate-case "$WORK/sem-err/dupcase.b" "Duplicate case label"

cat > "$WORK/sem-err/varcase.b" <<'CASE'
int f() { return 1; }
int main() { int x = 1; switch (x) { case f(): break; } return 0; }
CASE
expect_error sem-non-constant-case "$WORK/sem-err/varcase.b" "compile-time constant"

cat > "$WORK/sem-err/globalcall.b" <<'CASE'
int f() { return 3; }
int g = f();
int main() { return g; }
CASE
expect_error sem-global-needs-constant "$WORK/sem-err/globalcall.b" "must be a compile-time constant"

cat > "$WORK/sem-err/structcycle.b" <<'CASE'
struct A { B b; };
struct B { A a; };
int main() { A x; return x.b.a.b.a; }
CASE
expect_error sem-struct-value-cycle "$WORK/sem-err/structcycle.b" "contains itself by value"

echo
echo "Diagnostics"

mkdir -p "$WORK/diag"

cat > "$WORK/diag/typo.b" <<'CASE'
int main() {
    int total = 3;
    printf("%d\n", totl);
    return 0;
}
CASE
expect_error diag-suggests-close-name "$WORK/diag/typo.b" "did you mean 'total'?"
expect_error diag-points-at-line-and-column "$WORK/diag/typo.b" "typo.b:3:20:"

cat > "$WORK/diag/caret.b" <<'CASE'
int main() {
    return nope;
}
CASE
expect_error diag-shows-source-line "$WORK/diag/caret.b" "return nope;"

cat > "$WORK/diag/many.b" <<'CASE'
int main() {
    int a = 1;
    int b = 2;
    return 0;
}
CASE
expect_error diag-reports-every-error "$WORK/diag/many.b" "2 errors"

echo
echo "Unused-code rules"

mkdir -p "$WORK/unused"

cat > "$WORK/unused/var.b" <<'CASE'
int main() { int spare = 1; return 0; }
CASE
expect_error unused-variable "$WORK/unused/var.b" "unused variable 'spare'"

cat > "$WORK/unused/written.b" <<'CASE'
int main() { int spare = 1; spare = 2; return 0; }
CASE
expect_error unused-write-only-variable "$WORK/unused/written.b" "never read"

cat > "$WORK/unused/param.b" <<'CASE'
int twice(int v, int extra) { return v * 2; }
int main() { return twice(2, 3); }
CASE
expect_error unused-parameter "$WORK/unused/param.b" "unused parameter 'extra'"

cat > "$WORK/unused/func.b" <<'CASE'
int helper() { return 1; }
int main() { return 0; }
CASE
expect_error unused-function "$WORK/unused/func.b" "unused function 'helper'"

cat > "$WORK/unused/global.b" <<'CASE'
int spare = 1;
int main() { return 0; }
CASE
expect_error unused-global "$WORK/unused/global.b" "unused global 'spare'"

cat > "$WORK/unused/redeclare.b" <<'CASE'
int main() { int v = 1; int v = 2; printf("%d", v); return 0; }
CASE
expect_error unused-duplicate-in-scope "$WORK/unused/redeclare.b" "already declared in this scope"

cat > "$WORK/unused/opted-out.b" <<'CASE'
pub int spare = 1;
pub int helper() { return 1; }
int twice(int v, int _unusedHint) { return v * 2; }
int main() { int _scratch = 9; printf("%d\n", twice(21, 0)); return 0; }
CASE
run_case unused-pub-and-underscore-opt-out "$WORK/unused/opted-out.b" "42"

cat > "$WORK/unused/argcount.b" <<'CASE'
int twice(int v) { return v * 2; }
int main() { return twice(1, 2); }
CASE
expect_error unused-argument-count "$WORK/unused/argcount.b" "expects 1 argument(s) but got 2"

echo
echo "Allocator"

mkdir -p "$WORK/heap"

cat > "$WORK/heap/churn.b" <<'CASE'
struct Node { int value; own Node? next; };
int main() {
    int total = 0;
    {
        own Node b = new Node { value: 2, next: none };
        own Node a = new Node { value: 1, next: b };
        total = a.value;
        if some (rest = a.next) { total = total + rest.value; }
    }
    {
        own Node reused = new Node { value: 9, next: none };
        total = total + reused.value;
    }
    for (int i = 0; i < 200000; i = i + 1) {
        own Node scratch = new Node { value: i, next: none };
        total = total + (scratch.value % 2);
    }
    printf("%d\n", total);
    return 0;
}
CASE
run_case heap-alloc-free-and-reuse "$WORK/heap/churn.b" "100012"

cat > "$WORK/heap/big.b" <<'CASE'
struct Block { int a; int b; int c; int d; int e; int f; int g; int h; };
int main() {
    int sum = 0;
    for (int i = 0; i < 40000; i = i + 1) {
        own Block held = new Block { a: i, h: i };
        sum = sum + (held.a % 2);
    }
    int buffer[2000];
    buffer[0] = 11;
    buffer[1999] = 22;
    sum = sum + buffer[0] + buffer[1999];
    printf("%d\n", sum);
    return 0;
}
CASE
run_case heap-large-and-multi-chunk "$WORK/heap/big.b" "20033"

if command -v nm >/dev/null 2>&1; then
    if compile_in "$WORK/heap/churn.b" >/dev/null 2>&1; then
        if nm -u "$WORK/heap/churn" 2>/dev/null | grep -qE '\bmalloc\b|\bfree\b'; then
            fail heap-does-not-use-c-malloc "no undefined malloc/free" "still linked against libc allocator"
        else
            pass heap-does-not-use-c-malloc
        fi
    else
        fail heap-does-not-use-c-malloc "successful compile" "compile failed"
    fi
fi

echo
echo "Ownership and moves"

mkdir -p "$WORK/own"

cat > "$WORK/own/chain.b" <<'CASE'
struct Vec2 { int x; int y; };
int sum(own Vec2 v) { return v.x + v.y; }
int main() {
    own Vec2 a = new Vec2 { x: 2, y: 5 };
    own Vec2 b = a;
    printf("%d\n", sum(b));
    return 0;
}
CASE
run_case own-move-chain "$WORK/own/chain.b" "7"

cat > "$WORK/own/defaults.b" <<'CASE'
struct Vec2 { int x; int y; };
int main() {
    own Vec2 v = new Vec2 { y: 7 };
    printf("%d %d\n", v.x, v.y);
    return 0;
}
CASE
run_case own-new-zero-fills-rest "$WORK/own/defaults.b" "0 7"

cat > "$WORK/own/reassign.b" <<'CASE'
struct Vec2 { int x; int y; };
int take(own Vec2 v) { return v.x; }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    int total = 0;
    for (int i = 0; i < 3; i = i + 1) {
        total = total + take(a);
        a = new Vec2 { x: i, y: i };
    }
    printf("%d %d\n", total, a.y);
    return 0;
}
CASE
run_case own-reassign-in-loop-is-fine "$WORK/own/reassign.b" "2 2"

cat > "$WORK/own/afterinit.b" <<'CASE'
struct Vec2 { int x; int y; };
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    own Vec2 b = a;
    printf("%d\n", a.x);
    return 0;
}
CASE
expect_error own-use-after-move "$WORK/own/afterinit.b" "'a' is used after it was moved"

cat > "$WORK/own/twice.b" <<'CASE'
struct Vec2 { int x; int y; };
int take(own Vec2 v) { return v.x; }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    printf("%d %d\n", take(a), take(a));
    return 0;
}
CASE
expect_error own-move-twice-into-call "$WORK/own/twice.b" "has already been moved"

cat > "$WORK/own/branch.b" <<'CASE'
struct Vec2 { int x; int y; };
int take(own Vec2 v) { return v.x; }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    if (a.x > 0) { take(a); }
    return a.y;
}
CASE
expect_error own-move-in-one-branch "$WORK/own/branch.b" "used after it was moved"

cat > "$WORK/own/loop.b" <<'CASE'
struct Vec2 { int x; int y; };
int take(own Vec2 v) { return v.x; }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    int total = 0;
    for (int i = 0; i < 3; i = i + 1) { total = total + take(a); }
    return total;
}
CASE
expect_error own-move-in-loop "$WORK/own/loop.b" "moved inside a loop"

cat > "$WORK/own/badtype.b" <<'CASE'
int main() { own int v = 0; return v; }
CASE
expect_error own-needs-struct-type "$WORK/own/badtype.b" "'own' applies to struct types"

cat > "$WORK/own/badfield.b" <<'CASE'
struct Vec2 { int x; int y; };
int main() {
    own Vec2 v = new Vec2 { z: 1 };
    return v.x;
}
CASE
expect_error own-new-unknown-field "$WORK/own/badfield.b" "has no field 'z'"

echo
echo "Drop and RAII"

mkdir -p "$WORK/drop"

cat > "$WORK/drop/scopes.b" <<'CASE'
struct Vec2 { int x; int y; };
drop Vec2(&mut Vec2 self) { printf("drop(%d)\n", self.x); }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    {
        own Vec2 b = new Vec2 { x: 2, y: 3 };
        printf("inner %d\n", b.x);
    }
    printf("outer %d\n", a.x);
    return 0;
}
CASE
run_case drop-runs-at-scope-exit "$WORK/drop/scopes.b" "inner 2
drop(2)
outer 1
drop(1)"

cat > "$WORK/drop/moved.b" <<'CASE'
struct Vec2 { int x; int y; };
drop Vec2(&mut Vec2 self) { printf("drop(%d)\n", self.x); }
int take(own Vec2 v) { return v.x; }
int main() {
    own Vec2 a = new Vec2 { x: 7, y: 0 };
    printf("taken %d\n", take(a));
    return 0;
}
CASE
run_case drop-not-run-for-moved-value "$WORK/drop/moved.b" "drop(7)
taken 7"

cat > "$WORK/drop/conditional.b" <<'CASE'
struct Vec2 { int x; int y; };
drop Vec2(&mut Vec2 self) { printf("drop(%d)\n", self.x); }
int take(own Vec2 v) { return v.x; }
int main() {
    for (int i = 0; i < 2; i = i + 1) {
        own Vec2 a = new Vec2 { x: i, y: 0 };
        if (i == 0) { take(a); }
        printf("round %d\n", i);
    }
    return 0;
}
CASE
run_case drop-conditional-move-uses-flag "$WORK/drop/conditional.b" "drop(0)
round 0
round 1
drop(1)"

cat > "$WORK/drop/returned.b" <<'CASE'
struct Vec2 { int x; int y; };
drop Vec2(&mut Vec2 self) { printf("drop(%d)\n", self.x); }
own Vec2 make(int v) {
    own Vec2 result = new Vec2 { x: v, y: 0 };
    return result;
}
int main() {
    own Vec2 a = make(5);
    printf("got %d\n", a.x);
    return 0;
}
CASE
run_case drop-ownership-returned-out "$WORK/drop/returned.b" "got 5
drop(5)"

cat > "$WORK/drop/nested.b" <<'CASE'
struct Inner { int tag; };
struct Outer { int id; own Inner child; };
drop Inner(&mut Inner self) { printf("drop Inner(%d)\n", self.tag); }
drop Outer(&mut Outer self) { printf("drop Outer(%d)\n", self.id); }
int main() {
    own Outer o = new Outer { id: 1, child: new Inner { tag: 9 } };
    printf("built %d %d\n", o.id, o.child.tag);
    return 0;
}
CASE
run_case drop-owned-fields-recursively "$WORK/drop/nested.b" "built 1 9
drop Outer(1)
drop Inner(9)"

cat > "$WORK/drop/reassign.b" <<'CASE'
struct Vec2 { int x; int y; };
drop Vec2(&mut Vec2 self) { printf("drop(%d)\n", self.x); }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 0 };
    printf("first %d\n", a.x);
    a = new Vec2 { x: 2, y: 0 };
    printf("second %d\n", a.x);
    return 0;
}
CASE
run_case drop-reassignment-releases-old "$WORK/drop/reassign.b" "first 1
drop(1)
second 2
drop(2)"

cat > "$WORK/drop/jumps.b" <<'CASE'
struct Vec2 { int x; int y; };
drop Vec2(&mut Vec2 self) { printf("drop(%d)\n", self.x); }
int main() {
    for (int i = 0; i < 4; i = i + 1) {
        own Vec2 a = new Vec2 { x: i, y: 0 };
        if (i == 1) { continue; }
        if (i == 2) { break; }
        printf("body %d\n", a.x);
    }
    printf("after\n");
    return 0;
}
CASE
run_case drop-on-break-and-continue "$WORK/drop/jumps.b" "body 0
drop(0)
drop(1)
drop(2)
after"

cat > "$WORK/drop/pressure.b" <<'CASE'
struct Big { int a; int b; int c; int d; int e; int f; int g; int h; };
int main() {
    int acc = 0;
    for (int i = 0; i < 1000000; i = i + 1) {
        own Big v = new Big { a: i, h: i };
        acc = acc + (v.a % 2);
    }
    printf("%d\n", acc);
    return 0;
}
CASE
run_case drop-reclaims-memory-under-pressure "$WORK/drop/pressure.b" "500000"

cat > "$WORK/drop/badsig.b" <<'CASE'
struct Vec2 { int x; int y; };
drop Vec2(int v) { printf("%d", v); }
int main() { own Vec2 a = new Vec2 { x: 1, y: 2 }; return a.x; }
CASE
expect_error drop-signature-must-take-mut-borrow "$WORK/drop/badsig.b" "must take a '&mut Vec2'"

cat > "$WORK/drop/twice.b" <<'CASE'
struct Vec2 { int x; int y; };
drop Vec2(&mut Vec2 self) { printf("a%d", self.x); }
drop Vec2(&mut Vec2 self) { printf("b%d", self.x); }
int main() { own Vec2 a = new Vec2 { x: 1, y: 2 }; return a.x; }
CASE
expect_error drop-only-one-per-struct "$WORK/drop/twice.b" "already has a drop function"

echo
echo "Borrows and lifetimes"

mkdir -p "$WORK/borrow"

cat > "$WORK/borrow/basic.b" <<'CASE'
struct Vec2 { int x; int y; };
drop Vec2(&mut Vec2 self) { printf("drop(%d)\n", self.x); }
int sum(&Vec2 v) { return v.x + v.y; }
void bump(&mut Vec2 v) { v.x = v.x + 10; }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    printf("%d ", sum(&a));
    bump(&mut a);
    printf("%d\n", sum(&a));
    return 0;
}
CASE
run_case borrow-shared-and-mutable "$WORK/borrow/basic.b" "3 13
drop(11)"

cat > "$WORK/borrow/released.b" <<'CASE'
struct Vec2 { int x; int y; };
int sum(&Vec2 v) { return v.x + v.y; }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    {
        &Vec2 r = &a;
        printf("%d\n", sum(r));
    }
    own Vec2 b = a;
    printf("%d\n", sum(&b));
    return 0;
}
CASE
run_case borrow-ends-with-its-scope "$WORK/borrow/released.b" "3
3"

cat > "$WORK/borrow/twomut.b" <<'CASE'
struct Vec2 { int x; int y; };
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    &mut Vec2 p = &mut a;
    &mut Vec2 q = &mut a;
    return p.x + q.x;
}
CASE
expect_error borrow-two-mutable-borrows "$WORK/borrow/twomut.b" "already mutably borrowed"

cat > "$WORK/borrow/sharedthenmut.b" <<'CASE'
struct Vec2 { int x; int y; };
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    &Vec2 r = &a;
    &mut Vec2 w = &mut a;
    return r.x + w.y;
}
CASE
expect_error borrow-mutable-while-shared "$WORK/borrow/sharedthenmut.b" "mutably while it is borrowed"

cat > "$WORK/borrow/usewhilemut.b" <<'CASE'
struct Vec2 { int x; int y; };
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    &mut Vec2 w = &mut a;
    return a.x + w.y;
}
CASE
expect_error borrow-use-while-mutably-borrowed "$WORK/borrow/usewhilemut.b" "while it is mutably borrowed"

cat > "$WORK/borrow/moveborrowed.b" <<'CASE'
struct Vec2 { int x; int y; };
int take(own Vec2 v) { return v.x; }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    &Vec2 r = &a;
    return take(a) + r.y;
}
CASE
expect_error borrow-move-while-borrowed "$WORK/borrow/moveborrowed.b" "cannot move 'a' while it is borrowed"

cat > "$WORK/borrow/mutatethroughshared.b" <<'CASE'
struct Vec2 { int x; int y; };
void poke(&Vec2 v) { v.x = 5; }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    poke(&a);
    return a.x;
}
CASE
expect_error borrow-no-mutation-through-shared "$WORK/borrow/mutatethroughshared.b" "shared borrow"

cat > "$WORK/borrow/dangling.b" <<'CASE'
struct Vec2 { int x; int y; };
&Vec2 leak() {
    own Vec2 v = new Vec2 { x: 1, y: 2 };
    return &v;
}
int main() { return leak().x; }
CASE
expect_error borrow-no-dangling-return "$WORK/borrow/dangling.b" "cannot return a borrow of 'v'"

cat > "$WORK/borrow/outlive.b" <<'CASE'
struct Vec2 { int x; int y; };
int main() {
    own Vec2 outer = new Vec2 { x: 9, y: 9 };
    &Vec2 r = &outer;
    {
        own Vec2 inner = new Vec2 { x: 1, y: 2 };
        r = &inner;
    }
    return r.x;
}
CASE
expect_error borrow-must-not-outlive-owner "$WORK/borrow/outlive.b" "would outlive 'inner'"

cat > "$WORK/borrow/moved.b" <<'CASE'
struct Vec2 { int x; int y; };
int take(own Vec2 v) { return v.x; }
int main() {
    own Vec2 a = new Vec2 { x: 1, y: 2 };
    take(a);
    &Vec2 r = &a;
    return r.x;
}
CASE
expect_error borrow-of-moved-value "$WORK/borrow/moved.b" "because it has been moved"

echo
echo "Optionals and the end of null"

mkdir -p "$WORK/opt"

cat > "$WORK/opt/list.b" <<'CASE'
struct Node { int value; own Node? next; };
drop Node(&mut Node self) { printf("free %d\n", self.value); }
int total(&Node n) {
    int sum = n.value;
    if some (rest = n.next) { sum = sum + total(rest); }
    return sum;
}
int length(&Node n) {
    if some (rest = n.next) { return 1 + length(rest); }
    return 1;
}
int main() {
    own Node a = new Node { value: 3, next: none };
    own Node b = new Node { value: 2, next: a };
    own Node c = new Node { value: 1, next: b };
    printf("%d %d\n", total(&c), length(&c));
    return 0;
}
CASE
run_case option-linked-list-frees-once "$WORK/opt/list.b" "6 3
free 1
free 2
free 3"

cat > "$WORK/opt/else.b" <<'CASE'
struct Node { int value; own Node? next; };
int describe(&Node n) {
    if some (rest = n.next) {
        return rest.value;
    } else {
        return -1;
    }
}
int main() {
    own Node tail = new Node { value: 9, next: none };
    own Node head = new Node { value: 1, next: tail };
    printf("%d %d\n", describe(&head), 0);
    return 0;
}
CASE
run_case option-if-some-else "$WORK/opt/else.b" "9 0"

cat > "$WORK/opt/rawptr.b" <<'CASE'
struct Vec2 { int x; int y; };
int main() {
    Vec2* v = new Vec2 { x: 1, y: 2 };
    return v->x;
}
CASE
expect_error option-raw-pointers-are-gone "$WORK/opt/rawptr.b" "Raw pointers were removed from B"

cat > "$WORK/opt/malloc.b" <<'CASE'
int main() {
    int n = malloc(8);
    return n;
}
CASE
expect_error option-malloc-is-gone "$WORK/opt/malloc.b" "cannot find function 'malloc'"

cat > "$WORK/opt/needsoption.b" <<'CASE'
struct Vec2 { int x; int y; };
int main() {
    own Vec2 v = new Vec2 { x: 1, y: 2 };
    if some (inner = v) { return inner.x; }
    return 0;
}
CASE
expect_error option-if-some-needs-optional "$WORK/opt/needsoption.b" "needs an optional value"

cat > "$WORK/opt/plainoptional.b" <<'CASE'
int main() {
    int? n = none;
    return 0;
}
CASE
expect_error option-question-needs-handle "$WORK/opt/plainoptional.b" "'?' applies to 'own' and borrow types"

echo
echo "Slices"

mkdir -p "$WORK/slice"

cat > "$WORK/slice/basic.b" <<'CASE'
int sum(&[int] values) {
    int total = 0;
    for (int i = 0; i < len(values); i = i + 1) {
        total = total + values[i];
    }
    return total;
}
void fill(&mut [int] values) {
    for (int i = 0; i < len(values); i = i + 1) {
        values[i] = i * i;
    }
}
int main() {
    own [int] numbers = new [int](6);
    printf("%d %d ", len(numbers), numbers[3]);
    fill(&mut numbers);
    printf("%d %d\n", sum(&numbers), numbers[5]);
    return 0;
}
CASE
run_case slice-borrow-and-fill "$WORK/slice/basic.b" "6 0 55 25"

cat > "$WORK/slice/bounds.b" <<'CASE'
int main() {
    own [int] values = new [int](3);
    int i = 5;
    printf("%d\n", values[i]);
    return 0;
}
CASE
if out="$(compile_in "$WORK/slice/bounds.b")"; then
    aborted="$("$WORK/slice/bounds" 2>&1)"
    if printf '%s' "$aborted" | grep -qF "index out of range"; then
        pass slice-bounds-are-checked
    else
        fail slice-bounds-are-checked "an out-of-range abort" "$aborted"
    fi
else
    fail slice-bounds-are-checked "successful compile" "compile failed"
fi

cat > "$WORK/slice/drops.b" <<'CASE'
struct Cell { int tag; };
drop Cell(&mut Cell self) { printf("cell %d\n", self.tag); }
int main() {
    own [Cell] cells = new [Cell](3);
    for (int i = 0; i < len(cells); i = i + 1) { cells[i].tag = i; }
    printf("built %d\n", len(cells));
    return 0;
}
CASE
run_case slice-drops-its-elements "$WORK/slice/drops.b" "built 3
cell 0
cell 1
cell 2"

cat > "$WORK/slice/pressure.b" <<'CASE'
int main() {
    int acc = 0;
    for (int i = 0; i < 200000; i = i + 1) {
        own [int] buf = new [int](64);
        buf[0] = i;
        acc = acc + (buf[0] % 2);
    }
    printf("%d\n", acc);
    return 0;
}
CASE
run_case slice-does-not-leak "$WORK/slice/pressure.b" "100000"

cat > "$WORK/slice/moveout.b" <<'CASE'
struct Holder { own [int] data; };
int main() {
    own Holder h = new Holder { data: new [int](4) };
    own [int] stolen = h.data;
    return len(stolen);
}
CASE
expect_error slice-no-move-out-of-field "$WORK/slice/moveout.b" "cannot move ownership out of 'h'"

echo
echo "Standard library"

STDDIR="$WORK/stdlib"
mkdir -p "$STDDIR"
cp -r "$REPO_ROOT/std" "$STDDIR/std"

cat > "$STDDIR/math.b" <<'CASE'
import "std/math.b";
int main() {
    printf("%.6f %.6f %.6f\n", math::sqrt(2.0), math::sqrt(144.0), math::hypot(3.0, 4.0));
    printf("%.6f %.6f\n", math::sin(math::PI / 6.0), math::cos(0.0));
    printf("%.6f %.6f\n", math::exp(1.0), math::ln(math::E));
    printf("%.1f %.1f %d\n", math::floor(3.7), math::ceil(3.2), math::clampInt(99, 0, 10));
    return 0;
}
CASE
run_case std-math "$STDDIR/math.b" "1.414214 12.000000 5.000000
0.500000 1.000000
2.718282 1.000000
3.0 4.0 10"

cat > "$STDDIR/list.b" <<'CASE'
import "std/list.b";
int main() {
    own list::List<int> xs = list::make<int>();
    for (int i = 0; i < 20; i = i + 1) { list::push<int>(&mut xs, i * 3); }
    printf("%d %d ", list::size<int>(&xs), list::capacity<int>(&xs));
    printf("%d %d ", list::get<int>(&xs, 4), list::pop<int>(&mut xs));
    printf("%d %d ", list::indexOf<int>(&xs, 30), (int)list::contains<int>(&xs, 99));
    list::reverse<int>(&mut xs);
    printf("%d ", list::get<int>(&xs, 0));
    list::sort<int>(&mut xs);
    printf("%d\n", list::get<int>(&xs, 0));
    return 0;
}
CASE
run_case std-list "$STDDIR/list.b" "20 32 12 57 10 0 54 0"

cat > "$STDDIR/text.b" <<'CASE'
import "std/string.b";
int main() {
    own text::Str raw = text::fromLiteral("  Hello, wonderful World!  ");
    own text::Str trimmed = text::trim(&raw);
    own text::Str needle = text::fromLiteral("wonderful ");
    own text::Str blank = text::fromLiteral("");
    own text::Str shorter = text::replace(&trimmed, &needle, &blank);
    own text::Str upper = text::toUpper(&shorter);
    printf("[%s] %d\n", text::cstr(&trimmed), text::length(&trimmed));
    printf("%s / %s\n", text::cstr(&shorter), text::cstr(&upper));

    own text::Str csv = text::fromLiteral("a,bb,ccc,dddd");
    own [own text::Str] parts = text::split(&csv, ',');
    printf("%d:", len(parts));
    for (int i = 0; i < len(parts); i = i + 1) {
        printf(" %s", text::cstr(parts[i]));
    }
    printf("\n");

    own text::Str num = text::fromInt(0 - 4711);
    printf("%s %d\n", text::cstr(&num), text::toInt(&num));
    return 0;
}
CASE
run_case std-string "$STDDIR/text.b" "[Hello, wonderful World!] 23
Hello, World! / HELLO, WORLD!
4: a bb ccc dddd
-4711 -4711"

cat > "$STDDIR/dict.b" <<'CASE'
import "std/map.b";
int main() {
    own map::Map<int> ages = map::make<int>();
    own text::Str alice = text::fromLiteral("alice");
    own text::Str bob = text::fromLiteral("bob");
    own text::Str missing = text::fromLiteral("nobody");
    map::put<int>(&mut ages, &alice, 30);
    map::put<int>(&mut ages, &bob, 41);
    map::put<int>(&mut ages, &alice, 31);
    printf("%d %d %d ", map::size<int>(&ages),
           map::getOr<int>(&ages, &alice, 0 - 1), map::getOr<int>(&ages, &bob, 0 - 1));
    printf("%d %d\n", (int)map::has<int>(&ages, &missing),
           map::getOr<int>(&ages, &missing, 0 - 1));

    for (int i = 0; i < 60; i = i + 1) {
        own text::Str key = text::fromInt(i);
        map::put<int>(&mut ages, &key, i * 2);
    }
    own text::Str probe = text::fromLiteral("42");
    printf("%d %d\n", map::size<int>(&ages), map::getOr<int>(&ages, &probe, 0 - 1));
    return 0;
}
CASE
run_case std-map "$STDDIR/dict.b" "2 31 41 0 -1
62 84"

mkdir -p "$STDDIR/examples"
cp "$REPO_ROOT/examples/wordcount.b" "$STDDIR/examples/"
cp "$REPO_ROOT/examples/calc.b" "$STDDIR/examples/"
run_case std-calc-example "$STDDIR/examples/calc.b" "width = 8  ->  width = 8
height = 3 + 2  ->  height = 5
area = width * height  ->  area = 40
area + 10  ->  50
(width + height) * 2  ->  26
area / (height - 5)  ->  cannot parse
-width + area  ->  32
unknown * 2  ->  0
3 +  ->  cannot parse

stored 3 names, area = 40"
run_case std-wordcount-example "$STDDIR/examples/wordcount.b" "word count finished
words: 16, distinct: 11
the=4 fox=2 cat=0
shortest=3 longest=5"

cat > "$STDDIR/files.b" <<'CASE'
import "std/io.b";
int main() {
    own text::Str path = text::fromLiteral("b_stdlib_probe.txt");
    own text::Str body = text::fromLiteral("first\nsecond\nthird\n");
    if (!io::writeFile(&path, &body)) {
        printf("write failed\n");
        return 1;
    }
    if some mut (reader = io::openFile(&path)) {
        int n = 0;
        while (true) {
            if some (line = io::readLine(reader)) {
                n = n + 1;
                printf("%d:%s ", n, text::cstr(line));
            } else {
                printf("| %d\n", n);
                return 0;
            }
        }
    }
    printf("open failed\n");
    return 1;
}
CASE
run_case std-io-file-roundtrip "$STDDIR/files.b" "1:first 2:second 3:third | 3"

echo
echo "Sized slices"

mkdir -p "$WORK/sized"

cat > "$WORK/sized/basic.b" <<'CASE'
const int SIZE = 8;
int firstThree(&[int; SIZE] values) {
    return values[0] + values[1] + values[2];
}
int sumAny(&[int] values) {
    int total = 0;
    for (int i = 0; i < len(values); i = i + 1) { total = total + values[i]; }
    return total;
}
int main() {
    own [int; SIZE] fixed = new [int](SIZE);
    for (int i = 0; i < len(fixed); i = i + 1) { fixed[i] = i + 1; }
    printf("%d %d %d\n", len(fixed), firstThree(&fixed), sumAny(&fixed));
    return 0;
}
CASE
run_case sized-length-in-type "$WORK/sized/basic.b" "8 6 36"

cat > "$WORK/sized/mismatch.b" <<'CASE'
int take(&[int; 4] values) { return values[0]; }
int main() {
    own [int; 8] wrong = new [int](8);
    return take(&wrong);
}
CASE
expect_error sized-length-mismatch-rejected "$WORK/sized/mismatch.b" "expects '&[int; 4]' but got '&[int; 8]'"

cat > "$WORK/sized/initmismatch.b" <<'CASE'
int main() {
    own [int; 4] values = new [int](8);
    return values[0];
}
CASE
expect_error sized-initializer-mismatch "$WORK/sized/initmismatch.b" "Cannot initialize 'values'"

cat > "$WORK/sized/staticoob.b" <<'CASE'
int main() {
    own [int; 4] values = new [int](4);
    return values[9];
}
CASE
expect_error sized-constant-index-checked-at-compile-time "$WORK/sized/staticoob.b" "Index 9 is outside 'own [int; 4]'"

cat > "$WORK/sized/elementtype.b" <<'CASE'
struct Vec2 { int x; int y; };
int take(&[int] values) { return values[0]; }
int main() {
    own [Vec2] wrong = new [Vec2](4);
    return take(&wrong);
}
CASE
expect_error sized-element-type-checked "$WORK/sized/elementtype.b" "expects '&[int]' but got"

cat > "$WORK/sized/coerce.b" <<'CASE'
int sumAny(&[int] values) {
    int total = 0;
    for (int i = 0; i < len(values); i = i + 1) { total = total + values[i]; }
    return total;
}
int main() {
    own [int; 5] sized = new [int](5);
    for (int i = 0; i < 5; i = i + 1) { sized[i] = i; }
    printf("%d\n", sumAny(&sized));
    return 0;
}
CASE
run_case sized-coerces-to-unsized "$WORK/sized/coerce.b" "10"

if command -v objdump >/dev/null 2>&1; then
    cat > "$WORK/sized/bench.b" <<'CASE'
pub int sumFixed(&[int; 64] values) {
    int total = 0;
    for (int i = 0; i < len(values); i = i + 1) { total = total + values[i]; }
    return total;
}
pub int sumAny(&[int] values) {
    int total = 0;
    for (int i = 0; i < len(values); i = i + 1) { total = total + values[i]; }
    return total;
}
int main() {
    own [int; 64] a = new [int](64);
    for (int i = 0; i < 64; i = i + 1) { a[i] = i; }
    printf("%d %d\n", sumFixed(&a), sumAny(&a));
    return 0;
}
CASE
    if compile_in "$WORK/sized/bench.b" >/dev/null 2>&1; then
        fixedSize="$(objdump -d --disassemble=sumFixed "$WORK/sized/bench" 2>/dev/null | grep -cE '^[[:space:]]+[0-9a-f]+:')"
        anySize="$(objdump -d --disassemble=sumAny "$WORK/sized/bench" 2>/dev/null | grep -cE '^[[:space:]]+[0-9a-f]+:')"
        if [ "$fixedSize" -gt 0 ] && [ "$fixedSize" -lt "$anySize" ]; then
            pass "sized-emits-less-code-than-unsized ($fixedSize vs $anySize instructions)"
        else
            fail sized-emits-less-code-than-unsized "fewer instructions than the unsized version" \
                "$fixedSize vs $anySize"
        fi
    else
        fail sized-emits-less-code-than-unsized "successful compile" "compile failed"
    fi
fi

echo
echo "Null safety"

mkdir -p "$WORK/null"

cat > "$WORK/null/deref.b" <<'CASE'
struct V { int x; };
int main() {
    own V? maybe = none;
    printf("%d\n", maybe.x);
    return 0;
}
CASE
expect_error null-no-field-through-optional "$WORK/null/deref.b" "without unwrapping it first"

cat > "$WORK/null/index.b" <<'CASE'
struct V { int x; };
int main() {
    own V? maybe = none;
    own [int] xs = new [int](2);
    if some (v = maybe) { xs[0] = v.x; }
    return xs[maybe.x];
}
CASE
expect_error null-no-index-through-optional "$WORK/null/index.b" "without unwrapping it first"

cat > "$WORK/null/unwrapped.b" <<'CASE'
struct V { int x; };
int main() {
    own V? maybe = new V { x: 4 };
    if some (v = maybe) { printf("%d\n", v.x); }
    return 0;
}
CASE
run_case null-unwrapping-works "$WORK/null/unwrapped.b" "4"

cat > "$WORK/null/ownedslice.b" <<'CASE'
import "std/string.b";
int main() {
    own text::Str csv = text::fromLiteral("a,b");
    own [own text::Str] parts = text::split(&csv, ',');
    printf("%d %s\n", len(parts), text::cstr(parts[1]));
    return 0;
}
CASE
run_case null-owned-slice-is-not-optional "$WORK/null/ownedslice.b" "2 b"

echo
echo "RAII and stack slices"

mkdir -p "$WORK/raii"

cat > "$WORK/raii/guard.b" <<'CASE'
struct Conn { int id; };
drop Conn(&mut Conn self) { printf("closing %d\n", self.id); }
struct Plain { int v; };
int main() {
    own Conn guard = new Conn { id: 1 };
    printf("working\n");
    return 0;
}
CASE
run_case raii-guard-is-not-unused "$WORK/raii/guard.b" "working
closing 1"

cat > "$WORK/raii/plain.b" <<'CASE'
struct Plain { int v; };
int main() {
    own Plain p = new Plain { v: 2 };
    return 0;
}
CASE
expect_error raii-only-drop-types-are-exempt "$WORK/raii/plain.b" "unused variable 'p'"

cat > "$WORK/raii/stackslice.b" <<'CASE'
int sum(&[int] xs) {
    int total = 0;
    for (int i = 0; i < len(xs); i = i + 1) { total = total + xs[i]; }
    return total;
}
int main() {
    int buf[5];
    for (int i = 0; i < 5; i = i + 1) { buf[i] = i + 1; }
    printf("%d %d\n", len(buf), sum(&buf));
    return 0;
}
CASE
run_case raii-stack-array-is-a-real-slice "$WORK/raii/stackslice.b" "5 15"

cat > "$WORK/raii/itoa.b" <<'CASE'
int main() {
    printf("%s\n", itoa(4711));
    return 0;
}
CASE
run_case raii-itoa-uses-b-allocator "$WORK/raii/itoa.b" "4711"

cat > "$WORK/raii/prototype.b" <<'CASE'
int later(int v);
int later(int v) { return v; }
int main() { return later(0); }
CASE
expect_error raii-no-forward-declarations "$WORK/raii/prototype.b" "B needs no forward declarations"

cat > "$WORK/raii/adjacent.b" <<'CASE'
int main() {
    string s = "a" "b";
    printf("%s\n", s);
    return 0;
}
CASE
expect_error raii-adjacent-strings-explained "$WORK/raii/adjacent.b" "not joined in B"

cat > "$WORK/raii/nestedenum.b" <<'CASE'
enum Kind { LOW, HIGH };
struct Slot { Kind kind; };
int main() {
    own [Slot] slots = new [Slot](2);
    slots[0].kind = HIGH;
    slots[1].kind = LOW;
    int score = 0;
    if (slots[0].kind == HIGH) { score = 1; }
    if (slots[1].kind == LOW) { score = score + 10; }
    printf("%d\n", score);
    return 0;
}
CASE
run_case raii-enum-through-nested-place "$WORK/raii/nestedenum.b" "11"

echo
echo "Documentation"

if command -v python3 >/dev/null 2>&1; then
    docOutput="$(python3 "$REPO_ROOT/test/check_docs.py" "$B" "$REPO_ROOT/README.md" 2>&1)"
    if [ $? -eq 0 ]; then
        pass "$(printf '%s' "$docOutput" | head -1)"
    else
        fail doc-blocks-compile "every runnable block compiles" \
            "$(printf '%s' "$docOutput" | tail -3 | tr '\n' ' ')"
    fi
fi

echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]

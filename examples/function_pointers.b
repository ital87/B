typedef int (*Callback)(int, int);

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

int applyOperation(int a, int b, Callback op) {
    return op(a, b);
}

int main() {
    Callback cb1 = add;
    Callback cb2 = multiply;

    int result1 = applyOperation(5, 3, cb1);
    int result2 = applyOperation(5, 3, cb2);

    printf("Add: %d\n", result1);
    printf("Multiply: %d\n", result2);

    int result3 = cb1(10, 20);
    printf("Direct call: %d\n", result3);

    return 0;
}


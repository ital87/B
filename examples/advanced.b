int square(int n) {
    return n * n;
}

int sumSquares(int* arr, int count) {
    int total = 0;
    int i = 0;
    while (i < count) {
        total = total + square(arr[i]);
        i = i + 1;
    }
    return total;
}

int fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    int a = 0;
    int b = 1;
    int i = 2;
    for (i = 2; i <= n; i = i + 1) {
        int next = a + b;
        a = b;
        b = next;
    }
    return b;
}

int main() {
    println("=== Arc Demo ===");

    int* numbers = malloc(5 * 8);
    int i = 0;
    for (i = 0; i < 5; i = i + 1) {
        numbers[i] = i + 1;
    }

    int result = sumSquares(numbers, 5);
    print("Sum of squares 1..5: ");
    println(result);

    free(numbers);

    print("Fibonacci(10): ");
    println(fibonacci(10));

    int n = 5;
    int fact = 1;
    int k = 1;
    while (k <= n) {
        fact = fact * k;
        k = k + 1;
    }
    print("Factorial(5): ");
    println(fact);

    return 0;
}


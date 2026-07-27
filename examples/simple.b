int add(int a, int b) {
    return a + b;
}

int factorial(int n) {
    int result = 1;
    int i = 1;
    while (i <= n) {
        result = result * i;
        i = i + 1;
    }
    return result;
}

int main() {
    int x = 5;
    int y = 3;
    int sum = add(x, y);
    int fact = factorial(x);
    return sum;
}


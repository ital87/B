int main() {
    float a = 3.5;
    float b = 2.0;

    float sum = a + b;
    float diff = a - b;
    float prod = a * b;
    float quot = a / b;

    printf("sum=%f diff=%f prod=%f quot=%f\n", sum, diff, prod, quot);

    if (a > b) {
        println("a > b");
    }
    if (a != b) {
        println("a != b");
    }

    double d1 = 1.5;
    double d2 = 2.5;
    double dsum = d1 + d2;
    printf("dsum=%f\n", dsum);

    float mixed = a + 1;
    printf("mixed=%f\n", mixed);

    float neg = -a;
    printf("neg=%f\n", neg);

    return 0;
}


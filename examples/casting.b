int main() {
    int x = 10;
    float f = (float)x;
    printf("Float: %f\n", f);

    float g = 3.7;
    int y = (int)g;
    printf("Int: %d\n", y);

    int ascii = 65;
    char c = (char)ascii;
    printf("Char: %c\n", c);

    char ch = (char)65;
    int val = (int)ch;
    printf("ASCII: %d\n", val);

    double d = 3.14159;
    float f2 = (float)d;
    printf("Float from double: %f\n", f2);

    float f3 = 2.5;
    double d2 = (double)f3;
    printf("Double from float: %f\n", d2);

    return 0;
}


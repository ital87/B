int main() {
    string s = "Hello";
    int len = strlen(s);
    printf("Laenge: %d\n", len);

    if (strlen(s) > 3) {
        println("String ist lang!");
    }

    string s1 = "hello";
    string s2 = "hello";
    string s3 = "world";

    if (strcmp(s1, s2) == 0) {
        println("Strings sind gleich!");
    }
    if (strcmp(s1, s3) != 0) {
        println("Strings sind unterschiedlich!");
    }

    string original = "Hello";
    string copy = malloc(100);
    strcpy(copy, original);
    printf("Kopie: %s\n", copy);
    free(copy);

    string numberInput = "42";
    int number = atoi(numberInput);
    printf("Zahl: %d\n", number);

    string str = itoa(123);
    printf("String: %s\n", str);
    free(str);

    return 0;
}


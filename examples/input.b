int main() {
    println("Wie heisst du?");
    string name = malloc(100);
    scanf("%s", name);

    println("Wie alt bist du?");
    int age = 0;
    scanf("%d", &age);

    println("");
    printf("Hallo %s! Du bist %d Jahre alt.\n", name, age);

    free(name);
    return 0;
}


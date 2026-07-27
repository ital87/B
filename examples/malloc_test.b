int main() {
    int* buffer = malloc(40);
    buffer[0] = 65;
    buffer[1] = 66;
    buffer[2] = 67;

    printf("Array values: %d %d %d\n", buffer[0], buffer[1], buffer[2]);

    free(buffer);
    return 0;
}


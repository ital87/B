int main() {
    char* buffer = malloc(100);
    if (buffer == 0) {
        return 1;
    }

    buffer[0] = 65;
    buffer[1] = 66;
    buffer[2] = 67;

    int result = buffer[0];
    free(buffer);
    return result;
}


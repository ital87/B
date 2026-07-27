int main() {
    void* file = fopen("examples/minimal.arc", "r");
    if (file == 0) {
        return 1;
    }

    fseek(file, 0, 2);
    int fileSize = ftell(file);
    fseek(file, 0, 0);

    char* buffer = malloc(fileSize + 1);
    if (buffer == 0) {
        fclose(file);
        return 1;
    }

    int bytesRead = fread(buffer, 1, fileSize, file);
    buffer[fileSize] = 0;

    free(buffer);
    fclose(file);
    return bytesRead;
}


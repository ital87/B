int main() {
    int* arr = malloc(40);
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;

    int sum = arr[0] + arr[1] + arr[2];
    free(arr);
    return sum;
}


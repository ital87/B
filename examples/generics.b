struct Box<T> {
    T value;
};

struct Pair<A, B> {
    A first;
    B second;
};

T maxOf<T>(T a, T b) {
    if (a > b) {
        return a;
    }
    return b;
}

Box<T>* boxNew<T>(T value) {
    Box<T>* box = malloc(sizeof(Box<T>));
    box->value = value;
    return box;
}

int main() {
    printf("max int:   %d\n", maxOf<int>(3, 9));
    printf("max float: %f\n", maxOf<float>(1.5, 0.5));

    Box<int>* number = boxNew<int>(42);
    Box<string>* text = boxNew<string>("boxed");
    printf("box int:    %d\n", number->value);
    printf("box string: %s\n", text->value);

    Pair<int, float> pair;
    pair.first = 4;
    pair.second = 0.25;
    printf("pair: %d %f\n", pair.first, pair.second);

    free(number);
    free(text);
    return 0;
}

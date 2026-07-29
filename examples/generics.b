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

own Box<T> boxNew<T>(T value) {
    return new Box<T> { value: value };
}

T unbox<T>(&Box<T> box) {
    return box.value;
}

int main() {
    printf("max int:   %d\n", maxOf<int>(3, 9));
    printf("max float: %f\n", maxOf<float>(1.5, 0.5));

    own Box<int> number = boxNew<int>(42);
    own Box<string> text = boxNew<string>("boxed");
    printf("box int:    %d\n", unbox<int>(&number));
    printf("box string: %s\n", unbox<string>(&text));

    Pair<int, float> pair;
    pair.first = 4;
    pair.second = 0.25;
    printf("pair: %d %f\n", pair.first, pair.second);

    return 0;
}

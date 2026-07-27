enum Color {
    RED,
    GREEN,
    BLUE
};

struct Shape {
    Color color;
    int sides;
};

void describe(Color color) {
    switch (color) {
        case RED:
            println("red");
            break;
        case GREEN:
            println("green");
            break;
        case BLUE:
            println("blue");
            break;
    }
}

int main() {
    Color color = GREEN;
    describe(color);

    if (color != RED) {
        println("not red");
    }

    Shape shape;
    shape.color = BLUE;
    shape.sides = 4;
    describe(shape.color);

    Color parsed = (Color)0;
    describe(parsed);

    printf("BLUE as int: %d\n", (int)BLUE);
    return 0;
}

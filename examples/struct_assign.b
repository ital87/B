struct Point {
    int x;
    int y;
};

int main() {
    Point p;
    p.x = 3;
    p.y = 4;

    print("Point: (");
    print(p.x);
    print(", ");
    print(p.y);
    println(")");

    return p.x + p.y;
}


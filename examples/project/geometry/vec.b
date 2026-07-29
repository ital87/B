import "scalar.b";

struct Vec2 {
    int x;
    int y;
};

own Vec2 vecNew(int x, int y) {
    return new Vec2 { x: x, y: y };
}

own Vec2 vecScale(&Vec2 v) {
    return vecNew(scale(v.x), scale(v.y));
}

void vecPrint(&Vec2 v) {
    printf("Vec2(%d, %d)\n", v.x, v.y);
}

import "scalar.b";

struct Vec2 {
    int x;
    int y;
};

Vec2* vecNew(int x, int y) {
    Vec2* v = malloc(sizeof(Vec2));
    v->x = x;
    v->y = y;
    return v;
}

Vec2* vecScale(Vec2* v) {
    return vecNew(scale(v->x), scale(v->y));
}

void vecPrint(Vec2* v) {
    printf("Vec2(%d, %d)\n", v->x, v->y);
}

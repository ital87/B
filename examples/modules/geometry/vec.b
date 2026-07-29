namespace geometry {

    const int SCALE = 3;

    struct Vec2 {
        int x;
        int y;
    };

    namespace detail {
        int clamp(int value, int high) {
            if (value > high) {
                return high;
            }
            return value;
        }
    }

    own Vec2 make(int x, int y) {
        return new Vec2 { x: x, y: y };
    }

    own Vec2 scaled(&Vec2 v) {
        return make(detail::clamp(v.x * SCALE, 99), v.y * SCALE);
    }

    void print(&Vec2 v) {
        printf("Vec2(%d, %d)\n", v.x, v.y);
    }
}

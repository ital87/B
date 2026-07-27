import "geometry/vec.b";

enum Level {
    LOW,
    HIGH
};

void report(Level level, Vec2* v) {
    switch (level) {
        case LOW:
            print("low:  ");
            break;
        case HIGH:
            print("high: ");
            break;
    }
    vecPrint(v);
}

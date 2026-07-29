import "geometry/vec.b";

namespace report {

    enum Level {
        LOW,
        HIGH
    };

    void emit(Level level, &geometry::Vec2 v) {
        switch (level) {
            case LOW:
                print("low:  ");
                break;
            case HIGH:
                print("high: ");
                break;
        }
        geometry::print(v);
    }
}

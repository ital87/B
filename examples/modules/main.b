import "report.b";
import "geometry/vec.b";

using namespace geometry;

const int SCALE = 100;

int main() {
    own Vec2 origin = make(2, 5);
    own Vec2 big = geometry::scaled(&origin);

    report::emit(report::LOW, &origin);
    report::emit(report::HIGH, &big);

    printf("global SCALE = %d\n", SCALE);
    printf("geometry     = %d\n", geometry::SCALE);
    printf("clamped      = %d\n", geometry::detail::clamp(500, 42));

    return 0;
}

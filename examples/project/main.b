import "report.b";
import "geometry/vec.b";

int main() {
    Vec2* origin = vecNew(2, 5);
    Vec2* scaled = vecScale(origin);

    report(LOW, origin);
    report(HIGH, scaled);

    free(origin);
    free(scaled);
    return 0;
}

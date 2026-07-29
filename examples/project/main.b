import "report.b";
import "geometry/vec.b";

int main() {
    own Vec2 origin = vecNew(2, 5);
    own Vec2 scaled = vecScale(&origin);

    report(LOW, &origin);
    report(HIGH, &scaled);

    return 0;
}

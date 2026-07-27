enum Color {
    RED,
    GREEN,
    BLUE
};

enum Status {
    ACTIVE = 1,
    INACTIVE = 0,
    PENDING = 2
};

int main() {
    Color c = RED;

    if (c == RED) {
        println("Farbe ist rot!");
    }

    Status s = ACTIVE;
    printf("Status: %d\n", s);

    int status_value = PENDING;
    if (status_value == 2) {
        println("Pending!");
    }

    return 0;
}


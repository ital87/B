int main() {
    println("=== BREAK Example ===");
    for (int i = 0; i < 10; i = i + 1) {
        if (i == 5) {
            break;
        }
        println(i);
    }

    println("");
    println("=== CONTINUE Example ===");

    for (int i = 0; i < 5; i = i + 1) {
        if (i == 2) {
            continue;
        }
        println(i);
    }

    println("");
    println("=== WHILE mit BREAK ===");

    int x = 0;
    while (x < 100) {
        if (x > 5) {
            break;
        }
        println(x);
        x = x + 1;
    }

    println("");
    println("=== WHILE mit CONTINUE ===");

    int y = 0;
    while (y < 5) {
        y = y + 1;
        if (y == 3) {
            continue;
        }
        println(y);
    }

    return 0;
}


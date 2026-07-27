int main() {
    int day = 3;

    switch (day) {
        case 1:
            println("Monday");
            break;
        case 2:
            println("Tuesday");
            break;
        case 3:
            println("Wednesday");
            break;
        case 4:
            println("Thursday");
            break;
        default:
            println("Other day");
            break;
    }

    int x = 5;
    switch (x) {
        case 1:
            println("One");
            break;
        case 5:
            println("Five");
            break;
        default:
            println("Not one or five");
    }

    return 0;
}

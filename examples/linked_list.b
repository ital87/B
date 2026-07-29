struct Node {
    int value;
    own Node? next;
};

drop Node(&mut Node self) {
    printf("releasing %d\n", self.value);
}

void printList(&Node head) {
    printf("%d -> ", head.value);
    if some (rest = head.next) {
        printList(rest);
    } else {
        println("end");
    }
}

int length(&Node head) {
    if some (rest = head.next) {
        return 1 + length(rest);
    }
    return 1;
}

int total(&Node head) {
    int sum = head.value;
    if some (rest = head.next) {
        sum = sum + total(rest);
    }
    return sum;
}

int main() {
    own Node third = new Node { value: 3, next: none };
    own Node second = new Node { value: 2, next: third };
    own Node head = new Node { value: 1, next: second };

    printList(&head);
    printf("length %d, total %d\n", length(&head), total(&head));

    return 0;
}

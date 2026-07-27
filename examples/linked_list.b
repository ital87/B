struct Node {
    int value;
    Node* next;
};

Node* createNode(int value) {
    Node* node = malloc(sizeof(Node));
    node->value = value;
    node->next = 0;
    return node;
}

void printList(Node* head) {
    Node* current = head;
    while (current != 0) {
        printf("%d -> ", current->value);
        current = current->next;
    }
    println("null");
}

void freeList(Node* head) {
    Node* current = head;
    while (current != 0) {
        Node* next = current->next;
        free(current);
        current = next;
    }
}

int main() {
    Node* head = createNode(1);
    head->next = createNode(2);
    head->next->next = createNode(3);

    printList(head);
    printf("third value: %d\n", head->next->next->value);

    freeList(head);
    return 0;
}

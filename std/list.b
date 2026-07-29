namespace list {

pub struct List<T> {
    own [T] items;
    int count;
};

pub own List<T> make<T>() {
    return new List<T> { items: new [T](8), count: 0 };
}

pub own List<T> withCapacity<T>(int capacity) {
    int room = capacity;
    if (room < 1) {
        room = 1;
    }
    return new List<T> { items: new [T](room), count: 0 };
}

pub int size<T>(&List<T> self) {
    return self.count;
}

pub int capacity<T>(&List<T> self) {
    return len(self.items);
}

pub bool isEmpty<T>(&List<T> self) {
    return self.count == 0;
}

pub void reserve<T>(&mut List<T> self, int room) {
    if (room <= len(self.items)) {
        return;
    }
    own [T] bigger = new [T](room);
    for (int i = 0; i < self.count; i = i + 1) {
        bigger[i] = self.items[i];
    }
    self.items = bigger;
}

pub void push<T>(&mut List<T> self, T value) {
    if (self.count == len(self.items)) {
        reserve<T>(self, len(self.items) * 2);
    }
    self.items[self.count] = value;
    self.count = self.count + 1;
}

pub T get<T>(&List<T> self, int index) {
    return self.items[index];
}

pub void set<T>(&mut List<T> self, int index, T value) {
    self.items[index] = value;
}

pub T pop<T>(&mut List<T> self) {
    self.count = self.count - 1;
    return self.items[self.count];
}

pub void clear<T>(&mut List<T> self) {
    self.count = 0;
}

pub bool contains<T>(&List<T> self, T value) {
    for (int i = 0; i < self.count; i = i + 1) {
        if (self.items[i] == value) {
            return true;
        }
    }
    return false;
}

pub int indexOf<T>(&List<T> self, T value) {
    for (int i = 0; i < self.count; i = i + 1) {
        if (self.items[i] == value) {
            return i;
        }
    }
    return 0 - 1;
}

pub void removeAt<T>(&mut List<T> self, int index) {
    for (int i = index; i < self.count - 1; i = i + 1) {
        self.items[i] = self.items[i + 1];
    }
    self.count = self.count - 1;
}

pub void reverse<T>(&mut List<T> self) {
    int left = 0;
    int right = self.count - 1;
    while (left < right) {
        T held = self.items[left];
        self.items[left] = self.items[right];
        self.items[right] = held;
        left = left + 1;
        right = right - 1;
    }
}

pub void sort<T>(&mut List<T> self) {
    for (int i = 1; i < self.count; i = i + 1) {
        T held = self.items[i];
        int j = i - 1;
        while (j >= 0 && self.items[j] > held) {
            self.items[j + 1] = self.items[j];
            j = j - 1;
        }
        self.items[j + 1] = held;
    }
}

}

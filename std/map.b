import "string.b";

namespace map {

pub struct Entry<V> {
    own text::Str? key;
    V value;
    bool taken;
};

pub struct Map<V> {
    own [Entry<V>] slots;
    int count;
};

int hashOf(&text::Str key) {
    int hash = 0 - 2128831035;
    for (int i = 0; i < text::length(key); i = i + 1) {
        hash = hash ^ (int)text::charAt(key, i);
        hash = hash * 16777619;
    }
    if (hash < 0) {
        hash = 0 - hash;
    }
    return hash;
}

pub own Map<V> make<V>() {
    return new Map<V> { slots: new [Entry<V>](16), count: 0 };
}

pub int size<V>(&Map<V> self) {
    return self.count;
}

int slotIn<V>(&[Entry<V>] slots, &text::Str key) {
    int room = len(slots);
    int probe = hashOf(key) % room;
    for (int step = 0; step < room; step = step + 1) {
        int at = (probe + step) % room;
        if (!slots[at].taken) {
            return at;
        }
        if some (existing = slots[at].key) {
            if (text::equals(existing, key)) {
                return at;
            }
        }
    }
    return 0 - 1;
}

void insertInto<V>(&mut [Entry<V>] slots, &text::Str key, V value) {
    int at = slotIn<V>(slots, key);
    if (at < 0) {
        return;
    }
    if (!slots[at].taken) {
        slots[at].key = text::copy(key);
        slots[at].taken = true;
    }
    slots[at].value = value;
}

void rehash<V>(&mut Map<V> self) {
    own [Entry<V>] bigger = new [Entry<V>](len(self.slots) * 2);

    for (int i = 0; i < len(self.slots); i = i + 1) {
        if (self.slots[i].taken) {
            if some (key = self.slots[i].key) {
                insertInto<V>(&mut bigger, key, self.slots[i].value);
            }
        }
    }

    self.slots = bigger;
}

pub void put<V>(&mut Map<V> self, &text::Str key, V value) {
    if ((self.count + 1) * 4 > len(self.slots) * 3) {
        rehash<V>(self);
    }
    int at = slotIn<V>(&self.slots, key);
    if (at < 0) {
        return;
    }
    if (!self.slots[at].taken) {
        self.slots[at].key = text::copy(key);
        self.slots[at].taken = true;
        self.count = self.count + 1;
    }
    self.slots[at].value = value;
}

pub bool has<V>(&Map<V> self, &text::Str key) {
    int room = len(self.slots);
    int probe = hashOf(key) % room;
    for (int step = 0; step < room; step = step + 1) {
        int at = (probe + step) % room;
        if (!self.slots[at].taken) {
            return false;
        }
        if some (existing = self.slots[at].key) {
            if (text::equals(existing, key)) {
                return true;
            }
        }
    }
    return false;
}

pub V getOr<V>(&Map<V> self, &text::Str key, V fallback) {
    int room = len(self.slots);
    int probe = hashOf(key) % room;
    for (int step = 0; step < room; step = step + 1) {
        int at = (probe + step) % room;
        if (!self.slots[at].taken) {
            return fallback;
        }
        if some (existing = self.slots[at].key) {
            if (text::equals(existing, key)) {
                return self.slots[at].value;
            }
        }
    }
    return fallback;
}

pub int capacity<V>(&Map<V> self) {
    return len(self.slots);
}

}

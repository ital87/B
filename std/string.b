namespace text {

pub struct Str {
    own [char] data;
    int length;
};

own Str allocate(int length) {
    return new Str { data: new [char](length + 1), length: length };
}

pub own Str empty() {
    return allocate(0);
}

pub own Str fromLiteral(string source) {
    int size = strlen(source);
    own Str result = allocate(size);
    for (int i = 0; i < size; i = i + 1) {
        result.data[i] = source[i];
    }
    return result;
}

pub own Str allocateOne(char c) {
    own Str result = allocate(1);
    result.data[0] = c;
    return result;
}

pub own Str fromSlice(&[char] source, int start, int count) {
    int take = count;
    if (take < 0) {
        take = 0;
    }
    own Str result = allocate(take);
    for (int i = 0; i < take; i = i + 1) {
        result.data[i] = source[start + i];
    }
    return result;
}

pub own Str copy(&Str source) {
    own Str result = allocate(source.length);
    for (int i = 0; i < source.length; i = i + 1) {
        result.data[i] = source.data[i];
    }
    return result;
}

pub int length(&Str self) {
    return self.length;
}

pub bool isEmpty(&Str self) {
    return self.length == 0;
}

pub char charAt(&Str self, int index) {
    return self.data[index];
}

pub string cstr(&Str self) {
    return (string)self.data;
}

pub bool equals(&Str a, &Str b) {
    if (a.length != b.length) {
        return false;
    }
    for (int i = 0; i < a.length; i = i + 1) {
        if (a.data[i] != b.data[i]) {
            return false;
        }
    }
    return true;
}

pub own Str substring(&Str self, int start, int count) {
    int from = start;
    if (from < 0) {
        from = 0;
    }
    if (from > self.length) {
        from = self.length;
    }
    int take = count;
    if (take < 0) {
        take = 0;
    }
    if (from + take > self.length) {
        take = self.length - from;
    }
    own Str result = allocate(take);
    for (int i = 0; i < take; i = i + 1) {
        result.data[i] = self.data[from + i];
    }
    return result;
}

pub own Str concat(&Str a, &Str b) {
    own Str result = allocate(a.length + b.length);
    for (int i = 0; i < a.length; i = i + 1) {
        result.data[i] = a.data[i];
    }
    for (int i = 0; i < b.length; i = i + 1) {
        result.data[a.length + i] = b.data[i];
    }
    return result;
}

pub int indexOf(&Str haystack, &Str needle) {
    if (needle.length == 0) {
        return 0;
    }
    if (needle.length > haystack.length) {
        return 0 - 1;
    }
    for (int start = 0; start <= haystack.length - needle.length; start = start + 1) {
        bool matched = true;
        for (int i = 0; i < needle.length; i = i + 1) {
            if (haystack.data[start + i] != needle.data[i]) {
                matched = false;
            }
        }
        if (matched) {
            return start;
        }
    }
    return 0 - 1;
}

pub int indexOfChar(&Str self, char wanted) {
    for (int i = 0; i < self.length; i = i + 1) {
        if (self.data[i] == wanted) {
            return i;
        }
    }
    return 0 - 1;
}

pub bool contains(&Str haystack, &Str needle) {
    return indexOf(haystack, needle) >= 0;
}

pub bool startsWith(&Str self, &Str prefix) {
    if (prefix.length > self.length) {
        return false;
    }
    for (int i = 0; i < prefix.length; i = i + 1) {
        if (self.data[i] != prefix.data[i]) {
            return false;
        }
    }
    return true;
}

pub bool endsWith(&Str self, &Str suffix) {
    if (suffix.length > self.length) {
        return false;
    }
    int offset = self.length - suffix.length;
    for (int i = 0; i < suffix.length; i = i + 1) {
        if (self.data[offset + i] != suffix.data[i]) {
            return false;
        }
    }
    return true;
}

pub own Str toUpper(&Str self) {
    own Str result = copy(self);
    for (int i = 0; i < result.length; i = i + 1) {
        char c = result.data[i];
        if (c >= 'a' && c <= 'z') {
            result.data[i] = c - 32;
        }
    }
    return result;
}

pub own Str toLower(&Str self) {
    own Str result = copy(self);
    for (int i = 0; i < result.length; i = i + 1) {
        char c = result.data[i];
        if (c >= 'A' && c <= 'Z') {
            result.data[i] = c + 32;
        }
    }
    return result;
}

bool isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

pub own Str trim(&Str self) {
    int from = 0;
    while (from < self.length && isBlank(self.data[from])) {
        from = from + 1;
    }
    int upto = self.length;
    while (upto > from && isBlank(self.data[upto - 1])) {
        upto = upto - 1;
    }
    return substring(self, from, upto - from);
}

pub own Str replace(&Str self, &Str from, &Str to) {
    if (from.length == 0) {
        return copy(self);
    }

    int hits = 0;
    int scan = 0;
    while (scan <= self.length - from.length) {
        bool matched = true;
        for (int i = 0; i < from.length; i = i + 1) {
            if (self.data[scan + i] != from.data[i]) {
                matched = false;
            }
        }
        if (matched) {
            hits = hits + 1;
            scan = scan + from.length;
        } else {
            scan = scan + 1;
        }
    }

    own Str result = allocate(self.length + hits * (to.length - from.length));
    int read = 0;
    int write = 0;
    while (read < self.length) {
        bool matched = read + from.length <= self.length;
        if (matched) {
            for (int i = 0; i < from.length; i = i + 1) {
                if (self.data[read + i] != from.data[i]) {
                    matched = false;
                }
            }
        }
        if (matched) {
            for (int i = 0; i < to.length; i = i + 1) {
                result.data[write + i] = to.data[i];
            }
            write = write + to.length;
            read = read + from.length;
        } else {
            result.data[write] = self.data[read];
            write = write + 1;
            read = read + 1;
        }
    }
    return result;
}

pub int countChar(&Str self, char wanted) {
    int found = 0;
    for (int i = 0; i < self.length; i = i + 1) {
        if (self.data[i] == wanted) {
            found = found + 1;
        }
    }
    return found;
}

pub own [own Str] split(&Str self, char separator) {
    int pieces = countChar(self, separator) + 1;
    own [own Str] parts = new [own Str](pieces);

    int slot = 0;
    int start = 0;
    for (int i = 0; i <= self.length; i = i + 1) {
        if (i == self.length || self.data[i] == separator) {
            parts[slot] = substring(self, start, i - start);
            slot = slot + 1;
            start = i + 1;
        }
    }
    return parts;
}

pub own Str fromInt(int value) {
    if (value == 0) {
        return fromLiteral("0");
    }

    bool negative = value < 0;
    int remaining = value;
    if (negative) {
        remaining = 0 - remaining;
    }

    int digits = 0;
    int counting = remaining;
    while (counting > 0) {
        digits = digits + 1;
        counting = counting / 10;
    }

    int total = digits;
    if (negative) {
        total = total + 1;
    }

    own Str result = allocate(total);
    int cursor = total - 1;
    while (remaining > 0) {
        result.data[cursor] = (char)(48 + remaining % 10);
        remaining = remaining / 10;
        cursor = cursor - 1;
    }
    if (negative) {
        result.data[0] = '-';
    }
    return result;
}

pub int toInt(&Str self) {
    int result = 0;
    int i = 0;
    bool negative = false;
    if (self.length > 0 && self.data[0] == '-') {
        negative = true;
        i = 1;
    }
    while (i < self.length) {
        char c = self.data[i];
        if (c < '0' || c > '9') {
            i = self.length;
        } else {
            result = result * 10 + ((int)c - 48);
            i = i + 1;
        }
    }
    if (negative) {
        return 0 - result;
    }
    return result;
}

}

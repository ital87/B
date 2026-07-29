import "string.b";

namespace io {

pub const int STDIN = 0;
pub const int STDOUT = 1;
pub const int STDERR = 2;

const int READ_ONLY = 0;
const int WRITE_CREATE_TRUNCATE = 577;
const int DEFAULT_MODE = 420;

pub void write(&text::Str content) {
    b_write(STDOUT, content.data, text::length(content));
}

pub void writeLine(&text::Str content) {
    write(content);
    own text::Str newline = text::fromLiteral("\n");
    write(&newline);
}

pub void writeError(&text::Str content) {
    b_write(STDERR, content.data, text::length(content));
}

pub struct Reader {
    int handle;
    own [char] buffer;
    int filled;
    int cursor;
    bool exhausted;
};

drop Reader(&mut Reader self) {
    if (self.handle > STDERR) {
        b_close(self.handle);
    }
}

pub own Reader fromHandle(int handle) {
    return new Reader {
        handle: handle,
        buffer: new [char](8192),
        filled: 0,
        cursor: 0,
        exhausted: false
    };
}

pub own Reader stdin() {
    return fromHandle(STDIN);
}

pub own Reader? openFile(&text::Str path) {
    int handle = b_open(text::cstr(path), READ_ONLY, 0);
    if (handle < 0) {
        return none;
    }
    return fromHandle(handle);
}

bool refill(&mut Reader self) {
    if (self.exhausted) {
        return false;
    }
    int got = b_read(self.handle, self.buffer, len(self.buffer));
    if (got <= 0) {
        self.exhausted = true;
        return false;
    }
    self.filled = got;
    self.cursor = 0;
    return true;
}

pub own text::Str? readLine(&mut Reader self) {
    own text::Str line = text::empty();
    bool sawAny = false;

    while (true) {
        if (self.cursor >= self.filled) {
            if (!refill(self)) {
                if (sawAny) {
                    return line;
                }
                return none;
            }
        }

        char c = self.buffer[self.cursor];
        self.cursor = self.cursor + 1;
        sawAny = true;

        if (c == '\n') {
            return line;
        }

        own text::Str piece = text::allocateOne(c);
        line = text::concat(&line, &piece);
    }
    return line;
}

pub own text::Str readAll(&mut Reader self) {
    own text::Str content = text::empty();
    while (true) {
        if (self.cursor >= self.filled) {
            if (!refill(self)) {
                return content;
            }
        }
        int available = self.filled - self.cursor;
        own text::Str chunk = text::fromSlice(&self.buffer, self.cursor, available);
        self.cursor = self.filled;
        content = text::concat(&content, &chunk);
    }
    return content;
}

pub bool writeFile(&text::Str path, &text::Str content) {
    int handle = b_open(text::cstr(path), WRITE_CREATE_TRUNCATE, DEFAULT_MODE);
    if (handle < 0) {
        return false;
    }
    b_write(handle, content.data, text::length(content));
    b_close(handle);
    return true;
}

pub own text::Str? readFile(&text::Str path) {
    if some mut (reader = openFile(path)) {
        return readAll(reader);
    }
    return none;
}

}

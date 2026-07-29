import "std/string.b";
import "std/list.b";
import "std/map.b";

enum Kind {
    NUMBER,
    NAME,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    LPAREN,
    RPAREN,
    ASSIGN,
    END
};

struct Token {
    Kind kind;
    int value;
    own text::Str? text;
};

struct Lexer {
    own text::Str source;
    int at;
};

struct Parser {
    own [Token] tokens;
    int at;
    bool failed;
};

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isNameStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isSpace(char c) {
    return c == ' ' || c == '\t';
}

own [Token] tokenize(&text::Str source) {
    own list::List<Kind> kinds = list::make<Kind>();
    own list::List<int> values = list::make<int>();
    own list::List<int> starts = list::make<int>();
    own list::List<int> lengths = list::make<int>();

    int at = 0;
    int size = text::length(source);

    while (at < size) {
        char c = text::charAt(source, at);

        if (isSpace(c)) {
            at = at + 1;
        } else if (isDigit(c)) {
            int number = 0;
            int from = at;
            while (at < size && isDigit(text::charAt(source, at))) {
                number = number * 10 + ((int)text::charAt(source, at) - 48);
                at = at + 1;
            }
            list::push<Kind>(&mut kinds, NUMBER);
            list::push<int>(&mut values, number);
            list::push<int>(&mut starts, from);
            list::push<int>(&mut lengths, at - from);
        } else if (isNameStart(c)) {
            int from = at;
            while (at < size && (isNameStart(text::charAt(source, at)) ||
                                 isDigit(text::charAt(source, at)))) {
                at = at + 1;
            }
            list::push<Kind>(&mut kinds, NAME);
            list::push<int>(&mut values, 0);
            list::push<int>(&mut starts, from);
            list::push<int>(&mut lengths, at - from);
        } else {
            Kind kind = END;
            if (c == '+') { kind = PLUS; }
            if (c == '-') { kind = MINUS; }
            if (c == '*') { kind = STAR; }
            if (c == '/') { kind = SLASH; }
            if (c == '(') { kind = LPAREN; }
            if (c == ')') { kind = RPAREN; }
            if (c == '=') { kind = ASSIGN; }
            list::push<Kind>(&mut kinds, kind);
            list::push<int>(&mut values, 0);
            list::push<int>(&mut starts, at);
            list::push<int>(&mut lengths, 1);
            at = at + 1;
        }
    }

    int count = list::size<Kind>(&kinds);
    own [Token] tokens = new [Token](count + 1);
    for (int i = 0; i < count; i = i + 1) {
        tokens[i].kind = list::get<Kind>(&kinds, i);
        tokens[i].value = list::get<int>(&values, i);
        tokens[i].text = text::substring(source, list::get<int>(&starts, i),
                                         list::get<int>(&lengths, i));
    }
    tokens[count].kind = END;
    tokens[count].value = 0;
    tokens[count].text = none;
    return tokens;
}

Kind peekKind(&Parser self) {
    return self.tokens[self.at].kind;
}

int parsePrimary(&mut Parser self, &mut map::Map<int> names) {
    Kind kind = peekKind(self);

    if (kind == NUMBER) {
        int value = self.tokens[self.at].value;
        self.at = self.at + 1;
        return value;
    }

    if (kind == NAME) {
        int found = 0;
        if some (name = self.tokens[self.at].text) {
            found = map::getOr<int>(names, name, 0);
        }
        self.at = self.at + 1;
        return found;
    }

    if (kind == MINUS) {
        self.at = self.at + 1;
        return 0 - parsePrimary(self, names);
    }

    if (kind == LPAREN) {
        self.at = self.at + 1;
        int inner = parseExpression(self, names);
        if (peekKind(self) == RPAREN) {
            self.at = self.at + 1;
        } else {
            self.failed = true;
        }
        return inner;
    }

    self.failed = true;
    return 0;
}

int parseProduct(&mut Parser self, &mut map::Map<int> names) {
    int total = parsePrimary(self, names);
    while (true) {
        Kind kind = peekKind(self);
        if (kind == STAR) {
            self.at = self.at + 1;
            total = total * parsePrimary(self, names);
        } else if (kind == SLASH) {
            self.at = self.at + 1;
            int divisor = parsePrimary(self, names);
            if (divisor == 0) {
                self.failed = true;
                return 0;
            }
            total = total / divisor;
        } else {
            return total;
        }
    }
    return total;
}

int parseExpression(&mut Parser self, &mut map::Map<int> names) {
    int total = parseProduct(self, names);
    while (true) {
        Kind kind = peekKind(self);
        if (kind == PLUS) {
            self.at = self.at + 1;
            total = total + parseProduct(self, names);
        } else if (kind == MINUS) {
            self.at = self.at + 1;
            total = total - parseProduct(self, names);
        } else {
            return total;
        }
    }
    return total;
}

void runLine(&text::Str line, &mut map::Map<int> names) {
    own text::Str trimmed = text::trim(line);
    if (text::isEmpty(&trimmed)) {
        return;
    }

    own Parser parser = new Parser { tokens: tokenize(&trimmed), at: 0, failed: false };

    bool assignment = false;
    own text::Str target = text::empty();
    if (peekKind(&parser) == NAME && parser.tokens[1].kind == ASSIGN) {
        if some (name = parser.tokens[0].text) {
            target = text::copy(name);
            assignment = true;
        }
        parser.at = 2;
    }

    int result = parseExpression(&mut parser, names);

    if (parser.failed || peekKind(&parser) != END) {
        printf("%s  ->  cannot parse\n", text::cstr(&trimmed));
        return;
    }

    if (assignment) {
        map::put<int>(names, &target, result);
        printf("%s  ->  %s = %d\n", text::cstr(&trimmed), text::cstr(&target), result);
    } else {
        printf("%s  ->  %d\n", text::cstr(&trimmed), result);
    }
}

int main() {
    own map::Map<int> names = map::make<int>();

    own text::Str program = text::fromLiteral("width = 8\nheight = 3 + 2\narea = width * height\narea + 10\n(width + height) * 2\narea / (height - 5)\n-width + area\nunknown * 2\n3 +\n");

    own [own text::Str] lines = text::split(&program, '\n');
    for (int i = 0; i < len(lines); i = i + 1) {
        runLine(lines[i], &mut names);
    }

    own text::Str areaKey = text::fromLiteral("area");
    printf("\nstored %d names, area = %d\n",
           map::size<int>(&names), map::getOr<int>(&names, &areaKey, 0 - 1));
    return 0;
}

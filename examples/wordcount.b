import "std/io.b";
import "std/map.b";
import "std/list.b";

own text::Str normalize(&text::Str word) {
    own text::Str lowered = text::toLower(word);
    own text::Str result = text::empty();
    for (int i = 0; i < text::length(&lowered); i = i + 1) {
        char c = text::charAt(&lowered, i);
        if (c >= 'a' && c <= 'z') {
            own text::Str piece = text::allocateOne(c);
            result = text::concat(&result, &piece);
        }
    }
    return result;
}

int main() {
    own text::Str sample = text::fromLiteral(
        "the quick brown fox jumps over the lazy dog\nthe dog barks and the fox runs\n");

    own map::Map<int> counts = map::make<int>();
    own list::List<int> lengths = list::make<int>();

    own [own text::Str] lines = text::split(&sample, '\n');
    int totalWords = 0;

    for (int l = 0; l < len(lines); l = l + 1) {
        own [own text::Str] words = text::split(lines[l], ' ');
        for (int w = 0; w < len(words); w = w + 1) {
            own text::Str word = normalize(words[w]);
            if (text::length(&word) > 0) {
                totalWords = totalWords + 1;
                int seen = map::getOr<int>(&counts, &word, 0);
                map::put<int>(&mut counts, &word, seen + 1);
                list::push<int>(&mut lengths, text::length(&word));
            }
        }
    }

    printf("words: %d, distinct: %d\n", totalWords, map::size<int>(&counts));

    own text::Str the = text::fromLiteral("the");
    own text::Str fox = text::fromLiteral("fox");
    own text::Str cat = text::fromLiteral("cat");
    printf("the=%d fox=%d cat=%d\n",
           map::getOr<int>(&counts, &the, 0),
           map::getOr<int>(&counts, &fox, 0),
           map::getOr<int>(&counts, &cat, 0));

    list::sort<int>(&mut lengths);
    printf("shortest=%d longest=%d\n",
           list::get<int>(&lengths, 0),
           list::get<int>(&lengths, list::size<int>(&lengths) - 1));

    own text::Str report = text::fromLiteral("word count finished\n");
    io::write(&report);
    return 0;
}

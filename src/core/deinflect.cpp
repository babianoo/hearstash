#include "core/deinflect.h"

#include <algorithm>
#include <array>
#include <string>

// A rule takes an ending off a word and puts another in its place, e.g.
// かった -> い, which undoes an い adjective's past. It says two more
// things, which is what lets rules follow one another without every
// combination being written out:
//
//   - what the word is with the ending on (is): 食べない inflects like an
//     い adjective, 食べられる like an ichidan verb, 食べて is a て form
//   - what the word has to be with the ending off (needs): かった can
//     only come off an い adjective
//
// A rule can be tried on a word if what the word has to be is something
// the rule's ending makes. So 食べなかった goes
//   かった -> い   leaving 食べない, which has to be an い adjective
//   ない -> る     which makes one, leaving 食べる, an ichidan verb
// and 食べる is then looked up as an ichidan verb.

// Besides WordKind: what a rule's ending makes that isn't a kind of
// dictionary word, so only a rule can follow it
static constexpr unsigned TE = 64;      // A て form: 食べて, 読んで
static constexpr unsigned ANY = ~0u;    // The text as written: anything
// The end of the line: nothing inflects further, like a past
static constexpr unsigned DONE = 128;

struct Rule {
    std::string from, to;
    unsigned is, needs;
    const char* name;
    // Whether from can be the whole word. Not for an ending, た isn't a
    // verb, but the irregular words' rules have the word in them: した is
    // する.
    bool whole;
};

// The godan verbs' nine endings, and what each becomes in the other rows
// of the kana table, which is how they inflect
static constexpr std::array<const char*, 9> U = {"う", "く", "ぐ", "す", "つ", "ぬ", "ぶ", "む", "る"};
static constexpr std::array<const char*, 9> A = {"わ", "か", "が", "さ", "た", "な", "ば", "ま", "ら"};
static constexpr std::array<const char*, 9> I = {"い", "き", "ぎ", "し", "ち", "に", "び", "み", "り"};
static constexpr std::array<const char*, 9> E = {"え", "け", "げ", "せ", "て", "ね", "べ", "め", "れ"};
static constexpr std::array<const char*, 9> O = {"お", "こ", "ご", "そ", "と", "の", "ぼ", "も", "ろ"};
// Their て and た forms, which are irregular in their own regular way
static constexpr std::array<const char*, 9> TE_FORM = {"って", "いて", "いで", "して", "って", "んで", "んで", "んで", "って"};
static constexpr std::array<const char*, 9> TA_FORM = {"った", "いた", "いだ", "した", "った", "んだ", "んだ", "んだ", "った"};

// An ending that goes on a verb's stem, and what the verb is with it on
struct Ending {
    const char* text;
    unsigned is;
    const char* name;
};

// On the ます stem: 食べ-, 書き-, し-, 来-
static constexpr Ending STEM_ENDINGS[] = {
    {"ます", DONE, "polite"}, {"ました", DONE, "polite past"}, {"ません", DONE, "polite negative"},
    {"ませんでした", DONE, "polite past negative"}, {"ましょう", DONE, "polite volitional"},
    {"まして", DONE, "polite て"}, {"たい", KIND_ADJ_I, "want to"}, {"たがる", KIND_V5, "wants to"},
    {"なさい", DONE, "command"}, {"な", DONE, "command"}, {"ながら", DONE, "while"},
    {"すぎる", KIND_V1, "too much"}, {"そう", DONE, "looks like"}, {"やすい", KIND_ADJ_I, "easy to"},
    {"にくい", KIND_ADJ_I, "hard to"}, {"かた", DONE, "way of"}, {"方", DONE, "way of"},
    {"", DONE, "stem"},
};

// On the ない stem: 食べ-, 書か-, し-, 来-
static constexpr Ending NEGATIVE_ENDINGS[] = {
    {"ない", KIND_ADJ_I, "negative"}, {"ず", DONE, "negative"}, {"ずに", DONE, "without"},
    {"ぬ", DONE, "negative"}, {"ん", DONE, "negative"}, {"なきゃ", DONE, "must"}, {"なくちゃ", DONE, "must"},
};

// After a て form, and instead of its て or で where short says so:
// 食べている, 食べてる, 食べちゃう
struct TeEnding {
    const char* after_te;
    const char* after_de;
    // Whether it comes after the て (食べて + いる) or replaces it (食べ +
    // ちゃう)
    bool replaces;
    unsigned is;
    const char* name;
};
static constexpr TeEnding TE_ENDINGS[] = {
    {"いる", "いる", false, KIND_V1, "progressive"}, {"る", "る", false, KIND_V1, "progressive"},
    {"おる", "おる", false, KIND_V5, "progressive"},
    {"おく", "おく", false, KIND_V5, "in advance"}, {"とく", "どく", true, KIND_V5, "in advance"},
    {"しまう", "しまう", false, KIND_V5, "completely"}, {"ちゃう", "じゃう", true, KIND_V5, "completely"},
    {"ちまう", "じまう", true, KIND_V5, "completely"},
    {"ある", "ある", false, KIND_V5, "has been"}, {"いく", "いく", false, KIND_V5, "going on"},
    {"く", "く", false, KIND_V5, "going on"}, {"くる", "くる", false, KIND_VK, "coming to"},
    {"みる", "みる", false, KIND_V1, "try"}, {"ほしい", "ほしい", false, KIND_ADJ_I, "want someone to"},
    {"くれる", "くれる", false, KIND_V1, "for me"}, {"もらう", "もらう", false, KIND_V5, "have someone"},
    {"あげる", "あげる", false, KIND_V1, "for someone"}, {"ください", "ください", false, DONE, "please"},
    {"ちゃ", "じゃ", true, DONE, "ては"}, {"は", "は", false, DONE, "ては"}, {"も", "も", false, DONE, "ても"},
    {"から", "から", false, DONE, "after"},
};

// What comes on a past form in place of its た or だ: 食べたら, 読んだり
static constexpr Ending PAST_ENDINGS[] = {
    {"", DONE, "past"}, {"ら", DONE, "if"}, {"り", DONE, "and so on"}, {"ろう", DONE, "probably"},
};

static std::vector<Rule> make_rules(){
    std::vector<Rule> rules;
    // Set for the rules that have their word in them
    bool whole = false;
    const auto add = [&](std::string from, std::string to, unsigned is, unsigned needs, const char* name){
        rules.push_back({std::move(from), std::move(to), is, needs, name, whole});
    };
    using namespace std::string_literals;

    // ---- Ichidan verbs: everything goes on the stem, 食べ- ----
    for (const Ending& e : STEM_ENDINGS) add(e.text, "る", e.is, KIND_V1, e.name);
    for (const Ending& e : NEGATIVE_ENDINGS) add(e.text, "る", e.is, KIND_V1, e.name);
    add("て", "る", TE, KIND_V1, "て");
    for (const Ending& e : PAST_ENDINGS) add("た"s + e.text, "る", e.is, KIND_V1, e.name);
    add("られる", "る", KIND_V1, KIND_V1, "passive or potential");
    add("れる", "る", KIND_V1, KIND_V1, "potential");
    add("させる", "る", KIND_V1, KIND_V1, "causative");
    add("さす", "る", KIND_V5, KIND_V1, "causative");
    add("させられる", "る", KIND_V1, KIND_V1, "causative passive");
    add("よう", "る", DONE, KIND_V1, "volitional");
    add("れば", "る", DONE, KIND_V1, "if");
    add("りゃ", "る", DONE, KIND_V1, "if");
    add("ろ", "る", DONE, KIND_V1, "command");
    add("よ", "る", DONE, KIND_V1, "command");
    add("るな", "る", DONE, KIND_V1, "don't");

    // ---- Godan verbs: the ending moves along its row ----
    for (size_t k = 0; k < U.size(); ++k){
        for (const Ending& e : STEM_ENDINGS) add(I[k] + std::string(e.text), U[k], e.is, KIND_V5, e.name);
        for (const Ending& e : NEGATIVE_ENDINGS) add(A[k] + std::string(e.text), U[k], e.is, KIND_V5, e.name);
        add(TE_FORM[k], U[k], TE, KIND_V5, "て");
        for (const Ending& e : PAST_ENDINGS) add(TA_FORM[k] + std::string(e.text), U[k], e.is, KIND_V5, e.name);
        add(A[k] + "れる"s, U[k], KIND_V1, KIND_V5, "passive");
        add(A[k] + "せる"s, U[k], KIND_V1, KIND_V5, "causative");
        add(A[k] + "す"s, U[k], KIND_V5, KIND_V5, "causative");
        add(A[k] + "される"s, U[k], KIND_V1, KIND_V5, "causative passive");
        add(A[k] + "せられる"s, U[k], KIND_V1, KIND_V5, "causative passive");
        add(E[k] + "る"s, U[k], KIND_V1, KIND_V5, "potential");
        add(O[k] + "う"s, U[k], DONE, KIND_V5, "volitional");
        add(E[k] + "ば"s, U[k], DONE, KIND_V5, "if");
        add(E[k], U[k], DONE, KIND_V5, "command");
        add(U[k] + "な"s, U[k], DONE, KIND_V5, "don't");
    }
    // 行く, alone among the く verbs: 行って, not 行いて
    add("って", "く", TE, KIND_V5, "て");
    for (const Ending& e : PAST_ENDINGS) add("った"s + e.text, "く", e.is, KIND_V5, e.name);
    // いらっしゃる and its like have い for り in the polite forms
    whole = true;
    add("いらっしゃい", "いらっしゃる", DONE, KIND_V5, "stem");
    for (const char* honorific : {"いらっしゃ", "おっしゃ", "くださ", "なさ", "ござ"})
        for (const char* polite : {"います", "いました", "いません", "い"})
            add(honorific + std::string(polite), honorific + "る"s, DONE, KIND_V5, "polite");

    // ---- する, and what ends in it: 勉強する ----
    for (const Ending& e : STEM_ENDINGS) add("し"s + e.text, "する", e.is, KIND_VS, e.name);
    for (const Ending& e : NEGATIVE_ENDINGS){
        // せず and せぬ, not しず and しぬ
        const bool classical = e.text == "ず"s || e.text == "ずに"s || e.text == "ぬ"s;
        add((classical ? "せ"s : "し"s) + e.text, "する", e.is, KIND_VS, e.name);
    }
    add("して", "する", TE, KIND_VS, "て");
    for (const Ending& e : PAST_ENDINGS) add("した"s + e.text, "する", e.is, KIND_VS, e.name);
    add("される", "する", KIND_V1, KIND_VS, "passive");
    add("させる", "する", KIND_V1, KIND_VS, "causative");
    add("させられる", "する", KIND_V1, KIND_VS, "causative passive");
    add("できる", "する", KIND_V1, KIND_VS, "potential");
    add("出来る", "する", KIND_V1, KIND_VS, "potential");
    add("しよう", "する", DONE, KIND_VS, "volitional");
    add("すれば", "する", DONE, KIND_VS, "if");
    add("しろ", "する", DONE, KIND_VS, "command");
    add("せよ", "する", DONE, KIND_VS, "command");
    add("するな", "する", DONE, KIND_VS, "don't");

    // ---- ずる verbs: 論ずる, which also go like 論じる ----
    for (const Ending& e : STEM_ENDINGS) add("じ"s + e.text, "ずる", e.is, KIND_VZ, e.name);
    add("じない", "ずる", KIND_ADJ_I, KIND_VZ, "negative");
    add("じて", "ずる", TE, KIND_VZ, "て");
    for (const Ending& e : PAST_ENDINGS) add("じた"s + e.text, "ずる", e.is, KIND_VZ, e.name);
    add("じられる", "ずる", KIND_V1, KIND_VZ, "passive");
    add("じさせる", "ずる", KIND_V1, KIND_VZ, "causative");
    add("じよう", "ずる", DONE, KIND_VZ, "volitional");
    add("ずれば", "ずる", DONE, KIND_VZ, "if");
    add("じろ", "ずる", DONE, KIND_VZ, "command");

    // ---- 来る, written with its kanji or in kana ----
    // Its stem changes sound, which only shows in kana: き, こ, く
    struct Kuru { const char* masu; const char* nai; const char* base; const char* word; };
    for (const Kuru& k : {Kuru{"来", "来", "来", "来る"}, Kuru{"き", "こ", "く", "くる"}}){
        for (const Ending& e : STEM_ENDINGS) add(k.masu + std::string(e.text), k.word, e.is, KIND_VK, e.name);
        for (const Ending& e : NEGATIVE_ENDINGS) add(k.nai + std::string(e.text), k.word, e.is, KIND_VK, e.name);
        add(k.masu + "て"s, k.word, TE, KIND_VK, "て");
        for (const Ending& e : PAST_ENDINGS) add(k.masu + "た"s + e.text, k.word, e.is, KIND_VK, e.name);
        add(k.nai + "られる"s, k.word, KIND_V1, KIND_VK, "passive or potential");
        add(k.nai + "れる"s, k.word, KIND_V1, KIND_VK, "potential");
        add(k.nai + "させる"s, k.word, KIND_V1, KIND_VK, "causative");
        add(k.nai + "よう"s, k.word, DONE, KIND_VK, "volitional");
        add(k.base + "れば"s, k.word, DONE, KIND_VK, "if");
        add(k.nai + "い"s, k.word, DONE, KIND_VK, "command");
        add(k.base + "るな"s, k.word, DONE, KIND_VK, "don't");
    }

    whole = false;

    // ---- What follows a て form ----
    for (const TeEnding& e : TE_ENDINGS){
        if (e.replaces){
            add(e.after_te, "て", e.is, TE, e.name);
            add(e.after_de, "で", e.is, TE, e.name);
        } else {
            add("て"s + e.after_te, "て", e.is, TE, e.name);
            add("で"s + e.after_de, "で", e.is, TE, e.name);
        }
    }

    // ---- い adjectives, and what inflects like one: 食べない, 食べたい ----
    add("く", "い", DONE, KIND_ADJ_I, "adverb");
    add("くて", "い", DONE, KIND_ADJ_I, "て");
    add("くない", "い", KIND_ADJ_I, KIND_ADJ_I, "negative");
    add("くありません", "い", DONE, KIND_ADJ_I, "polite negative");
    for (const Ending& e : PAST_ENDINGS) add("かった"s + e.text, "い", e.is, KIND_ADJ_I, e.name);
    add("ければ", "い", DONE, KIND_ADJ_I, "if");
    add("けりゃ", "い", DONE, KIND_ADJ_I, "if");
    add("きゃ", "い", DONE, KIND_ADJ_I, "if");
    add("さ", "い", DONE, KIND_ADJ_I, "-ness");
    add("そう", "い", DONE, KIND_ADJ_I, "looks");
    add("すぎる", "い", KIND_V1, KIND_ADJ_I, "too");
    add("くなる", "い", KIND_V5, KIND_ADJ_I, "become");
    add("くする", "い", KIND_VS, KIND_ADJ_I, "make");
    add("かろう", "い", DONE, KIND_ADJ_I, "probably");
    add("き", "い", DONE, KIND_ADJ_I, "classical");
    add("え", "い", DONE, KIND_ADJ_I, "slurred");    // すげえ for すごい isn't this regular, but 高え is
    // いい inflects as よい
    whole = true;
    add("よく", "いい", DONE, KIND_ADJ_I, "adverb");
    add("よくない", "いい", KIND_ADJ_I, KIND_ADJ_I, "negative");
    add("よかった", "いい", DONE, KIND_ADJ_I, "past");
    add("よければ", "いい", DONE, KIND_ADJ_I, "if");
    add("よくて", "いい", DONE, KIND_ADJ_I, "て");

    return rules;
}

unsigned word_kinds(std::string_view rules){
    static constexpr std::pair<std::string_view, unsigned> NAMES[] = {
        {"v1", KIND_V1}, {"v5", KIND_V5}, {"vs", KIND_VS}, {"vk", KIND_VK}, {"vz", KIND_VZ}, {"adj-i", KIND_ADJ_I},
    };
    unsigned kinds = 0;
    size_t at = 0;
    while (at <= rules.size()){
        size_t end = rules.find(' ', at);
        if (end == std::string_view::npos) end = rules.size();
        const std::string_view rule = rules.substr(at, end - at);
        for (const auto& [name, kind] : NAMES)
            if (rule == name) kinds |= kind;
        at = end + 1;
    }
    return kinds;
}

std::vector<Deinflection> deinflect(std::string_view text){
    // Made once, the first time it's needed
    static const std::vector<Rule> rules = make_rules();
    // More endings than anyone says in a row, and it keeps a long text
    // from being taken apart in thousands of ways
    static constexpr size_t MAX_STEPS = 6, MAX_CANDIDATES = 400;
    static constexpr unsigned WORD_KINDS = KIND_V1 | KIND_V5 | KIND_VS | KIND_VK | KIND_VZ | KIND_ADJ_I;

    // What each candidate has to be, which the caller only gets the
    // dictionary kinds of
    std::vector<unsigned> needs{ANY};
    std::vector<Deinflection> found{{std::string(text), 0, {}}};

    // In the order found, so the ones with fewer steps come first
    for (size_t at = 0; at < found.size() && found.size() < MAX_CANDIDATES; ++at){
        if (found[at].steps.size() >= MAX_STEPS) continue;
        // By value: found and needs grow below, which moves their elements
        const std::string word = found[at].word;
        const unsigned need = needs[at];
        for (const Rule& rule : rules){
            if (!(need & rule.is) || word.size() < rule.from.size() || !word.ends_with(rule.from)) continue;
            // Nothing but an ending isn't a word with one
            if (word.size() == rule.from.size() && !rule.whole) continue;

            std::string base = word.substr(0, word.size() - rule.from.size()) + rule.to;
            const bool seen = std::ranges::any_of(found, [&](const Deinflection& d){
                return d.word == base && d.kinds == (rule.needs & WORD_KINDS);
            });
            if (seen) continue;

            std::vector<std::string> steps = found[at].steps;
            steps.push_back(rule.name);
            needs.push_back(rule.needs);
            found.push_back({std::move(base), rule.needs & WORD_KINDS, std::move(steps)});
            if (found.size() >= MAX_CANDIDATES) break;
        }
    }
    return found;
}

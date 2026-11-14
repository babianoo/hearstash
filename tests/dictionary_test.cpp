// Tests for ZipArchive, import_dictionary(), Dictionary, look_up_in(),
// dictionary_age_days() and the frequency list, on small made-up
// dictionaries in Yomitan's format. No framework, like sample_ring_test.cpp.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// JSON
#include <nlohmann/json.hpp>
// zlib
#include <zlib.h>

#include "core/dictionary.h"
#include "core/dictionary_files.h"
#include "core/dictionary_import.h"
#include "core/frequency_list.h"
#include "core/word_lookup.h"
#include "core/zip_archive.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

// Whether statement throws a std::runtime_error
#define THROWS(statement) [&]{ try { statement; } catch (const std::runtime_error&){ return true; } return false; }()

// ---- A zip to read ----

static void put(std::string& to, std::uint32_t n, int bytes){
    for (int i = 0; i < bytes; ++i) to += static_cast<char>(n >> (8 * i) & 0xFF);
}

// A zip of files, each deflated if it's given as true and stored as it is
// otherwise
static std::string make_zip(const std::vector<std::tuple<std::string, std::string, bool>>& files){
    std::string zip, list;
    for (const auto& [name, contents, deflate] : files){
        std::string stored = contents;
        if (deflate){
            z_stream stream{};
            deflateInit2(&stream, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
            stored.assign(deflateBound(&stream, static_cast<uLong>(contents.size())), '\0');
            stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(contents.data()));
            stream.avail_in = static_cast<uInt>(contents.size());
            stream.next_out = reinterpret_cast<Bytef*>(stored.data());
            stream.avail_out = static_cast<uInt>(stored.size());
            ::deflate(&stream, Z_FINISH);
            stored.resize(stream.total_out);
            deflateEnd(&stream);
        }
        const auto crc = static_cast<std::uint32_t>(
            crc32(0, reinterpret_cast<const Bytef*>(contents.data()), static_cast<uInt>(contents.size())));
        const auto offset = static_cast<std::uint32_t>(zip.size());

        // What a file's header and its line in the list share
        std::string common;
        put(common, 20, 2);     // Version needed
        put(common, 0, 2);      // Flags
        put(common, deflate ? 8 : 0, 2);
        put(common, 0, 4);      // Time and date
        put(common, crc, 4);
        put(common, static_cast<std::uint32_t>(stored.size()), 4);
        put(common, static_cast<std::uint32_t>(contents.size()), 4);
        put(common, static_cast<std::uint32_t>(name.size()), 2);
        put(common, 0, 2);      // Extra field

        put(zip, 0x04034b50, 4);
        zip += common + name + stored;

        put(list, 0x02014b50, 4);
        put(list, 20, 2);       // Version made by
        list += common;
        put(list, 0, 2);        // Comment
        put(list, 0, 2);        // Disk
        put(list, 0, 2);        // Attributes
        put(list, 0, 4);
        put(list, offset, 4);
        list += name;
    }
    const auto list_offset = static_cast<std::uint32_t>(zip.size());
    zip += list;
    put(zip, 0x06054b50, 4);
    put(zip, 0, 4);             // Disks
    put(zip, static_cast<std::uint32_t>(files.size()), 2);
    put(zip, static_cast<std::uint32_t>(files.size()), 2);
    put(zip, static_cast<std::uint32_t>(list.size()), 4);
    put(zip, list_offset, 4);
    put(zip, 0, 2);             // Comment
    return zip;
}

static void write_file(const fs::path& file, const std::string& bytes){
    std::ofstream(file, std::ios::binary) << bytes;
}

static std::string read_file(const fs::path& file){
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// ---- The dictionary ----

// An entry laid out the way Jitendex lays one out
static json entry(const std::vector<std::string>& parts_of_speech, const std::vector<std::vector<std::string>>& senses){
    json group = json::array();
    for (const std::string& part : parts_of_speech)
        group.push_back({{"tag", "span"}, {"data", {{"content", "part-of-speech-info"}, {"class", "tag"}}}, {"content", part}});
    json list = json::array();
    for (const auto& glosses : senses){
        json items = json::array();
        for (const std::string& gloss : glosses) items.push_back({{"tag", "li"}, {"content", gloss}});
        list.push_back({{"tag", "li"}, {"data", {{"content", "sense"}}}, {"content", json::array({
            {{"tag", "ul"}, {"data", {{"content", "glossary"}}}, {"content", items}},
            // Not a gloss: an example, with a reading over its kanji
            {{"tag", "div"}, {"data", {{"content", "extra-info"}}}, {"content", json::array({
                {{"tag", "ruby"}, {"content", json::array({"例", {{"tag", "rt"}, {"content", "れい"}}})}},
                {{"tag", "a"}, {"href", "https://example.com/1"}, {"content", "source"}},
            })}},
        })}});
    }
    group.push_back({{"tag", "ol"}, {"content", list}});
    return json::array({{{"type", "structured-content"}, {"content", {
        {"tag", "ul"}, {"data", {{"content", "sense-groups"}}},
        {"content", {{"tag", "li"}, {"data", {{"content", "sense-group"}}}, {"content", group}}},
    }}}});
}

static json row(const char* term, const char* reading, const char* tags, const char* rules, int score,
                const json& definitions, int sequence){
    return json::array({term, reading, tags, rules, score, definitions, sequence, ""});
}

static fs::path make_dictionary_zip(const fs::path& folder){
    const json kami = entry({"noun"}, {{"paper"}, {"sheet", "leaf"}});
    json first = json::array({
        row("紙", "かみ", "★", "", 200, kami, 1),
        row("髪", "かみ", "★", "", 190, entry({"noun"}, {{"hair"}}), 2),
        row("神", "かみ", "", "", 150, entry({"noun"}, {{"god"}}), 3),
        row("食べる", "たべる", "★", "v1", 200, entry({"1-dan", "transitive"}, {{"to eat"}}), 4),
        // Two spellings of one word, the second the rarer
        row("悩む", "なやむ", "★", "v5", 180, entry({"5-dan"}, {{"to be worried", "to be troubled"}}), 5),
        row("惱む", "", "old kanji form", "", -100,
            json::array({{{"type", "structured-content"}, {"content", "see 悩む"}}, json::array({"悩む", json::array({"redirected"})})}), -5),
        row("高い", "たかい", "★", "adj-i", 200, entry({"adjective"}, {{"high", "tall"}, {"expensive"}}), 6),
        // In kana only
        row("イチジク", "", "", "", 5, entry({"noun"}, {{"fig"}}), 7),
        row("無花果", "いちじく", "", "", 4, entry({"noun"}, {{"fig"}}), 7),
        // Not a verb, though it ends like the past of one
        row("した", "", "", "", 10, entry({"noun"}, {{"under"}}), 8),
        row("する", "", "★", "vs", 200, entry({"suru"}, {{"to do"}}), 9),
    });
    // Enough rows, each with definitions of its own, to fill several
    // blocks, and a last one after them
    json second = json::array();
    for (int i = 0; i < 400; ++i){
        const std::string filler(500, static_cast<char>('a' + i % 26));
        second.push_back(row(std::format("語{}", i).c_str(), std::format("ご{}", i).c_str(), "", "", 0,
                             json::array({std::format("word {} {}", i, filler)}), 100 + i));
    }
    second.push_back(row("筆", "ふで", "", "", 100, json::array({"brush", "pen"}), 600));

    const json index = {{"title", "Test [2026-10-03]"}, {"revision", "2026.10.03.0"}, {"format", 3},
                        {"author", "Nobody"}, {"url", "https://example.com"}, {"attribution", "Made up"},
                        {"isUpdatable", true}, {"indexUrl", "https://example.com/index.json"},
                        {"downloadUrl", "https://example.com/dictionary.zip"}};
    const fs::path zip = folder / "dictionary.zip";
    write_file(zip, make_zip({
        // Out of order, and one bank a number that sorts wrongly as text
        {"term_bank_10.json", second.dump(), true},
        {"index.json", index.dump(), false},
        {"styles.css", "li { color: red; }", true},
        {"term_bank_2.json", first.dump(), true},
        {"pictures/a.png", "not a picture", false},
    }));
    return zip;
}

static void test_zip(const fs::path& folder){
    const std::string text(5000, 'x');
    const fs::path file = folder / "a.zip";
    write_file(file, make_zip({{"stored.txt", "as it is", false}, {"folder/deflated.txt", text, true}, {"empty", "", false}}));

    const ZipArchive zip(file);
    CHECK(zip.entries().size() == 3);
    CHECK(zip.entries()[1].name == "folder/deflated.txt" && zip.entries()[1].size == 5000);
    CHECK(zip.find("stored.txt") && zip.read(*zip.find("stored.txt")) == "as it is");
    CHECK(zip.read(*zip.find("folder/deflated.txt")) == text);
    CHECK(zip.read(*zip.find("empty")).empty());
    CHECK(zip.find("missing") == nullptr);

    // A byte of a file changed: its checksum says so
    std::string bytes = read_file(file);
    bytes[bytes.find("as it is")] = 'A';
    write_file(folder / "damaged.zip", bytes);
    const ZipArchive damaged(folder / "damaged.zip");
    CHECK(THROWS(damaged.read(*damaged.find("stored.txt"))));
    CHECK(damaged.read(*damaged.find("folder/deflated.txt")) == text);

    // Cut off, as a download that stopped is; or not a zip at all
    write_file(folder / "cut.zip", bytes.substr(0, bytes.size() / 2));
    CHECK(THROWS(ZipArchive(folder / "cut.zip")));
    write_file(folder / "text.zip", "hello");
    CHECK(THROWS(ZipArchive(folder / "text.zip")));
    CHECK(THROWS(ZipArchive(folder / "none.zip")));
}

static void test_import(const fs::path& folder){
    const fs::path zip = make_dictionary_zip(folder);
    const fs::path file = folder / "made" / "test.hsdict";
    size_t calls = 0, last_done = 0, last_total = 0;
    const DictionaryInfo info = import_dictionary(zip, file, [&](size_t done, size_t total){
        ++calls;
        last_done = done;
        last_total = total;
    });
    CHECK(info.title == "Test [2026-10-03]" && info.revision == "2026.10.03.0");
    CHECK(calls == 3 && last_done == 2 && last_total == 2);
    CHECK(fs::exists(file) && !fs::exists(folder / "made" / "test.hsdict.part"));
    // 400 rows of 500 letters each, compressed
    CHECK(fs::file_size(file) < 60000);

    const Dictionary dictionary(file);
    CHECK(dictionary.info().title == info.title && dictionary.info().revision == info.revision);
    CHECK(dictionary.info().author == "Nobody" && dictionary.info().attribution == "Made up");
    CHECK(dictionary.info().index_url == "https://example.com/index.json");
    CHECK(dictionary.info().download_url == "https://example.com/dictionary.zip");
    CHECK(dictionary.info().styles == "li { color: red; }");
    CHECK(dictionary.info().converted > 1700000000);
    CHECK(dictionary.row_count() == 11 + 401);

    // By how it's written, and by how it's read; in the dictionary's
    // order, bank 2 before bank 10
    const auto kami = dictionary.find("紙");
    CHECK(kami.size() == 1 && kami[0].term == "紙" && kami[0].reading == "かみ" && kami[0].tags == "★");
    CHECK(kami.size() == 1 && kami[0].score == 200 && kami[0].sequence == 1 && kami[0].index == 0);
    const auto by_reading = dictionary.find("かみ");
    CHECK(by_reading.size() == 3 && by_reading[0].term == "紙" && by_reading[1].term == "髪" && by_reading[2].term == "神");
    CHECK(dictionary.find("食べる").size() == 1 && dictionary.find("食べる")[0].rules == "v1");
    CHECK(dictionary.find("イチジク").size() == 1 && dictionary.find("イチジク")[0].reading.empty());
    CHECK(dictionary.find("かみさま").empty() && dictionary.find("").empty() && dictionary.find("か").empty());

    // The definitions come back as they went in, from the first block and
    // from the last
    const json paper = dictionary.definitions(kami[0]);
    CHECK(paper == entry({"noun"}, {{"paper"}, {"sheet", "leaf"}}));
    const auto fude = dictionary.find("筆");
    CHECK(fude.size() == 1 && dictionary.definitions(fude[0]) == json::array({"brush", "pen"}));
    for (int i : {0, 199, 399}){
        const auto word = dictionary.find(std::format("ご{}", i));
        CHECK(word.size() == 1 && dictionary.definitions(word[0])[0].get<std::string>().starts_with(std::format("word {} ", i)));
    }

    // A spelling the word is only listed under another for
    const auto old = dictionary.find("惱む");
    CHECK(old.size() == 1 && old[0].redirect == "悩む" && old[0].sequence == -5);
    CHECK(old.size() == 1 && dictionary.definitions(old[0]).empty());

    // Replaces a file that's there, and leaves it alone when it fails
    import_dictionary(zip, file);
    write_file(folder / "empty.zip", make_zip({{"index.json", R"({"title": "T", "revision": "1", "format": 3})", false}}));
    CHECK(THROWS(import_dictionary(folder / "empty.zip", file)));
    write_file(folder / "old.zip", make_zip({{"index.json", R"({"title": "T", "revision": "1", "format": 1})", false},
                                             {"term_bank_1.json", "[]", false}}));
    CHECK(THROWS(import_dictionary(folder / "old.zip", file)));
    write_file(folder / "bad.zip", make_zip({{"index.json", R"({"title": "T", "revision": "1", "format": 3})", false},
                                             {"term_bank_1.json", R"([["紙", "かみ"]])", false}}));
    CHECK(THROWS(import_dictionary(folder / "bad.zip", file)));
    CHECK(!fs::exists(folder / "made" / "test.hsdict.part"));
    CHECK(Dictionary(file).row_count() == 412);

    // Stopped before it starts
    std::stop_source stop;
    stop.request_stop();
    CHECK(THROWS(import_dictionary(zip, folder / "stopped.hsdict", {}, stop.get_token())));
    CHECK(!fs::exists(folder / "stopped.hsdict"));

    // Not a dictionary file, or half of one
    CHECK(THROWS(Dictionary{zip}));
    const std::string bytes = read_file(file);
    write_file(folder / "half.hsdict", bytes.substr(0, bytes.size() / 2));
    CHECK(THROWS(Dictionary(folder / "half.hsdict")));
    CHECK(THROWS(Dictionary(folder / "none.hsdict")));
}

static void test_lookup(const fs::path& folder){
    const Dictionary dictionary(folder / "made" / "test.hsdict");

    // As written
    WordLookup found = look_up_in(dictionary, "紙", false);
    CHECK(found.matched == "紙" && found.entries.size() == 1);
    if (!found.entries.empty()){
        const WordEntry& kami = found.entries[0];
        CHECK(kami.forms.size() == 1 && kami.forms[0].word == "紙" && kami.forms[0].reading == "かみ");
        CHECK(kami.is_common && kami.source == "Test [2026-10-03]");
        // The senses plainly: no example, no reading over its kanji
        CHECK(kami.senses.size() == 2 && kami.senses[0].definitions == std::vector<std::string>({"paper"}));
        CHECK(kami.senses.size() == 2 && kami.senses[1].definitions == std::vector<std::string>({"sheet", "leaf"}));
        CHECK(kami.senses.size() == 2 && kami.senses[0].parts_of_speech == std::vector<std::string>({"noun"}));
        // And as a card gets them, with the dictionary's stylesheet scoped to it
        CHECK(kami.glossary_html.starts_with("<div style=\"text-align: left;\" class=\"yomitan-glossary\">"
                                             "<ol><li data-dictionary=\"Test [2026-10-03]\"><span><ul data-sc-content=\"sense-groups\">"));
        CHECK(kami.glossary_html.find("<a href=\"https://example.com/1\">source</a>") != std::string::npos);
        CHECK(kami.glossary_html.ends_with("</span></li></ol><style>.yomitan-glossary [data-dictionary=\"Test [2026-10-03]\"] "
                                           "li { color: red; }\n</style></div>"));
    }

    // By reading: the best known first
    found = look_up_in(dictionary, "かみ", false);
    CHECK(found.entries.size() == 3 && found.entries[0].forms[0].word == "紙" && found.entries[2].forms[0].word == "神");
    CHECK(found.entries.size() == 3 && !found.entries[2].is_common);

    // From an inflection, only of a word that inflects that way
    found = look_up_in(dictionary, "食べなかった", false);
    CHECK(found.matched == "食べなかった" && found.entries.size() == 1 && found.entries[0].forms[0].word == "食べる");
    CHECK(found.entries.size() == 1 && found.entries[0].senses[0].parts_of_speech == std::vector<std::string>({"1-dan", "transitive"}));
    found = look_up_in(dictionary, "悩んでた", false);
    CHECK(found.entries.size() == 1 && found.entries[0].forms[0].word == "悩む");
    found = look_up_in(dictionary, "高くなかった", false);
    CHECK(found.entries.size() == 1 && found.entries[0].forms[0].word == "高い");
    // 紙 isn't a verb: 紙た is nothing
    CHECK(look_up_in(dictionary, "紙た", false).entries.empty());
    // The word as written comes before the word it could be a form of
    found = look_up_in(dictionary, "した", false);
    CHECK(found.entries.size() == 2 && found.entries[0].senses[0].definitions[0] == "under");
    CHECK(found.entries.size() == 2 && found.entries[1].forms[0].reading == "する");

    // A selection that starts with a word, and one that doesn't
    found = look_up_in(dictionary, "紙と筆を", true);
    CHECK(found.matched == "紙" && found.entries.size() == 1);
    found = look_up_in(dictionary, "悩んでたかも", true);
    CHECK(found.matched == "悩んでた" && found.entries.size() == 1 && found.entries[0].forms[0].word == "悩む");
    CHECK(look_up_in(dictionary, "紙と筆を", false).entries.empty());
    CHECK(look_up_in(dictionary, "んん", true).entries.empty() && look_up_in(dictionary, "んん", true).matched.empty());
    CHECK(look_up_in(dictionary, "", true).entries.empty());

    // An old spelling gives the word it's listed under
    found = look_up_in(dictionary, "惱む", false);
    CHECK(found.entries.size() == 1 && found.entries[0].forms[0].word == "悩む");
    found = look_up_in(dictionary, "惱んだ", false);
    CHECK(found.entries.size() == 1 && found.entries[0].forms[0].word == "悩む");

    // Spellings of one word are one entry; in kana, a word has no reading
    // apart from itself
    found = look_up_in(dictionary, "いちじく", false);
    CHECK(found.entries.size() == 1 && found.entries[0].forms.size() == 1 && found.entries[0].forms[0].word == "無花果");
    found = look_up_in(dictionary, "イチジク", false);
    CHECK(found.entries.size() == 1 && found.entries[0].forms[0].word.empty() && found.entries[0].forms[0].reading == "イチジク");

    // Plain definitions are one sense
    found = look_up_in(dictionary, "筆", false);
    CHECK(found.entries.size() == 1 && found.entries[0].senses.size() == 1);
    CHECK(found.entries.size() == 1 && found.entries[0].senses[0].definitions == std::vector<std::string>({"brush", "pen"}));
    CHECK(found.entries.size() == 1 && found.entries[0].glossary_html.find("<ul><li>brush</li><li>pen</li></ul>") != std::string::npos);

    // Structured content that isn't laid out like Jitendex's: its text
    const auto senses = plain_senses(json::parse(R"([{"type": "structured-content", "content":
        {"tag": "div", "content": ["a  kind\nof ", {"tag": "span", "content": "tree"}]}}])"));
    CHECK(senses.size() == 1 && senses[0].definitions == std::vector<std::string>({"a kind of tree"}));
}

static void test_age(){
    using namespace std::chrono;
    const auto on = [](int y, unsigned m, unsigned d){ return system_clock::time_point(sys_days(year(y) / month(m) / day(d))); };
    DictionaryInfo info;
    info.revision = "2026.10.03.0";
    CHECK(dictionary_age_days(info, on(2026, 10, 3)) == 0);
    CHECK(dictionary_age_days(info, on(2026, 11, 11)) == 39);
    CHECK(dictionary_age_days(info, on(2027, 10, 3)) == 365);
    // One from the future isn't old
    CHECK(dictionary_age_days(info, on(2026, 1, 1)) == 0);
    // Without a date in the revision, since the file was made
    info.revision = "v7";
    info.converted = duration_cast<seconds>(on(2026, 11, 1).time_since_epoch()).count();
    CHECK(dictionary_age_days(info, on(2026, 11, 11)) == 10);
    info.revision = "2026.13.40.0";
    CHECK(dictionary_age_days(info, on(2026, 11, 11)) == 10);

    DictionaryInfo installed;
    installed.revision = "2026.10.03.0";
    CHECK(is_newer({.title = "", .revision = "2026.11.01.0", .download_url = ""}, &installed));
    CHECK(!is_newer({.title = "", .revision = "2026.10.03.0", .download_url = ""}, &installed));
    CHECK(!is_newer({.title = "", .revision = "2026.09.01.0", .download_url = ""}, &installed));
    CHECK(is_newer({.title = "", .revision = "2026.09.01.0", .download_url = ""}, nullptr));
}

// A frequency dictionary shaped like JPDBv2㋕, in two banks, with a pitch
// accent row among them that isn't a frequency
static void test_frequencies(const fs::path& folder){
    const json index = {{"title", "JPDBv2㋕"}, {"revision", "JPDB v2.2 Frequency Kana 2024-10-13"}, {"format", 3},
                        {"author", "jpdb, Kuuube, Gecko"}, {"frequencyMode", "rank-based"}};
    const json bank1 = json::parse(R"([
        ["の", "freq", {"value": 1, "displayValue": "1㋕"}],
        ["読む", "freq", {"reading": "よむ", "frequency": {"value": 312, "displayValue": "312"}}],
        ["仮名", "freq", {"reading": "かな", "frequency": {"value": 24501, "displayValue": "24501㋕"}}],
        ["読む", "freq", {"reading": "よむ", "frequency": {"value": 19896, "displayValue": "19896㋕"}}],
        ["読む", "pitch", {"reading": "よむ", "pitches": [{"position": 1}]}]
    ])");
    const json bank2 = json::parse(R"([
        ["よむ", "freq", {"value": 19896, "displayValue": "19896㋕"}],
        ["仮名", "freq", {"reading": "かな", "frequency": {"value": 34143, "displayValue": "34143"}}],
        ["仮名", "freq", {"reading": "かめい", "frequency": 82902}],
        ["猫", "freq", 1500],
        ["犬", "freq", "very common"]
    ])");
    const fs::path zip = folder / "frequencies.zip";
    write_file(zip, make_zip({{"index.json", index.dump(), false}, {"term_meta_bank_2.json", bank2.dump(), true},
                              {"term_meta_bank_1.json", bank1.dump(), true}}));
    const fs::path file = folder / "made" / "test.hsfreq";
    const FrequencyInfo info = import_frequencies(zip, file);
    CHECK(info.title == "JPDBv2㋕" && info.revision == "JPDB v2.2 Frequency Kana 2024-10-13");
    CHECK(!fs::exists(file.string() + ".part"));

    const FrequencyList list(file);
    CHECK(list.info().title == "JPDBv2㋕" && list.info().author == "jpdb, Kuuube, Gecko");
    CHECK(list.size() == 9);
    // As Yomitan shows them, in the dictionary's order
    using Shown = std::vector<std::string>;
    CHECK(list.find("読む", "よむ") == Shown({"312", "19896㋕"}));
    CHECK(list.find("よむ", "よむ") == Shown({"19896㋕"}));
    CHECK(list.find("よむ", "") == Shown({"19896㋕"}));
    CHECK(list.find("仮名", "かな") == Shown({"24501㋕", "34143"}));
    CHECK(list.find("仮名", "かめい") == Shown({"82902"}));
    CHECK(list.find("猫", "ねこ") == Shown({"1500"}));
    CHECK(list.find("犬", "いぬ") == Shown({"very common"}));
    CHECK(list.find("読む", "どく").empty());
    CHECK(list.find("書く", "かく").empty());
    CHECK(list.find("", "").empty());

    // On entries, for each one's first form
    std::vector<WordEntry> entries(3);
    entries[0].forms = {{"読む", "よむ"}, {"讀む", "よむ"}};
    entries[1].forms = {{"", "の"}};
    entries[2].forms = {{"書く", "かく"}};
    add_frequencies(list, entries);
    CHECK(entries[0].frequency_source == "JPDBv2㋕" && entries[0].frequencies == Shown({"312", "19896㋕"}));
    CHECK(entries[1].frequencies == Shown({"1㋕"}));
    CHECK(entries[2].frequencies.empty() && entries[2].frequency_source.empty());

    // Not one, or damaged
    write_file(folder / "no_freq.zip", make_zip({{"index.json", index.dump(), false}}));
    CHECK(THROWS(import_frequencies(folder / "no_freq.zip", folder / "made" / "none.hsfreq")));
    CHECK(THROWS(import_frequencies(make_dictionary_zip(folder), folder / "made" / "none.hsfreq")));
    std::string bytes = read_file(file);
    write_file(folder / "cut.hsfreq", bytes.substr(0, bytes.size() - 10));
    CHECK(THROWS(FrequencyList(folder / "cut.hsfreq")));
    CHECK(THROWS(FrequencyList(folder / "made" / "test.hsdict")));
    CHECK(THROWS(FrequencyList(folder / "missing.hsfreq")));
}

int main(){
    const fs::path folder = fs::temp_directory_path() / std::format("hearstash_dictionary_test_{}",
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(folder);
    try {
        test_zip(folder);
        test_import(folder);
        test_lookup(folder);
        test_age();
        test_frequencies(folder);
    } catch (const std::exception& e){
        std::fprintf(stderr, "Threw: %s\n", e.what());
        ++failures;
    }
    fs::remove_all(folder);
    if (failures == 0) std::puts("All dictionary tests passed");
    return failures;
}

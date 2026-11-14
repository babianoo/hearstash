#include "core/word_lookup.h"

#include <algorithm>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

// JSON
#include <nlohmann/json.hpp>

#include "core/card_format.h"
#include "core/deinflect.h"
#include "core/dictionary.h"
#include "core/dictionary_files.h"
#include "core/frequency_list.h"
#include "core/jisho.h"
#include "core/structured_content.h"

using json = nlohmann::json;

// ---- Plain senses ----

// What a dictionary marked node as, e.g. "sense" for
// {"data": {"content": "sense"}}. Empty if it didn't.
static std::string marked_as(const json& node){
    if (!node.is_object()) return "";
    const auto data = node.find("data");
    if (data == node.end() || !data->is_object()) return "";
    const auto content = data->find("content");
    return content != data->end() && content->is_string() ? content->get<std::string>() : "";
}

// node's text without its markup, or the readings written over kanji
static void add_text(const json& node, std::string& text){
    if (node.is_string()){
        text += node.get_ref<const std::string&>();
    } else if (node.is_array()){
        for (const json& child : node) add_text(child, text);
    } else if (node.is_object()){
        const auto tag = node.find("tag");
        if (tag != node.end() && tag->is_string() && (*tag == "rt" || *tag == "rp")) return;
        if (const auto content = node.find("content"); content != node.end()) add_text(*content, text);
    }
}

// text with each run of spaces and line breaks as one space, and none
// around it
static std::string tidied(const std::string& text){
    std::string out;
    for (char c : text){
        const bool blank = c == ' ' || c == '\n' || c == '\t' || c == '\r';
        if (!blank) out += c;
        else if (!out.empty() && out.back() != ' ') out += ' ';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Calls each for node's content: the one thing it holds, or each of
// several
template <typename Each>
static void for_children(const json& node, const Each& each){
    const auto content = node.find("content");
    if (content == node.end()) return;
    if (content->is_array()) for (const json& child : *content) each(child);
    else each(*content);
}

// The first thing in node marked as wanted, or nullptr
static const json* find_marked(const json& node, const std::string& wanted){
    if (node.is_array()){
        for (const json& child : node)
            if (const json* found = find_marked(child, wanted)) return found;
    } else if (node.is_object()){
        if (marked_as(node) == wanted) return &node;
        if (const auto content = node.find("content"); content != node.end()) return find_marked(*content, wanted);
    }
    return nullptr;
}

// Adds the senses in node, which Jitendex lays out as
//   sense-group: part-of-speech-info..., then sense...
//   sense: glossary (a list, a gloss an item), then examples and notes
static void add_senses(const json& node, const std::vector<std::string>& parts_of_speech,
                       std::vector<WordSense>& senses){
    if (node.is_array()){
        for (const json& child : node) add_senses(child, parts_of_speech, senses);
        return;
    }
    if (!node.is_object()) return;

    const std::string mark = marked_as(node);
    if (mark == "sense-group"){
        std::vector<std::string> parts;
        for_children(node, [&](const json& child){
            if (marked_as(child) != "part-of-speech-info") return;
            std::string text;
            add_text(child, text);
            if (!(text = tidied(text)).empty()) parts.push_back(std::move(text));
        });
        for_children(node, [&](const json& child){ add_senses(child, parts, senses); });
    } else if (mark == "sense"){
        WordSense sense{.definitions = {}, .parts_of_speech = parts_of_speech};
        if (const json* glossary = find_marked(node, "glossary")){
            for_children(*glossary, [&](const json& gloss){
                std::string text;
                add_text(gloss, text);
                if (!(text = tidied(text)).empty()) sense.definitions.push_back(std::move(text));
            });
        }
        if (!sense.definitions.empty()) senses.push_back(std::move(sense));
    } else {
        for_children(node, [&](const json& child){ add_senses(child, parts_of_speech, senses); });
    }
}

std::vector<WordSense> plain_senses(const json& definitions){
    std::vector<WordSense> senses;
    if (!definitions.is_array()) return senses;

    // Plain definitions are the glosses of one sense
    WordSense plain;
    for (const json& definition : definitions){
        if (definition.is_string()){
            plain.definitions.push_back(definition.get<std::string>());
        } else if (definition.is_object() && definition.value("type", "") == "text"){
            plain.definitions.push_back(definition.value("text", ""));
        } else if (definition.is_object() && definition.value("type", "") == "structured-content"
                   && definition.contains("content")){
            const size_t before = senses.size();
            add_senses(definition["content"], {}, senses);
            // Not marked up the way Jitendex does it: its text, whole
            if (senses.size() == before){
                std::string text;
                add_text(definition["content"], text);
                if (!(text = tidied(text)).empty()) senses.push_back({.definitions = {std::move(text)}, .parts_of_speech = {}});
            }
        }
    }
    if (!plain.definitions.empty()) senses.insert(senses.begin(), std::move(plain));
    return senses;
}

// ---- Looking up ----

// A row that matched, and how well
struct Hit {
    DictionaryRow row;
    // Found from an inflection rather than as written
    bool inflected;
    // Found under a spelling that's only listed as another way to write it
    bool redirected;
    // What was looked up is its reading, not how it's written
    bool by_reading;

    // Lower is better
    auto rank() const { return std::tuple(inflected, redirected, by_reading, -row.score, row.index); }
};

// The most entries one lookup gives. A reading like こう has hundreds of
// words; the best-known ones are what's wanted.
static constexpr size_t MAX_ENTRIES = 20;
// The most characters of a text tried as a word. Longer than any inflected
// word, and it keeps a whole paragraph from being looked up piece by piece.
static constexpr size_t MAX_WORD_CHARS = 24;

// The rows text is, as written or as an inflection of them
static std::vector<Hit> hits_for(const Dictionary& dictionary, std::string_view text){
    std::vector<Hit> hits;
    std::unordered_set<std::uint32_t> seen;
    for (const Deinflection& candidate : deinflect(text)){
        for (const DictionaryRow& found : dictionary.find(candidate.word)){
            // Listed under another spelling: that spelling's rows instead
            std::vector<DictionaryRow> rows;
            if (found.redirect.empty()){
                rows.push_back(found);
            } else {
                for (const DictionaryRow& target : dictionary.find(found.redirect))
                    if (target.redirect.empty() && target.term == found.redirect) rows.push_back(target);
            }
            for (const DictionaryRow& row : rows){
                // An inflection only of a word that inflects that way
                if (candidate.kinds && !(word_kinds(row.rules) & candidate.kinds)) continue;
                if (!seen.insert(row.index).second) continue;
                hits.push_back({row, candidate.kinds != 0, !found.redirect.empty(),
                                found.redirect.empty() && found.term != candidate.word});
            }
        }
    }
    return hits;
}

// The hits as entries: the rows of one word together, best first
static std::vector<WordEntry> entries_of(const Dictionary& dictionary, std::vector<Hit> hits){
    std::ranges::sort(hits, {}, &Hit::rank);

    // Rows with the same sequence number are spellings of one word. One
    // without a number is a word of its own.
    std::vector<std::vector<Hit>> words;
    std::unordered_map<int, size_t> word_of;
    for (const Hit& hit : hits){
        const auto found = hit.row.sequence > 0 ? word_of.find(hit.row.sequence) : word_of.end();
        if (found != word_of.end()){
            words[found->second].push_back(hit);
            continue;
        }
        if (words.size() == MAX_ENTRIES) continue;
        if (hit.row.sequence > 0) word_of[hit.row.sequence] = words.size();
        words.push_back({hit});
    }

    const DictionaryInfo& info = dictionary.info();
    const std::string styles = scope_styles(info.styles, info.title);
    std::vector<WordEntry> entries;
    for (const std::vector<Hit>& word : words){
        WordEntry entry;
        entry.source = info.title;
        for (const Hit& hit : word){
            const DictionaryRow& row = hit.row;
            // Written in kana, a word only has a reading
            const bool kana = row.reading.empty() || row.reading == row.term;
            WordForm form{.word = kana ? "" : std::string(row.term), .reading = std::string(kana ? row.term : row.reading)};
            const bool listed = std::ranges::any_of(entry.forms, [&](const WordForm& f){
                return f.word == form.word && f.reading == form.reading;
            });
            if (!listed) entry.forms.push_back(std::move(form));
            // Jitendex's mark for a common word
            entry.is_common = entry.is_common || row.tags.find("★") != std::string_view::npos;
        }
        // Every spelling of a word has the same definitions
        const json definitions = dictionary.definitions(word.front().row);
        entry.senses = plain_senses(definitions);
        entry.glossary_html = card_glossary(info.title, render_definitions(definitions), styles);
        entries.push_back(std::move(entry));
    }
    return entries;
}

// Where each character of text ends, up to MAX_WORD_CHARS of them
static std::vector<size_t> character_ends(std::string_view text){
    std::vector<size_t> ends;
    for (size_t at = 1; at <= text.size() && ends.size() < MAX_WORD_CHARS; ++at){
        // Not in the middle of a UTF-8 character: its later bytes are 10xxxxxx
        if (at == text.size() || (static_cast<unsigned char>(text[at]) & 0xC0) != 0x80) ends.push_back(at);
    }
    return ends;
}

WordLookup look_up_in(const Dictionary& dictionary, std::string_view text, bool from_start){
    if (text.empty()) return {};
    std::vector<size_t> ends = from_start ? character_ends(text) : std::vector<size_t>{text.size()};
    // Longest first
    std::ranges::reverse(ends);
    for (size_t end : ends){
        std::vector<Hit> hits = hits_for(dictionary, text.substr(0, end));
        if (hits.empty()) continue;
        return {.entries = entries_of(dictionary, std::move(hits)), .matched = std::string(text.substr(0, end))};
    }
    return {};
}

// ---- The app's dictionary ----

static std::mutex current_mutex;
static std::shared_ptr<const Dictionary> current;
// Whether current is as the files are now, even if that's none
static bool current_loaded = false;

std::shared_ptr<const Dictionary> current_dictionary(){
    const std::lock_guard lock(current_mutex);
    if (!current_loaded){
        current = open_installed_dictionary();
        current_loaded = true;
    }
    return current;
}

void reload_dictionary(){
    const std::lock_guard lock(current_mutex);
    current.reset();
    current_loaded = false;
}

void add_frequencies(const FrequencyList& list, std::vector<WordEntry>& entries){
    for (WordEntry& entry : entries){
        if (entry.forms.empty()) continue;
        const WordForm& main = entry.forms.front();
        // A word written in kana is its own reading
        entry.frequencies = list.find(main.word.empty() ? main.reading : main.word, main.reading);
        if (!entry.frequencies.empty()) entry.frequency_source = list.info().title;
    }
}

WordLookup look_up_word(std::string_view text, bool from_start){
    WordLookup found;
    if (const std::shared_ptr<const Dictionary> dictionary = current_dictionary())
        found = look_up_in(*dictionary, text, from_start);
    if (found.entries.empty()){
        found = {.entries = jisho_search(text), .matched = {}};
        if (!found.entries.empty()) found.matched = text;
    }
    if (const std::shared_ptr<const FrequencyList> list = current_frequencies()) add_frequencies(*list, found.entries);
    return found;
}

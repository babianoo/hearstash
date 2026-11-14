#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// JSON
#include <nlohmann/json_fwd.hpp>

#include "core/word_entry.h"

class Dictionary;
class FrequencyList;

// Looking a word up: in the dictionary file on this computer if there is
// one, which also finds a word from an inflected form of it, and on
// jisho.org otherwise.

struct WordLookup {
    // Best match first. Empty if nothing was found.
    std::vector<WordEntry> entries;
    // The part of the text the entries are for, as it's written there:
    // all of it, or its start when only that is a word (see look_up_in()).
    // Empty if nothing was found.
    std::string matched;
};

// Looks text up in dictionary: as it's written, and as every dictionary
// form it could be an inflection of, e.g. 悩んでた as 悩む. Words that are
// spellings of one another come as one entry, the best-known spelling
// first, and a word found as written before one found from an inflection.
//
// With from_start, text is where a word starts rather than a word: if all
// of it isn't one, ever less of its end is tried, so 悩んでたかも finds
// 悩む from 悩んでた. That's for a selection in a sentence, which often
// takes in a particle too. Without it only the whole text counts, as for
// what's typed in a search box.
//
// Throws std::runtime_error if the dictionary file can't be read any more.
WordLookup look_up_in(const Dictionary& dictionary, std::string_view text, bool from_start);

// A row's definitions (Dictionary::definitions()) as plain senses, for
// showing in the app and editing on a card: each sense's glosses and its
// parts of speech. Jitendex marks those up, which is what's read here;
// from another dictionary's structured content, each definition is one
// sense holding its text.
std::vector<WordSense> plain_senses(const nlohmann::json& definitions);

// The dictionary lookups use: the one shipped next to the program, or the
// newer of it and the one the user updated to, in
// data_dir()/dictionary. nullptr if there's neither, or it can't be read.
// Opened the first time it's asked for, which takes a moment, so not on
// the UI thread; safe to call from any thread after that too.
std::shared_ptr<const Dictionary> current_dictionary();
// Makes current_dictionary() look again, after an update put a new file
// there. Lookups already running keep the one they have.
void reload_dictionary();

// Fills in each entry's frequencies from list, for its first form, as
// Yomitan shows a frequency dictionary's beside a word
void add_frequencies(const FrequencyList& list, std::vector<WordEntry>& entries);

// Looks text up in current_dictionary(), like look_up_in(). If there's no
// dictionary, or it has nothing, on jisho.org instead, which also takes
// English and romaji. Either way, with frequencies from
// current_frequencies() if there's a list. Blocks while that runs, so not on the UI
// thread. Throws std::runtime_error if jisho.org was needed and couldn't
// be reached, or what look_up_in() throws.
WordLookup look_up_word(std::string_view text, bool from_start);

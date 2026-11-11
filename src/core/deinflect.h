#pragma once

#include <string>
#include <string_view>
#include <vector>

// Finds the dictionary forms a word in a sentence could be an inflection
// of, e.g. 食べなかった could be 食べる (an ichidan verb). A dictionary
// only lists 食べる, and says how it inflects; this is what turns what's
// written in the sentence into what to look up.
//
// It works on the spelling alone, by taking endings off and putting the
// dictionary form's back: 食べなかった -> 食べない -> 食べる. It doesn't
// know which words exist, so it also offers ones that don't, like 食ぶ
// for 食べる. The caller keeps the candidates the dictionary has, as the
// kind of word the candidate needs it to be.
//
// The rules are written from Japanese grammar as textbooks teach it; see
// deinflect.cpp.

// The kinds of word that inflect, as dictionaries in Yomitan's format name
// them in a row's rules
enum WordKind : unsigned {
    KIND_V1 = 1,        // "v1": ichidan verbs, 食べる
    KIND_V5 = 2,        // "v5": godan verbs, 書く
    KIND_VS = 4,        // "vs": する and words ending in it
    KIND_VK = 8,        // "vk": 来る
    KIND_VZ = 16,       // "vz": ずる verbs, 論ずる
    KIND_ADJ_I = 32,    // "adj-i": い adjectives, 高い
};

// What rules, e.g. "v5 vs", names
unsigned word_kinds(std::string_view rules);

struct Deinflection {
    // What the word could be an inflection of
    std::string word;
    // The kinds of word it then has to be, of WordKind. 0 for the text
    // itself, which may be any word.
    unsigned kinds = 0;
    // What was taken off to get there, outermost first, for showing the
    // user, e.g. "past" and "negative" for 食べなかった. Empty for the text
    // itself.
    std::vector<std::string> steps;
};

// text itself first, then every form it could be an inflection of, the
// ones with fewer steps first. No candidate is given twice for the same
// kinds.
std::vector<Deinflection> deinflect(std::string_view text);

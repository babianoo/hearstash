#include "core/anki_fields.h"

#include <algorithm>
#include <cctype>
#include <span>

std::string& field_for(AnkiSettings& settings, CardPart part){
    switch (part){
    case CardPart::Word: return settings.word_field;
    case CardPart::Reading: return settings.reading_field;
    case CardPart::Meaning: return settings.meaning_field;
    case CardPart::Sentence: return settings.sentence_field;
    case CardPart::Audio: return settings.audio_field;
    case CardPart::Picture: break;
    }
    return settings.picture_field;
}

const std::string& field_for(const AnkiSettings& settings, CardPart part){
    return field_for(const_cast<AnkiSettings&>(settings), part);
}

// Lowercase, without spaces, '_' and '-', so "Sentence Audio",
// "sentence_audio" and "SentenceAudio" are all "sentenceaudio"
static std::string normalize(const std::string& name){
    std::string out;
    for (char c : name)
        if (c != ' ' && c != '_' && c != '-') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::array<std::string, CARD_PART_COUNT> guess_fields(const std::vector<std::string>& fields){
    using Keywords = std::span<const char* const>;
    // Most telling first. The meaning comes after the reading, so
    // "ExpressionReading" goes to the reading, and the word last, so
    // "Expression" is left for it.
    static constexpr const char* AUDIO[] = {"sentenceaudio", "audio", "sound"};
    static constexpr const char* PICTURE[] = {"picture", "image", "screenshot", "snapshot"};
    static constexpr const char* SENTENCE[] = {"sentence", "example", "context"};
    static constexpr const char* READING[] = {"reading", "furigana", "kana"};
    static constexpr const char* MEANING[] = {"meaning", "definition", "glossary", "english", "back"};
    static constexpr const char* WORD[] = {"word", "expression", "vocab", "kanji", "target", "front"};
    const std::pair<CardPart, Keywords> order[] = {
        {CardPart::Audio, AUDIO}, {CardPart::Picture, PICTURE}, {CardPart::Sentence, SENTENCE},
        {CardPart::Reading, READING}, {CardPart::Meaning, MEANING}, {CardPart::Word, WORD},
    };

    std::vector<std::string> names;
    for (const std::string& field : fields) names.push_back(normalize(field));
    std::vector<bool> taken(fields.size(), false);

    std::array<std::string, CARD_PART_COUNT> guesses;
    for (const auto& [part, keywords] : order){
        // A field named just the keyword beats one that only has it in its
        // name: "Sentence" over "SentenceFurigana"
        bool found = false;
        for (bool exact : {true, false}){
            for (const char* keyword : keywords){
                for (size_t i = 0; i < fields.size() && !found; ++i){
                    if (taken[i]) continue;
                    if (exact ? names[i] == keyword : names[i].find(keyword) != std::string::npos){
                        guesses[static_cast<size_t>(part)] = fields[i];
                        taken[i] = true;
                        found = true;
                    }
                }
                if (found) break;
            }
            if (found) break;
        }
    }
    return guesses;
}

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "core/settings.h"

// The parts of a card the app fills in, in the order Settings lists them
enum class CardPart { Word, Reading, Meaning, Sentence, Audio, Picture };
inline constexpr size_t CARD_PART_COUNT = 6;
// Indexed by CardPart, as they're shown to the user
inline constexpr const char* CARD_PART_NAMES[] = {"Word", "Reading", "Meaning", "Sentence", "Audio", "Picture"};

// The setting saying which field part goes in
std::string& field_for(AnkiSettings& settings, CardPart part);
const std::string& field_for(const AnkiSettings& settings, CardPart part);

// For each part, indexed by CardPart, the field of a note type with these
// fields that's most likely meant for it, going by the names: e.g.
// "Expression" for the word, "SentenceAudio" for the audio. Empty for a
// part none fits. No field is given to two parts.
std::array<std::string, CARD_PART_COUNT> guess_fields(const std::vector<std::string>& fields);

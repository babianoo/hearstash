#pragma once

#include <iterator>
#include <string>
#include <vector>

#include "core/anki_fields.h"

// The app's own note type, so cards look right without the user having
// made one: the sentence on the front with the word highlighted, and the
// word, its reading, the meanings, the audio and the picture on the back,
// in big Japanese type, with the meanings styled like Jitendex's in
// Yomitan. It's the default in Settings > Anki, and is added to Anki with
// the first card that uses it.

inline constexpr const char* HEARSTASH_NOTE_TYPE = "Hearstash";

// Its fields, in order: CARD_PART_NAMES, one for each part of a card. The
// word comes first, so it's what Anki sorts by and checks for duplicates.
inline std::vector<std::string> hearstash_fields(){
    return {std::begin(CARD_PART_NAMES), std::end(CARD_PART_NAMES)};
}

// Adds the note type to Anki unless it has one by that name. One an older
// version of the app added gets this version's styling and card template,
// but only where it's still exactly as the app made it: what the user
// changed is left as it is. Throws like anki_connect.h.
void ensure_hearstash_note_type();

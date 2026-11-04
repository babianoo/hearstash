#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Talks to Anki through the AnkiConnect add-on, which listens on
// http://127.0.0.1:8765 while Anki is open. Ported from japanese-helper's
// src/api.cpp.
//
// Each call blocks until Anki answers, which is quick since it's on this
// machine. Each throws std::runtime_error with a message fit to show the
// user: e.g. that Anki isn't open, or what AnkiConnect said went wrong.

std::vector<std::string> anki_deck_names();
// The note types, e.g. "Basic", "Japanese (recognition)"
std::vector<std::string> anki_model_names();
// The fields of note type model, in order
std::vector<std::string> anki_model_field_names(const std::string& model);

// One kind of card a note type makes, as Anki's template syntax
struct AnkiCardTemplate {
    std::string name;   // e.g. "Card 1"
    std::string front;  // e.g. "{{Front}}"
    std::string back;   // e.g. "{{FrontSide}}<hr id=answer>{{Back}}"
};

// Adds a note type with these fields, in order, styled by css. Anki
// refuses one whose name is taken.
void anki_create_model(const std::string& name, const std::vector<std::string>& fields, const std::string& css,
                       const std::vector<AnkiCardTemplate>& templates);

struct AnkiNote {
    std::string deck;
    std::string model;
    // Field name and its HTML
    std::vector<std::pair<std::string, std::string>> fields;
    std::vector<std::string> tags;
};

// Stores data in Anki's media folder as filename, replacing a file with
// that name, so a note can show it with [sound:filename] or
// <img src="filename">. Name it uniquely enough not to replace another
// note's.
void anki_store_media(const std::string& filename, const std::vector<std::uint8_t>& data);

// Adds note and returns its id. Store its files first. Anki refuses a note
// whose first field is empty, or the same as another note's of that type
// in the deck.
std::int64_t anki_add_note(const AnkiNote& note);

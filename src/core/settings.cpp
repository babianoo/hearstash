#include "core/settings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>

#include "core/atomic_write.h"
#include "core/bad_file.h"
#include "core/shortcut.h"
#include "core/whisper_models.h"

// JSON
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

// Written into the file. Bump it when a field is renamed or changes
// meaning, so load_settings can tell an older file apart and convert it.
static constexpr int VERSION = 1;

// Saved by its id, not its number, so reordering Api can't change what an
// old file says. One it doesn't know (e.g. from a newer version) is
// Jisho, rather than failing the whole file like a wrong type would.
static void to_json(json& j, Api api){
    j = API_IDS[static_cast<size_t>(api)];
}

static void from_json(const json& j, Api& api){
    const auto found = std::ranges::find(API_IDS, j.get<std::string>());
    api = found == std::end(API_IDS) ? Api::Jisho : static_cast<Api>(found - std::begin(API_IDS));
}

// to_json and from_json for each struct, which json(settings) and
// get<Settings>() use. A field is only saved if it's listed here.
// _WITH_DEFAULT: a key the file doesn't have keeps the field's default
// instead of throwing.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AnkiSettings, deck, note_type, word_field, reading_field,
                                                meaning_field, sentence_field, audio_field, picture_field,
                                                picture_quality, crop_x, crop_y, crop_w, crop_h)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ApiSettings, deepl_key, last_api)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BufferSettings, enabled, seconds, selection_seconds, shortcut)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GeneralSettings, scroll_speed)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HistorySettings, capacity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SpeechSettings, whisper_model, input_device, use_gpu)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ScreenArea, x, y, w, h)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ScreenTextSettings, shortcut, areas)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, anki, api, buffer, general, history, screen_text, speech)

Settings load_settings(const fs::path& file){
    std::ifstream in(file);
    if (!in) return {};

    try {
        // Keys it doesn't know, like "version", are ignored
        Settings settings = json::parse(in).get<Settings>();
        sanitize(settings);
        return settings;
    } catch (const json::exception& e){
        // Not JSON, or a value of the wrong type, which fails the whole
        // file
        in.close();
        set_aside_bad_file(file, e.what(), "the settings start from the defaults");
        return {};
    }
}

void save_settings(const fs::path& file, const Settings& settings){
    json j = settings;
    j["version"] = VERSION;
    write_file_atomically(file, j.dump(4) + '\n');
}

void sanitize(Settings& settings){
    // DeepL rejects a key with the spaces or line break it may be pasted
    // with
    std::string& key = settings.api.deepl_key;
    key.erase(key.find_last_not_of(" \t\r\n") + 1);
    key.erase(0, key.find_first_not_of(" \t\r\n"));

    // E.g. edited by hand
    int& scroll_speed = settings.general.scroll_speed;
    scroll_speed = std::clamp(scroll_speed, GeneralSettings::MIN_SCROLL_SPEED, GeneralSettings::MAX_SCROLL_SPEED);
    int& capacity = settings.history.capacity;
    capacity = std::clamp(capacity, HistorySettings::MIN_CAPACITY, HistorySettings::MAX_CAPACITY);
    BufferSettings& buffer = settings.buffer;
    buffer.seconds = std::clamp(buffer.seconds, BufferSettings::MIN_SECONDS, BufferSettings::MAX_SECONDS);
    buffer.selection_seconds = std::clamp(buffer.selection_seconds, 1, buffer.seconds);
    // Written the one way, so a change can be told by comparing. One that
    // can't be read is replaced, so there's always one.
    const std::optional<Shortcut> shortcut = parse_shortcut(buffer.shortcut);
    buffer.shortcut = shortcut ? format_shortcut(*shortcut) : DEFAULT_SHORTCUT;
    std::string& screen_shortcut = settings.screen_text.shortcut;
    const std::optional<Shortcut> parsed_screen = parse_shortcut(screen_shortcut);
    screen_shortcut = parsed_screen ? format_shortcut(*parsed_screen) : DEFAULT_SCREEN_SHORTCUT;

    AnkiSettings& anki = settings.anki;
    // E.g. saved by a version that had no note type of its own, before one
    // was chosen. The fields went with no note type, so they mean nothing.
    if (anki.note_type.empty()){
        const AnkiSettings defaults;
        anki.note_type = defaults.note_type;
        anki.word_field = defaults.word_field;
        anki.reading_field = defaults.reading_field;
        anki.meaning_field = defaults.meaning_field;
        anki.sentence_field = defaults.sentence_field;
        anki.audio_field = defaults.audio_field;
        anki.picture_field = defaults.picture_field;
    }

    // A crop that doesn't fit in a screenshot is no crop
    anki.picture_quality = std::clamp(anki.picture_quality, AnkiSettings::MIN_QUALITY, AnkiSettings::MAX_QUALITY);
    const bool fits = anki.crop_x >= 0 && anki.crop_y >= 0 && anki.crop_w > 0 && anki.crop_h > 0
                   && anki.crop_x + anki.crop_w <= 1.0001 && anki.crop_y + anki.crop_h <= 1.0001;
    if (!fits) anki.crop_x = anki.crop_y = anki.crop_w = anki.crop_h = 0;

    // An area that doesn't fit in a screenshot is no area
    std::erase_if(settings.screen_text.areas, [](const auto& entry){
        const ScreenArea& a = entry.second;
        return a.empty() || a.x < 0 || a.y < 0 || a.x + a.w > 1.0001 || a.y + a.h > 1.0001;
    });

    // An id the app doesn't offer, e.g. one a version with other models
    // saved, or a typo in the file. Clearing it means nothing has to check
    // find_model() for nullptr before using it.
    std::string& model = settings.speech.whisper_model;
    if (!model.empty() && !find_model(model)) model.clear();
}

std::string effective_deepl_key(const Settings& settings){
    if (!settings.api.deepl_key.empty()) return settings.api.deepl_key;
    const char* key = std::getenv("DEEPL_API_KEY");
    return key ? key : "";
}

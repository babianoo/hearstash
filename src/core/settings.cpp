#include "core/settings.h"

#include <cstdlib>
#include <fstream>

#include "core/atomic_write.h"
#include "core/bad_file.h"
#include "core/whisper_models.h"

// JSON
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

// Written into the file. Bump it when a field is renamed or changes
// meaning, so load_settings can tell an older file apart and convert it.
static constexpr int VERSION = 1;

// to_json and from_json for each struct, which json(settings) and
// get<Settings>() use. A field is only saved if it's listed here.
// _WITH_DEFAULT: a key the file doesn't have keeps the field's default
// instead of throwing.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ApiSettings, deepl_key)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SpeechSettings, whisper_model, input_device)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, api, speech)

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

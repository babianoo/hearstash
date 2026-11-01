#include "core/anki_connect.h"

#include <format>
#include <stdexcept>

// JSON
#include <nlohmann/json.hpp>

#include "core/http.h"
#include "core/media_encoding.h"

using json = nlohmann::json;

// 127.0.0.1 rather than localhost, which may resolve to ::1 first, where
// AnkiConnect doesn't listen
static constexpr const char* ANKI_CONNECT = "http://127.0.0.1:8765";
// The API version these calls are written for
static constexpr int VERSION = 6;

// Sends action with params, and returns AnkiConnect's "result"
static json request(const std::string& action, const json& params = json::object()){
    const json body = {{"action", action}, {"version", VERSION}, {"params", params}};
    std::string response;
    try {
        response = http_post(ANKI_CONNECT, body.dump(), {"Content-Type: application/json"});
    } catch (const HttpError& e){
        throw std::runtime_error(std::format("AnkiConnect answered with an error: {}", e.what()));
    } catch (const std::runtime_error&){
        // Most likely nothing listens on the port
        throw std::runtime_error("Couldn't reach Anki. Check that it's open, with the AnkiConnect add-on installed.");
    }

    try {
        const json answer = json::parse(response);
        if (!answer.at("error").is_null())
            throw std::runtime_error(std::format("Anki said: {}", answer.at("error").get<std::string>()));
        return answer.at("result");
    } catch (const json::exception& e){
        throw std::runtime_error(std::format("Unexpected answer from AnkiConnect: {}", e.what()));
    }
}

static std::vector<std::string> strings(const json& list){
    try {
        return list.get<std::vector<std::string>>();
    } catch (const json::exception& e){
        throw std::runtime_error(std::format("Unexpected answer from AnkiConnect: {}", e.what()));
    }
}

std::vector<std::string> anki_deck_names(){
    return strings(request("deckNames"));
}

std::vector<std::string> anki_model_names(){
    return strings(request("modelNames"));
}

std::vector<std::string> anki_model_field_names(const std::string& model){
    return strings(request("modelFieldNames", {{"modelName", model}}));
}

void anki_store_media(const std::string& filename, const std::vector<std::uint8_t>& data){
    request("storeMediaFile", {{"filename", filename}, {"data", base64_encode(data)}});
}

std::int64_t anki_add_note(const AnkiNote& note){
    json fields = json::object();
    for (const auto& [name, value] : note.fields) fields[name] = value;

    const json params = {{"note", {
        {"deckName", note.deck},
        {"modelName", note.model},
        {"fields", fields},
        {"tags", note.tags},
        // Anki's own check, which says so instead of adding it again
        {"options", {{"allowDuplicate", false}}},
    }}};

    const json result = request("addNote", params);
    if (!result.is_number_integer()) throw std::runtime_error("Anki didn't add the note");
    return result.get<std::int64_t>();
}

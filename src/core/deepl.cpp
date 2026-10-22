#include "core/deepl.h"

#include <format>
#include <stdexcept>

// JSON
#include <nlohmann/json.hpp>

#include "core/http.h"

using json = nlohmann::json;

// DeepL's error codes, from https://developers.deepl.com/docs/best-practices/error-handling
static std::string describe_status(long status){
    switch (status){
        case 403: return "DeepL rejected the API key.";
        case 413: return "The text is too long for DeepL.";
        case 429: return "Too many DeepL requests. Wait a moment and try again.";
        case 456: return "DeepL's monthly character quota is used up.";
        default:
            if (status >= 500) return std::format("DeepL is having problems (HTTP {}). Try again later.", status);
            return std::format("DeepL returned HTTP {}.", status);
    }
}

std::string deepl_translate(std::string_view text, const std::string& api_key){
    if (api_key.empty()) throw std::runtime_error("No DeepL API key set.");

    // Free and Pro accounts use different hosts; free keys end in ":fx"
    const std::string url = api_key.ends_with(":fx")
        ? "https://api-free.deepl.com/v2/translate"
        : "https://api.deepl.com/v2/translate";

    // "text" is a list because DeepL can translate several texts at once
    const json request = {
        {"text", {text}},
        {"target_lang", "EN-US"},
    };

    std::string body;
    try {
        body = http_post(url, request.dump(), {
            "Authorization: DeepL-Auth-Key " + api_key,
            "Content-Type: application/json",
        });
    } catch (const HttpError& e){
        throw std::runtime_error(describe_status(e.status));
    }

    // {"translations": [{"detected_source_language": "JA", "text": "..."}]}
    try {
        return json::parse(body).at("translations").at(0).at("text").get<std::string>();
    } catch (const json::exception& e){
        throw std::runtime_error(std::string("Unexpected DeepL response: ") + e.what());
    }
}

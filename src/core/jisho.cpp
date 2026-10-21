#include "core/jisho.h"

#include <stdexcept>

// JSON
#include <nlohmann/json.hpp>

#include "core/http.h"

using json = nlohmann::json;

std::vector<JishoEntry> jisho_search(std::string_view keyword){
    const std::string body = http_get(
        "https://jisho.org/api/v1/search/words?keyword=" + url_encode(keyword));

    // Only the fields we use. value() falls back to the default when a
    // field is missing, since Jisho leaves out e.g. "word" for kana-only
    // words. Anything else unexpected throws json::exception.
    std::vector<JishoEntry> entries;
    try {
        for (const json& item : json::parse(body).at("data")){
            JishoEntry entry;
            entry.is_common = item.value("is_common", false);

            for (const json& jp : item.at("japanese")){
                entry.forms.push_back({
                    .word = jp.value("word", ""),
                    .reading = jp.value("reading", ""),
                });
            }

            for (const json& sense : item.at("senses")){
                entry.senses.push_back({
                    .definitions = sense.value("english_definitions", std::vector<std::string>{}),
                    .parts_of_speech = sense.value("parts_of_speech", std::vector<std::string>{}),
                });
            }

            entries.push_back(std::move(entry));
        }
    } catch (const json::exception& e){
        // Callers only need to catch one exception type
        throw std::runtime_error(std::string("Unexpected Jisho response: ") + e.what());
    }

    return entries;
}

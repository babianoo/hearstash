#include "core/card_format.h"

#include <cctype>
#include <format>

#include "core/anki_fields.h"

// The look of the highlight, from the user's Yomitan template
static constexpr const char* HIGHLIGHT_OPEN = "<span style=\"font-size:1.3em; color: #00828a;\">";
static constexpr const char* HIGHLIGHT_CLOSE = "</span>";
// Yomitan's {glossary-brief} wraps the glossary in this
static constexpr const char* GLOSSARY_OPEN = "<div style=\"text-align: left;\" class=\"yomitan-glossary\"><ol>"
                                             "<li data-dictionary=\"Jisho.org\"><span><ol>";
static constexpr const char* GLOSSARY_CLOSE = "</ol></span></li></ol></div>";
// What comes after the glossary in the template, "<br>{conjugation}<br>{url}",
// with no conjugation and no URL, which Yomitan writes as an empty link
static constexpr const char* AFTER_GLOSSARY = "<br><br><a href=\"\"></a>";

// text as HTML, which Anki's fields are: the characters HTML treats
// specially written out, and line breaks as <br>
static std::string escape(std::string_view text){
    std::string html;
    for (char c : text){
        switch (c){
        case '&': html += "&amp;"; break;
        case '<': html += "&lt;"; break;
        case '>': html += "&gt;"; break;
        case '"': html += "&quot;"; break;
        case '\n': html += "<br>"; break;
        default: html += c;
        }
    }
    return html;
}

static void append(std::string& html, const std::string& part, const char* separator){
    if (part.empty()) return;
    if (!html.empty()) html += separator;
    html += part;
}

// sentence with the first highlight in it in the highlight's span
static std::string highlighted(const std::string& sentence, const std::string& highlight){
    const size_t at = highlight.empty() ? std::string::npos : sentence.find(highlight);
    if (at == std::string::npos) return escape(sentence);
    return escape(sentence.substr(0, at)) + HIGHLIGHT_OPEN + escape(highlight) + HIGHLIGHT_CLOSE
         + escape(sentence.substr(at + highlight.size()));
}

// ① to ⑳, the markers Jitendex numbers senses with; past that, digits
static std::string sense_marker(int n){
    if (n < 1 || n > 20) return std::format("{}.", n);
    const char32_t c = U'①' + static_cast<char32_t>(n - 1);
    // All of them are three bytes in UTF-8
    return {static_cast<char>(0xE0 | (c >> 12)), static_cast<char>(0x80 | ((c >> 6) & 0x3F)),
            static_cast<char>(0x80 | (c & 0x3F))};
}

// The meanings, one a line, as Yomitan's glossary. Empty if there are none.
static std::string glossary(const std::string& meaning){
    std::string senses;
    int n = 0;
    size_t start = 0;
    while (start <= meaning.size()){
        size_t end = meaning.find('\n', start);
        if (end == std::string::npos) end = meaning.size();
        std::string_view line = std::string_view(meaning).substr(start, end - start);
        start = end + 1;

        // Without the "1. " the card window numbers them with
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        size_t digits = 0;
        while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits]))) ++digits;
        if (digits > 0 && digits < line.size() && line[digits] == '.') line.remove_prefix(digits + 1);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.remove_suffix(1);
        if (line.empty()) continue;

        senses += std::format("<li style=\"list-style-type: &quot;{}&quot;;\">{}</li>", sense_marker(++n), escape(line));
    }
    return senses.empty() ? "" : GLOSSARY_OPEN + senses + GLOSSARY_CLOSE;
}

std::vector<std::pair<std::string, std::string>> format_card(const CardContent& card, const AnkiSettings& settings){
    std::vector<std::pair<std::string, std::string>> fields;
    for (size_t p = 0; p < CARD_PART_COUNT; ++p){
        const std::string& field = field_for(settings, static_cast<CardPart>(p));
        if (field.empty()) continue;
        // Each field once, made when its first part comes up
        bool done = false;
        for (const auto& [name, html] : fields) done = done || name == field;
        if (done) continue;

        const auto here = [&](CardPart part){ return field_for(settings, part) == field; };
        std::string html;

        if (here(CardPart::Sentence) && !card.sentence.empty()) html = highlighted(card.sentence, card.highlight);

        // {expression}【{reading}】
        std::string head;
        if (here(CardPart::Word) && !card.word.empty()){
            head = escape(card.word);
            if (here(CardPart::Reading) && !card.reading.empty()) head += "【" + escape(card.reading) + "】";
        } else if (here(CardPart::Reading)){
            head = escape(card.reading);
        }
        append(html, head, "<br>");

        const std::string gloss = here(CardPart::Meaning) ? glossary(card.meaning) : "";
        if (!gloss.empty()){
            // After the word, the template's "{audio} {pitch-accents}<br>",
            // which has neither
            if (!head.empty()) html += " <br>";
            else if (!html.empty()) html += "<br>";
            html += gloss + AFTER_GLOSSARY;
        }

        if (here(CardPart::Audio) && !card.audio_file.empty())
            append(html, "[sound:" + card.audio_file + "]", "<br>");
        if (here(CardPart::Picture) && !card.picture_file.empty())
            append(html, "<img alt=\"snapshot\" src=\"" + escape(card.picture_file) + "\">", "<br>");

        if (!html.empty()) fields.emplace_back(field, std::move(html));
    }
    return fields;
}

#include "core/card_format.h"

#include <cctype>
#include <format>
#include <string_view>

#include "core/anki_fields.h"

// The look of the highlight, from the user's Yomitan template
static constexpr const char* HIGHLIGHT_OPEN = "<span style=\"font-size:1.3em; color: #00828a;\">";
static constexpr const char* HIGHLIGHT_CLOSE = "</span>";
// Yomitan's {glossary-brief} wraps the glossary in this. Inside it, the
// senses are marked up the way Jitendex's are, which the Hearstash note
// type styles (hearstash_note_type.cpp): groups of senses under their
// parts of speech, and in each sense a list of its glosses.
static constexpr const char* GLOSSARY_OPEN = "<div style=\"text-align: left;\" class=\"yomitan-glossary\"><ol>"
                                             "<li data-dictionary=\"Jisho.org\"><span>"
                                             "<ul data-sc-content=\"sense-groups\">";
static constexpr const char* GLOSSARY_CLOSE = "</ul></span></li></ol></div>";
static constexpr const char* GROUP_OPEN = "<li data-sc-content=\"sense-group\">";
static constexpr const char* TAG_OPEN = "<span data-sc-class=\"tag\" data-sc-content=\"part-of-speech-info\">";
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

static std::string_view trimmed(std::string_view text){
    const auto blank = [](char c){ return c == ' ' || c == '\t' || c == '\r'; };
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && blank(text.back())) text.remove_suffix(1);
    return text;
}

// The meanings as Yomitan's glossary: see CardContent::meaning for how
// they're written. Empty if there are none.
static std::string glossary(const std::string& meaning){
    std::string groups;
    // The group being read: its parts of speech and its senses. It's
    // added to groups when the next one starts, unless it has no senses.
    std::string tags, senses;
    const auto end_group = [&]{
        if (!senses.empty()) groups += GROUP_OPEN + tags + "<ol>" + senses + "</ol></li>";
        tags.clear();
        senses.clear();
    };

    int n = 0;
    size_t start = 0;
    while (start <= meaning.size()){
        size_t end = meaning.find('\n', start);
        if (end == std::string::npos) end = meaning.size();
        std::string_view line = trimmed(std::string_view(meaning).substr(start, end - start));
        start = end + 1;
        if (line.empty()) continue;

        // "[Noun] [Suru verb]": the parts of speech of the senses below
        if (line.front() == '[' && line.back() == ']'){
            end_group();
            while (!line.empty()){
                const size_t close = line.find(']');
                const std::string_view tag = trimmed(line.substr(1, close - 1));
                if (!tag.empty()) tags += TAG_OPEN + escape(tag) + "</span>";
                const size_t open = line.find('[', close);
                line = open == std::string_view::npos ? std::string_view() : line.substr(open);
            }
            continue;
        }

        // Without the "1. " the card window numbers them with
        size_t digits = 0;
        while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits]))) ++digits;
        if (digits > 0 && digits < line.size() && line[digits] == '.') line.remove_prefix(digits + 1);

        std::string glosses;
        while (!line.empty()){
            const size_t semicolon = line.find(';');
            const std::string_view gloss = trimmed(line.substr(0, semicolon));
            if (!gloss.empty()) glosses += "<li>" + escape(gloss) + "</li>";
            line = semicolon == std::string_view::npos ? std::string_view() : line.substr(semicolon + 1);
        }
        if (glosses.empty()) continue;

        senses += std::format("<li data-sc-content=\"sense\" style=\"list-style-type: &quot;{}&quot;;\">"
                              "<ul data-sc-content=\"glossary\">{}</ul></li>", sense_marker(++n), glosses);
    }
    end_group();
    return groups.empty() ? "" : GLOSSARY_OPEN + groups + GLOSSARY_CLOSE;
}

std::string card_glossary(std::string_view dictionary, std::string_view definitions, std::string_view styles){
    if (definitions.empty()) return "";
    std::string html = "<div style=\"text-align: left;\" class=\"yomitan-glossary\"><ol><li data-dictionary=\"";
    html += escape(dictionary);
    html += "\"><span>";
    html += definitions;
    html += "</span></li></ol>";
    // Nothing in a stylesheet closes a tag, so one that does isn't one
    if (!styles.empty() && styles.find("</") == std::string_view::npos){
        html += "<style>";
        html += styles;
        html += "</style>";
    }
    return html + "</div>";
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

        const std::string gloss = !here(CardPart::Meaning) ? ""
                                : card.glossary_html.empty() ? glossary(card.meaning) : card.glossary_html;
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

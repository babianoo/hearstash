#include "core/structured_content.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <iterator>
#include <vector>

// JSON
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ---- HTML ----

// text with the characters HTML treats specially written out. In an
// attribute a line break stays one; in text it becomes a <br>.
static std::string escape(std::string_view text, bool in_attribute){
    std::string html;
    for (char c : text){
        switch (c){
        case '&': html += "&amp;"; break;
        case '<': html += "&lt;"; break;
        case '>': html += "&gt;"; break;
        case '"': html += "&quot;"; break;
        case '\n': html += in_attribute ? "\n" : "<br>"; break;
        default: html += c;
        }
    }
    return html;
}

// ` name="value"`, with the space in front
static std::string attribute(std::string_view name, std::string_view value){
    return std::format(" {}=\"{}\"", name, escape(value, true));
}

// "listStyleType" as "list-style-type", the way both CSS and data-*
// attributes spell what JSON writes in camel case
static std::string kebab(std::string_view name){
    std::string out;
    for (char c : name){
        if (std::isupper(static_cast<unsigned char>(c))){
            out += '-';
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            out += c;
        }
    }
    return out;
}

// node's key if it's text, otherwise nothing
static std::string text_at(const json& node, const char* key){
    const auto found = node.find(key);
    return found != node.end() && found->is_string() ? found->get<std::string>() : "";
}

// ---- Attributes ----

// {"data": {"content": "sense", "scCode": "n"}} as
// ` data-sc-content="sense" data-sc-sc-code="n"`
static std::string data_attributes(const json& node){
    const auto data = node.find("data");
    if (data == node.end() || !data->is_object()) return "";

    std::string attributes;
    for (const auto& [key, value] : data->items()){
        // A name that couldn't be an attribute's is skipped
        const bool valid = !key.empty() && std::ranges::all_of(key, [](unsigned char c){
            return std::isalnum(c) || c == '-' || c == '_';
        });
        if (valid && value.is_string()) attributes += attribute("data-sc-" + kebab(key), value.get<std::string>());
    }
    return attributes;
}

// The style properties the format has, which are CSS's own in camel case
static constexpr const char* STYLE_PROPERTIES[] = {
    "fontStyle", "fontWeight", "fontSize", "color", "background", "backgroundColor",
    "textDecorationLine", "textDecorationStyle", "textDecorationColor",
    "borderColor", "borderStyle", "borderRadius", "borderWidth", "clipPath",
    "verticalAlign", "textAlign", "textEmphasis", "textShadow",
    "margin", "marginTop", "marginLeft", "marginRight", "marginBottom",
    "padding", "paddingTop", "paddingLeft", "paddingRight", "paddingBottom",
    "wordBreak", "whiteSpace", "cursor", "listStyleType",
};

// Whether value can go in a style attribute as one property's value: not
// if it could end the property and start another, or load something
static bool is_plain_css_value(std::string_view value){
    if (value.empty() || value.find_first_of(";{}<>\\\n\r") != std::string_view::npos) return false;
    std::string lower;
    for (char c : value) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("url(") == std::string::npos && lower.find("expression(") == std::string::npos;
}

// A property's value as CSS writes it, or nothing if it isn't one. Where
// the format takes a number, as it does for the margins, it's in em.
static std::string css_value(const json& value){
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number()) return std::format("{}em", value.get<double>());
    // textDecorationLine can be several: ["underline", "overline"]
    if (value.is_array()){
        std::string joined;
        for (const json& part : value){
            if (!part.is_string()) return "";
            if (!joined.empty()) joined += ' ';
            joined += part.get<std::string>();
        }
        return joined;
    }
    return "";
}

// {"style": {"fontWeight": "bold"}} as ` style="font-weight: bold;"`,
// along with what the caller already has to put in it
static std::string style_attribute(const json& node, std::string style = ""){
    const auto found = node.find("style");
    if (found != node.end() && found->is_object()){
        for (const char* property : STYLE_PROPERTIES){
            const auto value = found->find(property);
            if (value == found->end()) continue;
            const std::string css = css_value(*value);
            if (!is_plain_css_value(css)) continue;
            if (!style.empty()) style += ' ';
            style += std::format("{}: {};", kebab(property), css);
        }
    }
    return style.empty() ? "" : attribute("style", style);
}

// ---- Elements ----

// The elements that hold content, which all take the same attributes
static constexpr const char* CONTAINERS[] = {
    "ruby", "rt", "rp", "table", "thead", "tbody", "tfoot", "tr", "td", "th",
    "span", "div", "ol", "ul", "li", "details", "summary",
};

// ` colspan="2"` for {"colSpan": 2}
static std::string span_attribute(const json& node, const char* key, const char* name){
    const auto found = node.find(key);
    if (found == node.end() || !found->is_number_integer() || found->get<long long>() < 1) return "";
    return attribute(name, std::to_string(found->get<long long>()));
}

// An image, sized in default_units unless it says which. Nothing if the
// caller has no file for it.
static std::string image(const json& node, const ContentOptions& options, const char* default_units){
    const std::string path = text_at(node, "path");
    if (path.empty() || !options.image_src) return "";
    const std::string src = options.image_src(path);
    if (src.empty()) return "";

    std::string units = text_at(node, "sizeUnits");
    if (units != "px" && units != "em") units = default_units;

    std::string style;
    const auto add = [&](const char* property, const std::string& value){
        if (!is_plain_css_value(value)) return;
        if (!style.empty()) style += ' ';
        style += std::format("{}: {};", property, value);
    };
    for (const char* side : {"width", "height"}){
        const auto size = node.find(side);
        if (size != node.end() && size->is_number() && size->get<double>() > 0)
            add(side, std::format("{}{}", size->get<double>(), units));
    }
    add("vertical-align", text_at(node, "verticalAlign"));
    add("border", text_at(node, "border"));
    add("border-radius", text_at(node, "borderRadius"));
    // "pixelated": true is the older way of saying it
    const auto pixelated = node.find("pixelated");
    std::string rendering = text_at(node, "imageRendering");
    if (rendering.empty() && pixelated != node.end() && pixelated->is_boolean() && pixelated->get<bool>())
        rendering = "pixelated";
    if (rendering != "auto") add("image-rendering", rendering);

    // Read out, or shown if the picture can't be
    std::string alt = text_at(node, "alt");
    if (alt.empty()) alt = text_at(node, "description");

    std::string html = "<img" + attribute("src", src) + data_attributes(node);
    if (!alt.empty()) html += attribute("alt", alt);
    if (const std::string title = text_at(node, "title"); !title.empty()) html += attribute("title", title);
    if (!style.empty()) html += attribute("style", style);
    return html + ">";
}

static void render(const json& node, const ContentOptions& options, std::string& html){
    if (node.is_string()){
        html += escape(node.get_ref<const std::string&>(), false);
        return;
    }
    if (node.is_array()){
        for (const json& child : node) render(child, options, html);
        return;
    }
    if (!node.is_object()) return;

    const std::string tag = text_at(node, "tag");
    const auto content = node.find("content");
    if (tag == "br"){
        html += "<br>";
        return;
    }
    if (tag == "img"){
        // In the line of text it's in, so sized by the text
        html += image(node, options, "em");
        return;
    }

    std::string name = tag;
    std::string attributes;
    if (tag == "a"){
        // A link to a web page is kept. The other kind, "?query=...",
        // looks a word up in Yomitan, which a card can't do, so only its
        // text is.
        const std::string href = text_at(node, "href");
        if (href.starts_with("https://") || href.starts_with("http://")) attributes += attribute("href", href);
        else name = "span";
    } else if (std::ranges::find(CONTAINERS, tag) == std::end(CONTAINERS)){
        // Not in the format: no element, just what it holds
        if (content != node.end()) render(*content, options, html);
        return;
    }

    attributes += data_attributes(node);
    if (const std::string lang = text_at(node, "lang"); !lang.empty()) attributes += attribute("lang", lang);
    if (tag == "td" || tag == "th"){
        attributes += span_attribute(node, "colSpan", "colspan");
        attributes += span_attribute(node, "rowSpan", "rowspan");
    }
    attributes += style_attribute(node);
    if (const std::string title = text_at(node, "title"); !title.empty()) attributes += attribute("title", title);
    if (const auto open = node.find("open"); tag == "details" && open != node.end() && open->is_boolean() && open->get<bool>())
        attributes += " open";

    html += "<" + name + attributes + ">";
    if (content != node.end()) render(*content, options, html);
    html += "</" + name + ">";
}

std::string render_content(const json& content, const ContentOptions& options){
    std::string html;
    render(content, options, html);
    return html;
}

std::string render_definitions(const json& definitions, const ContentOptions& options){
    if (!definitions.is_array()) return "";

    std::vector<std::string> parts;
    for (const json& definition : definitions){
        std::string html;
        if (definition.is_string()){
            html = escape(definition.get_ref<const std::string&>(), false);
        } else if (definition.is_object()){
            const std::string type = text_at(definition, "type");
            if (type == "text") html = escape(text_at(definition, "text"), false);
            else if (type == "structured-content" && definition.contains("content"))
                html = render_content(definition["content"], options);
            // On a line of its own, so sized as the dictionary drew it
            else if (type == "image") html = image(definition, options, "px");
        }
        // The one other kind, ["食べる", ["past"]], says which word this
        // is a form of instead of what it means
        if (!html.empty()) parts.push_back(std::move(html));
    }

    if (parts.empty()) return "";
    if (parts.size() == 1) return parts.front();
    std::string list = "<ul>";
    for (const std::string& part : parts) list += "<li>" + part + "</li>";
    return list + "</ul>";
}

// ---- Stylesheet ----

static std::string_view trimmed(std::string_view text){
    const auto blank = [](char c){ return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; };
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && blank(text.back())) text.remove_suffix(1);
    return text;
}

// Where the string or comment starting at css[at] ends: the position
// after it. at itself if neither starts there.
static size_t skip_quoted(std::string_view css, size_t at){
    if (css.substr(at).starts_with("/*")){
        const size_t end = css.find("*/", at + 2);
        return end == std::string_view::npos ? css.size() : end + 2;
    }
    if (css[at] != '"' && css[at] != '\'') return at;
    for (size_t i = at + 1; i < css.size(); ++i){
        if (css[i] == '\\') ++i;
        else if (css[i] == css[at]) return i + 1;
    }
    return css.size();
}

// The first of any of stops in css from at on that's not in a string, a
// comment, or brackets opened after at. css.size() if there's none.
static size_t find_outside(std::string_view css, size_t at, std::string_view stops){
    int depth = 0;
    while (at < css.size()){
        const size_t after = skip_quoted(css, at);
        if (after != at){
            at = after;
            continue;
        }
        const char c = css[at];
        if (depth == 0 && stops.find(c) != std::string_view::npos) return at;
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if ((c == ')' || c == ']' || c == '}') && depth > 0) --depth;
        ++at;
    }
    return css.size();
}

// The rules in css, each selector with prefix in front
static std::string scoped(std::string_view css, const std::string& prefix){
    std::string out;
    size_t at = 0;
    while (at < css.size()){
        // A rule is "selectors { ... }"; an @import or such ends at a ';'
        const size_t open = find_outside(css, at, "{;");
        if (open == css.size()) break;
        std::string_view head = trimmed(css.substr(at, open - at));
        // Without the comments in front of it
        while (head.starts_with("/*")) head = trimmed(head.substr(skip_quoted(head, 0)));
        if (css[open] == ';'){
            at = open + 1;
            continue;
        }
        const size_t close = find_outside(css, open + 1, "}");
        const std::string_view body = css.substr(open + 1, close - open - 1);
        at = close == css.size() ? close : close + 1;

        if (head.starts_with("@")){
            // These hold rules of their own, which are scoped in turn.
            // Others, like @font-face, are kept as they are.
            const bool holds_rules = head.starts_with("@media") || head.starts_with("@supports")
                                  || head.starts_with("@container") || head.starts_with("@layer");
            out += std::format("{} {{{}}}\n", head, holds_rules ? "\n" + scoped(body, prefix) : std::string(body));
            continue;
        }

        std::string selectors;
        size_t s = 0;
        while (s <= head.size()){
            const size_t comma = find_outside(head, s, ",");
            const std::string_view selector = trimmed(head.substr(s, comma - s));
            s = comma + 1;
            if (selector.empty()) continue;
            if (!selectors.empty()) selectors += ", ";
            selectors += prefix;
            selectors += ' ';
            selectors += selector;
        }
        if (!selectors.empty()) out += std::format("{} {{{}}}\n", selectors, body);
    }
    return out;
}

std::string scope_styles(std::string_view css, std::string_view dictionary){
    // The title as a CSS string
    std::string title;
    for (char c : dictionary){
        if (c == '"' || c == '\\') title += '\\';
        // A line break can't be in one; a title wouldn't have it anyway
        title += c == '\n' || c == '\r' ? ' ' : c;
    }
    return scoped(css, std::format(".yomitan-glossary [data-dictionary=\"{}\"]", title));
}

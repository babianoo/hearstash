// Tests for render_content(), render_definitions() and scope_styles(). No
// framework, like sample_ring_test.cpp. The content is made up, in the
// shapes dictionaries use.

#include <cstdio>
#include <string>

// JSON
#include <nlohmann/json.hpp>

#include "core/structured_content.h"

using json = nlohmann::json;

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static std::string render(const char* content, const ContentOptions& options = {}){
    return render_content(json::parse(content), options);
}

static void test_text_and_lists(){
    CHECK(render(R"j("a < b & \"c\"")j") == "a &lt; b &amp; &quot;c&quot;");
    // A line break in text is one on the card
    CHECK(render(R"j("one\ntwo")j") == "one<br>two");
    CHECK(render(R"j(["one", {"tag": "br"}, ["two", "three"]])j") == "one<br>twothree");
    // Not content at all
    CHECK(render("12") == "");
    CHECK(render("null") == "");
}

// A sense the way dictionaries mark one up: data for the stylesheet, a
// style of its own, a hover text, and a language
static void test_elements(){
    CHECK(render(R"j({"tag": "span", "data": {"content": "part-of-speech-info", "class": "tag", "code": "n"},
                     "title": "noun (common)", "content": "noun"})j") ==
          "<span data-sc-class=\"tag\" data-sc-code=\"n\" data-sc-content=\"part-of-speech-info\""
          " title=\"noun (common)\">noun</span>");
    CHECK(render(R"j({"tag": "li", "data": {"content": "sense"}, "style": {"listStyleType": "\"①\""},
                     "content": {"tag": "ul", "lang": "ja", "content": [{"tag": "li", "content": "paper"}]}})j") ==
          "<li data-sc-content=\"sense\" style=\"list-style-type: &quot;①&quot;;\">"
          "<ul lang=\"ja\"><li>paper</li></ul></li>");
    CHECK(render(R"j({"tag": "ruby", "content": ["紙", {"tag": "rt", "content": "かみ"}]})j") ==
          "<ruby>紙<rt>かみ</rt></ruby>");
    // Camel case in data is spelled the way a data-* attribute is
    CHECK(render(R"j({"tag": "div", "data": {"extraBox": "x", "bad key": "y", "n": 1}})j") ==
          "<div data-sc-extra-box=\"x\"></div>");

    CHECK(render(R"j({"tag": "td", "colSpan": 2, "rowSpan": 3, "content": "x"})j") == "<td colspan=\"2\" rowspan=\"3\">x</td>");
    CHECK(render(R"j({"tag": "td", "colSpan": 0, "content": "x"})j") == "<td>x</td>");
    CHECK(render(R"j({"tag": "details", "open": true, "content": {"tag": "summary", "content": "more"}})j") ==
          "<details open><summary>more</summary></details>");
    CHECK(render(R"j({"tag": "details", "open": false})j") == "<details></details>");
}

static void test_styles(){
    // In the format's order, numbers in em, several decoration lines
    CHECK(render(R"j({"tag": "span", "style": {"marginTop": 0.5, "marginLeft": "2px", "fontWeight": "bold",
                     "textDecorationLine": ["underline", "overline"]}, "content": "x"})j") ==
          "<span style=\"font-weight: bold; text-decoration-line: underline overline;"
          " margin-top: 0.5em; margin-left: 2px;\">x</span>");
    // Not the format's, or not just a value: left out
    CHECK(render(R"j({"tag": "span", "style": {"position": "fixed", "color": "red; position: fixed",
                     "background": "url(http://example.com/a.png)", "cursor": "help"}, "content": "x"})j") ==
          "<span style=\"cursor: help;\">x</span>");
    CHECK(render(R"j({"tag": "span", "style": {"color": 3}, "content": "x"})j") == "<span style=\"color: 3em;\">x</span>");
    CHECK(render(R"j({"tag": "span", "style": {}, "content": "x"})j") == "<span>x</span>");
}

static void test_links_and_unknown_tags(){
    CHECK(render(R"j({"tag": "a", "href": "https://example.com/?a=1&b=2", "content": "source"})j") ==
          "<a href=\"https://example.com/?a=1&amp;b=2\">source</a>");
    // A lookup in Yomitan: just its text
    CHECK(render(R"j({"tag": "a", "href": "?query=紙&wildcards=off", "lang": "ja", "content": "紙"})j") ==
          "<span lang=\"ja\">紙</span>");
    CHECK(render(R"j({"tag": "a", "href": "javascript:alert(1)", "content": "x"})j") == "<span>x</span>");
    // Nothing a dictionary says becomes an element the format doesn't have
    CHECK(render(R"j({"tag": "script", "content": "alert(1)"})j") == "alert(1)");
    CHECK(render(R"j({"tag": "span onclick=\"x\"", "content": "y"})j") == "y");
    CHECK(render(R"j({"content": "no tag"})j") == "no tag");
}

static void test_images(){
    const char* img = R"j({"tag": "img", "path": "img/kami.png", "width": 2, "height": 1.5, "alt": "a sheet",
                          "verticalAlign": "middle", "pixelated": true})j";
    // Left out unless the caller has the file
    CHECK(render(img) == "");
    CHECK(render(img, {.image_src = [](std::string_view){ return std::string(); }}) == "");
    const ContentOptions stored{.image_src = [](std::string_view path){
        return path == "img/kami.png" ? std::string("dict_kami.png") : std::string();
    }};
    CHECK(render(img, stored) ==
          "<img src=\"dict_kami.png\" alt=\"a sheet\""
          " style=\"width: 2em; height: 1.5em; vertical-align: middle; image-rendering: pixelated;\">");
    CHECK(render(R"j({"tag": "img", "path": "img/kami.png", "width": 40, "sizeUnits": "px", "description": "d"})j", stored) ==
          "<img src=\"dict_kami.png\" alt=\"d\" style=\"width: 40px;\">");
}

static void test_definitions(){
    const auto definitions = [](const char* list){ return render_definitions(json::parse(list)); };
    CHECK(definitions(R"j(["paper"])j") == "paper");
    CHECK(definitions(R"j([{"type": "structured-content", "content": {"tag": "div", "content": "paper"}}])j") ==
          "<div>paper</div>");
    // Several are a list. What says which word this is a form of isn't one.
    CHECK(definitions(R"j(["paper", {"type": "text", "text": "a < b"}, ["紙", ["plural"]],
                          {"type": "image", "path": "a.png"}])j") ==
          "<ul><li>paper</li><li>a &lt; b</li></ul>");
    CHECK(definitions("[]") == "");
    CHECK(definitions(R"j("paper")j") == "");

    // On its own line, an image is in pixels
    const ContentOptions stored{.image_src = [](std::string_view){ return std::string("a.png"); }};
    CHECK(render_definitions(json::parse(R"j([{"type": "image", "path": "a.png", "width": 100, "height": 50}])j"), stored) ==
          "<img src=\"a.png\" style=\"width: 100px; height: 50px;\">");
}

static void test_scope_styles(){
    const std::string in = ".yomitan-glossary [data-dictionary=\"Dict [1]\"]";
    CHECK(scope_styles("span[title] { cursor: help; }", "Dict [1]") == in + " span[title] { cursor: help; }\n");
    // Each selector of a list, but not a comma inside one
    CHECK(scope_styles("li, div:is(.a, .b),\n ul[data-x=\"a,b{\"] {color: red}", "Dict [1]") ==
          in + " li, " + in + " div:is(.a, .b), " + in + " ul[data-x=\"a,b{\"] {color: red}\n");
    // Rules inside a rule are left to the rule they're in
    CHECK(scope_styles("li { padding: 0; & ul { margin: 0; } }\ndiv { color: red; }", "Dict [1]") ==
          in + " li { padding: 0; & ul { margin: 0; } }\n" + in + " div { color: red; }\n");
    // Inside @media too; nothing is fetched; a comment isn't a selector
    CHECK(scope_styles("@import url(\"http://example.com/a.css\");\n/* small; screens */\n"
                       "@media (max-width: 10em) { li { color: red; } }", "Dict [1]") ==
          "@media (max-width: 10em) {\n" + in + " li { color: red; }\n}\n");
    CHECK(scope_styles("@font-face { font-family: X; }", "Dict [1]") == "@font-face { font-family: X; }\n");
    CHECK(scope_styles("li { content: \"}\"; }", "a\"b") ==
          ".yomitan-glossary [data-dictionary=\"a\\\"b\"] li { content: \"}\"; }\n");
    // Cut off: what's whole is kept
    CHECK(scope_styles("li { color: red; } div { color", "D") == ".yomitan-glossary [data-dictionary=\"D\"] li { color: red; }\n"
                                                                  ".yomitan-glossary [data-dictionary=\"D\"] div { color}\n");
    CHECK(scope_styles("", "D") == "");
}

int main(){
    test_text_and_lists();
    test_elements();
    test_styles();
    test_links_and_unknown_tags();
    test_images();
    test_definitions();
    test_scope_styles();
    if (failures == 0) std::puts("All structured content tests passed");
    return failures;
}

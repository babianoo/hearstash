// Tests for core/ocr_text.h. No framework, like sample_ring_test.cpp.

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/ocr_text.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void test_detection_size(){
    // A 1920x1080 screen: the long side down to 960, both multiples of 32
    CHECK(detection_size(1920, 1080) == std::pair(960, 544));
    // Small enough already: only rounded to 32
    CHECK(detection_size(666, 300) == std::pair(672, 288));
    // Never less than 32
    CHECK(detection_size(10, 5) == std::pair(32, 32));
    // Tall
    CHECK(detection_size(500, 2000) == std::pair(256, 960));
}

static void test_detection_padding(){
    CHECK(detection_padding(1920, 1080) == 0);
    CHECK(detection_padding(400, 100) == 0);
    // One line, 27 times as wide as tall: padded to twice 1750/8
    CHECK(detection_padding(1750, 65) == (218 * 2 - 65) / 2);
    // Short: padded to 60
    CHECK(detection_padding(100, 20) == 20);
}

// A map_w x map_h map with the rectangles in on set to sure
static std::vector<float> map_with(int map_w, int map_h, const std::vector<Rect>& on, float value = 0.9f){
    std::vector<float> map(static_cast<size_t>(map_w) * map_h, 0.0f);
    for (const Rect& r : on)
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) map[static_cast<size_t>(y) * map_w + x] = value;
    return map;
}

static void test_boxes_from_map(){
    // Two lines on a 100x60 map, of a 200x120 image
    const std::vector<float> map = map_with(100, 60, {{.x = 10, .y = 10, .w = 50, .h = 8}, {.x = 10, .y = 30, .w = 30, .h = 8}});
    const std::vector<Rect> boxes = boxes_from_map(map, 100, 60, 200, 120);
    CHECK(boxes.size() == 2);
    if (boxes.size() == 2){
        // Grown beyond the marked pixels, and scaled to the image
        const Rect& a = boxes[0];
        CHECK(a.x < 20 && a.y < 20);
        CHECK(a.x + a.w > 2 * 59 && a.y + a.h > 2 * 17);
        CHECK(a.x >= 0 && a.y >= 0 && a.x + a.w <= 199 && a.y + a.h <= 119);
        CHECK(boxes[1].y > a.y + a.h / 2);
    }

    // Too thin to be text
    CHECK(boxes_from_map(map_with(100, 60, {{.x = 10, .y = 10, .w = 50, .h = 2}}), 100, 60, 100, 60).empty());
    // Not sure enough on average
    CHECK(boxes_from_map(map_with(100, 60, {{.x = 10, .y = 10, .w = 50, .h = 8}}, 0.3f), 100, 60, 100, 60).empty());
    // Characters a pixel apart join up into one line
    const std::vector<float> chars = map_with(100, 60, {{.x = 10, .y = 10, .w = 8, .h = 8}, {.x = 19, .y = 10, .w = 8, .h = 8}});
    CHECK(boxes_from_map(chars, 100, 60, 100, 60).size() == 1);
    // A map that doesn't match its size gives nothing
    CHECK(boxes_from_map(map, 100, 70, 100, 70).empty());
}

static void test_is_vertical(){
    CHECK(is_vertical({.x = 0, .y = 0, .w = 40, .h = 300}));
    CHECK(!is_vertical({.x = 0, .y = 0, .w = 300, .h = 40}));
    // One character, about square: read as is
    CHECK(!is_vertical({.x = 0, .y = 0, .w = 40, .h = 44}));
}

static void test_recognition_width(){
    // Scaled to 48 tall: 10x as wide
    CHECK(recognition_width(600, 60) == std::pair(480, 480));
    // Short ones are padded to 320
    CHECK(recognition_width(100, 48) == std::pair(100, 320));
    CHECK(recognition_width(0, 10) == std::pair(1, 320));
}

static void test_parse_character_list(){
    const std::string yml =
        "Global:\n"
        "  model_name: PP-OCRv6_small_rec\n"
        "PostProcess:\n"
        "  name: CTCLabelDecode\n"
        "  character_dict:\n"
        "  - '!'\n"
        "  - ''''\n"
        "  - $\n"
        "  - \\\n"
        "  - あ\n"
        "  - \"\\u3000\"\n"
        "  - '-'\n"
        "Other: 1\n";
    const std::vector<std::string> chars = parse_character_list(yml);
    CHECK(chars.size() == 9);
    if (chars.size() == 9){
        CHECK(chars[0].empty());        // The blank
        CHECK(chars[1] == "!");
        CHECK(chars[2] == "'");
        CHECK(chars[3] == "$");
        CHECK(chars[4] == "\\");
        CHECK(chars[5] == "あ");
        CHECK(chars[6] == "\u3000");
        CHECK(chars[7] == "-");
        CHECK(chars[8] == " ");         // Added at the end
    }

    bool threw = false;
    try {
        parse_character_list("Global:\n  model_name: x\n");
    } catch (const std::runtime_error&){
        threw = true;
    }
    CHECK(threw);
}

static void test_decode_line(){
    const std::vector<std::string> chars{"", "a", "b", " "};
    // Steps reading a a _ a b b _: "aab"
    const int best[] = {1, 1, 0, 1, 2, 2, 0};
    std::vector<float> scores;
    for (int b : best)
        for (int c = 0; c < 4; ++c) scores.push_back(c == b ? 0.9f : 0.03f);
    CHECK(decode_line(scores, 7, 4, chars) == "aab");
    CHECK(decode_line(scores, 0, 4, chars).empty());
    // Too few scores for the steps
    CHECK(decode_line(scores, 8, 4, chars).empty());
}

static void test_drop_furigana(){
    // まと over 纏, in a line of text
    const Rect line{.x = 100, .y = 360, .w = 1400, .h = 40};
    const Rect furigana{.x = 1010, .y = 335, .w = 36, .h = 15};
    const Rect next_line{.x = 100, .y = 420, .w = 400, .h = 40};
    const std::vector<Rect> kept = drop_furigana({line, furigana, next_line}, false);
    CHECK(kept.size() == 2);
    CHECK(kept.size() == 2 && kept[0] == line && kept[1] == next_line);

    // Vertical: to the right of the column
    const Rect column{.x = 500, .y = 100, .w = 40, .h = 600};
    const Rect ruby{.x = 542, .y = 300, .w = 15, .h = 40};
    CHECK(drop_furigana({column, ruby}, true).size() == 1);
    // A smaller line well away from the others stays
    CHECK(drop_furigana({line, {.x = 100, .y = 100, .w = 200, .h = 15}}, false).size() == 2);
}

static void test_reading_order(){
    // Two rows; the first split in two at a gap, given out of order
    const std::vector<Rect> boxes{
        {.x = 400, .y = 102, .w = 300, .h = 40},  // First row, right part
        {.x = 100, .y = 160, .w = 500, .h = 40},  // Second row
        {.x = 100, .y = 100, .w = 280, .h = 42},  // First row, left part
    };
    const auto rows = reading_order(boxes, false);
    CHECK(rows.size() == 2);
    CHECK(rows.size() == 2 && rows[0] == std::vector<size_t>({2, 0}) && rows[1] == std::vector<size_t>({1}));

    // Vertical: columns right to left, each top to bottom
    const std::vector<Rect> columns{
        {.x = 100, .y = 100, .w = 40, .h = 500},
        {.x = 200, .y = 300, .w = 40, .h = 200},
        {.x = 200, .y = 100, .w = 40, .h = 180},
    };
    const auto cols = reading_order(columns, true);
    CHECK(cols.size() == 2);
    CHECK(cols.size() == 2 && cols[0] == std::vector<size_t>({2, 1}) && cols[1] == std::vector<size_t>({0}));
    CHECK(reading_order({}, false).empty());
}

static void test_layout_lines(){
    // Your sample: the same paragraph in vertical columns on the right,
    // from the top, and in horizontal lines on the left, lower down
    const std::vector<Rect> boxes{
        {.x = 0, .y = 140, .w = 320, .h = 22},     // 0: first horizontal line
        {.x = 470, .y = 10, .w = 22, .h = 320},    // 1: first column, rightmost
        {.x = 0, .y = 170, .w = 320, .h = 22},     // 2: second line
        {.x = 440, .y = 10, .w = 22, .h = 320},    // 3: second column
        {.x = 410, .y = 10, .w = 22, .h = 250},    // 4: third column, shorter
        {.x = 410, .y = 262, .w = 22, .h = 22},    // 5: its last character, square
        {.x = 0, .y = 200, .w = 22, .h = 22},      // 6: a line of one character
    };
    const std::vector<TextLine> lines = layout_lines(boxes);
    CHECK(lines.size() == 6);
    if (lines.size() == 6){
        // The columns first, since they start higher, right to left
        CHECK(lines[0].vertical && lines[0].boxes == std::vector<size_t>({1}));
        CHECK(lines[1].vertical && lines[1].boxes == std::vector<size_t>({3}));
        // The square box goes with the column it's under
        CHECK(lines[2].vertical && lines[2].boxes == std::vector<size_t>({4, 5}));
        CHECK(!lines[3].vertical && lines[3].boxes == std::vector<size_t>({0}));
        CHECK(!lines[4].vertical && lines[4].boxes == std::vector<size_t>({2}));
        // On its own, a square box is horizontal
        CHECK(!lines[5].vertical && lines[5].boxes == std::vector<size_t>({6}));
    }

    // Furigana is still dropped, from either
    const std::vector<TextLine> with_ruby = layout_lines({{.x = 100, .y = 360, .w = 1400, .h = 40},
                                                          {.x = 1010, .y = 335, .w = 36, .h = 15}});
    CHECK(with_ruby.size() == 1 && with_ruby[0].boxes == std::vector<size_t>({0}));
    CHECK(layout_lines({}).empty());
}

static void test_tidy_line(){
    // The Chinese form the model reads for 躱
    CHECK(tidy_line("躲そうとする試みは無意味だろう。") == "躱そうとする試みは無意味だろう。");
    // The "next" arrow, read as a letter or symbol
    CHECK(tidy_line("そう、契約は完了した。V") == "そう、契約は完了した。");
    CHECK(tidy_line("されど。↓") == "されど。");
    CHECK(tidy_line("「後の事は解っているな」 >") == "「後の事は解っているな」");
    CHECK(tidy_line("↓").empty());
    // Not after the sentence's end: a real word
    CHECK(tidy_line("HPが減った") == "HPが減った");
    CHECK(tidy_line("レベルはLV") == "レベルはLV");
    // More than two after it: kept
    CHECK(tidy_line("終わり。ABC") == "終わり。ABC");
    // The arrow read as a character: only on the last line, after 。！？
    CHECK(tidy_line("されど。と", true) == "されど。");
    CHECK(tidy_line("されど。と") == "されど。と");
    CHECK(tidy_line("「後の事は解っているな」と", true) == "「後の事は解っているな」と");
    CHECK(tidy_line("そうか。それで", true) == "そうか。それで");
    // Spaces around it go
    CHECK(tidy_line("　ただ、それを ") == "ただ、それを");
    CHECK(tidy_line("").empty());
}

static void test_join_lines(){
    // Wrapped in the middle of a word: joined
    CHECK(join_lines({"その騎士が持っ", "ていただけ。"}) == "その騎士が持っていただけ。");
    // After a sentence: a new line
    CHECK(join_lines({"時間は止まっていた。", "されど。"}) == "時間は止まっていた。\nされど。");
    CHECK(join_lines({"「マスターか」", "闇を弾く声で"}) == "「マスターか」\n闇を弾く声で");
    // Empty lines are skipped
    CHECK(join_lines({"", "あ", ""}) == "あ");
    // Latin words keep a space
    CHECK(join_lines({"Press any", "key"}) == "Press any key");
    CHECK(join_lines({}).empty());
}

int main(){
    test_detection_size();
    test_detection_padding();
    test_boxes_from_map();
    test_is_vertical();
    test_recognition_width();
    test_parse_character_list();
    test_decode_line();
    test_drop_furigana();
    test_reading_order();
    test_layout_lines();
    test_tidy_line();
    test_join_lines();
    if (failures){
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("All ocr_text tests passed");
    return 0;
}

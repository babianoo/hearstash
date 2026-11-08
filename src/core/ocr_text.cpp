#include "core/ocr_text.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_map>

// --- UTF-8 ---

static std::u32string decode_utf8(std::string_view text){
    std::u32string out;
    for (size_t i = 0; i < text.size();){
        const auto b = static_cast<unsigned char>(text[i]);
        const int len = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + static_cast<size_t>(len) > text.size()){
            out += U'�';
            ++i;
            continue;
        }
        char32_t cp = len == 1 ? b : b & (0x7F >> len);
        for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + static_cast<size_t>(k)]) & 0x3F);
        out += cp;
        i += static_cast<size_t>(len);
    }
    return out;
}

static std::string encode_utf8(std::u32string_view text){
    std::string out;
    for (char32_t cp : text){
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800){
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000){
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

// --- Detection ---

std::pair<int, int> detection_size(int width, int height, int max_side){
    const int long_side = std::max(width, height);
    const double ratio = long_side > max_side ? static_cast<double>(max_side) / long_side : 1.0;
    auto to_32 = [](double side){
        return std::max(32, static_cast<int>(std::lround(std::floor(side) / 32.0)) * 32);
    };
    return {to_32(width * ratio), to_32(height * ratio)};
}

int detection_padding(int width, int height){
    // RapidOCR's numbers: anything 30 px tall or less, or more than 8
    // times as wide as tall, is padded to twice the taller of those
    constexpr int MIN_HEIGHT = 30;
    constexpr int MAX_RATIO = 8;
    if (height <= 0 || (height > MIN_HEIGHT && width <= MAX_RATIO * height)) return 0;
    const int padded = std::max(width / MAX_RATIO, MIN_HEIGHT) * 2;
    return std::max(0, padded - height) / 2;
}

std::vector<Rect> boxes_from_map(std::span<const float> map, int map_w, int map_h, int image_w, int image_h,
                                 const DetectionParams& params){
    std::vector<Rect> boxes;
    if (map_w <= 0 || map_h <= 0 || map.size() < static_cast<size_t>(map_w) * map_h) return boxes;
    auto at = [map_w](int x, int y){ return static_cast<size_t>(y) * map_w + x; };

    // Which pixels are text, grown by one pixel up and left (a 2x2
    // dilation, as PaddleOCR does), so a line's characters join up
    std::vector<std::uint8_t> text(map.size());
    for (int y = 0; y < map_h; ++y)
        for (int x = 0; x < map_w; ++x){
            bool on = false;
            for (int dy = 0; dy <= 1 && !on; ++dy)
                for (int dx = 0; dx <= 1 && !on; ++dx){
                    const int sx = x - dx, sy = y - dy;
                    on = sx >= 0 && sy >= 0 && map[at(sx, sy)] > params.threshold;
                }
            text[at(x, y)] = on;
        }

    // Each group of touching text pixels (diagonals count) is a box
    std::vector<std::uint8_t> seen(map.size());
    std::vector<std::pair<int, int>> stack;
    int candidates = 0;
    for (int y = 0; y < map_h; ++y)
        for (int x = 0; x < map_w; ++x){
            if (!text[at(x, y)] || seen[at(x, y)]) continue;
            if (candidates++ >= params.max_candidates) return boxes;

            int x0 = x, x1 = x, y0 = y, y1 = y;
            seen[at(x, y)] = 1;
            stack.assign(1, {x, y});
            while (!stack.empty()){
                const auto [px, py] = stack.back();
                stack.pop_back();
                x0 = std::min(x0, px); x1 = std::max(x1, px);
                y0 = std::min(y0, py); y1 = std::max(y1, py);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx){
                        const int nx = px + dx, ny = py + dy;
                        if (nx < 0 || ny < 0 || nx >= map_w || ny >= map_h) continue;
                        if (!text[at(nx, ny)] || seen[at(nx, ny)]) continue;
                        seen[at(nx, ny)] = 1;
                        stack.push_back({nx, ny});
                    }
            }

            // From pixel center to pixel center, as OpenCV measures a
            // contour: a box 4 pixels wide is 3 wide
            const double w = x1 - x0, h = y1 - y0;
            if (std::min(w, h) < 3) continue;

            double sum = 0;
            for (int by = y0; by <= y1; ++by)
                for (int bx = x0; bx <= x1; ++bx) sum += map[at(bx, by)];
            if (sum / ((w + 1) * (h + 1)) < params.box_threshold) continue;

            // Grown on every side by the same distance, from the box's area
            // and outline (PaddleOCR's "unclip")
            const double d = w * h * params.unclip_ratio / (2 * (w + h));
            if (std::min(w, h) + 2 * d < 5) continue;

            const double sx = static_cast<double>(image_w) / map_w, sy = static_cast<double>(image_h) / map_h;
            auto to_x = [&](double v){ return std::clamp(static_cast<int>(std::lround(v * sx)), 0, image_w - 1); };
            auto to_y = [&](double v){ return std::clamp(static_cast<int>(std::lround(v * sy)), 0, image_h - 1); };
            const int left = to_x(x0 - d), right = to_x(x1 + d), top = to_y(y0 - d), bottom = to_y(y1 + d);
            if (right - left <= 3 || bottom - top <= 3) continue;
            boxes.push_back({.x = left, .y = top, .w = right - left, .h = bottom - top});
        }
    return boxes;
}

bool is_vertical(const Rect& box){
    return box.w > 0 && box.h >= 1.5 * box.w;
}

// --- Recognition ---

std::pair<int, int> recognition_width(int width, int height){
    if (width <= 0 || height <= 0) return {1, RECOGNITION_MIN_WIDTH};
    const double ratio = static_cast<double>(width) / height;
    const int padded = static_cast<int>(RECOGNITION_HEIGHT * std::max(ratio, static_cast<double>(RECOGNITION_MIN_WIDTH) / RECOGNITION_HEIGHT));
    const int scaled = std::clamp(static_cast<int>(std::ceil(RECOGNITION_HEIGHT * ratio)), 1, padded);
    return {scaled, padded};
}

// A YAML list item's value, as yaml.dump writes them: 'quoted' with ''
// for a quote, "quoted" with backslash escapes, or plain
static std::string unquote(std::string_view v){
    if (v.size() >= 2 && v.front() == '\'' && v.back() == '\''){
        std::string out;
        for (size_t i = 1; i + 1 < v.size(); ++i){
            out += v[i];
            if (v[i] == '\'' && i + 2 < v.size() && v[i + 1] == '\'') ++i;
        }
        return out;
    }
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"'){
        std::u32string out;
        const std::u32string in = decode_utf8(v.substr(1, v.size() - 2));
        for (size_t i = 0; i < in.size(); ++i){
            if (in[i] != U'\\' || i + 1 == in.size()){
                out += in[i];
                continue;
            }
            const char32_t e = in[++i];
            const int digits = e == U'x' ? 2 : e == U'u' ? 4 : e == U'U' ? 8 : 0;
            if (digits && i + static_cast<size_t>(digits) < in.size()){
                char32_t cp = 0;
                for (int k = 1; k <= digits; ++k){
                    const char32_t h = in[i + static_cast<size_t>(k)];
                    cp = cp * 16 + (h <= U'9' ? h - U'0' : (h | 0x20) - U'a' + 10);
                }
                out += cp;
                i += static_cast<size_t>(digits);
            } else out += e == U'n' ? U'\n' : e == U't' ? U'\t' : e == U'0' ? U'\0' : e;
        }
        return encode_utf8(out);
    }
    return std::string(v);
}

std::vector<std::string> parse_character_list(std::string_view yml){
    std::vector<std::string> characters{""};    // The blank, never shown
    bool in_list = false, found = false;
    size_t pos = 0;
    while (pos < yml.size()){
        size_t end = yml.find('\n', pos);
        if (end == std::string_view::npos) end = yml.size();
        std::string_view line = yml.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        const size_t indent = line.find_first_not_of(' ');
        if (indent == std::string_view::npos) continue;
        const std::string_view content = line.substr(indent);
        if (!in_list){
            if (content == "character_dict:") in_list = found = true;
            continue;
        }
        if (!content.starts_with("- ")){
            if (content == "-") continue;
            break;
        }
        characters.push_back(unquote(content.substr(2)));
    }
    if (!found || characters.size() < 2)
        throw std::runtime_error("The text reading model's settings have no list of characters");
    characters.push_back(" ");
    return characters;
}

std::string decode_line(std::span<const float> scores, int steps, int classes,
                        const std::vector<std::string>& characters){
    std::string text;
    if (steps <= 0 || classes <= 0 || scores.size() < static_cast<size_t>(steps) * classes) return text;
    int previous = -1;
    for (int s = 0; s < steps; ++s){
        const auto row = scores.subspan(static_cast<size_t>(s) * classes, static_cast<size_t>(classes));
        const int best = static_cast<int>(std::ranges::max_element(row) - row.begin());
        // A character held over several steps is read once; a blank
        // between two of the same separates them
        if (best != 0 && best != previous && static_cast<size_t>(best) < characters.size())
            text += characters[static_cast<size_t>(best)];
        previous = best;
    }
    return text;
}

// --- Layout ---

std::vector<Rect> drop_furigana(std::vector<Rect> boxes, bool vertical){
    auto is_furigana = [&](const Rect& b){
        for (const Rect& a : boxes){
            if (vertical){
                // Thin, just right of the column, within its height
                if (b.w >= 0.5 * a.w) continue;
                const int a_right = a.x + a.w;
                const bool beside = b.x >= a_right - 0.3 * a.w && b.x <= a_right + 0.6 * a.w;
                const bool along = b.y >= a.y - 0.1 * a.w && b.y + b.h <= a.y + a.h + 0.1 * a.w;
                if (beside && along) return true;
            } else {
                // Short, just above the line, within its width
                if (b.h >= 0.5 * a.h) continue;
                const int b_bottom = b.y + b.h;
                const bool above = b_bottom >= a.y - 0.6 * a.h && b_bottom <= a.y + 0.3 * a.h;
                const bool along = b.x >= a.x - 0.1 * a.h && b.x + b.w <= a.x + a.w + 0.1 * a.h;
                if (above && along) return true;
            }
        }
        return false;
    };
    std::vector<Rect> kept;
    for (const Rect& b : boxes)
        if (!is_furigana(b)) kept.push_back(b);
    return kept;
}

std::vector<std::vector<size_t>> reading_order(const std::vector<Rect>& boxes, bool vertical){
    // Across: where a box is along the direction lines follow each other
    // (y for rows, x for columns); along: where it is within its line
    auto across0 = [&](const Rect& r){ return vertical ? r.x : r.y; };
    auto across_size = [&](const Rect& r){ return vertical ? r.w : r.h; };
    auto along0 = [&](const Rect& r){ return vertical ? r.y : r.x; };

    std::vector<size_t> order(boxes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::ranges::sort(order, [&](size_t a, size_t b){
        const double ca = across0(boxes[a]) + across_size(boxes[a]) / 2.0;
        const double cb = across0(boxes[b]) + across_size(boxes[b]) / 2.0;
        return vertical ? ca > cb : ca < cb;
    });

    struct Line { int from, to; std::vector<size_t> boxes; };
    std::vector<Line> lines;
    for (size_t i : order){
        const Rect& r = boxes[i];
        const int from = across0(r), to = from + across_size(r);
        Line* joined = nullptr;
        for (Line& line : lines){
            // Overlapping by at least half of the thinner one
            const int overlap = std::min(to, line.to) - std::max(from, line.from);
            if (overlap >= 0.5 * std::min(to - from, line.to - line.from)){
                joined = &line;
                break;
            }
        }
        if (joined){
            joined->from = std::min(joined->from, from);
            joined->to = std::max(joined->to, to);
            joined->boxes.push_back(i);
        } else lines.push_back({from, to, {i}});
    }

    std::vector<std::vector<size_t>> rows;
    for (Line& line : lines){
        std::ranges::sort(line.boxes, [&](size_t a, size_t b){ return along0(boxes[a]) < along0(boxes[b]); });
        rows.push_back(std::move(line.boxes));
    }
    return rows;
}

std::vector<TextLine> layout_lines(const std::vector<Rect>& boxes){
    auto wide = [](const Rect& r){ return r.h > 0 && r.w >= 1.5 * r.h; };
    // Which boxes are vertical: the tall ones, then the square ones in line
    // with one of those, just above or below it
    std::vector<bool> vertical(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) vertical[i] = is_vertical(boxes[i]);
    for (size_t i = 0; i < boxes.size(); ++i){
        const Rect& b = boxes[i];
        if (vertical[i] || wide(b)) continue;
        for (size_t j = 0; j < boxes.size(); ++j){
            const Rect& c = boxes[j];
            if (!is_vertical(c)) continue;
            const int overlap = std::min(b.x + b.w, c.x + c.w) - std::max(b.x, c.x);
            const int gap = std::max(b.y - (c.y + c.h), c.y - (b.y + b.h));
            if (overlap >= 0.5 * std::min(b.w, c.w) && gap <= b.w){
                vertical[i] = true;
                break;
            }
        }
    }

    // Each orientation's lines, as indexes into boxes
    auto lines_of = [&](bool v){
        std::vector<size_t> index;
        std::vector<Rect> some;
        for (size_t i = 0; i < boxes.size(); ++i)
            if (vertical[i] == v){
                index.push_back(i);
                some.push_back(boxes[i]);
            }
        // drop_furigana keeps the order, so what's left is matched back
        const std::vector<Rect> kept = drop_furigana(some, v);
        std::vector<size_t> kept_index;
        for (size_t k = 0, i = 0; k < kept.size() && i < some.size(); ++i)
            if (some[i] == kept[k]){
                kept_index.push_back(index[i]);
                ++k;
            }
        std::vector<TextLine> lines;
        for (const std::vector<size_t>& row : reading_order(kept, v)){
            TextLine line{.boxes = {}, .vertical = v};
            for (size_t k : row) line.boxes.push_back(kept_index[k]);
            lines.push_back(std::move(line));
        }
        return lines;
    };
    std::vector<TextLine> horizontal = lines_of(false), vertical_lines = lines_of(true);

    // The one that starts higher first
    auto top = [&](const std::vector<TextLine>& lines){
        int y = std::numeric_limits<int>::max();
        for (const TextLine& line : lines)
            for (size_t i : line.boxes) y = std::min(y, boxes[i].y);
        return y;
    };
    std::vector<TextLine> first = std::move(vertical_lines), second = std::move(horizontal);
    if (top(second) < top(first)) std::swap(first, second);
    first.insert(first.end(), std::make_move_iterator(second.begin()), std::make_move_iterator(second.end()));
    return first;
}

// --- Text ---

// Characters only Chinese writes, which the model gives for Japanese ones
// that look the same and aren't in its list of characters. Found by
// rendering each JIS X 0208 kanji missing from its list and reading it
// back (in two fonts). Most of those come back as another Japanese
// character (凧 as 応), which can't be told from a right reading, or as
// nothing (呟, 躾); these are the ones that come back as a form of
// themselves Japanese doesn't use.
static const std::unordered_map<char32_t, char32_t> JAPANESE_FORMS{
    {U'躲', U'躱'},
    {U'巔', U'巓'},
    {U'廄', U'廏'},
    {U'攢', U'攅'},
    {U'暨', U'曁'},
    {U'纘', U'纉'},
};

// What ends a sentence, so a line after it starts a new one
static bool ends_sentence(char32_t c){
    return std::u32string_view(U"。．！？!?」』）)】〉》”’…‥").find(c) != std::u32string_view::npos;
}

static bool is_japanese(char32_t c){
    return (c >= 0x3040 && c <= 0x30FF)        // Kana
        || (c >= 0x3400 && c <= 0x9FFF)        // Kanji
        || (c >= 0xF900 && c <= 0xFAFF)        // Compatibility kanji
        || (c >= 0xFF66 && c <= 0xFF9F)        // Half-width katakana
        || (c >= 0x3005 && c <= 0x3007);       // 々〆〇
}

// Could be an icon read as a character: not Japanese, not punctuation that
// ends a sentence
static bool is_stray(char32_t c){
    return !is_japanese(c) && !ends_sentence(c) && c != U' ' && c != U'　';
}

static bool is_latin(char32_t c){
    return (c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z');
}

std::string tidy_line(std::string_view text, bool last_line){
    std::u32string line = decode_utf8(text);
    for (char32_t& c : line)
        if (const auto it = JAPANESE_FORMS.find(c); it != JAPANESE_FORMS.end()) c = it->second;

    auto trim = [](std::u32string& s){
        while (!s.empty() && (s.back() == U' ' || s.back() == U'　')) s.pop_back();
        size_t start = 0;
        while (start < s.size() && (s[start] == U' ' || s[start] == U'　')) ++start;
        s.erase(0, start);
    };
    trim(line);

    // A symbol or two after the sentence's end, maybe after a space: the
    // "next" arrow
    const size_t end = line.find_last_of(U"。．！？!?」』）)】〉》”’…‥");
    if (end != std::u32string::npos && end + 1 < line.size()){
        const std::u32string_view tail = std::u32string_view(line).substr(end + 1);
        const auto symbols = std::ranges::count_if(tail, is_stray);
        const auto spaces = std::ranges::count_if(tail, [](char32_t c){ return c == U' ' || c == U'\u3000'; });
        if (symbols <= 2 && symbols + spaces == static_cast<std::ptrdiff_t>(tail.size())) line.erase(end + 1);
    }
    trim(line);

    // The arrow read as a character, after the last sentence on the screen
    if (last_line){
        const size_t stop = line.find_last_of(U"。．！？!?");
        if (stop != std::u32string::npos && stop + 2 == line.size()) line.pop_back();
    }

    // A line of nothing but one such symbol: the arrow on its own
    if (line.size() == 1 && is_stray(line[0]) && !is_latin(line[0])) line.clear();
    return encode_utf8(line);
}

std::string join_lines(const std::vector<std::string>& lines){
    std::u32string text;
    for (const std::string& line : lines){
        const std::u32string next = decode_utf8(line);
        if (next.empty()) continue;
        if (!text.empty()){
            if (ends_sentence(text.back())) text += U'\n';
            else if (is_latin(text.back()) && is_latin(next.front())) text += U' ';
        }
        text += next;
    }
    return encode_utf8(text);
}

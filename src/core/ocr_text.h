#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/image.h"

// The parts of reading text in a screenshot (OCR) that don't need the
// models, so they can be tested without them: what goes into the models
// and what's made of what comes out. TextReader (core/text_reader.h) runs
// the models in between.
//
// The models are PaddleOCR's PP-OCRv6 small ones: one finds the lines of
// text (detection), the other reads one line at a time (recognition).
// These follow PaddleOCR's own pre- and post-processing, simplified where
// game text allows: lines are horizontal or vertical, never slanted, so
// boxes are plain rectangles.

// How the detection model's output is turned into boxes. The defaults are
// the model's own, from its inference.yml.
struct DetectionParams {
    // A pixel is text if the model is at least this sure of it
    float threshold = 0.2f;
    // A box is kept if the model is on average at least this sure of its
    // pixels
    float box_threshold = 0.45f;
    // How much a box is grown: the model marks the middle of a line, not
    // all of it. Bigger grows more.
    float unclip_ratio = 1.4f;
    // At most this many boxes are looked at
    int max_candidates = 3000;
};

// The size to scale an image of width x height to for the detection model:
// the long side at most max_side, both sides multiples of 32, at least 32.
// Never bigger than it was, give or take the rounding: scaling a small
// crop up gains nothing and takes long.
std::pair<int, int> detection_size(int width, int height, int max_side = 960);

// How many rows of black to add above and below an image of width x height
// before detection, 0 for most. A very wide or very short image, e.g. a
// crop of one line, comes out of the scaling too thin for the model to
// find the text in.
int detection_padding(int width, int height);

// The boxes of text in the detection model's output: map_w x map_h values
// from 0 to 1, row by row, how sure the model is that each pixel is text.
// The boxes are in the pixels of the image the map was made from,
// image_w x image_h, which the map was scaled from.
std::vector<Rect> boxes_from_map(std::span<const float> map, int map_w, int map_h, int image_w, int image_h,
                                 const DetectionParams& params = {});

// Whether a line in this box is written top to bottom: it's much taller
// than wide. Its picture is then turned with rotate_left() before it's
// read, since the recognition model only reads left to right.
bool is_vertical(const Rect& box);

// The width a line's picture of width x height is scaled to for the
// recognition model, at its height of RECOGNITION_HEIGHT, keeping its
// shape; and the width it's then padded to, at least RECOGNITION_MIN_WIDTH.
inline constexpr int RECOGNITION_HEIGHT = 48;
inline constexpr int RECOGNITION_MIN_WIDTH = 320;
std::pair<int, int> recognition_width(int width, int height);

// The characters the recognition model reads, from its inference.yml
// (PostProcess: character_dict:), in the order of its output, with the
// "blank" index 0 and the space at the end that it adds. Throws
// std::runtime_error if the file has no such list.
std::vector<std::string> parse_character_list(std::string_view yml);

// The text in the recognition model's output for one line: steps x classes
// scores, row by row, one row per step along the line. The best class at
// each step, with repeats merged and blanks (index 0) dropped, as
// characters[class]. characters is parse_character_list()'s, and has to
// have classes entries.
std::string decode_line(std::span<const float> scores, int steps, int classes,
                        const std::vector<std::string>& characters);

// A line of text: indexes into a list of boxes, in reading order, which
// the detection model sometimes splits a line into at a gap
struct TextLine {
    std::vector<size_t> boxes;
    bool vertical = false;
};

// The lines in boxes, in reading order, horizontal and vertical text
// apart, since a screen can have both: a box much taller than wide is
// vertical, much wider than tall horizontal, and one about square (a
// character or two) goes with a vertical column it's in line with, or
// else is horizontal. Furigana is dropped from each (drop_furigana).
// Horizontal lines are read top to bottom, vertical ones right to left
// (reading_order), and whichever of the two starts higher on the screen
// comes first.
std::vector<TextLine> layout_lines(const std::vector<Rect>& boxes);

// Drops the boxes of furigana: a box much smaller than the line right
// below it (or to its right, in vertical text), within its width. It
// would otherwise be read as a line of its own, between the lines.
std::vector<Rect> drop_furigana(std::vector<Rect> boxes, bool vertical);

// The boxes in reading order, as rows of indexes into boxes: lines top to
// bottom (vertical: columns right to left), and the boxes in each line,
// which the detection model sometimes splits at a gap, left to right
// (vertical: top to bottom).
std::vector<std::vector<size_t>> reading_order(const std::vector<Rect>& boxes, bool vertical);

// A line's text as read, fixed up:
// - Characters only Chinese writes, which the model reads for the
//   Japanese ones they look like and which aren't in its list, e.g. 躲 for
//   躱, are turned into the Japanese ones.
// - What a VN puts after a line's last word, like a "next" arrow, which
//   is read as a stray letter or symbol (V, ↓, >), is dropped after the
//   line's last 。, ！, ？ or closing bracket.
// - On the last line on the screen, where the arrow is, it's sometimes
//   read as a character (と), so there any one character after the last
//   。, ！ or ？ is dropped: the text doesn't end with a sentence one
//   character long. Not after a closing bracket, as in 「…」と.
// Empty if nothing but such a symbol is left.
std::string tidy_line(std::string_view text, bool last_line = false);

// The lines of a screen, in reading order, as one text: a line is joined
// to the one before it, since Japanese wraps lines in the middle of words,
// unless that one ended a sentence (。！？ or a closing quote), which
// starts a new line. Latin words on either side of a join get a space.
std::string join_lines(const std::vector<std::string>& lines);

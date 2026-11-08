#pragma once

#include <cstdint>
#include <vector>

// A picture in memory: width * height pixels, row by row from the top,
// 3 bytes each (red, green, blue)
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;

    bool empty() const { return width <= 0 || height <= 0; }
};

// A rectangle of an Image, in its pixels
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
    bool operator==(const Rect&) const = default;
};

// The part of image inside rect, which is first clamped to the image.
// Empty if nothing of rect is in it.
Image crop(const Image& image, Rect rect);

// image made smaller, if it's taller than max_height, to max_height tall
// and as wide as keeps its shape. Each new pixel is the average of the
// ones it replaces, so fine detail like text doesn't break up. A smaller
// image comes back as it is.
Image shrink_to_height(const Image& image, int max_height);

// image scaled to width x height (both at least 1), each new pixel mixed
// from the 4 nearest old ones (bilinear, sampled at pixel centers, like
// OpenCV's INTER_LINEAR, which the text reading models were trained
// with). Unlike shrink_to_height, it can make an image bigger, and its
// shape can change.
Image resize(const Image& image, int width, int height);

// image turned a quarter turn to the left (counterclockwise): its top row
// becomes the left column, read from the bottom up. A column of vertical
// text then reads left to right.
Image rotate_left(const Image& image);

// image as a JPEG file's bytes, at quality from 1 to 100. Throws
// std::runtime_error if image is empty, or libjpeg fails.
std::vector<std::uint8_t> encode_jpeg(const Image& image, int quality);

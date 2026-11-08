// Tests for core/image.h and parse_ppm(). No framework, like
// sample_ring_test.cpp.

#include <cstdio>
#include <stdexcept>
#include <string>

#include "core/image.h"
#include "core/screenshot.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)){ \
        std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

// A width x height image whose pixel (x, y) is (x, y, 7)
static Image numbered(int width, int height){
    Image image{.width = width, .height = height, .rgb = {}};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) image.rgb.insert(image.rgb.end(), {std::uint8_t(x), std::uint8_t(y), 7});
    return image;
}

static void test_crop(){
    const Image image = numbered(10, 8);
    const Image part = crop(image, {.x = 2, .y = 3, .w = 4, .h = 2});
    CHECK(part.width == 4 && part.height == 2);
    CHECK(part.rgb.size() == 4 * 2 * 3);
    CHECK(part.rgb[0] == 2 && part.rgb[1] == 3);                // Top left
    CHECK(part.rgb[(1 * 4 + 3) * 3] == 5 && part.rgb[(1 * 4 + 3) * 3 + 1] == 4);   // Bottom right

    // Clamped to the image
    const Image edge = crop(image, {.x = 8, .y = -5, .w = 10, .h = 7});
    CHECK(edge.width == 2 && edge.height == 2);
    CHECK(crop(image, {.x = 20, .y = 0, .w = 5, .h = 5}).empty());
}

static void test_shrink(){
    // Each 2x2 block of 0, 10, 20, 30 averages to 15
    Image image{.width = 4, .height = 4, .rgb = {}};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x){
            const std::uint8_t v = static_cast<std::uint8_t>(((y % 2) * 2 + x % 2) * 10);
            image.rgb.insert(image.rgb.end(), {v, v, v});
        }
    const Image half = shrink_to_height(image, 2);
    CHECK(half.width == 2 && half.height == 2);
    CHECK(half.rgb.size() == 2 * 2 * 3);
    bool all_15 = true;
    for (std::uint8_t v : half.rgb) all_15 = all_15 && v == 15;
    CHECK(all_15);

    // Never made bigger
    CHECK(shrink_to_height(image, 10).height == 4);
    // Keeps its shape
    const Image wide = shrink_to_height(numbered(160, 90), 45);
    CHECK(wide.width == 80 && wide.height == 45);
}

static void test_resize(){
    // A flat color stays flat, at any size
    Image flat{.width = 3, .height = 2, .rgb = std::vector<std::uint8_t>(3 * 2 * 3, 100)};
    const Image big = resize(flat, 7, 5);
    CHECK(big.width == 7 && big.height == 5 && big.rgb.size() == 7 * 5 * 3);
    bool all_100 = true;
    for (std::uint8_t v : big.rgb) all_100 = all_100 && v == 100;
    CHECK(all_100);

    // Doubling a 0, 100 pair: the edges keep their values, the middle blends
    Image pair{.width = 2, .height = 1, .rgb = {0, 0, 0, 100, 100, 100}};
    const Image wide = resize(pair, 4, 1);
    CHECK(wide.rgb[0] == 0 && wide.rgb[9] == 100);
    CHECK(wide.rgb[3] == 25 && wide.rgb[6] == 75);

    // Its shape can change
    const Image squashed = resize(numbered(100, 10), 20, 30);
    CHECK(squashed.width == 20 && squashed.height == 30);
}

static void test_rotate_left(){
    // 3x2, pixel (x, y) is (x, y, 7)
    const Image turned = rotate_left(numbered(3, 2));
    CHECK(turned.width == 2 && turned.height == 3);
    // The old top right corner is the new top left
    CHECK(turned.rgb[0] == 2 && turned.rgb[1] == 0);
    // The old top left corner is the new bottom left
    CHECK(turned.rgb[(2 * 2 + 0) * 3] == 0 && turned.rgb[(2 * 2 + 0) * 3 + 1] == 0);
    // The old bottom left corner is the new bottom right
    CHECK(turned.rgb[(2 * 2 + 1) * 3] == 0 && turned.rgb[(2 * 2 + 1) * 3 + 1] == 1);
}

static void test_jpeg(){
    const std::vector<std::uint8_t> jpeg = encode_jpeg(numbered(64, 48), 85);
    // Starts and ends with JPEG's markers
    CHECK(jpeg.size() > 100);
    CHECK(jpeg[0] == 0xFF && jpeg[1] == 0xD8);
    CHECK(jpeg[jpeg.size() - 2] == 0xFF && jpeg.back() == 0xD9);

    bool threw = false;
    try { encode_jpeg({}, 85); } catch (const std::runtime_error&){ threw = true; }
    CHECK(threw);
}

static void test_ppm(){
    const std::string text = "P6\n# made by a test\n2 1\n255\n";
    std::vector<std::uint8_t> data(text.begin(), text.end());
    data.insert(data.end(), {1, 2, 3, 4, 5, 6});
    const Image image = parse_ppm(data);
    CHECK(image.width == 2 && image.height == 1);
    CHECK(image.rgb == std::vector<std::uint8_t>({1, 2, 3, 4, 5, 6}));

    bool threw = false;
    data.pop_back();    // Cut short
    try { parse_ppm(data); } catch (const std::runtime_error&){ threw = true; }
    CHECK(threw);
}

#ifndef _WIN32
#include <png.h>

static void test_png(){
    // A 3x2 PNG with alpha, which comes back as RGB without it
    const std::uint8_t rgba[] = {1, 2, 3, 255,  4, 5, 6, 255,  7, 8, 9, 255,
                                 10, 11, 12, 255,  13, 14, 15, 255,  16, 17, 18, 255};
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    png.width = 3;
    png.height = 2;
    png.format = PNG_FORMAT_RGBA;
    png_alloc_size_t size = 0;
    CHECK(png_image_write_to_memory(&png, nullptr, &size, 0, rgba, 0, nullptr));
    std::vector<std::uint8_t> data(size);
    CHECK(png_image_write_to_memory(&png, data.data(), &size, 0, rgba, 0, nullptr));
    data.resize(size);

    const Image image = parse_png(data);
    CHECK(image.width == 3 && image.height == 2);
    CHECK(image.rgb == std::vector<std::uint8_t>({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18}));

    bool threw = false;
    try { parse_png({1, 2, 3}); } catch (const std::runtime_error&){ threw = true; }
    CHECK(threw);
    threw = false;
    data.resize(data.size() / 2);    // Cut short
    try { parse_png(data); } catch (const std::runtime_error&){ threw = true; }
    CHECK(threw);
}

// kscreen-doctor -j's layout, cut down: a 1920x1080 monitor beside a 4K
// one, lower down
static const char* LAYOUT = R"({"outputs": [
    {"name": "HDMI-A-1", "enabled": true, "pos": {"x": 1920, "y": 0},
     "size": {"width": 3840, "height": 2160}, "scale": 1, "rotation": 1},
    {"name": "DP-1", "enabled": true, "pos": {"x": 0, "y": 504},
     "size": {"width": 1920, "height": 1080}, "scale": 1, "rotation": 1},
    {"name": "DP-2", "enabled": false, "pos": {"x": 0, "y": 0},
     "size": {"width": 800, "height": 600}, "scale": 1, "rotation": 1}
]})";

static void test_monitor_in_screenshot(){
    // The portal's screenshot is the box around both, 5760x2160
    CHECK(monitor_in_screenshot(LAYOUT, "DP-1", 5760, 2160) == (Rect{.x = 0, .y = 504, .w = 1920, .h = 1080}));
    CHECK(monitor_in_screenshot(LAYOUT, "HDMI-A-1", 5760, 2160) == (Rect{.x = 1920, .y = 0, .w = 3840, .h = 2160}));
    // At half the size
    CHECK(monitor_in_screenshot(LAYOUT, "DP-1", 2880, 1080) == (Rect{.x = 0, .y = 252, .w = 960, .h = 540}));
    // A screenshot of another shape, a monitor that isn't there, one
    // that's off, or a layout that can't be read: no part
    CHECK(monitor_in_screenshot(LAYOUT, "DP-1", 3840, 2160).empty());
    CHECK(monitor_in_screenshot(LAYOUT, "DP-9", 5760, 2160).empty());
    CHECK(monitor_in_screenshot(LAYOUT, "DP-2", 5760, 2160).empty());
    CHECK(monitor_in_screenshot("not json", "DP-1", 5760, 2160).empty());

    // Turned on its side, and scaled: a 4K monitor at 200% is 1080x1920
    // logical pixels standing up
    const char* turned = R"({"outputs": [
        {"name": "A", "enabled": true, "pos": {"x": 0, "y": 0},
         "size": {"width": 3840, "height": 2160}, "scale": 2, "rotation": 2},
        {"name": "B", "enabled": true, "pos": {"x": 1080, "y": 0},
         "size": {"width": 1920, "height": 1080}, "scale": 1, "rotation": 1}]})";
    CHECK(monitor_in_screenshot(turned, "A", 3000, 1920) == (Rect{.x = 0, .y = 0, .w = 1080, .h = 1920}));
    CHECK(monitor_in_screenshot(turned, "B", 3000, 1920) == (Rect{.x = 1080, .y = 0, .w = 1920, .h = 1080}));
}
#endif

int main(){
    test_crop();
    test_shrink();
    test_resize();
    test_rotate_left();
    test_jpeg();
    test_ppm();
#ifndef _WIN32
    test_png();
    test_monitor_in_screenshot();
#endif
    if (failures == 0) std::puts("All image tests passed");
    return failures;
}

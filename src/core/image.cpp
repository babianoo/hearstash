#include "core/image.h"

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

// libjpeg (libjpeg-turbo). Its header needs size_t and FILE declared first.
#include <jpeglib.h>

Image crop(const Image& image, Rect rect){
    const int x0 = std::clamp(rect.x, 0, image.width), y0 = std::clamp(rect.y, 0, image.height);
    const int x1 = std::clamp(rect.x + rect.w, x0, image.width), y1 = std::clamp(rect.y + rect.h, y0, image.height);

    Image out{.width = x1 - x0, .height = y1 - y0, .rgb = {}};
    out.rgb.reserve(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 3);
    for (int y = y0; y < y1; ++y){
        const auto row = image.rgb.begin() + (static_cast<std::ptrdiff_t>(y) * image.width + x0) * 3;
        out.rgb.insert(out.rgb.end(), row, row + static_cast<std::ptrdiff_t>(out.width) * 3);
    }
    return out;
}

Image shrink_to_height(const Image& image, int max_height){
    if (image.empty() || image.height <= max_height || max_height <= 0) return image;

    const int height = max_height;
    const int width = std::max(1, static_cast<int>(static_cast<long long>(image.width) * height / image.height));
    Image out{.width = width, .height = height, .rgb = std::vector<std::uint8_t>(static_cast<size_t>(width) * height * 3)};

    for (int y = 0; y < height; ++y){
        // The source rows this row covers: at least one
        const int sy0 = static_cast<int>(static_cast<long long>(y) * image.height / height);
        const int sy1 = std::max(sy0 + 1, static_cast<int>(static_cast<long long>(y + 1) * image.height / height));
        for (int x = 0; x < width; ++x){
            const int sx0 = static_cast<int>(static_cast<long long>(x) * image.width / width);
            const int sx1 = std::max(sx0 + 1, static_cast<int>(static_cast<long long>(x + 1) * image.width / width));
            unsigned sum[3] = {0, 0, 0};
            for (int sy = sy0; sy < sy1; ++sy)
                for (int sx = sx0; sx < sx1; ++sx)
                    for (int c = 0; c < 3; ++c)
                        sum[c] += image.rgb[(static_cast<size_t>(sy) * image.width + sx) * 3 + c];
            const unsigned count = static_cast<unsigned>((sy1 - sy0) * (sx1 - sx0));
            for (int c = 0; c < 3; ++c)
                out.rgb[(static_cast<size_t>(y) * width + x) * 3 + c] = static_cast<std::uint8_t>((sum[c] + count / 2) / count);
        }
    }
    return out;
}

// libjpeg's default error handler prints and calls exit(). This one
// jumps back to compress() instead: an exception can't safely unwind
// through libjpeg's C code.
struct JpegError {
    jpeg_error_mgr manager;
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

[[noreturn]] static void jump_back(j_common_ptr cinfo){
    auto* error = reinterpret_cast<JpegError*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, error->message);
    std::longjmp(error->jump, 1);
}

// Only C-style locals here, since longjmp skips destructors. On success
// *buffer is malloc'd for the caller to free. Returns false on failure,
// with error.message saying why.
static bool compress(const Image& image, int quality, unsigned char** buffer, unsigned long* size, JpegError& error){
    jpeg_compress_struct cinfo;
    cinfo.err = jpeg_std_error(&error.manager);
    error.manager.error_exit = jump_back;
    if (setjmp(error.jump)){
        jpeg_destroy_compress(&cinfo);
        return false;
    }

    jpeg_create_compress(&cinfo);
    // It allocates the output with malloc, and grows it
    jpeg_mem_dest(&cinfo, buffer, size);
    cinfo.image_width = static_cast<JDIMENSION>(image.width);
    cinfo.image_height = static_cast<JDIMENSION>(image.height);
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, std::clamp(quality, 1, 100), TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height){
        // libjpeg takes rows as non-const, but only reads them
        JSAMPROW row = const_cast<JSAMPROW>(image.rgb.data() + static_cast<size_t>(cinfo.next_scanline) * image.width * 3);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    return true;
}

std::vector<std::uint8_t> encode_jpeg(const Image& image, int quality){
    if (image.empty()) throw std::runtime_error("Couldn't save the picture as JPEG: it's empty");

    unsigned char* buffer = nullptr;
    unsigned long size = 0;
    JpegError error{};
    const bool ok = compress(image, quality, &buffer, &size, error);
    std::vector<std::uint8_t> bytes;
    if (ok) bytes.assign(buffer, buffer + size);
    std::free(buffer);
    if (!ok) throw std::runtime_error(std::string("Couldn't save the picture as JPEG: ") + error.message);
    return bytes;
}

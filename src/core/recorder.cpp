#include "core/recorder.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <stdexcept>

// miniaudio, with the MA_NO_* definitions CMake gives it
#include <miniaudio.h>

// What the audio thread writes into. Separate from Impl, which is private
// to Recorder, since on_audio isn't a member.
struct Capture {
    // Set aside once, MAX_SECONDS long, so the audio thread never
    // allocates. It writes up to the end and no further.
    std::vector<float> buffer = std::vector<float>(Recorder::SAMPLE_RATE * Recorder::MAX_SECONDS);
    // How much of buffer is recorded. Only the audio thread writes it
    // while recording; release/acquire so a reader sees the samples too.
    std::atomic<size_t> frames{0};
    std::atomic<float> level{0};
};

struct Recorder::Impl {
    ma_context context;
    ma_device device;
    // Whether device is initialized, i.e. recording
    bool open = false;
    Capture capture;
};

// miniaudio calls it on its own thread with each chunk of audio, already
// converted to 16 kHz mono floats. It mustn't block or allocate.
static void on_audio(ma_device* device, void*, const void* input, ma_uint32 count){
    auto* capture = static_cast<Capture*>(device->pUserData);
    const float* samples = static_cast<const float*>(input);

    const size_t at = capture->frames.load(std::memory_order_relaxed);
    const size_t n = std::min<size_t>(count, capture->buffer.size() - at);
    std::copy_n(samples, n, capture->buffer.data() + at);
    capture->frames.store(at + n, std::memory_order_release);

    float peak = 0;
    for (ma_uint32 i = 0; i < count; ++i) peak = std::max(peak, std::fabs(samples[i]));
    capture->level.store(peak, std::memory_order_relaxed);
}

Recorder::Recorder(Backend backend) : impl(std::make_unique<Impl>()){
    // nullptr tries the system's backends in miniaudio's order
    ma_backend null_backend[] = {ma_backend_null};
    const ma_result result = backend == Backend::Null
        ? ma_context_init(null_backend, 1, nullptr, &impl->context)
        : ma_context_init(nullptr, 0, nullptr, &impl->context);
    if (result != MA_SUCCESS)
        throw std::runtime_error(std::format("No audio system to record with: {}", ma_result_description(result)));
}

Recorder::~Recorder(){
    if (impl->open) ma_device_uninit(&impl->device);
    ma_context_uninit(&impl->context);
}

std::vector<std::string> list_input_devices(){
    // A context of its own, so the settings window can list them without
    // a Recorder. Takes a few milliseconds.
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) return {};

    ma_device_info* playback;
    ma_uint32 playback_count;
    ma_device_info* capture;
    ma_uint32 capture_count;
    std::vector<std::string> names;
    // The infos belong to the context, so the names are copied before
    // it's freed
    if (ma_context_get_devices(&context, &playback, &playback_count, &capture, &capture_count) == MA_SUCCESS)
        for (ma_uint32 i = 0; i < capture_count; ++i) names.emplace_back(capture[i].name);

    ma_context_uninit(&context);
    return names;
}

void Recorder::start(const std::string& device){
    if (impl->open) return;

    ma_device_info* playback;
    ma_uint32 playback_count;
    ma_device_info* capture = nullptr;
    ma_uint32 capture_count = 0;
    ma_context_get_devices(&impl->context, &playback, &playback_count, &capture, &capture_count);
    if (capture_count == 0) throw std::runtime_error("No microphone found");

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = on_audio;
    config.pUserData = &impl->capture;
    // By name: a device's id isn't kept between runs on every backend.
    // Not found leaves pDeviceID null, which is the default.
    for (ma_uint32 i = 0; i < capture_count; ++i)
        if (!device.empty() && device == capture[i].name) config.capture.pDeviceID = &capture[i].id;

    impl->capture.frames.store(0);
    impl->capture.level.store(0);

    ma_result result = ma_device_init(&impl->context, &config, &impl->device);
    if (result != MA_SUCCESS){
#ifdef _WIN32
        throw std::runtime_error(std::format(
            "Couldn't open the microphone: {}\n\nIf it's plugged in, check that apps may use it in "
            "Windows Settings > Privacy & security > Microphone.", ma_result_description(result)));
#else
        throw std::runtime_error(std::format("Couldn't open the microphone: {}", ma_result_description(result)));
#endif
    }

    result = ma_device_start(&impl->device);
    if (result != MA_SUCCESS){
        ma_device_uninit(&impl->device);
        throw std::runtime_error(std::format("Couldn't start recording: {}", ma_result_description(result)));
    }
    impl->open = true;
}

std::vector<float> Recorder::stop(){
    if (!impl->open) return {};

    // Waits for the audio thread's last call to return, so nothing writes
    // into buffer after this
    ma_device_uninit(&impl->device);
    impl->open = false;

    const size_t frames = impl->capture.frames.load(std::memory_order_acquire);
    impl->capture.level.store(0);
    return std::vector<float>(impl->capture.buffer.begin(), impl->capture.buffer.begin() + static_cast<std::ptrdiff_t>(frames));
}

bool Recorder::recording() const {
    return impl->open;
}

bool Recorder::full() const {
    return impl->capture.frames.load(std::memory_order_relaxed) == impl->capture.buffer.size();
}

double Recorder::seconds() const {
    return static_cast<double>(impl->capture.frames.load(std::memory_order_relaxed)) / SAMPLE_RATE;
}

float Recorder::level() const {
    return impl->capture.level.load(std::memory_order_relaxed);
}

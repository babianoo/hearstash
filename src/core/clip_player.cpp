#include "core/clip_player.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <stdexcept>
#include <vector>

// miniaudio, with the MA_NO_* definitions CMake gives it
#include <miniaudio.h>

// What the audio thread reads from. Separate from Impl, which is private
// to ClipPlayer, since on_audio isn't a member.
struct Playback {
    // Only changed while the device is closed
    std::vector<float> samples;
    // How far it's played. Only the audio thread writes it while playing.
    std::atomic<size_t> position{0};
};

struct ClipPlayer::Impl {
    ma_context context;
    ma_device device;
    // Whether device is initialized
    bool open = false;
    Playback playback;
};

// miniaudio calls it on its own thread for each chunk to play. It mustn't
// block or allocate.
static void on_audio(ma_device* device, void* output, const void*, ma_uint32 count){
    auto* playback = static_cast<Playback*>(device->pUserData);
    float* out = static_cast<float*>(output);

    const size_t at = playback->position.load(std::memory_order_relaxed);
    const size_t n = std::min<size_t>(count, playback->samples.size() - at);
    std::copy_n(playback->samples.data() + at, n, out);
    // Silence after the end, until stop() closes the device
    std::fill(out + n, out + count, 0.0f);
    playback->position.store(at + n, std::memory_order_relaxed);
}

ClipPlayer::ClipPlayer(Backend backend) : impl(std::make_unique<Impl>()){
    // nullptr tries the system's backends in miniaudio's order
    ma_backend null_backend[] = {ma_backend_null};
    const ma_result result = backend == Backend::Null
        ? ma_context_init(null_backend, 1, nullptr, &impl->context)
        : ma_context_init(nullptr, 0, nullptr, &impl->context);
    if (result != MA_SUCCESS)
        throw std::runtime_error(std::format("No audio system to play with: {}", ma_result_description(result)));
}

ClipPlayer::~ClipPlayer(){
    stop();
    ma_context_uninit(&impl->context);
}

void ClipPlayer::play(std::span<const float> samples, int rate){
    stop();
    impl->playback.samples.assign(samples.begin(), samples.end());
    impl->playback.position.store(0);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = static_cast<ma_uint32>(rate);
    config.dataCallback = on_audio;
    config.pUserData = &impl->playback;

    ma_result result = ma_device_init(&impl->context, &config, &impl->device);
    if (result != MA_SUCCESS)
        throw std::runtime_error(std::format("Couldn't open the audio output: {}", ma_result_description(result)));
    result = ma_device_start(&impl->device);
    if (result != MA_SUCCESS){
        ma_device_uninit(&impl->device);
        throw std::runtime_error(std::format("Couldn't play the audio: {}", ma_result_description(result)));
    }
    impl->open = true;
}

void ClipPlayer::stop(){
    if (!impl->open) return;
    // Waits for the audio thread's last call to return
    ma_device_uninit(&impl->device);
    impl->open = false;
}

bool ClipPlayer::playing() const {
    return impl->open && position() < impl->playback.samples.size();
}

size_t ClipPlayer::position() const {
    return impl->playback.position.load(std::memory_order_relaxed);
}

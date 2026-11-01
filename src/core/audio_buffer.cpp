#include "core/audio_buffer.h"

#include <chrono>
#include <format>
#include <stdexcept>
#include <string>

// miniaudio, with the MA_NO_* definitions CMake gives it
#include <miniaudio.h>

#include "core/sample_ring.h"

// What the audio thread writes into. Separate from Impl, which is private
// to AudioBuffer, since on_audio isn't a member.
struct Capture {
    SampleRing ring;
    // Only the audio thread uses it
    GapDetector gaps{AudioBuffer::SAMPLE_RATE};

    explicit Capture(int seconds)
        : ring(static_cast<size_t>(seconds + AudioBuffer::MARGIN_SECONDS) * AudioBuffer::SAMPLE_RATE){}
};

struct AudioBuffer::Impl {
    ma_context context;
    ma_device device;
    // Whether device is initialized, i.e. recording
    bool open = false;
    Backend backend;
    // The output whose monitor device records, by PulseAudio's name for it
    std::string output;
    // Replaced whenever what was recorded is forgotten. Only touched while
    // the device is closed.
    std::unique_ptr<Capture> capture;
};

// miniaudio calls it on its own thread with each chunk of audio, already
// converted to 48 kHz mono floats. It mustn't block or allocate.
static void on_audio(ma_device* device, void*, const void* input, ma_uint32 count){
    auto* capture = static_cast<Capture*>(device->pUserData);
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    if (const std::uint64_t silence = capture->gaps.silence_before(now, count))
        capture->ring.write_silence(static_cast<size_t>(silence));
    capture->ring.write({static_cast<const float*>(input), count});
}

AudioBuffer::AudioBuffer(int seconds, Backend backend) : length(seconds), impl(std::make_unique<Impl>()){
    impl->backend = backend;
    impl->capture = std::make_unique<Capture>(seconds);

    ma_context_config config = ma_context_config_init();
    // How it's listed in pavucontrol and the like, so it's clear what's
    // recording
    config.pulse.pApplicationName = "Prototype";

    ma_backend null_backend[] = {ma_backend_null};
    // Only PulseAudio's monitor devices record what's played. ALSA has no
    // such thing, and neither does JACK.
    ma_backend pulse_backend[] = {ma_backend_pulseaudio};
    const ma_result result = backend == Backend::Null
        ? ma_context_init(null_backend, 1, &config, &impl->context)
        : ma_context_init(pulse_backend, 1, &config, &impl->context);
    if (result != MA_SUCCESS)
        throw std::runtime_error(std::format(
            "Recording what the computer plays needs PulseAudio, or PipeWire with its PulseAudio server: {}",
            ma_result_description(result)));
}

AudioBuffer::~AudioBuffer(){
    close_device();
    ma_context_uninit(&impl->context);
}

// PulseAudio's name for the default output, e.g.
// "alsa_output.pci-0000_00_1f.3.analog-stereo". Empty if there's none.
static std::string default_output(ma_context& context){
    ma_device_info* playback;
    ma_uint32 playback_count = 0;
    ma_device_info* capture;
    ma_uint32 capture_count;
    if (ma_context_get_devices(&context, &playback, &playback_count, &capture, &capture_count) != MA_SUCCESS)
        return "";
    for (ma_uint32 i = 0; i < playback_count; ++i)
        if (playback[i].isDefault) return playback[i].id.pulse;
    return "";
}

void AudioBuffer::open_device(){
#ifdef _WIN32
    // TODO(Windows): ma_device_type_loopback on WASAPI, which records the
    // default output and follows it when it changes
    throw std::runtime_error("Recording what the computer plays isn't done on Windows yet");
#else
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = on_audio;
    config.pUserData = impl->capture.get();
    config.pulse.pStreamNameCapture = "Rolling buffer of what's playing";

    // Every output has a monitor device, which records what it plays.
    // With the null backend the default capture device is silence.
    ma_device_id id{};
    if (impl->backend == Backend::System){
        impl->output = default_output(impl->context);
        if (impl->output.empty()) throw std::runtime_error("No audio output found to record from");
        const std::string monitor = impl->output + ".monitor";
        if (monitor.size() >= sizeof(id.pulse)) throw std::runtime_error("The audio output's name is too long");
        monitor.copy(id.pulse, monitor.size());
        config.capture.pDeviceID = &id;
    }

    ma_result result = ma_device_init(&impl->context, &config, &impl->device);
    if (result != MA_SUCCESS)
        throw std::runtime_error(std::format("Couldn't record what the computer plays: {}", ma_result_description(result)));

    result = ma_device_start(&impl->device);
    if (result != MA_SUCCESS){
        ma_device_uninit(&impl->device);
        throw std::runtime_error(std::format("Couldn't start recording what the computer plays: {}",
                                             ma_result_description(result)));
    }
    impl->open = true;
#endif
}

void AudioBuffer::close_device(){
    if (!impl->open) return;
    // Waits for the audio thread's last call to return, so nothing writes
    // into the capture after this
    ma_device_uninit(&impl->device);
    impl->open = false;
}

void AudioBuffer::start(){
    if (!impl->open) open_device();
}

void AudioBuffer::stop(){
    close_device();
    impl->capture = std::make_unique<Capture>(length);
}

bool AudioBuffer::running() const {
    return impl->open;
}

void AudioBuffer::set_seconds(int seconds){
    if (seconds == length) return;
    const bool was_running = impl->open;
    // Frees the old ring before the new one is allocated, so the two are
    // never in memory at once
    close_device();
    impl->capture.reset();
    length = seconds;
    impl->capture = std::make_unique<Capture>(seconds);
    if (was_running) open_device();
}

std::vector<float> AudioBuffer::snapshot() const {
    return impl->capture->ring.snapshot(static_cast<size_t>(length) * SAMPLE_RATE);
}

void AudioBuffer::follow_default_output(){
    if (!impl->open || impl->backend != Backend::System) return;
    const std::string output = default_output(impl->context);
    if (output.empty() || output == impl->output) return;
    // The capture stays, and its GapDetector fills the moment in between
    // with silence
    close_device();
    open_device();
}

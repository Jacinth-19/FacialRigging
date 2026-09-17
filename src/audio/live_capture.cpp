#include "audio/live_capture.h"
#include "audio/fft.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#if FR_HAVE_PORTAUDIO
#include <portaudio.h>
#endif

namespace fr {

namespace {
constexpr double kRingSeconds = 10.0;
#if FR_HAVE_PORTAUDIO
struct PaInit {
    PaError err;
    PaInit() : err(Pa_Initialize()) {}
    ~PaInit() { if (err == paNoError) Pa_Terminate(); }
};
PaInit& paInit() { static PaInit p; return p; }
int paCallback(const void* input, void*, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user) {
    auto* self = static_cast<LiveCapture*>(user);
    if (input) self->pushSamples(static_cast<const float*>(input), frames, 1);
    return paContinue;
}
#endif
} // namespace

LiveCapture::LiveCapture() { smooth_[0] = 1.0f; }
LiveCapture::~LiveCapture() { stop(); }

bool LiveCapture::available() {
#if FR_HAVE_PORTAUDIO
    return paInit().err == paNoError;
#else
    return false;
#endif
}

std::vector<AudioDevice> LiveCapture::listInputDevices(std::string* error) {
    std::vector<AudioDevice> out;
#if FR_HAVE_PORTAUDIO
    if (paInit().err != paNoError) { if (error) *error = Pa_GetErrorText(paInit().err); return out; }
    int n = Pa_GetDeviceCount();
    if (n < 0) { if (error) *error = Pa_GetErrorText(n); return out; }
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
        if (d && d->maxInputChannels > 0) out.push_back({i, d->name, d->maxInputChannels, d->defaultSampleRate});
    }
    if (out.empty() && error) *error = "no audio input devices found";
#else
    if (error) *error = "PortAudio not compiled in (FR_WITH_PORTAUDIO=OFF)";
#endif
    return out;
}

bool LiveCapture::start(int deviceIndex, int sampleRate, std::string* error) {
#if FR_HAVE_PORTAUDIO
    stop();
    if (paInit().err != paNoError) { if (error) *error = Pa_GetErrorText(paInit().err); return false; }
    PaStreamParameters in{};
    in.device = deviceIndex >= 0 ? deviceIndex : Pa_GetDefaultInputDevice();
    if (in.device == paNoDevice) { if (error) *error = "no default input device"; return false; }
    in.channelCount = 1;
    in.sampleFormat = paFloat32;
    in.suggestedLatency = Pa_GetDeviceInfo(in.device)->defaultLowInputLatency;
    sampleRate_ = sampleRate;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ring_.assign(size_t(kRingSeconds * sampleRate_), 0.0f);
        writePos_ = 0; totalWritten_ = 0; lastAnalysed_ = 0; maxRms_ = 1e-3f;
    }
    PaStream* s = nullptr;
    PaError e = Pa_OpenStream(&s, &in, nullptr, sampleRate_, 256, paClipOff, paCallback, this);
    if (e != paNoError) { if (error) *error = Pa_GetErrorText(e); return false; }
    e = Pa_StartStream(s);
    if (e != paNoError) { if (error) *error = Pa_GetErrorText(e); Pa_CloseStream(s); return false; }
    stream_ = s; running_ = true;
    return true;
#else
    (void)deviceIndex; (void)sampleRate;
    if (error) *error = "PortAudio not compiled in (FR_WITH_PORTAUDIO=OFF)";
    return false;
#endif
}

void LiveCapture::stop() {
#if FR_HAVE_PORTAUDIO
    if (stream_) { Pa_StopStream(static_cast<PaStream*>(stream_)); Pa_CloseStream(static_cast<PaStream*>(stream_)); stream_ = nullptr; }
#endif
    running_ = false;
}

void LiveCapture::pushSamples(const float* in, size_t frames, int channels) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ring_.empty()) return;
    float peak = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        float s = 0.0f;
        for (int c = 0; c < channels; ++c) s += in[i * channels + c];
        s /= float(channels);
        peak = std::max(peak, std::abs(s));
        ring_[writePos_] = s;
        writePos_ = (writePos_ + 1) % ring_.size();
    }
    totalWritten_ += frames;
    level_ = 0.8f * level_.load() + 0.2f * peak;
}

std::vector<float> LiveCapture::recent(double seconds) const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = std::min(ring_.size(), size_t(seconds * sampleRate_));
    std::vector<float> out(n);
    size_t start = (writePos_ + ring_.size() - n) % ring_.size();
    for (size_t i = 0; i < n; ++i) out[i] = ring_[(start + i) % ring_.size()];
    return out;
}

LiveCapture::LiveFrame LiveCapture::poll() {
    LiveFrame lf;
    uint64_t total = totalWritten_.load();
    if (!running_ || total == lastAnalysed_ || total < uint64_t(featureConfig.frameSize)) return lf;
    if (total - lastAnalysed_ < uint64_t(featureConfig.hopSize)) return lf;
    lastAnalysed_ = total;
    // Analyse the newest frame as a one-frame AudioBuffer (reuses the offline extractor).
    AudioBuffer buf; buf.sampleRate = sampleRate_; buf.channels = 1;
    buf.samples = recent(double(featureConfig.frameSize) / sampleRate_);
    FeatureConfig cfg = featureConfig; cfg.hopSize = cfg.frameSize; // exactly one frame
    FeatureExtractor fx(cfg);
    FeatureTrack t = fx.extract(buf);
    if (t.frames.empty()) return lf;
    AudioFrameFeatures f = t.frames.front();
    // Running normalisation for loudness (offline mode normalises against the clip maximum).
    maxRms_ = std::max(f.rms, maxRms_ * 0.999f);
    f.loudness = std::clamp(f.rms / maxRms_, 0.0f, 1.0f);
    t.frames[0] = f;
    VisemeMapper fallback;
    const VisemeMapper& mapper = mapper_ ? *mapper_ : fallback;
    auto vis = mapper.map(t);
    lf.features = f;
    lf.viseme = vis.empty() ? VisemeFrame{} : vis.front();
    // temporal smoothing across polls (the mapper only sees one frame at a time here)
    for (size_t i = 0; i < smooth_.size(); ++i) { smooth_[i] = 0.6f * smooth_[i] + 0.4f * lf.viseme.weights[i]; lf.viseme.weights[i] = smooth_[i]; }
    lf.valid = true;
    return lf;
}

} // namespace fr

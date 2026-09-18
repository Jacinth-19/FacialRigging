#include "audio/live_capture.h"
#include "audio/fft.h"
#include "audio/wav_io.h"
#include <chrono>
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
int paCallback(const void* input, void*, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags flags, void* user) {
    auto* self = static_cast<LiveCapture*>(user);
    if (flags & paInputOverflow) self->noteOverrun();
    if (input) self->pushSamples(static_cast<const float*>(input), frames, 1);
    return paContinue;
}
#endif
} // namespace

LiveCapture::LiveCapture() { smooth_[0] = 1.0f; }
LiveCapture::~LiveCapture() { stop(); }

bool LiveCapture::available() {
#if FR_HAVE_PORTAUDIO
    (void)paInit();
    return true; // compiled in; whether a host API / device exists is reported by listInputDevices()
#else
    return false;
#endif
}

std::string LiveCapture::backendInfo() {
#if FR_HAVE_PORTAUDIO
    if (paInit().err != paNoError) return std::string("PortAudio init failed: ") + Pa_GetErrorText(paInit().err);
    std::string s = Pa_GetVersionText();
    s += " | host APIs:";
    int n = Pa_GetHostApiCount();
    if (n <= 0) s += " none (on Linux this means libasound.so.2 could not be dlopen'ed and no OSS device exists)";
    for (int i = 0; i < n; ++i) { const PaHostApiInfo* h = Pa_GetHostApiInfo(i); if (h) s += std::string(" ") + h->name + "(" + std::to_string(h->deviceCount) + ")"; }
    return s;
#else
    return "PortAudio not compiled in";
#endif
}

std::vector<AudioDevice> LiveCapture::listInputDevices(std::string* error) {
    std::vector<AudioDevice> out;
#if FR_HAVE_PORTAUDIO
    if (paInit().err != paNoError) { if (error) *error = Pa_GetErrorText(paInit().err); }
    else {
        int n = Pa_GetDeviceCount();
        if (n < 0) { if (error) *error = Pa_GetErrorText(n); }
        for (int i = 0; i < n; ++i) {
            const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
            if (d && d->maxInputChannels > 0) out.push_back({i, d->name, d->maxInputChannels, d->defaultSampleRate});
        }
        if (out.empty() && error && error->empty()) *error = "no audio input devices (" + backendInfo() + ")";
    }
#else
    if (error) *error = "PortAudio not compiled in (FR_WITH_PORTAUDIO=OFF)";
#endif
    out.push_back({kTestSignalDevice, "Test signal (synthetic speech loop)", 1, 16000.0});
    return out;
}

bool LiveCapture::start(int deviceIndex, int sampleRate, std::string* error) {
    stop();
    if (deviceIndex == kTestSignalDevice) {
        sampleRate_ = sampleRate;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ring_.assign(size_t(kRingSeconds * sampleRate_), 0.0f);
            writePos_ = 0; totalWritten_ = 0; lastAnalysed_ = 0; maxRms_ = 1e-3f;
        }
        AudioBuffer speech = synthesizeTestSpeech(3.0, sampleRate_);
        std::vector<float> mono = speech.mono();
        mono.resize(mono.size() + size_t(std::max(0.0f, testGapSeconds) * sampleRate_), 0.0f);   // silence between repeats (exercises the gate)
        const float noiseAmp = testNoiseDb > -99.0f ? std::pow(10.0f, testNoiseDb / 20.0f) * 1.7320508f : 0.0f; // uniform noise with that RMS
        conditioner.reset(sampleRate_); deviceLatencyMs_ = 0.0f; overruns_ = 0; lastCallbackTime_ = 0.0;
        fakeRun_ = true; running_ = true;
        fakeThread_.reset(new std::thread([this, mono, noiseAmp]() {
            const size_t block = size_t(std::clamp(latency.blockFrames, 64, 1024)); size_t pos = 0; uint32_t rng = 12345u;
            auto next = std::chrono::steady_clock::now();
            std::vector<float> buf(block);
            while (fakeRun_) {
                for (size_t i = 0; i < block; ++i) { rng = rng * 1664525u + 1013904223u; float nz = (float(rng >> 8) / 16777216.0f - 0.5f) * 2.0f * noiseAmp; buf[i] = mono[pos] + nz; pos = (pos + 1) % mono.size(); }
                pushSamples(buf.data(), block, 1);
                next += std::chrono::microseconds(int64_t(1e6 * double(block) / sampleRate_));
                std::this_thread::sleep_until(next);
            }
        }));
        return true;
    }
#if FR_HAVE_PORTAUDIO
    if (paInit().err != paNoError) { if (error) *error = Pa_GetErrorText(paInit().err); return false; }
    PaStreamParameters in{};
    in.device = deviceIndex >= 0 ? deviceIndex : Pa_GetDefaultInputDevice();
    if (in.device == paNoDevice) { if (error) *error = "no default input device"; return false; }
    in.channelCount = 1;
    in.sampleFormat = paFloat32;
    in.suggestedLatency = latency.latencyPreset == 0 ? Pa_GetDeviceInfo(in.device)->defaultLowInputLatency : Pa_GetDeviceInfo(in.device)->defaultHighInputLatency;
    sampleRate_ = sampleRate;
    conditioner.reset(sampleRate_); overruns_ = 0; lastCallbackTime_ = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ring_.assign(size_t(kRingSeconds * sampleRate_), 0.0f);
        writePos_ = 0; totalWritten_ = 0; lastAnalysed_ = 0; maxRms_ = 1e-3f;
    }
    PaStream* s = nullptr;
    PaError e = Pa_OpenStream(&s, &in, nullptr, sampleRate_, static_cast<unsigned long>(std::clamp(latency.blockFrames, 64, 1024)), paClipOff, paCallback, this);
    if (e != paNoError) { if (error) *error = Pa_GetErrorText(e); return false; }
    e = Pa_StartStream(s);
    if (e != paNoError) { if (error) *error = Pa_GetErrorText(e); Pa_CloseStream(s); return false; }
    if (const PaStreamInfo* si = Pa_GetStreamInfo(s)) deviceLatencyMs_ = float(si->inputLatency * 1000.0);
    stream_ = s; running_ = true;
    return true;
#else
    (void)deviceIndex; (void)sampleRate;
    if (error) *error = "PortAudio not compiled in (FR_WITH_PORTAUDIO=OFF)";
    return false;
#endif
}

void LiveCapture::stop() {
    if (fakeThread_) { fakeRun_ = false; fakeThread_->join(); fakeThread_.reset(); }
#if FR_HAVE_PORTAUDIO
    if (stream_) { Pa_StopStream(static_cast<PaStream*>(stream_)); Pa_CloseStream(static_cast<PaStream*>(stream_)); stream_ = nullptr; }
#endif
    running_ = false;
}

void LiveCapture::pushSamples(const float* in, size_t frames, int channels) {
    {   // callback cadence (measured, for the latency readout)
        double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        double prev = lastCallbackTime_.exchange(now);
        if (prev > 0.0) { float ms = float((now - prev) * 1000.0); float cur = callbackIntervalMs_.load(); callbackIntervalMs_ = cur <= 0 ? ms : 0.9f * cur + 0.1f * ms; }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (ring_.empty()) return;
    scratch_.resize(frames);
    float peak = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        float s = 0.0f;
        for (int c = 0; c < channels; ++c) s += in[i * channels + c];
        s /= float(channels);
        peak = std::max(peak, std::abs(s));
        scratch_[i] = s;
    }
    lastSpeech_ = conditioner.process(scratch_.data(), frames);
    { std::lock_guard<std::mutex> sl(statusMutex_); micStatus_ = conditioner.status(); }
    for (size_t i = 0; i < frames; ++i) { ring_[writePos_] = scratch_[i]; writePos_ = (writePos_ + 1) % ring_.size(); }
    totalWritten_ += frames;
    level_ = 0.8f * level_.load() + 0.2f * peak;
}

MicStatus LiveCapture::micStatus() const { std::lock_guard<std::mutex> sl(statusMutex_); return micStatus_; }

LiveCapture::LatencyReport LiveCapture::latencyReport() const {
    LatencyReport r; const float sr = float(std::max(1, sampleRate_));
    r.deviceMs = deviceLatencyMs_;
    r.blockMs = 1000.0f * float(std::clamp(latency.blockFrames, 64, 1024)) / sr;
    r.windowMs = 1000.0f * 0.5f * float(featureConfig.frameSize) / sr;   // features are centred in the window
    r.hopMs = 1000.0f * float(featureConfig.hopSize) / sr;
    // exponential smoothing 'a' per poll (hop): time constant = -hop / ln(a)
    r.smoothingMs = latency.smoothing > 0.0f && latency.smoothing < 1.0f ? -r.hopMs / std::log(latency.smoothing) : 0.0f;
    r.estimatedMs = r.deviceMs + r.blockMs + r.windowMs + r.hopMs + r.smoothingMs;
    r.measuredAgeMs = measuredAgeMs_; r.callbackIntervalMs = callbackIntervalMs_.load(); r.overruns = overruns_.load();
    return r;
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
    {   // measured ring age: how long ago the newest sample was captured (callback cadence granularity)
        double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        double lc = lastCallbackTime_.load(); if (lc > 0.0) measuredAgeMs_ = float((now - lc) * 1000.0);
    }
    // Analyse a short rolling window (last ~10 hops) so the mapper's own temporal smoothing and
    // pitch/voicing context behave as in offline mode; the newest frame is the live result.
    const int hopsInWindow = 10;
    AudioBuffer buf; buf.sampleRate = sampleRate_; buf.channels = 1;
    buf.samples = recent(double(featureConfig.frameSize + featureConfig.hopSize * (hopsInWindow - 1)) / sampleRate_);
    FeatureExtractor fx(featureConfig);
    FeatureTrack t = fx.extract(buf);
    if (t.frames.empty()) return lf;
    // Running loudness normalisation (offline mode normalises against the clip maximum).
    for (auto& f : t.frames) maxRms_ = std::max(f.rms, maxRms_);
    maxRms_ *= 0.999f; maxRms_ = std::max(maxRms_, 1e-3f);
    for (auto& f : t.frames) f.loudness = std::clamp(f.rms / maxRms_, 0.0f, 1.0f);
    VisemeMapper fallback;
    const VisemeMapper& mapper = mapper_ ? *mapper_ : fallback;
    auto vis = mapper.map(t);
    lf.features = t.frames.back();
    lf.features.time = float(double(total) / sampleRate_);
    lf.viseme = vis.empty() ? VisemeFrame{} : vis.back();
    // light smoothing across polls
    const float a = std::clamp(latency.smoothing, 0.0f, 0.95f);
    for (size_t i = 0; i < smooth_.size(); ++i) { smooth_[i] = a * smooth_[i] + (1.0f - a) * lf.viseme.weights[i]; lf.viseme.weights[i] = smooth_[i]; }
    // Gate closed -> force silence (the ring already holds muted samples, this just skips the decay tail)
    if (conditioner.settings.gate && !micStatus().speech && conditioner.settings.gateFloorGain <= 0.0f) { for (auto& w : lf.viseme.weights) w *= 0.7f; lf.viseme.weights[0] = std::max(lf.viseme.weights[0], 1.0f - 0.7f * (1.0f - lf.viseme.weights[0])); }
    lf.valid = true;
    return lf;
}

} // namespace fr

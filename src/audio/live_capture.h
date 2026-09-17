#pragma once
#include "audio/features.h"
#include "audio/viseme_mapper.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fr {

struct AudioDevice { int index; std::string name; int maxInputChannels; double defaultSampleRate; };

/// Live microphone capture (PortAudio). Audio arrives on the PortAudio callback thread and is
/// pushed into a lock-free-ish ring buffer; `poll()` (called from the main thread) runs feature
/// extraction + viseme mapping on the newest analysis window and returns the current frame.
/// Compiles to an "unavailable" stub when FR_HAVE_PORTAUDIO == 0.
class LiveCapture {
public:
    LiveCapture();
    ~LiveCapture();
    static bool available();
    static std::vector<AudioDevice> listInputDevices(std::string* error = nullptr);

    bool start(int deviceIndex = -1, int sampleRate = 16000, std::string* error = nullptr);
    void stop();
    bool running() const { return running_; }
    int sampleRate() const { return sampleRate_; }

    struct LiveFrame {
        AudioFrameFeatures features;
        VisemeFrame viseme;
        bool valid = false;
    };
    /// Analyses the latest window; returns quickly when no new audio has arrived since last poll.
    LiveFrame poll();
    /// Copy of the last `seconds` of captured mono audio (for waveform display / recording).
    std::vector<float> recent(double seconds) const;
    float inputLevel() const { return level_; }
    void setMapper(std::shared_ptr<VisemeMapper> m) { mapper_ = std::move(m); }
    FeatureConfig featureConfig{};

    // Called from the audio thread (public for the C callback).
    void pushSamples(const float* interleaved, size_t frames, int channels);

private:
    void* stream_ = nullptr;
    std::atomic<bool> running_{false};
    int sampleRate_ = 16000;
    mutable std::mutex mutex_;
    std::vector<float> ring_;
    size_t writePos_ = 0;
    std::atomic<uint64_t> totalWritten_{0};
    uint64_t lastAnalysed_ = 0;
    std::atomic<float> level_{0.0f};
    std::shared_ptr<VisemeMapper> mapper_;
    std::array<float, size_t(Viseme::Count)> smooth_{};
    float maxRms_ = 1e-3f;
};

} // namespace fr

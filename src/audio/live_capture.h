#pragma once
#include "audio/features.h"
#include "audio/viseme_mapper.h"
#include "audio/mic_conditioner.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <array>
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

    /// Latency-related knobs. Total glass-to-mouth latency ~= device input latency + block/sampleRate
    /// + analysis window centre (frameSize/2) + hop + smoothing + one display frame; see latency().
    struct LatencySettings {
        int blockFrames = 256;           ///< PortAudio callback block (64..1024). Smaller = lower latency, more callbacks
        int latencyPreset = 0;           ///< 0 device low latency, 1 default high latency (safer on USB mics)
        float smoothing = 0.5f;          ///< viseme smoothing across polls (0 none .. 0.9 heavy)
    };
    LatencySettings latency;
    struct LatencyReport {
        float deviceMs = 0;              ///< what PortAudio reports for the opened stream (0 for the test signal)
        float blockMs = 0, windowMs = 0, hopMs = 0, smoothingMs = 0;
        float estimatedMs = 0;           ///< sum of the above
        float measuredAgeMs = 0;         ///< age of the newest sample at the last poll (ring latency actually observed)
        float callbackIntervalMs = 0;    ///< measured mean time between audio callbacks
        int overruns = 0;                ///< PortAudio input overflow flags seen
    };
    LatencyReport latencyReport() const;
    /// Microphone conditioning (high-pass, VAD gate, AGC) applied on the audio thread before the ring.
    MicConditioner conditioner;
    MicStatus micStatus() const;
    /// Optional test-signal degradation: mixes white noise at `noiseDb` dBFS and pauses between repeats.
    float testNoiseDb = -100.0f; float testGapSeconds = 0.8f;

    bool start(int deviceIndex = -1, int sampleRate = 16000, std::string* error = nullptr);
    /// Device index that selects the built-in test signal (synthetic speech looped through the
    /// exact capture path) - lets the live pipeline be exercised on machines without a microphone.
    static constexpr int kTestSignalDevice = -2;
    /// Human-readable summary of what PortAudio found (host APIs, default input) for diagnostics.
    static std::string backendInfo();
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
    void noteOverrun() { ++overruns_; }

private:
    void* stream_ = nullptr;
    // test-signal source (no PortAudio): a thread pushes synthetic speech in real time
    std::unique_ptr<std::thread> fakeThread_;
    std::atomic<bool> fakeRun_{false};
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
    float deviceLatencyMs_ = 0.0f; std::atomic<int> overruns_{0};
    std::atomic<double> lastCallbackTime_{0.0}; std::atomic<float> callbackIntervalMs_{0.0f};
    float measuredAgeMs_ = 0.0f;
    std::vector<float> scratch_;
    mutable std::mutex statusMutex_; MicStatus micStatus_;
    bool lastSpeech_ = false;
};

} // namespace fr

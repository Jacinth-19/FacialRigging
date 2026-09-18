// Microphone self-test: records from a device through the exact live-capture path, measures the
// noise floor, speech level / SNR, clipping, DC offset, spectral balance, VAD statistics, callback
// cadence / overruns and end-to-end latency, then writes a Markdown + JSON report the user can send
// back (fr_cli --mic-check). Runs unattended: a scripted sequence of "stay silent" / "read this"
// phases with console prompts.
#pragma once
#include "audio/live_capture.h"
#include <string>
#include <vector>

namespace fr {

struct MicCheckOptions {
    int device = -1;                    ///< PortAudio input index (-1 default, LiveCapture::kTestSignalDevice = synthetic)
    int sampleRate = 16000;
    double silenceSeconds = 3.0;        ///< phase 1: noise floor
    double speechSeconds = 8.0;         ///< phase 2: read the pangram
    double latencySeconds = 4.0;        ///< phase 3: clap / tap latency probe (0 = skip)
    std::string reportPath = "mic_report.md";   ///< also writes <stem>.json and <stem>.wav
    bool saveRecording = true;
    bool quiet = false;                 ///< no console prompts (tests)
};

struct MicCheckPhase {
    std::string name; double seconds = 0.0;
    float rmsDb = -100.0f, peakDb = -100.0f, noiseFloorDb = -100.0f, snrDb = 0.0f, dcOffset = 0.0f;
    float clipFraction = 0.0f;          ///< share of samples at |x| >= 0.99
    float speechFraction = 0.0f;        ///< share of 20 ms blocks the VAD judged speech
    float lowBandDb = -100.0f, midBandDb = -100.0f, highBandDb = -100.0f;   ///< <300 Hz, 300..3400 Hz, >3400 Hz energy (dBFS)
    float humDb = -100.0f;              ///< strongest of 50/60 Hz (+harmonics) relative to the mid band (dB, >0 = hum stands out)
    int polls = 0, framesWithVisemes = 0, speakingFrames = 0;
    std::vector<std::string> visemeHistogram;   ///< "name: count" of dominant visemes while speaking
};

struct MicCheckResult {
    bool ok = false; std::string error;
    std::string deviceName, backend; int sampleRate = 0;
    LiveCapture::LatencyReport latency;
    std::vector<MicCheckPhase> phases;
    float measuredLatencyMs = -1.0f;    ///< clap probe: onset-to-poll delay (median), -1 when not measured / no clap heard
    int clapsHeard = 0;
    float agcGainDb = 0.0f;
    std::vector<std::string> verdicts;  ///< human-readable findings ("OK: ...", "WARN: ...", "FAIL: ...")
    int warnings = 0, failures = 0;
    std::string markdown() const;
    std::string json() const;
};

/// Runs the whole self-test (blocking, ~15 s with defaults). `mapper` may be null (no viseme stats).
MicCheckResult runMicCheck(const MicCheckOptions& opt, std::shared_ptr<VisemeMapper> mapper = nullptr);
/// Writes report.md / .json / .wav next to opt.reportPath; returns the markdown path.
std::string writeMicReport(const MicCheckResult& r, const MicCheckOptions& opt, const std::vector<float>& recording, std::string* error = nullptr);

/// Pure analysis of one mono buffer (exposed for tests / offline analysis of a WAV).
MicCheckPhase analyseMicPhase(const std::string& name, const std::vector<float>& mono, int sampleRate);

} // namespace fr

// Microphone front-end: DC/high-pass, noise-floor tracking, voice activity detection (VAD) with
// hang time, and automatic gain control (AGC). Runs sample-block by sample-block on the audio
// thread; all state is plain floats so it is cheap and allocation-free.
#pragma once
#include <cstddef>
#include <vector>

namespace fr {

struct MicSettings {
    bool highPass = true;         ///< 80 Hz one-pole high-pass (removes DC / rumble / handling noise)
    bool gate = true;             ///< VAD noise gate
    float gateThresholdDb = 8.0f; ///< speech must exceed the tracked noise floor by this many dB
    float gateHangMs = 180.0f;    ///< keep the gate open this long after speech stops (avoids chattering on stops)
    float gateAttackMs = 5.0f, gateReleaseMs = 60.0f;  ///< gain ramps
    float gateFloorGain = 0.0f;   ///< residual gain while closed (0 = hard mute, 0.1 = -20 dB)
    bool agc = true;              ///< automatic gain control toward `agcTargetDb`
    float agcTargetDb = -18.0f;   ///< target RMS while speaking (dBFS)
    float agcMaxGainDb = 24.0f, agcMinGainDb = -12.0f;
    float agcAttackMs = 30.0f, agcReleaseMs = 400.0f;  ///< gain decreases fast, increases slowly
    float noiseFloorRiseMs = 2000.0f, noiseFloorFallMs = 100.0f; ///< min-statistics style floor tracker
};

struct MicStatus {
    float inputRmsDb = -100.0f;   ///< pre-processing RMS of the last block
    float outputRmsDb = -100.0f;
    float noiseFloorDb = -100.0f; ///< tracked floor
    float snrDb = 0.0f;           ///< inputRmsDb - noiseFloorDb
    bool speech = false;          ///< VAD decision (after hang)
    float gateGain = 1.0f;        ///< current gate gain 0..1
    float agcGainDb = 0.0f;       ///< current AGC gain
    float speechFraction = 0.0f;  ///< share of recent blocks judged speech (0..1, ~2 s window)
};

class MicConditioner {
public:
    MicSettings settings;
    void reset(int sampleRate);
    /// In-place processing of a mono block. Returns the VAD decision for the block.
    bool process(float* samples, size_t n);
    const MicStatus& status() const { return status_; }
    static float dbOf(float rms);
    static float rmsOf(const float* s, size_t n);

private:
    int sr_ = 16000;
    float hpPrevIn_ = 0, hpPrevOut_ = 0, hpCoef_ = 0.97f;
    float floorLin_ = 1e-4f;      ///< tracked noise RMS (linear)
    float envLin_ = 0.0f;         ///< fast RMS envelope
    float gateGain_ = 0.0f;
    float hangLeft_ = 0.0f;       ///< seconds of hang remaining
    float agcGainLin_ = 1.0f;
    float speechAvg_ = 0.0f;
    MicStatus status_;
};

} // namespace fr

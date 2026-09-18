#include "audio/mic_conditioner.h"
#include <algorithm>
#include <cmath>

namespace fr {

float MicConditioner::dbOf(float rms) { return 20.0f * std::log10(std::max(rms, 1e-6f)); }
float MicConditioner::rmsOf(const float* s, size_t n) { double a = 0; for (size_t i = 0; i < n; ++i) a += double(s[i]) * s[i]; return n ? float(std::sqrt(a / double(n))) : 0.0f; }

void MicConditioner::reset(int sampleRate) {
    sr_ = std::max(1000, sampleRate);
    hpPrevIn_ = hpPrevOut_ = 0; hpCoef_ = std::exp(-2.0f * 3.14159265f * 80.0f / float(sr_));
    floorLin_ = 3e-3f; envLin_ = 0; gateGain_ = 0; hangLeft_ = 0; agcGainLin_ = 1.0f; speechAvg_ = 0;
    status_ = MicStatus{};
}

namespace { float coef(float ms, float blockSec) { return ms <= 0 ? 1.0f : 1.0f - std::exp(-blockSec * 1000.0f / ms); } }

bool MicConditioner::process(float* s, size_t n) {
    if (n == 0) return status_.speech;
    const float blockSec = float(n) / float(sr_);
    // 1. high-pass (one-pole, 80 Hz)
    if (settings.highPass) {
        for (size_t i = 0; i < n; ++i) { float x = s[i]; float y = hpCoef_ * (hpPrevOut_ + x - hpPrevIn_); hpPrevIn_ = x; hpPrevOut_ = y; s[i] = y; }
    }
    const float rms = rmsOf(s, n);
    status_.inputRmsDb = dbOf(rms);
    // 2. envelope + noise floor (rises slowly, falls fast -> follows the minimum = noise)
    envLin_ += coef(10.0f, blockSec) * (rms - envLin_);
    if (rms < floorLin_) floorLin_ += coef(settings.noiseFloorFallMs, blockSec) * (rms - floorLin_);
    else floorLin_ += coef(settings.noiseFloorRiseMs, blockSec) * (rms - floorLin_);
    floorLin_ = std::clamp(floorLin_, 1e-5f, 0.5f);
    status_.noiseFloorDb = dbOf(floorLin_);
    status_.snrDb = status_.inputRmsDb - status_.noiseFloorDb;
    // 3. VAD: energy above the floor by the threshold, with hysteresis (-3 dB to stay open) and hang time
    bool above = status_.snrDb > (status_.speech ? settings.gateThresholdDb - 3.0f : settings.gateThresholdDb) && status_.inputRmsDb > -60.0f;
    if (above) hangLeft_ = settings.gateHangMs * 0.001f; else hangLeft_ = std::max(0.0f, hangLeft_ - blockSec);
    bool speech = above || hangLeft_ > 0.0f;
    status_.speech = speech;
    speechAvg_ += coef(2000.0f, blockSec) * ((speech ? 1.0f : 0.0f) - speechAvg_); status_.speechFraction = speechAvg_;
    // 4. gate gain ramp
    float targetGate = settings.gate ? (speech ? 1.0f : settings.gateFloorGain) : 1.0f;
    gateGain_ += coef(targetGate > gateGain_ ? settings.gateAttackMs : settings.gateReleaseMs, blockSec) * (targetGate - gateGain_);
    status_.gateGain = gateGain_;
    // 5. AGC: only adapt while speaking, so silence isn't pumped up to the target
    if (settings.agc) {
        if (above && rms > 1e-5f) {
            float wantDb = std::clamp(settings.agcTargetDb - status_.inputRmsDb, settings.agcMinGainDb, settings.agcMaxGainDb);
            float haveDb = dbOf(agcGainLin_) ;
            float c = coef(wantDb < haveDb ? settings.agcAttackMs : settings.agcReleaseMs, blockSec);
            agcGainLin_ = std::pow(10.0f, (haveDb + c * (wantDb - haveDb)) / 20.0f);
        }
    } else agcGainLin_ = 1.0f;
    status_.agcGainDb = dbOf(agcGainLin_);
    const float g = agcGainLin_ * gateGain_;
    for (size_t i = 0; i < n; ++i) s[i] = std::clamp(s[i] * g, -1.0f, 1.0f);
    status_.outputRmsDb = dbOf(rms * g);
    return speech;
}

} // namespace fr

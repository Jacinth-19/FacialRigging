#include "anim/idle_motion.h"
#include <algorithm>
#include <cmath>

namespace fr {

namespace {
struct Rng { // xorshift32, deterministic per seed
    unsigned s;
    float uniform() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return float(s & 0xffffff) / float(0x1000000); }
};
}

float IdleMotionModel::blinkProfile(float s) const {
    const float c = settings.closeSec, h = settings.holdSec, o = settings.openSec;
    if (s < 0.0f) return 0.0f;
    if (s < c) { float x = s / c; return x * x * (3 - 2 * x); }           // fast close (smoothstep)
    if (s < c + h) return 1.0f;
    if (s < c + h + o) { float x = (s - c - h) / o; return 1.0f - x * x * (2.0f - x); } // slower, decelerating open
    return 0.0f;
}

std::vector<IdleSample> IdleMotionModel::bake(const FeatureTrack& track, float dt, const std::vector<float>* gazePitch, const std::vector<int>* saccades) const {
    const int frames = int(track.duration / dt) + 1;
    std::vector<IdleSample> out(static_cast<size_t>(std::max(frames, 0)));
    if (frames <= 0 || dt <= 0.0f) return out;
    Rng rng{settings.seed * 2654435761u + 1u};

    // --- speech activity envelope + pause detection
    std::vector<float> speaking(size_t(frames), 0.0f);
    float env = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const AudioFrameFeatures* f = track.at(i * dt);
        float l = f ? f->loudness : 0.0f;
        env += ((l > 0.08f ? 1.0f : 0.0f) - env) * std::min(1.0f, 6.0f * dt);
        speaking[size_t(i)] = env;
    }
    // A "pause" is a frame where the envelope just dropped after speech (clause boundary).
    std::vector<float> pauseCue(size_t(frames), 0.0f);
    for (int i = 1; i < frames; ++i) {
        bool wasSpeaking = speaking[size_t(i - 1)] > 0.5f, nowQuiet = speaking[size_t(i)] <= 0.5f;
        if (wasSpeaking && nowQuiet) for (int k = i; k < std::min(frames, i + int(0.25f / dt)); ++k) pauseCue[size_t(k)] = 1.0f;
    }
    std::vector<char> saccadeCue(size_t(frames), 0);
    if (saccades) for (int s : *saccades) for (int k = s; k < std::min(frames, s + int(0.12f / dt)); ++k) if (k >= 0) saccadeCue[size_t(k)] = 1;

    // --- blinks: inhomogeneous Poisson process with refractory period
    float sinceBlink = 1.5f;      // start "ready-ish" so the first blink comes naturally
    float blinkT = -10.0f;        // start time of the active blink
    int pendingDouble = 0;
    const float dur = blinkDuration();
    if (settings.blinkRate > 0.0f) {
        for (int i = 0; i < frames; ++i) {
            float t = i * dt;
            sinceBlink += dt;
            bool active = t - blinkT < dur;
            if (!active && pendingDouble > 0 && sinceBlink > 0.12f) { blinkT = t; sinceBlink = 0.0f; --pendingDouble; out[size_t(i)].blinkStart = true; active = true; }
            if (!active) {
                // Baseline: Weibull(k=2) renewal process - hazard grows linearly with time since the
                // last blink (mean interval ~3 s while speaking, ~4.5 s while listening; ~17-20/min).
                const float meanInterval = (speaking[size_t(i)] > 0.5f ? 3.0f : 4.5f) / std::max(settings.blinkRate, 1e-3f);
                float hazard = 2.0f * sinceBlink / (meanInterval * meanInterval);
                if (sinceBlink < 0.3f) hazard = 0.0f;                                  // refractory
                if (saccadeCue[size_t(i)]) hazard *= 3.0f;                            // gaze shift
                if (sinceBlink > 7.0f) hazard = std::max(hazard, 3.0f);               // nobody stares > 7 s
                float p = 1.0f - std::exp(-hazard * dt);
                // Clause boundary: people blink at ~70 % of speech pauses. One draw at the pause
                // onset (not a per-frame hazard) so the probability does not depend on frame rate.
                bool pauseOnset = pauseCue[size_t(i)] > 0.0f && (i == 0 || pauseCue[size_t(i - 1)] == 0.0f);
                if (pauseOnset && sinceBlink > 0.6f) p = std::max(p, std::min(0.95f, 0.175f * settings.pauseBlinkBoost));
                if (rng.uniform() < p) { blinkT = t; sinceBlink = 0.0f; out[size_t(i)].blinkStart = true; if (rng.uniform() < settings.doubleBlinkProb) pendingDouble = 1; }
            }
            out[size_t(i)].blink = blinkProfile(t - blinkT);
        }
    }
    // --- eyelid follows downward gaze
    if (gazePitch && settings.lidFollowGaze > 0.0f)
        for (int i = 0; i < frames && size_t(i) < gazePitch->size(); ++i) {
            float down = std::max(0.0f, -(*gazePitch)[size_t(i)]);
            out[size_t(i)].blink = std::max(out[size_t(i)].blink, std::min(0.45f, settings.lidFollowGaze * down));
        }

    // --- breathing: phase advances faster while speaking; asymmetric (inhale 40 % / exhale 60 %)
    if (settings.breathing > 0.0f) {
        float phase = 0.15f;
        for (int i = 0; i < frames; ++i) {
            float rate = settings.breathRateHz * (1.0f + 0.3f * speaking[size_t(i)]);
            // Before a phrase starts (quiet -> speaking transition ahead), snap to an inhale.
            phase += rate * dt; if (phase >= 1.0f) phase -= 1.0f;
            float b = phase < 0.4f ? std::sin(phase / 0.4f * float(M_PI) * 0.5f)                         // inhale rise 0->1
                                   : std::cos((phase - 0.4f) / 0.6f * float(M_PI) * 0.5f);                 // exhale fall 1->0
            out[size_t(i)].breath = (b * 2.0f - 1.0f) * settings.breathing;
            // inhale cue: peaks right at the phrase onset (speaking envelope rising through 0.3)
            if (i > 0 && speaking[size_t(i - 1)] <= 0.3f && speaking[size_t(i)] > 0.3f)
                for (int k = std::max(0, i - int(0.30f / dt)); k <= i; ++k) { float x = 1.0f - float(i - k) / std::max(1.0f, 0.30f / dt); out[size_t(k)].inhaleCue = std::max(out[size_t(k)].inhaleCue, x * x); }
        }
    }
    return out;
}

} // namespace fr

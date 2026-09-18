#pragma once
#include "audio/features.h"
#include <vector>

namespace fr {

/// Physiologically-motivated blink + breathing layer.
///
/// Blinks: a stochastic process whose hazard rises with time since the last blink (refractory
/// ~0.3 s, mean interval ~3-4 s while speaking, longer when silent-attentive), strongly
/// increased at speech pauses / phrase boundaries (people blink at clause ends) and at gaze
/// shifts. ~15 % of blinks are double blinks. Each blink has a fast close (~80 ms), brief hold,
/// slower open (~150 ms) - the asymmetric profile from eyelid EMG studies, not a sine.
/// Eyelid follow: the upper lid tracks vertical gaze (looking down partially closes the lids).
/// Breathing: ~0.25 Hz diaphragmatic cycle, slightly faster while speaking; inhale is quicker
/// than exhale; drives a small chest/head pitch and a brief nostril/lip-part cue on inhale
/// before long phrases.
struct IdleMotionSettings {
    float blinkRate = 1.0f;          ///< multiplier on blink frequency (0 disables)
    float pauseBlinkBoost = 4.0f;    ///< pause-onset blink probability = 0.175 * boost (default 0.7)
    float doubleBlinkProb = 0.15f;
    float closeSec = 0.08f, holdSec = 0.03f, openSec = 0.15f;
    float lidFollowGaze = 0.6f;      ///< EyeBlink weight per radian of downward gaze pitch (lids follow the eyes)
    float breathing = 1.0f;          ///< 0 disables breathing motion
    float breathRateHz = 0.25f;      ///< resting rate; speaking raises it ~30 %
    unsigned seed = 7;
};

struct IdleSample { float blink = 0.0f; float breath = 0.0f; float inhaleCue = 0.0f; bool blinkStart = false; };

class IdleMotionModel {
public:
    explicit IdleMotionModel(IdleMotionSettings s = {}) : settings(s) {}
    IdleMotionSettings settings;
    /// Bakes blink (0..1 lid closure) and breathing (-1 exhale .. +1 inhale) samples at `dt` over
    /// the track. `gazePitchRad(t)` (looking down < 0) and `saccadeAt` frames (indices where gaze
    /// jumped) let blinks coincide with gaze shifts and lids follow gaze.
    std::vector<IdleSample> bake(const FeatureTrack& track, float dt, const std::vector<float>* gazePitchRad = nullptr, const std::vector<int>* saccadeFrames = nullptr) const;
    /// Single blink profile (0..1) at time `s` since blink start; returns 0 when finished.
    float blinkProfile(float s) const;
    float blinkDuration() const { return settings.closeSec + settings.holdSec + settings.openSec; }
};

} // namespace fr

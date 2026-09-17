#pragma once
#include "anim/animation_clip.h"
#include "audio/features.h"
#include "audio/viseme_mapper.h"

namespace fr {

class Rig;

struct LipSyncSettings {
    float frameRate = 30.0f;
    float intensity = 1.0f;          ///< global mouth-shape multiplier
    float jawFromLoudness = 0.35f;   ///< extra jaw opening proportional to loudness
    float browFromPitch = 0.25f;     ///< brow raise proportional to pitch above the speaker median
    float smileBias = 0.0f;          ///< constant added to MouthSmile
    float blinkIntervalSec = 3.5f;   ///< 0 disables procedural blinks
    float blinkDurationSec = 0.15f;
    int smoothingRadiusFrames = 1;   ///< box-filter radius applied to baked curves
    float jawBoneDegrees = 12.0f;    ///< peak jaw bone rotation (x-axis) at full JawOpen
};

/// Turns acoustic features + viseme probabilities into a baked AnimationClip that
/// targets the default face rig's blendshape names and Jaw bone.
class LipSyncGenerator {
public:
    explicit LipSyncGenerator(LipSyncSettings s = {}) : settings(s) {}
    LipSyncSettings settings;
    AnimationClip generate(const FeatureTrack& features, const std::vector<VisemeFrame>& visemes, const Rig& rig) const;
    /// Convenience: extract features, map visemes, then generate.
    AnimationClip generate(const AudioBuffer& audio, const Rig& rig, FeatureTrack* outFeatures = nullptr) const;
};

} // namespace fr

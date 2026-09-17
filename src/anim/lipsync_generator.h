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
    float jawShapeScale = 1.0f;      ///< multiplier on the JawOpen blendshape (set <1 when the shape already includes jaw drop, e.g. authored ARKit sets)
    // --- performance layer (on top of the phonetic mouth shapes)
    std::string emotion = "neutral"; ///< neutral | happy | sad | angry | surprised
    float emotionAmount = 0.0f;      ///< 0..1 strength of the emotion preset
    float headMotion = 0.5f;         ///< 0..1 audio-driven head nods / sways (Head bone)
    float gazeMotion = 0.5f;         ///< 0..1 saccades + slow gaze drift (eye bones, when present)
    unsigned seed = 1;               ///< deterministic randomness for saccades / head sway
};

/// Static blendshape offsets for an emotion preset (added to the lip-sync curves).
struct ExpressionPreset { const char* name; float smile, frown, browRaise, browDown, eyeWide, jaw, lipsPress, pucker; };
const ExpressionPreset* findExpressionPreset(const std::string& name);   ///< nullptr when unknown
extern const ExpressionPreset kExpressionPresets[];
extern const int kExpressionPresetCount;

/// Turns acoustic features + viseme probabilities into a baked AnimationClip that
/// targets the default face rig's blendshape names and Jaw bone.
class LipSyncGenerator {
public:
    explicit LipSyncGenerator(LipSyncSettings s = {}) : settings(s) {}
    LipSyncSettings settings;
    AnimationClip generate(const FeatureTrack& features, const std::vector<VisemeFrame>& visemes, const Rig& rig) const;
    /// Convenience: extract features, map visemes, then generate.
    AnimationClip generate(const AudioBuffer& audio, const Rig& rig, FeatureTrack* outFeatures = nullptr) const;
    /// Blendshape weights (JawOpen, Smile, Pucker, Wide, LipsPress, Funnel) for one viseme frame.
    struct MouthPose { float jawOpen = 0, smile = 0, pucker = 0, wide = 0, lipsPress = 0, funnel = 0; };
    MouthPose mouthPose(const VisemeFrame& v, float loudness) const;
    /// Live path: pose the rig (blend weights + jaw bone) directly from one viseme frame.
    void applyVisemeToRig(const VisemeFrame& v, const AudioFrameFeatures& f, Rig& rig) const;
};

} // namespace fr

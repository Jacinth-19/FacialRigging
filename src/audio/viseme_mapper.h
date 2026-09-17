#pragma once
#include "audio/features.h"
#include <array>
#include <string>
#include <vector>

namespace fr {

/// Compact viseme set (a subset of the Preston Blair / Oculus sets) that covers
/// the mouth shapes distinguishable without a phoneme recogniser.
enum class Viseme : int { Silence = 0, AA, EE, IH, OH, UW, MBP, FV, L_TH, Count };
const char* visemeName(Viseme v);

/// Blendshape weights for a viseme expressed in terms of the default rig's shapes.
struct VisemePose {
    float jawOpen = 0, smile = 0, pucker = 0, wide = 0, lipsPress = 0, funnel = 0;
};
const VisemePose& visemePose(Viseme v);

/// Per-frame viseme classification with soft weights.
struct VisemeFrame {
    double time = 0.0;
    std::array<float, size_t(Viseme::Count)> weights{}; ///< sum ~1
    Viseme dominant() const;
};

/// Rule-based acoustic viseme classifier: uses loudness, voicing, spectral centroid
/// and the low-order MFCCs / mel envelope to infer a coarse mouth shape. This is
/// the "rule-based" strategy from the design doc; an ML mapper can implement the
/// same interface later.
class VisemeMapper {
public:
    virtual ~VisemeMapper() = default;
    virtual std::vector<VisemeFrame> map(const FeatureTrack& track) const;
    /// Temporal smoothing (exponential) applied to the soft weights to emulate co-articulation.
    float smoothing = 0.55f;
    /// Loudness below which frames are treated as silence.
    float silenceLoudness = 0.06f;
};

/// Phoneme string (ARPAbet-like) -> viseme dictionary, for transcripts/forced alignment.
Viseme phonemeToViseme(const std::string& phoneme);

} // namespace fr

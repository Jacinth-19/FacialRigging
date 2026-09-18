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
    float tongueUp = 0, tongueOut = 0;   ///< tongue bone targets (L/N/T/D lift the tip, TH protrudes)
};
const VisemePose& visemePose(Viseme v);

/// Co-articulation parameters per viseme (Cohen & Massaro 1993 dominance model): how strongly a
/// viseme imposes its shape and how quickly its influence rises / decays (seconds). Bilabials
/// and labiodentals are "strong" (must close fully, short time constants); vowels are weaker
/// and spread further into their neighbours.
struct VisemeDominance { float magnitude, onsetTau, offsetTau; };
const VisemeDominance& visemeDominance(Viseme v);

/// A contiguous run of one viseme (from a phoneme alignment or from collapsing per-frame
/// classification), the unit the co-articulation model blends.
struct VisemeSegment {
    Viseme viseme = Viseme::Silence; double start = 0.0, end = 0.0; float confidence = 1.0f;
    bool operator==(const VisemeSegment& o) const { return viseme == o.viseme && start == o.start && end == o.end && confidence == o.confidence; }
};


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

/// Collapses per-frame soft visemes into segments (dominant viseme per frame, merged runs, runs
/// shorter than `minDurationSec` absorbed into their neighbours).
std::vector<VisemeSegment> segmentVisemes(const std::vector<VisemeFrame>& frames, double frameInterval, double minDurationSec = 0.04);

/// Phoneme string (ARPAbet-like) -> viseme dictionary, for transcripts/forced alignment.
Viseme phonemeToViseme(const std::string& phoneme);

} // namespace fr

#pragma once
#include "audio/viseme_mapper.h"
#include <vector>

namespace fr {

/// Cohen-Massaro dominance-based co-articulation.
///
/// Every viseme segment k exerts a dominance function over time
///     D_k(t) = alpha_k * exp(-(|t - c_k| - plateau_k)^p / tau)   measured from the segment
/// centre c_k with a plateau over the middle half of the segment, separate onset / offset time
/// constants (tau), and the blended articulator value
/// is the dominance-weighted average of the segment targets:
///     F(t) = sum_k D_k(t) * T_k / sum_k D_k(t).
/// Strong, short-time-constant visemes (bilabials) win locally so plosives still close fully,
/// while weak vowels spread into neighbours (anticipatory rounding on "two", etc.). This
/// replaces box-filter smoothing, which smeared closures and could never anticipate.
struct CoarticulationSettings {
    bool enabled = true;
    float spread = 1.0f;          ///< scales all time constants (1 = defaults; >1 lazier speech)
    float exponent = 1.0f;        ///< p in exp(-(|dt|/tau)^p); 1 = exponential, 2 = gaussian
    float silenceFloor = 0.5f;    ///< relative dominance of Silence segments (keeps the mouth closing between phrases)
    float minSegmentSec = 0.04f;  ///< flicker suppression when segmenting per-frame visemes
};

class Coarticulator {
public:
    explicit Coarticulator(CoarticulationSettings s = {}) : settings(s) {}
    CoarticulationSettings settings;

    /// Sets the segments (from a phoneme alignment or segmentVisemes()).
    void setSegments(std::vector<VisemeSegment> segs);
    const std::vector<VisemeSegment>& segments() const { return segs_; }

    /// Blended articulator targets at time t (jaw / smile / pucker / wide / press / funnel / tongue).
    VisemePose poseAt(double t) const;
    /// Soft viseme weights at time t (the normalised dominances), for visualisation / ML parity.
    VisemeFrame weightsAt(double t) const;

    /// Dominance of segment k at time t (public for tests).
    float dominance(size_t k, double t) const;

private:
    std::vector<VisemeSegment> segs_;
};

} // namespace fr

#include "anim/coarticulation.h"
#include <algorithm>
#include <cmath>

namespace fr {

void Coarticulator::setSegments(std::vector<VisemeSegment> segs) {
    std::sort(segs.begin(), segs.end(), [](const VisemeSegment& a, const VisemeSegment& b) { return a.start < b.start; });
    segs_ = std::move(segs);
}

float Coarticulator::dominance(size_t k, double t) const {
    const VisemeSegment& s = segs_[k];
    const VisemeDominance& d = visemeDominance(s.viseme);
    float alpha = (s.viseme == Viseme::Silence ? settings.silenceFloor * d.magnitude / 0.5f : d.magnitude) * std::max(s.confidence, 0.25f);
    // Cohen-Massaro: distance is measured from the segment *centre*, with a plateau over the
    // middle half of the segment where the articulator is at target. Measuring from the edges
    // would let a long vowel dominate a short consonant next to it.
    const double centre = 0.5 * (s.start + s.end), plateau = 0.25 * (s.end - s.start);
    double dt = std::abs(t - centre) - plateau;
    if (dt <= 0.0) return alpha;
    float tau = t < centre ? d.onsetTau : d.offsetTau;
    tau = std::max(tau * settings.spread, 1e-3f);
    double x = dt / tau;
    if (x > 6.0) return 0.0f; // negligible
    return alpha * float(std::exp(-std::pow(x, double(settings.exponent))));
}

VisemePose Coarticulator::poseAt(double t) const {
    VisemePose out{};
    if (segs_.empty()) return out;
    if (!settings.enabled) {
        for (const auto& s : segs_) if (t >= s.start && t < s.end) return visemePose(s.viseme);
        return out;
    }
    // Only segments whose dominance can be non-negligible: start/end within ~6 tau of t.
    const double reach = 6.0 * 0.15 * std::max(settings.spread, 0.1f);
    float sum = 0.0f;
    for (size_t k = 0; k < segs_.size(); ++k) {
        if (segs_[k].end < t - reach) continue;
        if (segs_[k].start > t + reach) break;
        float D = dominance(k, t);
        if (D <= 0.0f) continue;
        const VisemePose& p = visemePose(segs_[k].viseme);
        const float tUp = segs_[k].tongueUp >= 0.0f ? std::max(segs_[k].tongueUp, p.tongueUp) : p.tongueUp;
        out.jawOpen += D * p.jawOpen; out.smile += D * p.smile; out.pucker += D * p.pucker; out.wide += D * p.wide;
        out.lipsPress += D * p.lipsPress; out.funnel += D * p.funnel; out.tongueUp += D * tUp; out.tongueOut += D * p.tongueOut;
        sum += D;
    }
    if (sum > 1e-6f) {
        float inv = 1.0f / sum;
        out.jawOpen *= inv; out.smile *= inv; out.pucker *= inv; out.wide *= inv; out.lipsPress *= inv; out.funnel *= inv; out.tongueUp *= inv; out.tongueOut *= inv;
    }
    return out;
}

VisemeFrame Coarticulator::weightsAt(double t) const {
    VisemeFrame f; f.time = t;
    float sum = 0.0f;
    const double reach = 6.0 * 0.15 * std::max(settings.spread, 0.1f);
    for (size_t k = 0; k < segs_.size(); ++k) {
        if (segs_[k].end < t - reach) continue;
        if (segs_[k].start > t + reach) break;
        float D = dominance(k, t); f.weights[size_t(segs_[k].viseme)] += D; sum += D;
    }
    if (sum > 1e-6f) for (auto& w : f.weights) w /= sum; else f.weights[0] = 1.0f;
    return f;
}

} // namespace fr

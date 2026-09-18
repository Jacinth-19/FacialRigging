#include "audio/phoneme_aligner.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace fr {

namespace {
constexpr float kNegInf = -std::numeric_limits<float>::infinity();

struct Unit { std::string phone; Viseme viseme; int wordIndex; bool optional; int minFrames; int typFrames; float skipCost; };
}

AlignmentResult PhonemeAligner::align(const std::string& transcript, const FeatureTrack& track, const std::vector<VisemeFrame>& posteriors) const {
    return alignPhones(transcriptToPhonemes(transcript), track, posteriors);
}

AlignmentResult PhonemeAligner::alignPhones(const std::vector<TranscriptWord>& words, const FeatureTrack& track, const std::vector<VisemeFrame>& posteriors) const {
    AlignmentResult res; res.words = words;
    const int T = int(std::min(track.frames.size(), posteriors.size()));
    const double dt = track.frameInterval();
    if (T <= 0 || dt <= 0) return res;
    const int minF = std::max(1, int(std::lround(settings.minPhoneSec / dt)));
    const int typF = std::max(minF, int(std::lround(settings.typicalPhoneSec / dt)));

    // --- unit sequence: SIL? w1p1 w1p2 .. SIL? w2p1 .. SIL?
    std::vector<Unit> units;
    const float del = settings.deletionPenalty;
    auto sil = [&](bool optional) { units.push_back({"SIL", Viseme::Silence, -1, optional, 1, typF, 0.0f}); };
    sil(true);
    for (size_t w = 0; w < words.size(); ++w) {
        for (const auto& ph : words[w].phones) {
            Viseme v = phonemeToViseme(ph);
            // plosives / nasals are short, vowels and fricatives longer
            bool shortPh = (v == Viseme::MBP || v == Viseme::L_TH || ph == "K" || ph == "G" || ph == "T" || ph == "D" || ph == "P" || ph == "B");
            // A word's first phone is never deleted (keeps the word anchored); others may be, at a cost.
            bool canSkip = del >= 0.0f && !units.empty() && units.back().wordIndex == int(w);
            units.push_back({ph, v, int(w), canSkip, minF, shortPh ? std::max(minF, typF / 2) : typF, canSkip ? del : 0.0f});
        }
        if (w + 1 < words.size()) { if (settings.optionalSilenceBetweenWords) sil(true); }
    }
    sil(true);
    const int U = int(units.size());
    if (U == 0) return res;

    // --- emissions: log P(viseme | frame), silence boosted by low loudness
    const float invTemp = 1.0f / std::max(settings.temperature, 0.05f);
    std::vector<std::array<float, size_t(Viseme::Count)>> logP(static_cast<size_t>(T));
    for (int t = 0; t < T; ++t) {
        const auto& w = posteriors[size_t(t)].weights;
        float loud = track.frames[size_t(t)].loudness;
        float silBoost = loud < settings.silenceLoudness ? 1.0f : std::exp(-(loud - settings.silenceLoudness) * 12.0f);
        std::array<float, size_t(Viseme::Count)> p{};
        float sum = 0.0f;
        for (size_t k = 0; k < p.size(); ++k) { p[k] = std::pow(std::max(w[k], 1e-4f), invTemp); sum += p[k]; }
        // mix in the energy silence model
        for (size_t k = 0; k < p.size(); ++k) p[k] /= sum;
        p[0] = 0.5f * p[0] + 0.5f * silBoost; float nonSil = 0.0f; for (size_t k = 1; k < p.size(); ++k) nonSil += p[k];
        float scale = nonSil > 1e-6f ? (1.0f - p[0]) / nonSil : 0.0f; for (size_t k = 1; k < p.size(); ++k) p[k] *= scale;
        for (size_t k = 0; k < p.size(); ++k) logP[size_t(t)][k] = std::log(std::max(p[k], 1e-6f));
    }

    // --- Viterbi over (unit, frames-in-unit) with a duration prior. State = (u, d) where d = frames
    // already spent in unit u (capped at typF*3 for the prior). Transition: stay (d+1) or advance to
    // u+1 (d=1) if d >= minFrames; optional units may be skipped.
    const int Dmax = std::max(typF * 3, minF + 1);
    auto durLogPrior = [&](const Unit& u, int d) -> float {
        // log of a soft duration prior: 0 up to typical, then linear decay in log domain
        if (d < u.minFrames) return kNegInf;
        if (u.optional) return 0.0f;
        return d <= u.typFrames ? 0.0f : -0.15f * float(d - u.typFrames);
    };
    const size_t S = size_t(U) * size_t(Dmax);
    std::vector<float> prev(S, kNegInf), cur(S, kNegInf);
    std::vector<int32_t> back(size_t(T) * S, -1); // previous state index
    auto idx = [&](int u, int d) { return size_t(u) * size_t(Dmax) + size_t(std::min(d, Dmax) - 1); };
    // init at t=0: unit 0, or any prefix of optional units skipped
    { float acc = 0.0f; for (int u = 0; u < U; ++u) { prev[idx(u, 1)] = logP[0][size_t(units[size_t(u)].viseme)] - acc; if (!units[size_t(u)].optional) break; acc += units[size_t(u)].skipCost; } }
    for (int t = 1; t < T; ++t) {
        std::fill(cur.begin(), cur.end(), kNegInf);
        for (int u = 0; u < U; ++u) {
            const float e = logP[size_t(t)][size_t(units[size_t(u)].viseme)];
            for (int d = 1; d <= Dmax; ++d) {
                float s = prev[idx(u, d)]; if (s == kNegInf) continue;
                // stay
                { int nd = std::min(d + 1, Dmax); float pen = durLogPrior(units[size_t(u)], nd); float v = s + e + (pen == kNegInf ? 0.0f : pen) - (durLogPrior(units[size_t(u)], d) == kNegInf ? 0.0f : durLogPrior(units[size_t(u)], d));
                  if (v > cur[idx(u, nd)]) { cur[idx(u, nd)] = v; back[size_t(t) * S + idx(u, nd)] = int32_t(idx(u, d)); } }
                // advance (possibly skipping optional units)
                if (d >= units[size_t(u)].minFrames) {
                    float skipAcc = 0.0f;
                    for (int nu = u + 1; nu < U; ++nu) {
                        float e2 = logP[size_t(t)][size_t(units[size_t(nu)].viseme)];
                        float v = s + e2 - skipAcc;
                        if (v > cur[idx(nu, 1)]) { cur[idx(nu, 1)] = v; back[size_t(t) * S + idx(nu, 1)] = int32_t(idx(u, d)); }
                        if (!units[size_t(nu)].optional) break;
                        skipAcc += units[size_t(nu)].skipCost;
                    }
                }
            }
        }
        std::swap(prev, cur);
    }
    // best end state: last unit or trailing optional units satisfied
    int bestU = -1, bestD = -1; float best = kNegInf; float tailSkip = 0.0f;
    for (int u = U - 1; u >= 0; --u) {
        for (int d = 1; d <= Dmax; ++d) { float s = prev[idx(u, d)] - tailSkip; if (s > best && d >= units[size_t(u)].minFrames) { best = s; bestU = u; bestD = d; } }
        if (!units[size_t(u)].optional) break;
        tailSkip += units[size_t(u)].skipCost;
    }
    if (bestU < 0) return res;
    // backtrack
    std::vector<int> unitOfFrame(size_t(T), -1);
    size_t st = idx(bestU, bestD);
    for (int t = T - 1; t >= 0; --t) {
        unitOfFrame[size_t(t)] = int(st / size_t(Dmax));
        if (t > 0) { int32_t b = back[size_t(t) * S + st]; if (b < 0) break; st = size_t(b); }
    }
    // --- phones from frame labels
    for (int t = 0; t < T; ++t) {
        int u = unitOfFrame[size_t(t)]; if (u < 0) continue;
        double t0 = t * dt, t1 = (t + 1) * dt;
        float lp = logP[size_t(t)][size_t(units[size_t(u)].viseme)];
        if (!res.phones.empty() && res.phones.back().wordIndex == units[size_t(u)].wordIndex && res.phones.back().phone == units[size_t(u)].phone && std::abs(res.phones.back().end - t0) < 1e-9 && (t > 0 && unitOfFrame[size_t(t) - 1] == u)) {
            auto& ph = res.phones.back(); int n = int(std::lround((ph.end - ph.start) / dt)); ph.score = (ph.score * n + lp) / float(n + 1); ph.end = t1;
        } else {
            res.phones.push_back({units[size_t(u)].phone, units[size_t(u)].viseme, t0, t1, units[size_t(u)].wordIndex, lp});
        }
    }
    // --- segments (merge equal neighbouring visemes) + quality
    float lpSum = 0.0f; int lpN = 0, speech = 0, covered = 0;
    for (const auto& ph : res.phones) {
        int n = std::max(1, int(std::lround((ph.end - ph.start) / dt))); lpSum += ph.score * n; lpN += n;
        float tUp = phonemeTongueUp(ph.phone);
        if (!res.segments.empty() && res.segments.back().viseme == ph.viseme && std::abs(res.segments.back().tongueUp - tUp) < 1e-6f) res.segments.back().end = ph.end;
        else { VisemeSegment sg; sg.viseme = ph.viseme; sg.start = ph.start; sg.end = ph.end; sg.confidence = 1.0f; sg.tongueUp = tUp; res.segments.push_back(sg); }
    }
    for (int t = 0; t < T; ++t) if (track.frames[size_t(t)].loudness >= settings.silenceLoudness) { ++speech; int u = unitOfFrame[size_t(t)]; if (u >= 0 && units[size_t(u)].viseme != Viseme::Silence) ++covered; }
    res.meanLogPosterior = lpN ? lpSum / float(lpN) : 0.0f;
    res.coverage = speech ? float(covered) / float(speech) : 0.0f;
    return res;
}

} // namespace fr

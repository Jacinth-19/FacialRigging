#include "audio/viseme_mapper.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace fr {

const char* visemeName(Viseme v) {
    static const char* names[] = {"Silence", "AA", "EE", "IH", "OH", "UW", "MBP", "FV", "L_TH"};
    int i = int(v); return (i >= 0 && i < int(Viseme::Count)) ? names[i] : "?";
}

const VisemePose& visemePose(Viseme v) {
    //                 jaw   smile pucker wide  press funnel tUp  tOut
    static const VisemePose poses[] = {
        /* Silence */ {0.00f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        /* AA      */ {0.90f, 0.0f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f},
        /* EE      */ {0.30f, 0.4f, 0.0f, 0.8f, 0.0f, 0.0f, 0.2f, 0.0f},
        /* IH      */ {0.40f, 0.2f, 0.0f, 0.4f, 0.0f, 0.0f, 0.1f, 0.0f},
        /* OH      */ {0.55f, 0.0f, 0.4f, 0.0f, 0.0f, 0.6f, 0.0f, 0.0f},
        /* UW      */ {0.25f, 0.0f, 0.9f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f},
        /* MBP     */ {0.00f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        /* FV      */ {0.10f, 0.0f, 0.0f, 0.2f, 0.6f, 0.0f, 0.0f, 0.0f},
        /* L_TH    */ {0.35f, 0.0f, 0.0f, 0.3f, 0.0f, 0.1f, 1.0f, 0.5f},
    };
    int i = std::clamp(int(v), 0, int(Viseme::Count) - 1);
    return poses[i];
}

Viseme VisemeFrame::dominant() const {
    return Viseme(std::max_element(weights.begin(), weights.end()) - weights.begin());
}

std::vector<VisemeFrame> VisemeMapper::map(const FeatureTrack& track) const {
    std::vector<VisemeFrame> out;
    out.reserve(track.frames.size());
    std::array<float, size_t(Viseme::Count)> prev{}; prev[0] = 1.0f;
    for (const auto& f : track.frames) {
        VisemeFrame vf; vf.time = f.time;
        std::array<float, size_t(Viseme::Count)> s{};
        if (f.loudness < silenceLoudness) {
            s[size_t(Viseme::Silence)] = 1.0f;
        } else {
            // Normalised descriptors
            float bright = std::clamp((f.spectralCentroid - 400.0f) / 2600.0f, 0.0f, 1.0f); // 0 dark .. 1 bright
            float loud = f.loudness;
            float voiced = f.voicing;
            // Low-mel vs mid-mel energy ratio approximates F1 height (openness) when voiced.
            float lowE = 0, midE = 0;
            int nm = int(f.melEnergies.size());
            for (int m = 0; m < nm; ++m) { float e = std::exp(f.melEnergies[m]); if (m < nm / 4) lowE += e; else if (m < nm / 2) midE += e; }
            float openness = std::clamp(midE / (lowE + midE + 1e-9f) * 1.6f, 0.0f, 1.0f);

            if (voiced > 0.5f) {
                // Vowels: split by brightness (F2 proxy) and openness (F1 proxy).
                s[size_t(Viseme::AA)] = loud * openness * (1.0f - 0.5f * bright);
                s[size_t(Viseme::EE)] = loud * bright * (0.5f + 0.5f * openness);
                s[size_t(Viseme::IH)] = loud * 0.5f * (1.0f - std::abs(bright - 0.5f) * 2.0f);
                s[size_t(Viseme::OH)] = loud * (1.0f - bright) * openness * 0.8f;
                s[size_t(Viseme::UW)] = loud * (1.0f - bright) * (1.0f - openness);
                s[size_t(Viseme::L_TH)] = 0.15f * loud;
            } else {
                // Unvoiced / noisy: fricatives (bright) vs stops/closures (dark, low energy).
                s[size_t(Viseme::FV)] = bright * 0.8f + 0.2f;
                s[size_t(Viseme::MBP)] = (1.0f - bright) * (1.0f - loud);
                s[size_t(Viseme::L_TH)] = 0.2f;
                s[size_t(Viseme::IH)] = 0.2f * loud;
            }
            if (f.onset) s[size_t(Viseme::MBP)] += 0.4f; // plosive-ish attack
        }
        float sum = 0.0f; for (float v : s) sum += v;
        if (sum <= 1e-9f) { s = {}; s[0] = 1.0f; sum = 1.0f; }
        for (size_t i = 0; i < s.size(); ++i) {
            s[i] /= sum;
            vf.weights[i] = smoothing * prev[i] + (1.0f - smoothing) * s[i];
        }
        prev = vf.weights;
        out.push_back(vf);
    }
    return out;
}

const VisemeDominance& visemeDominance(Viseme v) {
    //                                 mag   onset  offset (s)
    // Magnitudes are relative (only ratios matter). Consonants with a hard articulatory target
    // (lip closure, lip-teeth contact) dominate by a wide margin so they win even when squeezed
    // between long vowels; rounding (UW/OH) is strong and slow so it anticipates; lax vowels
    // are weak and easily coloured by their neighbours.
    static const VisemeDominance d[] = {
        /* Silence */ {0.50f, 0.12f, 0.12f},
        /* AA      */ {1.00f, 0.09f, 0.11f},
        /* EE      */ {0.90f, 0.08f, 0.10f},
        /* IH      */ {0.60f, 0.07f, 0.09f},
        /* OH      */ {1.30f, 0.10f, 0.12f},
        /* UW      */ {1.60f, 0.10f, 0.12f},   // rounding anticipates strongly
        /* MBP     */ {4.00f, 0.04f, 0.05f},   // lips must close: strong, sharp
        /* FV      */ {3.00f, 0.05f, 0.06f},
        /* L_TH    */ {1.20f, 0.05f, 0.07f},
    };
    return d[std::clamp(int(v), 0, int(Viseme::Count) - 1)];
}

std::vector<VisemeSegment> segmentVisemes(const std::vector<VisemeFrame>& frames, double frameInterval, double minDurationSec) {
    std::vector<VisemeSegment> segs;
    for (size_t i = 0; i < frames.size(); ++i) {
        Viseme v = frames[i].dominant();
        double t0 = frameInterval > 0 ? i * frameInterval : frames[i].time;
        if (!segs.empty() && segs.back().viseme == v) { segs.back().end = t0 + frameInterval; segs.back().confidence = std::max(segs.back().confidence, frames[i].weights[size_t(v)]); }
        else segs.push_back({v, t0, t0 + frameInterval, frames[i].weights[size_t(v)]});
    }
    // absorb flickers shorter than minDuration into the longer neighbour
    bool changed = true;
    while (changed && segs.size() > 1) {
        changed = false;
        for (size_t i = 0; i < segs.size(); ++i) {
            if (segs[i].end - segs[i].start >= minDurationSec) continue;
            size_t into = (i == 0) ? 1 : (i + 1 == segs.size() ? i - 1 : ((segs[i - 1].end - segs[i - 1].start) >= (segs[i + 1].end - segs[i + 1].start) ? i - 1 : i + 1));
            if (into < i) segs[into].end = segs[i].end; else segs[into].start = segs[i].start;
            segs.erase(segs.begin() + long(i)); changed = true; break;
        }
        // merge equal neighbours produced by the absorption
        for (size_t i = 0; i + 1 < segs.size(); ++i) if (segs[i].viseme == segs[i + 1].viseme) { segs[i].end = segs[i + 1].end; segs.erase(segs.begin() + long(i) + 1); changed = true; break; }
    }
    return segs;
}

Viseme phonemeToViseme(const std::string& phIn) {
    // Canonical phone -> viseme table, shared by the trainer (tools/train_visemes.cpp), the G2P /
    // aligner and the TIMIT evaluation so posteriors and transcript classes agree. Input may be
    // ARPAbet (any case, optional stress digit) or TIMIT (closures, reduced vowels).
    std::string p; for (char c : phIn) if (!std::isdigit((unsigned char)c)) p += char(std::toupper((unsigned char)c));
    static const struct { const char* ph; Viseme v; } table[] = {
        // silence / non-speech
        {"SIL", Viseme::Silence}, {"SP", Viseme::Silence}, {"H#", Viseme::Silence}, {"PAU", Viseme::Silence}, {"EPI", Viseme::Silence}, {"", Viseme::Silence},
        // open / neutral vowels + glottals
        {"AA", Viseme::AA}, {"AO", Viseme::AA}, {"AH", Viseme::AA}, {"AX", Viseme::AA}, {"AX-H", Viseme::AA}, {"AY", Viseme::AA}, {"AW", Viseme::AA}, {"HH", Viseme::AA}, {"HV", Viseme::AA}, {"Q", Viseme::AA},
        // spread vowels
        {"IY", Viseme::EE}, {"EY", Viseme::EE}, {"Y", Viseme::EE},
        // lax / mid vowels and the lingual consonants whose lip shape is neutral
        {"IH", Viseme::IH}, {"IX", Viseme::IH}, {"EH", Viseme::IH}, {"AE", Viseme::IH}, {"AXR", Viseme::IH}, {"ER", Viseme::IH},
        {"K", Viseme::IH}, {"G", Viseme::IH}, {"NG", Viseme::IH}, {"ENG", Viseme::IH}, {"N", Viseme::IH}, {"NX", Viseme::IH}, {"EN", Viseme::IH},
        {"T", Viseme::IH}, {"D", Viseme::IH}, {"DX", Viseme::IH}, {"S", Viseme::IH}, {"Z", Viseme::IH},
        // rounded
        {"OW", Viseme::OH}, {"OY", Viseme::OH}, {"R", Viseme::OH},
        {"UW", Viseme::UW}, {"UX", Viseme::UW}, {"UH", Viseme::UW}, {"W", Viseme::UW}, {"SH", Viseme::UW}, {"ZH", Viseme::UW}, {"CH", Viseme::UW}, {"JH", Viseme::UW},
        // bilabial / labiodental
        {"M", Viseme::MBP}, {"EM", Viseme::MBP}, {"B", Viseme::MBP}, {"P", Viseme::MBP}, {"BCL", Viseme::MBP}, {"PCL", Viseme::MBP},
        {"F", Viseme::FV}, {"V", Viseme::FV},
        // tongue-visible
        {"L", Viseme::L_TH}, {"EL", Viseme::L_TH}, {"TH", Viseme::L_TH}, {"DH", Viseme::L_TH},
    };
    for (auto& e : table) if (p == e.ph) return e.v;
    // TIMIT stop closures take the shape of their release
    if (p == "TCL" || p == "DCL" || p == "KCL" || p == "GCL") return Viseme::IH;
    return Viseme::IH;
}

/// Tongue-tip targets that are *not* implied by the viseme class alone (alveolars share the IH
/// lip shape but lift the tongue tip). Used by the aligner to add tongue motion per phone.
float phonemeTongueUp(const std::string& phIn) {
    std::string p; for (char c : phIn) if (!std::isdigit((unsigned char)c)) p += char(std::toupper((unsigned char)c));
    if (p == "T" || p == "D" || p == "N" || p == "DX" || p == "EN" || p == "NX" || p == "TCL" || p == "DCL") return 0.8f;
    if (p == "S" || p == "Z") return 0.35f;
    return 0.0f;
}

} // namespace fr

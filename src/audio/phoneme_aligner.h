#pragma once
#include "audio/features.h"
#include "audio/g2p.h"
#include "audio/viseme_mapper.h"
#include <string>
#include <vector>

namespace fr {

/// Transcript-driven lip-sync: turns a script into phonemes (G2P), maps them to visemes and
/// force-aligns the resulting sequence to the audio with a Viterbi search over the per-frame
/// viseme posteriors produced by a VisemeMapper (trained MLP or rules) plus an energy-based
/// silence model. Optional silences are inserted between words so pauses are absorbed.
///
/// Because the search is constrained to the known sequence, every frame gets the right viseme
/// *class* even when the frame classifier alone would have confused it - the only free variables
/// are the boundaries. Emission scores are log posteriors with a temperature; the duration prior
/// is a per-viseme min/typical length (frames) implemented with a left-to-right state chain.
struct AlignmentSettings {
    float temperature = 1.0f;          ///< softens the posteriors (>1 = trust the classifier less)
    float silenceLoudness = 0.08f;     ///< loudness below which the silence model fires strongly
    double minPhoneSec = 0.03;         ///< minimum phone duration
    double typicalPhoneSec = 0.08;     ///< duration prior centre (per phone)
    bool optionalSilenceBetweenWords = true;
};

struct AlignedPhone {
    std::string phone;                 ///< ARPAbet (or "SIL")
    Viseme viseme = Viseme::Silence;
    double start = 0.0, end = 0.0;
    int wordIndex = -1;                ///< -1 for inserted silences
    float score = 0.0f;                ///< mean log posterior over the phone
};

struct AlignmentResult {
    std::vector<AlignedPhone> phones;
    std::vector<VisemeSegment> segments;   ///< phones collapsed to viseme runs (co-articulation input)
    std::vector<TranscriptWord> words;
    float meanLogPosterior = 0.0f;         ///< alignment quality (closer to 0 is better)
    float coverage = 0.0f;                 ///< fraction of speech frames (loud) covered by non-silence phones
};

class PhonemeAligner {
public:
    explicit PhonemeAligner(AlignmentSettings s = {}) : settings(s) {}
    AlignmentSettings settings;
    /// `posteriors` are the mapper's per-frame soft visemes (same frame grid as `track`).
    AlignmentResult align(const std::string& transcript, const FeatureTrack& track, const std::vector<VisemeFrame>& posteriors) const;
    /// Same, from an explicit phone list (e.g. a TIMIT .PHN transcript without times).
    AlignmentResult alignPhones(const std::vector<TranscriptWord>& words, const FeatureTrack& track, const std::vector<VisemeFrame>& posteriors) const;
};

} // namespace fr

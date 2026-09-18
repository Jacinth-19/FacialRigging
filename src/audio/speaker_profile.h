// Speaker-adaptive calibration for the viseme mapper.
//
// The trained MLP standardises its inputs with the global TIMIT mean / std. A new speaker (or a
// different microphone / room) shifts those statistics - deeper voice, brighter mic, quieter room -
// and the network sees systematically off-centre inputs. A SpeakerProfile captures the speaker's own
// feature statistics from ~10 s of speech and (a) re-centres the input normalisation (feature-space
// CMVN-style adaptation, blended with the global stats by `strength`), (b) stores the pitch median
// and loudness reference used by the generator, and optionally (c) fine-tunes the network's last
// layer on the speaker's aligned calibration sentence (few Adam steps, L2-anchored to the original).
#pragma once
#include "audio/features.h"
#include "audio/ml_viseme_mapper.h"
#include <string>
#include <vector>

namespace fr {

struct SpeakerProfile {
    std::string name = "default";
    bool valid = false;
    int dims = 0;                                  ///< base feature dims (17)
    std::vector<float> mean, stdev;                ///< speaker statistics per base feature (active frames)
    float pitchMedianHz = 0.0f;                    ///< speaker's median voiced pitch
    float loudnessRef = 0.0f;                      ///< 90th percentile RMS (dBFS) for live normalisation
    float strength = 0.3f;                         ///< 0 = ignore, 1 = fully speaker-normalised inputs (0.3 tuned on TIMIT)
    float secondsUsed = 0.0f; int framesUsed = 0;
    std::string source;                            ///< "mic 10 s", "file x.wav"
    /// Optional fine-tuned last layer (same shape as the model's final W/b); empty = not fine-tuned.
    std::vector<float> lastW, lastB; std::string fineTuneInfo;

    std::string toJson() const;
    bool fromJson(const std::string& json, std::string* error = nullptr);
    bool save(const std::string& path, std::string* error = nullptr) const;
    bool load(const std::string& path, std::string* error = nullptr);
};

struct CalibrationStats { int activeFrames = 0; float seconds = 0, voicedFraction = 0, snrDb = 0; std::string warning; };

/// Suggested read-aloud text (~10 s, viseme-balanced: covers every viseme class several times).
const char* calibrationSentence();

/// Builds a profile from the speaker's audio. `minSeconds` of active speech are required.
SpeakerProfile calibrateSpeaker(const AudioBuffer& audio, const std::string& name = "speaker", float minSeconds = 3.0f, CalibrationStats* stats = nullptr);
SpeakerProfile calibrateSpeaker(const FeatureTrack& track, const std::string& name = "speaker", float minSeconds = 3.0f, CalibrationStats* stats = nullptr);

/// Returns a copy of `model` whose input standardisation is re-centred on the speaker. For each
/// context-stacked input i (base dim d = i % dims): mean' = mix(globalMean, speakerMean), and
/// invStd' = 1 / mix(globalStd, speakerStd). Loudness-type dims (already normalised 0..1) keep their
/// global scale. Applies the fine-tuned last layer when present.
VisemeMlpWeights adaptModel(const VisemeMlpWeights& model, const SpeakerProfile& profile);

/// Supervised last-layer fine-tune: `labels` are per-frame viseme indices on the track's frame grid
/// (-1 = ignore), typically from the forced alignment of the calibration sentence. Writes lastW/lastB
/// into `profile`. Returns the label accuracy before/after through `accBefore`/`accAfter`.
bool fineTuneLastLayer(const VisemeMlpWeights& model, SpeakerProfile& profile, const FeatureTrack& track, const std::vector<int>& labels,
                       int steps = 20, float lr = 5e-4f, float anchor = 1.0f, float* accBefore = nullptr, float* accAfter = nullptr);

} // namespace fr

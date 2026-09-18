#pragma once
#include "anim/animation_clip.h"
#include "audio/phoneme_aligner.h"
#include "anim/lipsync_generator.h"
#include "audio/features.h"
#include "audio/wav_io.h"
#include "rig/rig.h"
#include <functional>
#include <memory>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace fr {

/// A named tweak applied on top of a generated clip to produce a variation
/// (e.g. "Increase smile" -> MouthSmile += 0.3).
struct Variation {
    std::string name;
    std::function<void(AnimationClip&)> apply;
};
/// Parses free-form variation descriptions used by the agent prompt ("Increase smile",
/// "Raise eyebrows", "intensity=1.4", "smile+=0.3", "brow+=0.2", "smooth=3", "happy", "angry=0.5", ...).
Variation parseVariation(const std::string& text);

/// Headless orchestration shared by the CLI, the GUI and the agent entry point:
/// load model -> build rig -> load audio -> generate clip -> export (+ variations).
class Pipeline {
public:
    Rig rig;
    AudioBuffer audio;
    FeatureTrack features;
    AnimationClip clip;
    LipSyncSettings lipSync;
    std::vector<std::string> log;
    /// Optional transcript of the audio. When non-empty, generateAnimation() force-aligns its
    /// phonemes to the audio (PhonemeAligner) and drives the mouth from the aligned viseme
    /// segments instead of per-frame classification.
    std::string transcript;
    AlignmentSettings alignment;
    AlignmentResult lastAlignment;                 ///< filled by generateAnimation() when a transcript was used
    std::vector<VisemeSegment> lastSegments;       ///< viseme segments actually used for the mouth (aligned or collapsed)

    enum class MapperKind { RuleBased, Ml };
    MapperKind mapperKind = MapperKind::RuleBased;
    std::string mlModelPath;                      ///< .frvm or TorchScript .pt; empty -> assets/models/viseme_mlp.frvm (else built-in torch MLP)
    std::string assetDir = FR_ASSET_DIR;          ///< where assets/ lives
    enum class UpAxis { Auto, Y, Z };
    UpAxis modelUpAxis = UpAxis::Auto;            ///< how to interpret imported OBJ orientation                      ///< TorchScript .pt; empty -> built-in MLP
    std::shared_ptr<VisemeMapper> makeMapper(std::string* note = nullptr) const;

    bool loadModel(const std::string& path, std::string* error = nullptr);
    /// Transform applied to the last loaded model (so external data such as blendshape deltas can follow).
    struct ModelTransform { bool zUp = false; glm::vec3 centre{0.0f}; float scale = 1.0f; };
    ModelTransform modelTransform;
    std::string authoredShapesPath;             ///< .fbs loaded alongside the model ("" = none)
    int authoredCanonicalCoverage = 0;
    const std::vector<BlendShape>& authoredShapes() const { return authoredShapes_; }
private:
    std::vector<BlendShape> authoredShapes_;
public: ///< empty path -> procedural head
    void buildDefaultRig();
    /// Check-Model step: rotate / mirror / offset the loaded model (and its authored blendshapes)
    /// before rigging. `R` is applied about the model centre; the mesh is re-normalised afterwards.
    void transformModel(const glm::mat3& R);
    void translateModel(const glm::vec3& d);
    void resetModel() { transformModel(glm::mat3(1.0f)); }
    bool loadAudio(const std::string& path, std::string* error = nullptr);  ///< empty path -> synthetic speech
    bool generateAnimation();
    /// Exports `clip` plus one file per variation using `outputPattern` (e.g. "scene" -> scene.glb, scene_var1_*.glb).
    std::vector<std::string> exportAll(const std::string& outputPattern, const std::string& ext,
                                       const std::vector<Variation>& variations, std::string* error = nullptr);
    /// Returns the path actually written (may differ in extension after an FBX->glTF fallback).
    bool exportClip(const AnimationClip& c, const std::string& path, std::string* error = nullptr, std::string* writtenPath = nullptr);
private:
    void note(const std::string& s);
};

} // namespace fr

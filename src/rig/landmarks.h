// Automatic face landmarking for rig placement.
//
// The default rig used to place its landmarks (mouth, mouth corners, brows, eyelids) at fixed
// proportions of the bounding box. `detectLandmarks()` instead renders the mesh front-on with a
// small software rasterizer (grey Lambert shading + per-pixel world position), runs dlib's HOG
// frontal face detector and the 68-point shape predictor on that image, and lifts the 2D points
// back onto the surface through the position buffer. Any failure (no dlib, no model file, no face
// found) leaves `found == false` and the caller falls back to the proportional guesses.
#pragma once
#include "core/mesh.h"
#include <array>
#include <string>
#include <vector>

namespace fr {

struct FaceLandmarks {
    bool found = false;
    float confidence = 0.0f;                 ///< detector score (0 when not found)
    glm::vec3 mouth{0}, cornerL{0}, cornerR{0}, browL{0}, browR{0}, eyeL{0}, eyeR{0}, chin{0}, noseTip{0};
    glm::vec3 upperLip{0}, lowerLip{0}, eyeLOuter{0}, eyeLInner{0}, eyeROuter{0}, eyeRInner{0};
    std::vector<glm::vec3> points68;         ///< all 68 iBUG points on the surface (empty when not found)
    std::vector<glm::vec2> pixels68;         ///< the same points in render pixels
    int renderWidth = 0, renderHeight = 0;
    std::string note;                        ///< human-readable diagnostics ("dlib: face 0.93, 68 points, 3 off-surface")
};

enum class LandmarkEngine { Auto, Builtin, Dlib };   ///< Auto = built-in cascade, dlib only when the cascade fails / is missing

struct LandmarkOptions {
    int renderSize = 512;                    ///< square front render resolution (dlib path / lift buffer)
    std::string modelPath;                   ///< dlib shape_predictor_68_face_landmarks.dat ("" = data/models/...)
    bool upsample = true;                    ///< dlib: run the detector on a 2x pyramid level (small faces)
    LandmarkEngine engine = LandmarkEngine::Auto;
    float minConfidence = 0.35f;             ///< built-in cascade: below this the result counts as "not found"
};

/// Software front render (orthographic, looking down -z, y up). Fills grey8 shading and per-pixel
/// world positions (w = 1 where a surface was hit). Exposed for tests / the reference overlay.
struct FrontRender {
    int width = 0, height = 0;
    std::vector<unsigned char> gray;         ///< width*height
    std::vector<glm::vec4> world;            ///< width*height, xyz = surface point, w = 1 if hit
    glm::vec2 pixelOf(const glm::vec3& p) const;      ///< world -> pixel (x right, y down)
    glm::vec3 origin{0}; float scale = 1.0f;          ///< pixel = (p.x - origin.x) * scale, (origin.y - p.y) * scale
};
FrontRender renderFront(const Mesh& mesh, int size, int onlyPart = -1);
bool savePgm(const FrontRender& r, const std::string& path, const std::vector<glm::vec2>* marks = nullptr);

/// True when some engine can run: the built-in cascade model (assets/models/face_landmarks.frlm) or dlib + its predictor.
bool landmarkerAvailable(std::string* why = nullptr, const std::string& modelPath = "", LandmarkEngine engine = LandmarkEngine::Auto);
/// Runs the detector on a front render of the mesh. Never throws.
FaceLandmarks detectLandmarks(const Mesh& mesh, const LandmarkOptions& opt = {});
/// Proportional fallback used when detection fails (the historical bounding-box guesses).
FaceLandmarks proportionalLandmarks(const Mesh& mesh);
/// Mirror-average a landmark set about x = centre so L/R pairs are symmetric (optional).
void symmetrize(FaceLandmarks& lm, float centreX);

} // namespace fr

// Built-in 68-point landmark cascade (supervised descent, Xiong & De la Torre 2013) trained on
// ICT-FaceKit identities - replaces the 99 MB dlib shape predictor for auto-landmarking.
//
// Input is the same software front render used by the dlib path (rig/landmarks.h): per-pixel
// depth and normals. A face box is found without any detector by anchoring on the nose tip (the
// most forward pixel) and the cheek silhouette width at that height; the cascade then refines a
// mean shape inside the box using linear regressors over depth/normal patches. Being trained on
// geometry rather than photographs it is unaffected by the missing skin texture that made the
// HOG detector marginal on smooth scans. Model file: assets/models/face_landmarks.frlm (~1.2 MB).
#pragma once
#include "core/mesh.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace fr {

struct FrontRender;   // rig/landmarks.h

/// Orthographic depth + normal image of the mesh seen from +z (the "photo" the model reads).
struct GeoImage {
    int res = 0; glm::vec2 originTopLeft{0.0f}; float unitsPerPixel = 1.0f;
    std::vector<float> depth;        ///< z per pixel, -1e30 where empty
    std::vector<glm::vec3> normal;
    bool empty(int x, int y) const { return depth[size_t(y) * res + x] <= -1e29f; }
    static GeoImage rasterize(const Mesh& mesh, const std::vector<glm::vec3>& positions, int res, glm::vec2 originTopLeft, float worldWidth, int onlyPart = -1);
};

/// Square face window in world units: top-left corner, side; `noseTip` anchors depth normalisation.
struct GeoFaceBox { glm::vec2 topLeft{0.0f}; float side = 0.0f; glm::vec3 noseTip{0.0f}; float cheekWidth = 0.0f; bool valid = false; };

constexpr int kLm68 = 68;

struct LandmarkCascade {
    int res = 96, patch = 5, channels = 3; float spacing = 0.035f, margin = 0.25f;
    std::vector<glm::vec2> meanShape;             ///< 68 points in box units (u right, v down, side = 1)
    std::vector<std::vector<float>> stages;       ///< each (F+1) x 136 row-major
    std::string info;
    int featureCount() const { return kLm68 * patch * patch * channels; }
    bool valid() const { return meanShape.size() == kLm68 && !stages.empty(); }
    bool save(const std::string& path, std::string* error = nullptr) const;   ///< "FRLM" v1, int16 per output column
    bool load(const std::string& path, std::string* error = nullptr);
    void features(const GeoImage& img, const GeoFaceBox& box, const std::vector<glm::vec2>& shape, std::vector<float>& out) const;
    std::vector<glm::vec2> run(const GeoImage& img, const GeoFaceBox& box, std::vector<glm::vec2> init, std::vector<std::vector<glm::vec2>>* perStage = nullptr) const;

    /// Nose-anchored face box from a whole-head render (no learned detector needed). Use facePart = -1 (whole mesh, as trained) unless you retrain.
    static GeoFaceBox detectBox(const Mesh& mesh, const std::vector<glm::vec3>& positions, int facePart = -1);
    /// Render of the box expanded by `margin` on each side at `res`.
    GeoImage renderBox(const Mesh& mesh, const std::vector<glm::vec3>& positions, const GeoFaceBox& box, int facePart = -1) const;
    /// Full inference: box -> cascade -> box-unit shape. Returns false when no box.
    bool predict(const Mesh& mesh, const std::vector<glm::vec3>& positions, GeoFaceBox& box, std::vector<glm::vec2>& shape, float* confidence = nullptr, int facePart = -1) const;
    /// Box-unit shape -> world x,y (z from the caller's lift).
    static glm::vec2 toWorldXY(const GeoFaceBox& box, glm::vec2 s) { return glm::vec2(box.topLeft.x + s.x * box.side, box.topLeft.y - s.y * box.side); }

    /// Multi-PIE 68 landmark vertex ids of the ICT-FaceKit topology (0-based OBJ order) for training / tests.
    static const int* ictLandmarkVertices();
    /// The shipped model (assets/models/face_landmarks.frlm), loaded once. nullptr when missing.
    static const LandmarkCascade* builtin(std::string* why = nullptr);
};

} // namespace fr

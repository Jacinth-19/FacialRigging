#pragma once
#include "core/mesh.h"
#include "rig/landmarks.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <string>
#include <vector>

namespace fr {

// ------------------------------------------------------------------ skeleton
struct Bone {
    std::string name;
    int parent = -1;                       ///< index into Skeleton::bones or -1
    glm::vec3 bindTranslation{0.0f};       ///< local bind pose (relative to parent)
    glm::quat bindRotation{1, 0, 0, 0};
    // Current pose (local, applied on top of bind)
    glm::quat poseRotation{1, 0, 0, 0};
    glm::vec3 poseTranslation{0.0f};
};

struct Skeleton {
    std::vector<Bone> bones;
    int find(const std::string& name) const;
    /// World (model-space) matrices of the bind pose.
    std::vector<glm::mat4> bindWorldMatrices() const;
    /// World matrices for the current pose.
    std::vector<glm::mat4> poseWorldMatrices() const;
    /// Skinning matrices = poseWorld * inverse(bindWorld).
    std::vector<glm::mat4> skinningMatrices() const;
    void resetPose();
};

/// Up to 4 influences per vertex (matches common GPU layouts and glTF JOINTS_0/WEIGHTS_0).
struct VertexInfluence {
    glm::ivec4 bones{0};
    glm::vec4 weights{0.0f};
    void add(int bone, float w);
    void normalize();
};

// ------------------------------------------------------------------ blendshapes
struct BlendShape {
    std::string name;
    std::vector<uint32_t> indices;   ///< affected vertices (sparse)
    std::vector<glm::vec3> deltas;   ///< same length as indices
    float weight = 0.0f;             ///< current weight, typically [0,1]
    std::vector<std::string> aliases; ///< source (ARKit/ICT) shape names merged into this canonical shape
    /// Expands to a dense delta array over `vertexCount` vertices.
    std::vector<glm::vec3> dense(size_t vertexCount) const;
};

// ------------------------------------------------------------------ control points
enum class BindingType { Unbound, Bone, BlendShape, FreeForm };

/// A user-placed handle on the mesh surface. Dragging it drives the rig depending on its binding.
struct ControlPoint {
    std::string name;
    glm::vec3 restPosition{0.0f};    ///< placed position on the bind-pose surface
    glm::vec3 offset{0.0f};          ///< current drag offset from rest
    uint32_t nearestVertex = 0;
    BindingType binding = BindingType::Unbound;
    int target = -1;                 ///< bone index or blendshape index
    glm::vec3 driveAxis{0, -1, 0};   ///< BlendShape: offset projected on this axis -> weight
    float driveRange = 0.1f;         ///< BlendShape: offset length giving weight 1.0
    float radius = 0.15f;            ///< FreeForm: RBF influence radius
    glm::vec3 currentPosition() const { return restPosition + offset; }
};

// ------------------------------------------------------------------ corrective shapes
/// A blendshape driven automatically by the product (or min) of two other shapes' weights - the
/// standard fix for volume loss when e.g. JawOpen and MouthPucker are both on.
struct CombinationShape {
    std::string shape;               ///< corrective blendshape name (exists in Rig::blendShapes)
    std::string driverA, driverB;
    float gain = 1.0f;
    bool useMin = false;
};

// ------------------------------------------------------------------ rig
/// Face rig combining linear-blend skinning, blendshapes and free-form (RBF) control points.
/// Evaluation order: skin -> blendshapes -> free-form handles.
class Rig {
public:
    Mesh mesh;
    Skeleton skeleton;
    std::vector<VertexInfluence> skin;   ///< size == mesh.vertexCount() when skinning is used
    std::vector<BlendShape> blendShapes;
    std::vector<ControlPoint> controlPoints;
    std::vector<CombinationShape> combinations;   ///< corrective shapes, re-evaluated by applyCombinations()
    bool skinFirst = true;               ///< skin then morph (default) vs morph then skin

    void setMesh(const Mesh& m);
    bool hasSkin() const { return !skin.empty() && skin.size() == mesh.vertexCount(); }

    int findBlendShape(const std::string& name) const;
    float blendWeight(const std::string& name) const;
    void setBlendWeight(const std::string& name, float w);
    void setBlendWeight(int index, float w);
    std::vector<float> blendWeights() const;
    void setBlendWeights(const std::vector<float>& w);
    void resetPose();                    ///< zero weights, identity bone poses, zero CP offsets
    /// Sets every corrective's weight from its drivers (called by setBlendWeights / clip playback;
    /// call it yourself after poking individual weights). Returns the number of correctives updated.
    int applyCombinations();
    static float combinationWeight(const CombinationShape& c, float wA, float wB);
    int findCombination(const std::string& shape) const;

    // control points ------------------------------------------------------
    int addControlPoint(const glm::vec3& surfacePoint, const std::string& name = "");
    void removeControlPoint(int index);
    void bindToBone(int cp, int bone);
    void bindToBlendShape(int cp, int shape, const glm::vec3& driveAxis, float range);
    void bindFreeForm(int cp, float radius);
    /// Moves a control point; propagates to bone/blendshape depending on the binding.
    void moveControlPoint(int cp, const glm::vec3& newOffset);
    /// Symmetry: when set, moveControlPoint also applies the X-mirrored offset to the point's
    /// left/right partner (see mirrorPartner). Toggled by the Check Model "Force Symmetry" box.
    bool forceSymmetry = false;
    /// Index of the control point mirrored across x = 0 (by "L"/"R" name suffix or by position), or -1.
    int mirrorPartner(int i) const;
    /// Applies rig state (bone pose / weights) back onto control point offsets, so handles follow the animation.
    void syncControlPointsFromRig();

    // evaluation -------------------------------------------------------------
    /// Full deformation of the bind mesh into `out` (resized to vertexCount()).
    void evaluate(std::vector<glm::vec3>& out) const;
    std::vector<glm::vec3> evaluate() const { std::vector<glm::vec3> v; evaluate(v); return v; }

    // procedural authoring ---------------------------------------------------
    /// Builds a generic default face rig on the current mesh: head+jaw bones with
    /// height-based weights, and a set of procedural blendshapes (JawOpen, MouthSmile,
    /// MouthPucker, MouthWide, LipsPress, BrowRaise, EyeBlink, MouthFunnel).
    void buildDefaultFaceRig(const FaceLandmarks* landmarks = nullptr);
    FaceLandmarks landmarks;   ///< landmarks the current rig was built from (detected or proportional)

    /// Replaces the procedural shapes with authored ones (e.g. ICT-FaceKit / ARKit set). Shapes
    /// whose names map to a canonical shape (see canonicalShapeName) are merged into it so the
    /// lip-sync generator keeps working; every source shape is also kept under its own name.
    /// Rebinds the default control points. Returns the number of canonical shapes covered.
    int installAuthoredBlendShapes(const std::vector<BlendShape>& authored);

    /// Bone names created by buildDefaultFaceRig.
    static constexpr const char* kHeadBone = "Head";
    static constexpr const char* kJawBone = "Jaw";
    static constexpr const char* kEyeLBone = "EyeL";
    static constexpr const char* kEyeRBone = "EyeR";
    static constexpr const char* kTongueBone = "Tongue";   ///< child of Jaw, pivot at the tongue root (when a Tongue part exists)
    bool hasTongueBone() const { return skeleton.find(kTongueBone) >= 0; }
    /// Tongue pose helper: up = tip raised toward the palate (L/N/T/D), out = protrusion (TH), each 0..1.
    void setTongue(float up, float out);
    /// Gaze: rotates both eye bones so the eyeballs look toward `target` (mesh space). No-op
    /// without eye bones (single-surface heads).
    void lookAt(const glm::vec3& target);
    /// Gaze as yaw/pitch (degrees, +yaw = look to the model's left/+x, +pitch = look up).
    void setGaze(float yawDeg, float pitchDeg);
    bool hasEyeBones() const { return skeleton.find(kEyeLBone) >= 0 && skeleton.find(kEyeRBone) >= 0; }

    /// Names of mesh parts (from OBJ groups) recognised for anatomical rigging.
    struct PartInfo { int face = -1, browL = -1, browR = -1, eyeL = -1, eyeR = -1, teethUpper = -1, teethLower = -1, gumsUpper = -1, gumsLower = -1, tongue = -1, lashes = -1; bool any() const { return face >= 0 || teethLower >= 0 || browL >= 0; } };
    PartInfo detectParts() const;

private:
    void applySkin(std::vector<glm::vec3>& v) const;
    void applyBlendShapes(std::vector<glm::vec3>& v) const;
    void applyFreeForm(std::vector<glm::vec3>& v) const;
};

/// Names of the procedural blendshapes created by Rig::buildDefaultFaceRig.
namespace shapes {
inline constexpr const char* JawOpen = "JawOpen";
inline constexpr const char* MouthSmile = "MouthSmile";
inline constexpr const char* MouthPucker = "MouthPucker";
inline constexpr const char* MouthWide = "MouthWide";
inline constexpr const char* LipsPress = "LipsPress";
inline constexpr const char* BrowRaise = "BrowRaise";
inline constexpr const char* EyeBlink = "EyeBlink";
inline constexpr const char* MouthFunnel = "MouthFunnel";
inline constexpr const char* MouthFrown = "MouthFrown";
inline constexpr const char* BrowDown = "BrowDown";
inline constexpr const char* EyeWide = "EyeWide";
inline constexpr const char* All[] = {JawOpen, MouthSmile, MouthPucker, MouthWide, LipsPress, BrowRaise, EyeBlink, MouthFunnel, MouthFrown, BrowDown, EyeWide};
} // namespace shapes

} // namespace fr

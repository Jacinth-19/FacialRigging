#pragma once
// Interactive rig authoring tools: skin-weight brush + mirroring, baking handle poses into new
// blendshapes, and corrective (combination) shapes. All functions operate on a Rig in place and
// are UI-agnostic so they can be unit tested.
#include "rig/rig.h"
#include <string>
#include <vector>

namespace fr {

// ------------------------------------------------------------------ mesh adjacency
/// Vertex neighbourhood over the triangle mesh; vertices split on UV/normal seams are joined
/// through Mesh::sourceVertex when present so smoothing/mirroring doesn't tear seams.
struct VertexAdjacency {
    std::vector<std::vector<uint32_t>> neighbours;   ///< per vertex, unique, sorted
    void build(const Mesh& mesh);
    bool empty() const { return neighbours.empty(); }
};

/// Nearest-vertex lookup across the x = 0 plane (for L<->R mirroring). Built once per mesh.
struct MirrorMap {
    std::vector<int> partner;        ///< index of the vertex nearest to (-x, y, z), or -1 when none within tolerance
    float tolerance = 0.0f;
    void build(const Mesh& mesh, float toleranceFraction = 0.01f);   ///< tolerance as a fraction of the mesh height
    bool empty() const { return partner.empty(); }
};

// ------------------------------------------------------------------ weight painting
enum class WeightBrushMode { Add, Subtract, Replace, Smooth };

struct WeightBrush {
    WeightBrushMode mode = WeightBrushMode::Add;
    int bone = 1;                    ///< bone being painted
    float radius = 0.08f;            ///< model units
    float strength = 0.25f;          ///< 0..1 per application (Replace: blend toward `value`)
    float value = 1.0f;              ///< Replace target weight
    float falloff = 1.0f;            ///< 0 = hard, 1 = smooth (smoothstep) falloff
    bool symmetric = false;          ///< also paint the mirrored stroke on the partner bone (or same bone when no partner)
    bool frontFacingOnly = true;     ///< skip vertices whose normal faces away from `viewDir` (prevents painting through the head)
};

/// Applies one brush dab centred at `centre` (mesh/bind space). Returns the number of vertices changed.
/// `viewDir` is the camera forward vector used for the front-facing test (ignored when zero).
/// Weights stay normalised (the delta is taken from / given to the other influences proportionally).
int paintWeights(Rig& rig, const WeightBrush& brush, const glm::vec3& centre, const glm::vec3& viewDir,
                 const VertexAdjacency* adjacency = nullptr, const MirrorMap* mirror = nullptr);

/// Copies weights from one side of x = 0 to the other. `fromPositiveX` = source side; bone names
/// ending in L/R (or _L/_R) are swapped, others map to themselves. Returns vertices written.
int mirrorWeights(Rig& rig, bool fromPositiveX, const MirrorMap& mirror);

/// Bone index whose name is the L/R counterpart of `bone` (e.g. EyeL <-> EyeR), or `bone` itself.
int mirrorBone(const Skeleton& skeleton, int bone);

/// Per-vertex weight of `bone` (0 when not an influence) - handy for the heat map and tests.
float boneWeightOf(const VertexInfluence& inf, int bone);

/// Renormalises every vertex and drops near-zero influences.
void cleanWeights(Rig& rig, float epsilon = 1e-4f);

// ------------------------------------------------------------------ bake pose -> blendshape
struct BakeShapeOptions {
    std::string name = "Custom";
    bool includeBlendShapes = true;  ///< fold currently active blendshape weights into the bake
    bool includeFreeForm = true;     ///< fold free-form (RBF) handle displacement into the bake
    bool includeSkin = false;        ///< fold bone pose deltas in (usually false: bones stay bones)
    bool subtractExisting = true;    ///< store only the residual over the existing shapes' contribution
                                     ///< (so the new shape is a *corrective* on top, not a duplicate)
    bool fallbackToTotal = true;     ///< subtractExisting with nothing residual: bake the total deformation instead of failing
    float minDelta = 1e-6f;          ///< sparsify: drop vertices displaced less than this
    bool mirrorToOtherSide = false;  ///< create name+"_L"/"_R" pair instead of one shape
};

/// Captures the current deformation (relative to the bind mesh) as a new sparse blendshape,
/// appends it to rig.blendShapes with weight 0, zeroes the free-form handles that were baked and
/// returns the new shape index (or -1 when nothing moved). With `mirrorToOtherSide` two shapes are
/// created and the index of the first is returned.
int bakePoseAsBlendShape(Rig& rig, const BakeShapeOptions& opts, const MirrorMap* mirror = nullptr);

// ------------------------------------------------------------------ corrective / combination shapes
/// Builds a corrective for driverA+driverB from the current pose: pose both drivers (typically at
/// 1.0) plus whatever fix (free-form handles / other shapes); the residual over the existing shapes
/// is baked as `name`, normalised to fire fully at wA = wB = 1, and registered in rig.combinations.
/// Returns the new shape index (or -1 when the drivers don't exist or nothing moved).
int bakeCorrectiveShape(Rig& rig, const std::string& driverA, const std::string& driverB,
                        const std::string& name, const MirrorMap* mirror = nullptr);

} // namespace fr

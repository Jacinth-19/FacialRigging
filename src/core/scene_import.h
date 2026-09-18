#pragma once
// Imports rigged characters (FBX / glTF / GLB / DAE / OBJ ...) through Assimp: meshes are merged
// into one Mesh (each source mesh becomes a MeshPart), the skeleton, skin weights and morph
// targets are brought across, and the first animation (if any) is converted to an AnimationClip.
// Compiled to a stub returning false when Assimp is disabled (FR_HAVE_ASSIMP=0).
#include "anim/animation_clip.h"
#include "rig/rig.h"
#include <string>
#include <vector>

namespace fr {

struct SceneImportOptions {
    bool importSkeleton = true;
    bool importBlendShapes = true;
    bool importAnimation = true;
    bool normalizeToUnit = true;     ///< rescale/recentre like OBJ loads (deltas and bones follow)
    bool triangulate = true;
    /// Only keep bones that actually influence a vertex (plus their ancestors); drops
    /// helper/IK nodes from DCC exports.
    bool pruneUnusedBones = true;
};

struct SceneImportResult {
    Mesh mesh;
    Skeleton skeleton;
    std::vector<VertexInfluence> skin;      ///< empty when the file has no skin
    std::vector<BlendShape> blendShapes;    ///< morph targets (dense -> sparsified)
    std::vector<AnimationClip> clips;       ///< converted animations (bone rotations/translations + morph weights)
    std::string log;                        ///< human-readable summary
    int arkitShapeCount = 0;                ///< how many morph targets have recognisable ARKit / ICT names
    bool hasSkin() const { return !skin.empty(); }
};

/// Returns true when the file loaded and produced at least one triangle.
bool importScene(const std::string& path, const SceneImportOptions& opts, SceneImportResult& out, std::string* error = nullptr);

/// Extensions the importer understands (lower-case, with dot), e.g. {".fbx", ".glb", ".gltf", ".dae", ".obj"}.
std::vector<std::string> importableExtensions();
bool sceneImportAvailable();

/// Installs an import result into a rig: mesh, skeleton, skin and shapes; canonical shapes are
/// aliased from ARKit names (JawOpen <- jawOpen ...) so the lip-sync generator can drive the
/// character unchanged. Returns the number of canonical shapes covered.
int installImportedRig(Rig& rig, SceneImportResult& in);

} // namespace fr

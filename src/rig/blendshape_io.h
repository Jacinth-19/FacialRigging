#pragma once
#include "rig/rig.h"
#include <string>
#include <vector>

namespace fr {

/// FRBS: compact sparse blendshape container written by tools/prepare_ict_facekit.py.
///   magic "FRBS", u32 version=1, u32 vertexCount, u32 shapeCount,
///   per shape: u16 nameLen, name, u32 n, u32 idx[n], f32 delta[n*3]
/// Deltas are in the *source* model's units; pass the same transform that was applied to the
/// mesh on load (scale + optional Z-up rotation) so they stay consistent.
bool loadBlendShapesFRBS(const std::string& path, std::vector<BlendShape>& out, size_t expectedVertexCount, std::string* error = nullptr);
/// Re-indexes shapes authored on the OBJ's original `v` list onto a mesh whose vertices were
/// split on seams (uses Mesh::sourceVertex). No-op when the counts already match.
void remapBlendShapesToMesh(std::vector<BlendShape>& shapes, const Mesh& mesh);
bool saveBlendShapesFRBS(const std::string& path, const std::vector<BlendShape>& shapes, size_t vertexCount, std::string* error = nullptr);

/// Loads blendshapes from per-target OBJ files (same topology as the base mesh): each file
/// becomes one shape named after the file. Used for hand-authored targets.
bool loadBlendShapeOBJ(const std::string& path, const Mesh& base, BlendShape& out, std::string* error = nullptr);

/// Maps ARKit / ICT-FaceKit expression names onto the canonical shapes the animation
/// generator drives (JawOpen, MouthSmile, ...). Returns canonical name or "" when unmapped.
std::string canonicalShapeName(const std::string& arkitName);
/// Inverse: ARKit / ICT names a canonical shape should drive (rig aliases first, else the table).
std::vector<std::string> arkitNamesForShape(const std::string& canonical, const Rig& rig);

} // namespace fr

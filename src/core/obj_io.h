#pragma once
#include "core/mesh.h"
#include <string>

namespace fr {

/// Minimal Wavefront OBJ reader: v / vt / vn / f (triangles and polygons, negative indices).
/// Vertices are de-duplicated on (v, vt, vn) tuples. Returns false and sets `error` on failure.
bool loadObj(const std::string& path, Mesh& out, std::string* error = nullptr);
bool parseObj(const std::string& text, Mesh& out, std::string* error = nullptr);

/// Writes a mesh (positions/normals/uvs/triangles) as OBJ.
bool saveObj(const std::string& path, const Mesh& mesh, std::string* error = nullptr);

} // namespace fr

#include "core/scene_import.h"
namespace fr {
bool sceneImportAvailable() { return false; }
std::vector<std::string> importableExtensions() { return {}; }
bool importScene(const std::string&, const SceneImportOptions&, SceneImportResult&, std::string* error) { if (error) *error = "built without Assimp (FR_WITH_ASSIMP=OFF)"; return false; }
int installImportedRig(Rig& rig, SceneImportResult& in) { rig.setMesh(in.mesh); return 0; }
}

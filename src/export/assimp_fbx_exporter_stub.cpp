#include "export/exporter.h"
namespace fr {
bool AssimpFbxExporter::available() const { return false; }
bool AssimpFbxExporter::exportScene(const Rig&, const std::vector<AnimationClip>&, const std::string&, const ExportOptions&, std::string* error) {
    if (error) *error = "Assimp FBX exporter not compiled in (FR_WITH_ASSIMP=OFF)";
    return false;
}
} // namespace fr

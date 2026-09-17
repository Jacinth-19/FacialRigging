#include "export/exporter.h"

namespace fr {
bool FbxExporter::available() const { return false; }
bool FbxExporter::exportScene(const Rig&, const std::vector<AnimationClip>&, const std::string&, const ExportOptions&, std::string* error) {
    if (error) *error = "FBX export unavailable: build with -DFR_WITH_FBX_SDK=ON -DFBX_SDK_ROOT=<path to Autodesk FBX SDK>";
    return false;
}
} // namespace fr

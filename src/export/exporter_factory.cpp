#include "export/exporter.h"
#include <algorithm>
#include <cctype>

namespace fr {

std::unique_ptr<Exporter> makeExporterForPath(const std::string& path, std::string* note) {
    std::string ext;
    auto dot = path.find_last_of('.');
    if (dot != std::string::npos) { ext = path.substr(dot); for (auto& c : ext) c = char(std::tolower((unsigned char)c)); }
    if (ext == ".fbx") {
        auto fbx = std::make_unique<FbxExporter>();
        if (fbx->available()) return fbx;
        auto assimp = std::make_unique<AssimpFbxExporter>();
        if (assimp->available()) return assimp;
        if (note) *note = "no FBX writer compiled in; falling back to glTF (.glb)";
        return std::make_unique<GltfExporter>();
    }
    return std::make_unique<GltfExporter>();
}

} // namespace fr

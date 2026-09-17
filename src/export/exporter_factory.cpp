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
        if (note) *note = "FBX SDK not compiled in (configure with -DFR_WITH_FBX_SDK=ON -DFBX_SDK_ROOT=...); falling back to glTF (.glb)";
        return std::make_unique<GltfExporter>();
    }
    return std::make_unique<GltfExporter>();
}

} // namespace fr

#include "rig/blendshape_io.h"
#include "core/obj_io.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <algorithm>

namespace fr {

namespace {
template <class T> bool rd(std::ifstream& f, T& v) { return bool(f.read(reinterpret_cast<char*>(&v), sizeof v)); }
}

bool loadBlendShapesFRBS(const std::string& path, std::vector<BlendShape>& out, size_t expectedVertexCount, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    char magic[4]; if (!f.read(magic, 4) || std::memcmp(magic, "FRBS", 4) != 0) { if (error) *error = "not an FRBS file: " + path; return false; }
    uint32_t version = 0, vcount = 0, scount = 0;
    if (!rd(f, version) || !rd(f, vcount) || !rd(f, scount) || version != 1) { if (error) *error = "bad FRBS header"; return false; }
    if (expectedVertexCount && vcount != expectedVertexCount) {
        if (error) *error = "blendshape vertex count " + std::to_string(vcount) + " != mesh " + std::to_string(expectedVertexCount);
        return false;
    }
    out.clear(); out.reserve(scount);
    for (uint32_t s = 0; s < scount; ++s) {
        uint16_t nl = 0; if (!rd(f, nl)) { if (error) *error = "truncated FRBS"; return false; }
        std::string name(nl, '\0'); f.read(name.data(), nl);
        uint32_t n = 0; rd(f, n);
        BlendShape bs; bs.name = name; bs.indices.resize(n); bs.deltas.resize(n);
        f.read(reinterpret_cast<char*>(bs.indices.data()), std::streamsize(n * sizeof(uint32_t)));
        f.read(reinterpret_cast<char*>(bs.deltas.data()), std::streamsize(n * sizeof(glm::vec3)));
        if (!f) { if (error) *error = "truncated FRBS in shape " + name; return false; }
        out.push_back(std::move(bs));
    }
    return true;
}

void remapBlendShapesToMesh(std::vector<BlendShape>& shapes, const Mesh& mesh) {
    if (mesh.sourceVertex.size() != mesh.vertexCount()) return;
    // source index -> list of split vertices
    uint32_t maxSrc = 0; for (uint32_t s : mesh.sourceVertex) maxSrc = std::max(maxSrc, s);
    std::vector<std::vector<uint32_t>> bySrc(size_t(maxSrc) + 1);
    for (uint32_t v = 0; v < mesh.sourceVertex.size(); ++v) bySrc[mesh.sourceVertex[v]].push_back(v);
    for (auto& bs : shapes) {
        std::vector<uint32_t> idx; std::vector<glm::vec3> del;
        idx.reserve(bs.indices.size()); del.reserve(bs.indices.size());
        for (size_t k = 0; k < bs.indices.size(); ++k) {
            uint32_t s = bs.indices[k];
            if (s >= bySrc.size()) continue;
            for (uint32_t v : bySrc[s]) { idx.push_back(v); del.push_back(bs.deltas[k]); }
        }
        bs.indices.swap(idx); bs.deltas.swap(del);
    }
}

bool saveBlendShapesFRBS(const std::string& path, const std::vector<BlendShape>& shapes, size_t vertexCount, std::string* error) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    f.write("FRBS", 4);
    uint32_t v = 1, vc = uint32_t(vertexCount), sc = uint32_t(shapes.size());
    f.write(reinterpret_cast<const char*>(&v), 4); f.write(reinterpret_cast<const char*>(&vc), 4); f.write(reinterpret_cast<const char*>(&sc), 4);
    for (const auto& bs : shapes) {
        uint16_t nl = uint16_t(bs.name.size()); f.write(reinterpret_cast<const char*>(&nl), 2); f.write(bs.name.data(), nl);
        uint32_t n = uint32_t(bs.indices.size()); f.write(reinterpret_cast<const char*>(&n), 4);
        f.write(reinterpret_cast<const char*>(bs.indices.data()), std::streamsize(n * sizeof(uint32_t)));
        f.write(reinterpret_cast<const char*>(bs.deltas.data()), std::streamsize(n * sizeof(glm::vec3)));
    }
    return bool(f);
}

bool loadBlendShapeOBJ(const std::string& path, const Mesh& base, BlendShape& out, std::string* error) {
    Mesh t;
    if (!loadObj(path, t, error)) return false;
    if (t.vertexCount() != base.vertexCount()) { if (error) *error = "target vertex count differs from base mesh"; return false; }
    out = BlendShape{};
    auto slash = path.find_last_of("/\\"); auto dot = path.find_last_of('.');
    out.name = path.substr(slash == std::string::npos ? 0 : slash + 1, dot == std::string::npos ? std::string::npos : dot - (slash == std::string::npos ? 0 : slash + 1));
    for (size_t i = 0; i < base.vertexCount(); ++i) {
        glm::vec3 d = t.positions[i] - base.positions[i];
        if (glm::dot(d, d) > 1e-14f) { out.indices.push_back(uint32_t(i)); out.deltas.push_back(d); }
    }
    return true;
}

std::string canonicalShapeName(const std::string& n) {
    static const std::map<std::string, std::string> m = {
        {"jawOpen", shapes::JawOpen},
        {"mouthSmile", shapes::MouthSmile}, {"mouthSmile_L", shapes::MouthSmile}, {"mouthSmile_R", shapes::MouthSmile},
        {"mouthPucker", shapes::MouthPucker},
        {"mouthStretch", shapes::MouthWide}, {"mouthStretch_L", shapes::MouthWide}, {"mouthStretch_R", shapes::MouthWide},
        {"mouthPress", shapes::LipsPress}, {"mouthPress_L", shapes::LipsPress}, {"mouthPress_R", shapes::LipsPress}, {"mouthClose", shapes::LipsPress},
        {"browInnerUp", shapes::BrowRaise}, {"browInnerUp_L", shapes::BrowRaise}, {"browInnerUp_R", shapes::BrowRaise},
        {"browOuterUp_L", shapes::BrowRaise}, {"browOuterUp_R", shapes::BrowRaise},
        {"eyeBlink", shapes::EyeBlink}, {"eyeBlink_L", shapes::EyeBlink}, {"eyeBlink_R", shapes::EyeBlink},
        {"mouthFunnel", shapes::MouthFunnel},
    };
    auto it = m.find(n);
    return it == m.end() ? "" : it->second;
}

} // namespace fr

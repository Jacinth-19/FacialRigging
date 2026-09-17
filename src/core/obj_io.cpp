#include "core/obj_io.h"
#include <fstream>
#include <algorithm>
#include <sstream>
#include <map>
#include <tuple>
#include <cstdlib>

namespace fr {

static bool readAll(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool loadObj(const std::string& path, Mesh& out, std::string* error) {
    std::string text;
    if (!readAll(path, text)) { if (error) *error = "cannot open " + path; return false; }
    if (!parseObj(text, out, error)) return false;
    if (out.name == "mesh") {
        auto slash = path.find_last_of("/\\");
        out.name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    }
    return true;
}

// Resolve an OBJ 1-based (possibly negative) index into 0-based.
static bool resolve(long idx, size_t count, uint32_t& out) {
    if (idx > 0 && size_t(idx) <= count) { out = uint32_t(idx - 1); return true; }
    if (idx < 0 && size_t(-idx) <= count) { out = uint32_t(count + idx); return true; }
    return false;
}

bool parseObj(const std::string& text, Mesh& out, std::string* error) {
    std::vector<glm::vec3> P, N; std::vector<glm::vec2> T;
    std::map<std::tuple<uint32_t, long, long>, uint32_t> cache;
    out = Mesh{};
    std::istringstream in(text);
    std::string line;
    size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string tag; ls >> tag;
        if (tag == "v") { glm::vec3 p; ls >> p.x >> p.y >> p.z; P.push_back(p); }
        else if (tag == "vt") { glm::vec2 t; ls >> t.x >> t.y; T.push_back(t); }
        else if (tag == "vn") { glm::vec3 n; ls >> n.x >> n.y >> n.z; N.push_back(n); }
        else if (tag == "o" || tag == "g") {
            std::string n; ls >> n;
            if (n.empty()) n = "part" + std::to_string(out.parts.size());
            if (tag == "o" && out.name == "mesh") out.name = n;
            if (!out.parts.empty() && out.parts.back().indexCount == 0) out.parts.back().name = n; // consecutive g/o lines
            else out.parts.push_back({n, uint32_t(out.indices.size()), 0});
        }
        else if (tag == "f") {
            std::vector<uint32_t> face;
            std::string tok;
            while (ls >> tok) {
                long vi = 0, ti = 0, ni = 0;
                char* end = nullptr;
                vi = std::strtol(tok.c_str(), &end, 10);
                if (*end == '/') { ++end; if (*end != '/') ti = std::strtol(end, &end, 10);
                    if (*end == '/') { ++end; ni = std::strtol(end, &end, 10); } }
                uint32_t pv, pt = 0, pn = 0;
                if (!resolve(vi, P.size(), pv)) { if (error) *error = "bad vertex index at line " + std::to_string(lineNo); return false; }
                bool hasT = ti != 0 && resolve(ti, T.size(), pt);
                bool hasN = ni != 0 && resolve(ni, N.size(), pn);
                auto key = std::make_tuple(pv, hasT ? long(pt) + 1 : 0L, hasN ? long(pn) + 1 : 0L);
                auto it = cache.find(key);
                if (it == cache.end()) {
                    uint32_t id = uint32_t(out.positions.size());
                    out.positions.push_back(P[pv]);
                    out.sourceVertex.push_back(pv);
                    if (hasT) out.uvs.push_back(T[pt]); else if (!T.empty()) out.uvs.emplace_back(0, 0);
                    if (hasN) out.normals.push_back(N[pn]); else if (!N.empty()) out.normals.emplace_back(0, 0, 1);
                    it = cache.emplace(key, id).first;
                }
                face.push_back(it->second);
            }
            if (face.size() < 3) { if (error) *error = "degenerate face at line " + std::to_string(lineNo); return false; }
            if (out.parts.empty()) out.parts.push_back({"default", 0, 0});
            for (size_t i = 1; i + 1 < face.size(); ++i) // fan triangulation
                out.indices.insert(out.indices.end(), {face[0], face[i], face[i + 1]});
            out.parts.back().indexCount += uint32_t(3 * (face.size() - 2));
        }
    }
    if (out.positions.empty()) { if (error) *error = "no geometry found"; return false; }
    out.parts.erase(std::remove_if(out.parts.begin(), out.parts.end(), [](const MeshPart& p) { return p.indexCount == 0; }), out.parts.end());
    if (out.parts.size() == 1 && out.parts[0].name == "default") out.parts.clear();
    if (out.normals.size() != out.positions.size()) out.recomputeNormals();
    if (!out.uvs.empty() && out.uvs.size() != out.positions.size()) out.uvs.clear();
    return true;
}

bool saveObj(const std::string& path, const Mesh& mesh, std::string* error) {
    std::ofstream f(path);
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    f << "# FacialRigging export\no " << mesh.name << "\n";
    for (auto& p : mesh.positions) f << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    for (auto& t : mesh.uvs) f << "vt " << t.x << ' ' << t.y << '\n';
    for (auto& n : mesh.normals) f << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
    bool hasT = !mesh.uvs.empty(), hasN = !mesh.normals.empty();
    size_t nextPart = 0;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        while (nextPart < mesh.parts.size() && mesh.parts[nextPart].firstIndex == i) { f << "g " << mesh.parts[nextPart].name << '\n'; ++nextPart; }
        f << 'f';
        for (int k = 0; k < 3; ++k) {
            uint32_t id = mesh.indices[i + k] + 1;
            f << ' ' << id;
            if (hasT || hasN) { f << '/'; if (hasT) f << id; if (hasN) f << '/' << id; }
        }
        f << '\n';
    }
    return true;
}

} // namespace fr

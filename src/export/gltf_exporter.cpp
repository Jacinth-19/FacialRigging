// Minimal dependency-free glTF 2.0 (.glb / .gltf+.bin) writer for a skinned, morph-targeted
// mesh with baked animations. Only the subset of the spec we need is emitted.
#include "export/exporter.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <limits>

namespace fr {
namespace {

struct BinBuilder {
    std::vector<uint8_t> data;
    struct View { size_t offset, length; int target; };
    std::vector<View> views;
    struct Accessor { int view = 0; int componentType = 0; int count = 0; std::string type; std::vector<float> min{}, max{}; bool hasMinMax = false; };
    std::vector<Accessor> accessors;

    static Accessor mk(int view, int ct, int count, const char* type) { Accessor a; a.view = view; a.componentType = ct; a.count = count; a.type = type; return a; }
    void align(size_t n = 4) { while (data.size() % n) data.push_back(0); }
    template <typename T> int addView(const std::vector<T>& v, int target = 0) {
        align();
        size_t off = data.size(), len = v.size() * sizeof(T);
        data.resize(off + len);
        if (len) std::memcpy(data.data() + off, v.data(), len);
        views.push_back({off, len, target});
        return int(views.size()) - 1;
    }
    int addVec3(const std::vector<glm::vec3>& v, bool minmax = false) {
        int view = addView(v, 34962);
        Accessor a; a.view = view; a.componentType = 5126; a.count = int(v.size()); a.type = "VEC3";
        if (minmax && !v.empty()) {
            glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
            for (auto& p : v) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
            a.min = {lo.x, lo.y, lo.z}; a.max = {hi.x, hi.y, hi.z}; a.hasMinMax = true;
        }
        accessors.push_back(a); return int(accessors.size()) - 1;
    }
    int addVec2(const std::vector<glm::vec2>& v) { int view = addView(v, 34962); accessors.push_back(mk(view, 5126, int(v.size()), "VEC2")); return int(accessors.size()) - 1; }
    int addVec4(const std::vector<glm::vec4>& v, int target = 34962) { int view = addView(v, target); accessors.push_back(mk(view, 5126, int(v.size()), "VEC4")); return int(accessors.size()) - 1; }
    int addQuat(const std::vector<glm::quat>& q) {
        std::vector<glm::vec4> v; v.reserve(q.size());
        for (auto& r : q) v.emplace_back(r.x, r.y, r.z, r.w);
        return addVec4(v, 0);
    }
    int addUShort4(const std::vector<glm::u16vec4>& v) { int view = addView(v, 34962); accessors.push_back(mk(view, 5123, int(v.size()), "VEC4")); return int(accessors.size()) - 1; }
    int addIndices(const std::vector<uint32_t>& v) { int view = addView(v, 34963); accessors.push_back(mk(view, 5125, int(v.size()), "SCALAR")); return int(accessors.size()) - 1; }
    int addScalars(const std::vector<float>& v, bool minmax = false) {
        int view = addView(v, 0);
        Accessor a; a.view = view; a.componentType = 5126; a.count = int(v.size()); a.type = "SCALAR";
        if (minmax && !v.empty()) { float lo = v.front(), hi = v.front(); for (float f : v) { lo = std::min(lo, f); hi = std::max(hi, f); } a.min = {lo}; a.max = {hi}; a.hasMinMax = true; }
        accessors.push_back(a); return int(accessors.size()) - 1;
    }
    int addMat4(const std::vector<glm::mat4>& v) { int view = addView(v, 0); accessors.push_back(mk(view, 5126, int(v.size()), "MAT4")); return int(accessors.size()) - 1; }
};

std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((unsigned char)c < 0x20) o += ' '; else o += c; }
    return o + "\"";
}
std::string num(float f) { std::ostringstream ss; ss << std::setprecision(9) << f; return ss.str(); }
template <typename T> std::string arr(const std::vector<T>& v, std::string (*fmt)(T)) { std::string s = "["; for (size_t i = 0; i < v.size(); ++i) { if (i) s += ","; s += fmt(v[i]); } return s + "]"; }
std::string fnum(float f) { return num(f); }
std::string inum(int i) { return std::to_string(i); }

} // namespace

bool GltfExporter::exportScene(const Rig& rig, const std::vector<AnimationClip>& clips, const std::string& path,
                               const ExportOptions& opts, std::string* error) {
    const Mesh& mesh = rig.mesh;
    if (mesh.positions.empty()) { if (error) *error = "empty mesh"; return false; }
    const bool binary = path.size() < 5 || path.substr(path.size() - 5) != ".gltf";
    const bool withSkin = opts.exportSkeleton && rig.hasSkin() && !rig.skeleton.bones.empty();
    const bool withMorph = opts.exportBlendShapes && !rig.blendShapes.empty();

    BinBuilder bin;
    std::vector<std::string> nodes, meshes, skins, animations, accessorsJson, viewsJson;
    // --- mesh -----------------------------------------------------------------
    int accPos = bin.addVec3(mesh.positions, true);
    std::vector<glm::vec3> normals = mesh.normals.size() == mesh.positions.size() ? mesh.normals : Mesh::computeNormals(mesh.positions, mesh.indices);
    int accNrm = bin.addVec3(normals);
    int accUv = mesh.uvs.size() == mesh.positions.size() ? bin.addVec2(mesh.uvs) : -1;
    int accIdx = bin.addIndices(mesh.indices);
    int accJoints = -1, accWeights = -1;
    if (withSkin) {
        std::vector<glm::u16vec4> j(mesh.vertexCount()); std::vector<glm::vec4> w(mesh.vertexCount());
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            VertexInfluence inf = rig.skin[i]; inf.normalize();
            j[i] = glm::u16vec4(inf.bones); w[i] = inf.weights;
            for (int k = 0; k < 4; ++k) if (w[i][k] <= 0.0f) j[i][k] = 0;
        }
        accJoints = bin.addUShort4(j); accWeights = bin.addVec4(w);
    }
    std::string prim = "{\"attributes\":{\"POSITION\":" + inum(accPos) + ",\"NORMAL\":" + inum(accNrm);
    if (accUv >= 0) prim += ",\"TEXCOORD_0\":" + inum(accUv);
    if (withSkin) prim += ",\"JOINTS_0\":" + inum(accJoints) + ",\"WEIGHTS_0\":" + inum(accWeights);
    prim += "},\"indices\":" + inum(accIdx) + ",\"mode\":4";
    std::string targetNames;
    if (withMorph) {
        prim += ",\"targets\":[";
        for (size_t s = 0; s < rig.blendShapes.size(); ++s) {
            auto dense = rig.blendShapes[s].dense(mesh.vertexCount());
            int acc = bin.addVec3(dense, true);
            if (s) { prim += ","; targetNames += ","; }
            prim += "{\"POSITION\":" + inum(acc) + "}";
            targetNames += jsonStr(rig.blendShapes[s].name);
        }
        prim += "]";
    }
    prim += "}";
    std::string meshJson = "{\"name\":" + jsonStr(mesh.name) + ",\"primitives\":[" + prim + "]";
    if (withMorph) {
        std::vector<float> w0; for (auto& bs : rig.blendShapes) w0.push_back(bs.weight);
        meshJson += ",\"weights\":" + arr(w0, fnum) + ",\"extras\":{\"targetNames\":[" + targetNames + "]}";
    }
    meshJson += "}";
    meshes.push_back(meshJson);

    // --- nodes: 0 = mesh node, 1.. = bones -------------------------------------
    std::string meshNode = "{\"name\":" + jsonStr(mesh.name) + ",\"mesh\":0";
    if (withSkin) meshNode += ",\"skin\":0";
    meshNode += "}";
    nodes.push_back(meshNode);
    std::vector<int> sceneRoots = {0};
    if (withSkin) {
        const auto& bones = rig.skeleton.bones;
        int base = int(nodes.size());
        std::vector<std::vector<int>> children(bones.size());
        for (size_t b = 0; b < bones.size(); ++b) if (bones[b].parent >= 0) children[bones[b].parent].push_back(base + int(b)); else sceneRoots.push_back(base + int(b));
        for (size_t b = 0; b < bones.size(); ++b) {
            const Bone& bn = bones[b];
            glm::quat r = bn.bindRotation * bn.poseRotation; glm::vec3 t = bn.bindTranslation + bn.poseTranslation;
            std::string n = "{\"name\":" + jsonStr(bn.name) + ",\"translation\":[" + num(t.x) + "," + num(t.y) + "," + num(t.z) + "],\"rotation\":[" + num(r.x) + "," + num(r.y) + "," + num(r.z) + "," + num(r.w) + "]";
            if (!children[b].empty()) n += ",\"children\":" + arr(children[b], inum);
            nodes.push_back(n + "}");
        }
        auto bind = rig.skeleton.bindWorldMatrices();
        for (auto& m : bind) m = glm::inverse(m);
        int accIbm = bin.addMat4(bind);
        std::vector<int> joints; for (size_t b = 0; b < bones.size(); ++b) joints.push_back(base + int(b));
        skins.push_back("{\"name\":\"FaceSkin\",\"inverseBindMatrices\":" + inum(accIbm) + ",\"joints\":" + arr(joints, inum) + ",\"skeleton\":" + inum(base) + "}");
    }

    // --- animations -----------------------------------------------------------
    for (const auto& clip : clips) {
        std::vector<std::string> samplers, channels;
        if (withMorph && !clip.blendCurves.empty()) {
            // glTF wants one "weights" channel with all targets interleaved per keyframe: bake on the clip's frame grid.
            int frames = std::max(clip.frameCount(), 1);
            std::vector<float> times(frames), weights; weights.reserve(size_t(frames) * rig.blendShapes.size());
            for (int f = 0; f < frames; ++f) {
                float t = frames > 1 ? f / clip.frameRate : 0.0f; times[f] = t;
                for (auto& bs : rig.blendShapes) { const auto* c = clip.findBlendCurve(bs.name); weights.push_back(c ? c->sample(t) : bs.weight); }
            }
            int accT = bin.addScalars(times, true), accW = bin.addScalars(weights);
            channels.push_back("{\"sampler\":" + inum(int(samplers.size())) + ",\"target\":{\"node\":0,\"path\":\"weights\"}}");
            samplers.push_back("{\"input\":" + inum(accT) + ",\"output\":" + inum(accW) + ",\"interpolation\":\"LINEAR\"}");
        }
        if (withSkin) {
            for (const auto& c : clip.boneRotations) {
                int b = rig.skeleton.find(c.target); if (b < 0 || c.empty()) continue;
                std::vector<glm::quat> q; for (auto& v : c.values) q.push_back(glm::normalize(rig.skeleton.bones[b].bindRotation * v));
                int accT = bin.addScalars(c.times, true), accR = bin.addQuat(q);
                channels.push_back("{\"sampler\":" + inum(int(samplers.size())) + ",\"target\":{\"node\":" + inum(1 + b) + ",\"path\":\"rotation\"}}");
                samplers.push_back("{\"input\":" + inum(accT) + ",\"output\":" + inum(accR) + ",\"interpolation\":\"LINEAR\"}");
            }
            for (const auto& c : clip.boneTranslations) {
                int b = rig.skeleton.find(c.target); if (b < 0 || c.empty()) continue;
                std::vector<glm::vec3> tr; for (auto& v : c.values) tr.push_back(rig.skeleton.bones[b].bindTranslation + v);
                int accT = bin.addScalars(c.times, true), accV = bin.addVec3(tr);
                channels.push_back("{\"sampler\":" + inum(int(samplers.size())) + ",\"target\":{\"node\":" + inum(1 + b) + ",\"path\":\"translation\"}}");
                samplers.push_back("{\"input\":" + inum(accT) + ",\"output\":" + inum(accV) + ",\"interpolation\":\"LINEAR\"}");
            }
        }
        if (channels.empty()) continue;
        std::string a = "{\"name\":" + jsonStr(clip.name) + ",\"samplers\":[";
        for (size_t i = 0; i < samplers.size(); ++i) { if (i) a += ","; a += samplers[i]; }
        a += "],\"channels\":[";
        for (size_t i = 0; i < channels.size(); ++i) { if (i) a += ","; a += channels[i]; }
        animations.push_back(a + "]}");
    }

    // --- buffer views / accessors ----------------------------------------------
    bin.align();
    std::string binName;
    if (!binary) { auto slash = path.find_last_of("/\\"); binName = path.substr(slash == std::string::npos ? 0 : slash + 1); binName = binName.substr(0, binName.size() - 5) + ".bin"; }
    for (auto& v : bin.views) {
        std::string s = "{\"buffer\":0,\"byteOffset\":" + std::to_string(v.offset) + ",\"byteLength\":" + std::to_string(v.length);
        if (v.target) s += ",\"target\":" + inum(v.target);
        viewsJson.push_back(s + "}");
    }
    for (auto& a : bin.accessors) {
        std::string s = "{\"bufferView\":" + inum(a.view) + ",\"componentType\":" + inum(a.componentType) + ",\"count\":" + inum(a.count) + ",\"type\":" + jsonStr(a.type);
        if (a.hasMinMax) s += ",\"min\":" + arr(a.min, fnum) + ",\"max\":" + arr(a.max, fnum);
        accessorsJson.push_back(s + "}");
    }
    auto join = [](const std::vector<std::string>& v) { std::string s; for (size_t i = 0; i < v.size(); ++i) { if (i) s += ","; s += v[i]; } return s; };
    std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"FacialRigging\"},\"scene\":0,\"scenes\":[{\"nodes\":" + arr(sceneRoots, inum) + "}],"
        "\"nodes\":[" + join(nodes) + "],\"meshes\":[" + join(meshes) + "]";
    if (!skins.empty()) json += ",\"skins\":[" + join(skins) + "]";
    if (!animations.empty()) json += ",\"animations\":[" + join(animations) + "]";
    json += ",\"bufferViews\":[" + join(viewsJson) + "],\"accessors\":[" + join(accessorsJson) + "],\"buffers\":[{\"byteLength\":" + std::to_string(bin.data.size());
    if (!binary) json += ",\"uri\":" + jsonStr(binName);
    json += "}]}";

    // --- write ----------------------------------------------------------------
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    if (binary) {
        while (json.size() % 4) json += ' ';
        uint32_t total = 12 + 8 + uint32_t(json.size()) + 8 + uint32_t(bin.data.size());
        auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        w32(0x46546C67); w32(2); w32(total);
        w32(uint32_t(json.size())); w32(0x4E4F534A); f.write(json.data(), std::streamsize(json.size()));
        w32(uint32_t(bin.data.size())); w32(0x004E4942); f.write(reinterpret_cast<const char*>(bin.data.data()), std::streamsize(bin.data.size()));
    } else {
        f << json;
        std::string binPath = path.substr(0, path.size() - 5) + ".bin";
        std::ofstream b(binPath, std::ios::binary);
        if (!b) { if (error) *error = "cannot write " + binPath; return false; }
        b.write(reinterpret_cast<const char*>(bin.data.data()), std::streamsize(bin.data.size()));
    }
    return true;
}

} // namespace fr

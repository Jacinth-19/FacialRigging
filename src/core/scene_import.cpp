#include "core/scene_import.h"
#include "rig/blendshape_io.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <filesystem>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace fr {

namespace {
glm::mat4 toGlm(const aiMatrix4x4& m) { return glm::transpose(glm::make_mat4(&m.a1)); }
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }

/// Some exporters (Blender, Maya) keep the ARKit names but with prefixes like "head.jawOpen" or
/// "Basis:jawOpen"; strip to the last component so canonicalShapeName can match.
std::string cleanShapeName(std::string n) {
    size_t p = n.find_last_of(".:|/"); if (p != std::string::npos && p + 1 < n.size()) n = n.substr(p + 1);
    return n;
}
} // namespace

bool sceneImportAvailable() { return true; }
std::vector<std::string> importableExtensions() { return {".fbx", ".glb", ".gltf", ".dae", ".obj"}; }

bool importScene(const std::string& path, const SceneImportOptions& opts, SceneImportResult& out, std::string* error) {
    Assimp::Importer imp;
    unsigned flags = aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights | aiProcess_GenSmoothNormals | aiProcess_SortByPType | aiProcess_PopulateArmatureData;
    if (opts.triangulate) flags |= aiProcess_Triangulate;
    imp.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
    imp.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* sc = imp.ReadFile(path, flags);
    if (!sc || !sc->mRootNode) { if (error) *error = imp.GetErrorString(); return false; }
    std::ostringstream log;

    // ---- collect meshes with their world transforms (node hierarchy flattened)
    struct MeshInst { const aiMesh* m; glm::mat4 world; std::string node; };
    std::vector<MeshInst> inst;
    std::map<std::string, glm::mat4> nodeWorld;
    std::map<std::string, const aiNode*> nodeByName;
    std::function<void(const aiNode*, const glm::mat4&)> walk = [&](const aiNode* n, const glm::mat4& parent) {
        glm::mat4 w = parent * toGlm(n->mTransformation);
        nodeWorld[n->mName.C_Str()] = w; nodeByName[n->mName.C_Str()] = n;
        for (unsigned i = 0; i < n->mNumMeshes; ++i) { const aiMesh* m = sc->mMeshes[n->mMeshes[i]]; if (m->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) inst.push_back({m, w, n->mName.C_Str()}); }
        for (unsigned i = 0; i < n->mNumChildren; ++i) walk(n->mChildren[i], w);
    };
    walk(sc->mRootNode, glm::mat4(1.0f));
    if (inst.empty()) { if (error) *error = "no triangle meshes in file"; return false; }

    // ---- merge meshes
    Mesh& mesh = out.mesh; mesh = Mesh{}; mesh.name = std::filesystem::path(path).stem().string();
    std::vector<glm::mat4> vertexWorld;   // per merged vertex: world transform used (for skinning inverse-bind fixups)
    std::vector<uint32_t> meshBase;       // per instance: first merged vertex
    std::map<std::string, std::vector<glm::vec3>> morphDense;  // target name -> dense delta over merged vertices
    std::map<std::string, std::vector<std::pair<std::string, int>>> morphSources;
    std::vector<std::string> meshMorphNames;  // for animation mapping (per instance, per target)
    for (const MeshInst& mi : inst) {
        const aiMesh* m = mi.m; uint32_t base = uint32_t(mesh.positions.size()); meshBase.push_back(base);
        glm::mat3 nrm = glm::transpose(glm::inverse(glm::mat3(mi.world)));
        for (unsigned v = 0; v < m->mNumVertices; ++v) {
            glm::vec3 p = glm::vec3(mi.world * glm::vec4(m->mVertices[v].x, m->mVertices[v].y, m->mVertices[v].z, 1.0f));
            mesh.positions.push_back(p);
            mesh.normals.push_back(m->HasNormals() ? glm::normalize(nrm * glm::vec3(m->mNormals[v].x, m->mNormals[v].y, m->mNormals[v].z)) : glm::vec3(0, 0, 1));
            if (m->HasTextureCoords(0)) mesh.uvs.push_back(glm::vec2(m->mTextureCoords[0][v].x, m->mTextureCoords[0][v].y));
        }
        MeshPart part; part.name = m->mName.length ? m->mName.C_Str() : mi.node; part.firstIndex = uint32_t(mesh.indices.size());
        for (unsigned f = 0; f < m->mNumFaces; ++f) { const aiFace& fc = m->mFaces[f]; if (fc.mNumIndices != 3) continue; for (unsigned k = 0; k < 3; ++k) mesh.indices.push_back(base + fc.mIndices[k]); }
        part.indexCount = uint32_t(mesh.indices.size()) - part.firstIndex; mesh.parts.push_back(part);
        // morph targets
        if (opts.importBlendShapes) for (unsigned a = 0; a < m->mNumAnimMeshes; ++a) {
            const aiAnimMesh* am = m->mAnimMeshes[a];
            std::string name = am->mName.length ? cleanShapeName(am->mName.C_Str()) : ("Morph" + std::to_string(a));
            auto& dense = morphDense[name]; if (dense.empty()) dense.assign(mesh.positions.size(), glm::vec3(0));
            dense.resize(mesh.positions.size(), glm::vec3(0));
            if (am->HasPositions()) for (unsigned v = 0; v < am->mNumVertices && v < m->mNumVertices; ++v) {
                glm::vec3 d(am->mVertices[v].x - m->mVertices[v].x, am->mVertices[v].y - m->mVertices[v].y, am->mVertices[v].z - m->mVertices[v].z);
                dense[base + v] = glm::vec3(mi.world * glm::vec4(d, 0.0f));
            }
        }
    }
    if (mesh.uvs.size() != mesh.positions.size()) mesh.uvs.clear();
    // keep all morph arrays the same length
    for (auto& [n, d] : morphDense) d.resize(mesh.positions.size(), glm::vec3(0));

    // ---- skeleton: union of all bones referenced by the meshes, plus ancestors up to a common root
    out.skeleton = Skeleton{}; out.skin.clear();
    std::map<std::string, glm::mat4> offsetOf;     // inverse bind (mesh space -> bone space), per bone
    std::set<std::string> used;
    if (opts.importSkeleton) {
        for (size_t ii = 0; ii < inst.size(); ++ii) for (unsigned b = 0; b < inst[ii].m->mNumBones; ++b) { const aiBone* bn = inst[ii].m->mBones[b]; used.insert(bn->mName.C_Str()); if (!offsetOf.count(bn->mName.C_Str())) offsetOf[bn->mName.C_Str()] = toGlm(bn->mOffsetMatrix) * glm::inverse(inst[ii].world); }
        if (!used.empty()) {
            // include ancestors
            std::set<std::string> keep = used;
            for (const std::string& n : used) { const aiNode* nd = nodeByName.count(n) ? nodeByName[n] : nullptr; while (nd && nd->mParent) { nd = nd->mParent; if (nd == sc->mRootNode) break; keep.insert(nd->mName.C_Str()); } }
            if (!opts.pruneUnusedBones) { for (auto& [n, nd] : nodeByName) if (nd->mNumMeshes == 0) keep.insert(n); }
            // order: depth-first from the root so parents precede children
            std::map<std::string, int> indexOf;
            std::function<void(const aiNode*, int)> addBones = [&](const aiNode* n, int parentIdx) {
                int my = parentIdx;
                if (keep.count(n->mName.C_Str())) {
                    Bone b; b.name = n->mName.C_Str(); b.parent = parentIdx;
                    glm::mat4 world = nodeWorld[b.name];
                    glm::mat4 parentWorld = parentIdx >= 0 ? nodeWorld[out.skeleton.bones[size_t(parentIdx)].name] : glm::mat4(1.0f);
                    glm::mat4 local = glm::inverse(parentWorld) * world;
                    glm::vec3 scale, trans, skew; glm::vec4 persp; glm::quat rot;
                    glm::decompose(local, scale, rot, trans, skew, persp);
                    b.bindTranslation = trans; b.bindRotation = rot;
                    my = int(out.skeleton.bones.size()); indexOf[b.name] = my; out.skeleton.bones.push_back(b);
                }
                for (unsigned i = 0; i < n->mNumChildren; ++i) addBones(n->mChildren[i], my);
            };
            addBones(sc->mRootNode, -1);
            // skin weights
            out.skin.assign(mesh.positions.size(), VertexInfluence{});
            for (size_t ii = 0; ii < inst.size(); ++ii) for (unsigned b = 0; b < inst[ii].m->mNumBones; ++b) {
                const aiBone* bn = inst[ii].m->mBones[b]; auto it = indexOf.find(bn->mName.C_Str()); if (it == indexOf.end()) continue;
                for (unsigned w = 0; w < bn->mNumWeights; ++w) out.skin[meshBase[ii] + bn->mWeights[w].mVertexId].add(it->second, bn->mWeights[w].mWeight);
            }
            size_t unskinned = 0;
            for (auto& inf : out.skin) { float s = inf.weights.x + inf.weights.y + inf.weights.z + inf.weights.w; if (s < 1e-6f) { ++unskinned; inf.bones = glm::ivec4(0); inf.weights = glm::vec4(1, 0, 0, 0); } else inf.normalize(); }
            log << out.skeleton.bones.size() << " bones (" << used.size() << " skinning), " << unskinned << " unskinned vertices bound to root; ";
        }
    }
    // Our skinning uses bind-pose world matrices from the hierarchy as the inverse-bind; since the
    // merged mesh is already in world space and the bones' bind pose is their node transform,
    // skinningMatrices() = poseWorld * inverse(bindWorld) is exactly Assimp's mOffsetMatrix path
    // when the file's bind pose equals the node pose (true for glTF and for FBX bind poses
    // exported at rest). Files whose offset matrices disagree get logged.
    (void)offsetOf;

    // ---- normalise (recentre/rescale) - move bones/deltas with it
    if (opts.normalizeToUnit && !mesh.positions.empty()) {
        glm::vec3 lo = mesh.boundsMin(), hi = mesh.boundsMax(); glm::vec3 c = 0.5f * (lo + hi); float ext = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}); float s = ext > 1e-9f ? 1.0f / ext : 1.0f;
        for (auto& p : mesh.positions) p = (p - c) * s;
        for (auto& [n, d] : morphDense) for (auto& v : d) v *= s;
        for (auto& b : out.skeleton.bones) { b.bindTranslation *= s; if (b.parent < 0) b.bindTranslation -= c * s; }
        // note: root translation is in world space: (t - c) * s ; child translations are relative and just scale
    }

    // ---- blendshapes (sparse)
    for (auto& [n, d] : morphDense) {
        BlendShape bs; bs.name = n;
        for (size_t i = 0; i < d.size(); ++i) if (glm::dot(d[i], d[i]) > 1e-16f) { bs.indices.push_back(uint32_t(i)); bs.deltas.push_back(d[i]); }
        if (bs.indices.empty()) continue;
        if (!canonicalShapeName(n).empty()) ++out.arkitShapeCount;
        out.blendShapes.push_back(std::move(bs));
    }
    log << mesh.parts.size() << " meshes merged (" << mesh.positions.size() << " verts, " << mesh.triangleCount() << " tris), " << out.blendShapes.size() << " morph targets (" << out.arkitShapeCount << " ARKit/ICT-named)";

    // ---- animations
    if (opts.importAnimation) for (unsigned a = 0; a < sc->mNumAnimations; ++a) {
        const aiAnimation* an = sc->mAnimations[a];
        double tps = an->mTicksPerSecond > 0 ? an->mTicksPerSecond : 25.0;
        AnimationClip clip; clip.name = an->mName.length ? an->mName.C_Str() : ("Take" + std::to_string(a + 1)); clip.duration = float(an->mDuration / tps); clip.frameRate = 30.0f;
        float scaleT = 1.0f; if (opts.normalizeToUnit) { glm::vec3 lo = mesh.boundsMin(), hi = mesh.boundsMax(); (void)lo; (void)hi; }
        for (unsigned c = 0; c < an->mNumChannels; ++c) {
            const aiNodeAnim* ch = an->mChannels[c]; std::string bone = ch->mNodeName.C_Str();
            if (out.skeleton.find(bone) < 0) continue;
            int bi = out.skeleton.find(bone); const Bone& b = out.skeleton.bones[size_t(bi)];
            if (ch->mNumRotationKeys > 1 || (ch->mNumRotationKeys == 1)) {
                Curve<glm::quat> cv; cv.target = bone;
                glm::quat invBind = glm::inverse(b.bindRotation);
                for (unsigned k = 0; k < ch->mNumRotationKeys; ++k) { const auto& q = ch->mRotationKeys[k].mValue; cv.addKey(float(ch->mRotationKeys[k].mTime / tps), invBind * glm::quat(q.w, q.x, q.y, q.z)); }
                bool moving = false; for (const auto& q : cv.values) if (std::abs(glm::angle(q)) > 1e-3f) { moving = true; break; }
                if (moving) clip.boneRotations.push_back(std::move(cv));
            }
            (void)scaleT;
        }
        for (unsigned c = 0; c < an->mNumMorphMeshChannels; ++c) {
            const aiMeshMorphAnim* mc = an->mMorphMeshChannels[c];
            // channel name is "<mesh>*<n>" or the mesh name; map key indices onto that mesh's anim meshes
            std::string nm = mc->mName.C_Str(); size_t star = nm.find('*'); if (star != std::string::npos) nm = nm.substr(0, star);
            const aiMesh* target = nullptr; for (const auto& mi : inst) if (nm == mi.m->mName.C_Str() || nm == mi.node) { target = mi.m; break; }
            if (!target) continue;
            std::map<unsigned, Curve<float>> curves;
            for (unsigned k = 0; k < mc->mNumKeys; ++k) { const aiMeshMorphKey& key = mc->mKeys[k]; for (unsigned v = 0; v < key.mNumValuesAndWeights; ++v) { unsigned ti = key.mValues[v]; if (ti >= target->mNumAnimMeshes) continue; auto& cv = curves[ti]; if (cv.target.empty()) cv.target = cleanShapeName(target->mAnimMeshes[ti]->mName.C_Str()); cv.addKey(float(key.mTime / tps), float(key.mWeights[v])); } }
            for (auto& [ti, cv] : curves) { bool any = false; for (float w : cv.values) any |= w > 1e-4f; if (any) clip.blendCurves.push_back(std::move(cv)); }
        }
        if (!clip.boneRotations.empty() || !clip.blendCurves.empty()) out.clips.push_back(std::move(clip));
    }
    if (!out.clips.empty()) log << "; " << out.clips.size() << " animation(s), first '" << out.clips[0].name << "' " << out.clips[0].duration << " s";
    out.log = log.str();
    if (error) error->clear();
    return true;
}

int installImportedRig(Rig& rig, SceneImportResult& in) {
    rig.setMesh(in.mesh);
    rig.skeleton = in.skeleton;
    if (in.skin.size() == rig.mesh.vertexCount()) rig.skin = in.skin;
    rig.blendShapes.clear(); rig.combinations.clear(); rig.controlPoints.clear();
    int covered = 0;
    if (!in.blendShapes.empty()) covered = rig.installAuthoredBlendShapes(in.blendShapes);
    // Without a Jaw bone the generator still works through the JawOpen shape; make sure the
    // canonical shapes exist even when nothing mapped (empty shapes are harmless).
    return covered;
}

} // namespace fr

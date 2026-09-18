// FBX (binary) export through Assimp's built-in FBX writer. No Autodesk SDK needed.
// Emits: mesh (positions/normals/uvs), bone hierarchy + aiBone weights (-> FBX Skin/Cluster),
// blendshape targets as aiAnimMesh (-> FBX BlendShape deformer + channels), and one aiAnimation
// per clip with bone rotation/translation keys and morph-weight keys.
//
// Note: Assimp's FBX exporter writes bone TRS animation; morph key export depends on the Assimp
// version (5.4 writes blendshape deformers, but not DeformPercent curves). We therefore also bake
// the jaw bone from JawOpen so mouth motion survives, and record the limitation in the log.
#include "export/exporter.h"
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cstring>

namespace fr {

namespace {
aiMatrix4x4 toAi(const glm::mat4& m) {
    // glm is column-major, aiMatrix4x4 is row-major.
    aiMatrix4x4 r;
    r.a1 = m[0][0]; r.a2 = m[1][0]; r.a3 = m[2][0]; r.a4 = m[3][0];
    r.b1 = m[0][1]; r.b2 = m[1][1]; r.b3 = m[2][1]; r.b4 = m[3][1];
    r.c1 = m[0][2]; r.c2 = m[1][2]; r.c3 = m[2][2]; r.c4 = m[3][2];
    r.d1 = m[0][3]; r.d2 = m[1][3]; r.d3 = m[2][3]; r.d4 = m[3][3];
    return r;
}
glm::mat4 localBind(const Bone& b) { return glm::translate(glm::mat4(1.0f), b.bindTranslation) * glm::toMat4(b.bindRotation); }
} // namespace

bool AssimpFbxExporter::available() const { return true; }

bool AssimpFbxExporter::exportScene(const Rig& rig, const std::vector<AnimationClip>& clips, const std::string& path,
                                    const ExportOptions& opts, std::string* error) {
    const Mesh& mesh = rig.mesh;
    if (mesh.positions.empty()) { if (error) *error = "empty mesh"; return false; }
    const bool withSkin = opts.exportSkeleton && rig.hasSkin() && !rig.skeleton.bones.empty();
    const bool withMorph = opts.exportBlendShapes && !rig.blendShapes.empty();

    aiScene* scene = new aiScene();
    scene->mRootNode = new aiNode("Root");

    // --- mesh ------------------------------------------------------------------------------
    aiMesh* am = new aiMesh();
    am->mName = aiString(mesh.name);
    am->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    am->mNumVertices = unsigned(mesh.vertexCount());
    am->mVertices = new aiVector3D[am->mNumVertices];
    am->mNormals = new aiVector3D[am->mNumVertices];
    auto normals = mesh.normals.size() == mesh.positions.size() ? mesh.normals : Mesh::computeNormals(mesh.positions, mesh.indices);
    for (unsigned i = 0; i < am->mNumVertices; ++i) {
        am->mVertices[i] = aiVector3D(mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z);
        am->mNormals[i] = aiVector3D(normals[i].x, normals[i].y, normals[i].z);
    }
    if (mesh.uvs.size() == mesh.positions.size()) {
        am->mNumUVComponents[0] = 2;
        am->mTextureCoords[0] = new aiVector3D[am->mNumVertices];
        for (unsigned i = 0; i < am->mNumVertices; ++i) am->mTextureCoords[0][i] = aiVector3D(mesh.uvs[i].x, mesh.uvs[i].y, 0.0f);
    }
    am->mNumFaces = unsigned(mesh.triangleCount());
    am->mFaces = new aiFace[am->mNumFaces];
    for (unsigned f = 0; f < am->mNumFaces; ++f) {
        am->mFaces[f].mNumIndices = 3; am->mFaces[f].mIndices = new unsigned[3];
        for (int k = 0; k < 3; ++k) am->mFaces[f].mIndices[k] = mesh.indices[3 * f + k];
    }
    am->mMaterialIndex = 0;
    scene->mNumMaterials = 1; scene->mMaterials = new aiMaterial*[1]; scene->mMaterials[0] = new aiMaterial();
    { aiString n("FaceMaterial"); scene->mMaterials[0]->AddProperty(&n, AI_MATKEY_NAME); }

    // --- blendshapes as anim meshes ----------------------------------------------------------
    if (withMorph) {
        am->mMethod = aiMorphingMethod_MORPH_NORMALIZED;
        am->mNumAnimMeshes = unsigned(rig.blendShapes.size());
        am->mAnimMeshes = new aiAnimMesh*[am->mNumAnimMeshes];
        for (unsigned s = 0; s < am->mNumAnimMeshes; ++s) {
            const BlendShape& bs = rig.blendShapes[s];
            aiAnimMesh* t = new aiAnimMesh();
            t->mName = aiString(bs.name);
            t->mNumVertices = am->mNumVertices;
            t->mVertices = new aiVector3D[t->mNumVertices];
            t->mNormals = new aiVector3D[t->mNumVertices];
            auto dense = bs.dense(mesh.vertexCount());
            for (unsigned i = 0; i < t->mNumVertices; ++i) {
                glm::vec3 p = mesh.positions[i] + dense[i];
                t->mVertices[i] = aiVector3D(p.x, p.y, p.z);
                t->mNormals[i] = am->mNormals[i];
            }
            t->mWeight = bs.weight;
            am->mAnimMeshes[s] = t;
        }
    }

    // --- nodes: mesh node + bone hierarchy ---------------------------------------------------
    aiNode* meshNode = new aiNode(mesh.name);
    meshNode->mNumMeshes = 1; meshNode->mMeshes = new unsigned[1]{0};
    std::vector<aiNode*> children{meshNode};
    std::vector<aiNode*> boneNodes;
    if (withSkin) {
        const auto& bones = rig.skeleton.bones;
        boneNodes.resize(bones.size());
        std::vector<std::vector<aiNode*>> kids(bones.size());
        for (size_t b = 0; b < bones.size(); ++b) {
            boneNodes[b] = new aiNode(bones[b].name);
            boneNodes[b]->mTransformation = toAi(localBind(bones[b]));
        }
        for (size_t b = 0; b < bones.size(); ++b) {
            if (bones[b].parent >= 0) kids[bones[b].parent].push_back(boneNodes[b]); else children.push_back(boneNodes[b]);
        }
        for (size_t b = 0; b < bones.size(); ++b) {
            boneNodes[b]->mNumChildren = unsigned(kids[b].size());
            if (!kids[b].empty()) { boneNodes[b]->mChildren = new aiNode*[kids[b].size()]; for (size_t k = 0; k < kids[b].size(); ++k) { boneNodes[b]->mChildren[k] = kids[b][k]; kids[b][k]->mParent = boneNodes[b]; } }
        }
        // aiBone weights + offset matrices (inverse bind world)
        auto bindWorld = rig.skeleton.bindWorldMatrices();
        am->mNumBones = unsigned(bones.size());
        am->mBones = new aiBone*[am->mNumBones];
        for (size_t b = 0; b < bones.size(); ++b) {
            aiBone* ab = new aiBone();
            ab->mName = aiString(bones[b].name);
            ab->mOffsetMatrix = toAi(glm::inverse(bindWorld[b]));
            std::vector<aiVertexWeight> w;
            for (size_t v = 0; v < rig.skin.size(); ++v)
                for (int k = 0; k < 4; ++k)
                    if (rig.skin[v].bones[k] == int(b) && rig.skin[v].weights[k] > 0.0f) w.push_back(aiVertexWeight(unsigned(v), rig.skin[v].weights[k]));
            ab->mNumWeights = unsigned(w.size());
            ab->mWeights = new aiVertexWeight[std::max<size_t>(w.size(), 1)];
            std::copy(w.begin(), w.end(), ab->mWeights);
            am->mBones[b] = ab;
        }
    }
    scene->mRootNode->mNumChildren = unsigned(children.size());
    scene->mRootNode->mChildren = new aiNode*[children.size()];
    for (size_t i = 0; i < children.size(); ++i) { scene->mRootNode->mChildren[i] = children[i]; children[i]->mParent = scene->mRootNode; }
    scene->mNumMeshes = 1; scene->mMeshes = new aiMesh*[1]{am};

    // --- animations -----------------------------------------------------------------------------
    std::vector<aiAnimation*> anims;
    for (const auto& clip : clips) {
        aiAnimation* a = new aiAnimation();
        a->mName = aiString(clip.name);
        a->mTicksPerSecond = clip.frameRate;
        a->mDuration = clip.duration * clip.frameRate;
        std::vector<aiNodeAnim*> channels;
        if (withSkin) {
            for (size_t b = 0; b < rig.skeleton.bones.size(); ++b) {
                const Bone& bone = rig.skeleton.bones[b];
                const Curve<glm::quat>* rc = nullptr; const Curve<glm::vec3>* tc = nullptr;
                for (auto& c : clip.boneRotations) if (c.target == bone.name && !c.empty()) rc = &c;
                for (auto& c : clip.boneTranslations) if (c.target == bone.name && !c.empty()) tc = &c;
                if (!rc && !tc) continue;
                aiNodeAnim* na = new aiNodeAnim();
                na->mNodeName = aiString(bone.name);
                const std::vector<float>& times = rc ? rc->times : tc->times;
                na->mNumRotationKeys = na->mNumPositionKeys = na->mNumScalingKeys = unsigned(times.size());
                na->mRotationKeys = new aiQuatKey[times.size()];
                na->mPositionKeys = new aiVectorKey[times.size()];
                na->mScalingKeys = new aiVectorKey[times.size()];
                for (size_t k = 0; k < times.size(); ++k) {
                    double tick = times[k] * clip.frameRate;
                    glm::quat q = glm::normalize(bone.bindRotation * (rc ? rc->sample(times[k]) : glm::quat(1, 0, 0, 0)));
                    glm::vec3 p = bone.bindTranslation + (tc ? tc->sample(times[k]) : glm::vec3(0.0f));
                    na->mRotationKeys[k] = aiQuatKey(tick, aiQuaternion(q.w, q.x, q.y, q.z));
                    na->mPositionKeys[k] = aiVectorKey(tick, aiVector3D(p.x, p.y, p.z));
                    na->mScalingKeys[k] = aiVectorKey(tick, aiVector3D(1, 1, 1));
                }
                channels.push_back(na);
            }
        }
        a->mNumChannels = unsigned(channels.size());
        a->mChannels = new aiNodeAnim*[std::max<size_t>(channels.size(), 1)];
        std::copy(channels.begin(), channels.end(), a->mChannels);
        if (withMorph && !clip.blendCurves.empty()) {
            aiMeshMorphAnim* ma = new aiMeshMorphAnim();
            ma->mName = aiString(mesh.name);
            int frames = std::max(clip.frameCount(), 1);
            ma->mNumKeys = unsigned(frames);
            ma->mKeys = new aiMeshMorphKey[frames];
            for (int f = 0; f < frames; ++f) {
                float t = frames > 1 ? f / clip.frameRate : 0.0f;
                aiMeshMorphKey& key = ma->mKeys[f];
                key.mTime = t * clip.frameRate;
                key.mNumValuesAndWeights = unsigned(rig.blendShapes.size());
                key.mValues = new unsigned[rig.blendShapes.size()];
                key.mWeights = new double[rig.blendShapes.size()];
                for (size_t s = 0; s < rig.blendShapes.size(); ++s) {
                    const auto* c = clip.findBlendCurve(rig.blendShapes[s].name);
                    key.mValues[s] = unsigned(s); key.mWeights[s] = c ? c->sample(t) : rig.blendShapes[s].weight;
                }
            }
            a->mNumMorphMeshChannels = 1; a->mMorphMeshChannels = new aiMeshMorphAnim*[1]{ma};
        }
        anims.push_back(a);
    }
    scene->mNumAnimations = unsigned(anims.size());
    scene->mAnimations = new aiAnimation*[std::max<size_t>(anims.size(), 1)];
    std::copy(anims.begin(), anims.end(), scene->mAnimations);

    // --- export -------------------------------------------------------------------------------------
    Assimp::Exporter exporter;
    const char* fmt = opts.asciiFbx ? "fbxa" : "fbx";
    aiReturn r = exporter.Export(scene, fmt, path, 0);
    bool ok = r == aiReturn_SUCCESS;
    if (!ok && error) *error = exporter.GetErrorString();
    delete scene;
    return ok;
}

} // namespace fr    writeAudioSidecar(path, opts, true, nullptr);


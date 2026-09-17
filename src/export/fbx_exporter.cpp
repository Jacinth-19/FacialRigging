// Autodesk FBX SDK exporter. Compiled only when FR_WITH_FBX_SDK=ON.
// Creates: mesh node (with normals/UVs), skeleton nodes + FbxSkin cluster weights,
// FbxBlendShape deformer with one channel per blendshape, and one FbxAnimStack per clip
// with baked keys on blendshape DeformPercent and bone rotation/translation curves.
#include "export/exporter.h"
#include <fbxsdk.h>
#include <glm/gtc/quaternion.hpp>

namespace fr {

bool FbxExporter::available() const { return true; }

namespace {
FbxAMatrix toFbx(const glm::mat4& m) {
    FbxAMatrix r;
    for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 4; ++rr) r[c][rr] = m[c][rr];
    return r;
}
FbxVector4 eulerDeg(const glm::quat& q) {
    glm::vec3 e = glm::degrees(glm::eulerAngles(q));
    return FbxVector4(e.x, e.y, e.z);
}
} // namespace

bool FbxExporter::exportScene(const Rig& rig, const std::vector<AnimationClip>& clips, const std::string& path,
                              const ExportOptions& opts, std::string* error) {
    const Mesh& mesh = rig.mesh;
    if (mesh.positions.empty()) { if (error) *error = "empty mesh"; return false; }
    FbxManager* manager = FbxManager::Create();
    FbxIOSettings* ios = FbxIOSettings::Create(manager, IOSROOT);
    manager->SetIOSettings(ios);
    FbxScene* scene = FbxScene::Create(manager, "FaceScene");
    scene->GetGlobalSettings().SetSystemUnit(FbxSystemUnit::m);
    scene->GetGlobalSettings().SetAxisSystem(FbxAxisSystem::OpenGL);
    FbxTime::EMode timeMode = FbxTime::eFrames30;
    if (!clips.empty()) { float fr = clips.front().frameRate; if (fr > 59) timeMode = FbxTime::eFrames60; else if (fr < 25) timeMode = FbxTime::eFrames24; }
    scene->GetGlobalSettings().SetTimeMode(timeMode);

    // --- mesh --------------------------------------------------------------------
    FbxMesh* fbxMesh = FbxMesh::Create(scene, mesh.name.c_str());
    fbxMesh->InitControlPoints(int(mesh.vertexCount()));
    FbxVector4* cps = fbxMesh->GetControlPoints();
    for (size_t i = 0; i < mesh.vertexCount(); ++i) cps[i] = FbxVector4(mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z);
    auto normals = mesh.normals.size() == mesh.positions.size() ? mesh.normals : Mesh::computeNormals(mesh.positions, mesh.indices);
    FbxGeometryElementNormal* nEl = fbxMesh->CreateElementNormal();
    nEl->SetMappingMode(FbxGeometryElement::eByControlPoint); nEl->SetReferenceMode(FbxGeometryElement::eDirect);
    for (auto& n : normals) nEl->GetDirectArray().Add(FbxVector4(n.x, n.y, n.z));
    if (mesh.uvs.size() == mesh.positions.size()) {
        FbxGeometryElementUV* uvEl = fbxMesh->CreateElementUV("UVMap");
        uvEl->SetMappingMode(FbxGeometryElement::eByControlPoint); uvEl->SetReferenceMode(FbxGeometryElement::eDirect);
        for (auto& t : mesh.uvs) uvEl->GetDirectArray().Add(FbxVector2(t.x, t.y));
    }
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        fbxMesh->BeginPolygon();
        fbxMesh->AddPolygon(int(mesh.indices[i])); fbxMesh->AddPolygon(int(mesh.indices[i + 1])); fbxMesh->AddPolygon(int(mesh.indices[i + 2]));
        fbxMesh->EndPolygon();
    }
    FbxNode* meshNode = FbxNode::Create(scene, mesh.name.c_str());
    meshNode->SetNodeAttribute(fbxMesh);
    meshNode->SetShadingMode(FbxNode::eTextureShading);
    scene->GetRootNode()->AddChild(meshNode);

    // --- skeleton + skin -----------------------------------------------------------
    std::vector<FbxNode*> boneNodes;
    const bool withSkin = opts.exportSkeleton && rig.hasSkin() && !rig.skeleton.bones.empty();
    if (withSkin) {
        const auto& bones = rig.skeleton.bones;
        boneNodes.resize(bones.size());
        for (size_t b = 0; b < bones.size(); ++b) {
            FbxSkeleton* sk = FbxSkeleton::Create(scene, bones[b].name.c_str());
            sk->SetSkeletonType(bones[b].parent < 0 ? FbxSkeleton::eRoot : FbxSkeleton::eLimbNode);
            FbxNode* n = FbxNode::Create(scene, bones[b].name.c_str());
            n->SetNodeAttribute(sk);
            n->LclTranslation.Set(FbxDouble3(bones[b].bindTranslation.x, bones[b].bindTranslation.y, bones[b].bindTranslation.z));
            FbxVector4 e = eulerDeg(bones[b].bindRotation); n->LclRotation.Set(FbxDouble3(e[0], e[1], e[2]));
            boneNodes[b] = n;
            (bones[b].parent < 0 ? scene->GetRootNode() : boneNodes[bones[b].parent])->AddChild(n);
        }
        FbxSkin* skin = FbxSkin::Create(scene, "FaceSkin");
        auto bindWorld = rig.skeleton.bindWorldMatrices();
        FbxAMatrix meshXform = meshNode->EvaluateGlobalTransform();
        for (size_t b = 0; b < bones.size(); ++b) {
            FbxCluster* cl = FbxCluster::Create(scene, (bones[b].name + "_cluster").c_str());
            cl->SetLink(boneNodes[b]);
            cl->SetLinkMode(FbxCluster::eNormalize);
            for (size_t v = 0; v < rig.skin.size(); ++v)
                for (int k = 0; k < 4; ++k)
                    if (rig.skin[v].bones[k] == int(b) && rig.skin[v].weights[k] > 0.0f) cl->AddControlPointIndex(int(v), rig.skin[v].weights[k]);
            cl->SetTransformMatrix(meshXform);
            cl->SetTransformLinkMatrix(toFbx(bindWorld[b]));
            skin->AddCluster(cl);
        }
        fbxMesh->AddDeformer(skin);
        FbxPose* pose = FbxPose::Create(scene, "BindPose"); pose->SetIsBindPose(true);
        pose->Add(meshNode, meshXform);
        for (size_t b = 0; b < bones.size(); ++b) pose->Add(boneNodes[b], toFbx(bindWorld[b]));
        scene->AddPose(pose);
    }

    // --- blendshapes -----------------------------------------------------------------
    std::vector<FbxBlendShapeChannel*> channels;
    if (opts.exportBlendShapes && !rig.blendShapes.empty()) {
        FbxBlendShape* bsDeformer = FbxBlendShape::Create(scene, "FaceBlendShapes");
        for (const auto& bs : rig.blendShapes) {
            FbxBlendShapeChannel* ch = FbxBlendShapeChannel::Create(scene, bs.name.c_str());
            FbxShape* shape = FbxShape::Create(scene, bs.name.c_str());
            shape->InitControlPoints(int(mesh.vertexCount()));
            FbxVector4* sp = shape->GetControlPoints();
            auto dense = bs.dense(mesh.vertexCount());
            for (size_t i = 0; i < mesh.vertexCount(); ++i) { glm::vec3 p = mesh.positions[i] + dense[i]; sp[i] = FbxVector4(p.x, p.y, p.z); }
            ch->AddTargetShape(shape);
            ch->DeformPercent.Set(bs.weight * 100.0);
            bsDeformer->AddBlendShapeChannel(ch);
            channels.push_back(ch);
        }
        fbxMesh->AddDeformer(bsDeformer);
    }

    // --- animation -------------------------------------------------------------------
    for (const auto& clip : clips) {
        FbxAnimStack* stack = FbxAnimStack::Create(scene, clip.name.c_str());
        FbxAnimLayer* layer = FbxAnimLayer::Create(scene, "BaseLayer");
        stack->AddMember(layer);
        FbxTime end; end.SetSecondDouble(clip.duration);
        stack->SetLocalTimeSpan(FbxTimeSpan(FBXSDK_TIME_ZERO, end));
        for (size_t s = 0; s < channels.size() && s < rig.blendShapes.size(); ++s) {
            const auto* c = clip.findBlendCurve(rig.blendShapes[s].name);
            if (!c || c->empty()) continue;
            FbxAnimCurve* curve = channels[s]->DeformPercent.GetCurve(layer, true);
            curve->KeyModifyBegin();
            for (size_t k = 0; k < c->times.size(); ++k) {
                FbxTime t; t.SetSecondDouble(c->times[k]);
                int idx = curve->KeyAdd(t);
                curve->KeySet(idx, t, c->values[k] * 100.0f, FbxAnimCurveDef::eInterpolationLinear);
            }
            curve->KeyModifyEnd();
        }
        if (withSkin) {
            for (const auto& c : clip.boneRotations) {
                int b = rig.skeleton.find(c.target); if (b < 0 || c.empty()) continue;
                FbxAnimCurve* cx = boneNodes[b]->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X, true);
                FbxAnimCurve* cy = boneNodes[b]->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
                FbxAnimCurve* cz = boneNodes[b]->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z, true);
                cx->KeyModifyBegin(); cy->KeyModifyBegin(); cz->KeyModifyBegin();
                for (size_t k = 0; k < c.times.size(); ++k) {
                    FbxTime t; t.SetSecondDouble(c.times[k]);
                    FbxVector4 e = eulerDeg(rig.skeleton.bones[b].bindRotation * c.values[k]);
                    cx->KeySet(cx->KeyAdd(t), t, float(e[0]), FbxAnimCurveDef::eInterpolationLinear);
                    cy->KeySet(cy->KeyAdd(t), t, float(e[1]), FbxAnimCurveDef::eInterpolationLinear);
                    cz->KeySet(cz->KeyAdd(t), t, float(e[2]), FbxAnimCurveDef::eInterpolationLinear);
                }
                cx->KeyModifyEnd(); cy->KeyModifyEnd(); cz->KeyModifyEnd();
            }
            for (const auto& c : clip.boneTranslations) {
                int b = rig.skeleton.find(c.target); if (b < 0 || c.empty()) continue;
                FbxAnimCurve* cx = boneNodes[b]->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X, true);
                FbxAnimCurve* cy = boneNodes[b]->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y, true);
                FbxAnimCurve* cz = boneNodes[b]->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z, true);
                cx->KeyModifyBegin(); cy->KeyModifyBegin(); cz->KeyModifyBegin();
                for (size_t k = 0; k < c.times.size(); ++k) {
                    FbxTime t; t.SetSecondDouble(c.times[k]);
                    glm::vec3 p = rig.skeleton.bones[b].bindTranslation + c.values[k];
                    cx->KeySet(cx->KeyAdd(t), t, p.x, FbxAnimCurveDef::eInterpolationLinear);
                    cy->KeySet(cy->KeyAdd(t), t, p.y, FbxAnimCurveDef::eInterpolationLinear);
                    cz->KeySet(cz->KeyAdd(t), t, p.z, FbxAnimCurveDef::eInterpolationLinear);
                }
                cx->KeyModifyEnd(); cy->KeyModifyEnd(); cz->KeyModifyEnd();
            }
        }
    }

    // --- export ------------------------------------------------------------------------
    int format = -1;
    if (opts.asciiFbx) {
        FbxIOPluginRegistry* reg = manager->GetIOPluginRegistry();
        for (int i = 0; i < reg->GetWriterFormatCount(); ++i)
            if (reg->WriterIsFBX(i) && std::string(reg->GetWriterFormatDescription(i)).find("ascii") != std::string::npos) { format = i; break; }
    }
    ::FbxExporter* exporter = ::FbxExporter::Create(manager, "");
    bool ok = exporter->Initialize(path.c_str(), format, manager->GetIOSettings());
    if (!ok) { if (error) *error = exporter->GetStatus().GetErrorString(); }
    else { ok = exporter->Export(scene); if (!ok && error) *error = exporter->GetStatus().GetErrorString(); }
    exporter->Destroy();
    manager->Destroy();
    return ok;
}

} // namespace fr

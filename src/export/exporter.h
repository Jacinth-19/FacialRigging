#pragma once
#include "anim/animation_clip.h"
#include "rig/rig.h"
#include <memory>
#include <string>
#include <vector>

namespace fr {

struct ExportOptions {
    bool embedBuffers = true;      ///< glTF: write a single .glb / data-URI buffer vs sidecar .bin
    bool exportSkeleton = true;
    bool exportBlendShapes = true;
    bool asciiFbx = false;
};

/// Common interface for scene exporters. Writes the bind mesh, skeleton, blendshape targets
/// and one or more baked animation clips.
class Exporter {
public:
    virtual ~Exporter() = default;
    virtual std::string formatName() const = 0;
    virtual std::string fileExtension() const = 0;
    virtual bool available() const { return true; }
    virtual bool exportScene(const Rig& rig, const std::vector<AnimationClip>& clips,
                             const std::string& path, const ExportOptions& opts, std::string* error) = 0;
};

class GltfExporter : public Exporter {
public:
    std::string formatName() const override { return "glTF 2.0"; }
    std::string fileExtension() const override { return ".glb"; }
    bool exportScene(const Rig&, const std::vector<AnimationClip>&, const std::string&, const ExportOptions&, std::string*) override;
};

class FbxExporter : public Exporter {
public:
    std::string formatName() const override { return "Autodesk FBX"; }
    std::string fileExtension() const override { return ".fbx"; }
    bool available() const override;
    bool exportScene(const Rig&, const std::vector<AnimationClip>&, const std::string&, const ExportOptions&, std::string*) override;
};

/// FBX through Assimp's FBX writer (binary or ASCII). Always compiled; no proprietary SDK.
class AssimpFbxExporter : public Exporter {
public:
    std::string formatName() const override { return "FBX (Assimp)"; }
    std::string fileExtension() const override { return ".fbx"; }
    bool available() const override;
    bool exportScene(const Rig&, const std::vector<AnimationClip>&, const std::string&, const ExportOptions&, std::string*) override;
};

/// Plain-JSON animation clips (".json"): blendshape curves with ARKit aliases, bone rotation /
/// translation curves. Mesh-free, so it is the lightweight interchange for retargeting and can be
/// read back with loadClipsJson (File > Import clips).
class JsonClipExporter : public Exporter {
public:
    std::string formatName() const override { return "Clip JSON"; }
    std::string fileExtension() const override { return ".json"; }
    bool exportScene(const Rig&, const std::vector<AnimationClip>&, const std::string&, const ExportOptions&, std::string*) override;
};
bool loadClipsJson(const std::string& path, std::vector<AnimationClip>& out, std::string* error = nullptr);

/// Picks an exporter by file extension (".fbx" / ".glb" / ".gltf" / ".json"). For ".fbx" the Autodesk SDK
/// exporter is preferred when compiled in, otherwise the Assimp FBX writer; glTF is the last resort.
std::unique_ptr<Exporter> makeExporterForPath(const std::string& path, std::string* note = nullptr);

} // namespace fr

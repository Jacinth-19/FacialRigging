#pragma once
#include "render/gl.h"
#include "render/shader.h"
#include "rig/rig.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace fr {

/// Uploads the rig's bind mesh + skin attributes + blendshape deltas (texture buffer) and
/// draws it with GPU skinning/morphing. Free-form (RBF) handles are CPU-evaluated, so when
/// any of them is active the renderer uploads CPU-deformed positions instead.
class MeshRenderer {
public:
    ~MeshRenderer();
    bool init(const std::string& shaderDir, std::string* log);
    void upload(const Rig& rig);                        ///< (re)upload geometry/skin/shapes
    void uploadWeights(const Rig& rig);                 ///< skin bones/weights only (weight painting)
    void draw(const Rig& rig, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos);
    bool wireframe = false;
    enum class ShadeMode { Lit = 0, Normals, BoneWeights, ShapeInfluence, Displacement };
    ShadeMode shadeMode = ShadeMode::Lit;
    int heatBone = 1;                                   ///< bone shown in BoneWeights mode
    int heatShape = -1;                                 ///< shape shown in ShapeInfluence mode (-1 = all active)
    float heatScale = 20.0f;                            ///< displacement (model units) -> heat; set from mesh size
    bool gpuDeform = true;                              ///< toggle CPU fallback for comparison
    glm::vec3 baseColor{0.86f, 0.70f, 0.62f};
    /// Last CPU-evaluated positions (valid when the CPU path was used this frame) - used for picking.
    const std::vector<glm::vec3>& cpuPositions() const { return cpuPos_; }
    bool usedCpuPath() const { return usedCpu_; }
    int vertexCount() const { return vertexCount_; }
    int indexCount() const { return indexCount_; }

private:
    Shader shader_;
    GLuint vao_ = 0, vboPos_ = 0, vboNrm_ = 0, vboBone_ = 0, vboWeight_ = 0, ebo_ = 0;
    GLuint shapeTex_ = 0; int shapeTexW_ = 4096;
    int vertexCount_ = 0, indexCount_ = 0, shapeCount_ = 0;
    std::vector<glm::vec3> cpuPos_, cpuNrm_;
    bool usedCpu_ = false;
    void destroy();
};

} // namespace fr

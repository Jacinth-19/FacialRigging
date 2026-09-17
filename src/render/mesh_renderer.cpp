#include "render/mesh_renderer.h"
#include <algorithm>

namespace fr {

MeshRenderer::~MeshRenderer() { destroy(); }

void MeshRenderer::destroy() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    GLuint bufs[] = {vboPos_, vboNrm_, vboBone_, vboWeight_, ebo_};
    glDeleteBuffers(5, bufs);
    if (shapeTex_) glDeleteTextures(1, &shapeTex_);
    vao_ = vboPos_ = vboNrm_ = vboBone_ = vboWeight_ = ebo_ = shapeTex_ = 0;
}

bool MeshRenderer::init(const std::string& dir, std::string* log) {
    return shader_.loadFromFiles(dir + "/face.vert", dir + "/face.frag", log);
}

void MeshRenderer::upload(const Rig& rig) {
    destroy();
    const Mesh& m = rig.mesh;
    vertexCount_ = int(m.vertexCount()); indexCount_ = int(m.indices.size());
    glGenVertexArrays(1, &vao_); glBindVertexArray(vao_);
    glGenBuffers(1, &vboPos_); glBindBuffer(GL_ARRAY_BUFFER, vboPos_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m.positions.size() * sizeof(glm::vec3)), m.positions.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glGenBuffers(1, &vboNrm_); glBindBuffer(GL_ARRAY_BUFFER, vboNrm_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m.normals.size() * sizeof(glm::vec3)), m.normals.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    std::vector<glm::ivec4> bones(m.vertexCount(), glm::ivec4(0));
    std::vector<glm::vec4> weights(m.vertexCount(), glm::vec4(1, 0, 0, 0));
    if (rig.hasSkin()) for (size_t i = 0; i < m.vertexCount(); ++i) { bones[i] = rig.skin[i].bones; weights[i] = rig.skin[i].weights; }
    glGenBuffers(1, &vboBone_); glBindBuffer(GL_ARRAY_BUFFER, vboBone_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(bones.size() * sizeof(glm::ivec4)), bones.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(2); glVertexAttribIPointer(2, 4, GL_INT, 0, nullptr);
    glGenBuffers(1, &vboWeight_); glBindBuffer(GL_ARRAY_BUFFER, vboWeight_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(weights.size() * sizeof(glm::vec4)), weights.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    glGenBuffers(1, &ebo_); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(m.indices.size() * sizeof(uint32_t)), m.indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    // Blendshape deltas: dense [shape][vertex] vec3 in a texture buffer
    shapeCount_ = int(std::min<size_t>(rig.blendShapes.size(), 32));
    std::vector<glm::vec3> deltas(size_t(shapeCount_) * m.vertexCount(), glm::vec3(0.0f));
    for (int s = 0; s < shapeCount_; ++s) {
        auto d = rig.blendShapes[s].dense(m.vertexCount());
        std::copy(d.begin(), d.end(), deltas.begin() + size_t(s) * m.vertexCount());
    }
    // Stored in a 2D RGB32F texture (texelFetch) so the same path works on GL 3.3 and ES 3.0.
    GLint maxTex = 4096; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    shapeTexW_ = std::min<int>(4096, maxTex);
    size_t texels = std::max<size_t>(deltas.size(), 1);
    int rows = int((texels + size_t(shapeTexW_) - 1) / size_t(shapeTexW_));
    deltas.resize(size_t(shapeTexW_) * size_t(rows), glm::vec3(0.0f));
    glGenTextures(1, &shapeTex_); glBindTexture(GL_TEXTURE_2D, shapeTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, shapeTexW_, rows, 0, GL_RGB, GL_FLOAT, deltas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    cpuPos_ = m.positions; cpuNrm_ = m.normals;
}

void MeshRenderer::draw(const Rig& rig, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos) {
    if (!vao_) return;
    bool anyFreeForm = false;
    for (auto& c : rig.controlPoints) if (c.binding == BindingType::FreeForm && glm::dot(c.offset, c.offset) > 1e-12f) anyFreeForm = true;
    usedCpu_ = !gpuDeform || anyFreeForm || rig.skeleton.bones.size() > 32 || !rig.skinFirst;
    shader_.use();
    shader_.set("u_Model", glm::mat4(1.0f)); shader_.set("u_View", view); shader_.set("u_Proj", proj);
    shader_.set("u_CameraPos", camPos); shader_.set("u_BaseColor", baseColor);
    shader_.set("u_VertexCount", vertexCount_);
    if (usedCpu_) {
        rig.evaluate(cpuPos_);
        cpuNrm_ = Mesh::computeNormals(cpuPos_, rig.mesh.indices);
        glBindBuffer(GL_ARRAY_BUFFER, vboPos_); glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(cpuPos_.size() * sizeof(glm::vec3)), cpuPos_.data());
        glBindBuffer(GL_ARRAY_BUFFER, vboNrm_); glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(cpuNrm_.size() * sizeof(glm::vec3)), cpuNrm_.data());
        shader_.set("u_UseCpuPositions", 1); shader_.set("u_BoneCount", 0); shader_.set("u_ShapeCount", 0);
    } else {
        // restore bind positions if a previous frame used the CPU path
        if (cpuPos_ != rig.mesh.positions) {
            cpuPos_ = rig.mesh.positions; cpuNrm_ = rig.mesh.normals;
            glBindBuffer(GL_ARRAY_BUFFER, vboPos_); glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(cpuPos_.size() * sizeof(glm::vec3)), cpuPos_.data());
            glBindBuffer(GL_ARRAY_BUFFER, vboNrm_); glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(cpuNrm_.size() * sizeof(glm::vec3)), cpuNrm_.data());
        }
        shader_.set("u_UseCpuPositions", 0);
        std::vector<glm::mat4> mats = rig.hasSkin() ? rig.skeleton.skinningMatrices() : std::vector<glm::mat4>{};
        shader_.set("u_BoneCount", int(mats.size()));
        if (!mats.empty()) shader_.set("u_BoneMatrices", mats);
        std::vector<float> w = rig.blendWeights(); w.resize(size_t(shapeCount_));
        shader_.set("u_ShapeCount", shapeCount_);
        if (shapeCount_) shader_.set("u_BlendWeights", w);
    }
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, shapeTex_); shader_.set("u_ShapeDeltas", 0); shader_.set("u_ShapeTexWidth", shapeTexW_);
    glBindVertexArray(vao_);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glEnable(GL_CULL_FACE);
    shader_.set("u_Wireframe", 0);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    if (wireframe && glPolygonMode) { // glPolygonMode does not exist on OpenGL ES
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glEnable(GL_POLYGON_OFFSET_LINE); glPolygonOffset(-1.0f, -1.0f);
        shader_.set("u_Wireframe", 1);
        glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glDisable(GL_POLYGON_OFFSET_LINE); glDisable(GL_BLEND);
    }
    glBindVertexArray(0);
}

} // namespace fr

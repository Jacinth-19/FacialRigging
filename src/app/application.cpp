#include "app/application.h"
#include "core/raycast.h"
#include "render/gl.h"
#include "ui/panels.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstdlib>

namespace fr {

bool Application::initWindow() {
    glfwSetErrorCallback([](int code, const char* desc) { std::fprintf(stderr, "GLFW error %d: %s\n", code, desc); });
    if (opts_.headless) {
        // No display: GLFW null platform + EGL pbuffer surface (patched GLFW), driven by whatever
        // libEGL/libGLESv2 is on the library path (SwiftShader, Mesa llvmpipe, ANGLE...).
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
        opts_.useGLES = true;
    }
    if (!glfwInit()) return false;
    if (opts_.useGLES) {
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    } else {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
        glfwWindowHint(GLFW_SAMPLES, 4);
    }
    window_ = glfwCreateWindow(opts_.width, opts_.height, "FacialRigging", nullptr, nullptr);
    if (!window_) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(opts_.headless ? 0 : 1);
    if (!loadGL(reinterpret_cast<void* (*)(const char*)>(glfwGetProcAddress))) { std::fprintf(stderr, "Failed to load OpenGL functions\n"); return false; }
    std::printf("OpenGL %s | %s | GLSL %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER), glGetString(GL_SHADING_LANGUAGE_VERSION));
    glfwSetWindowUserPointer(window_, this);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr; // fixed shell layout - nothing to persist
    float xs = 1.0f, ys = 1.0f; if (!opts_.headless) glfwGetWindowContentScale(window_, &xs, &ys);
    initPanels(*this, std::max(1.0f, xs) * opts_.uiScale);
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init(glIsES() ? "#version 300 es" : "#version 330 core");
    return true;
}

int Application::run() {
    if (!initWindow()) return 1;
    std::string log;
    if (!meshRenderer.init(opts_.shaderDir, &log) || !gizmos.init(opts_.shaderDir, &log)) { std::fprintf(stderr, "Shader error:\n%s", log.c_str()); return 1; }
    pipe.mapperKind = opts_.mapper == "ml" ? Pipeline::MapperKind::Ml : Pipeline::MapperKind::RuleBased;
    pipe.mlModelPath = opts_.modelPt;
    pipe.modelUpAxis = opts_.upAxis == "z" ? Pipeline::UpAxis::Z : opts_.upAxis == "y" ? Pipeline::UpAxis::Y : Pipeline::UpAxis::Auto;
    loadModel(opts_.modelPath);
    bool wantClip = !opts_.audioPath.empty() || opts_.autoGenerate || !opts_.exportOnStart.empty() || opts_.renderFrames > 0;
    if (wantClip) loadAudio(opts_.audioPath);
    if (opts_.autoGenerate || !opts_.exportOnStart.empty() || opts_.renderFrames > 0) generate();
    if (!opts_.exportOnStart.empty()) {
        exportNow(opts_.exportOnStart, opts_.variations);
        for (auto& l : pipe.log) std::printf("[fr] %s\n", l.c_str());
        if (opts_.renderFrames <= 0) glfwSetWindowShouldClose(window_, 1);
    }
    if (opts_.live) toggleLive();
    if (opts_.startStep >= 0) setStep(opts_.startStep);
    lastFrameTime_ = glfwGetTime();
    if (opts_.renderFrames > 0) {
        // Offscreen proof-of-render: step through the clip deterministically and dump frames.
        playing = false;
        for (int i = 0; i < opts_.renderFrames; ++i) {
            playTime = pipe.clip.duration > 0 ? float(i) / float(opts_.renderFrames - 1 > 0 ? opts_.renderFrames - 1 : 1) * pipe.clip.duration : 0.0f;
            if (pipe.clip.duration > 0) pipe.clip.applyTo(pipe.rig, playTime);
            glfwPollEvents(); frame();
            char buf[512]; std::snprintf(buf, sizeof buf, opts_.framePattern.c_str(), i);
            if (saveFrame(buf)) std::printf("[fr] wrote %s (t=%.2fs)\n", buf, playTime); else std::fprintf(stderr, "[fr] failed to write %s\n", buf);
        }
        glfwSetWindowShouldClose(window_, 1);
    }
    while (!glfwWindowShouldClose(window_)) { glfwPollEvents(); frame(); }
    live.stop();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(window_); glfwTerminate();
    return 0;
}

void Application::frame() {
    double now = glfwGetTime(); float dt = float(now - lastFrameTime_); lastFrameTime_ = now;
    glfwGetFramebufferSize(window_, &fbSize_.x, &fbSize_.y);
    if (fbSize_.x <= 0 || fbSize_.y <= 0) return;
    view_ = camera.view(); proj_ = camera.projection(float(fbSize_.x) / float(fbSize_.y));

    if (liveEnabled) {
        auto lf = live.poll();
        if (lf.valid) {
            liveViseme = lf.viseme;
            // Drive the rig directly from the live viseme (same mapping as the offline generator).
            LipSyncGenerator gen(pipe.lipSync);
            gen.applyVisemeToRig(liveViseme, lf.features, pipe.rig);
        }
        liveWave = live.recent(2.0);
    }
    if (playing && pipe.clip.duration > 0) {
        playTime += dt;
        if (playTime > pipe.clip.duration) { if (loop) playTime = 0.0f; else { playTime = pipe.clip.duration; playing = false; } }
        pipe.clip.applyTo(pipe.rig, playTime);
    }

    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
    handleViewportInput();
    drawPanels(*this);
    drawOverlayLabels(*this);
    ImGui::Render();

    glViewport(0, 0, fbSize_.x, fbSize_.y);
    glClearColor(0.235f, 0.235f, 0.235f, 1.0f); // viewport grey (theme::kViewportBg)
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!glIsES()) glEnable(GL_MULTISAMPLE);
    drawScene();
    drawGizmos();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

Ray Application::mouseRay() const {
    double mx, my; glfwGetCursorPos(window_, &mx, &my);
    int ww, wh; glfwGetWindowSize(window_, &ww, &wh);
    return screenPointToRay(float(mx), float(my), float(ww), float(wh), view_, proj_);
}

bool Application::project(const glm::vec3& w, glm::vec2& px) const {
    glm::vec4 c = proj_ * view_ * glm::vec4(w, 1.0f);
    if (c.w <= 0.0f) return false;
    c /= c.w;
    int ww, wh; glfwGetWindowSize(window_, &ww, &wh);
    px = glm::vec2((c.x * 0.5f + 0.5f) * ww, (1.0f - (c.y * 0.5f + 0.5f)) * wh);
    return true;
}

int Application::pickControlPoint(const Ray& r, float pixelRadius) const {
    int best = -1; float bestD = 1e9f;
    double mx, my; glfwGetCursorPos(window_, &mx, &my);
    for (size_t i = 0; i < pipe.rig.controlPoints.size(); ++i) {
        glm::vec2 px;
        if (!project(pipe.rig.controlPoints[i].currentPosition(), px)) continue;
        float d = glm::length(px - glm::vec2(mx, my));
        if (d < pixelRadius && d < bestD) { bestD = d; best = int(i); }
    }
    (void)r;
    return best;
}

void Application::handleViewportInput() {
    ImGuiIO& io = ImGui::GetIO();
    // keyboard shortcuts
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) tool = Tool::Orbit;
        if (ImGui::IsKeyPressed(ImGuiKey_W)) tool = Tool::AddPoint;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) tool = Tool::MovePoint;
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && pipe.clip.duration > 0) playing = !playing;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedPoint >= 0) { pushUndo(); pipe.rig.removeControlPoint(selectedPoint); selectedPoint = -1; }
    }
    double mx, my; glfwGetCursorPos(window_, &mx, &my);
    if ((io.WantCaptureMouse || !viewportContains(float(mx), float(my))) && !dragging_) return;
    glm::dvec2 mouse(mx, my), delta = mouse - lastMouse_; lastMouse_ = mouse;
    bool lmb = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool rmb = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    bool mmb = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    if (io.MouseWheel != 0.0f) camera.zoom(io.MouseWheel > 0 ? 0.9f : 1.1f);
    if (rmb || mmb) camera.pan(float(delta.x) / fbSize_.x, float(delta.y) / fbSize_.y);

    const std::vector<glm::vec3>* deformed = meshRenderer.usedCpuPath() ? &meshRenderer.cpuPositions() : nullptr;
    switch (tool) {
    case Tool::Orbit:
        if (lmb) camera.orbit(-float(delta.x) * 0.005f, float(delta.y) * 0.005f);
        break;
    case Tool::AddPoint:
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            Ray r = mouseRay();
            // Place on the bind-pose surface so the rest position is stable.
            auto hit = raycastMesh(r, pipe.rig.mesh, nullptr);
            if (!hit && deformed) hit = raycastMesh(r, pipe.rig.mesh, deformed);
            if (hit) {
                pushUndo();
                int id = pipe.rig.addControlPoint(hit->point);
                if (newPointBinding == BindingType::FreeForm) pipe.rig.bindFreeForm(id, newPointRadius);
                else if (newPointBinding == BindingType::BlendShape && !pipe.rig.blendShapes.empty()) pipe.rig.bindToBlendShape(id, newPointShape, hit->normal, 0.05f);
                else if (newPointBinding == BindingType::Bone && !pipe.rig.skeleton.bones.empty()) pipe.rig.bindToBone(id, int(pipe.rig.skeleton.bones.size()) - 1);
                selectedPoint = id;
                status = "Added " + pipe.rig.controlPoints[id].name;
            }
        } else if (lmb) camera.orbit(-float(delta.x) * 0.005f, float(delta.y) * 0.005f);
        break;
    case Tool::MovePoint:
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int id = pickControlPoint(mouseRay(), 14.0f);
            if (id >= 0) {
                selectedPoint = id; dragging_ = true; pushUndo();
                const auto& c = pipe.rig.controlPoints[id];
                dragPlaneN_ = -glm::normalize(glm::vec3(glm::inverse(view_)[2])); // camera-facing plane
                Ray r = mouseRay();
                float t = glm::dot(c.currentPosition() - r.origin, dragPlaneN_) / glm::dot(r.dir, dragPlaneN_);
                dragStartWorld_ = r.origin + r.dir * t; dragStartOffset_ = c.offset;
            }
        }
        if (dragging_) {
            if (!lmb) { dragging_ = false; break; }
            Ray r = mouseRay();
            float denom = glm::dot(r.dir, dragPlaneN_);
            if (std::abs(denom) > 1e-6f) {
                float t = glm::dot(dragStartWorld_ - r.origin, dragPlaneN_) / denom;
                glm::vec3 p = r.origin + r.dir * t;
                pipe.rig.moveControlPoint(selectedPoint, dragStartOffset_ + (p - dragStartWorld_));
            }
        } else if (lmb) camera.orbit(-float(delta.x) * 0.005f, float(delta.y) * 0.005f);
        break;
    }
}

void Application::drawScene() { meshRenderer.draw(pipe.rig, view_, proj_, camera.position()); }

void Application::drawGizmos() {
    const Rig& rig = pipe.rig;
    if (showBones) {
        auto W = rig.skeleton.poseWorldMatrices();
        for (size_t i = 0; i < W.size(); ++i) {
            glm::vec3 p(W[i][3]);
            if (rig.skeleton.bones[i].parent >= 0) gizmos.line(glm::vec3(W[rig.skeleton.bones[i].parent][3]), p, {0.4f, 0.8f, 1.0f, 0.9f});
            gizmos.axes(p, 0.06f, 0.8f);
            gizmos.point(p, {0.4f, 0.8f, 1.0f, 1.0f});
        }
    }
    if (showPoints) {
        for (size_t i = 0; i < rig.controlPoints.size(); ++i) {
            const auto& c = rig.controlPoints[i];
            bool sel = int(i) == selectedPoint;
            glm::vec4 col = c.binding == BindingType::Unbound ? glm::vec4(1, 0.5f, 0.2f, 1) : c.binding == BindingType::FreeForm ? glm::vec4(0.9f, 0.3f, 0.9f, 1) : c.binding == BindingType::Bone ? glm::vec4(0.3f, 0.9f, 1.0f, 1) : glm::vec4(0.3f, 1.0f, 0.4f, 1);
            if (sel) col = glm::vec4(1, 0.9f, 0.3f, 1);
            gizmos.point(c.currentPosition(), col);
            if (glm::dot(c.offset, c.offset) > 1e-10f) gizmos.line(c.restPosition, c.currentPosition(), {col.r, col.g, col.b, 0.6f});
            if (sel) {
                gizmos.axes(c.currentPosition(), 0.08f);
                if (c.binding == BindingType::FreeForm) gizmos.circle(c.restPosition, glm::normalize(camera.position() - c.restPosition), c.radius, {0.9f, 0.3f, 0.9f, 0.5f});
                if (c.binding == BindingType::BlendShape) gizmos.line(c.restPosition, c.restPosition + c.driveAxis * c.driveRange, {1, 1, 1, 0.7f});
            }
        }
    }
    gizmos.flush(proj_ * view_, 12.0f, false);
}

bool Application::saveFrame(const std::string& path) const {
    int w = fbSize_.x, h = fbSize_.y;
    if (w <= 0 || h <= 0) return false;
    std::vector<unsigned char> rgba(size_t(w) * size_t(h) * 4);
    glFinish();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<unsigned char> row(size_t(w) * 3);
    for (int y = h - 1; y >= 0; --y) { // GL rows are bottom-up
        for (int x = 0; x < w; ++x) { const unsigned char* p = &rgba[(size_t(y) * w + x) * 4]; row[x * 3] = p[0]; row[x * 3 + 1] = p[1]; row[x * 3 + 2] = p[2]; }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

void Application::toggleLive(int device) {
    if (liveEnabled) { live.stop(); liveEnabled = false; status = "Live capture stopped"; return; }
    std::string err;
    live.setMapper(pipe.makeMapper());
    if (live.start(device, 16000, &err)) { liveEnabled = true; liveError.clear(); status = "Live capture running"; }
    else { liveError = err; status = "Live capture failed: " + err; }
}

void Application::pushUndo() { undo_.push_back({pipe.rig.controlPoints, pipe.rig.blendWeights()}); if (undo_.size() > 64) undo_.erase(undo_.begin()); redo_.clear(); }
void Application::undo() {
    if (undo_.empty()) return;
    redo_.push_back({pipe.rig.controlPoints, pipe.rig.blendWeights()});
    pipe.rig.controlPoints = undo_.back().controlPoints; pipe.rig.setBlendWeights(undo_.back().blendWeights); undo_.pop_back();
    if (selectedPoint >= int(pipe.rig.controlPoints.size())) selectedPoint = -1;
}
void Application::redo() {
    if (redo_.empty()) return;
    undo_.push_back({pipe.rig.controlPoints, pipe.rig.blendWeights()});
    pipe.rig.controlPoints = redo_.back().controlPoints; pipe.rig.setBlendWeights(redo_.back().blendWeights); redo_.pop_back();
}

void Application::reuploadMesh() { meshRenderer.upload(pipe.rig); }

void Application::loadModel(const std::string& path) {
    std::string err;
    if (!pipe.loadModel(path, &err)) { status = "Model load failed: " + err; return; }
    pipe.buildDefaultRig();
    notifyModelLoaded(true); notifyRigBuilt();
    reuploadMesh();
    camera.target = 0.5f * (pipe.rig.mesh.boundsMin() + pipe.rig.mesh.boundsMax());
    camera.distance = 2.2f * glm::length(pipe.rig.mesh.boundsMax() - pipe.rig.mesh.boundsMin());
    selectedPoint = -1; undo_.clear(); redo_.clear();
    status = pipe.log.empty() ? "" : pipe.log.back();
}

void Application::loadAudio(const std::string& path) {
    std::string err;
    if (!pipe.loadAudio(path, &err)) { status = "Audio load failed: " + err; return; }
    FeatureExtractor fx; pipe.features = fx.extract(pipe.audio);
    status = pipe.log.back();
}

void Application::generate() {
    if (pipe.audio.samples.empty()) loadAudio("");
    if (pipe.generateAnimation()) { playing = true; playTime = 0.0f; status = pipe.log.back(); notifyClipGenerated(); }
}

void Application::exportNow(const std::string& path, const std::vector<std::string>& variationTexts) {
    if (pipe.clip.duration <= 0) generate();
    std::vector<Variation> vars; for (auto& t : variationTexts) vars.push_back(parseVariation(t));
    auto slash = path.find_last_of("/\\");
    if (slash != std::string::npos) { std::string cmd = "mkdir -p \"" + path.substr(0, slash) + "\""; (void)std::system(cmd.c_str()); }
    std::string ext = "glb"; auto dot = path.find_last_of('.'); if (dot != std::string::npos && dot > slash + 0) ext = path.substr(dot + 1);
    std::string err;
    auto files = pipe.exportAll(path, ext, vars, &err);
    status = files.empty() ? "Export failed: " + err : "Exported " + std::to_string(files.size()) + " file(s): " + files.front();
}

} // namespace fr

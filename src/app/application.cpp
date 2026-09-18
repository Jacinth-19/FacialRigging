#include "app/application.h"
#include "app/project.h"
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
    if (!opts_.emotion.empty()) { pipe.lipSync.emotion = opts_.emotion; pipe.lipSync.emotionAmount = opts_.emotionAmount; }
    if (!opts_.transcript.empty()) pipe.transcript = opts_.transcript;
    if (opts_.headMotion >= 0) pipe.lipSync.headMotion = opts_.headMotion;
    if (opts_.gazeMotion >= 0) pipe.lipSync.gazeMotion = opts_.gazeMotion;
    meshRenderer.shadeMode = MeshRenderer::ShadeMode(std::clamp(opts_.shadeMode, 0, 4));
    msaaSamples = opts_.msaa;
    if (!opts_.projectPath.empty() && loadProject(opts_.projectPath)) { projectLoadedOnStart = true; } else
    loadModel(opts_.modelPath);
    if (opts_.gazeYaw != 0.0f || opts_.gazePitch != 0.0f) pipe.rig.setGaze(opts_.gazeYaw, opts_.gazePitch);
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
    if (opts_.rigTab >= 0) setRigTab(opts_.rigTab);
    if (opts_.paintBone >= 0 && size_t(opts_.paintBone) < pipe.rig.skeleton.bones.size()) { tool = Tool::PaintWeights; brush.bone = opts_.paintBone; meshRenderer.shadeMode = MeshRenderer::ShadeMode::BoneWeights; meshRenderer.heatBone = brush.bone;
        if (opts_.paintDemo) {
            // scripted stroke: Add on a diagonal across the model's left cheek, symmetric, then show the cursor
            glm::vec3 lo = pipe.rig.mesh.boundsMin(), hi = pipe.rig.mesh.boundsMax(); float H = hi.y - lo.y;
            brush.mode = WeightBrushMode::Add; brush.strength = 0.6f; brush.radius = 0.07f * H; brush.symmetric = true;
            pushUndo("paint weights (demo)");
            for (int k = 0; k <= 12; ++k) {
                float u = float(k) / 12.0f;
                glm::vec3 target(0.12f * (hi.x - lo.x) + 0.1f * (hi.x - lo.x) * u, lo.y + (0.62f - 0.12f * u) * H, hi.z + 1.0f);
                auto hit = raycastMesh(Ray{target, glm::vec3(0, 0, -1)}, pipe.rig.mesh, nullptr);
                if (hit) { applyBrushDab(hit->point); brushPos = hit->point; brushNormal = hit->normal; brushHover = true; }
            }
            status = "Demo stroke painted (" + std::to_string(brushStrokeChanges) + " vertex updates)";
        }
    }
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
    destroyMsaaTarget();
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
    liveLinkTick();

    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
    handleViewportInput();
    drawPanels(*this);
    drawOverlayLabels(*this);
    ImGui::Render();

    const bool msaa = ensureMsaaTarget();
    glBindFramebuffer(GL_FRAMEBUFFER, msaa ? msaaFbo_ : 0);
    glViewport(0, 0, fbSize_.x, fbSize_.y);
    glClearColor(0.235f, 0.235f, 0.235f, 1.0f); // viewport grey (theme::kViewportBg)
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!glIsES()) glEnable(GL_MULTISAMPLE);
    drawScene();
    drawGizmos();
    if (msaa) { // resolve samples into the window
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, fbSize_.x, fbSize_.y, 0, 0, fbSize_.x, fbSize_.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

bool Application::ensureMsaaTarget() {
    if (msaaMax_ == 0) { GLint m = 0; glGetIntegerv(GL_MAX_SAMPLES, &m); msaaMax_ = std::max(1, int(m)); }
    int want = std::clamp(msaaSamples, 0, msaaMax_);
    if (want < 2) { destroyMsaaTarget(); msaaActive_ = 0; return false; }
    if (msaaFbo_ && msaaSize_ == fbSize_ && msaaActive_ == want) return true;
    destroyMsaaTarget();
    glGenFramebuffers(1, &msaaFbo_); glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
    glGenRenderbuffers(1, &msaaColor_); glBindRenderbuffer(GL_RENDERBUFFER, msaaColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, want, GL_RGBA8, fbSize_.x, fbSize_.y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColor_);
    glGenRenderbuffers(1, &msaaDepth_); glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, want, GL_DEPTH_COMPONENT24, fbSize_.x, fbSize_.y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaaDepth_);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindRenderbuffer(GL_RENDERBUFFER, 0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) { std::fprintf(stderr, "[fr] %dx MSAA framebuffer incomplete - anti-aliasing disabled\n", want); destroyMsaaTarget(); msaaSamples = 0; msaaActive_ = 0; return false; }
    msaaSize_ = fbSize_; msaaActive_ = want;
    return true;
}

void Application::destroyMsaaTarget() {
    if (msaaDepth_) glDeleteRenderbuffers(1, &msaaDepth_);
    if (msaaColor_) glDeleteRenderbuffers(1, &msaaColor_);
    if (msaaFbo_) glDeleteFramebuffers(1, &msaaFbo_);
    msaaFbo_ = msaaColor_ = msaaDepth_ = 0; msaaSize_ = glm::ivec2(0);
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
        if (ImGui::IsKeyPressed(ImGuiKey_R) && !io.KeyCtrl) tool = Tool::PaintWeights;
        if (tool == Tool::PaintWeights) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) brush.radius = std::max(0.005f, brush.radius * 0.8f);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) brush.radius = std::min(2.0f, brush.radius * 1.25f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && pipe.clip.duration > 0) playing = !playing;
        for (int k = 0; k < 5; ++k) if (ImGui::IsKeyPressed(ImGuiKey(int(ImGuiKey_1) + k))) meshRenderer.shadeMode = MeshRenderer::ShadeMode(k);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && pipe.rig.mesh.vertexCount() > 0) saveProject("out/session.frproj");
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedPoint >= 0) { pushUndo("delete handle"); pipe.rig.removeControlPoint(selectedPoint); selectedPoint = -1; }
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
                pushUndo("add handle");
                int id = pipe.rig.addControlPoint(hit->point);
                if (newPointBinding == BindingType::FreeForm) pipe.rig.bindFreeForm(id, newPointRadius);
                else if (newPointBinding == BindingType::BlendShape && !pipe.rig.blendShapes.empty()) pipe.rig.bindToBlendShape(id, newPointShape, hit->normal, 0.05f);
                else if (newPointBinding == BindingType::Bone && !pipe.rig.skeleton.bones.empty()) pipe.rig.bindToBone(id, int(pipe.rig.skeleton.bones.size()) - 1);
                selectedPoint = id;
                status = "Added " + pipe.rig.controlPoints[id].name;
            }
        } else if (lmb) camera.orbit(-float(delta.x) * 0.005f, float(delta.y) * 0.005f);
        break;
    case Tool::PaintWeights: {
        Ray r = mouseRay();
        auto hit = raycastMesh(r, pipe.rig.mesh, nullptr);
        brushHover = hit.has_value();
        if (hit) { brushPos = hit->point; brushNormal = hit->normal; }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hit) {
            pushUndo("paint weights"); dragging_ = true; brushStrokeChanges = 0; meshRenderer.shadeMode = MeshRenderer::ShadeMode::BoneWeights; meshRenderer.heatBone = brush.bone;
            applyBrushDab(hit->point); lastBrush_ = hit->point;
        } else if (dragging_) {
            if (!lmb) { dragging_ = false; char b[96]; std::snprintf(b, sizeof b, "Painted %s: %d vertex updates", pipe.rig.skeleton.bones[size_t(brush.bone)].name.c_str(), brushStrokeChanges); status = b; break; }
            if (hit) {
                // space the dabs at ~1/4 radius along the stroke so fast moves don't leave gaps
                glm::vec3 d = hit->point - lastBrush_; float len = glm::length(d); float step = std::max(brush.radius * 0.25f, 1e-4f);
                int nDabs = std::max(1, int(len / step));
                for (int k = 1; k <= nDabs; ++k) applyBrushDab(lastBrush_ + d * (float(k) / float(nDabs)));
                lastBrush_ = hit->point;
            }
        } else if (lmb && !hit) camera.orbit(-float(delta.x) * 0.005f, float(delta.y) * 0.005f);
        break; }
    case Tool::MovePoint:
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int id = pickControlPoint(mouseRay(), 14.0f);
            if (id >= 0) {
                selectedPoint = id; dragging_ = true; pushUndo("move handle");
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
    if (tool == Tool::PaintWeights && brushHover) {
        glm::vec4 col = brush.mode == WeightBrushMode::Subtract ? glm::vec4(1, 0.35f, 0.3f, 0.9f) : brush.mode == WeightBrushMode::Smooth ? glm::vec4(0.4f, 0.7f, 1, 0.9f) : glm::vec4(0.55f, 0.78f, 0.18f, 0.9f);
        gizmos.circle(brushPos, brushNormal, brush.radius, col);
        gizmos.circle(brushPos, brushNormal, brush.radius * 0.5f * (1.0f - 0.5f * brush.falloff), {col.r, col.g, col.b, 0.4f});
        gizmos.line(brushPos, brushPos + brushNormal * brush.radius * 0.5f, col);
        if (brush.symmetric && std::abs(brushPos.x) > 1e-5f) gizmos.circle(brushPos * glm::vec3(-1, 1, 1), brushNormal * glm::vec3(-1, 1, 1), brush.radius, {col.r, col.g, col.b, 0.45f});
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

size_t UndoState::bytes() const {
    size_t b = skin.size() * sizeof(VertexInfluence) + controlPoints.size() * sizeof(ControlPoint) + meshPositions.size() * sizeof(glm::vec3);
    for (const auto& bs : blendShapes) b += bs.indices.size() * (sizeof(uint32_t) + sizeof(glm::vec3));
    for (const auto& c : clip.blendCurves) b += c.values.size() * 8;
    return b + 1024;
}
namespace {
UndoState snapshot(const Pipeline& pipe, const char* label, bool meshChanged) {
    UndoState u; u.label = label; const Rig& r = pipe.rig;
    u.skeleton = r.skeleton; u.skin = r.skin; u.blendShapes = r.blendShapes; u.controlPoints = r.controlPoints; u.combinations = r.combinations; u.skinFirst = r.skinFirst; u.clip = pipe.clip;
    u.meshChanged = meshChanged; if (meshChanged) u.meshPositions = r.mesh.positions;
    return u;
}
void restore(Application& app, const UndoState& u) {
    Rig& r = app.pipe.rig;
    bool structural = u.skeleton.bones.size() != r.skeleton.bones.size() || u.blendShapes.size() != r.blendShapes.size() || u.skin.size() != r.skin.size() || u.meshChanged;
    if (u.meshChanged && u.meshPositions.size() == r.mesh.positions.size()) { r.mesh.positions = u.meshPositions; r.mesh.recomputeNormals(); }
    r.skeleton = u.skeleton; r.skin = u.skin; r.blendShapes = u.blendShapes; r.controlPoints = u.controlPoints; r.combinations = u.combinations; r.skinFirst = u.skinFirst; app.pipe.clip = u.clip;
    if (!structural) {
        // shapes' deltas may still differ (bake replaced a corrective) - compare cheaply by index counts
        for (size_t i = 0; i < u.blendShapes.size() && !structural; ++i) structural |= u.blendShapes[i].indices.size() != r.blendShapes[i].indices.size();
    }
    if (app.pipe.clip.duration > 0) app.pipe.clip.applyTo(r, app.playTime); else r.applyCombinations();
    if (structural) app.reuploadMesh(); else app.reuploadWeights();
    if (app.selectedPoint >= int(r.controlPoints.size())) app.selectedPoint = -1;
}
} // namespace

void Application::pushUndo(const char* label, bool meshChanged) {
    undo_.push_back(snapshot(pipe, label, meshChanged));
    size_t total = 0; for (const auto& u : undo_) total += u.bytes();
    const size_t budget = size_t(256) << 20;   // 256 MB of history
    while ((undo_.size() > 64 || total > budget) && undo_.size() > 1) { total -= undo_.front().bytes(); undo_.erase(undo_.begin()); }
    redo_.clear();
}
void Application::undo() {
    if (undo_.empty()) return;
    UndoState cur = snapshot(pipe, undo_.back().label.c_str(), undo_.back().meshChanged);
    redo_.push_back(std::move(cur));
    UndoState u = std::move(undo_.back()); undo_.pop_back();
    restore(*this, u); status = std::string("Undo: ") + u.label;
}
void Application::redo() {
    if (redo_.empty()) return;
    UndoState cur = snapshot(pipe, redo_.back().label.c_str(), redo_.back().meshChanged);
    undo_.push_back(std::move(cur));
    UndoState u = std::move(redo_.back()); redo_.pop_back();
    restore(*this, u); status = std::string("Redo: ") + u.label;
}
void Application::reuploadWeights() { meshRenderer.uploadWeights(pipe.rig); }
void Application::applyBrushDab(const glm::vec3& centre) {
    glm::vec3 viewDir = -glm::normalize(glm::vec3(glm::inverse(view_)[2]));
    int n = paintWeights(pipe.rig, brush, centre, viewDir, &adjacency, &mirrorMap);
    if (n > 0) { brushStrokeChanges += n; pipe.skinEdited = true; reuploadWeights(); }
}

void Application::reuploadMesh() {
    meshRenderer.upload(pipe.rig);
    if (adjacencyFor_ != pipe.rig.mesh.vertexCount() || adjacency.empty()) { adjacency.build(pipe.rig.mesh); mirrorMap.build(pipe.rig.mesh); adjacencyFor_ = pipe.rig.mesh.vertexCount(); }
    glm::vec3 e = pipe.rig.mesh.boundsMax() - pipe.rig.mesh.boundsMin(); if (brush.radius > e.y) brush.radius = e.y * 0.08f;
}

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

bool Application::saveProject(const std::string& path) {
    std::string err;
    bool ok = fr::saveProject(path, pipe, ProjectSaveOptions{}, &err);
    status = ok ? "Saved project " + path : "Save project failed: " + err;
    return ok;
}

bool Application::loadProject(const std::string& path) {
    std::string err;
    if (!fr::loadProject(path, pipe, &err)) { status = "Open project failed: " + err; return false; }
    notifyModelLoaded(true); notifyRigBuilt();
    if (!pipe.audio.samples.empty()) { FeatureExtractor fx; pipe.features = fx.extract(pipe.audio); }
    if (pipe.clip.duration > 0) notifyClipGenerated();
    reuploadMesh(); reuploadWeights();
    camera.target = 0.5f * (pipe.rig.mesh.boundsMin() + pipe.rig.mesh.boundsMax());
    camera.distance = 2.2f * glm::length(pipe.rig.mesh.boundsMax() - pipe.rig.mesh.boundsMin());
    selectedPoint = -1; undo_.clear(); redo_.clear(); playing = false; playTime = 0.0f;
    status = pipe.log.empty() ? "" : pipe.log.back();
    return true;
}

bool Application::liveLinkStart(const std::string& host, int port) {
    LiveLinkSender::Settings s; s.host = host; s.port = uint16_t(port); s.frameRate = 60.0f;
    std::string err;
    if (!liveLink_.open(s, &err)) { status = "Live Link: " + err; return false; }
    arkitMap_.build(pipe.rig, pipe.lipSync.jawBoneDegrees > 0 ? pipe.lipSync.jawBoneDegrees : 25.0f);
    liveLinkFrame_ = 0; liveLinkNext_ = 0.0;
    status = "Live Link streaming to " + host + ":" + std::to_string(port) + " (" + std::to_string(arkitMap_.mappedCount()) + "/52 ARKit shapes mapped)";
    return true;
}

void Application::liveLinkStop() { liveLink_.close(); status = "Live Link stopped after " + std::to_string(liveLink_.framesSent()) + " frames"; }

void Application::liveLinkTick() {
    if (!liveLink_.isOpen()) return;
    const double now = glfwGetTime();
    if (now < liveLinkNext_) return;
    liveLinkNext_ = now + 1.0 / double(liveLink_.settings().frameRate);
    liveLink_.send(arkitFrameFromRig(pipe.rig, arkitMap_), liveLinkFrame_++);
}

} // namespace fr

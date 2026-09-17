#include "ui/panels.h"
#include "app/application.h"
#include "audio/viseme_mapper.h"
#include <imgui.h>
#include <glm/gtc/quaternion.hpp>
#include <cstring>

namespace fr {

namespace {
char modelBuf[512] = "", audioBuf[512] = "", exportBuf[512] = "out/scene.glb", variationBuf[256] = "Increase smile; Raise eyebrows";
bool bufsInit = false;

const char* bindingName(BindingType b) {
    switch (b) { case BindingType::Bone: return "Bone"; case BindingType::BlendShape: return "BlendShape"; case BindingType::FreeForm: return "FreeForm"; default: return "Unbound"; }
}

void toolbar(Application& app) {
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    auto toolBtn = [&](const char* label, Application::Tool t, const char* tip) {
        bool active = app.tool == t;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.9f, 1.0f));
        if (ImGui::Button(label)) app.tool = t;
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };
    toolBtn("Orbit (Q)", Application::Tool::Orbit, "LMB orbit, RMB/MMB pan, wheel zoom");
    toolBtn("Add Point (W)", Application::Tool::AddPoint, "Click on the mesh to place a control point");
    toolBtn("Move Point (E)", Application::Tool::MovePoint, "Drag a control point handle to deform the face");
    ImGui::Separator(); ImGui::SameLine();
    if (ImGui::Button("Undo (Ctrl+Z)")) app.undo();
    ImGui::SameLine();
    if (ImGui::Button("Redo (Ctrl+Y)")) app.redo();
    ImGui::SameLine();
    if (ImGui::Button("Reset Pose")) { app.pushUndo(); app.pipe.rig.resetPose(); app.playing = false; app.playTime = 0; }
    ImGui::SameLine(); ImGui::Checkbox("Wireframe", &app.meshRenderer.wireframe);
    ImGui::SameLine(); ImGui::Checkbox("GPU deform", &app.meshRenderer.gpuDeform);
    ImGui::SameLine(); ImGui::TextDisabled("| %s path", app.meshRenderer.usedCpuPath() ? "CPU" : "GPU");
    ImGui::SameLine(); ImGui::TextDisabled("| %.1f fps", ImGui::GetIO().Framerate);
    ImGui::End();
}

void controlPointsPanel(Application& app) {
    Rig& rig = app.pipe.rig;
    ImGui::Begin("Control Points");
    ImGui::TextWrapped("Place handles on the mesh (Add Point), then bind each one to a bone, a blendshape or a free-form (RBF) warp. Drag with Move Point.");
    ImGui::Separator();
    ImGui::Text("New point binding:");
    int b = int(app.newPointBinding);
    ImGui::RadioButton("Free-form", &b, int(BindingType::FreeForm)); ImGui::SameLine();
    ImGui::RadioButton("BlendShape", &b, int(BindingType::BlendShape)); ImGui::SameLine();
    ImGui::RadioButton("Bone", &b, int(BindingType::Bone));
    app.newPointBinding = BindingType(b);
    if (app.newPointBinding == BindingType::FreeForm) ImGui::SliderFloat("Radius", &app.newPointRadius, 0.02f, 0.6f);
    if (app.newPointBinding == BindingType::BlendShape && !rig.blendShapes.empty()) {
        if (ImGui::BeginCombo("Shape", rig.blendShapes[std::min<size_t>(app.newPointShape, rig.blendShapes.size() - 1)].name.c_str())) {
            for (size_t i = 0; i < rig.blendShapes.size(); ++i) if (ImGui::Selectable(rig.blendShapes[i].name.c_str(), int(i) == app.newPointShape)) app.newPointShape = int(i);
            ImGui::EndCombo();
        }
    }
    ImGui::Separator();
    ImGui::Checkbox("Show points", &app.showPoints); ImGui::SameLine(); ImGui::Checkbox("Show bones", &app.showBones); ImGui::SameLine(); ImGui::Checkbox("Labels", &app.showLabels);
    ImGui::Separator();
    if (ImGui::BeginTable("cps", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, 180))) {
        ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("Binding"); ImGui::TableSetupColumn("Target"); ImGui::TableSetupColumn("Offset"); ImGui::TableHeadersRow();
        for (size_t i = 0; i < rig.controlPoints.size(); ++i) {
            const ControlPoint& c = rig.controlPoints[i];
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            ImGui::PushID(int(i));
            if (ImGui::Selectable(c.name.c_str(), app.selectedPoint == int(i), ImGuiSelectableFlags_SpanAllColumns)) app.selectedPoint = int(i);
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(bindingName(c.binding));
            ImGui::TableSetColumnIndex(2);
            if (c.binding == BindingType::Bone && c.target >= 0) ImGui::TextUnformatted(rig.skeleton.bones[c.target].name.c_str());
            else if (c.binding == BindingType::BlendShape && c.target >= 0) ImGui::TextUnformatted(rig.blendShapes[c.target].name.c_str());
            else if (c.binding == BindingType::FreeForm) ImGui::Text("r=%.2f", c.radius);
            else { ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "not rigged"); }
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", glm::length(c.offset));
        }
        ImGui::EndTable();
    }
    if (app.selectedPoint >= 0 && size_t(app.selectedPoint) < rig.controlPoints.size()) {
        ControlPoint& c = rig.controlPoints[app.selectedPoint];
        ImGui::Separator();
        char name[64]; std::strncpy(name, c.name.c_str(), sizeof name - 1); name[sizeof name - 1] = 0;
        if (ImGui::InputText("Name", name, sizeof name)) c.name = name;
        int bt = int(c.binding);
        if (ImGui::Combo("Binding", &bt, "Unbound\0Bone\0BlendShape\0FreeForm\0")) { app.pushUndo(); c.binding = BindingType(bt); c.offset = glm::vec3(0); if (c.binding != BindingType::FreeForm) c.target = 0; }
        if (c.binding == BindingType::Bone && !rig.skeleton.bones.empty()) {
            int t = std::max(c.target, 0);
            if (ImGui::BeginCombo("Bone", rig.skeleton.bones[std::min<size_t>(t, rig.skeleton.bones.size() - 1)].name.c_str())) {
                for (size_t i = 0; i < rig.skeleton.bones.size(); ++i) if (ImGui::Selectable(rig.skeleton.bones[i].name.c_str(), int(i) == t)) { app.pushUndo(); rig.bindToBone(app.selectedPoint, int(i)); }
                ImGui::EndCombo();
            }
        } else if (c.binding == BindingType::BlendShape && !rig.blendShapes.empty()) {
            int t = std::max(c.target, 0);
            if (ImGui::BeginCombo("Shape", rig.blendShapes[std::min<size_t>(t, rig.blendShapes.size() - 1)].name.c_str())) {
                for (size_t i = 0; i < rig.blendShapes.size(); ++i) if (ImGui::Selectable(rig.blendShapes[i].name.c_str(), int(i) == t)) { app.pushUndo(); rig.bindToBlendShape(app.selectedPoint, int(i), c.driveAxis, c.driveRange); }
                ImGui::EndCombo();
            }
            ImGui::DragFloat3("Drive axis", &c.driveAxis.x, 0.01f, -1, 1);
            ImGui::DragFloat("Drive range", &c.driveRange, 0.001f, 0.005f, 1.0f);
        } else if (c.binding == BindingType::FreeForm) {
            ImGui::SliderFloat("RBF radius", &c.radius, 0.02f, 0.6f);
        }
        glm::vec3 off = c.offset;
        if (ImGui::DragFloat3("Offset", &off.x, 0.002f, -0.5f, 0.5f)) rig.moveControlPoint(app.selectedPoint, off);
        if (ImGui::Button("Zero offset")) rig.moveControlPoint(app.selectedPoint, glm::vec3(0));
        ImGui::SameLine();
        if (ImGui::Button("Delete point")) { app.pushUndo(); rig.removeControlPoint(app.selectedPoint); app.selectedPoint = -1; }
    }
    ImGui::End();
}

void rigPanel(Application& app) {
    Rig& rig = app.pipe.rig;
    ImGui::Begin("Rig");
    if (ImGui::Button("Rebuild default face rig")) { app.pushUndo(); rig.buildDefaultFaceRig(); app.reuploadMesh(); app.status = "Rebuilt default rig"; }
    ImGui::SameLine(); ImGui::Checkbox("Skin first", &rig.skinFirst);
    if (ImGui::CollapsingHeader("Blendshapes", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < rig.blendShapes.size(); ++i) {
            auto& bs = rig.blendShapes[i];
            ImGui::PushID(int(i));
            if (ImGui::SliderFloat(bs.name.c_str(), &bs.weight, 0.0f, 1.0f)) rig.syncControlPointsFromRig();
            ImGui::SameLine(); ImGui::TextDisabled("(%zu v)", bs.indices.size());
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Bones", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < rig.skeleton.bones.size(); ++i) {
            Bone& b = rig.skeleton.bones[i];
            ImGui::PushID(int(i) + 1000);
            glm::vec3 e = glm::degrees(glm::eulerAngles(b.poseRotation));
            ImGui::Text("%s%s", b.name.c_str(), b.parent >= 0 ? "" : " (root)");
            if (ImGui::SliderFloat3("rot", &e.x, -45.0f, 45.0f)) { b.poseRotation = glm::quat(glm::radians(e)); }
            if (ImGui::DragFloat3("pos", &b.poseTranslation.x, 0.002f, -0.3f, 0.3f)) rig.syncControlPointsFromRig();
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void audioPanel(Application& app) {
    Pipeline& p = app.pipe;
    ImGui::Begin("Audio & Animation");
    if (!bufsInit) { std::strncpy(modelBuf, app.options().modelPath.c_str(), sizeof modelBuf - 1); std::strncpy(audioBuf, app.options().audioPath.c_str(), sizeof audioBuf - 1); bufsInit = true; }
    ImGui::InputText("Model (.obj)", modelBuf, sizeof modelBuf); ImGui::SameLine();
    if (ImGui::Button("Load##model")) app.loadModel(modelBuf);
    ImGui::SameLine(); if (ImGui::Button("Procedural")) app.loadModel("");
    ImGui::InputText("Audio (.wav)", audioBuf, sizeof audioBuf); ImGui::SameLine();
    if (ImGui::Button("Load##audio")) app.loadAudio(audioBuf);
    ImGui::SameLine(); if (ImGui::Button("Synth speech")) app.loadAudio("");
    if (!p.audio.samples.empty()) {
        ImGui::Text("%.2f s, %d Hz, %d ch, %zu feature frames", p.audio.duration(), p.audio.sampleRate, p.audio.channels, p.features.frames.size());
        // waveform + loudness plot
        static std::vector<float> wave;
        if (wave.size() != 512) { wave.assign(512, 0.0f); auto m = p.audio.mono(); for (size_t i = 0; i < 512; ++i) { size_t a = i * m.size() / 512, b = (i + 1) * m.size() / 512; float mx = 0; for (size_t k = a; k < b && k < m.size(); ++k) mx = std::max(mx, std::abs(m[k])); wave[i] = mx; } }
        ImGui::PlotHistogram("##wave", wave.data(), 512, 0, "waveform", 0.0f, 1.0f, ImVec2(-1, 60));
        if (!p.features.frames.empty()) {
            static std::vector<float> loud; loud.resize(p.features.frames.size());
            for (size_t i = 0; i < loud.size(); ++i) loud[i] = p.features.frames[i].loudness;
            ImGui::PlotLines("##loud", loud.data(), int(loud.size()), 0, "loudness", 0.0f, 1.0f, ImVec2(-1, 50));
            const auto* f = p.features.at(app.playTime);
            if (f) ImGui::Text("t=%.2fs  rms=%.3f  pitch=%.0f Hz (voicing %.2f)  centroid=%.0f Hz %s", f->time, f->rms, f->pitchHz, f->voicing, f->spectralCentroid, f->onset ? "ONSET" : "");
        }
    } else ImGui::TextDisabled("No audio loaded.");
    ImGui::Separator();
    ImGui::Text("Lip-sync settings");
    ImGui::SliderFloat("Intensity", &p.lipSync.intensity, 0.2f, 2.0f);
    ImGui::SliderFloat("Jaw from loudness", &p.lipSync.jawFromLoudness, 0.0f, 1.0f);
    ImGui::SliderFloat("Brow from pitch", &p.lipSync.browFromPitch, 0.0f, 1.0f);
    ImGui::SliderFloat("Smile bias", &p.lipSync.smileBias, 0.0f, 1.0f);
    ImGui::SliderInt("Smoothing (frames)", &p.lipSync.smoothingRadiusFrames, 0, 5);
    ImGui::SliderFloat("Frame rate", &p.lipSync.frameRate, 24.0f, 60.0f, "%.0f");
    if (ImGui::Button("Generate animation")) app.generate();
    ImGui::SameLine(); ImGui::TextDisabled("rule-based viseme mapper");
    if (p.clip.frameCount() > 1 && p.clip.duration > 0) {
        ImGui::Separator();
        ImGui::Text("Clip '%s': %d frames @ %.0f fps", p.clip.name.c_str(), p.clip.frameCount(), p.clip.frameRate);
        if (ImGui::Button(app.playing ? "Pause" : "Play")) app.playing = !app.playing;
        ImGui::SameLine(); if (ImGui::Button("Stop")) { app.playing = false; app.playTime = 0; p.clip.applyTo(p.rig, 0); }
        ImGui::SameLine(); ImGui::Checkbox("Loop", &app.loop);
        if (ImGui::SliderFloat("Time", &app.playTime, 0.0f, p.clip.duration, "%.2f s")) p.clip.applyTo(p.rig, app.playTime);
        if (auto* jaw = p.clip.findBlendCurve(shapes::JawOpen)) ImGui::PlotLines("JawOpen", jaw->values.data(), int(jaw->values.size()), 0, nullptr, 0, 1, ImVec2(-1, 40));
        if (auto* sm = p.clip.findBlendCurve(shapes::MouthSmile)) ImGui::PlotLines("MouthSmile", sm->values.data(), int(sm->values.size()), 0, nullptr, 0, 1, ImVec2(-1, 40));
        if (auto* pk = p.clip.findBlendCurve(shapes::MouthPucker)) ImGui::PlotLines("MouthPucker", pk->values.data(), int(pk->values.size()), 0, nullptr, 0, 1, ImVec2(-1, 40));
    }
    ImGui::End();
}

void exportPanel(Application& app) {
    ImGui::Begin("Export");
    ImGui::InputText("Output path", exportBuf, sizeof exportBuf);
    ImGui::TextDisabled(FR_HAVE_FBX_SDK ? "FBX SDK available: .fbx / .glb / .gltf" : ".glb / .gltf (FBX SDK not compiled in; .fbx falls back to .glb)");
    ImGui::InputText("Variations (; separated)", variationBuf, sizeof variationBuf);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("e.g. \"Increase smile; Raise eyebrows; intensity=1.4; subtle\"");
    if (ImGui::Button("Export clip (+ variations)")) {
        std::vector<std::string> vars; std::string s = variationBuf, tok; size_t pos;
        while ((pos = s.find(';')) != std::string::npos) { tok = s.substr(0, pos); s.erase(0, pos + 1); if (!tok.empty()) vars.push_back(tok); }
        if (!s.empty() && s.find_first_not_of(' ') != std::string::npos) vars.push_back(s);
        app.exportNow(exportBuf, vars);
    }
    ImGui::SameLine();
    if (ImGui::Button("Export current pose only")) { std::string err; AnimationClip c = snapshotPose(app.pipe.rig); if (app.pipe.exportClip(c, exportBuf, &err)) app.status = "Exported pose to " + std::string(exportBuf); else app.status = "Export failed: " + err; }
    ImGui::Separator();
    ImGui::TextUnformatted("Log");
    ImGui::BeginChild("log", ImVec2(0, 140), true);
    for (auto& l : app.pipe.log) ImGui::TextWrapped("%s", l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}
} // namespace

void drawPanels(Application& app) {
    toolbar(app);
    controlPointsPanel(app);
    rigPanel(app);
    audioPanel(app);
    exportPanel(app);
    if (!app.status.empty()) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 10, vp->WorkPos.y + vp->WorkSize.y - 30));
        ImGui::Begin("status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1), "%s", app.status.c_str());
        ImGui::End();
    }
}

void drawOverlayLabels(Application& app) {
    if (!app.showLabels) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const Rig& rig = app.pipe.rig;
    if (app.showPoints) for (size_t i = 0; i < rig.controlPoints.size(); ++i) {
        glm::vec2 px;
        if (app.project(rig.controlPoints[i].currentPosition(), px)) {
            ImU32 col = int(i) == app.selectedPoint ? IM_COL32(255, 230, 80, 255) : IM_COL32(230, 230, 230, 200);
            dl->AddText(ImVec2(px.x + 8, px.y - 8), col, rig.controlPoints[i].name.c_str());
        }
    }
    if (app.showBones) {
        auto W = rig.skeleton.poseWorldMatrices();
        for (size_t i = 0; i < W.size(); ++i) { glm::vec2 px; if (app.project(glm::vec3(W[i][3]), px)) dl->AddText(ImVec2(px.x + 6, px.y + 4), IM_COL32(120, 220, 255, 220), rig.skeleton.bones[i].name.c_str()); }
    }
}

} // namespace fr

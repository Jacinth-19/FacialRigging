// AccuRIG-style shell: menu bar, left step wizard, centre viewport, right property panel.
// Steps (facial-rigging flavour of AccuRIG's Load / Check Model / Body Rig / Hand Rig / Check Animation):
//   1 Load Face   2 Check Model (orientation)   3 Face Rig   4 Lip-sync   5 Check Animation / Export
#include "ui/panels.h"
#include "ui/theme.h"
#include "app/application.h"
#include "audio/viseme_mapper.h"
#include "audio/ml_viseme_mapper.h"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace fr {

namespace {
using namespace theme;

enum Step { StepLoad = 0, StepCheck, StepRig, StepLipSync, StepAnim, StepCount };
struct UiState {
    int step = StepLoad;
    bool stepDone[StepCount] = {false, false, false, false, false};
    char modelBuf[512] = "", audioBuf[512] = "", exportBuf[512] = "out/scene.fbx", variationBuf[256] = "Increase smile; Raise eyebrows";
    bool init = false;
    // check-model
    bool symmetry = true; float centreLine = 0.0f; int upAxisChoice = 0;
    // rig
    bool showParts = true;
    // export
    bool showExportDialog = false;
    std::vector<AudioDevice> devs; bool devsListed = false; int devSel = -1; std::string devErr;
    Fonts fonts; float scale = 1.0f;
};
UiState ui;

const char* bindingName(BindingType b) {
    switch (b) { case BindingType::Bone: return "Bone"; case BindingType::BlendShape: return "BlendShape"; case BindingType::FreeForm: return "FreeForm"; default: return "Unbound"; }
}

float S() { return ui.scale; }

// ------------------------------------------------------------------------------------------ menu
void menuBar(Application& app) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * S(), 5 * S()));
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Load Face Model...")) ui.step = StepLoad;
            if (ImGui::MenuItem("Load ICT-FaceKit head")) { std::strncpy(ui.modelBuf, (app.options().assetDir + "/models/ict_face/ict_face.obj").c_str(), sizeof ui.modelBuf - 1); app.loadModel(ui.modelBuf); ui.stepDone[StepLoad] = true; ui.step = StepCheck; }
            if (ImGui::MenuItem("Load procedural head")) { ui.modelBuf[0] = 0; app.loadModel(""); ui.stepDone[StepLoad] = true; ui.step = StepCheck; }
            ImGui::Separator();
            if (ImGui::MenuItem("Export...", nullptr, false, app.pipe.rig.mesh.vertexCount() > 0)) ui.showExportDialog = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) app.undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) app.redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Pose")) { app.pushUndo(); app.pipe.rig.resetPose(); app.playing = false; app.playTime = 0; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Mode")) {
            if (ImGui::MenuItem("Orbit", "Q", app.tool == Application::Tool::Orbit)) app.tool = Application::Tool::Orbit;
            if (ImGui::MenuItem("Add Control Point", "W", app.tool == Application::Tool::AddPoint)) app.tool = Application::Tool::AddPoint;
            if (ImGui::MenuItem("Move Control Point", "E", app.tool == Application::Tool::MovePoint)) app.tool = Application::Tool::MovePoint;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Wireframe", nullptr, &app.meshRenderer.wireframe);
            ImGui::MenuItem("Control points", nullptr, &app.showPoints);
            ImGui::MenuItem("Bones", nullptr, &app.showBones);
            ImGui::MenuItem("Labels", nullptr, &app.showLabels);
            ImGui::MenuItem("GPU deformation", nullptr, &app.meshRenderer.gpuDeform);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Q/W/E tools  -  Space play  -  Ctrl+Z undo", nullptr, false, false);
            ImGui::MenuItem("LMB orbit  -  RMB/MMB pan  -  wheel zoom", nullptr, false, false);
            ImGui::EndMenu();
        }
        // right side: status dot like AccuRIG's "Online"
        const char* mode = app.liveEnabled ? "Live" : (app.playing ? "Playing" : "Ready");
        float tw = ImGui::CalcTextSize(mode).x + 40 * S();
        ImGui::SameLine(ImGui::GetWindowWidth() - tw - 90 * S());
        ImGui::TextColored(kTextDim, "%.0f fps", ImGui::GetIO().Framerate);
        ImGui::SameLine(ImGui::GetWindowWidth() - tw);
        ImGui::TextColored(kAccent, "%s", app.liveEnabled ? ICON_MD_MIC : app.playing ? ICON_MD_PLAY_ARROW : ICON_MD_CIRCLE); ImGui::SameLine();
        ImGui::TextUnformatted(mode);
        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleVar();
}

// ------------------------------------------------------------------------------------ left column
void leftColumn(Application& app) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(kLeftWidth * S(), vp->WorkSize.y));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg);
    ImGui::Begin("##left", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();

    // logo block
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 58 * S()), ImGui::ColorConvertFloat4ToU32(kPanelAlt), 4 * S());
        dl->AddCircleFilled(ImVec2(p.x + 24 * S(), p.y + 29 * S()), 15 * S(), ImGui::ColorConvertFloat4ToU32(kAccent));
        { ImFont* iF = ui.fonts.icons; ImVec2 is = iF->CalcTextSizeA(iF->FontSize, 1000, 0, ICON_MD_FACE);
          dl->AddText(iF, iF->FontSize, ImVec2(p.x + 24 * S() - is.x * 0.5f, p.y + 29 * S() - is.y * 0.5f), ImGui::ColorConvertFloat4ToU32(kBg), ICON_MD_FACE); }
        dl->AddText(ui.fonts.small, ui.fonts.small->FontSize, ImVec2(p.x + 46 * S(), p.y + 10 * S()), ImGui::ColorConvertFloat4ToU32(kTextDim), "facial");
        dl->AddText(ui.fonts.logo, ui.fonts.logo->FontSize, ImVec2(p.x + 45 * S(), p.y + 22 * S()), ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 1)), "accu");
        float aw = ui.fonts.logo->CalcTextSizeA(ui.fonts.logo->FontSize, 1000, 0, "accu").x;
        dl->AddText(ui.fonts.logo, ui.fonts.logo->FontSize, ImVec2(p.x + 45 * S() + aw, p.y + 22 * S()), ImGui::ColorConvertFloat4ToU32(kAccent), "FACE");
        ImGui::Dummy(ImVec2(0, 66 * S()));
    }

    const bool haveMesh = app.pipe.rig.mesh.vertexCount() > 0, haveRig = !app.pipe.rig.skeleton.bones.empty(), haveClip = app.pipe.clip.duration > 0;
    struct StepDef { const char* icon; const char* title; const char* sub; bool enabled; };
    const StepDef defs[StepCount] = {
        {ICON_MD_FOLDER_OPEN, "Load Face", ui.stepDone[StepLoad] ? "Model loaded" : "OBJ / ICT-FaceKit", true},
        {ICON_MD_3D_ROTATION, "Check Model", "Orient. & Center", haveMesh},
        {ICON_MD_FACE_RETOUCHING_NATURAL, "Face Rig", "Bones, shapes, handles", haveMesh},
        {ICON_MD_RECORD_VOICE_OVER, "Lip-sync", "Audio & microphone", haveRig},
        {ICON_MD_MOVIE, "Check Animation", "Preview & export", haveClip || haveRig},
    };
    for (int i = 0; i < StepCount; ++i) {
        if (StepCard(i + 1, defs[i].icon, defs[i].title, defs[i].sub, ui.step == i, ui.stepDone[i], defs[i].enabled, ui.fonts)) ui.step = i;
    }

    // bottom promo-style info card (AccuRIG has an ad here; we show pipeline stats)
    float bottomH = 118 * S();
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - bottomH - 10 * S());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelAlt);
    ImGui::BeginChild("info", ImVec2(0, bottomH), ImGuiChildFlags_Borders);
    ImGui::PushFont(ui.fonts.bold); ImGui::TextColored(ImVec4(1, 1, 1, 1), "SCENE"); ImGui::PopFont();
    const Rig& r = app.pipe.rig;
    ImGui::TextColored(kTextDim, "%zu verts  %zu tris", r.mesh.vertexCount(), r.mesh.triangleCount());
    ImGui::TextColored(kTextDim, "%zu parts  %zu shapes  %zu bones", r.mesh.parts.size(), r.blendShapes.size(), r.skeleton.bones.size());
    ImGui::TextColored(kTextDim, "%zu control points", r.controlPoints.size());
    if (haveClip) ImGui::TextColored(kAccent, "clip %d frames @ %.0f fps", app.pipe.clip.frameCount(), app.pipe.clip.frameRate);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

// ------------------------------------------------------------------------------- right: step pages
void pageLoad(Application& app) {
    SectionLabel("Face model (.obj, optional <name>.fbs blendshapes) :");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##model", "path/to/head.obj", ui.modelBuf, sizeof ui.modelBuf);
    if (PrimaryButton(ICON_MD_FOLDER_OPEN "  Load Face Model", ImVec2(-1, 34 * S()))) { app.loadModel(ui.modelBuf); ui.stepDone[StepLoad] = app.pipe.rig.mesh.vertexCount() > 0; if (ui.stepDone[StepLoad]) ui.step = StepCheck; }
    ImGui::Spacing();
    SectionLabel("Bundled heads :");
    auto bundled = [&](const char* label, const std::string& path, const char* tip) {
        if (WideButton(label, ImVec2(-1, 30 * S()))) { std::strncpy(ui.modelBuf, path.c_str(), sizeof ui.modelBuf - 1); app.loadModel(path); ui.stepDone[StepLoad] = true; ui.step = StepCheck; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    const std::string a = app.options().assetDir;
    bundled("ICT-FaceKit head (13 parts, 53 shapes)", a + "/models/ict_face/ict_face.obj", "Separated brows, eyes, lashes, upper/lower teeth, gums, tongue + ARKit-style blendshapes");
    bundled("Max Planck scan", a + "/models/max-planck.obj", "Single-surface scan (Stanford/MPI)");
    bundled("Nefertiti bust", a + "/models/nefertiti.obj", "Single-surface scan");
    bundled("Procedural head", "", "Generated ellipsoid head - no external file needed");
    Rule();
    SectionLabel("Import up-axis :");
    int up = app.pipe.modelUpAxis == Pipeline::UpAxis::Auto ? 0 : app.pipe.modelUpAxis == Pipeline::UpAxis::Y ? 1 : 2;
    ImGui::RadioButton("Auto", &up, 0); ImGui::SameLine(); ImGui::RadioButton("Y up", &up, 1); ImGui::SameLine(); ImGui::RadioButton("Z up", &up, 2);
    app.pipe.modelUpAxis = up == 0 ? Pipeline::UpAxis::Auto : up == 1 ? Pipeline::UpAxis::Y : Pipeline::UpAxis::Z;
    if (app.pipe.rig.mesh.vertexCount() > 0) {
        Rule();
        ImGui::TextColored(kTextDim, "Loaded:"); ImGui::SameLine(); ImGui::TextWrapped("%s", app.pipe.rig.mesh.name.c_str());
        if (!app.pipe.rig.mesh.parts.empty()) {
            ImGui::TextColored(kTextDim, "Parts:"); ImGui::SameLine();
            std::string ps; for (auto& p : app.pipe.rig.mesh.parts) ps += p.name + "  "; ImGui::TextWrapped("%s", ps.c_str());
        } else ImGui::TextColored(kWarn, "No named parts (single surface) - teeth/eyes will not be separated.");
        if (!app.pipe.authoredShapesPath.empty()) ImGui::TextColored(kAccent, "%zu authored blendshapes found", app.pipe.authoredShapes().size());
    }
}

void pageCheck(Application& app) {
    Pipeline& p = app.pipe;
    // Reference thumbnail box like AccuRIG's "Place the center line on the middle of the hips".
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.09f, 0.09f, 1));
    ImGui::BeginChild("ref", ImVec2(0, 150 * S()), ImGuiChildFlags_Borders);
    ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p0 = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
    ImVec2 c(p0.x + sz.x * 0.5f, p0.y + sz.y * 0.5f - 8 * S());
    ImU32 line = ImGui::ColorConvertFloat4ToU32(ImVec4(0.75f, 0.75f, 0.75f, 1));
    dl->AddEllipse(c, ImVec2(34 * S(), 44 * S()), line, 0, 0, 1.5f * S());                       // head outline
    dl->AddLine(ImVec2(c.x, c.y - 60 * S()), ImVec2(c.x, c.y + 62 * S()), ImGui::ColorConvertFloat4ToU32(kAccent), 1.5f * S()); // centre line
    dl->AddCircle(ImVec2(c.x - 13 * S(), c.y - 8 * S()), 5 * S(), line, 0, 1.2f * S()); dl->AddCircle(ImVec2(c.x + 13 * S(), c.y - 8 * S()), 5 * S(), line, 0, 1.2f * S());
    dl->AddLine(ImVec2(c.x - 12 * S(), c.y + 18 * S()), ImVec2(c.x + 12 * S(), c.y + 18 * S()), line, 1.5f * S());
    dl->AddLine(ImVec2(c.x + 60 * S(), c.y), ImVec2(c.x + 90 * S(), c.y), line, 1.2f * S()); dl->AddText(ImVec2(c.x + 62 * S(), c.y - 16 * S()), line, "+Z front");
    dl->AddLine(ImVec2(c.x - 90 * S(), c.y + 40 * S()), ImVec2(c.x - 90 * S(), c.y - 20 * S()), line, 1.2f * S()); dl->AddText(ImVec2(c.x - 105 * S(), c.y - 36 * S()), line, "+Y up");
    ImGui::SetCursorPos(ImVec2(8 * S(), sz.y - 24 * S()));
    ImGui::TextColored(kText, "Face toward +Z, up +Y, line on the nose");
    ImGui::EndChild(); ImGui::PopStyleColor();

    SectionLabel("Rotate Model :");
    auto rot = [&](const char* label, const glm::mat3& R, const char* tip) {
        if (ImGui::Button(label, ImVec2(92 * S(), 30 * S()))) { p.transformModel(R); app.reuploadMesh(); app.pushUndo(); ui.stepDone[StepRig] = false; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };
    const float h = glm::half_pi<float>();
    rot(ICON_MD_ROTATE_RIGHT " X +90", glm::mat3(glm::rotate(glm::mat4(1), h, glm::vec3(1, 0, 0))), "Rotate +90 deg about X (Z-up -> Y-up)");
    rot(ICON_MD_ROTATE_LEFT " X -90", glm::mat3(glm::rotate(glm::mat4(1), -h, glm::vec3(1, 0, 0))), "Rotate -90 deg about X");
    rot(ICON_MD_ROTATE_RIGHT " Y +90", glm::mat3(glm::rotate(glm::mat4(1), h, glm::vec3(0, 1, 0))), "Turn the head a quarter to the left");
    rot(ICON_MD_SWAP_VERT " Y 180", glm::mat3(glm::rotate(glm::mat4(1), glm::pi<float>(), glm::vec3(0, 1, 0))), "Face was looking away: turn around");
    ImGui::NewLine();
    rot(ICON_MD_ROTATE_RIGHT " Z +90", glm::mat3(glm::rotate(glm::mat4(1), h, glm::vec3(0, 0, 1))), "Roll +90 deg");
    rot(ICON_MD_ROTATE_LEFT " Z -90", glm::mat3(glm::rotate(glm::mat4(1), -h, glm::vec3(0, 0, 1))), "Roll -90 deg");
    rot(ICON_MD_FLIP, glm::mat3(glm::scale(glm::mat4(1), glm::vec3(-1, 1, 1))), "Mirror left/right");
    ImGui::NewLine();
    if (WideButton(ICON_MD_AUTO_FIX_HIGH "  Auto-detect orientation", ImVec2(-1, 28 * S()))) {
        if (p.rig.mesh.looksZUp()) { p.transformModel(glm::mat3(glm::rotate(glm::mat4(1), -h, glm::vec3(1, 0, 0)))); app.reuploadMesh(); app.status = "Auto: rotated Z-up model to Y-up"; }
        else app.status = "Auto: model already looks Y-up";
    }
    SectionLabel("Center Line :");
    ImGui::SetNextItemWidth(-60 * S());
    if (ImGui::SliderFloat("##centre", &ui.centreLine, -0.25f, 0.25f, "%.3f")) {}
    ImGui::SameLine(); if (ImGui::Button("Apply", ImVec2(-1, 0))) { p.translateModel(glm::vec3(-ui.centreLine, 0, 0)); ui.centreLine = 0; app.reuploadMesh(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shift the model so the nose sits on x = 0 (mirror symmetry for L/R shapes)");
    if (ImGui::Checkbox("Force Symmetry", &ui.symmetry)) p.rig.forceSymmetry = ui.symmetry;
    p.rig.forceSymmetry = ui.symmetry;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dragging a left/right control point also moves its mirrored partner (BrowL <-> BrowR, MouthCornerL <-> MouthCornerR, ...)");
    Rule();
    // orientation diagnostics
    const Mesh& m = p.rig.mesh;
    if (m.vertexCount()) {
        glm::vec3 lo = m.boundsMin(), hi = m.boundsMax(), e = hi - lo;
        bool tall = e.y >= e.x * 0.9f && e.y >= e.z * 0.9f;
        // "front" heuristic: more surface area on +Z than -Z half (a face is convex forward)
        double zsum = 0; for (auto& v : m.positions) zsum += v.z; bool front = zsum / double(m.vertexCount()) > -0.02;
        ImGui::TextColored(kTextDim, "Extents  x %.2f  y %.2f  z %.2f", e.x, e.y, e.z);
        bool cx = std::abs(0.5f * (lo.x + hi.x)) < 0.02f;
        ImGui::TextColored(tall ? kAccent : kWarn, "%s  vertical axis is Y", tall ? ICON_MD_CHECK_CIRCLE : ICON_MD_WARNING);
        ImGui::TextColored(front ? kAccent : kWarn, "%s  face points toward +Z", front ? ICON_MD_CHECK_CIRCLE : ICON_MD_WARNING);
        ImGui::TextColored(cx ? kAccent : kWarn, "%s  centred on x = 0", cx ? ICON_MD_CHECK_CIRCLE : ICON_MD_WARNING);
    }
    ImGui::Dummy(ImVec2(0, 6 * S()));
    if (PrimaryButton(ICON_MD_FACE_RETOUCHING_NATURAL "  Rig Face", ImVec2(-1, 36 * S()), m.vertexCount() > 0)) {
        app.pushUndo(); p.buildDefaultRig(); app.reuploadMesh(); app.status = p.log.back();
        ui.stepDone[StepCheck] = ui.stepDone[StepRig] = true; ui.step = StepRig;
    }
}

void controlPointEditor(Application& app) {
    Rig& rig = app.pipe.rig;
    SectionLabel("Control points :");
    int b = int(app.newPointBinding);
    ImGui::RadioButton("Free-form", &b, int(BindingType::FreeForm)); ImGui::SameLine();
    ImGui::RadioButton("Shape", &b, int(BindingType::BlendShape)); ImGui::SameLine();
    ImGui::RadioButton("Bone", &b, int(BindingType::Bone));
    app.newPointBinding = BindingType(b);
    ImGui::SetNextItemWidth(-1);
    if (app.newPointBinding == BindingType::FreeForm) ImGui::SliderFloat("##radius", &app.newPointRadius, 0.02f, 0.6f, "RBF radius %.2f");
    if (app.newPointBinding == BindingType::BlendShape && !rig.blendShapes.empty()) {
        if (ImGui::BeginCombo("##shape", rig.blendShapes[std::min<size_t>(app.newPointShape, rig.blendShapes.size() - 1)].name.c_str())) {
            for (size_t i = 0; i < rig.blendShapes.size(); ++i) if (ImGui::Selectable(rig.blendShapes[i].name.c_str(), int(i) == app.newPointShape)) app.newPointShape = int(i);
            ImGui::EndCombo();
        }
    }
    auto toolBtn = [&](const char* label, Application::Tool t) {
        bool active = app.tool == t;
        if (active) { ImGui::PushStyleColor(ImGuiCol_Button, kAccent); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.08f, 0.02f, 1)); }
        if (ImGui::Button(label, ImVec2(0, 28 * S()))) app.tool = t;
        if (active) ImGui::PopStyleColor(2);
        ImGui::SameLine();
    };
    if (ImGui::Checkbox("Symmetry", &ui.symmetry)) {} rig.forceSymmetry = ui.symmetry; ImGui::SameLine();
    toolBtn(ICON_MD_3D_ROTATION, Application::Tool::Orbit); toolBtn(ICON_MD_ADD_LOCATION_ALT, Application::Tool::AddPoint); toolBtn(ICON_MD_OPEN_WITH, Application::Tool::MovePoint); ImGui::TextColored(kTextDim, "%s", app.tool == Application::Tool::Orbit ? "Orbit (Q)" : app.tool == Application::Tool::AddPoint ? "Add point (W)" : "Move point (E)"); ImGui::NewLine();
    if (ImGui::BeginTable("cps", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH, ImVec2(0, 150 * S()))) {
        ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("Binding"); ImGui::TableSetupColumn("Target"); ImGui::TableHeadersRow();
        for (size_t i = 0; i < rig.controlPoints.size(); ++i) {
            const ControlPoint& c = rig.controlPoints[i];
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::PushID(int(i));
            if (ImGui::Selectable(c.name.c_str(), app.selectedPoint == int(i), ImGuiSelectableFlags_SpanAllColumns)) app.selectedPoint = int(i);
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(bindingName(c.binding));
            ImGui::TableSetColumnIndex(2);
            if (c.binding == BindingType::Bone && c.target >= 0) ImGui::TextUnformatted(rig.skeleton.bones[c.target].name.c_str());
            else if (c.binding == BindingType::BlendShape && c.target >= 0) ImGui::TextUnformatted(rig.blendShapes[c.target].name.c_str());
            else if (c.binding == BindingType::FreeForm) ImGui::Text("r=%.2f", c.radius);
            else ImGui::TextColored(kWarn, "not rigged");
        }
        ImGui::EndTable();
    }
    if (app.selectedPoint >= 0 && size_t(app.selectedPoint) < rig.controlPoints.size()) {
        ControlPoint& c = rig.controlPoints[app.selectedPoint];
        char name[64]; std::strncpy(name, c.name.c_str(), sizeof name - 1); name[sizeof name - 1] = 0;
        ImGui::SetNextItemWidth(-1); if (ImGui::InputText("##name", name, sizeof name)) c.name = name;
        int bt = int(c.binding);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##binding", &bt, "Unbound\0Bone\0BlendShape\0FreeForm\0")) { app.pushUndo(); c.binding = BindingType(bt); c.offset = glm::vec3(0); if (c.binding != BindingType::FreeForm) c.target = 0; }
        ImGui::SetNextItemWidth(-1);
        if (c.binding == BindingType::Bone && !rig.skeleton.bones.empty()) {
            int t = std::max(c.target, 0);
            if (ImGui::BeginCombo("##bone", rig.skeleton.bones[std::min<size_t>(t, rig.skeleton.bones.size() - 1)].name.c_str())) {
                for (size_t i = 0; i < rig.skeleton.bones.size(); ++i) if (ImGui::Selectable(rig.skeleton.bones[i].name.c_str(), int(i) == t)) { app.pushUndo(); rig.bindToBone(app.selectedPoint, int(i)); }
                ImGui::EndCombo();
            }
        } else if (c.binding == BindingType::BlendShape && !rig.blendShapes.empty()) {
            int t = std::max(c.target, 0);
            if (ImGui::BeginCombo("##bs", rig.blendShapes[std::min<size_t>(t, rig.blendShapes.size() - 1)].name.c_str())) {
                for (size_t i = 0; i < rig.blendShapes.size(); ++i) if (ImGui::Selectable(rig.blendShapes[i].name.c_str(), int(i) == t)) { app.pushUndo(); rig.bindToBlendShape(app.selectedPoint, int(i), c.driveAxis, c.driveRange); }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(-1); ImGui::DragFloat3("##axis", &c.driveAxis.x, 0.01f, -1, 1, "axis %.2f");
            ImGui::SetNextItemWidth(-1); ImGui::DragFloat("##range", &c.driveRange, 0.001f, 0.005f, 1.0f, "range %.3f");
        } else if (c.binding == BindingType::FreeForm) { ImGui::SliderFloat("##rbf", &c.radius, 0.02f, 0.6f, "RBF radius %.2f"); }
        glm::vec3 off = c.offset; ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##off", &off.x, 0.002f, -0.5f, 0.5f, "%.3f")) rig.moveControlPoint(app.selectedPoint, off);
        if (WideButton("Zero offset", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4 * S(), 26 * S()))) rig.moveControlPoint(app.selectedPoint, glm::vec3(0));
        ImGui::SameLine();
        if (WideButton("Delete", ImVec2(-1, 26 * S()))) { app.pushUndo(); rig.removeControlPoint(app.selectedPoint); app.selectedPoint = -1; }
    }
}

void pageRig(Application& app) {
    Rig& rig = app.pipe.rig;
    if (rig.skeleton.bones.empty()) {
        ImGui::TextWrapped("The face is not rigged yet. Check the orientation in step 2, then press Rig Face.");
        if (PrimaryButton("Rig Face", ImVec2(-1, 36 * S()))) { app.pushUndo(); app.pipe.buildDefaultRig(); app.reuploadMesh(); ui.stepDone[StepCheck] = ui.stepDone[StepRig] = true; }
        return;
    }
    auto parts = rig.detectParts();
    ImGui::TextColored(kTextDim, "%zu bones  %zu blendshapes  %zu control points", rig.skeleton.bones.size(), rig.blendShapes.size(), rig.controlPoints.size());
    if (parts.any()) {
        ImGui::TextColored(kAccent, "Anatomical parts bound:"); ImGui::SameLine();
        std::string s; if (parts.teethLower >= 0) s += "lower teeth/gums/tongue -> Jaw  "; if (parts.browL >= 0) s += "brows  "; if (parts.eyeL >= 0) s += "eyes";
        ImGui::TextWrapped("%s", s.c_str());
    } else ImGui::TextColored(kWarn, "Single surface: jaw skinned by height falloff.");
    if (WideButton("Rebuild default face rig", ImVec2(-1, 28 * S()))) { app.pushUndo(); app.pipe.buildDefaultRig(); app.reuploadMesh(); app.status = "Rebuilt default rig"; }
    ImGui::SameLine(0, 0);
    ImGui::Checkbox("Skin first", &rig.skinFirst);

    if (ImGui::BeginTabBar("rigtabs")) {
        if (ImGui::BeginTabItem("Handles")) { controlPointEditor(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Blendshapes")) {
            static char filter[64] = "";
            ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##filter", "filter (e.g. mouth, brow)", filter, sizeof filter);
            ImGui::BeginChild("bs", ImVec2(0, 300 * S()));
            for (size_t i = 0; i < rig.blendShapes.size(); ++i) {
                auto& bs = rig.blendShapes[i];
                if (filter[0] && bs.name.find(filter) == std::string::npos) continue;
                ImGui::PushID(int(i));
                if (i == std::size(shapes::All) && rig.blendShapes.size() > std::size(shapes::All)) ImGui::SeparatorText("Authored (ARKit / ICT)");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##w", &bs.weight, 0.0f, 1.0f, bs.name.c_str())) rig.syncControlPointsFromRig();
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (WideButton("Reset all weights", ImVec2(-1, 26 * S()))) { app.pushUndo(); rig.resetPose(); }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Bones")) {
            for (size_t i = 0; i < rig.skeleton.bones.size(); ++i) {
                Bone& b = rig.skeleton.bones[i]; ImGui::PushID(int(i) + 1000);
                glm::vec3 e = glm::degrees(glm::eulerAngles(b.poseRotation));
                ImGui::TextColored(kText, "%s%s", b.name.c_str(), b.parent >= 0 ? "" : "  (root)");
                ImGui::SetNextItemWidth(-1); if (ImGui::SliderFloat3("##rot", &e.x, -45.0f, 45.0f, "%.1f deg")) b.poseRotation = glm::quat(glm::radians(e));
                ImGui::SetNextItemWidth(-1); if (ImGui::DragFloat3("##pos", &b.poseTranslation.x, 0.002f, -0.3f, 0.3f, "%.3f")) rig.syncControlPointsFromRig();
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Parts")) {
            const Mesh& m = rig.mesh;
            if (m.parts.empty()) ImGui::TextColored(kWarn, "This model has no named parts.");
            if (ImGui::BeginTable("parts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("Part"); ImGui::TableSetupColumn("Tris"); ImGui::TableSetupColumn("Follows"); ImGui::TableHeadersRow();
                for (size_t i = 0; i < m.parts.size(); ++i) {
                    auto pv = m.partVertices(int(i)); int jaw = 0;
                    for (uint32_t v : pv) if (rig.skin[v].bones[0] == 1 && rig.skin[v].weights[0] > 0.99f) ++jaw;
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(m.parts[i].name.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%u", m.parts[i].indexCount / 3);
                    ImGui::TableSetColumnIndex(2);
                    if (jaw == int(pv.size())) ImGui::TextColored(kAccent, "Jaw"); else if (jaw == 0) ImGui::TextColored(kTextDim, "Head / blend"); else ImGui::TextColored(kTextDim, "mixed");
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Dummy(ImVec2(0, 4 * S()));
    if (PrimaryButton("Next: Lip-sync", ImVec2(-1, 34 * S()))) { ui.stepDone[StepRig] = true; ui.step = StepLipSync; }
}

void pageLipSync(Application& app) {
    Pipeline& p = app.pipe;
    if (ImGui::BeginTabBar("lstabs")) {
        if (ImGui::BeginTabItem("Audio file")) {
            SectionLabel("Speech audio (.wav) :");
            ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##audio", "path/to/speech.wav (empty = synthetic test speech)", ui.audioBuf, sizeof ui.audioBuf);
            if (WideButton("Load audio", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4 * S(), 28 * S()))) app.loadAudio(ui.audioBuf);
            ImGui::SameLine(); if (WideButton("Synthetic speech", ImVec2(-1, 28 * S()))) app.loadAudio("");
            if (!p.audio.samples.empty()) {
                ImGui::TextColored(kTextDim, "%.2f s   %d Hz   %d ch   %zu frames", p.audio.duration(), p.audio.sampleRate, p.audio.channels, p.features.frames.size());
                static std::vector<float> wave; static size_t waveFor = 0;
                if (waveFor != p.audio.samples.size()) { waveFor = p.audio.samples.size(); wave.assign(512, 0.0f); auto m = p.audio.mono(); for (size_t i = 0; i < 512; ++i) { size_t a = i * m.size() / 512, b2 = std::max(a + 1, (i + 1) * m.size() / 512); float mx = 0; for (size_t k = a; k < b2 && k < m.size(); ++k) mx = std::max(mx, std::abs(m[k])); wave[i] = mx; } }
                ImGui::PlotHistogram("##wave", wave.data(), 512, 0, nullptr, 0.0f, 1.0f, ImVec2(-1, 54 * S()));
                if (!p.features.frames.empty()) {
                    static std::vector<float> loud; loud.resize(p.features.frames.size());
                    for (size_t i = 0; i < loud.size(); ++i) loud[i] = p.features.frames[i].loudness;
                    ImGui::PlotLines("##loud", loud.data(), int(loud.size()), 0, nullptr, 0.0f, 1.0f, ImVec2(-1, 40 * S()));
                    if (const auto* f = p.features.at(app.playTime)) ImGui::TextColored(kTextDim, "t %.2fs  rms %.3f  pitch %.0f Hz  voicing %.2f%s", f->time, f->rms, f->pitchHz, f->voicing, f->onset ? "  ONSET" : "");
                }
            } else ImGui::TextColored(kTextDim, "No audio loaded.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Microphone")) {
            if (!ui.devsListed || WideButton("Refresh devices", ImVec2(-1, 26 * S()))) { ui.devs = LiveCapture::listInputDevices(&ui.devErr); ui.devsListed = true; }
            ImGui::TextColored(kTextDim, "%s", LiveCapture::backendInfo().c_str());
            std::string cur = ui.devSel < 0 ? "(default input)" : ui.devs[std::min<size_t>(size_t(ui.devSel), ui.devs.size() - 1)].name;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##dev", cur.c_str())) {
                if (ImGui::Selectable("(default input)", ui.devSel < 0)) ui.devSel = -1;
                for (size_t i = 0; i < ui.devs.size(); ++i) if (ImGui::Selectable(ui.devs[i].name.c_str(), ui.devSel == int(i))) ui.devSel = int(i);
                ImGui::EndCombo();
            }
            if (!ui.devErr.empty() && ui.devs.size() <= 1) ImGui::TextColored(kWarn, "%s", ui.devErr.c_str());
            if (PrimaryButton(app.liveEnabled ? ICON_MD_MIC_OFF "  Stop capture" : ICON_MD_MIC "  Start capture", ImVec2(-1, 32 * S()))) app.toggleLive(ui.devSel < 0 ? -1 : ui.devs[size_t(ui.devSel)].index);
            if (!app.liveError.empty() && !app.liveEnabled) ImGui::TextColored(kError, "%s", app.liveError.c_str());
            if (app.liveEnabled) {
                ImGui::ProgressBar(std::min(1.0f, app.live.inputLevel() * 3.0f), ImVec2(-1, 6 * S()), "");
                if (!app.liveWave.empty()) ImGui::PlotLines("##livewave", app.liveWave.data(), int(app.liveWave.size()), 0, nullptr, -1.0f, 1.0f, ImVec2(-1, 54 * S()));
                ImGui::TextColored(kAccent, "Viseme  %s", visemeName(app.liveViseme.dominant()));
                for (size_t i = 0; i < app.liveViseme.weights.size(); ++i) { ImGui::ProgressBar(app.liveViseme.weights[i], ImVec2(110 * S(), 8 * S()), ""); ImGui::SameLine(); ImGui::TextColored(kTextDim, "%s", visemeName(Viseme(i))); }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    SectionLabel("Viseme mapper :");
    int mk = p.mapperKind == Pipeline::MapperKind::Ml ? 1 : 0;
    ImGui::RadioButton("Rule-based", &mk, 0); ImGui::SameLine(); ImGui::RadioButton("Trained MLP", &mk, 1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("assets/models/viseme_mlp.frvm - trained on TIMIT phone alignments (tools/train_visemes.cpp)");
    p.mapperKind = mk ? Pipeline::MapperKind::Ml : Pipeline::MapperKind::RuleBased;
    SectionLabel("Lip-sync settings :");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##int", &p.lipSync.intensity, 0.2f, 2.0f, "Intensity  %.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##jaw", &p.lipSync.jawFromLoudness, 0.0f, 1.0f, "Jaw from loudness  %.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##brow", &p.lipSync.browFromPitch, 0.0f, 1.0f, "Brow from pitch  %.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##smile", &p.lipSync.smileBias, 0.0f, 1.0f, "Smile bias  %.2f");
    ImGui::SetNextItemWidth(-1); ImGui::SliderInt("##smooth", &p.lipSync.smoothingRadiusFrames, 0, 5, "Smoothing  %d frames");
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##fps", &p.lipSync.frameRate, 24.0f, 60.0f, "Frame rate  %.0f fps");
    ImGui::Dummy(ImVec2(0, 4 * S()));
    if (PrimaryButton(ICON_MD_ANIMATION "  Generate Animation", ImVec2(-1, 36 * S()))) { app.generate(); if (p.clip.duration > 0) { ui.stepDone[StepLipSync] = true; ui.step = StepAnim; } }
}

void pageAnim(Application& app) {
    Pipeline& p = app.pipe;
    if (p.clip.duration <= 0) {
        ImGui::TextWrapped("No animation yet. Generate one from audio in step 4, or export the current pose.");
    } else {
        ImGui::TextColored(kTextDim, "Clip '%s'   %d frames @ %.0f fps   %.2f s", p.clip.name.c_str(), p.clip.frameCount(), p.clip.frameRate, p.clip.duration);
        // transport
        float bw = (ImGui::GetContentRegionAvail().x - 16 * S()) / 3.0f;
        if (PrimaryButton(app.playing ? ICON_MD_PAUSE " Pause" : ICON_MD_PLAY_ARROW " Play", ImVec2(bw, 30 * S()))) app.playing = !app.playing;
        ImGui::SameLine(); if (WideButton(ICON_MD_STOP " Stop", ImVec2(bw, 30 * S()))) { app.playing = false; app.playTime = 0; p.clip.applyTo(p.rig, 0); }
        ImGui::SameLine(); ImGui::Checkbox("Loop", &app.loop);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##time", &app.playTime, 0.0f, p.clip.duration, "%.2f s")) p.clip.applyTo(p.rig, app.playTime);
        SectionLabel("Curves :");
        auto curve = [&](const char* n) { if (auto* c = p.clip.findBlendCurve(n)) { ImGui::PlotLines(("##" + std::string(n)).c_str(), c->values.data(), int(c->values.size()), 0, n, 0, 1, ImVec2(-1, 38 * S())); } };
        curve(shapes::JawOpen); curve(shapes::MouthSmile); curve(shapes::MouthPucker); curve(shapes::BrowRaise);
    }
    Rule();
    SectionLabel("Export :");
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##out", "out/scene.fbx", ui.exportBuf, sizeof ui.exportBuf);
    ImGui::TextColored(kTextDim, FR_HAVE_FBX_SDK ? "FBX SDK writer: .fbx / .glb / .gltf" : FR_HAVE_ASSIMP ? "Assimp FBX writer: .fbx / .glb / .gltf" : ".glb / .gltf (no FBX writer compiled in)");
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##var", "variations, ; separated", ui.variationBuf, sizeof ui.variationBuf);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("e.g. \"Increase smile; Raise eyebrows; intensity=1.4; subtle\" - one extra file per variation");
    if (PrimaryButton(ICON_MD_IOS_SHARE "  Export...", ImVec2(-1, 36 * S()))) ui.showExportDialog = true;
    Rule();
    ImGui::TextColored(kTextDim, "Log");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.09f, 0.09f, 1));
    ImGui::BeginChild("log", ImVec2(0, 120 * S()), ImGuiChildFlags_Borders);
    ImGui::PushFont(ui.fonts.small);
    for (auto& l : p.log) ImGui::TextWrapped("%s", l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10) ImGui::SetScrollHereY(1.0f);
    ImGui::PopFont(); ImGui::EndChild(); ImGui::PopStyleColor();
}

// AccuRIG-style modal: "Export FBX..." / "Export glTF..." buttons.
void exportDialog(Application& app) {
    if (ui.showExportDialog) { ImGui::OpenPopup("Export"); ui.showExportDialog = false; }
    ImGui::SetNextWindowSize(ImVec2(360 * S(), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * S(), 16 * S()));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kAccent); ImGui::PushStyleColor(ImGuiCol_TitleBg, kAccent);
    if (ImGui::BeginPopupModal("Export", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        auto vars = [&]() { std::vector<std::string> v; std::string s = ui.variationBuf, tok; size_t pos; while ((pos = s.find(';')) != std::string::npos) { tok = s.substr(0, pos); s.erase(0, pos + 1); if (tok.find_first_not_of(' ') != std::string::npos) v.push_back(tok); } if (s.find_first_not_of(' ') != std::string::npos) v.push_back(s); return v; };
        auto withExt = [&](const char* ext) { std::string o = ui.exportBuf; auto dot = o.find_last_of('.'), sl = o.find_last_of("/\\"); if (dot != std::string::npos && (sl == std::string::npos || dot > sl)) o = o.substr(0, dot); return o + "." + ext; };
        if (WideButton(ICON_MD_FILE_DOWNLOAD "  Export FBX...", ImVec2(-1, 36 * S()), FR_HAVE_FBX_SDK || FR_HAVE_ASSIMP)) { app.exportNow(withExt("fbx"), vars()); ui.stepDone[StepAnim] = true; ImGui::CloseCurrentPopup(); }
        if (WideButton(ICON_MD_FILE_DOWNLOAD "  Export glTF binary (.glb)...", ImVec2(-1, 36 * S()))) { app.exportNow(withExt("glb"), vars()); ui.stepDone[StepAnim] = true; ImGui::CloseCurrentPopup(); }
        if (WideButton(ICON_MD_FILE_DOWNLOAD "  Export glTF (.gltf + .bin)...", ImVec2(-1, 36 * S()))) { app.exportNow(withExt("gltf"), vars()); ui.stepDone[StepAnim] = true; ImGui::CloseCurrentPopup(); }
        if (WideButton(ICON_MD_SAVE "  Save current pose only...", ImVec2(-1, 36 * S()))) { std::string err; AnimationClip c = snapshotPose(app.pipe.rig); app.status = app.pipe.exportClip(c, ui.exportBuf, &err) ? "Exported pose to " + std::string(ui.exportBuf) : "Export failed: " + err; ImGui::CloseCurrentPopup(); }
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(-1, 26 * S()))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2); ImGui::PopStyleVar();
}

void rightColumn(Application& app) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - kRightWidth * S(), vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(kRightWidth * S(), vp->WorkSize.y));
    ImGui::Begin("##right", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    static const char* titles[StepCount] = {"Load Face", "Check Model", "Face Rig", "Lip-sync", "Check Animation"};
    ImGui::PushFont(ui.fonts.title); ImGui::TextColored(ImVec4(1, 1, 1, 1), "%d  %s", ui.step + 1, titles[ui.step]); ImGui::PopFont();
    ImGui::Separator();
    ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    switch (ui.step) {
        case StepLoad: pageLoad(app); break;
        case StepCheck: pageCheck(app); break;
        case StepRig: pageRig(app); break;
        case StepLipSync: pageLipSync(app); break;
        default: pageAnim(app); break;
    }
    ImGui::EndChild();
    ImGui::End();
}

// ------------------------------------------------------------------------------ viewport overlays
void viewportOverlay(Application& app) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float x0 = vp->WorkPos.x + kLeftWidth * S(), x1 = vp->WorkPos.x + vp->WorkSize.x - kRightWidth * S();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // top-left stats (AccuRIG: "Total tris / Character height")
    const Rig& r = app.pipe.rig;
    char buf[160];
    ImVec2 p(x0 + 14 * S(), vp->WorkPos.y + 12 * S());
    std::snprintf(buf, sizeof buf, "Total tris : %zu", r.mesh.triangleCount());
    dl->AddText(ui.fonts.regular, ui.fonts.regular->FontSize, p, ImGui::ColorConvertFloat4ToU32(kText), buf);
    glm::vec3 e = r.mesh.boundsMax() - r.mesh.boundsMin();
    std::snprintf(buf, sizeof buf, "Face height : %.2f units   parts : %zu", e.y, r.mesh.parts.size());
    dl->AddText(ui.fonts.regular, ui.fonts.regular->FontSize, ImVec2(p.x, p.y + 20 * S()), ImGui::ColorConvertFloat4ToU32(kTextDim), buf);
    // check-model step: vertical centre line over the viewport like AccuRIG
    if (ui.step == StepCheck) {
        glm::vec2 px; if (app.project(glm::vec3(ui.centreLine, 0, 0), px)) {
            dl->AddLine(ImVec2(px.x, vp->WorkPos.y), ImVec2(px.x, vp->WorkPos.y + vp->WorkSize.y), ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.9f)), 1.5f * S());
        }
        const char* hint = "Rotate / mirror until the face looks at you, upright, with the line on the nose. Then press Rig Face.";
        ImVec2 ts = ImGui::CalcTextSize(hint);
        ImVec2 hp((x0 + x1) * 0.5f - ts.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - 36 * S());
        dl->AddRectFilled(ImVec2(hp.x - 10 * S(), hp.y - 6 * S()), ImVec2(hp.x + ts.x + 10 * S(), hp.y + ts.y + 6 * S()), ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0.55f)), 4 * S());
        dl->AddText(hp, ImGui::ColorConvertFloat4ToU32(kText), hint);
    }
    // vertical tool strip at the viewport's left edge (AccuRIG's icon column)
    ImGui::SetNextWindowPos(ImVec2(x0 + 6 * S(), vp->WorkPos.y + 60 * S()));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##tools", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
    auto iconBtn = [&](const char* glyph, bool active, const char* tip) { return IconButton(glyph, active, tip, ui.fonts, 32.0f); };
    if (iconBtn(ICON_MD_3D_ROTATION, app.tool == Application::Tool::Orbit, "Orbit (Q)")) app.tool = Application::Tool::Orbit;
    if (iconBtn(ICON_MD_ADD_LOCATION_ALT, app.tool == Application::Tool::AddPoint, "Add control point (W)")) app.tool = Application::Tool::AddPoint;
    if (iconBtn(ICON_MD_OPEN_WITH, app.tool == Application::Tool::MovePoint, "Move control point (E)")) app.tool = Application::Tool::MovePoint;
    ImGui::Dummy(ImVec2(0, 6 * S()));
    if (iconBtn(ICON_MD_GRID_ON, app.meshRenderer.wireframe, "Wireframe")) app.meshRenderer.wireframe = !app.meshRenderer.wireframe;
    if (iconBtn(ICON_MD_ACCESSIBILITY_NEW, app.showBones, "Show bones")) app.showBones = !app.showBones;
    if (iconBtn(ICON_MD_HIGHLIGHT_ALT, app.showPoints, "Show control points")) app.showPoints = !app.showPoints;
    if (iconBtn(ICON_MD_LABEL, app.showLabels, "Show labels")) app.showLabels = !app.showLabels;
    ImGui::Dummy(ImVec2(0, 6 * S()));
    if (iconBtn(ICON_MD_UNDO, false, "Undo (Ctrl+Z)")) app.undo();
    if (iconBtn(ICON_MD_REDO, false, "Redo (Ctrl+Y)")) app.redo();
    ImGui::Dummy(ImVec2(0, 6 * S()));
    if (iconBtn(ICON_MD_CENTER_FOCUS_STRONG, false, "Frame the face")) { app.camera.target = 0.5f * (r.mesh.boundsMin() + r.mesh.boundsMax()); app.camera.distance = 2.2f * glm::length(e); app.camera.yaw = app.camera.pitch = 0; }
    ImGui::End();
    // status line bottom-left of viewport
    if (!app.status.empty()) {
        ImVec2 sp(x0 + 14 * S(), vp->WorkPos.y + vp->WorkSize.y - 22 * S());
        dl->AddText(ui.fonts.small, ui.fonts.small->FontSize, sp, ImGui::ColorConvertFloat4ToU32(kAccent), app.status.c_str());
    }
    // playback bar across the viewport bottom while a clip exists
    if (app.pipe.clip.duration > 0 && ui.step != StepCheck) {
        ImGui::SetNextWindowPos(ImVec2((x0 + x1) * 0.5f, vp->WorkPos.y + vp->WorkSize.y - 44 * S()), ImGuiCond_Always, ImVec2(0.5f, 0));
        ImGui::SetNextWindowSize(ImVec2(std::min(520 * S(), x1 - x0 - 80 * S()), 0));
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("##transport", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
        if (PrimaryButton(app.playing ? ICON_MD_PAUSE : ICON_MD_PLAY_ARROW, ImVec2(30 * S(), 24 * S()))) app.playing = !app.playing;
        ImGui::SameLine(); ImGui::SetNextItemWidth(-60 * S());
        if (ImGui::SliderFloat("##t", &app.playTime, 0.0f, app.pipe.clip.duration, "%.2f s")) app.pipe.clip.applyTo(app.pipe.rig, app.playTime);
        ImGui::SameLine(); ImGui::TextColored(kTextDim, "%d f", int(app.playTime * app.pipe.clip.frameRate));
        ImGui::End();
    }
}
} // namespace

void initPanels(Application& app, float uiScale) {
    ui.scale = uiScale;
    ui.fonts = theme::apply(app.options().assetDir, app.options().iconFontPath, uiScale);
    std::strncpy(ui.modelBuf, app.options().modelPath.c_str(), sizeof ui.modelBuf - 1);
    std::strncpy(ui.audioBuf, app.options().audioPath.c_str(), sizeof ui.audioBuf - 1);
    ui.init = true;
}

void notifyModelLoaded(bool ok) { ui.stepDone[StepLoad] = ok; ui.stepDone[StepRig] = false; if (ok && ui.step == StepLoad) ui.step = StepCheck; }
void notifyRigBuilt() { ui.stepDone[StepCheck] = ui.stepDone[StepRig] = true; if (ui.step < StepRig) ui.step = StepRig; }
void notifyClipGenerated() { ui.stepDone[StepLipSync] = true; if (ui.step < StepAnim) ui.step = StepAnim; }
void setStep(int step) { ui.step = std::clamp(step, 0, StepCount - 1); }
bool viewportContains(float x, float y) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    return x >= vp->WorkPos.x + kLeftWidth * S() && x <= vp->WorkPos.x + vp->WorkSize.x - kRightWidth * S() && y >= vp->WorkPos.y;
}

void drawPanels(Application& app) {
    if (!ui.init) initPanels(app, 1.0f);
    // paint the viewport area's chrome background under everything (panels cover left/right)
    menuBar(app);
    leftColumn(app);
    rightColumn(app);
    viewportOverlay(app);
    exportDialog(app);
}

void drawOverlayLabels(Application& app) {
    if (!app.showLabels) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const Rig& rig = app.pipe.rig;
    ImFont* f = ui.fonts.small ? ui.fonts.small : ImGui::GetFont();
    if (app.showPoints) for (size_t i = 0; i < rig.controlPoints.size(); ++i) {
        glm::vec2 px;
        if (app.project(rig.controlPoints[i].currentPosition(), px) && viewportContains(px.x, px.y)) {
            ImU32 col = int(i) == app.selectedPoint ? ImGui::ColorConvertFloat4ToU32(kAccentHi) : IM_COL32(235, 235, 235, 210);
            dl->AddText(f, f->FontSize, ImVec2(px.x + 8, px.y - 8), col, rig.controlPoints[i].name.c_str());
        }
    }
    if (app.showBones) {
        auto W = rig.skeleton.poseWorldMatrices();
        for (size_t i = 0; i < W.size(); ++i) { glm::vec2 px; if (app.project(glm::vec3(W[i][3]), px) && viewportContains(px.x, px.y)) dl->AddText(f, f->FontSize, ImVec2(px.x + 6, px.y + 4), IM_COL32(140, 200, 255, 220), rig.skeleton.bones[i].name.c_str()); }
    }
}

} // namespace fr

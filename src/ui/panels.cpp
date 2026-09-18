// AccuRIG-style shell: menu bar, left step wizard, centre viewport, right property panel.
// Steps (facial-rigging flavour of AccuRIG's Load / Check Model / Body Rig / Hand Rig / Check Animation):
//   1 Load Face   2 Check Model (orientation)   3 Face Rig   4 Lip-sync   5 Check Animation / Export
#include "ui/panels.h"
#include "ui/theme.h"
#include "app/application.h"
#include "rig/rig_tools.h"
#include "audio/viseme_mapper.h"
#include "audio/phoneme_aligner.h"
#include "audio/ml_viseme_mapper.h"
#include <algorithm>
#include <functional>
#include <cstring>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include "export/exporter.h"
#include "anim/lipsync_generator.h"
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
    int emotionSel = 0; float emotionAmt = 0.8f; float gazeYaw = 0.0f, gazePitch = 0.0f;
    char clipBuf[512] = "out/clip.json";
    char projectBuf[512] = "out/session.frproj";
    char liveLinkHost[64] = "127.0.0.1"; int liveLinkPort = 11111;
    // export
    bool showExportDialog = false, showImportClip = false; int showProjectDialog = 0; // 1 open, 2 save
    int rigTabRequest = -1;
    // bake / correctives
    char bakeName[64] = "Custom"; bool bakeResidual = true, bakeSplit = false; int corrA = 0, corrB = 0;
    std::vector<AudioDevice> devs; bool devsListed = false; int devSel = -1; std::string devErr;
    Fonts fonts; float scale = 1.0f;
    // timeline editor
    bool timelineOpen = true; float tlZoom = 1.0f, tlScroll = 0.0f;   // seconds visible = duration / zoom; scroll in seconds
    float selA = -1.0f, selB = -1.0f;                                   // time-range selection (seconds), selA<0 = none
    int tlCurve = 0;                                                    // active curve in the editor
    bool tlPainting = false; float tlLastT = 0, tlLastV = 0;
    int tlMode = 0, tlSelKey = -1, tlDrag = 0;                          // drag 4 = box select, 5 = move selection
    std::vector<int> tlBoxSel; ImVec2 tlBoxA, tlBoxB; float tlMoveLastT = 0;  // multi-key selection on the active channel                          // 0 paint / 1 keys; drag: -1 in-handle, 1 out-handle, 2 key, 3 scrub
    float tlGain = 1.0f, tlOffset = 0.0f; int tlSmooth = 1;
    bool tlShow[128] = {true, true, true, false, false, true, false, false, false, false, false, false, false, false, false, false};
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
            if (ImGui::MenuItem("Open Project...", "Ctrl+O")) ui.showProjectDialog = 1;
            if (ImGui::MenuItem("Save Project...", "Ctrl+S", false, app.pipe.rig.mesh.vertexCount() > 0)) ui.showProjectDialog = 2;
            ImGui::Separator();
            if (ImGui::MenuItem("Export...", nullptr, false, app.pipe.rig.mesh.vertexCount() > 0)) ui.showExportDialog = true;
            if (ImGui::MenuItem("Import clip JSON...", nullptr, false, app.pipe.rig.mesh.vertexCount() > 0)) { ui.step = StepAnim; ui.showImportClip = true; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem((std::string("Undo ") + app.undoLabel()).c_str(), "Ctrl+Z", false, app.canUndo())) app.undo();
            if (ImGui::MenuItem((std::string("Redo ") + app.redoLabel()).c_str(), "Ctrl+Y", false, app.canRedo())) app.redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Pose")) { app.pushUndo("reset pose"); app.pipe.rig.resetPose(); app.playing = false; app.playTime = 0; }
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
            if (ImGui::BeginMenu("Anti-aliasing")) {
                for (int n : {0, 2, 4, 8, 16}) {
                    if (n > 0 && n > app.msaaMax()) continue;
                    char label[32]; std::snprintf(label, sizeof label, n ? "%dx MSAA" : "Off", n);
                    if (ImGui::MenuItem(label, nullptr, app.msaaSamples == n)) app.msaaSamples = n;
                }
                ImGui::Separator(); ImGui::TextDisabled("active: %dx (max %dx)", app.msaaActive(), app.msaaMax());
                ImGui::EndMenu();
            }
            ImGui::Separator();
            using SM = MeshRenderer::ShadeMode;
            auto& sm = app.meshRenderer.shadeMode;
            if (ImGui::MenuItem("Shading: Lit", "1", sm == SM::Lit)) sm = SM::Lit;
            if (ImGui::MenuItem("Shading: Normals", "2", sm == SM::Normals)) sm = SM::Normals;
            if (ImGui::MenuItem("Shading: Bone weights", "3", sm == SM::BoneWeights)) sm = SM::BoneWeights;
            if (ImGui::MenuItem("Shading: Blendshape influence", "4", sm == SM::ShapeInfluence)) sm = SM::ShapeInfluence;
            if (ImGui::MenuItem("Shading: Displacement", "5", sm == SM::Displacement)) sm = SM::Displacement;
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
        if (ImGui::Button(label, ImVec2(92 * S(), 30 * S()))) { app.pushUndo("transform model", true); p.transformModel(R); app.reuploadMesh(); ui.stepDone[StepRig] = false; }
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
        app.pushUndo("rig face"); p.buildDefaultRig(); app.reuploadMesh(); app.status = p.log.back();
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
    toolBtn(ICON_MD_3D_ROTATION, Application::Tool::Orbit); toolBtn(ICON_MD_ADD_LOCATION_ALT, Application::Tool::AddPoint); toolBtn(ICON_MD_OPEN_WITH, Application::Tool::MovePoint); ImGui::TextColored(kTextDim, "%s", app.tool == Application::Tool::Orbit ? "Orbit (Q)" : app.tool == Application::Tool::AddPoint ? "Add point (W)" : app.tool == Application::Tool::MovePoint ? "Move point (E)" : "Paint weights (R)"); ImGui::NewLine();
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
        if (ImGui::Combo("##binding", &bt, "Unbound\0Bone\0BlendShape\0FreeForm\0")) { app.pushUndo("change binding"); c.binding = BindingType(bt); c.offset = glm::vec3(0); if (c.binding != BindingType::FreeForm) c.target = 0; }
        ImGui::SetNextItemWidth(-1);
        if (c.binding == BindingType::Bone && !rig.skeleton.bones.empty()) {
            int t = std::max(c.target, 0);
            if (ImGui::BeginCombo("##bone", rig.skeleton.bones[std::min<size_t>(t, rig.skeleton.bones.size() - 1)].name.c_str())) {
                for (size_t i = 0; i < rig.skeleton.bones.size(); ++i) if (ImGui::Selectable(rig.skeleton.bones[i].name.c_str(), int(i) == t)) { app.pushUndo("bind to bone"); rig.bindToBone(app.selectedPoint, int(i)); }
                ImGui::EndCombo();
            }
        } else if (c.binding == BindingType::BlendShape && !rig.blendShapes.empty()) {
            int t = std::max(c.target, 0);
            if (ImGui::BeginCombo("##bs", rig.blendShapes[std::min<size_t>(t, rig.blendShapes.size() - 1)].name.c_str())) {
                for (size_t i = 0; i < rig.blendShapes.size(); ++i) if (ImGui::Selectable(rig.blendShapes[i].name.c_str(), int(i) == t)) { app.pushUndo("bind to shape"); rig.bindToBlendShape(app.selectedPoint, int(i), c.driveAxis, c.driveRange); }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(-1); ImGui::DragFloat3("##axis", &c.driveAxis.x, 0.01f, -1, 1, "axis %.2f");
            ImGui::SetNextItemWidth(-1); ImGui::DragFloat("##range", &c.driveRange, 0.001f, 0.005f, 1.0f, "range %.3f");
        } else if (c.binding == BindingType::FreeForm) { ImGui::SliderFloat("##rbf", &c.radius, 0.02f, 0.6f, "RBF radius %.2f"); }
        glm::vec3 off = c.offset; ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat3("##off", &off.x, 0.002f, -0.5f, 0.5f, "%.3f")) rig.moveControlPoint(app.selectedPoint, off);
        if (WideButton("Zero offset", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4 * S(), 26 * S()))) rig.moveControlPoint(app.selectedPoint, glm::vec3(0));
        ImGui::SameLine();
        if (WideButton("Delete", ImVec2(-1, 26 * S()))) { app.pushUndo("delete handle"); rig.removeControlPoint(app.selectedPoint); app.selectedPoint = -1; }
    }
    // ---- bake the current sculpt (free-form handles + active shapes) into a new blendshape
    Rule(); SectionLabel("Bake pose as blendshape :");
    bool anyFree = false; for (const auto& c : rig.controlPoints) anyFree |= c.binding == BindingType::FreeForm && glm::dot(c.offset, c.offset) > 1e-10f;
    ImGui::TextWrapped("Sculpt with free-form handles (and any shape sliders), then bake the result as a new shape you can animate and export.");
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##bakeName", "shape name", ui.bakeName, sizeof ui.bakeName);
    ImGui::Checkbox("Residual only", &ui.bakeResidual); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Store only what the handles add on top of the active shapes (a corrective), not the shapes themselves.");
    ImGui::SameLine(); ImGui::Checkbox("Split L / R", &ui.bakeSplit); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create name_L (x>0) and a mirrored name_R.");
    if (PrimaryButton(ICON_MD_SAVE_ALT "  Bake as new blendshape", ImVec2(-1, 30 * S()), anyFree || std::any_of(rig.blendShapes.begin(), rig.blendShapes.end(), [](const BlendShape& b) { return b.weight > 1e-4f; }))) {
        app.pushUndo("bake blendshape");
        BakeShapeOptions o; o.name = ui.bakeName[0] ? ui.bakeName : "Custom"; o.subtractExisting = ui.bakeResidual; o.mirrorToOtherSide = ui.bakeSplit;
        int idx = bakePoseAsBlendShape(rig, o, &app.mirrorMap);
        if (idx >= 0) { rig.setBlendWeight(idx, 1.0f); if (ui.bakeSplit && idx + 1 < int(rig.blendShapes.size())) rig.setBlendWeight(idx + 1, 1.0f); app.reuploadMesh(); app.status = "Baked '" + rig.blendShapes[size_t(idx)].name + "' (" + std::to_string(rig.blendShapes[size_t(idx)].indices.size()) + " vertices)"; app.meshRenderer.shadeMode = MeshRenderer::ShadeMode::ShapeInfluence; app.meshRenderer.heatShape = idx; }
        else { app.status = "Nothing to bake - move a free-form handle first"; app.undo(); }
    }
}

// ---- weight painting tab
void weightPaintEditor(Application& app) {
    Rig& rig = app.pipe.rig; WeightBrush& b = app.brush;
    if (!rig.hasSkin()) { ImGui::TextWrapped("No skin weights on this rig."); return; }
    bool paintTool = app.tool == Application::Tool::PaintWeights;
    if (PrimaryButton(paintTool ? ICON_MD_BRUSH "  Painting - drag on the face" : ICON_MD_BRUSH "  Paint weights (R)", ImVec2(-1, 30 * S()))) { app.tool = paintTool ? Application::Tool::Orbit : Application::Tool::PaintWeights; if (!paintTool) { app.meshRenderer.shadeMode = MeshRenderer::ShadeMode::BoneWeights; app.meshRenderer.heatBone = b.bone; } }
    SectionLabel("Bone :");
    if (b.bone < 0 || size_t(b.bone) >= rig.skeleton.bones.size()) b.bone = std::min<int>(1, int(rig.skeleton.bones.size()) - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##pbone", rig.skeleton.bones[size_t(b.bone)].name.c_str())) {
        for (size_t i = 0; i < rig.skeleton.bones.size(); ++i) if (ImGui::Selectable(rig.skeleton.bones[i].name.c_str(), int(i) == b.bone)) { b.bone = int(i); app.meshRenderer.heatBone = b.bone; app.meshRenderer.shadeMode = MeshRenderer::ShadeMode::BoneWeights; }
        ImGui::EndCombo();
    }
    SectionLabel("Brush :");
    int mode = int(b.mode);
    const char* modes[] = {"Add", "Subtract", "Replace", "Smooth"};
    float bw = (ImGui::GetContentRegionAvail().x - 12 * S()) / 4.0f;
    for (int i = 0; i < 4; ++i) { if (i) ImGui::SameLine(); bool on = mode == i; if (on) { ImGui::PushStyleColor(ImGuiCol_Button, kAccent); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.08f, 0.02f, 1)); } if (ImGui::Button(modes[i], ImVec2(bw, 26 * S()))) b.mode = WeightBrushMode(i); if (on) ImGui::PopStyleColor(2); }
    glm::vec3 e = rig.mesh.boundsMax() - rig.mesh.boundsMin();
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##rad", &b.radius, 0.005f * e.y, 0.5f * e.y, "Radius  %.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##str", &b.strength, 0.01f, 1.0f, "Strength  %.2f");
    if (b.mode == WeightBrushMode::Replace) { ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##val", &b.value, 0.0f, 1.0f, "Target weight  %.2f"); }
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##fall", &b.falloff, 0.0f, 1.0f, "Falloff  %.2f");
    ImGui::Checkbox("Symmetric (X mirror)", &b.symmetric); ImGui::SameLine(); ImGui::Checkbox("Front faces only", &b.frontFacingOnly);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skip vertices facing away from the camera so a stroke on the cheek doesn't paint the back of the head.");
    SectionLabel("Mirror :");
    { float w = (ImGui::GetContentRegionAvail().x - 8 * S()) / 2.0f;
      if (WideButton(ICON_MD_FLIP "  L  ->  R", ImVec2(w, 28 * S()))) { app.pushUndo("mirror weights L->R"); int n = mirrorWeights(rig, true, app.mirrorMap); app.pipe.skinEdited = true; app.reuploadWeights(); app.status = "Mirrored " + std::to_string(n) + " vertices (model's left, +x, onto the right)"; }
      ImGui::SameLine();
      if (WideButton(ICON_MD_FLIP "  R  ->  L", ImVec2(w, 28 * S()))) { app.pushUndo("mirror weights R->L"); int n = mirrorWeights(rig, false, app.mirrorMap); app.pipe.skinEdited = true; app.reuploadWeights(); app.status = "Mirrored " + std::to_string(n) + " vertices (model's right, -x, onto the left)"; }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies weights across x = 0 (nearest mirrored vertex), swapping L/R bones (EyeL <-> EyeR).");
    if (WideButton("Normalise / clean weights", ImVec2(-1, 26 * S()))) { app.pushUndo("clean weights"); cleanWeights(rig); app.reuploadWeights(); app.status = "Weights normalised"; }
    size_t paired = 0; for (int q : app.mirrorMap.partner) paired += q >= 0;
    ImGui::TextColored(kTextDim, "%zu / %zu vertices have a mirror partner", paired, rig.mesh.vertexCount());
    ImGui::TextWrapped("Heat map = weight of the selected bone only (a symmetric stroke on the partner bone shows when you select it). Drag to paint; [ ] change the radius; Ctrl+Z undoes a whole stroke.");
}

// ---- corrective / combination shapes tab
void correctiveEditor(Application& app) {
    Rig& rig = app.pipe.rig;
    ImGui::TextWrapped("A corrective fires automatically when two shapes are both on (weight = A x B). Use it to fix volume loss, e.g. JawOpen + MouthPucker.");
    auto shapeCombo = [&](const char* id, int& sel) {
        sel = std::clamp(sel, 0, int(rig.blendShapes.size()) - 1);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo(id, rig.blendShapes[size_t(sel)].name.c_str())) { for (size_t i = 0; i < rig.blendShapes.size(); ++i) if (ImGui::Selectable(rig.blendShapes[i].name.c_str(), int(i) == sel)) sel = int(i); ImGui::EndCombo(); }
    };
    if (rig.blendShapes.size() < 2) { ImGui::TextColored(kWarn, "Need at least two blendshapes."); return; }
    SectionLabel("Drivers :");
    if (ui.corrB == 0 && ui.corrA == 0) { ui.corrA = std::max(0, rig.findBlendShape(shapes::JawOpen)); ui.corrB = std::max(0, rig.findBlendShape(shapes::MouthPucker)); }
    shapeCombo("##corrA", ui.corrA); shapeCombo("##corrB", ui.corrB);
    const std::string nameA = rig.blendShapes[size_t(ui.corrA)].name, nameB = rig.blendShapes[size_t(ui.corrB)].name;
    if (WideButton("1. Pose both drivers at 1.0", ImVec2(-1, 26 * S()), ui.corrA != ui.corrB)) { app.pushUndo("pose drivers"); rig.resetPose(); rig.setBlendWeight(ui.corrA, 1.0f); rig.setBlendWeight(ui.corrB, 1.0f); rig.applyCombinations(); rig.syncControlPointsFromRig(); app.playing = false; }
    ImGui::TextWrapped("2. Fix the shape with free-form handles (Handles tab, tool W/E) or other shape sliders.");
    std::string autoName = nameA + "_" + nameB;
    if (WideButton(("3. Bake corrective '" + autoName + "'").c_str(), ImVec2(-1, 26 * S()), ui.corrA != ui.corrB)) {
        app.pushUndo("bake corrective");
        int idx = bakeCorrectiveShape(rig, nameA, nameB, autoName, &app.mirrorMap);
        if (idx >= 0) { app.reuploadMesh(); app.status = "Corrective '" + autoName + "' baked (" + std::to_string(rig.blendShapes[size_t(idx)].indices.size()) + " vertices) - fires at " + nameA + " x " + nameB; }
        else { app.status = "Nothing to bake: move a handle or another shape first"; app.undo(); }
    }
    SectionLabel("Active correctives :");
    if (rig.combinations.empty()) ImGui::TextColored(kTextDim, "none");
    int remove = -1;
    for (size_t i = 0; i < rig.combinations.size(); ++i) {
        auto& c = rig.combinations[i]; ImGui::PushID(int(i) + 5000);
        int s = rig.findBlendShape(c.shape);
        ImGui::TextColored(kText, "%s", c.shape.c_str()); ImGui::SameLine(); ImGui::TextColored(kTextDim, "= %s x %s", c.driverA.c_str(), c.driverB.c_str());
        ImGui::SetNextItemWidth(90 * S()); ImGui::SliderFloat("##gain", &c.gain, 0.0f, 2.0f, "gain %.2f"); ImGui::SameLine();
        ImGui::Checkbox("min", &c.useMin); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drive by min(A, B) instead of A x B (fires earlier)."); ImGui::SameLine();
        if (s >= 0) ImGui::TextColored(kAccent, "w %.2f", rig.blendShapes[size_t(s)].weight);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 18 * S()); if (IconButton(ICON_MD_CLOSE, false, "Remove corrective (keeps the shape)", ui.fonts, 20.0f)) remove = int(i);
        ImGui::PopID();
    }
    if (remove >= 0) { app.pushUndo("remove corrective"); rig.combinations.erase(rig.combinations.begin() + remove); }
    rig.applyCombinations();
}

void pageRig(Application& app) {
    Rig& rig = app.pipe.rig;
    if (rig.skeleton.bones.empty()) {
        ImGui::TextWrapped("The face is not rigged yet. Check the orientation in step 2, then press Rig Face.");
        if (PrimaryButton("Rig Face", ImVec2(-1, 36 * S()))) { app.pushUndo("rig face"); app.pipe.buildDefaultRig(); app.reuploadMesh(); ui.stepDone[StepCheck] = ui.stepDone[StepRig] = true; }
        return;
    }
    auto parts = rig.detectParts();
    ImGui::TextColored(kTextDim, "%zu bones  %zu blendshapes  %zu control points", rig.skeleton.bones.size(), rig.blendShapes.size(), rig.controlPoints.size());
    if (parts.any()) {
        ImGui::TextColored(kAccent, "Anatomical parts bound:"); ImGui::SameLine();
        std::string s; if (parts.teethLower >= 0) s += "lower teeth/gums/tongue -> Jaw  "; if (parts.browL >= 0) s += "brows  "; if (parts.eyeL >= 0) s += "eyes";
        ImGui::TextWrapped("%s", s.c_str());
    } else ImGui::TextColored(kWarn, "Single surface: jaw skinned by height falloff.");
    if (WideButton("Rebuild default face rig", ImVec2(-1, 28 * S()))) { app.pushUndo("rebuild rig"); app.pipe.buildDefaultRig(); app.reuploadMesh(); app.status = "Rebuilt default rig"; }
    ImGui::SameLine(0, 0);
    ImGui::Checkbox("Skin first", &rig.skinFirst);

    if (ImGui::BeginTabBar("rigtabs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton)) {
        auto flag = [&](int i) { return ui.rigTabRequest == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
        if (ImGui::BeginTabItem("Handles", nullptr, flag(0))) { controlPointEditor(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Weights", nullptr, flag(1))) { weightPaintEditor(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Correctives", nullptr, flag(2))) { correctiveEditor(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Blendshapes", nullptr, flag(3))) {
            static char filter[64] = "";
            ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##filter", "filter (e.g. mouth, brow)", filter, sizeof filter);
            ImGui::BeginChild("bs", ImVec2(0, 300 * S()));
            for (size_t i = 0; i < rig.blendShapes.size(); ++i) {
                auto& bs = rig.blendShapes[i];
                if (filter[0] && bs.name.find(filter) == std::string::npos) continue;
                ImGui::PushID(int(i));
                if (i == std::size(shapes::All) && rig.blendShapes.size() > std::size(shapes::All)) ImGui::SeparatorText("Authored (ARKit / ICT)");
                ImGui::SetNextItemWidth(-26 * S());
                if (ImGui::SliderFloat("##w", &bs.weight, 0.0f, 1.0f, bs.name.c_str())) rig.syncControlPointsFromRig();
                ImGui::SameLine();
                { bool on = app.meshRenderer.shadeMode == MeshRenderer::ShadeMode::ShapeInfluence && app.meshRenderer.heatShape == int(i);
                  if (IconButton(ICON_MD_GRADIENT, on, "Show this shape's influence as a heat map", ui.fonts, 22.0f)) {
                      if (on) { app.meshRenderer.shadeMode = MeshRenderer::ShadeMode::Lit; app.meshRenderer.heatShape = -1; }
                      else { app.meshRenderer.shadeMode = MeshRenderer::ShadeMode::ShapeInfluence; app.meshRenderer.heatShape = int(i); } } }
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (WideButton("Reset all weights", ImVec2(-1, 26 * S()))) { app.pushUndo("reset weights"); rig.resetPose(); }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Bones", nullptr, flag(4))) {
            for (size_t i = 0; i < rig.skeleton.bones.size(); ++i) {
                Bone& b = rig.skeleton.bones[i]; ImGui::PushID(int(i) + 1000);
                glm::vec3 e = glm::degrees(glm::eulerAngles(b.poseRotation));
                ImGui::TextColored(kText, "%s%s", b.name.c_str(), b.parent >= 0 ? "" : "  (root)");
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4 * S());
                { bool on = app.meshRenderer.shadeMode == MeshRenderer::ShadeMode::BoneWeights && app.meshRenderer.heatBone == int(i);
                  if (IconButton(ICON_MD_THERMOSTAT, on, "Show skin weights for this bone", ui.fonts, 22.0f)) {
                      app.meshRenderer.shadeMode = on ? MeshRenderer::ShadeMode::Lit : MeshRenderer::ShadeMode::BoneWeights; app.meshRenderer.heatBone = int(i); } }
                ImGui::SetNextItemWidth(-1); if (ImGui::SliderFloat3("##rot", &e.x, -45.0f, 45.0f, "%.1f deg")) b.poseRotation = glm::quat(glm::radians(e));
                ImGui::SetNextItemWidth(-1); if (ImGui::DragFloat3("##pos", &b.poseTranslation.x, 0.002f, -0.3f, 0.3f, "%.3f")) rig.syncControlPointsFromRig();
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Expression")) {
            SectionLabel("Emotion preset :");
            std::vector<const char*> names; for (int i = 0; i < kExpressionPresetCount; ++i) names.push_back(kExpressionPresets[i].name);
            ImGui::SetNextItemWidth(-1); ImGui::Combo("##emo", &ui.emotionSel, names.data(), int(names.size()));
            ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##emoAmt", &ui.emotionAmt, 0.0f, 1.0f, "Amount  %.2f");
            if (WideButton(ICON_MD_MOOD "  Apply to pose", ImVec2(-1, 28 * S()))) {
                app.pushUndo("apply expression"); const ExpressionPreset& e = kExpressionPresets[ui.emotionSel]; const float a = ui.emotionAmt;
                auto set = [&](const char* n, float v) { rig.setBlendWeight(n, v * a); };
                set(shapes::MouthSmile, e.smile); set(shapes::MouthFrown, e.frown); set(shapes::BrowRaise, e.browRaise); set(shapes::BrowDown, e.browDown);
                set(shapes::EyeWide, e.eyeWide); set(shapes::JawOpen, e.jaw); set(shapes::LipsPress, e.lipsPress); set(shapes::MouthPucker, e.pucker);
                rig.syncControlPointsFromRig();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also usable as an export variation: type the preset name (e.g. \"angry=0.6\") in the variations box.");
            Rule();
            SectionLabel("Gaze :");
            if (!rig.hasEyeBones()) ImGui::TextColored(kTextDim, "No separate eyeballs on this model - gaze needs EyeL / EyeR parts (e.g. the ICT-FaceKit head).");
            else {
                bool ch = false;
                ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##gy", &ui.gazeYaw, -35.0f, 35.0f, "Look left/right  %.0f deg");
                ImGui::SetNextItemWidth(-1); ch |= ImGui::SliderFloat("##gp", &ui.gazePitch, -25.0f, 25.0f, "Look down/up  %.0f deg");
                if (ch) rig.setGaze(ui.gazeYaw, ui.gazePitch);
                if (WideButton(ICON_MD_REMOVE_RED_EYE "  Look at camera", ImVec2(-1, 28 * S()))) { rig.lookAt(app.camera.position()); ui.gazeYaw = ui.gazePitch = 0; }
                ImGui::SameLine(); if (WideButton("Centre", ImVec2(-1, 28 * S()))) { ui.gazeYaw = ui.gazePitch = 0; rig.setGaze(0, 0); }
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
        ImGui::EndTabBar(); ui.rigTabRequest = -1;
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
    SectionLabel("Transcript (optional) :");
    {
        static char transcript[2048] = "";
        static bool synced = false;
        if (!synced) { std::snprintf(transcript, sizeof transcript, "%s", p.transcript.c_str()); synced = true; }
        if (ImGui::InputTextMultiline("##transcript", transcript, sizeof transcript, ImVec2(-1, 56 * S()))) p.transcript = transcript;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("What is said in the audio. Phones come from CMUdict (+ rules), forced-aligned to the\nviseme posteriors; use [HH AH L OW] for explicit ARPAbet. Leave empty for purely acoustic lip-sync.");
        if (!p.lastAlignment.phones.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Aligned %zu words / %zu phones, speech coverage %.0f%%", p.lastAlignment.words.size(), p.lastAlignment.phones.size(), p.lastAlignment.coverage * 100.0f);
            ImGui::PopStyleColor();
        }
    }
    SectionLabel("Performance layer :");
    {
        int sel = 0; std::vector<const char*> names;
        for (int i = 0; i < kExpressionPresetCount; ++i) { names.push_back(kExpressionPresets[i].name); if (p.lipSync.emotion == kExpressionPresets[i].name) sel = i; }
        ImGui::SetNextItemWidth(-1); if (ImGui::Combo("##lsEmo", &sel, names.data(), int(names.size()))) p.lipSync.emotion = names[size_t(sel)];
        ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##lsEmoAmt", &p.lipSync.emotionAmount, 0.0f, 1.0f, "Emotion amount  %.2f");
        ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##head", &p.lipSync.headMotion, 0.0f, 1.0f, "Head nods / sway  %.2f");
        ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##gaze", &p.lipSync.gazeMotion, 0.0f, 1.0f, "Eye saccades  %.2f");
        if (ImGui::IsItemHovered() && !p.rig.hasEyeBones()) ImGui::SetTooltip("This model has no eyeball parts; saccades need EyeL / EyeR bones.");
    }
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
        if (ImGui::Checkbox("Timeline editor (viewport)", &ui.timelineOpen)) {}
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Words / phones / visemes lanes and paintable curves under the viewport.\nEdits are undoable (Ctrl+Z).");
        SectionLabel("Curves :");
        auto curve = [&](const char* n) { if (auto* c = p.clip.findBlendCurve(n)) { ImGui::PlotLines(("##" + std::string(n)).c_str(), c->values.data(), int(c->values.size()), 0, n, 0, 1, ImVec2(-1, 38 * S())); } };
        curve(shapes::JawOpen); curve(shapes::MouthSmile); curve(shapes::MouthPucker); curve(shapes::BrowRaise);
        if (auto* c = p.clip.findBlendCurve(shapes::MouthFrown)) { bool any = false; for (float v : c->values) any |= v > 1e-4f; if (any) curve(shapes::MouthFrown); }
        if (auto* c = p.clip.findBlendCurve(shapes::BrowDown)) { bool any = false; for (float v : c->values) any |= v > 1e-4f; if (any) curve(shapes::BrowDown); }
        for (const auto& bc : p.clip.boneRotations) if (!bc.empty()) {
            std::vector<float> deg; deg.reserve(bc.values.size()); for (const auto& q : bc.values) deg.push_back(glm::degrees(glm::angle(q)));
            ImGui::PlotLines(("##" + bc.target).c_str(), deg.data(), int(deg.size()), 0, (bc.target + " (deg)").c_str(), 0, bc.target == "Jaw" ? 20.0f : 12.0f, ImVec2(-1, 32 * S()));
        }
    }
    Rule();
    SectionLabel("Clip JSON :");
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##clip", "out/clip.json", ui.clipBuf, sizeof ui.clipBuf);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mesh-free curves with ARKit aliases - load onto another rig or hand-edit.");
    {
        float bw = (ImGui::GetContentRegionAvail().x - 8 * S()) / 2.0f;
        if (WideButton(ICON_MD_FILE_UPLOAD "  Import", ImVec2(bw, 28 * S())) || ui.showImportClip) {
            ui.showImportClip = false; std::vector<AnimationClip> in; std::string err;
            if (loadClipsJson(ui.clipBuf, in, &err) && !in.empty()) { app.pushUndo("import clip"); p.clip = in[0]; app.playTime = 0; app.playing = true; p.clip.applyTo(p.rig, 0); app.status = "Imported clip '" + p.clip.name + "' (" + std::to_string(in.size()) + " in file)"; ui.stepDone[StepLipSync] = true; }
            else app.status = "Import failed: " + (err.empty() ? std::string("no clips in file") : err);
        }
        ImGui::SameLine();
        if (WideButton(ICON_MD_SAVE "  Save", ImVec2(bw, 28 * S()), p.clip.duration > 0)) { std::string err; app.status = p.exportClip(p.clip, ui.clipBuf, &err) ? "Saved clip JSON " + std::string(ui.clipBuf) : "Save failed: " + err; }
    }
    Rule();
    SectionLabel("Live Link (ARKit over UDP) :");
    {
        const bool on = app.liveLinkActive();
        ImGui::BeginDisabled(on);
        ImGui::SetNextItemWidth(140 * S()); ImGui::InputText("##llhost", ui.liveLinkHost, sizeof ui.liveLinkHost); ImGui::SameLine();
        ImGui::SetNextItemWidth(80 * S()); ImGui::InputInt("##llport", &ui.liveLinkPort, 0, 0);
        ImGui::EndDisabled();
        if (WideButton(on ? ICON_MD_STOP "  Stop streaming" : ICON_MD_SENSORS "  Stream to Live Link", ImVec2(-1, 28 * S()))) { if (on) app.liveLinkStop(); else app.liveLinkStart(ui.liveLinkHost, ui.liveLinkPort); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Streams the current pose as Live Link Face packets (52 ARKit coefficients + head/eye rotation, 60 fps). Add an 'Apple ARKit' Live Link source in Unreal with this port.");
        if (on) ImGui::TextColored(kAccent, ICON_MD_SENSORS " streaming: %llu frames, %d/52 shapes mapped", (unsigned long long)app.liveLinkFrames(), app.liveLinkMapped());
    }
    Rule();
    SectionLabel("Export :");
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##out", "out/scene.fbx", ui.exportBuf, sizeof ui.exportBuf);
    ImGui::TextColored(kTextDim, FR_HAVE_FBX_SDK ? "FBX SDK writer: .fbx / .glb / .gltf / .json / .csv" : FR_HAVE_ASSIMP ? "Assimp FBX writer: .fbx / .glb / .gltf / .json / .csv" : ".glb / .gltf / .json / .csv (no FBX writer compiled in)");
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


// -------------------------------------------------------------------------------------- timeline
// Dope-sheet style editor across the viewport bottom: aligned words / phones / visemes lanes, the
// baked curve of the active blendshape (drag to paint, shift-drag to select a range) and a
// playhead. Range operations (gain / offset / smooth / flatten) act on the selection or the whole clip.
namespace timeline {

float tlHeight() { return 300 * S(); }

// A channel the editor can show: a baked blend curve (axis -1) or a bone rotation axis (deg).
struct Channel { std::string target; int axis; int bakedIndex; bool bone; };
std::vector<Channel> channels(const AnimationClip& clip) {
    std::vector<Channel> ch;
    for (size_t i = 0; i < clip.blendCurves.size(); ++i) ch.push_back({clip.blendCurves[i].target, -1, int(i), false});
    for (size_t i = 0; i < clip.boneRotations.size(); ++i) for (int ax = 0; ax < 3; ++ax) ch.push_back({clip.boneRotations[i].target, ax, int(i), true});
    return ch;
}
std::string channelLabel(const Channel& c) { return c.bone ? c.target + (c.axis == 0 ? " X (deg)" : c.axis == 1 ? " Y (deg)" : " Z (deg)") : c.target; }
// baked value in channel units (weight 0..1 or degrees)
float bakedAt(const AnimationClip& clip, const Channel& c, float t) {
    if (!c.bone) return clip.blendCurves[size_t(c.bakedIndex)].sample(t);
    glm::vec3 e = glm::degrees(glm::eulerAngles(clip.boneRotations[size_t(c.bakedIndex)].sample(t))); return e[c.axis];
}
float compositeAt(const AnimationClip& clip, const Channel& c, float t) { return bakedAt(clip, c, t) + clip.keyLayer.evaluate(c.target, c.axis, t); }
bool shown(size_t i) { return i < 128 ? ui.tlShow[i] : false; }

void rangeApply(Application& app, const char* what, const std::function<void(Curve<float>&, size_t)>& fn) {
    Pipeline& p = app.pipe; if (p.clip.duration <= 0) return;
    app.pushUndo(what);
    float a = ui.selA >= 0 ? std::min(ui.selA, ui.selB) : 0.0f, b = ui.selA >= 0 ? std::max(ui.selA, ui.selB) : p.clip.duration;
    int n = 0;
    for (size_t ci = 0; ci < p.clip.blendCurves.size(); ++ci) {
        if (!shown(ci)) continue;
        auto& c = p.clip.blendCurves[ci];
        for (size_t k = 0; k < c.times.size(); ++k) if (c.times[k] >= a && c.times[k] <= b) { fn(c, k); ++n; }
    }
    for (auto& c : p.clip.blendCurves) for (auto& v : c.values) v = std::clamp(v, 0.0f, 1.0f);
    p.clip.applyTo(p.rig, app.playTime);
    char buf[128]; std::snprintf(buf, sizeof buf, "%s: %d keys in %.2f-%.2f s", what, n, a, b); app.status = buf;
}

// Waveform min/max envelope cache (per pixel column) - rebuilt when the audio, zoom, scroll or width change.
struct WaveCache { const float* src = nullptr; size_t n = 0; float scroll = -1, vis = -1; int w = 0; std::vector<float> lo, hi; };
static WaveCache g_wave;
void buildWave(const AudioBuffer& a, float scroll, float vis, int w) {
    if (g_wave.src == a.samples.data() && g_wave.n == a.samples.size() && g_wave.scroll == scroll && g_wave.vis == vis && g_wave.w == w) return;
    g_wave = {a.samples.data(), a.samples.size(), scroll, vis, w, std::vector<float>(size_t(w), 0.0f), std::vector<float>(size_t(w), 0.0f)};
    if (a.samples.empty() || w <= 0) return;
    const size_t frames = a.frames(); const int ch = std::max(1, a.channels);
    for (int x = 0; x < w; ++x) {
        double t0 = scroll + vis * x / w, t1 = scroll + vis * (x + 1) / w;
        size_t f0 = size_t(std::clamp(t0 * a.sampleRate, 0.0, double(frames))), f1 = size_t(std::clamp(t1 * a.sampleRate, 0.0, double(frames)));
        if (f1 <= f0) f1 = std::min(frames, f0 + 1);
        float lo = 0, hi = 0; size_t step = std::max<size_t>(1, (f1 - f0) / 512);
        for (size_t f = f0; f < f1; f += step) { float v = a.samples[f * size_t(ch)]; lo = std::min(lo, v); hi = std::max(hi, v); }
        g_wave.lo[size_t(x)] = lo; g_wave.hi[size_t(x)] = hi;
    }
    // display normalisation against the whole clip's peak so quiet recordings are still readable
    float peak = 1e-3f; for (size_t f = 0; f < frames; f += 16) peak = std::max(peak, std::fabs(a.samples[f * size_t(ch)]));
    const float g = std::min(1.0f / peak, 20.0f);
    for (int x = 0; x < w; ++x) { g_wave.lo[size_t(x)] *= g; g_wave.hi[size_t(x)] *= g; }
}

void draw(Application& app, float x0, float x1, float yTop, float yBot) {
    Pipeline& p = app.pipe; AnimationClip& clip = p.clip;
    const std::vector<Channel> chans = channels(clip);
    if (chans.empty()) return;
    ui.tlCurve = std::clamp(ui.tlCurve, 0, int(chans.size()) - 1);
    const Channel& act = chans[size_t(ui.tlCurve)];
    ImGui::SetNextWindowPos(ImVec2(x0, yTop)); ImGui::SetNextWindowSize(ImVec2(x1 - x0, yBot - yTop));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * S(), 6 * S()));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 0.96f));
    ImGui::Begin("##timeline", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    // ---- header row: transport, time, zoom, channel chooser, mode
    if (PrimaryButton(app.playing ? ICON_MD_PAUSE : ICON_MD_PLAY_ARROW, ImVec2(28 * S(), 24 * S()))) app.playing = !app.playing;
    ImGui::SameLine(); if (WideButton(ICON_MD_STOP, ImVec2(28 * S(), 24 * S()))) { app.playing = false; app.playTime = 0; clip.applyTo(p.rig, 0); }
    ImGui::SameLine(); if (WideButton(ICON_MD_SKIP_PREVIOUS, ImVec2(28 * S(), 24 * S()))) { app.playTime = std::max(0.0f, app.playTime - 1.0f / clip.frameRate); clip.applyTo(p.rig, app.playTime); }
    ImGui::SameLine(); if (WideButton(ICON_MD_SKIP_NEXT, ImVec2(28 * S(), 24 * S()))) { app.playTime = std::min(clip.duration, app.playTime + 1.0f / clip.frameRate); clip.applyTo(p.rig, app.playTime); }
    ImGui::SameLine(); ImGui::Checkbox("Loop", &app.loop);
    ImGui::SameLine(); ImGui::TextColored(kAccent, "%6.2f s", app.playTime); ImGui::SameLine(); ImGui::TextColored(kTextDim, "f %d / %d", int(app.playTime * clip.frameRate + 0.5f), clip.frameCount() - 1);
    ImGui::SameLine(0, 16 * S()); ImGui::SetNextItemWidth(100 * S()); ImGui::SliderFloat("##zoom", &ui.tlZoom, 1.0f, 16.0f, "zoom %.1fx", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine(0, 12 * S()); ImGui::TextColored(kTextDim, "Channel");
    ImGui::SameLine(); ImGui::SetNextItemWidth(150 * S());
    if (ImGui::BeginCombo("##tlcurve", channelLabel(act).c_str())) {
        for (int pass = 0; pass < 2; ++pass) {
          ImGui::TextColored(kTextDim, pass == 0 ? "Bones (additive rotation, degrees)" : "Blend shapes (weight)");
          for (size_t i = 0; i < chans.size() && i < 128; ++i) {
            if (chans[i].bone != (pass == 0)) continue;
            ImGui::PushID(int(i)); ImGui::Checkbox("##vis", &ui.tlShow[i]); ImGui::SameLine();
            const KeyCurve* kc = clip.keyLayer.find(chans[i].target, chans[i].axis);
            std::string lbl = channelLabel(chans[i]) + (kc && !kc->keys.empty() ? "  [" + std::to_string(kc->keys.size()) + " keys]" : "");
            if (ImGui::Selectable(lbl.c_str(), int(i) == ui.tlCurve)) { ui.tlCurve = int(i); ui.tlShow[i] = true; ui.tlSelKey = -1; }
            ImGui::PopID();
          }
          if (pass == 0) ImGui::Separator();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Active channel is edited in the curve lane. Ticked channels are drawn (blend curves also get the range operations).\nBone axes edit an additive rotation in degrees on top of the generated head / jaw / eye motion.");
    ImGui::SameLine(0, 12 * S());
    ImGui::TextColored(kTextDim, "Mode"); ImGui::SameLine();
    if (ImGui::RadioButton("Paint", ui.tlMode == 0)) ui.tlMode = 0;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to overwrite the baked frame values (blend curves only)");
    ImGui::SameLine(); if (ImGui::RadioButton("Keys", ui.tlMode == 1)) ui.tlMode = 1; if (ImGui::IsItemHovered()) ImGui::SetTooltip("Non-destructive key layer: double-click adds a key, drag keys / tangent handles, Delete removes.\nComposite = baked + keys; regenerating the lip-sync keeps the keys.");
    ImGui::SameLine(); if (WideButton(ICON_MD_CLOSE, ImVec2(24 * S(), 24 * S()))) ui.timelineOpen = false;
    // second row: range operations (paint) or key tools (keys)
    if (ui.tlMode == 0) {
        ImGui::TextColored(kTextDim, "Range ops"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Act on the ticked blend curves inside the shift-drag selection (or the whole clip). Undoable.");
        ImGui::SameLine(0, 10 * S()); ImGui::SetNextItemWidth(80 * S()); ImGui::DragFloat("##gain", &ui.tlGain, 0.01f, 0.0f, 3.0f, "x %.2f");
        ImGui::SameLine(); if (WideButton("Gain", ImVec2(0, 24 * S()))) { float g = ui.tlGain; rangeApply(app, "Gain", [g](Curve<float>& c, size_t k) { c.values[k] *= g; }); }
        ImGui::SameLine(); ImGui::SetNextItemWidth(80 * S()); ImGui::DragFloat("##off", &ui.tlOffset, 0.01f, -1.0f, 1.0f, "%+.2f");
        ImGui::SameLine(); if (WideButton("Offset", ImVec2(0, 24 * S()))) { float o = ui.tlOffset; rangeApply(app, "Offset", [o](Curve<float>& c, size_t k) { c.values[k] += o; }); }
        ImGui::SameLine(); ImGui::SetNextItemWidth(70 * S()); ImGui::SliderInt("##sm", &ui.tlSmooth, 1, 6, "r=%d");
        ImGui::SameLine(); if (WideButton("Smooth", ImVec2(0, 24 * S()))) {
            int r = ui.tlSmooth;
            std::vector<std::vector<float>> src; for (auto& c : clip.blendCurves) src.push_back(c.values);
            rangeApply(app, "Smooth", [&src, &clip, r](Curve<float>& c, size_t k) {
                size_t ci = size_t(&c - clip.blendCurves.data()); const auto& v = src[ci]; float sum = 0; int n = 0;
                for (int d = -r; d <= r; ++d) { long j = long(k) + d; if (j >= 0 && j < long(v.size())) { sum += v[size_t(j)]; ++n; } }
                c.values[k] = n ? sum / float(n) : c.values[k]; });
        }
        ImGui::SameLine(); if (WideButton("Flatten", ImVec2(0, 24 * S()))) rangeApply(app, "Flatten", [](Curve<float>& c, size_t k) { c.values[k] = 0.0f; });
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zero the ticked curves inside the selection (shift-drag in a lane to select; click empty to clear).");
        ImGui::SameLine(); if (WideButton("Clear sel.", ImVec2(0, 24 * S()), ui.selA >= 0)) ui.selA = ui.selB = -1;
        if (ui.selA >= 0) { ImGui::SameLine(); ImGui::TextColored(kAccent, "%.2f - %.2f s", std::min(ui.selA, ui.selB), std::max(ui.selA, ui.selB)); }
    } else {
        KeyLayer& L = clip.keyLayer; KeyCurve* kc = L.find(act.target, act.axis);
        ImGui::TextColored(kTextDim, "Key layer"); ImGui::SameLine();
        if (ImGui::Checkbox("##len", &L.enabled)) clip.applyTo(p.rig, app.playTime);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mute / unmute the whole layer");
        ImGui::SameLine(); ImGui::SetNextItemWidth(90 * S()); if (ImGui::SliderFloat("##lw", &L.weight, 0.0f, 1.0f, "w %.2f")) clip.applyTo(p.rig, app.playTime);
        ImGui::SameLine(0, 10 * S());
        if (WideButton(ICON_MD_ADD "  Key", ImVec2(0, 24 * S()))) { app.pushUndo("add key"); ui.tlSelKey = L.get(act.target, act.axis).addKey(app.playTime, L.evaluate(act.target, act.axis, app.playTime)); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a key at the playhead on the active channel (K)");
        const bool haveSel = kc && ui.tlSelKey >= 0 && size_t(ui.tlSelKey) < kc->keys.size();
        ImGui::SameLine(); if (WideButton(ICON_MD_DELETE "  Key", ImVec2(0, 24 * S()), haveSel)) { app.pushUndo("delete key"); kc->removeKey(ui.tlSelKey); ui.tlSelKey = -1; L.prune(); clip.applyTo(p.rig, app.playTime); }
        ImGui::SameLine(0, 10 * S()); ImGui::TextColored(kTextDim, "Tangents");
        for (int m = 0; m < 4; ++m) {
            ImGui::SameLine();
            bool cur = haveSel && kc->keys[size_t(ui.tlSelKey)].inMode == TangentMode(m) && kc->keys[size_t(ui.tlSelKey)].outMode == TangentMode(m);
            if (cur) ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
            if (WideButton(tangentModeName(TangentMode(m)), ImVec2(0, 24 * S()), haveSel)) { app.pushUndo("tangent mode"); kc->keys[size_t(ui.tlSelKey)].broken = false; kc->setTangentMode(ui.tlSelKey, TangentMode(m)); clip.applyTo(p.rig, app.playTime); }
            if (cur) ImGui::PopStyleColor();
        }
        ImGui::SameLine(); bool br = haveSel && kc->keys[size_t(ui.tlSelKey)].broken;
        ImGui::BeginDisabled(!haveSel); if (ImGui::Checkbox("Broken", &br) && haveSel) { app.pushUndo("break tangents"); kc->keys[size_t(ui.tlSelKey)].broken = br; if (!br) { kc->keys[size_t(ui.tlSelKey)].inSlope = kc->keys[size_t(ui.tlSelKey)].outSlope; kc->updateTangents(); } } ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Edit the in and out tangent independently (drag a handle with Free tangents)");
        ImGui::SameLine(0, 10 * S()); if (WideButton("Flatten to baked", ImVec2(0, 24 * S()), !L.empty())) { app.pushUndo("flatten key layer"); clip = clip.flattened(); ui.tlSelKey = -1; clip.applyTo(p.rig, app.playTime); app.status = "Key layer merged into the baked curves"; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bake the key layer into the curves (exports do this implicitly)");
        ImGui::SameLine(); if (WideButton("Clear layer", ImVec2(0, 24 * S()), !L.empty())) { app.pushUndo("clear key layer"); L.clear(); ui.tlSelKey = -1; ui.tlBoxSel.clear(); clip.applyTo(p.rig, app.playTime); }
        ImGui::SameLine(0, 10 * S());
        if (WideButton(ICON_MD_FACE "  Pose as keys", ImVec2(0, 24 * S()))) {
            // key the difference between the rig's current pose (sliders / handles / gaze) and the baked clip, on every channel that differs
            app.pushUndo("pose as keys"); int n = 0; const float t = app.playTime;
            for (const auto& bc : clip.blendCurves) { float d = p.rig.blendWeight(bc.target) - bc.sample(t); if (std::fabs(d) > 1e-3f) { L.get(bc.target).addKey(t, d); ++n; } }
            for (const auto& bc : clip.boneRotations) { int b = p.rig.skeleton.find(bc.target); if (b < 0) continue; glm::vec3 e = glm::degrees(glm::eulerAngles(glm::normalize(glm::inverse(bc.sample(t)) * p.rig.skeleton.bones[size_t(b)].poseRotation))); for (int ax = 0; ax < 3; ++ax) if (std::fabs(e[ax]) > 0.05f) { L.get(bc.target, ax).addKey(t, e[ax]); ++n; } }
            app.status = n ? "Keyed " + std::to_string(n) + " channel(s) at " + std::to_string(t).substr(0, 4) + " s" : "Pose matches the clip - nothing to key";
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bake the current viewport pose (sliders, handles, gaze) as keys at the playhead, on every channel that differs from the clip.\nPose first (playback paused), then press this.");
        if (ui.tlBoxSel.size() > 1) { ImGui::SameLine(); ImGui::TextColored(kAccent, "%zu keys selected (drag to move, Del)", ui.tlBoxSel.size()); }
        if (haveSel) { const Key& k = kc->keys[size_t(ui.tlSelKey)]; ImGui::SameLine(); ImGui::TextColored(kAccent, "key %d: %.3f s  %+.3f  in %.2f out %.2f", ui.tlSelKey, k.time, k.value, k.inSlope, k.outSlope); }
    }

    // ---- lanes
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float labelW = 74 * S();
    ImVec2 area = ImGui::GetCursorScreenPos(); area.y += 2 * S();
    const float laneX0 = area.x + labelW, laneX1 = x1 - 10 * S(), laneW = std::max(10.0f, laneX1 - laneX0);
    const float visibleSec = clip.duration / ui.tlZoom;
    ui.tlScroll = std::clamp(ui.tlScroll, 0.0f, std::max(0.0f, clip.duration - visibleSec));
    if (app.playing && (app.playTime < ui.tlScroll || app.playTime > ui.tlScroll + visibleSec)) ui.tlScroll = std::clamp(app.playTime - visibleSec * 0.1f, 0.0f, std::max(0.0f, clip.duration - visibleSec));
    auto tx = [&](double t) { return laneX0 + float((t - ui.tlScroll) / visibleSec) * laneW; };
    auto xt = [&](float x) { return std::clamp(ui.tlScroll + (x - laneX0) / laneW * visibleSec, 0.0f, clip.duration); };
    const ImU32 colLane = IM_COL32(28, 28, 28, 255), colGrid = IM_COL32(60, 60, 60, 255), colText = ImGui::ColorConvertFloat4ToU32(kText), colDim = ImGui::ColorConvertFloat4ToU32(kTextDim);
    const ImU32 colAccent = ImGui::ColorConvertFloat4ToU32(kAccent), colSel = IM_COL32(139, 199, 46, 40), colWave = IM_COL32(90, 160, 200, 200), colBaked = IM_COL32(150, 150, 150, 160);
    float y = area.y;
    const float rulerH = 16 * S(), laneH = 18 * S(), waveH = 34 * S(), curveH = std::max(40.0f, yBot - 8 * S() - y - rulerH - waveH - 3 * laneH - 5 * S());
    // ruler
    dl->AddRectFilled(ImVec2(laneX0, y), ImVec2(laneX1, y + rulerH), colLane);
    {
        float step = 0.1f; while (visibleSec / step > laneW / (44 * S())) step *= (std::fabs(std::fmod(std::log10(step), 1.0f)) < 1e-3f ? 2.0f : 2.5f);
        for (float t = std::floor(ui.tlScroll / step) * step; t <= ui.tlScroll + visibleSec + 1e-4f; t += step) {
            float x = tx(t); if (x < laneX0 - 1 || x > laneX1 + 1) continue;
            dl->AddLine(ImVec2(x, y + rulerH - 5 * S()), ImVec2(x, y + rulerH), colGrid);
            char b[16]; std::snprintf(b, sizeof b, "%.2f", t); dl->AddText(ui.fonts.small, ui.fonts.small->FontSize, ImVec2(x + 2, y), colDim, b);
        }
    }
    y += rulerH + 1 * S();
    // audio waveform lane (min/max envelope per pixel + onset ticks)
    {
        dl->AddText(ImVec2(area.x, y + 10 * S()), colDim, "Audio");
        dl->AddRectFilled(ImVec2(laneX0, y), ImVec2(laneX1, y + waveH), colLane);
        const float mid = y + waveH * 0.5f, amp = waveH * 0.48f;
        if (p.audio.samples.empty()) dl->AddText(ui.fonts.small, ui.fonts.small->FontSize, ImVec2(laneX0 + 6 * S(), y + 10 * S()), colDim, "no audio loaded");
        else {
            buildWave(p.audio, ui.tlScroll, visibleSec, int(laneW));
            dl->AddLine(ImVec2(laneX0, mid), ImVec2(laneX1, mid), colGrid);
            for (int x = 0; x < int(laneW); ++x) { float lo = g_wave.lo[size_t(x)], hi = g_wave.hi[size_t(x)]; if (hi - lo < 0.004f) { lo -= 0.002f; hi += 0.002f; } dl->AddLine(ImVec2(laneX0 + x, mid - hi * amp), ImVec2(laneX0 + x, mid - lo * amp), colWave); }
            for (const auto& f : p.features.frames) if (f.onset) { float x = tx(f.time); if (x >= laneX0 && x <= laneX1) dl->AddLine(ImVec2(x, y), ImVec2(x, y + 5 * S()), colAccent, 1.5f * S()); }
        }
        y += waveH + 1 * S();
    }
    // word / phone / viseme lanes
    struct Lane { const char* name; int kind; }; const Lane lanes[3] = {{"Words", 0}, {"Phones", 1}, {"Visemes", 2}};
    const ImU32 visCol[] = {IM_COL32(70, 70, 70, 255), IM_COL32(200, 120, 60, 255), IM_COL32(80, 170, 220, 255), IM_COL32(110, 150, 230, 255), IM_COL32(220, 90, 120, 255), IM_COL32(200, 70, 190, 255), IM_COL32(230, 200, 60, 255), IM_COL32(120, 210, 120, 255), IM_COL32(170, 170, 90, 255)};
    for (const Lane& L : lanes) {
        dl->AddText(ImVec2(area.x, y + 2 * S()), colDim, L.name);
        dl->AddRectFilled(ImVec2(laneX0, y), ImVec2(laneX1, y + laneH), colLane);
        auto block = [&](double t0, double t1, const char* label, ImU32 col, bool outline) {
            float xa = std::max(tx(t0), laneX0), xb = std::min(tx(t1), laneX1); if (xb - xa < 1) return;
            if (outline) { dl->AddRectFilled(ImVec2(xa, y + 2 * S()), ImVec2(xb, y + laneH - 2 * S()), IM_COL32(50, 50, 50, 255), 3 * S()); dl->AddRect(ImVec2(xa, y + 2 * S()), ImVec2(xb, y + laneH - 2 * S()), col, 3 * S()); }
            else dl->AddRectFilled(ImVec2(xa, y + 2 * S()), ImVec2(xb, y + laneH - 2 * S()), col, 2 * S());
            ImVec2 ts = ImGui::CalcTextSize(label);
            if (ts.x + 6 * S() < xb - xa) { dl->PushClipRect(ImVec2(xa, y), ImVec2(xb, y + laneH), true); dl->AddText(ui.fonts.small, ui.fonts.small->FontSize, ImVec2(xa + 3 * S(), y + 3 * S()), outline ? colText : IM_COL32(15, 15, 15, 255), label); dl->PopClipRect(); }
        };
        if (L.kind == 0) {
            const auto& al = p.lastAlignment;
            if (al.phones.empty()) dl->AddText(ui.fonts.small, ui.fonts.small->FontSize, ImVec2(laneX0 + 6 * S(), y + 3 * S()), colDim, "no transcript - enter one in step 4 to see words and phones");
            for (size_t w = 0; w < al.words.size(); ++w) {
                double t0 = -1, t1 = -1; for (const auto& ph : al.phones) if (ph.wordIndex == int(w)) { if (t0 < 0) t0 = ph.start; t1 = ph.end; }
                if (t0 >= 0) block(t0, t1, al.words[w].text.c_str(), colAccent, true);
            }
        } else if (L.kind == 1) {
            for (const auto& ph : p.lastAlignment.phones) if (ph.wordIndex >= 0) block(ph.start, ph.end, ph.phone.c_str(), visCol[size_t(ph.viseme) % 9], false);
        } else {
            for (const auto& sg : p.lastSegments) if (sg.viseme != Viseme::Silence) block(sg.start, sg.end, visemeName(sg.viseme), visCol[size_t(sg.viseme) % 9], false);
        }
        y += laneH + 1 * S();
    }
    // curve lane: value range depends on the active channel (weights 0..1, degrees -range..+range)
    const float cy0 = y, cy1 = y + curveH;
    float vMin = 0.0f, vMax = 1.0f;
    if (act.bone) {
        float m = 5.0f; const auto& bc = clip.boneRotations[size_t(act.bakedIndex)];
        for (size_t k = 0; k < bc.times.size(); k += 2) m = std::max(m, std::fabs(compositeAt(clip, act, bc.times[k])));
        if (const KeyCurve* kc = clip.keyLayer.find(act.target, act.axis)) for (const Key& k : kc->keys) m = std::max(m, std::fabs(compositeAt(clip, act, k.time)));
        m = std::ceil(m / 5.0f) * 5.0f; vMin = -m; vMax = m;
    }
    auto vy = [&](float v) { return cy1 - 1 * S() - std::clamp((v - vMin) / (vMax - vMin), 0.0f, 1.0f) * (curveH - 2 * S()); };
    auto yv = [&](float yy) { return vMin + std::clamp((cy1 - 1 * S() - yy) / (curveH - 2 * S()), 0.0f, 1.0f) * (vMax - vMin); };
    dl->AddRectFilled(ImVec2(laneX0, cy0), ImVec2(laneX1, cy1), colLane);
    for (int g = 1; g < 4; ++g) dl->AddLine(ImVec2(laneX0, cy1 - curveH * 0.25f * g), ImVec2(laneX1, cy1 - curveH * 0.25f * g), colGrid);
    if (act.bone) dl->AddLine(ImVec2(laneX0, vy(0)), ImVec2(laneX1, vy(0)), IM_COL32(90, 90, 90, 255));
    { char b[16]; std::snprintf(b, sizeof b, act.bone ? "%+.0f" : "%.1f", vMax); dl->AddText(ui.fonts.small, ui.fonts.small->FontSize, ImVec2(area.x, cy0), colDim, b); std::snprintf(b, sizeof b, act.bone ? "%+.0f" : "%.1f", vMin); dl->AddText(ui.fonts.small, ui.fonts.small->FontSize, ImVec2(area.x, cy1 - 12 * S()), colDim, b); }
    if (ui.selA >= 0) dl->AddRectFilled(ImVec2(std::max(tx(std::min(ui.selA, ui.selB)), laneX0), area.y), ImVec2(std::min(tx(std::max(ui.selA, ui.selB)), laneX1), cy1), colSel);
    // channels: sample the composite at frame resolution across the visible range
    const int nCols = std::max(2, int(laneW / (2 * S())));
    for (size_t ci = 0; ci < chans.size() && ci < 128; ++ci) {
        if (!shown(ci)) continue;
        const Channel& c = chans[ci]; const bool active = int(ci) == ui.tlCurve;
        if (c.bone != act.bone && !active) continue;   // different units: only draw same-kind channels together
        ImU32 col = active ? colAccent : IM_COL32(140 + 40 * (ci % 3), 120 + 30 * (ci % 4), 200 - 30 * (ci % 5), 130);
        const bool hasLayer = clip.keyLayer.find(c.target, c.axis) != nullptr && clip.keyLayer.enabled;
        ImVec2 prev, prevB; bool have = false;
        for (int i = 0; i <= nCols; ++i) {
            float t = ui.tlScroll + visibleSec * i / nCols; if (t > clip.duration) break;
            ImVec2 pt(tx(t), vy(compositeAt(clip, c, t))), pb(tx(t), vy(bakedAt(clip, c, t)));
            if (have) { if (active && hasLayer) dl->AddLine(prevB, pb, colBaked, 1.0f * S()); dl->AddLine(prev, pt, col, active ? 2.0f * S() : 1.0f * S()); }
            prev = pt; prevB = pb; have = true;
        }
        if (active) { std::string lbl = channelLabel(c) + (hasLayer ? "   (grey = baked)" : ""); ImVec2 ts = ImGui::CalcTextSize(lbl.c_str()); dl->AddText(ImVec2(laneX1 - ts.x - 6 * S(), cy0 + 3 * S()), col, lbl.c_str()); }
    }
    // keys of the active channel (key mode) with tangent handles on the selected one
    const float handleLen = 40 * S(); int hotKey = -1, hotHandle = 0;   // hotHandle: -1 in, +1 out
    KeyCurve* akc = clip.keyLayer.find(act.target, act.axis);
    ImGuiIO& io = ImGui::GetIO();
    if (ui.tlMode == 1 && akc) {
        for (size_t k = 0; k < akc->keys.size(); ++k) {
            const Key& key = akc->keys[k];
            ImVec2 pt(tx(key.time), vy(compositeAt(clip, act, key.time)));
            if (pt.x < laneX0 - 6 || pt.x > laneX1 + 6) continue;
            const bool sel = int(k) == ui.tlSelKey;
            if (sel) {
                // tangent handles: slope in value/sec -> screen direction
                const float pxPerSec = laneW / visibleSec, pxPerVal = (curveH - 2 * S()) / (vMax - vMin);
                auto hdir = [&](float slope, float sign) { ImVec2 d(sign * pxPerSec, -sign * slope * pxPerVal); float l = std::sqrt(d.x * d.x + d.y * d.y); return ImVec2(pt.x + d.x / l * handleLen, pt.y + d.y / l * handleLen); };
                ImVec2 hi = hdir(key.inSlope, -1.0f), ho = hdir(key.outSlope, 1.0f);
                dl->AddLine(pt, hi, IM_COL32(255, 200, 80, 220), 1.0f * S()); dl->AddLine(pt, ho, IM_COL32(255, 200, 80, 220), 1.0f * S());
                dl->AddCircleFilled(hi, 4 * S(), IM_COL32(255, 200, 80, 255)); dl->AddCircleFilled(ho, 4 * S(), IM_COL32(255, 200, 80, 255));
                auto near = [&](ImVec2 a) { float dx = io.MousePos.x - a.x, dy = io.MousePos.y - a.y; return dx * dx + dy * dy < 36 * S() * S(); };
                if (near(hi)) { hotKey = int(k); hotHandle = -1; } else if (near(ho)) { hotKey = int(k); hotHandle = 1; }
            }
            const bool boxSel = std::find(ui.tlBoxSel.begin(), ui.tlBoxSel.end(), int(k)) != ui.tlBoxSel.end();
            dl->AddRectFilled(ImVec2(pt.x - 4 * S(), pt.y - 4 * S()), ImVec2(pt.x + 4 * S(), pt.y + 4 * S()), sel || boxSel ? IM_COL32(255, 255, 255, 255) : colAccent, 1.0f);
            dl->AddRect(ImVec2(pt.x - 4 * S(), pt.y - 4 * S()), ImVec2(pt.x + 4 * S(), pt.y + 4 * S()), IM_COL32(0, 0, 0, 255), 1.0f);
            if (hotKey < 0) { float dx = io.MousePos.x - pt.x, dy = io.MousePos.y - pt.y; if (dx * dx + dy * dy < 49 * S() * S()) { hotKey = int(k); hotHandle = 0; } }
        }
    }
    if (ui.tlDrag == 4) { ImVec2 a(std::min(ui.tlBoxA.x, ui.tlBoxB.x), std::min(ui.tlBoxA.y, ui.tlBoxB.y)), b(std::max(ui.tlBoxA.x, ui.tlBoxB.x), std::max(ui.tlBoxA.y, ui.tlBoxB.y)); dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 25)); dl->AddRect(a, b, IM_COL32(255, 255, 255, 160)); }
    // playhead
    { float px = tx(app.playTime); if (px >= laneX0 && px <= laneX1) { dl->AddLine(ImVec2(px, area.y), ImVec2(px, cy1), IM_COL32(255, 255, 255, 220), 1.5f * S()); dl->AddTriangleFilled(ImVec2(px - 5 * S(), area.y), ImVec2(px + 5 * S(), area.y), ImVec2(px, area.y + 7 * S()), IM_COL32(255, 255, 255, 220)); } }

    // ---- interaction: invisible button over all lanes
    ImGui::SetCursorScreenPos(ImVec2(laneX0, area.y));
    ImGui::InvisibleButton("##lanes", ImVec2(laneW, cy1 - area.y));
    const bool hovered = ImGui::IsItemHovered(), activeBtn = ImGui::IsItemActive();
    if (hovered && io.MouseWheel != 0.0f) {
        if (io.KeyCtrl) { float tAt = xt(io.MousePos.x); ui.tlZoom = std::clamp(ui.tlZoom * (io.MouseWheel > 0 ? 1.25f : 0.8f), 1.0f, 16.0f); float vs = clip.duration / ui.tlZoom; ui.tlScroll = std::clamp(tAt - (io.MousePos.x - laneX0) / laneW * vs, 0.0f, std::max(0.0f, clip.duration - vs)); }
        else ui.tlScroll = std::clamp(ui.tlScroll - io.MouseWheel * visibleSec * 0.1f, 0.0f, std::max(0.0f, clip.duration - visibleSec));
    }
    const bool inCurve = io.MousePos.y >= cy0 && io.MousePos.y <= cy1;
    // key mode shortcuts
    if (ui.tlMode == 1 && hovered && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_K)) { app.pushUndo("add key"); ui.tlSelKey = clip.keyLayer.get(act.target, act.axis).addKey(app.playTime, clip.keyLayer.evaluate(act.target, act.axis, app.playTime)); }
        if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && akc) {
            std::vector<int> del = ui.tlBoxSel; if (del.empty() && ui.tlSelKey >= 0) del.push_back(ui.tlSelKey);
            if (!del.empty()) { app.pushUndo(del.size() > 1 ? "delete keys" : "delete key"); std::sort(del.rbegin(), del.rend()); for (int i : del) akc->removeKey(i); ui.tlSelKey = -1; ui.tlBoxSel.clear(); clip.keyLayer.prune(); clip.applyTo(p.rig, app.playTime); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_A) && io.KeyCtrl && akc) { ui.tlBoxSel.clear(); for (size_t k = 0; k < akc->keys.size(); ++k) ui.tlBoxSel.push_back(int(k)); }
    }
    if (ImGui::IsItemDeactivated()) { if (ui.tlDrag == 5 && akc) { /* keys of the moved selection may have crossed unselected ones: reselect by time proximity is overkill - just keep indices */ } ui.tlDrag = 0; }
    if (ImGui::IsItemActivated()) {
        ui.tlPainting = false; ui.tlDrag = 0;
        if (io.KeyShift) { ui.selA = ui.selB = xt(io.MousePos.x); }
        else if (inCurve && ui.tlMode == 1 && !io.KeyAlt) {
            app.playing = false;
            const bool inBox = hotKey >= 0 && std::find(ui.tlBoxSel.begin(), ui.tlBoxSel.end(), hotKey) != ui.tlBoxSel.end();
            if (hotKey >= 0 && hotHandle != 0) { app.pushUndo("edit tangent"); ui.tlSelKey = hotKey; ui.tlDrag = hotHandle; ui.tlBoxSel.clear(); }
            else if (inBox && ui.tlBoxSel.size() > 1) { app.pushUndo("move keys"); ui.tlSelKey = hotKey; ui.tlDrag = 5; ui.tlMoveLastT = xt(io.MousePos.x); }
            else if (hotKey >= 0) { app.pushUndo("move key"); ui.tlSelKey = hotKey; ui.tlDrag = 2; ui.tlBoxSel.clear(); }
            else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                app.pushUndo("add key"); KeyCurve& kc = clip.keyLayer.get(act.target, act.axis);
                float t = xt(io.MousePos.x); ui.tlSelKey = kc.addKey(t, yv(io.MousePos.y) - bakedAt(clip, act, t)); ui.tlDrag = 2; clip.applyTo(p.rig, app.playTime);
            } else { ui.tlSelKey = -1; ui.tlBoxSel.clear(); ui.tlDrag = 4; ui.tlBoxA = ui.tlBoxB = io.MousePos; }
        }
        else if (inCurve && !act.bone && ui.tlMode == 0 && !io.KeyAlt) {
            app.pushUndo("paint curve"); ui.tlPainting = true; ui.tlLastT = xt(io.MousePos.x); ui.tlLastV = std::clamp(yv(io.MousePos.y), 0.0f, 1.0f);
            app.playing = false;
        } else { ui.selA = ui.selB = -1; app.playing = false; ui.tlDrag = 3; }
    }
    if (activeBtn) {
        float t = xt(io.MousePos.x);
        if (io.KeyShift && ui.selA >= 0 && !ui.tlPainting && ui.tlDrag == 0) ui.selB = t;
        else if (ui.tlPainting) {
            auto& c = clip.blendCurves[size_t(act.bakedIndex)];
            float v = std::clamp(yv(io.MousePos.y), 0.0f, 1.0f);
            float ta = std::min(ui.tlLastT, t), tb = std::max(ui.tlLastT, t);
            for (size_t k = 0; k < c.times.size(); ++k) {
                float ct = c.times[k]; if (ct < ta - 1e-4f || ct > tb + 1e-4f) continue;
                float u = tb - ta > 1e-5f ? (ct - ta) / (tb - ta) : 1.0f; if (t < ui.tlLastT) u = 1.0f - u;
                c.values[k] = ui.tlLastV + (v - ui.tlLastV) * u;
            }
            ui.tlLastT = t; ui.tlLastV = v; app.playTime = t; clip.applyTo(p.rig, t);
        } else if (ui.tlDrag == 2 && akc && ui.tlSelKey >= 0 && size_t(ui.tlSelKey) < akc->keys.size()) {
            // move key: value = composite target - baked at the new time; time snaps to frames unless Ctrl
            float nt = io.KeyCtrl ? t : std::round(t * clip.frameRate) / clip.frameRate;
            ui.tlSelKey = akc->moveKey(ui.tlSelKey, nt);
            akc->keys[size_t(ui.tlSelKey)].value = yv(io.MousePos.y) - bakedAt(clip, act, nt); akc->updateTangents();
            app.playTime = nt; clip.applyTo(p.rig, nt);
        } else if ((ui.tlDrag == -1 || ui.tlDrag == 1) && akc && ui.tlSelKey >= 0 && size_t(ui.tlSelKey) < akc->keys.size()) {
            Key& key = akc->keys[size_t(ui.tlSelKey)];
            ImVec2 pt(tx(key.time), vy(compositeAt(clip, act, key.time)));
            float dx = (io.MousePos.x - pt.x) * float(ui.tlDrag), dy = -(io.MousePos.y - pt.y) * float(ui.tlDrag);
            const float pxPerSec = laneW / visibleSec, pxPerVal = (curveH - 2 * S()) / (vMax - vMin);
            if (dx > 2.0f) {
                float slope = (dy / pxPerVal) / (dx / pxPerSec);
                if (ui.tlDrag < 0) { key.inMode = TangentMode::Free; key.inSlope = slope; if (!key.broken) { key.outMode = TangentMode::Free; key.outSlope = slope; } }
                else { key.outMode = TangentMode::Free; key.outSlope = slope; if (!key.broken) { key.inMode = TangentMode::Free; key.inSlope = slope; } }
                akc->updateTangents(); clip.applyTo(p.rig, app.playTime);
            }
        } else if (ui.tlDrag == 4) {
            ui.tlBoxB = io.MousePos; ui.tlBoxSel.clear();
            if (akc) { float xa = std::min(ui.tlBoxA.x, ui.tlBoxB.x), xb = std::max(ui.tlBoxA.x, ui.tlBoxB.x), ya = std::min(ui.tlBoxA.y, ui.tlBoxB.y), yb = std::max(ui.tlBoxA.y, ui.tlBoxB.y);
                for (size_t k = 0; k < akc->keys.size(); ++k) { ImVec2 pt(tx(akc->keys[k].time), vy(compositeAt(clip, act, akc->keys[k].time))); if (pt.x >= xa && pt.x <= xb && pt.y >= ya && pt.y <= yb) ui.tlBoxSel.push_back(int(k)); } }
            // a tiny box = a click on empty space: scrub instead
            if (std::fabs(ui.tlBoxB.x - ui.tlBoxA.x) < 3 && std::fabs(ui.tlBoxB.y - ui.tlBoxA.y) < 3) { app.playTime = t; clip.applyTo(p.rig, t); }
        } else if (ui.tlDrag == 5 && akc && !ui.tlBoxSel.empty()) {
            // move the whole selection in time (frame-snapped delta) keeping relative spacing; indices stay valid because order is preserved
            float dt = (io.KeyCtrl ? t : std::round(t * clip.frameRate) / clip.frameRate) - (io.KeyCtrl ? ui.tlMoveLastT : std::round(ui.tlMoveLastT * clip.frameRate) / clip.frameRate);
            if (std::fabs(dt) > 1e-6f) {
                float lo = 1e9f, hi = -1e9f; for (int i : ui.tlBoxSel) { lo = std::min(lo, akc->keys[size_t(i)].time); hi = std::max(hi, akc->keys[size_t(i)].time); }
                dt = std::clamp(dt, -lo, clip.duration - hi);
                for (int i : ui.tlBoxSel) akc->keys[size_t(i)].time += dt;
                std::sort(akc->keys.begin(), akc->keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; }); akc->updateTangents();
                ui.tlMoveLastT = t; clip.applyTo(p.rig, app.playTime);
            }
        } else if (ui.tlDrag == 3 || ui.tlDrag == 0) { app.playTime = t; clip.applyTo(p.rig, t); }
    }
    if (hovered && !activeBtn) {
        float t = xt(io.MousePos.x); char b[160];
        const char* ph = ""; for (const auto& x : p.lastAlignment.phones) if (t >= x.start && t < x.end) { ph = x.phone.c_str(); break; }
        const char* vs = ""; for (const auto& x : p.lastSegments) if (t >= x.start && t < x.end) { vs = visemeName(x.viseme); break; }
        std::snprintf(b, sizeof b, "%.3f s  f %d%s%s%s%s   %s = %.3f", t, int(t * clip.frameRate + 0.5f), *ph ? "   phone " : "", ph, *vs ? "   viseme " : "", vs, channelLabel(act).c_str(), compositeAt(clip, act, t));
        if (ui.tlMode == 1) ImGui::SetTooltip("%s\nDouble-click: add key  |  drag key / orange handles: move / tangent  |  drag empty: box-select keys, then drag them together  |  K: key at playhead  |  Del: remove  |  Ctrl+A: all\nShift-drag: range  |  Alt-drag / drag ruler: scrub  |  Ctrl+wheel: zoom  |  Ctrl while dragging: no frame snap", b);
        else ImGui::SetTooltip("%s\nDrag in the curve lane to paint  |  Shift-drag: select range  |  Alt-drag / drag ruler: scrub  |  Ctrl+wheel: zoom", b);
    }
    ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar();
}
} // namespace timeline

// AccuRIG-style modal: "Export FBX..." / "Export glTF..." buttons.
void exportDialog(Application& app) {
    if (ui.showProjectDialog) { ImGui::OpenPopup("Project"); }
    if (ImGui::BeginPopupModal("Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        const bool open = ui.showProjectDialog == 1;
        ImGui::TextUnformatted(open ? "Open a project (.frproj): model, audio, rig edits, settings and clip are restored." : "Save the whole session as one JSON project file (.frproj).");
        ImGui::SetNextItemWidth(360 * S()); ImGui::InputText("##proj", ui.projectBuf, sizeof ui.projectBuf);
        if (PrimaryButton(open ? ICON_MD_FOLDER_OPEN "  Open" : ICON_MD_SAVE "  Save", ImVec2(-1, 34 * S()))) {
            bool ok = open ? app.loadProject(ui.projectBuf) : app.saveProject(ui.projectBuf);
            if (ok && open) { for (int i = 0; i < StepCount; ++i) ui.stepDone[i] = true; ui.step = app.pipe.clip.duration > 0 ? StepAnim : StepRig; std::strncpy(ui.modelBuf, app.pipe.modelPath.c_str(), sizeof ui.modelBuf - 1); std::strncpy(ui.audioBuf, app.pipe.audioPath.c_str(), sizeof ui.audioBuf - 1); }
            ui.showProjectDialog = 0; ImGui::CloseCurrentPopup();
        }
        if (WideButton("Cancel", ImVec2(-1, 28 * S()))) { ui.showProjectDialog = 0; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
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
        if (WideButton(ICON_MD_FILE_DOWNLOAD "  Export clip JSON (curves + ARKit names)...", ImVec2(-1, 36 * S()))) { app.exportNow(withExt("json"), vars()); ui.stepDone[StepAnim] = true; ImGui::CloseCurrentPopup(); }
        if (WideButton(ICON_MD_FACE "  Export ARKit mocap CSV (52 coefficients, 60 fps)...", ImVec2(-1, 36 * S()))) { app.exportNow(withExt("csv"), vars()); ui.stepDone[StepAnim] = true; ImGui::CloseCurrentPopup(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Same layout as Live Link Face / Face Cap recordings: Timecode, BlendShapeCount, EyeBlinkLeft ... RightEyeRoll");
        ImGui::Spacing();
        ImGui::Checkbox("Ship audio with the export (.wav sidecar + offsets)", &app.pipe.exportAudioSidecar);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Writes <out>.wav next to the file and records its start offset in glTF asset.extras.audio / a .audio.json manifest for FBX and CSV");
        ImGui::BeginDisabled(!app.pipe.exportAudioSidecar);
        ImGui::Checkbox("Embed WAV inside .glb", &app.pipe.embedAudioInGlb);
        ImGui::EndDisabled();
        ImGui::Spacing();
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
    if (iconBtn(ICON_MD_BRUSH, app.tool == Application::Tool::PaintWeights, "Paint skin weights (R)  [ ] = radius")) { app.tool = Application::Tool::PaintWeights; app.meshRenderer.shadeMode = MeshRenderer::ShadeMode::BoneWeights; app.meshRenderer.heatBone = app.brush.bone; }
    ImGui::Dummy(ImVec2(0, 6 * S()));
    if (iconBtn(ICON_MD_GRID_ON, app.meshRenderer.wireframe, "Wireframe")) app.meshRenderer.wireframe = !app.meshRenderer.wireframe;
    { using SM = MeshRenderer::ShadeMode; auto& sm = app.meshRenderer.shadeMode;
      static const char* tips[] = {"Shading: Lit (click to cycle)", "Shading: Normals", "Shading: Bone weights", "Shading: Blendshape influence", "Shading: Displacement"};
      if (iconBtn(ICON_MD_GRADIENT, sm != SM::Lit, tips[int(sm)])) sm = SM((int(sm) + 1) % 5); }
    if (iconBtn(ICON_MD_ACCESSIBILITY_NEW, app.showBones, "Show bones")) app.showBones = !app.showBones;
    if (iconBtn(ICON_MD_HIGHLIGHT_ALT, app.showPoints, "Show control points")) app.showPoints = !app.showPoints;
    if (iconBtn(ICON_MD_LABEL, app.showLabels, "Show labels")) app.showLabels = !app.showLabels;
    ImGui::Dummy(ImVec2(0, 6 * S()));
    { std::string t = std::string("Undo ") + app.undoLabel() + " (Ctrl+Z)"; if (iconBtn(ICON_MD_UNDO, false, t.c_str())) app.undo(); }
    { std::string t = std::string("Redo ") + app.redoLabel() + " (Ctrl+Y)"; if (iconBtn(ICON_MD_REDO, false, t.c_str())) app.redo(); }
    ImGui::Dummy(ImVec2(0, 6 * S()));
    if (iconBtn(ICON_MD_CENTER_FOCUS_STRONG, false, "Frame the face")) { app.camera.target = 0.5f * (r.mesh.boundsMin() + r.mesh.boundsMax()); app.camera.distance = 2.2f * glm::length(e); app.camera.yaw = app.camera.pitch = 0; }
    ImGui::End();
    // status line bottom-left of viewport
    if (!app.status.empty()) {
        float lift = (app.pipe.clip.duration > 0 && ui.step == StepAnim && ui.timelineOpen) ? timeline::tlHeight() : 0.0f;
        ImVec2 sp(x0 + 14 * S(), vp->WorkPos.y + vp->WorkSize.y - 22 * S() - lift);
        dl->AddText(ui.fonts.small, ui.fonts.small->FontSize, sp, ImGui::ColorConvertFloat4ToU32(kAccent), app.status.c_str());
    }
    // timeline editor across the viewport bottom on the animation step; compact bar elsewhere
    if (app.pipe.clip.duration > 0 && ui.step == StepAnim && ui.timelineOpen) {
        timeline::draw(app, x0, x1, vp->WorkPos.y + vp->WorkSize.y - timeline::tlHeight(), vp->WorkPos.y + vp->WorkSize.y);
    } else if (app.pipe.clip.duration > 0 && ui.step != StepCheck) {
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
void setRigTab(int tab) { ui.rigTabRequest = tab; }
void setTimelineChannel(int channel) { ui.tlCurve = channel; if (channel >= 0 && channel < 128) ui.tlShow[channel] = true; }
void setTimelineKeyMode(int mode, int selectedKey) { ui.tlMode = mode; ui.tlSelKey = selectedKey; ui.timelineOpen = true; }
bool viewportContains(float x, float y) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    return x >= vp->WorkPos.x + kLeftWidth * S() && x <= vp->WorkPos.x + vp->WorkSize.x - kRightWidth * S() && y >= vp->WorkPos.y;
}

void drawPanels(Application& app) {
    if (!ui.init) { initPanels(app, 1.0f); if (app.projectLoadedOnStart) { for (int i = 0; i < StepCount; ++i) ui.stepDone[i] = true; ui.step = app.pipe.clip.duration > 0 ? StepAnim : StepRig; std::strncpy(ui.modelBuf, app.pipe.modelPath.c_str(), sizeof ui.modelBuf - 1); std::strncpy(ui.audioBuf, app.pipe.audioPath.c_str(), sizeof ui.audioBuf - 1); std::strncpy(ui.projectBuf, app.options().projectPath.c_str(), sizeof ui.projectBuf - 1); } }
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

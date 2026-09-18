#pragma once
namespace fr {
class Application;
/// Must be called once after ImGui::CreateContext (loads Inter, applies the AccuRIG-style theme).
void initPanels(Application& app, float uiScale);
/// Draws the whole shell: menu bar, left step wizard, right property page, viewport overlays.
void drawPanels(Application& app);
/// Screen-space labels for control points / bones (drawn into the background draw list).
void drawOverlayLabels(Application& app);
/// Workflow notifications so the wizard advances when actions happen from code / CLI flags.
void notifyModelLoaded(bool ok);
void notifyRigBuilt();
void notifyClipGenerated();
void setStep(int step);
void setRigTab(int tab);
void setTimelineKeyMode(int mode, int selectedKey);
void setTimelineChannel(int channel);   ///< 0 paint / 1 keys (used by --keys-demo)   ///< 0 handles, 1 weights, 2 correctives, 3 blendshapes, 4 bones
/// True when window pixel (x, y) lies over the 3D viewport (not the side columns).
bool viewportContains(float x, float y);
} // namespace fr

#pragma once
namespace fr {
class Application;
/// Draws all ImGui panels (toolbar, control points, rig, audio/animation, export, log) for the app.
void drawPanels(Application& app);
/// Screen-space labels for control points / bones (drawn into the background draw list).
void drawOverlayLabels(Application& app);
} // namespace fr

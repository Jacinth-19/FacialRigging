#pragma once
#include <imgui.h>
#include <string>
#include "ui/icons_material.h"

namespace fr::theme {

// AccuRIG-style palette: near-black chrome, dark-grey panels, single lime-green accent.
inline const ImVec4 kBg        = ImVec4(0.110f, 0.110f, 0.110f, 1.0f);  // #1c1c1c window chrome
inline const ImVec4 kPanel     = ImVec4(0.153f, 0.153f, 0.153f, 1.0f);  // #272727 panels
inline const ImVec4 kPanelAlt  = ImVec4(0.196f, 0.196f, 0.196f, 1.0f);  // #323232 cards / frames
inline const ImVec4 kPanelHi   = ImVec4(0.245f, 0.245f, 0.245f, 1.0f);  // #3e3e3e hover
inline const ImVec4 kBorder    = ImVec4(0.080f, 0.080f, 0.080f, 1.0f);
inline const ImVec4 kText      = ImVec4(0.870f, 0.870f, 0.870f, 1.0f);
inline const ImVec4 kTextDim   = ImVec4(0.560f, 0.560f, 0.560f, 1.0f);
inline const ImVec4 kTextMute  = ImVec4(0.380f, 0.380f, 0.380f, 1.0f);
inline const ImVec4 kAccent    = ImVec4(0.545f, 0.780f, 0.180f, 1.0f);  // #8bc72e lime
inline const ImVec4 kAccentHi  = ImVec4(0.620f, 0.860f, 0.240f, 1.0f);
inline const ImVec4 kAccentLo  = ImVec4(0.420f, 0.620f, 0.130f, 1.0f);
inline const ImVec4 kAccentDim = ImVec4(0.545f, 0.780f, 0.180f, 0.35f);
inline const ImVec4 kWarn      = ImVec4(0.95f, 0.72f, 0.25f, 1.0f);
inline const ImVec4 kError     = ImVec4(0.92f, 0.36f, 0.32f, 1.0f);
inline const ImVec4 kViewportBg= ImVec4(0.235f, 0.235f, 0.235f, 1.0f);  // #3c3c3c viewport

// Layout constants (logical pixels; scaled by uiScale).
inline constexpr float kLeftWidth = 250.0f;
inline constexpr float kRightWidth = 320.0f;
inline constexpr float kMenuHeight = 26.0f;
inline constexpr float kStepCardHeight = 64.0f;

/// Every text font has Material Icons merged in (ICON_MD_* strings render inline); `icons` is a
/// larger icon-only face for the step cards and tool strip.
struct Fonts { ImFont* regular = nullptr; ImFont* bold = nullptr; ImFont* small = nullptr; ImFont* title = nullptr; ImFont* logo = nullptr; ImFont* icons = nullptr; ImFont* iconsLarge = nullptr; bool haveIcons = false; };

/// Loads Inter (assets/fonts/Inter.ttf) at the given scale and applies the dark/lime style.
Fonts apply(const std::string& assetDir, const std::string& iconFontPath, float uiScale);

// Widgets used across the app -----------------------------------------------------------------
/// Wide lime primary action button (AccuRIG "Rig Body"). Returns true when clicked.
bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(-1, 34), bool enabled = true);
/// Neutral grey button spanning the width (AccuRIG "Export FBX...").
bool WideButton(const char* label, const ImVec2& size = ImVec2(-1, 32), bool enabled = true);
/// Section label in the right panel ("Rotate Character :").
void SectionLabel(const char* text);
/// Thin horizontal rule with panel spacing.
void Rule();
/// A left-column wizard step card: number badge, icon glyph, title, subtitle. Returns true when clicked.
bool StepCard(int number, const char* icon, const char* title, const char* subtitle, bool active, bool done, bool enabled, const Fonts& f);
/// Square icon button for the viewport tool strip. `active` paints it lime.
bool IconButton(const char* icon, bool active, const char* tooltip, const Fonts& f, float size = 32.0f);

} // namespace fr::theme

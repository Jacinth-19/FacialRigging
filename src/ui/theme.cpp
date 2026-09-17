#include "ui/theme.h"
#include <cstdio>
#include <sys/stat.h>

namespace fr::theme {

namespace {
bool fileExists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }
ImU32 c32(const ImVec4& v, float a = -1.0f) { ImVec4 c = v; if (a >= 0) c.w = a; return ImGui::ColorConvertFloat4ToU32(c); }
}

Fonts apply(const std::string& assetDir, const std::string& iconFontPath, float s) {
    ImGuiIO& io = ImGui::GetIO();
    Fonts f;
    std::string ttf = assetDir + "/fonts/Inter.ttf";
    const bool haveInter = fileExists(ttf), haveIcons = fileExists(iconFontPath);
    static const ImWchar iconRange[] = {ICON_MIN_MD, ICON_MAX_MD, 0};
    f.haveIcons = haveIcons;
    // text face + merged icon face (icons sized ~1.25x the text so they read at UI weight)
    auto addText = [&](float px) -> ImFont* {
        ImFontConfig cfg; cfg.OversampleH = 3; cfg.OversampleV = 2;
        ImFont* font = haveInter ? io.Fonts->AddFontFromFileTTF(ttf.c_str(), px, &cfg) : io.Fonts->AddFontDefault();
        if (haveIcons) {
            ImFontConfig ic; ic.MergeMode = true; ic.PixelSnapH = true; ic.GlyphMinAdvanceX = px * 1.2f; ic.GlyphOffset = ImVec2(0, px * 0.12f);
            io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), px * 1.25f, &ic, iconRange);
        }
        return font;
    };
    f.regular = addText(15.0f * s);
    f.small   = addText(12.5f * s);
    f.bold    = addText(16.0f * s);
    f.title   = addText(19.0f * s);
    f.logo    = addText(26.0f * s);
    if (haveIcons) {
        ImFontConfig ic; ic.PixelSnapH = true;
        f.icons = io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), 22.0f * s, &ic, iconRange);
        f.iconsLarge = io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), 30.0f * s, &ic, iconRange);
    } else { f.icons = f.regular; f.iconsLarge = f.title; std::fprintf(stderr, "[fr] Material Icons font not found at %s - icons fall back to text\n", iconFontPath.c_str()); }
    if (!haveInter) std::fprintf(stderr, "[fr] Inter font not found at %s - using ImGui default\n", ttf.c_str());
    io.FontDefault = f.regular;

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(10, 10); st.FramePadding = ImVec2(8, 5); st.CellPadding = ImVec2(6, 3);
    st.ItemSpacing = ImVec2(8, 7); st.ItemInnerSpacing = ImVec2(6, 4); st.IndentSpacing = 16;
    st.ScrollbarSize = 10; st.GrabMinSize = 12;
    st.WindowBorderSize = 0; st.ChildBorderSize = 1; st.PopupBorderSize = 1; st.FrameBorderSize = 0; st.TabBorderSize = 0;
    st.WindowRounding = 0; st.ChildRounding = 3; st.FrameRounding = 3; st.PopupRounding = 3; st.ScrollbarRounding = 6; st.GrabRounding = 3; st.TabRounding = 3;
    st.WindowTitleAlign = ImVec2(0.5f, 0.5f); st.ButtonTextAlign = ImVec2(0.5f, 0.5f); st.SeparatorTextBorderSize = 1; st.SeparatorTextPadding = ImVec2(0, 4);
    st.ScaleAllSizes(s);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = kText; c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_WindowBg] = kPanel; c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.13f, 0.13f, 0.98f);
    c[ImGuiCol_Border] = kBorder; c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.09f, 0.09f, 1); c[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.12f, 0.12f, 1); c[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 1);
    c[ImGuiCol_TitleBg] = kBg; c[ImGuiCol_TitleBgActive] = kBg; c[ImGuiCol_TitleBgCollapsed] = kBg;
    c[ImGuiCol_MenuBarBg] = kBg;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ScrollbarGrab] = kPanelHi; c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.32f, 0.32f, 1); c[ImGuiCol_ScrollbarGrabActive] = kAccentLo;
    c[ImGuiCol_CheckMark] = kAccent; c[ImGuiCol_SliderGrab] = ImVec4(0.75f, 0.75f, 0.75f, 1); c[ImGuiCol_SliderGrabActive] = kAccent;
    c[ImGuiCol_Button] = kPanelAlt; c[ImGuiCol_ButtonHovered] = kPanelHi; c[ImGuiCol_ButtonActive] = kAccentLo;
    c[ImGuiCol_Header] = ImVec4(0.545f, 0.780f, 0.180f, 0.22f); c[ImGuiCol_HeaderHovered] = ImVec4(0.545f, 0.780f, 0.180f, 0.32f); c[ImGuiCol_HeaderActive] = ImVec4(0.545f, 0.780f, 0.180f, 0.45f);
    c[ImGuiCol_Separator] = ImVec4(0.09f, 0.09f, 0.09f, 1); c[ImGuiCol_SeparatorHovered] = kAccentDim; c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ResizeGripHovered] = kAccentDim; c[ImGuiCol_ResizeGripActive] = kAccent;
    c[ImGuiCol_Tab] = kPanelAlt; c[ImGuiCol_TabHovered] = kPanelHi; c[ImGuiCol_TabSelected] = kPanel; c[ImGuiCol_TabSelectedOverline] = kAccent; c[ImGuiCol_TabDimmed] = kPanelAlt; c[ImGuiCol_TabDimmedSelected] = kPanel;
    c[ImGuiCol_PlotLines] = kAccent; c[ImGuiCol_PlotLinesHovered] = kAccentHi; c[ImGuiCol_PlotHistogram] = kAccent; c[ImGuiCol_PlotHistogramHovered] = kAccentHi;
    c[ImGuiCol_TableHeaderBg] = kPanelAlt; c[ImGuiCol_TableBorderStrong] = kBorder; c[ImGuiCol_TableBorderLight] = ImVec4(0.12f, 0.12f, 0.12f, 1); c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_TextSelectedBg] = kAccentDim; c[ImGuiCol_DragDropTarget] = kAccent; c[ImGuiCol_NavCursor] = kAccent;
    c[ImGuiCol_DockingPreview] = kAccentDim; c[ImGuiCol_DockingEmptyBg] = kViewportBg;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    return f;
}

bool PrimaryButton(const char* label, const ImVec2& size, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, enabled ? kAccent : kPanelAlt);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHi);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentLo);
    ImGui::PushStyleColor(ImGuiCol_Text, enabled ? ImVec4(0.06f, 0.08f, 0.02f, 1) : kTextMute);
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return r;
}

bool WideButton(const char* label, const ImVec2& size, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.30f, 0.30f, 1));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(); ImGui::PopStyleVar();
    ImGui::EndDisabled();
    return r;
}

void SectionLabel(const char* text) { ImGui::Spacing(); ImGui::TextColored(kTextDim, "%s", text); }
void Rule() { ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); }

bool StepCard(int number, const char* icon, const char* title, const char* subtitle, bool active, bool done, bool enabled, const Fonts& f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x, h = kStepCardHeight * ImGui::GetIO().FontGlobalScale;
    const float s = ImGui::GetFontSize() / 15.0f; // scale factor relative to base font
    ImVec2 p0 = ImGui::GetCursorScreenPos(), p1 = ImVec2(p0.x + w, p0.y + h * s);
    ImGui::PushID(number);
    ImGui::InvisibleButton("step", ImVec2(w, h * s), enabled ? 0 : ImGuiButtonFlags_None);
    bool hovered = enabled && ImGui::IsItemHovered(), clicked = enabled && ImGui::IsItemClicked();
    ImGui::PopID();
    ImU32 bg = c32(active ? kPanelHi : hovered ? ImVec4(0.22f, 0.22f, 0.22f, 1) : kPanelAlt, enabled ? 1.0f : 0.5f);
    dl->AddRectFilled(p0, p1, bg, 4.0f * s);
    if (active) dl->AddRect(p0, p1, c32(kAccent), 4.0f * s, 0, 2.0f * s);
    else dl->AddRect(p0, p1, c32(kBorder), 4.0f * s);
    // number badge (top-left, overlapping the card edge like AccuRIG)
    ImVec2 bc(p0.x + 12 * s, p0.y + 12 * s);
    dl->AddCircleFilled(bc, 9.5f * s, c32(done || active ? kAccent : ImVec4(0.42f, 0.42f, 0.42f, 1)));
    char num[8]; std::snprintf(num, sizeof num, "%d", number);
    ImVec2 ns = f.small->CalcTextSizeA(f.small->FontSize, 100, 0, num);
    dl->AddText(f.small, f.small->FontSize, ImVec2(bc.x - ns.x * 0.5f, bc.y - ns.y * 0.5f), c32(ImVec4(0.05f, 0.05f, 0.05f, 1)), num);
    // Material icon
    {
        ImFont* iF = f.iconsLarge; float isz = iF->FontSize;
        ImVec2 is = iF->CalcTextSizeA(isz, 1000, 0, icon);
        ImVec2 ic(p0.x + 40 * s - is.x * 0.5f, p0.y + h * s * 0.5f - is.y * 0.5f);
        dl->AddText(iF, isz, ic, c32(enabled ? (active ? ImVec4(1, 1, 1, 1) : kText) : kTextMute), icon);
    }
    // title + subtitle
    float tx = p0.x + 62 * s;
    ImVec4 tc = enabled ? (active ? ImVec4(1, 1, 1, 1) : kText) : kTextMute;
    dl->AddText(f.bold, f.bold->FontSize, ImVec2(tx, p0.y + (subtitle && *subtitle ? 14 : 22) * s), c32(tc), title);
    if (subtitle && *subtitle) dl->AddText(f.small, f.small->FontSize, ImVec2(tx, p0.y + 36 * s), c32(enabled ? kTextDim : kTextMute), subtitle);
    if (done && !active) dl->AddText(f.icons, f.icons->FontSize * 0.8f, ImVec2(p1.x - 24 * s, p0.y + 6 * s), c32(kAccent), ICON_MD_CHECK_CIRCLE);
    ImGui::Dummy(ImVec2(0, 4 * s));
    return clicked;
}

bool IconButton(const char* icon, bool active, const char* tooltip, const Fonts& f, float size) {
    const float s = ImGui::GetFontSize() / 15.0f;
    if (active) { ImGui::PushStyleColor(ImGuiCol_Button, kAccent); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.06f, 0.08f, 0.02f, 1)); }
    ImGui::PushFont(f.icons);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    bool r = ImGui::Button(icon, ImVec2(size * s, size * s));
    ImGui::PopStyleVar(); ImGui::PopFont();
    if (active) ImGui::PopStyleColor(2);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return r;
}

} // namespace fr::theme

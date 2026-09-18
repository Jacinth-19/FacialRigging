#include "ui/graph_editor.h"
#include "ui/theme.h"
#include "ui/timeline_common.h"
#include "app/application.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>

namespace fr::graph {
using namespace theme;
using tl::Channel;

namespace {
enum class Norm { Normalised, Absolute, Stacked };
struct SelKey { int channel; int key; bool operator==(const SelKey& o) const { return channel == o.channel && key == o.key; } };
struct MoveOrig { int channel; float time, value; Key key; int curIndex; bool placed = false; };
enum class Drag { None, Scrub, Box, Move, TangentIn, TangentOut, Pan };

struct State {
    bool open = false, wantOpen = false;
    Norm norm = Norm::Normalised;
    std::vector<char> visible;            // per channel (grows with the clip)
    int current = -1;                     // channel that gets K / value readout when nothing is selected
    // view: time window and (absolute mode) value window
    float t0 = 0.0f, t1 = 1.0f, v0 = -0.1f, v1 = 1.1f; bool viewInit = false;
    // interaction
    std::vector<SelKey> sel; Drag drag = Drag::None; ImVec2 dragStart, dragLast; float moveLastT = 0.0f;
    std::vector<MoveOrig> moveOrig;   // selected keys at drag start
    bool onlySelectedTangents = true, showBaked = true, snap = true;
    char filter[64] = "";
    float scale = 1.0f;
} g;

ImU32 channelColour(size_t i, bool bone, float alpha = 1.0f) {
    static const ImU32 pal[] = {IM_COL32(139, 199, 46, 255), IM_COL32(80, 170, 220, 255), IM_COL32(230, 120, 60, 255), IM_COL32(200, 90, 190, 255), IM_COL32(230, 200, 60, 255), IM_COL32(120, 210, 170, 255), IM_COL32(220, 90, 120, 255), IM_COL32(150, 150, 240, 255)};
    ImU32 c = pal[i % 8]; if (bone) c = (c & 0x00FFFFFF) | 0xFF000000;
    return (c & 0x00FFFFFF) | (ImU32(alpha * 255.0f) << 24);
}
float S() { return g.scale; }

/// Value <-> normalised 0..1 for a channel under the current mode.
struct Mapper {
    float lo, hi;                 // channel natural range
    float toN(float v) const { return (v - lo) / (hi - lo); }
    float fromN(float n) const { return lo + n * (hi - lo); }
};
} // namespace

void setOpen(bool open) { g.wantOpen = open; g.open = open; }
bool isOpen() { return g.open; }
void showChannel(int channel, bool solo) {
    if (channel < 0) return;
    if (size_t(channel) >= g.visible.size()) g.visible.resize(size_t(channel) + 1, 0);
    if (solo) std::fill(g.visible.begin(), g.visible.end(), 0);
    g.visible[size_t(channel)] = 1; g.current = channel; g.open = g.wantOpen = true;
}
std::vector<std::pair<int, int>> selection() { std::vector<std::pair<int, int>> out; for (auto& s : g.sel) out.push_back({s.channel, s.key}); return out; }

bool draw(Application& app) {
    if (!g.open) return false;
    Pipeline& p = app.pipe; AnimationClip& clip = p.clip;
    g.scale = ImGui::GetIO().FontGlobalScale > 0 ? ImGui::GetFontSize() / 15.0f : 1.0f;
    const std::vector<Channel> chans = tl::channels(clip);
    if (g.visible.size() < chans.size()) { bool first = g.visible.empty(); g.visible.resize(chans.size(), 0); if (first) for (size_t i = 0; i < chans.size() && i < 3; ++i) g.visible[i] = 1; }
    if (!g.viewInit && clip.duration > 0) { g.t0 = 0; g.t1 = clip.duration; g.viewInit = true; }
    ImGui::SetNextWindowSize(ImVec2(900 * S(), 420 * S()), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkPos.x + 260 * S(), ImGui::GetMainViewport()->WorkPos.y + 60 * S()), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 0.98f));
    bool open = g.open;
    if (!ImGui::Begin(ICON_MD_SHOW_CHART "  Graph editor", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); ImGui::PopStyleColor(); g.open = open; return false; }
    g.open = open;
    if (chans.empty() || clip.duration <= 0) { ImGui::TextColored(kTextDim, "Generate a clip (step 4) to edit its curves here."); ImGui::End(); ImGui::PopStyleColor(); return false; }
    KeyLayer& L = clip.keyLayer;
    ImGuiIO& io = ImGui::GetIO();
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // ------------------------------------------------------------------ toolbar
    if (PrimaryButton(app.playing ? ICON_MD_PAUSE : ICON_MD_PLAY_ARROW, ImVec2(26 * S(), 22 * S()))) app.playing = !app.playing;
    ImGui::SameLine(); ImGui::TextColored(kAccent, "%6.2f s", app.playTime);
    ImGui::SameLine(0, 12 * S()); ImGui::TextColored(kTextDim, "Values"); ImGui::SameLine();
    const char* normNames[] = {"Normalised", "Absolute", "Stacked"}; int ni = int(g.norm); ImGui::SetNextItemWidth(110 * S()); if (ImGui::Combo("##norm", &ni, normNames, 3)) g.norm = Norm(ni);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Normalised: every channel stretched to its own range (weights 0..1, bone axes +-range deg) so they share the graph.\nAbsolute: one value axis in channel units.\nStacked: one strip per channel.");
    ImGui::SameLine(0, 10 * S()); ImGui::Checkbox("Baked", &g.showBaked); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draw the generated (baked) curve under the composite when a channel has keys");
    ImGui::SameLine(); ImGui::Checkbox("Snap", &g.snap); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap key times to frames while dragging (hold Ctrl to bypass)");
    ImGui::SameLine(0, 10 * S());
    if (WideButton(ICON_MD_ADD "  Key (K)", ImVec2(0, 22 * S()))) {
        app.pushUndo("add keys"); int n = 0;
        for (size_t i = 0; i < chans.size(); ++i) if (g.visible[i]) { L.get(chans[i].target, chans[i].axis).addKey(app.playTime, L.evaluate(chans[i].target, chans[i].axis, app.playTime)); ++n; }
        app.status = "Keyed " + std::to_string(n) + " visible channel(s) at the playhead";
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Key every visible channel at the playhead (holds the current composite value)");
    ImGui::SameLine(); if (WideButton(ICON_MD_DELETE, ImVec2(26 * S(), 22 * S()), !g.sel.empty())) {
        app.pushUndo(g.sel.size() > 1 ? "delete keys" : "delete key");
        std::sort(g.sel.begin(), g.sel.end(), [](const SelKey& a, const SelKey& b) { return a.channel != b.channel ? a.channel < b.channel : a.key > b.key; });
        for (auto& s : g.sel) if (KeyCurve* kc = L.find(chans[size_t(s.channel)].target, chans[size_t(s.channel)].axis)) kc->removeKey(s.key);
        g.sel.clear(); L.prune(); clip.applyTo(p.rig, app.playTime);
    }
    ImGui::SameLine(0, 10 * S()); ImGui::TextColored(kTextDim, "Tangents");
    for (int m = 0; m < 4; ++m) {
        ImGui::SameLine();
        if (WideButton(tangentModeName(TangentMode(m)), ImVec2(0, 22 * S()), !g.sel.empty())) {
            app.pushUndo("tangent mode");
            for (auto& s : g.sel) if (KeyCurve* kc = L.find(chans[size_t(s.channel)].target, chans[size_t(s.channel)].axis)) { if (s.key < int(kc->keys.size())) { kc->keys[size_t(s.key)].broken = false; kc->setTangentMode(s.key, TangentMode(m)); } }
            clip.applyTo(p.rig, app.playTime);
        }
    }
    ImGui::SameLine(0, 10 * S()); if (WideButton("Frame (F)", ImVec2(0, 22 * S()))) { g.t0 = 0; g.t1 = clip.duration; g.v0 = -0.1f; g.v1 = 1.1f; }
    ImGui::SameLine(); if (WideButton(ICON_MD_FACE "  Pose as keys", ImVec2(0, 22 * S()))) {
        app.pushUndo("pose as keys"); int n = 0; const float t = app.playTime;
        for (const auto& bc : clip.blendCurves) { float d = p.rig.blendWeight(bc.target) - bc.sample(t); if (std::fabs(d) > 1e-3f) { L.get(bc.target).addKey(t, d); ++n; } }
        for (const auto& bc : clip.boneRotations) { int b = p.rig.skeleton.find(bc.target); if (b < 0) continue; glm::vec3 e = glm::degrees(glm::eulerAngles(glm::normalize(glm::inverse(bc.sample(t)) * p.rig.skeleton.bones[size_t(b)].poseRotation))); for (int ax = 0; ax < 3; ++ax) if (std::fabs(e[ax]) > 0.05f) { L.get(bc.target, ax).addKey(t, e[ax]); ++n; } }
        for (size_t i = 0; i < chans.size(); ++i) if (L.find(chans[i].target, chans[i].axis)) g.visible[i] = 1;
        app.status = n ? "Keyed " + std::to_string(n) + " channel(s)" : "Pose matches the clip - nothing to key";
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bake the viewport pose (sliders / handles / gaze) as keys at the playhead on every channel that differs from the clip");
    ImGui::SameLine(0, 10 * S()); if (ImGui::Checkbox("##len", &L.enabled)) clip.applyTo(p.rig, app.playTime); ImGui::SameLine(); ImGui::SetNextItemWidth(80 * S()); if (ImGui::SliderFloat("##lw", &L.weight, 0.0f, 1.0f, "layer %.2f")) clip.applyTo(p.rig, app.playTime);
    if (!g.sel.empty()) {
        const SelKey& s = g.sel.front(); const Channel& c = chans[size_t(s.channel)];
        if (KeyCurve* kc = L.find(c.target, c.axis)) if (s.key < int(kc->keys.size())) {
            Key& k = kc->keys[size_t(s.key)];
            ImGui::SameLine(0, 12 * S()); ImGui::TextColored(kAccent, "%s", tl::channelLabel(c).c_str());
            ImGui::SameLine(); ImGui::SetNextItemWidth(70 * S()); float tt = k.time; if (ImGui::DragFloat("##kt", &tt, 0.005f, 0.0f, clip.duration, "%.3f s")) { app.pushUndo("move key"); int ni2 = kc->moveKey(s.key, tt); g.sel.front().key = ni2; clip.applyTo(p.rig, app.playTime); }
            ImGui::SameLine(); ImGui::SetNextItemWidth(70 * S()); float vv = k.value; if (ImGui::DragFloat("##kv", &vv, c.bone ? 0.1f : 0.005f, -1e3f, 1e3f, c.bone ? "%+.1f" : "%+.3f")) { app.pushUndo("key value"); k.value = vv; kc->updateTangents(); clip.applyTo(p.rig, app.playTime); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Layer delta on top of the baked value (composite = %.3f)", tl::compositeAt(clip, c, k.time));
            ImGui::SameLine(); bool br = k.broken; if (ImGui::Checkbox("Broken", &br)) { app.pushUndo("break tangents"); k.broken = br; if (!br) { k.inSlope = k.outSlope; kc->updateTangents(); } clip.applyTo(p.rig, app.playTime); }
            if (g.sel.size() > 1) { ImGui::SameLine(); ImGui::TextColored(kTextDim, "(+%zu more)", g.sel.size() - 1); }
        }
    }

    // ------------------------------------------------------------------ channel list (left) + graph (right)
    const float listW = 190 * S();
    ImGui::BeginChild("##chanlist", ImVec2(listW, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##filter", ICON_MD_SEARCH " filter", g.filter, sizeof g.filter);
    if (ImGui::SmallButton("all")) for (size_t i = 0; i < chans.size(); ++i) g.visible[i] = 1;
    ImGui::SameLine(); if (ImGui::SmallButton("none")) std::fill(g.visible.begin(), g.visible.end(), 0);
    ImGui::SameLine(); if (ImGui::SmallButton("keyed")) for (size_t i = 0; i < chans.size(); ++i) g.visible[i] = L.find(chans[i].target, chans[i].axis) && !L.find(chans[i].target, chans[i].axis)->keys.empty();
    ImGui::SameLine(); if (ImGui::SmallButton("active")) for (size_t i = 0; i < chans.size(); ++i) { const auto& bc = chans[i]; bool any = false; if (!bc.bone) { for (float v : clip.blendCurves[size_t(bc.bakedIndex)].values) if (v > 0.02f) { any = true; break; } } else any = true; g.visible[i] = any; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show blend channels that move in this clip (plus bone axes)");
    ImGui::Separator();
    ImGui::BeginChild("##chanscroll");
    std::string filt = g.filter; std::transform(filt.begin(), filt.end(), filt.begin(), ::tolower);
    for (int pass = 0; pass < 2; ++pass) {
        ImGui::TextColored(kTextMute, pass == 0 ? "BONES (deg)" : "BLEND SHAPES");
        for (size_t i = 0; i < chans.size(); ++i) {
            if (chans[i].bone != (pass == 0)) continue;
            std::string lbl = tl::channelLabel(chans[i]);
            if (!filt.empty()) { std::string low = lbl; std::transform(low.begin(), low.end(), low.begin(), ::tolower); if (low.find(filt) == std::string::npos) continue; }
            ImGui::PushID(int(i));
            ImU32 col = channelColour(i, chans[i].bone);
            ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 cp = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(ImVec2(cp.x, cp.y + 4 * S()), ImVec2(cp.x + 4 * S(), cp.y + 14 * S()), g.visible[i] ? col : IM_COL32(70, 70, 70, 255));
            ImGui::Dummy(ImVec2(6 * S(), 0)); ImGui::SameLine();
            bool v = g.visible[i] != 0; if (ImGui::Checkbox("##v", &v)) g.visible[i] = v;
            ImGui::SameLine();
            const KeyCurve* kc = L.find(chans[i].target, chans[i].axis); int nk = kc ? int(kc->keys.size()) : 0;
            if (nk) lbl += "  [" + std::to_string(nk) + "]";
            if (ImGui::Selectable(lbl.c_str(), g.current == int(i))) { if (io.KeyShift) { std::fill(g.visible.begin(), g.visible.end(), 0); } g.visible[i] = 1; g.current = int(i); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click: show + make current. Shift-click: solo. Numbers = keys on the channel.");
            ImGui::PopID();
        }
        ImGui::Spacing();
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::SameLine();

    // ------------------------------------------------------------------ graph
    ImGui::BeginChild("##graph", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 gp = ImGui::GetCursorScreenPos(), gs = ImGui::GetContentRegionAvail();
    const float axisW = 44 * S(), rulerH = 16 * S();
    const float gx0 = gp.x + axisW, gx1 = gp.x + gs.x - 4 * S(), gy0 = gp.y + rulerH, gy1 = gp.y + gs.y - 4 * S();
    const float gw = std::max(10.0f, gx1 - gx0), gh = std::max(10.0f, gy1 - gy0);
    if (g.t1 - g.t0 < 0.02f) g.t1 = g.t0 + 0.02f;
    auto tx = [&](float t) { return gx0 + (t - g.t0) / (g.t1 - g.t0) * gw; };
    auto xt = [&](float x) { return g.t0 + (x - gx0) / gw * (g.t1 - g.t0); };
    // visible channel set + their mappers; stacked strips
    std::vector<size_t> vis; for (size_t i = 0; i < chans.size(); ++i) if (g.visible[i]) vis.push_back(i);
    std::vector<Mapper> mp(chans.size()); for (size_t i : vis) tl::channelRange(clip, chans[i], mp[i].lo, mp[i].hi);
    float absLo = 0, absHi = 1; if (g.norm == Norm::Absolute && !vis.empty()) { absLo = 1e9f; absHi = -1e9f; for (size_t i : vis) { absLo = std::min(absLo, mp[i].lo); absHi = std::max(absHi, mp[i].hi); } }
    const int nStrips = std::max<size_t>(1, vis.size());
    // value -> pixel y for a channel (index in vis for stacked)
    auto vy = [&](size_t ci, int stripIdx, float v) {
        float n; // normalised 0..1 of the drawing region
        if (g.norm == Norm::Absolute) n = (v - absLo) / (absHi - absLo); else n = mp[ci].toN(v);
        // view window in normalised space (v0..v1), stacked splits vertically
        float top = gy0, bot = gy1;
        if (g.norm == Norm::Stacked) { float sh = gh / nStrips; top = gy0 + sh * stripIdx + 2 * S(); bot = gy0 + sh * (stripIdx + 1) - 2 * S(); }
        float u = (n - g.v0) / (g.v1 - g.v0);
        return bot - u * (bot - top);
    };
    auto yv = [&](size_t ci, int stripIdx, float y) {
        float top = gy0, bot = gy1;
        if (g.norm == Norm::Stacked) { float sh = gh / nStrips; top = gy0 + sh * stripIdx + 2 * S(); bot = gy0 + sh * (stripIdx + 1) - 2 * S(); }
        float u = (bot - y) / std::max(1.0f, bot - top); float n = g.v0 + u * (g.v1 - g.v0);
        return g.norm == Norm::Absolute ? absLo + n * (absHi - absLo) : mp[ci].fromN(n);
    };
    auto stripOf = [&](size_t ci) { for (size_t k = 0; k < vis.size(); ++k) if (vis[k] == ci) return int(k); return 0; };
    // background, grid, ruler
    dl->AddRectFilled(ImVec2(gx0, gy0), ImVec2(gx1, gy1), IM_COL32(24, 24, 24, 255));
    {
        float step = 0.1f; while ((g.t1 - g.t0) / step > gw / (48 * S())) step *= (std::fabs(std::fmod(std::log10(step), 1.0f)) < 1e-3f ? 2.0f : 2.5f);
        for (float t = std::floor(g.t0 / step) * step; t <= g.t1 + 1e-4f; t += step) { float x = tx(t); if (x < gx0 || x > gx1) continue; dl->AddLine(ImVec2(x, gy0), ImVec2(x, gy1), IM_COL32(40, 40, 40, 255)); char b[16]; std::snprintf(b, sizeof b, "%.2f", t); dl->AddText(ImVec2(x + 2, gp.y), ImGui::ColorConvertFloat4ToU32(kTextDim), b); }
        // horizontal grid: normalised 0..1 quarters (or absolute ticks), stacked per strip
        for (int sIdx = 0; sIdx < (g.norm == Norm::Stacked ? nStrips : 1); ++sIdx) {
            size_t ci = vis.empty() ? 0 : vis[size_t(sIdx)];
            for (int q = 0; q <= 4; ++q) {
                float n = q * 0.25f; float y; float top = gy0, bot = gy1;
                if (g.norm == Norm::Stacked) { float sh = gh / nStrips; top = gy0 + sh * sIdx + 2 * S(); bot = gy0 + sh * (sIdx + 1) - 2 * S(); }
                y = bot - (n - g.v0) / (g.v1 - g.v0) * (bot - top); if (y < top || y > bot) continue;
                dl->AddLine(ImVec2(gx0, y), ImVec2(gx1, y), q == 0 || q == 4 ? IM_COL32(70, 70, 70, 255) : IM_COL32(40, 40, 40, 255));
                char b[24];
                if (g.norm == Norm::Absolute) std::snprintf(b, sizeof b, "%.2f", absLo + n * (absHi - absLo));
                else if (g.norm == Norm::Stacked && !vis.empty()) std::snprintf(b, sizeof b, chans[ci].bone ? "%+.0f" : "%.2f", mp[ci].fromN(n));
                else std::snprintf(b, sizeof b, "%.2f", n);
                if (q == 0 || q == 4 || g.norm != Norm::Stacked) dl->AddText(ImVec2(gp.x + 2, y - 7 * S()), ImGui::ColorConvertFloat4ToU32(kTextDim), b);
            }
            if (g.norm == Norm::Stacked && !vis.empty()) { float sh = gh / nStrips; dl->AddText(ImVec2(gx0 + 6 * S(), gy0 + sh * sIdx + 3 * S()), channelColour(ci, chans[ci].bone), tl::channelLabel(chans[ci]).c_str()); if (sIdx) dl->AddLine(ImVec2(gx0, gy0 + sh * sIdx), ImVec2(gx1, gy0 + sh * sIdx), IM_COL32(60, 60, 60, 255)); }
        }
    }
    dl->PushClipRect(ImVec2(gx0, gy0), ImVec2(gx1, gy1), true);
    // curves
    const int nCols = std::max(2, int(gw / (2 * S())));
    for (size_t k = 0; k < vis.size(); ++k) {
        size_t ci = vis[k]; const Channel& c = chans[ci]; const int strip = g.norm == Norm::Stacked ? int(k) : 0;
        const bool cur = int(ci) == g.current; ImU32 col = channelColour(ci, c.bone, cur ? 1.0f : 0.8f);
        const KeyCurve* kc = L.find(c.target, c.axis); const bool hasLayer = kc && !kc->keys.empty() && L.enabled;
        ImVec2 prev, prevB; bool have = false;
        for (int i = 0; i <= nCols; ++i) {
            float t = g.t0 + (g.t1 - g.t0) * i / nCols; if (t < 0 || t > clip.duration) { have = false; continue; }
            ImVec2 pt(tx(t), vy(ci, strip, tl::compositeAt(clip, c, t))), pb(tx(t), vy(ci, strip, tl::bakedAt(clip, c, t)));
            if (have) { if (g.showBaked && hasLayer) dl->AddLine(prevB, pb, (col & 0x00FFFFFF) | 0x50000000, 1.0f * S()); dl->AddLine(prev, pt, col, cur ? 2.0f * S() : 1.2f * S()); }
            prev = pt; prevB = pb; have = true;
        }
        if (g.norm != Norm::Stacked && cur) { std::string lbl = tl::channelLabel(c); ImVec2 ts = ImGui::CalcTextSize(lbl.c_str()); dl->AddText(ImVec2(gx1 - ts.x - 6 * S(), gy0 + 3 * S() + 14 * S() * float(k % 6)), col, lbl.c_str()); }
    }
    // keys + tangent handles
    struct Hot { int channel = -1, key = -1, handle = 0; float d2 = 1e9f; } hot;
    const float handleLen = 36 * S();
    auto isSel = [&](int ci, int k) { return std::find(g.sel.begin(), g.sel.end(), SelKey{ci, k}) != g.sel.end(); };
    for (size_t k = 0; k < vis.size(); ++k) {
        size_t ci = vis[k]; const Channel& c = chans[ci]; const int strip = g.norm == Norm::Stacked ? int(k) : 0;
        KeyCurve* kc = L.find(c.target, c.axis); if (!kc) continue;
        ImU32 col = channelColour(ci, c.bone);
        for (size_t ki = 0; ki < kc->keys.size(); ++ki) {
            const Key& key = kc->keys[ki];
            ImVec2 pt(tx(key.time), vy(ci, strip, tl::compositeAt(clip, c, key.time)));
            if (pt.x < gx0 - 8 || pt.x > gx1 + 8) continue;
            const bool sel = isSel(int(ci), int(ki));
            if (sel || !g.onlySelectedTangents) {
                const float pxPerSec = gw / (g.t1 - g.t0);
                float pxPerVal = std::fabs(vy(ci, strip, mp[ci].lo + 1.0f) - vy(ci, strip, mp[ci].lo));   // pixels per channel unit
                auto hdir = [&](float slope, float sign) { ImVec2 d(sign * pxPerSec, -sign * slope * pxPerVal); float l = std::sqrt(d.x * d.x + d.y * d.y); if (l < 1e-6f) l = 1; return ImVec2(pt.x + d.x / l * handleLen, pt.y + d.y / l * handleLen); };
                ImVec2 hi = hdir(key.inSlope, -1.0f), ho = hdir(key.outSlope, 1.0f);
                ImU32 hc = IM_COL32(255, 200, 80, sel ? 230 : 110);
                dl->AddLine(pt, hi, hc, 1.0f * S()); dl->AddLine(pt, ho, hc, 1.0f * S());
                dl->AddCircleFilled(hi, 3.5f * S(), hc); dl->AddCircleFilled(ho, 3.5f * S(), hc);
                if (sel) { auto d2 = [&](ImVec2 a) { float dx = io.MousePos.x - a.x, dy = io.MousePos.y - a.y; return dx * dx + dy * dy; };
                    float di = d2(hi), dout = d2(ho); if (di < 49 * S() * S() && di < hot.d2) hot = {int(ci), int(ki), -1, di}; if (dout < 49 * S() * S() && dout < hot.d2) hot = {int(ci), int(ki), 1, dout}; }
            }
            const float r = 4 * S();
            dl->AddRectFilled(ImVec2(pt.x - r, pt.y - r), ImVec2(pt.x + r, pt.y + r), sel ? IM_COL32(255, 255, 255, 255) : col, 1.0f);
            dl->AddRect(ImVec2(pt.x - r, pt.y - r), ImVec2(pt.x + r, pt.y + r), IM_COL32(0, 0, 0, 255), 1.0f);
            float dx = io.MousePos.x - pt.x, dy = io.MousePos.y - pt.y, d2 = dx * dx + dy * dy;
            if (d2 < 64 * S() * S() && d2 < hot.d2 && hot.handle == 0) hot = {int(ci), int(ki), 0, d2};
        }
    }
    if (g.drag == Drag::Box) { ImVec2 a(std::min(g.dragStart.x, io.MousePos.x), std::min(g.dragStart.y, io.MousePos.y)), b(std::max(g.dragStart.x, io.MousePos.x), std::max(g.dragStart.y, io.MousePos.y)); dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 25)); dl->AddRect(a, b, IM_COL32(255, 255, 255, 160)); }
    // playhead
    { float px = tx(app.playTime); if (px >= gx0 && px <= gx1) { dl->AddLine(ImVec2(px, gy0), ImVec2(px, gy1), IM_COL32(255, 255, 255, 200), 1.5f * S()); } }
    dl->PopClipRect();
    { float px = tx(app.playTime); if (px >= gx0 && px <= gx1) dl->AddTriangleFilled(ImVec2(px - 5 * S(), gp.y), ImVec2(px + 5 * S(), gp.y), ImVec2(px, gp.y + 7 * S()), IM_COL32(255, 255, 255, 220)); }

    // ------------------------------------------------------------------ interaction
    ImGui::SetCursorScreenPos(ImVec2(gx0, gp.y));
    ImGui::InvisibleButton("##ggraph", ImVec2(gw, gy1 - gp.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const bool inRuler = io.MousePos.y < gy0;
    if (hovered && io.MouseWheel != 0.0f) {
        if (io.KeyShift) { // value zoom about the cursor (normalised space)
            float cy = (gy1 - io.MousePos.y) / gh; float nAt = g.v0 + cy * (g.v1 - g.v0); float f = io.MouseWheel > 0 ? 0.8f : 1.25f; g.v0 = nAt - (nAt - g.v0) * f; g.v1 = nAt + (g.v1 - nAt) * f;
        } else if (io.KeyCtrl) { float tAt = xt(io.MousePos.x); float f = io.MouseWheel > 0 ? 0.8f : 1.25f; g.t0 = std::max(0.0f, tAt - (tAt - g.t0) * f); g.t1 = std::min(clip.duration, tAt + (g.t1 - tAt) * f); }
        else { float span = g.t1 - g.t0, d = -io.MouseWheel * span * 0.1f; d = std::clamp(d, -g.t0, clip.duration - g.t1); g.t0 += d; g.t1 += d; }
    }
    auto snapT = [&](float t) { return (g.snap && !io.KeyCtrl) ? std::round(t * clip.frameRate) / clip.frameRate : t; };
    if (ImGui::IsItemActivated()) {
        g.drag = Drag::None; app.playing = false;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyAlt)) { g.drag = Drag::Pan; g.dragLast = io.MousePos; }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || inRuler) { g.drag = Drag::Scrub; }
        else if (hot.channel >= 0 && hot.handle != 0) { app.pushUndo("edit tangent"); g.drag = hot.handle < 0 ? Drag::TangentIn : Drag::TangentOut; g.sel = {SelKey{hot.channel, hot.key}}; }
        else if (hot.channel >= 0) {
            if (io.KeyShift) { SelKey sk{hot.channel, hot.key}; auto it = std::find(g.sel.begin(), g.sel.end(), sk); if (it != g.sel.end()) g.sel.erase(it); else g.sel.push_back(sk); }
            else if (!isSel(hot.channel, hot.key)) g.sel = {SelKey{hot.channel, hot.key}};
            g.current = hot.channel;
            if (!io.KeyShift) { app.pushUndo(g.sel.size() > 1 ? "move keys" : "move key"); g.drag = Drag::Move; g.dragStart = io.MousePos; g.moveLastT = xt(io.MousePos.x);
                g.moveOrig.clear(); for (auto& s : g.sel) if (KeyCurve* kc = L.find(chans[size_t(s.channel)].target, chans[size_t(s.channel)].axis)) if (s.key < int(kc->keys.size())) g.moveOrig.push_back({s.channel, kc->keys[size_t(s.key)].time, kc->keys[size_t(s.key)].value, kc->keys[size_t(s.key)], s.key}); }
        }
        else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            // add a key on the current channel (must be visible) at the clicked composite value
            int ci = g.current; if (ci >= 0 && ci < int(chans.size()) && g.visible[size_t(ci)]) {
                app.pushUndo("add key"); const Channel& c = chans[size_t(ci)]; float t = std::clamp(snapT(xt(io.MousePos.x)), 0.0f, clip.duration);
                float v = yv(size_t(ci), stripOf(size_t(ci)), io.MousePos.y) - tl::bakedAt(clip, c, t);
                int ki = L.get(c.target, c.axis).addKey(t, v); g.sel = {SelKey{ci, ki}}; clip.applyTo(p.rig, app.playTime);
                g.drag = Drag::Move; g.dragStart = io.MousePos; g.moveLastT = xt(io.MousePos.x); g.moveOrig = {{ci, t, v, L.get(c.target, c.axis).keys[size_t(ki)], ki}};
            }
        } else { if (!io.KeyShift) g.sel.clear(); g.drag = Drag::Box; g.dragStart = io.MousePos; }
    }
    if (active) {
        const float t = xt(io.MousePos.x);
        switch (g.drag) {
        case Drag::Scrub: { float tt = std::clamp(t, 0.0f, clip.duration); app.playTime = tt; clip.applyTo(p.rig, tt); break; }
        case Drag::Pan: { float dt = -(io.MousePos.x - g.dragLast.x) / gw * (g.t1 - g.t0); dt = std::clamp(dt, -g.t0, clip.duration - g.t1); g.t0 += dt; g.t1 += dt; float dv = (io.MousePos.y - g.dragLast.y) / gh * (g.v1 - g.v0); g.v0 += dv; g.v1 += dv; g.dragLast = io.MousePos; break; }
        case Drag::Box: {
            ImVec2 a(std::min(g.dragStart.x, io.MousePos.x), std::min(g.dragStart.y, io.MousePos.y)), b(std::max(g.dragStart.x, io.MousePos.x), std::max(g.dragStart.y, io.MousePos.y));
            if (!io.KeyShift) g.sel.clear();
            for (size_t k = 0; k < vis.size(); ++k) { size_t ci = vis[k]; const KeyCurve* kc = L.find(chans[ci].target, chans[ci].axis); if (!kc) continue; const int strip = g.norm == Norm::Stacked ? int(k) : 0;
                for (size_t ki = 0; ki < kc->keys.size(); ++ki) { ImVec2 pt(tx(kc->keys[ki].time), vy(ci, strip, tl::compositeAt(clip, chans[ci], kc->keys[ki].time))); if (pt.x >= a.x && pt.x <= b.x && pt.y >= a.y && pt.y <= b.y && !isSel(int(ci), int(ki))) g.sel.push_back({int(ci), int(ki)}); } }
            break; }
        case Drag::Move: {
            // delta from drag start: time (snapped) and value (pixel delta -> each key's own channel units).
            // Per channel: pull the selected keys out, apply the delta from their ORIGINAL positions, re-insert sorted.
            const float dtRaw = t - xt(g.dragStart.x), dyPix = io.MousePos.y - g.dragStart.y;
            const bool lockT = io.KeyShift && std::fabs(dyPix) > std::fabs(io.MousePos.x - g.dragStart.x), lockV = io.KeyShift && !lockT;
            std::vector<SelKey> newSel;
            for (size_t ci = 0; ci < chans.size(); ++ci) {
                std::vector<MoveOrig*> mine; for (auto& mo : g.moveOrig) if (mo.channel == int(ci)) mine.push_back(&mo);
                if (mine.empty()) continue;
                const Channel& c = chans[ci]; KeyCurve* kc = L.find(c.target, c.axis); if (!kc) continue;
                const int strip = stripOf(ci);
                const float perPix = (yv(ci, strip, 100.0f) - yv(ci, strip, 0.0f)) / 100.0f;   // channel units per pixel (negative: y down)
                // remove the moving keys (highest index first), then re-insert at their new positions
                std::vector<int> idx; for (auto* mo : mine) idx.push_back(mo->curIndex); std::sort(idx.rbegin(), idx.rend());
                std::vector<Key> moving; for (int i : idx) if (i >= 0 && i < int(kc->keys.size())) { moving.push_back(kc->keys[size_t(i)]); kc->keys.erase(kc->keys.begin() + i); }
                for (auto* mo : mine) {
                    float nt = lockT ? mo->time : std::clamp(snapT(mo->time + dtRaw), 0.0f, clip.duration);
                    float nv = lockV ? mo->value : mo->value + dyPix * perPix;
                    if (!c.bone) nv = std::clamp(nv, -tl::bakedAt(clip, c, nt), 1.0f - tl::bakedAt(clip, c, nt));   // weights stay in 0..1
                    Key k = mo->key; k.time = nt; k.value = nv;
                    for (const Key& o : kc->keys) if (std::fabs(o.time - k.time) < 1e-4f) k.time += 2e-4f;
                    auto it = std::upper_bound(kc->keys.begin(), kc->keys.end(), k.time, [](float v, const Key& kk) { return v < kk.time; });
                    mo->curIndex = int(it - kc->keys.begin()); kc->keys.insert(it, k);
                    // inserting shifts the indices of already re-inserted keys after it
                    for (auto* other : mine) if (other != mo && other->curIndex >= mo->curIndex && other->placed) ++other->curIndex;
                    mo->placed = true;
                }
                for (auto* mo : mine) { mo->placed = false; newSel.push_back({int(ci), mo->curIndex}); }
                kc->updateTangents();
            }
            if (!newSel.empty()) g.sel = newSel;
            clip.applyTo(p.rig, app.playTime);
            break; }
        case Drag::TangentIn: case Drag::TangentOut: {
            if (g.sel.empty()) break;
            const SelKey& s = g.sel.front(); const Channel& c = chans[size_t(s.channel)]; KeyCurve* kc = L.find(c.target, c.axis); if (!kc || s.key >= int(kc->keys.size())) break;
            Key& key = kc->keys[size_t(s.key)]; int strip = stripOf(size_t(s.channel));
            ImVec2 pt(tx(key.time), vy(size_t(s.channel), strip, tl::compositeAt(clip, c, key.time)));
            const float sign = g.drag == Drag::TangentIn ? -1.0f : 1.0f;
            float dx = (io.MousePos.x - pt.x) * sign, dy = -(io.MousePos.y - pt.y) * sign;
            const float pxPerSec = gw / (g.t1 - g.t0); float pxPerVal = std::fabs(vy(size_t(s.channel), strip, mp[size_t(s.channel)].lo + 1.0f) - vy(size_t(s.channel), strip, mp[size_t(s.channel)].lo));
            if (dx > 2.0f) {
                float slope = (dy / pxPerVal) / (dx / pxPerSec);
                if (g.drag == Drag::TangentIn) { key.inMode = TangentMode::Free; key.inSlope = slope; if (!key.broken) { key.outMode = TangentMode::Free; key.outSlope = slope; } }
                else { key.outMode = TangentMode::Free; key.outSlope = slope; if (!key.broken) { key.inMode = TangentMode::Free; key.inSlope = slope; } }
                kc->updateTangents(); clip.applyTo(p.rig, app.playTime);
            }
            break; }
        default: break;
        }
    }
    if (ImGui::IsItemDeactivated()) { g.drag = Drag::None; g.moveOrig.clear(); }
    // keyboard
    bool consumed = false;
    if ((hovered || focused) && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_K)) { app.pushUndo("add keys"); for (size_t i : vis) L.get(chans[i].target, chans[i].axis).addKey(app.playTime, L.evaluate(chans[i].target, chans[i].axis, app.playTime)); consumed = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            // frame selection (or everything)
            if (!g.sel.empty()) { float lo = 1e9f, hi = -1e9f; for (auto& s : g.sel) if (const KeyCurve* kc = L.find(chans[size_t(s.channel)].target, chans[size_t(s.channel)].axis)) if (s.key < int(kc->keys.size())) { lo = std::min(lo, kc->keys[size_t(s.key)].time); hi = std::max(hi, kc->keys[size_t(s.key)].time); }
                float pad = std::max(0.25f, (hi - lo) * 0.25f); g.t0 = std::max(0.0f, lo - pad); g.t1 = std::min(clip.duration, hi + pad); }
            else { g.t0 = 0; g.t1 = clip.duration; }
            g.v0 = -0.1f; g.v1 = 1.1f; consumed = true;
        }
        if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && !g.sel.empty()) {
            app.pushUndo(g.sel.size() > 1 ? "delete keys" : "delete key");
            std::sort(g.sel.begin(), g.sel.end(), [](const SelKey& a, const SelKey& b) { return a.channel != b.channel ? a.channel < b.channel : a.key > b.key; });
            for (auto& s : g.sel) if (KeyCurve* kc = L.find(chans[size_t(s.channel)].target, chans[size_t(s.channel)].axis)) kc->removeKey(s.key);
            g.sel.clear(); L.prune(); clip.applyTo(p.rig, app.playTime); consumed = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_A) && io.KeyCtrl) { g.sel.clear(); for (size_t i : vis) if (const KeyCurve* kc = L.find(chans[i].target, chans[i].axis)) for (size_t k = 0; k < kc->keys.size(); ++k) g.sel.push_back({int(i), int(k)}); consumed = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) { app.playing = !app.playing; consumed = true; }
    }
    if (hovered && g.drag == Drag::None) {
        float t = xt(io.MousePos.x); char b[200];
        if (hot.channel >= 0) { const Channel& c = chans[size_t(hot.channel)]; const KeyCurve* kc = L.find(c.target, c.axis); const Key& k = kc->keys[size_t(hot.key)]; std::snprintf(b, sizeof b, "%s  key %d  %.3f s  delta %+.3f  composite %.3f", tl::channelLabel(c).c_str(), hot.key, k.time, k.value, tl::compositeAt(clip, c, k.time)); }
        else if (g.current >= 0 && g.current < int(chans.size())) std::snprintf(b, sizeof b, "%.3f s  f %d   %s = %.3f", t, int(t * clip.frameRate + 0.5f), tl::channelLabel(chans[size_t(g.current)]).c_str(), tl::compositeAt(clip, chans[size_t(g.current)], std::clamp(t, 0.0f, clip.duration)));
        else std::snprintf(b, sizeof b, "%.3f s", t);
        ImGui::SetTooltip("%s\nDouble-click: key on the current channel  |  drag key(s): move (Shift = lock axis, Ctrl = no snap)  |  orange handles: tangents\nDrag empty: box-select across channels (Shift adds)  |  Right-drag / ruler: scrub  |  Middle / Alt-drag: pan  |  Wheel: scroll, Ctrl: zoom time, Shift: zoom values\nK: key visible channels  |  F: frame  |  Del: delete  |  Ctrl+A: all visible", b);
    }
    ImGui::EndChild();
    ImGui::End(); ImGui::PopStyleColor();
    return consumed;
}

} // namespace fr::graph

#include "app/pipeline.h"
#include "core/obj_io.h"
#include "export/exporter.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace fr {

namespace {
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }
std::string slug(const std::string& s) { std::string o; for (char c : s) o += std::isalnum((unsigned char)c) ? char(std::tolower((unsigned char)c)) : '_'; return o; }
bool parseKV(const std::string& s, const char* key, const char* op, float& v) {
    auto pos = s.find(key); if (pos == std::string::npos) return false;
    auto opPos = s.find(op, pos); if (opPos == std::string::npos) return false;
    return std::sscanf(s.c_str() + opPos + std::string(op).size(), "%f", &v) == 1;
}
} // namespace

Variation parseVariation(const std::string& text) {
    Variation v; v.name = text;
    std::string t = lower(text);
    float x = 0.0f;
    if (parseKV(t, "intensity", "=", x))       v.apply = [x](AnimationClip& c) { c.scaleBlendCurves(x); };
    else if (parseKV(t, "smile", "+=", x))     v.apply = [x](AnimationClip& c) { c.offsetBlendCurve(shapes::MouthSmile, x); };
    else if (parseKV(t, "brow", "+=", x))      v.apply = [x](AnimationClip& c) { c.offsetBlendCurve(shapes::BrowRaise, x); };
    else if (parseKV(t, "smooth", "=", x))     v.apply = [x](AnimationClip& c) { c.smoothBlendCurves(int(x)); };
    else if (t.find("smile") != std::string::npos)      v.apply = [](AnimationClip& c) { c.offsetBlendCurve(shapes::MouthSmile, 0.35f); };
    else if (t.find("brow") != std::string::npos || t.find("eyebrow") != std::string::npos)
                                                        v.apply = [](AnimationClip& c) { c.offsetBlendCurve(shapes::BrowRaise, 0.4f); };
    else if (t.find("subtle") != std::string::npos || t.find("less") != std::string::npos || t.find("calm") != std::string::npos)
                                                        v.apply = [](AnimationClip& c) { c.scaleBlendCurves(0.6f); };
    else if (t.find("exagger") != std::string::npos || t.find("more") != std::string::npos || t.find("intense") != std::string::npos)
                                                        v.apply = [](AnimationClip& c) { c.scaleBlendCurves(1.4f); };
    else v.apply = [](AnimationClip&) {};
    return v;
}

void Pipeline::note(const std::string& s) { log.push_back(s); }

bool Pipeline::loadModel(const std::string& path, std::string* error) {
    Mesh m;
    if (path.empty()) { m = makeProceduralHead(); note("Using procedural head mesh"); }
    else {
        std::string err;
        if (!loadObj(path, m, &err)) { if (error) *error = err; note("Model load failed: " + err); return false; }
        m.normalizeToUnit();
        note("Loaded " + path + " (" + std::to_string(m.vertexCount()) + " verts, " + std::to_string(m.triangleCount()) + " tris)");
    }
    rig.setMesh(m);
    return true;
}

void Pipeline::buildDefaultRig() {
    rig.buildDefaultFaceRig();
    note("Built default face rig: " + std::to_string(rig.skeleton.bones.size()) + " bones, " + std::to_string(rig.blendShapes.size()) + " blendshapes, " + std::to_string(rig.controlPoints.size()) + " control points");
}

bool Pipeline::loadAudio(const std::string& path, std::string* error) {
    if (path.empty()) { audio = synthesizeTestSpeech(3.0); note("Using synthetic test speech (3 s)"); return true; }
    std::string err;
    if (!loadWav(path, audio, &err)) { if (error) *error = err; note("Audio load failed: " + err); return false; }
    char buf[128]; std::snprintf(buf, sizeof buf, " (%.2f s, %d Hz, %d ch)", audio.duration(), audio.sampleRate, audio.channels);
    note("Loaded " + path + buf);
    return true;
}

bool Pipeline::generateAnimation() {
    if (audio.samples.empty()) { note("No audio loaded"); return false; }
    if (rig.blendShapes.empty()) buildDefaultRig();
    LipSyncGenerator gen(lipSync);
    clip = gen.generate(audio, rig, &features);
    int onsets = 0; for (auto& f : features.frames) onsets += f.onset;
    note("Generated clip: " + std::to_string(clip.frameCount()) + " frames @ " + std::to_string(int(clip.frameRate)) + " fps, " + std::to_string(features.frames.size()) + " audio frames, " + std::to_string(onsets) + " onsets");
    return true;
}

bool Pipeline::exportClip(const AnimationClip& c, const std::string& path, std::string* error, std::string* writtenPath) {
    std::string fbNote;
    auto ex = makeExporterForPath(path, &fbNote);
    std::string out = path;
    if (!fbNote.empty()) { note(fbNote); auto dot = out.find_last_of('.'); out = out.substr(0, dot) + ex->fileExtension(); }
    std::string err;
    if (!ex->exportScene(rig, {c}, out, ExportOptions{}, &err)) { if (error) *error = err; note("Export failed: " + err); return false; }
    note("Exported " + ex->formatName() + " -> " + out);
    if (writtenPath) *writtenPath = out;
    return true;
}

std::vector<std::string> Pipeline::exportAll(const std::string& pattern, const std::string& ext,
                                             const std::vector<Variation>& variations, std::string* error) {
    std::vector<std::string> written;
    std::string e = ext.empty() ? ".glb" : (ext[0] == '.' ? ext : "." + ext);
    std::string base = pattern;
    if (base.size() > e.size() && lower(base.substr(base.size() - e.size())) == lower(e)) base = base.substr(0, base.size() - e.size());
    std::string path = base + e, actual;
    if (!exportClip(clip, path, error, &actual)) return written;
    written.push_back(actual);
    int i = 1;
    for (const auto& v : variations) {
        AnimationClip c = clip; c.name = clip.name + "_" + slug(v.name);
        if (v.apply) v.apply(c);
        std::string p = base + "_var" + std::to_string(i++) + "_" + slug(v.name) + e;
        if (!exportClip(c, p, error, &actual)) return written;
        written.push_back(actual);
    }
    return written;
}

} // namespace fr

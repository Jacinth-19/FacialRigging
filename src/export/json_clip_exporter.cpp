#include "export/exporter.h"
#include "rig/blendshape_io.h"
#include "core/json_io.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fr {

// Format ("frclip" v1):
// { "format":"frclip","version":1,"clips":[ { "name":..,"duration":..,"fps":..,
//     "blendCurves":[{"target":..,"arkit":[..],"times":[..],"values":[..]}],
//     "boneRotations":[{"target":..,"times":[..],"values":[[x,y,z,w],..]}],
//     "boneTranslations":[{"target":..,"times":[..],"values":[[x,y,z],..]}] } ] }
namespace {
std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; }
void arr(std::ostream& o, const std::vector<float>& v) { o << '['; for (size_t i = 0; i < v.size(); ++i) { if (i) o << ','; char b[32]; std::snprintf(b, sizeof b, "%.5g", double(v[i])); o << b; } o << ']'; }
}

bool JsonClipExporter::exportScene(const Rig& rig, const std::vector<AnimationClip>& clips, const std::string& path, const ExportOptions& opts, std::string* error) {
    std::ostringstream o;
    o << "{\"format\":\"frclip\",\"version\":1,";
    { std::string uri = writeAudioSidecar(path, opts, false, nullptr); if (!uri.empty()) o << "\"audio\":{\"uri\":\"" << esc(uri) << "\",\"offset\":" << opts.audioOffset << ",\"sampleRate\":" << opts.audio->sampleRate << ",\"duration\":" << opts.audio->duration() << "},"; }
    o << "\"clips\":[";
    for (size_t ci = 0; ci < clips.size(); ++ci) {
        const auto& c = clips[ci];
        if (ci) o << ',';
        o << "{\"name\":\"" << esc(c.name) << "\",\"duration\":" << c.duration << ",\"fps\":" << c.frameRate << ",\"blendCurves\":[";
        bool first = true;
        for (const auto& bc : c.blendCurves) {
            if (bc.empty()) continue;
            if (!first) o << ',';
            first = false;
            o << "{\"target\":\"" << esc(bc.target) << "\",\"arkit\":[";
            auto names = arkitNamesForShape(bc.target, rig);
            for (size_t i = 0; i < names.size(); ++i) { if (i) o << ','; o << '"' << esc(names[i]) << '"'; }
            o << "],\"times\":"; arr(o, bc.times); o << ",\"values\":"; arr(o, bc.values); o << '}';
        }
        o << "],\"boneRotations\":["; first = true;
        for (const auto& bc : c.boneRotations) {
            if (bc.empty()) continue;
            if (!first) o << ',';
            first = false;
            o << "{\"target\":\"" << esc(bc.target) << "\",\"times\":"; arr(o, bc.times); o << ",\"values\":[";
            for (size_t i = 0; i < bc.values.size(); ++i) { const auto& q = bc.values[i]; if (i) o << ','; o << '[' << q.x << ',' << q.y << ',' << q.z << ',' << q.w << ']'; }
            o << "]}";
        }
        o << "],\"boneTranslations\":["; first = true;
        for (const auto& bc : c.boneTranslations) {
            if (bc.empty()) continue;
            if (!first) o << ',';
            first = false;
            o << "{\"target\":\"" << esc(bc.target) << "\",\"times\":"; arr(o, bc.times); o << ",\"values\":[";
            for (size_t i = 0; i < bc.values.size(); ++i) { const auto& v = bc.values[i]; if (i) o << ','; o << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
            o << "]}";
        }
        o << "]}";
    }
    o << "]}\n";
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    f << o.str();
    return true;
}

// --- minimal reader for the format above (only what we write; tolerant of whitespace)
namespace {
using json::Cursor; using json::objEach; using json::arrEach;
}

bool loadClipsJson(const std::string& path, std::vector<AnimationClip>& out, std::string* error) {
    std::ifstream f(path, std::ios::binary); if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf(); const std::string text = ss.str(); Cursor p(text);
    bool ok = objEach(p, [&](const std::string& k) {
        if (k != "clips") return p.skipValue();
        return arrEach(p, [&]() {
            AnimationClip c;
            bool r = objEach(p, [&](const std::string& ck) {
                if (ck == "name") return p.str(c.name);
                if (ck == "duration") return p.num(c.duration);
                if (ck == "fps") return p.num(c.frameRate);
                if (ck == "blendCurves") return arrEach(p, [&]() { Curve<float> cv; std::string d; bool r2 = objEach(p, [&](const std::string& fk) {
                    if (fk == "target") return p.str(cv.target);
                    if (fk == "times") return p.numArr(cv.times);
                    if (fk == "values") return p.numArr(cv.values);
                    return p.skipValue(); });
                    if (cv.times.size() != cv.values.size()) return false;
                    c.blendCurves.push_back(std::move(cv)); return r2; });
                if (ck == "boneRotations") return arrEach(p, [&]() { Curve<glm::quat> cv; std::vector<std::vector<float>> vals; bool r2 = objEach(p, [&](const std::string& fk) {
                    if (fk == "target") return p.str(cv.target);
                    if (fk == "times") return p.numArr(cv.times);
                    if (fk == "values") return p.vecArr(vals);
                    return p.skipValue(); });
                    if (vals.size() != cv.times.size()) return false;
                    for (auto& q : vals) { if (q.size() != 4) return false; cv.values.push_back(glm::quat(q[3], q[0], q[1], q[2])); }
                    c.boneRotations.push_back(std::move(cv)); return r2; });
                if (ck == "boneTranslations") return arrEach(p, [&]() { Curve<glm::vec3> cv; std::vector<std::vector<float>> vals; bool r2 = objEach(p, [&](const std::string& fk) {
                    if (fk == "target") return p.str(cv.target);
                    if (fk == "times") return p.numArr(cv.times);
                    if (fk == "values") return p.vecArr(vals);
                    return p.skipValue(); });
                    if (vals.size() != cv.times.size()) return false;
                    for (auto& v : vals) { if (v.size() != 3) return false; cv.values.push_back(glm::vec3(v[0], v[1], v[2])); }
                    c.boneTranslations.push_back(std::move(cv)); return r2; });
                return p.skipValue();
            });
            if (r) out.push_back(std::move(c));
            return r;
        });
    });
    if (!ok && error) *error = "malformed clip JSON: " + path;
    return ok;
}

} // namespace fr

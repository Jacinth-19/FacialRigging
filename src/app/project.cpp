#include "app/project.h"
#include "app/pipeline.h"
#include "core/json_io.h"
#include "rig/rig_tools.h"
#include <filesystem>
#include <set>
#include <glm/gtx/quaternion.hpp>
#include <fstream>
#include <sstream>

namespace fr {

namespace {
using json::str; using json::num; using json::Cursor; using json::objEach; using json::arrEach;
namespace fs = std::filesystem;

std::string rel(const std::string& file, const fs::path& base, bool relative) {
    if (file.empty() || !relative) return file;
    std::error_code ec; fs::path r = fs::relative(fs::absolute(file, ec), base, ec);
    return ec || r.empty() ? file : r.generic_string();
}
std::string abs(const std::string& file, const fs::path& base) {
    if (file.empty()) return file;
    fs::path p(file);
    if (p.is_absolute()) return file;
    fs::path cand = base / p; std::error_code ec; return fs::exists(cand, ec) ? cand.string() : file;
}
std::string v3(const glm::vec3& v) { return "[" + num(v.x) + "," + num(v.y) + "," + num(v.z) + "]"; }
std::string q4(const glm::quat& q) { return "[" + num(q.w) + "," + num(q.x) + "," + num(q.y) + "," + num(q.z) + "]"; }
std::string m3(const glm::mat3& m) { std::string o = "["; for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) { if (c || r) o += ','; o += num(m[c][r]); } return o + "]"; }
bool rdV3(Cursor& p, glm::vec3& v) { std::vector<float> a; if (!p.numArr(a) || a.size() != 3) return false; v = {a[0], a[1], a[2]}; return true; }
bool rdQ(Cursor& p, glm::quat& q) { std::vector<float> a; if (!p.numArr(a) || a.size() != 4) return false; q = glm::quat(a[0], a[1], a[2], a[3]); return true; }

std::string curveF(const Curve<float>& c) { return "{\"target\":" + str(c.target) + ",\"times\":" + json::arr(c.times) + ",\"values\":" + json::arr(c.values) + "}"; }
} // namespace

bool saveProject(const std::string& path, const Pipeline& pipe, const ProjectSaveOptions& opts, std::string* error) {
    const Rig& r = pipe.rig; const LipSyncSettings& ls = pipe.lipSync;
    fs::path base = fs::absolute(fs::path(path)).parent_path();
    std::ostringstream o;
    o << "{\"format\":\"frproj\",\"version\":1,\n";
    o << " \"model\":{\"path\":" << str(rel(pipe.modelPath, base, opts.relativePaths)) << ",\"upAxis\":" << int(pipe.modelUpAxis)
      << ",\"userRotation\":" << m3(pipe.userRotation) << ",\"userTranslation\":" << v3(pipe.userTranslation) << "},\n";
    o << " \"audio\":{\"path\":" << str(rel(pipe.audioPath, base, opts.relativePaths)) << ",\"transcript\":" << str(pipe.transcript) << "},\n";
    o << " \"mapper\":{\"kind\":" << (pipe.mapperKind == Pipeline::MapperKind::Ml ? "\"ml\"" : "\"rules\"") << ",\"model\":" << str(pipe.mlModelPath) << ",\"speaker\":" << (pipe.speaker.valid ? pipe.speaker.toJson() : std::string("null")) << "},\n";
    o << " \"lipSync\":{\"frameRate\":" << num(ls.frameRate) << ",\"intensity\":" << num(ls.intensity) << ",\"jawFromLoudness\":" << num(ls.jawFromLoudness) << ",\"browFromPitch\":" << num(ls.browFromPitch)
      << ",\"smileBias\":" << num(ls.smileBias) << ",\"smoothing\":" << ls.smoothingRadiusFrames << ",\"coarticulation\":" << (ls.coarticulation.enabled ? "true" : "false")
      << ",\"tongue\":" << num(ls.tongue) << ",\"jawBoneDegrees\":" << num(ls.jawBoneDegrees) << ",\"jawShapeScale\":" << num(ls.jawShapeScale)
      << ",\"emotion\":" << str(ls.emotion) << ",\"emotionAmount\":" << num(ls.emotionAmount) << ",\"headMotion\":" << num(ls.headMotion) << ",\"gazeMotion\":" << num(ls.gazeMotion)
      << ",\"seed\":" << ls.seed << ",\"blinkRate\":" << num(ls.idle.blinkRate) << ",\"breathing\":" << num(ls.idle.breathing) << "},\n";
    o << " \"export\":{\"audioSidecar\":" << (pipe.exportAudioSidecar ? "true" : "false") << ",\"embedAudioInGlb\":" << (pipe.embedAudioInGlb ? "true" : "false") << "},\n";
    // ---- rig edits
    o << " \"rig\":{\"skinFirst\":" << (r.skinFirst ? "true" : "false") << ",\"forceSymmetry\":" << (r.forceSymmetry ? "true" : "false") << ",\n";
    o << "  \"controlPoints\":[";
    for (size_t i = 0; i < r.controlPoints.size(); ++i) {
        const ControlPoint& c = r.controlPoints[i]; if (i) o << ',';
        o << "{\"name\":" << str(c.name) << ",\"rest\":" << v3(c.restPosition) << ",\"offset\":" << v3(c.offset) << ",\"binding\":" << int(c.binding)
          << ",\"target\":" << str(c.binding == BindingType::Bone && c.target >= 0 && size_t(c.target) < r.skeleton.bones.size() ? r.skeleton.bones[size_t(c.target)].name : c.binding == BindingType::BlendShape && c.target >= 0 && size_t(c.target) < r.blendShapes.size() ? r.blendShapes[size_t(c.target)].name : "")
          << ",\"driveAxis\":" << v3(c.driveAxis) << ",\"driveRange\":" << num(c.driveRange) << ",\"radius\":" << num(c.radius) << "}";
    }
    o << "],\n  \"bonePoses\":[";
    bool first = true;
    for (const Bone& b : r.skeleton.bones) { if (std::abs(glm::angle(b.poseRotation)) < 1e-5f && glm::dot(b.poseTranslation, b.poseTranslation) < 1e-12f) continue; if (!first) o << ','; first = false; o << "{\"bone\":" << str(b.name) << ",\"rotation\":" << q4(b.poseRotation) << ",\"translation\":" << v3(b.poseTranslation) << "}"; }
    o << "],\n  \"blendWeights\":[";
    first = true; for (const BlendShape& bs : r.blendShapes) { if (std::abs(bs.weight) < 1e-5f) continue; if (!first) o << ','; first = false; o << "{\"shape\":" << str(bs.name) << ",\"w\":" << num(bs.weight) << "}"; }
    o << "],\n  \"combinations\":[";
    for (size_t i = 0; i < r.combinations.size(); ++i) { const auto& c = r.combinations[i]; if (i) o << ','; o << "{\"shape\":" << str(c.shape) << ",\"a\":" << str(c.driverA) << ",\"b\":" << str(c.driverB) << ",\"gain\":" << num(c.gain) << ",\"min\":" << (c.useMin ? "true" : "false") << "}"; }
    o << "],\n";
    // user-created shapes = anything not produced by the default/authored build: detect by rebuilding names set
    {
        std::set<std::string> builtin; for (const char* n : shapes::All) builtin.insert(n); for (const auto& a : pipe.authoredShapes()) builtin.insert(a.name);
        o << "  \"userShapes\":[";
        bool f2 = true;
        for (const BlendShape& bs : r.blendShapes) {
            if (builtin.count(bs.name)) continue;
            if (!f2) o << ',';
            f2 = false;
            o << "{\"name\":" << str(bs.name) << ",\"indices\":[";
            for (size_t k = 0; k < bs.indices.size(); ++k) { if (k) o << ','; o << bs.indices[k]; }
            o << "],\"deltas\":[";
            for (size_t k = 0; k < bs.deltas.size(); ++k) { if (k) o << ','; o << num(bs.deltas[k].x) << ',' << num(bs.deltas[k].y) << ',' << num(bs.deltas[k].z); }
            o << "]}";
        }
        o << "],\n";
    }
    // skin weights: only when they differ from a fresh default build (painted / mirrored / imported)
    o << "  \"skin\":";
    bool wroteSkin = false;
    if (opts.includeSkinWeights && r.hasSkin() && pipe.skinEdited) {
        wroteSkin = true;
        o << "{\"bones\":[";
        for (size_t b = 0; b < r.skeleton.bones.size(); ++b) { if (b) o << ','; o << str(r.skeleton.bones[b].name); }
        o << "],\"influences\":[";
        for (size_t i = 0; i < r.skin.size(); ++i) { const auto& inf = r.skin[i]; if (i) o << ','; o << inf.bones.x << ',' << inf.bones.y << ',' << inf.bones.z << ',' << inf.bones.w << ',' << num(inf.weights.x) << ',' << num(inf.weights.y) << ',' << num(inf.weights.z) << ',' << num(inf.weights.w); }
        o << "]}";
    }
    if (!wroteSkin) o << "null";
    o << "\n },\n";
    // ---- clip
    o << " \"clip\":";
    if (opts.includeClip && pipe.clip.duration > 0) {
        const AnimationClip& c = pipe.clip;
        o << "{\"name\":" << str(c.name) << ",\"duration\":" << num(c.duration) << ",\"fps\":" << num(c.frameRate) << ",\"blendCurves\":[";
        for (size_t i = 0; i < c.blendCurves.size(); ++i) { if (i) o << ','; o << curveF(c.blendCurves[i]); }
        o << "],\"boneRotations\":[";
        for (size_t i = 0; i < c.boneRotations.size(); ++i) { const auto& cv = c.boneRotations[i]; if (i) o << ','; o << "{\"target\":" << str(cv.target) << ",\"times\":" << json::arr(cv.times) << ",\"values\":["; for (size_t k = 0; k < cv.values.size(); ++k) { if (k) o << ','; o << q4(cv.values[k]); } o << "]}"; }
        o << "],\"boneTranslations\":[";
        for (size_t i = 0; i < c.boneTranslations.size(); ++i) { const auto& cv = c.boneTranslations[i]; if (i) o << ','; o << "{\"target\":" << str(cv.target) << ",\"times\":" << json::arr(cv.times) << ",\"values\":["; for (size_t k = 0; k < cv.values.size(); ++k) { if (k) o << ','; o << v3(cv.values[k]); } o << "]}"; }
        o << "]";
        if (!c.keyLayer.empty()) o << ",\"keyLayer\":" << c.keyLayer.toJson();
        o << "}";
    } else o << "null";
    o << "\n}\n";
    std::ofstream f(path); if (!f) { if (error) *error = "cannot write " + path; return false; }
    f << o.str();
    return true;
}

bool loadProject(const std::string& path, Pipeline& pipe, std::string* error) {
    std::ifstream f(path, std::ios::binary); if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf(); const std::string text = ss.str();
    fs::path base = fs::absolute(fs::path(path)).parent_path();
    // ---- pass 1: everything into a staging struct
    struct CP { std::string name, target; glm::vec3 rest{0}, offset{0}, axis{0, -1, 0}; int binding = 0; float range = 0.1f, radius = 0.15f; };
    struct BonePose { std::string bone; glm::quat q{1, 0, 0, 0}; glm::vec3 t{0}; };
    struct UserShape { std::string name; std::vector<float> idx, deltas; };
    struct Comb { std::string shape, a, b; float gain = 1; bool mn = false; };
    std::string modelPath, audioPath, transcript, mapperKind = "rules", mlModel; SpeakerProfile speakerProfile; int upAxis = 0; glm::mat3 userRot(1.0f); glm::vec3 userTrans(0);
    std::vector<CP> cps; std::vector<BonePose> poses; std::vector<std::pair<std::string, float>> weights; std::vector<UserShape> userShapes; std::vector<Comb> combs;
    std::vector<std::string> skinBones; std::vector<float> skinInf; bool haveSkin = false, skinFirst = true, forceSym = false;
    bool haveClip = false; AnimationClip clip;
    LipSyncSettings ls = pipe.lipSync; bool audioSidecar = pipe.exportAudioSidecar, embedGlb = pipe.embedAudioInGlb;
    Cursor p(text);
    auto rdF = [&](float& v) { return p.num(v); };
    bool ok = objEach(p, [&](const std::string& k) {
        if (k == "model") return objEach(p, [&](const std::string& mk) {
            if (mk == "path") return p.str(modelPath);
            if (mk == "upAxis") { float v;
            if (!p.num(v)) return false;
            upAxis = int(v);
            return true;
            }
            if (mk == "userRotation") { std::vector<float> a; if (!p.numArr(a) || a.size() != 9) return false; for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) userRot[c][r] = a[size_t(c * 3 + r)]; return true; }
            if (mk == "userTranslation") return rdV3(p, userTrans);
            return p.skipValue();
            });
        if (k == "audio") return objEach(p, [&](const std::string& ak) { if (ak == "path") return p.str(audioPath);
        if (ak == "transcript") return p.str(transcript);
        return p.skipValue();
        });
        if (k == "mapper") return objEach(p, [&](const std::string& mk) { if (mk == "kind") return p.str(mapperKind);
        if (mk == "model") return p.str(mlModel);
        if (mk == "speaker") { size_t start = p.i; p.ws(); if (!p.skipValue()) return false; std::string sub = p.s.substr(start, p.i - start); if (sub.find('{') != std::string::npos) { std::string e; speakerProfile.fromJson(sub.substr(sub.find('{')), &e); } return true; }
        return p.skipValue();
        });
        if (k == "lipSync") return objEach(p, [&](const std::string& lk) {
            if (lk == "frameRate") return rdF(ls.frameRate);
            if (lk == "intensity") return rdF(ls.intensity);
            if (lk == "jawFromLoudness") return rdF(ls.jawFromLoudness);
            if (lk == "browFromPitch") return rdF(ls.browFromPitch);
            if (lk == "smileBias") return rdF(ls.smileBias);
            if (lk == "smoothing") { float v;
            if (!p.num(v)) return false;
            ls.smoothingRadiusFrames = int(v);
            return true;
            }
            if (lk == "coarticulation") return json::boolean(p, ls.coarticulation.enabled);
            if (lk == "tongue") return rdF(ls.tongue);
            if (lk == "jawBoneDegrees") return rdF(ls.jawBoneDegrees);
            if (lk == "jawShapeScale") return rdF(ls.jawShapeScale);
            if (lk == "emotion") return p.str(ls.emotion);
            if (lk == "emotionAmount") return rdF(ls.emotionAmount);
            if (lk == "headMotion") return rdF(ls.headMotion);
            if (lk == "gazeMotion") return rdF(ls.gazeMotion);
            if (lk == "seed") { float v;
            if (!p.num(v)) return false;
            ls.seed = unsigned(v);
            return true;
            } if (lk == "blinkRate") return rdF(ls.idle.blinkRate);
            if (lk == "breathing") return rdF(ls.idle.breathing);
            return p.skipValue(); });
        if (k == "export") return objEach(p, [&](const std::string& ek) { if (ek == "audioSidecar") return json::boolean(p, audioSidecar);
        if (ek == "embedAudioInGlb") return json::boolean(p, embedGlb);
        return p.skipValue();
        });
        if (k == "rig") return objEach(p, [&](const std::string& rk) {
            if (rk == "skinFirst") return json::boolean(p, skinFirst);
            if (rk == "forceSymmetry") return json::boolean(p, forceSym);
            if (rk == "controlPoints") return arrEach(p, [&]() { CP c; bool r = objEach(p, [&](const std::string& ck) {
                if (ck == "name") return p.str(c.name);
                if (ck == "rest") return rdV3(p, c.rest);
                if (ck == "offset") return rdV3(p, c.offset);
                if (ck == "binding") { float v;
                if (!p.num(v)) return false;
                c.binding = int(v);
                return true;
                }
                if (ck == "target") return p.str(c.target);
                if (ck == "driveAxis") return rdV3(p, c.axis);
                if (ck == "driveRange") return rdF(c.range);
                if (ck == "radius") return rdF(c.radius);
                return p.skipValue();
                });
                cps.push_back(c); return r; });
            if (rk == "bonePoses") return arrEach(p, [&]() { BonePose b; bool r = objEach(p, [&](const std::string& bk) { if (bk == "bone") return p.str(b.bone);
            if (bk == "rotation") return rdQ(p, b.q);
            if (bk == "translation") return rdV3(p, b.t);
            return p.skipValue();
            }); poses.push_back(b); return r; });
            if (rk == "blendWeights") return arrEach(p, [&]() { std::string n; float w = 0; bool r = objEach(p, [&](const std::string& wk) { if (wk == "shape") return p.str(n);
            if (wk == "w") return p.num(w);
            return p.skipValue();
            }); weights.push_back({n, w}); return r; });
            if (rk == "combinations") return arrEach(p, [&]() { Comb c; bool r = objEach(p, [&](const std::string& ck) { if (ck == "shape") return p.str(c.shape);
            if (ck == "a") return p.str(c.a);
            if (ck == "b") return p.str(c.b);
            if (ck == "gain") return p.num(c.gain);
            if (ck == "min") return json::boolean(p, c.mn);
            return p.skipValue();
            }); combs.push_back(c); return r; });
            if (rk == "userShapes") return arrEach(p, [&]() { UserShape u; bool r = objEach(p, [&](const std::string& uk) { if (uk == "name") return p.str(u.name);
            if (uk == "indices") return p.numArr(u.idx);
            if (uk == "deltas") return p.numArr(u.deltas);
            return p.skipValue();
            }); userShapes.push_back(std::move(u)); return r; });
            if (rk == "skin") { if (p.peek('n')) return p.skipValue(); haveSkin = true; return objEach(p, [&](const std::string& sk) { if (sk == "bones") return arrEach(p, [&]() { std::string b;
            if (!p.str(b)) return false;
            skinBones.push_back(b);
            return true;
            });
            if (sk == "influences") return p.numArr(skinInf);
            return p.skipValue();
            }); }
            return p.skipValue(); });
        if (k == "clip") {
            if (p.peek('n')) return p.skipValue();
            haveClip = true;
            return objEach(p, [&](const std::string& ck) {
                if (ck == "name") return p.str(clip.name);
                if (ck == "duration") return p.num(clip.duration);
                if (ck == "fps") return p.num(clip.frameRate);
                if (ck == "blendCurves") return arrEach(p, [&]() { Curve<float> cv; bool r = objEach(p, [&](const std::string& fk) { if (fk == "target") return p.str(cv.target);
                if (fk == "times") return p.numArr(cv.times);
                if (fk == "values") return p.numArr(cv.values);
                return p.skipValue();
                });
                if (cv.times.size() == cv.values.size()) clip.blendCurves.push_back(std::move(cv));
                return r;
                });
                if (ck == "boneRotations") return arrEach(p, [&]() { Curve<glm::quat> cv; std::vector<std::vector<float>> vals; bool r = objEach(p, [&](const std::string& fk) { if (fk == "target") return p.str(cv.target);
                if (fk == "times") return p.numArr(cv.times);
                if (fk == "values") return p.vecArr(vals);
                return p.skipValue();
                }); for (auto& v : vals) if (v.size() == 4) cv.values.push_back(glm::quat(v[0], v[1], v[2], v[3]));
                if (cv.times.size() == cv.values.size()) clip.boneRotations.push_back(std::move(cv));
                return r;
                });
                if (ck == "keyLayer") { p.ws(); return clip.keyLayer.fromJson(text, p.i); }
                if (ck == "boneTranslations") return arrEach(p, [&]() { Curve<glm::vec3> cv; std::vector<std::vector<float>> vals; bool r = objEach(p, [&](const std::string& fk) { if (fk == "target") return p.str(cv.target);
                if (fk == "times") return p.numArr(cv.times);
                if (fk == "values") return p.vecArr(vals);
                return p.skipValue();
                }); for (auto& v : vals) if (v.size() == 3) cv.values.push_back(glm::vec3(v[0], v[1], v[2]));
                if (cv.times.size() == cv.values.size()) clip.boneTranslations.push_back(std::move(cv));
                return r;
                });
                return p.skipValue(); });
        }
        return p.skipValue();
    });
    if (!ok) { if (error) *error = "malformed project file"; return false; }

    // ---- pass 2: rebuild the session
    pipe.modelUpAxis = Pipeline::UpAxis(upAxis);
    std::string err;
    if (!pipe.loadModel(abs(modelPath, base), &err)) { if (error) *error = "model: " + err; return false; }
    if (userRot != glm::mat3(1.0f)) pipe.transformModel(userRot);
    if (glm::dot(userTrans, userTrans) > 0) pipe.translateModel(userTrans);
    pipe.mapperKind = mapperKind == "ml" ? Pipeline::MapperKind::Ml : Pipeline::MapperKind::RuleBased; pipe.mlModelPath = mlModel; pipe.speaker = speakerProfile;
    pipe.lipSync = ls; pipe.transcript = transcript; pipe.exportAudioSidecar = audioSidecar; pipe.embedAudioInGlb = embedGlb;
    pipe.buildDefaultRig();
    Rig& r = pipe.rig; r.skinFirst = skinFirst; r.forceSymmetry = forceSym;
    // user shapes
    for (const UserShape& u : userShapes) {
        if (u.idx.size() * 3 != u.deltas.size()) continue;
        BlendShape bs; bs.name = u.name; for (size_t k = 0; k < u.idx.size(); ++k) { uint32_t vi = uint32_t(u.idx[k]); if (vi >= r.mesh.vertexCount()) continue; bs.indices.push_back(vi); bs.deltas.push_back(glm::vec3(u.deltas[k * 3], u.deltas[k * 3 + 1], u.deltas[k * 3 + 2])); }
        int old = r.findBlendShape(bs.name); if (old >= 0) r.blendShapes[size_t(old)] = bs; else r.blendShapes.push_back(bs);
    }
    for (const Comb& c : combs) r.combinations.push_back(CombinationShape{c.shape, c.a, c.b, c.gain, c.mn});
    // skin
    if (haveSkin && r.hasSkin() && skinInf.size() == r.skin.size() * 8) {
        std::vector<int> map(skinBones.size(), 0); for (size_t b = 0; b < skinBones.size(); ++b) { int j = r.skeleton.find(skinBones[b]); map[b] = j >= 0 ? j : 0; }
        for (size_t i = 0; i < r.skin.size(); ++i) { auto& inf = r.skin[i]; for (int k = 0; k < 4; ++k) { int sb = int(skinInf[i * 8 + size_t(k)]); inf.bones[k] = sb >= 0 && size_t(sb) < map.size() ? map[size_t(sb)] : 0; inf.weights[k] = skinInf[i * 8 + 4 + size_t(k)]; } inf.normalize(); }
        pipe.skinEdited = true;
    }
    // control points: replace the defaults with the saved set
    r.controlPoints.clear();
    for (const CP& c : cps) {
        int id = r.addControlPoint(c.rest, c.name); ControlPoint& cp = r.controlPoints[size_t(id)];
        cp.binding = BindingType(c.binding); cp.driveAxis = c.axis; cp.driveRange = c.range; cp.radius = c.radius;
        if (cp.binding == BindingType::Bone) { cp.target = r.skeleton.find(c.target); if (cp.target < 0) cp.binding = BindingType::Unbound; }
        else if (cp.binding == BindingType::BlendShape) { cp.target = r.findBlendShape(c.target); if (cp.target < 0) cp.binding = BindingType::Unbound; }
        cp.offset = c.offset;
    }
    // pose
    for (const BonePose& b : poses) { int j = r.skeleton.find(b.bone); if (j >= 0) { r.skeleton.bones[size_t(j)].poseRotation = b.q; r.skeleton.bones[size_t(j)].poseTranslation = b.t; } }
    for (const auto& [n, w] : weights) r.setBlendWeight(n, w);
    r.applyCombinations();
    // audio + clip
    if (!audioPath.empty() || haveClip) { std::string aerr; if (!pipe.loadAudio(abs(audioPath, base), &aerr)) pipe.log.push_back("Project: audio not loaded (" + aerr + ")"); }
    if (haveClip) { pipe.clip = clip; pipe.log.push_back("Project: restored clip '" + clip.name + "' (" + std::to_string(clip.frameCount()) + " frames)"); }
    pipe.log.push_back("Project loaded: " + path + " (" + std::to_string(cps.size()) + " handles, " + std::to_string(userShapes.size()) + " user shapes, " + std::to_string(combs.size()) + " correctives" + (haveSkin ? ", painted skin" : "") + ")");
    return true;
}

} // namespace fr

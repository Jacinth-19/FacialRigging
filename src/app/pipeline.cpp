#include "app/pipeline.h"
#include "core/scene_import.h"
#include "core/obj_io.h"
#include "export/exporter.h"
#include "audio/ml_viseme_mapper.h"
#include "rig/blendshape_io.h"
#include <fstream>
#include <iterator>
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
    else if (const ExpressionPreset* e = [&]() -> const ExpressionPreset* { for (int i = 0; i < kExpressionPresetCount; ++i) if (t.find(kExpressionPresets[i].name) != std::string::npos) return &kExpressionPresets[i]; return nullptr; }()) {
        float amt = 0.8f; (void)parseKV(t, e->name, "=", amt);
        v.apply = [e, amt](AnimationClip& c) {
            c.offsetBlendCurve(shapes::MouthSmile, e->smile * amt); c.offsetBlendCurve(shapes::MouthFrown, e->frown * amt);
            c.offsetBlendCurve(shapes::BrowRaise, e->browRaise * amt); c.offsetBlendCurve(shapes::BrowDown, e->browDown * amt);
            c.offsetBlendCurve(shapes::EyeWide, e->eyeWide * amt); c.offsetBlendCurve(shapes::LipsPress, e->lipsPress * amt);
            c.offsetBlendCurve(shapes::MouthPucker, e->pucker * amt); c.offsetBlendCurve(shapes::JawOpen, e->jaw * amt);
        };
    }
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
    modelPath = path; userRotation = glm::mat3(1.0f); userTranslation = glm::vec3(0.0f);
    importedSkeleton = false; importedClips.clear(); importedSkel_ = Skeleton{}; importedSkin_.clear();
    std::string ext; { auto dot = path.find_last_of('.'); if (dot != std::string::npos) { ext = path.substr(dot); for (auto& c : ext) c = char(std::tolower((unsigned char)c)); } }
    if (!path.empty() && ext != ".obj") {
        // rigged character through Assimp (FBX / glTF / DAE ...)
        SceneImportResult in; SceneImportOptions io; std::string err;
        if (!importScene(path, io, in, &err)) { if (error) *error = err; note("Import failed: " + err); return false; }
        note("Imported " + path + ": " + in.log);
        modelTransform = ModelTransform{};
        rig.setMesh(in.mesh);
        authoredShapes_ = std::move(in.blendShapes); authoredShapesPath = path; authoredCanonicalCoverage = 0;
        if (!in.skeleton.bones.empty() && in.hasSkin()) { importedSkeleton = true; importedSkel_ = in.skeleton; importedSkin_ = in.skin; }
        importedClips = std::move(in.clips);
        return true;
    }
    if (path.empty()) { m = makeProceduralHead(); note("Using procedural head mesh"); }
    else {
        std::string err;
        if (!loadObj(path, m, &err)) { if (error) *error = err; note("Model load failed: " + err); return false; }
        bool zUp = modelUpAxis == UpAxis::Z || (modelUpAxis == UpAxis::Auto && m.looksZUp());
        if (zUp) { m.zUpToYUp(); note("Model treated as Z-up: rotated to Y-up (override with --up y)"); }
        glm::vec3 lo = m.boundsMin(), hi = m.boundsMax();
        modelTransform.zUp = zUp; modelTransform.centre = 0.5f * (lo + hi);
        float ext = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
        modelTransform.scale = ext > 1e-9f ? 1.0f / ext : 1.0f;
        m.normalizeToUnit();
        std::string partsNote;
        if (!m.parts.empty()) { partsNote = ", " + std::to_string(m.parts.size()) + " parts:"; for (auto& pt : m.parts) partsNote += " " + pt.name; }
        note("Loaded " + path + " (" + std::to_string(m.vertexCount()) + " verts, " + std::to_string(m.triangleCount()) + " tris" + partsNote + ")");
    }
    rig.setMesh(m);
    authoredShapes_.clear(); authoredShapesPath.clear(); authoredCanonicalCoverage = 0;
    // Authored blendshapes: <model>.fbs next to the OBJ (see tools/prepare_ict_facekit.py)
    if (!path.empty()) {
        auto dot = path.find_last_of('.');
        std::string fbs = (dot == std::string::npos ? path : path.substr(0, dot)) + ".fbs";
        std::string err;
        std::vector<BlendShape> shapesIn;
        size_t srcCount = 0; for (uint32_t sv : rig.mesh.sourceVertex) srcCount = std::max<size_t>(srcCount, sv + 1);
        if (loadBlendShapesFRBS(fbs, shapesIn, srcCount ? srcCount : rig.mesh.vertexCount(), &err)) {
            remapBlendShapesToMesh(shapesIn, rig.mesh);
            for (auto& bs : shapesIn) for (auto& d : bs.deltas) { if (modelTransform.zUp) d = glm::vec3(d.x, d.z, -d.y); d *= modelTransform.scale; }
            authoredShapes_ = std::move(shapesIn); authoredShapesPath = fbs;
            note("Loaded " + std::to_string(authoredShapes_.size()) + " authored blendshapes from " + fbs);
        } else if (std::ifstream(fbs).good()) note("Blendshape file ignored: " + err);
    }
    return true;
}

void Pipeline::transformModel(const glm::mat3& R) {
    userRotation = R * userRotation;
    Mesh m = rig.mesh;
    if (m.positions.empty()) return;
    glm::vec3 lo = m.boundsMin(), hi = m.boundsMax(); float extBefore = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    for (auto& p : m.positions) p = R * p;
    for (auto& n : m.normals) n = glm::normalize(R * n);
    m.normalizeToUnit();
    lo = m.boundsMin(); hi = m.boundsMax(); float extAfter = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    float k = extAfter > 1e-9f ? extAfter / extBefore : 1.0f; // normaliseToUnit scale change
    for (auto& bs : authoredShapes_) for (auto& d : bs.deltas) d = k * (R * d);
    rig.setMesh(m);
}

void Pipeline::translateModel(const glm::vec3& d) {
    userTranslation += d;
    for (auto& p : rig.mesh.positions) p += d;
    Mesh m = rig.mesh; rig.setMesh(m);
}

bool Pipeline::detectLandmarksNow() {
    std::string why;
    if (!landmarkerAvailable(&why)) { lastLandmarks = FaceLandmarks{}; lastLandmarks.note = why; note("Auto-landmarking unavailable: " + why); return false; }
    lastLandmarks = detectLandmarks(rig.mesh);
    if (lastLandmarks.found) {
        // Sanity: the detected mouth must sit below the eyes and inside the bounds, else distrust it.
        const glm::vec3 lo = rig.mesh.boundsMin(), hi = rig.mesh.boundsMax();
        bool sane = lastLandmarks.mouth.y < lastLandmarks.eyeL.y && lastLandmarks.eyeL.x < lastLandmarks.eyeR.x && lastLandmarks.cornerL.x < lastLandmarks.cornerR.x
                 && lastLandmarks.mouth.y > lo.y && lastLandmarks.browL.y < hi.y;
        if (!sane) { lastLandmarks.found = false; lastLandmarks.note += " (rejected: implausible layout)"; }
    }
    note(std::string(lastLandmarks.found ? "Auto-landmarks: " : "Auto-landmarks failed: ") + lastLandmarks.note);
    return lastLandmarks.found;
}

void Pipeline::buildDefaultRig() {
    if (autoLandmarks != AutoLandmarks::Off && rig.mesh.vertexCount() > 0) {
        std::string why;
        if (autoLandmarks == AutoLandmarks::On || landmarkerAvailable(&why)) detectLandmarksNow();
        else { lastLandmarks = FaceLandmarks{}; lastLandmarks.note = why; }
    } else lastLandmarks = FaceLandmarks{};
    const FaceLandmarks* lm = lastLandmarks.found ? &lastLandmarks : nullptr;
    if (importedSkeleton && importedSkin_.size() == rig.mesh.vertexCount()) {
        // Keep the character's own skeleton and weights; the default builder only contributes
        // control points and procedural fallbacks for canonical shapes the file doesn't have.
        rig.buildDefaultFaceRig(lm);
        rig.skeleton = importedSkel_; rig.skin = importedSkin_;
        for (auto& cp : rig.controlPoints) if (cp.binding == BindingType::Bone) { int j = rig.skeleton.find(cp.target == 1 ? Rig::kJawBone : Rig::kHeadBone); if (j >= 0) cp.target = j; else cp.binding = BindingType::Unbound; }
        int jaw = rig.skeleton.find(Rig::kJawBone);
        if (jaw < 0) { lipSync.jawBoneDegrees = 0.0f; note("Imported skeleton has no 'Jaw' bone: jaw motion goes through the JawOpen shape only"); }
        if (!authoredShapes_.empty()) authoredCanonicalCoverage = rig.installAuthoredBlendShapes(authoredShapes_);
        note("Using imported skeleton: " + std::to_string(rig.skeleton.bones.size()) + " bones, " + std::to_string(authoredCanonicalCoverage) + "/" + std::to_string(std::size(shapes::All)) + " canonical shapes from " + std::to_string(authoredShapes_.size()) + " imported morph targets");
        if (!importedClips.empty() && clip.duration <= 0) { clip = importedClips[0]; note("Loaded imported animation '" + clip.name + "'"); }
        return;
    }
    rig.buildDefaultFaceRig(lm);
    if (!authoredShapes_.empty()) {
        authoredCanonicalCoverage = rig.installAuthoredBlendShapes(authoredShapes_);
        // An authored jawOpen already drops the whole lower face; keep the bone for the inner
        // mouth parts (teeth/tongue) but scale the two so they do not add up to an over-open jaw.
        lipSync.jawShapeScale = 0.6f; lipSync.jawBoneDegrees = 6.0f;
        note("Installed authored blendshapes: " + std::to_string(authoredCanonicalCoverage) + "/" + std::to_string(std::size(shapes::All)) + " canonical shapes replaced, " + std::to_string(authoredShapes_.size()) + " source shapes kept");
    }
    auto parts = rig.detectParts();
    std::string anat;
    if (parts.any()) anat = " (anatomical parts: " + std::string(parts.teethLower >= 0 ? "lower teeth/gums/tongue follow the jaw; " : "") + std::string(parts.browL >= 0 ? "eyebrows; " : "") + std::string(parts.eyeL >= 0 ? "eyeballs" : "") + ")";
    note("Built default face rig: " + std::to_string(rig.skeleton.bones.size()) + " bones, " + std::to_string(rig.blendShapes.size()) + " blendshapes, " + std::to_string(rig.controlPoints.size()) + " control points" + anat);
}

bool Pipeline::loadAudio(const std::string& path, std::string* error) {
    audioPath = path;
    if (path.empty()) { audio = synthesizeTestSpeech(3.0); note("Using synthetic test speech (3 s)"); return true; }
    std::string err;
    if (!loadWav(path, audio, &err)) { if (error) *error = err; note("Audio load failed: " + err); return false; }
    char buf[128]; std::snprintf(buf, sizeof buf, " (%.2f s, %d Hz, %d ch)", audio.duration(), audio.sampleRate, audio.channels);
    note("Loaded " + path + buf);
    return true;
}

std::shared_ptr<VisemeMapper> Pipeline::makeMapper(std::string* noteOut) const {
    if (mapperKind == MapperKind::Ml) {
        auto ml = std::make_shared<MlVisemeMapper>();
        std::string err;
        bool ok = false;
        if (!mlModelPath.empty()) ok = ml->load(mlModelPath, &err);
        else { ok = ml->loadDefault(assetDir.empty() ? "assets" : assetDir, &err); if (!ok) { std::string e2; ok = ml->loadBuiltin(&e2); if (!ok) err += "; " + e2; } }
        if (ok) { if (noteOut) *noteOut = "ML viseme mapper: " + ml->modelInfo(); return ml; }
        if (noteOut) *noteOut = "ML mapper unavailable (" + err + "); using rule-based mapper";
    } else if (noteOut) *noteOut = "rule-based viseme mapper";
    return std::make_shared<VisemeMapper>();
}

bool Pipeline::generateAnimation() {
    if (audio.samples.empty()) { note("No audio loaded"); return false; }
    if (rig.blendShapes.empty()) buildDefaultRig();
    std::string mnote;
    auto mapper = makeMapper(&mnote);
    note(mnote);
    FeatureExtractor fx;
    features = fx.extract(audio);
    auto visemes = mapper->map(features);
    LipSyncGenerator gen(lipSync);
    lastAlignment = AlignmentResult{};
    if (!transcript.empty()) {
        PhonemeAligner aligner(alignment);
        lastAlignment = aligner.align(transcript, features, visemes);
        if (lastAlignment.phones.empty()) { note("Transcript alignment failed (no phones) - falling back to acoustic visemes"); lastSegments = segmentVisemes(visemes, features.frameInterval(), lipSync.coarticulation.minSegmentSec); }
        else {
            lastSegments = lastAlignment.segments;
            char b[160]; std::snprintf(b, sizeof b, "Aligned transcript: %zu words, %zu phones, mean log-post %.2f, speech coverage %.0f%%", lastAlignment.words.size(), lastAlignment.phones.size(), lastAlignment.meanLogPosterior, lastAlignment.coverage * 100.0f);
            note(b);
        }
        KeyLayer keep = clip.keyLayer;
        clip = gen.generate(features, lastSegments, visemes, rig);
        clip.keyLayer = keep;
    } else {
        lastSegments = segmentVisemes(visemes, features.frameInterval(), lipSync.coarticulation.minSegmentSec);
        KeyLayer keep = clip.keyLayer;
        clip = gen.generate(features, visemes, rig);
        clip.keyLayer = keep;
    }
    if (!clip.keyLayer.empty()) note("Kept " + std::to_string(clip.keyLayer.keyCount()) + " user keys from the key layer");
    int onsets = 0; for (auto& f : features.frames) onsets += f.onset;
    note("Generated clip: " + std::to_string(clip.frameCount()) + " frames @ " + std::to_string(int(clip.frameRate)) + " fps, " + std::to_string(features.frames.size()) + " audio frames, " + std::to_string(onsets) + " onsets");
    return true;
}

bool Pipeline::exportClip(const AnimationClip& cIn, const std::string& path, std::string* error, std::string* writtenPath) {
    const bool keepLayer = path.size() > 5 && path.compare(path.size() - 5, 5, ".json") == 0;   // clip JSON round-trips the editable layer
    const AnimationClip c = keepLayer ? cIn : cIn.flattened();
    std::string fbNote;
    auto ex = makeExporterForPath(path, &fbNote);
    std::string out = path;
    if (!fbNote.empty()) { note(fbNote); auto dot = out.find_last_of('.'); out = out.substr(0, dot) + ex->fileExtension(); }
    std::string err;
    ExportOptions eo; if (exportAudioSidecar && !audio.samples.empty()) { eo.audio = &audio; eo.audioOffset = 0.0f; eo.embedAudioInGlb = embedAudioInGlb; }
    if (!ex->exportScene(rig, {c}, out, eo, &err)) { if (error) *error = err; note("Export failed: " + err); return false; }
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

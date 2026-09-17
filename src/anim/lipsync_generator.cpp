#include "anim/lipsync_generator.h"
#include "rig/rig.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace fr {

//                                   name        smile frown browUp browDn eyeWide jaw  press pucker
const ExpressionPreset kExpressionPresets[] = {
    {"neutral",   0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f},
    {"happy",     0.65f, 0.00f, 0.25f, 0.00f, 0.10f, 0.05f, 0.00f, 0.00f},
    {"sad",       0.00f, 0.55f, 0.35f, 0.10f, 0.00f, 0.00f, 0.15f, 0.05f},
    {"angry",     0.00f, 0.40f, 0.00f, 0.80f, 0.20f, 0.05f, 0.45f, 0.00f},
    {"surprised", 0.10f, 0.00f, 0.90f, 0.00f, 0.80f, 0.35f, 0.00f, 0.15f},
    {"disgusted", 0.00f, 0.50f, 0.00f, 0.45f, 0.00f, 0.00f, 0.30f, 0.10f},
};
const int kExpressionPresetCount = int(sizeof(kExpressionPresets) / sizeof(kExpressionPresets[0]));
const ExpressionPreset* findExpressionPreset(const std::string& name) {
    for (int i = 0; i < kExpressionPresetCount; ++i) if (name == kExpressionPresets[i].name) return &kExpressionPresets[i];
    return nullptr;
}

namespace {
// Tiny deterministic value-noise (smooth random in [-1,1]) for head sway / gaze drift.
struct Noise1D {
    unsigned seed;
    float hash(int i) const { unsigned x = unsigned(i) * 374761393u + seed * 668265263u; x = (x ^ (x >> 13)) * 1274126177u; return float(x & 0xffffff) / float(0xffffff) * 2.0f - 1.0f; }
    float at(float t) const { int i = int(std::floor(t)); float f = t - float(i); f = f * f * (3 - 2 * f); return hash(i) * (1 - f) + hash(i + 1) * f; }
};
}

AnimationClip LipSyncGenerator::generate(const AudioBuffer& audio, const Rig& rig, FeatureTrack* outFeatures) const {
    FeatureExtractor fx;
    FeatureTrack track = fx.extract(audio);
    VisemeMapper mapper;
    auto vis = mapper.map(track);
    if (outFeatures) *outFeatures = track;
    return generate(track, vis, rig);
}

AnimationClip LipSyncGenerator::generate(const FeatureTrack& features, const std::vector<VisemeFrame>& visemes, const Rig& rig) const {
    AnimationClip clip;
    clip.name = "LipSync";
    clip.frameRate = settings.frameRate;
    clip.duration = float(features.duration);
    const int frames = clip.frameCount();
    const float dt = 1.0f / settings.frameRate;

    // Curves for every blendshape in the rig (keeps export complete even if a shape stays 0).
    for (const auto& bs : rig.blendShapes) { Curve<float> c; c.target = bs.name; c.times.reserve(frames); c.values.reserve(frames); clip.blendCurves.push_back(c); }
    auto curve = [&](const char* n) { return clip.findBlendCurve(n); };
    Curve<float>* jaw = curve(shapes::JawOpen); Curve<float>* smile = curve(shapes::MouthSmile);
    Curve<float>* pucker = curve(shapes::MouthPucker); Curve<float>* wide = curve(shapes::MouthWide);
    Curve<float>* press = curve(shapes::LipsPress); Curve<float>* funnel = curve(shapes::MouthFunnel);
    Curve<float>* brow = curve(shapes::BrowRaise); Curve<float>* blink = curve(shapes::EyeBlink);
    Curve<float>* frown = curve(shapes::MouthFrown); Curve<float>* browDown = curve(shapes::BrowDown); Curve<float>* eyeWide = curve(shapes::EyeWide);
    const ExpressionPreset* emo = findExpressionPreset(settings.emotion); const float E = emo ? std::clamp(settings.emotionAmount, 0.0f, 1.0f) : 0.0f;
    auto emoAdd = [&](float v) { return emo ? v * E : 0.0f; };

    // Speaker pitch median for relative intonation.
    std::vector<float> voicedPitch;
    for (auto& f : features.frames) if (f.pitchHz > 0 && f.voicing > 0.5f) voicedPitch.push_back(f.pitchHz);
    float medianPitch = 0.0f;
    if (!voicedPitch.empty()) { std::nth_element(voicedPitch.begin(), voicedPitch.begin() + voicedPitch.size() / 2, voicedPitch.end()); medianPitch = voicedPitch[voicedPitch.size() / 2]; }

    const double frameInterval = features.frameInterval();
    auto visemeAt = [&](double t) -> const VisemeFrame* {
        if (visemes.empty()) return nullptr;
        long i = frameInterval > 0 ? std::lround(t / frameInterval) : 0;
        i = std::clamp<long>(i, 0, long(visemes.size()) - 1);
        return &visemes[size_t(i)];
    };

    int jawBone = rig.skeleton.find("Jaw");
    Curve<glm::quat> jawRot; jawRot.target = "Jaw";
    const int headBone = rig.skeleton.find(Rig::kHeadBone), eyeL = rig.skeleton.find(Rig::kEyeLBone), eyeR = rig.skeleton.find(Rig::kEyeRBone);
    Curve<glm::quat> headRot; headRot.target = Rig::kHeadBone;
    Curve<glm::quat> eyeLRot; eyeLRot.target = Rig::kEyeLBone; Curve<glm::quat> eyeRRot; eyeRRot.target = Rig::kEyeRBone;
    Noise1D nSway{settings.seed * 3 + 1}, nNod{settings.seed * 3 + 2}, nGaze{settings.seed * 3 + 3}, nGazeY{settings.seed * 7 + 5};
    // Saccades: a new fixation every 0.8-2.5 s, gaze jumps then holds.
    float nextSaccade = 0.6f, gazeYaw = 0.0f, gazePitch = 0.0f, curYaw = 0.0f, curPitch = 0.0f;
    // Smoothed loudness envelope drives nods (slow) and emphasis (onset).
    float loudEnv = 0.0f, nod = 0.0f, nodVel = 0.0f;

    for (int i = 0; i < frames; ++i) {
        float t = i * dt;
        const AudioFrameFeatures* f = features.at(t);
        const VisemeFrame* v = visemeAt(t);
        float loud = f ? f->loudness : 0.0f;
        MouthPose pose = mouthPose(v ? *v : VisemeFrame{}, loud);
        float I = settings.intensity;
        float jawW = pose.jawOpen;
        float browW = 0.0f;
        if (f && medianPitch > 0 && f->pitchHz > 0 && f->voicing > 0.5f)
            browW = std::clamp(settings.browFromPitch * std::log2(f->pitchHz / medianPitch) * 2.0f, 0.0f, 1.0f);
        float blinkW = 0.0f;
        if (settings.blinkIntervalSec > 0) {
            float phase = std::fmod(t + 0.9f, settings.blinkIntervalSec);
            if (phase < settings.blinkDurationSec) blinkW = std::sin(float(M_PI) * phase / settings.blinkDurationSec);
        }
        auto cl = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
        if (jaw) jaw->addKey(t, cl(jawW * settings.jawShapeScale + emoAdd(emo ? emo->jaw : 0)));
        if (smile) smile->addKey(t, cl(pose.smile + emoAdd(emo ? emo->smile : 0)));
        if (pucker) pucker->addKey(t, cl(pose.pucker + emoAdd(emo ? emo->pucker : 0)));
        if (wide) wide->addKey(t, pose.wide);
        if (press) press->addKey(t, cl(pose.lipsPress + emoAdd(emo ? emo->lipsPress : 0)));
        if (funnel) funnel->addKey(t, pose.funnel);
        (void)I;
        if (brow) brow->addKey(t, cl(browW + emoAdd(emo ? emo->browRaise : 0)));
        if (blink) blink->addKey(t, blinkW);
        if (frown) frown->addKey(t, emoAdd(emo ? emo->frown : 0));
        if (browDown) browDown->addKey(t, emoAdd(emo ? emo->browDown : 0));
        if (eyeWide) eyeWide->addKey(t, cl(emoAdd(emo ? emo->eyeWide : 0) - blinkW));
        if (jawBone >= 0) jawRot.addKey(t, glm::angleAxis(glm::radians(settings.jawBoneDegrees * jawW), glm::vec3(1, 0, 0)));
        // --- head motion: slow sway + loudness-driven nod (critically damped spring on the envelope)
        if (headBone >= 0 && settings.headMotion > 0.0f) {
            loudEnv += (loud - loudEnv) * std::min(1.0f, 8.0f * dt);
            float target = (f && f->onset) ? 1.0f : loudEnv * 0.6f;
            float acc = 180.0f * (target - nod) - 22.0f * nodVel; nodVel += acc * dt; nod += nodVel * dt;
            float m = settings.headMotion;
            float yaw = glm::radians(3.5f * m) * nSway.at(t * 0.35f), roll = glm::radians(2.0f * m) * nSway.at(t * 0.5f + 100.0f);
            float pitch = glm::radians(2.5f * m) * nNod.at(t * 0.6f + 50.0f) + glm::radians(3.0f * m) * nod;
            headRot.addKey(t, glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(pitch, glm::vec3(1, 0, 0)) * glm::angleAxis(roll, glm::vec3(0, 0, 1)));
        }
        // --- gaze: saccade + hold, with slight drift; blinks are the natural moment for a saccade
        if (eyeL >= 0 && eyeR >= 0 && settings.gazeMotion > 0.0f) {
            if (t >= nextSaccade) { gazeYaw = 14.0f * nGaze.at(t * 10.0f); gazePitch = 8.0f * nGazeY.at(t * 10.0f); nextSaccade = t + 0.8f + 1.7f * (0.5f + 0.5f * nGaze.at(t * 3.0f + 7.0f)); }
            float k = std::min(1.0f, 25.0f * dt); curYaw += (gazeYaw - curYaw) * k; curPitch += (gazePitch - curPitch) * k; // ~40 ms saccade
            float g = settings.gazeMotion;
            float yawR = glm::radians(g * (curYaw + 1.5f * nGaze.at(t * 0.8f + 30.0f))), pitchR = glm::radians(g * (curPitch + 1.0f * nGazeY.at(t * 0.7f + 31.0f)));
            glm::quat q = glm::angleAxis(yawR, glm::vec3(0, 1, 0)) * glm::angleAxis(-pitchR, glm::vec3(1, 0, 0));
            eyeLRot.addKey(t, q); eyeRRot.addKey(t, q);
        }
    }
    // Any remaining curves with no keys get a flat zero so exporters see a full set.
    for (auto& c : clip.blendCurves) if (c.empty()) { c.addKey(0.0f, 0.0f); c.addKey(clip.duration, 0.0f); }
    if (jawBone >= 0) clip.boneRotations.push_back(jawRot);
    if (!headRot.empty()) clip.boneRotations.push_back(headRot);
    if (!eyeLRot.empty()) { clip.boneRotations.push_back(eyeLRot); clip.boneRotations.push_back(eyeRRot); }
    clip.smoothBlendCurves(settings.smoothingRadiusFrames);
    return clip;
}

LipSyncGenerator::MouthPose LipSyncGenerator::mouthPose(const VisemeFrame& v, float loud) const {
    VisemePose pose{};
    for (size_t k = 0; k < v.weights.size(); ++k) {
        const VisemePose& p = visemePose(Viseme(k)); float w = v.weights[k];
        pose.jawOpen += w * p.jawOpen; pose.smile += w * p.smile; pose.pucker += w * p.pucker;
        pose.wide += w * p.wide; pose.lipsPress += w * p.lipsPress; pose.funnel += w * p.funnel;
    }
    const float I = settings.intensity;
    MouthPose m;
    m.jawOpen = std::clamp((pose.jawOpen + settings.jawFromLoudness * loud) * I, 0.0f, 1.0f);
    m.smile = std::clamp(pose.smile * I + settings.smileBias, 0.0f, 1.0f);
    m.pucker = std::clamp(pose.pucker * I, 0.0f, 1.0f);
    m.wide = std::clamp(pose.wide * I, 0.0f, 1.0f);
    m.lipsPress = std::clamp(pose.lipsPress * I, 0.0f, 1.0f);
    m.funnel = std::clamp(pose.funnel * I, 0.0f, 1.0f);
    return m;
}

void LipSyncGenerator::applyVisemeToRig(const VisemeFrame& v, const AudioFrameFeatures& f, Rig& rig) const {
    MouthPose m = mouthPose(v, f.loudness);
    rig.setBlendWeight(shapes::JawOpen, m.jawOpen * settings.jawShapeScale); rig.setBlendWeight(shapes::MouthSmile, m.smile);
    rig.setBlendWeight(shapes::MouthPucker, m.pucker); rig.setBlendWeight(shapes::MouthWide, m.wide);
    rig.setBlendWeight(shapes::LipsPress, m.lipsPress); rig.setBlendWeight(shapes::MouthFunnel, m.funnel);
    int jaw = rig.skeleton.find("Jaw");
    if (jaw >= 0) rig.skeleton.bones[size_t(jaw)].poseRotation = glm::angleAxis(glm::radians(settings.jawBoneDegrees * m.jawOpen), glm::vec3(1, 0, 0));
    rig.syncControlPointsFromRig();
}

} // namespace fr

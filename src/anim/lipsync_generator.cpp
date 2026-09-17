#include "anim/lipsync_generator.h"
#include "rig/rig.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace fr {

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
        if (jaw) jaw->addKey(t, jawW * settings.jawShapeScale);
        if (smile) smile->addKey(t, pose.smile);
        if (pucker) pucker->addKey(t, pose.pucker);
        if (wide) wide->addKey(t, pose.wide);
        if (press) press->addKey(t, pose.lipsPress);
        if (funnel) funnel->addKey(t, pose.funnel);
        (void)I;
        if (brow) brow->addKey(t, browW);
        if (blink) blink->addKey(t, blinkW);
        if (jawBone >= 0) jawRot.addKey(t, glm::angleAxis(glm::radians(settings.jawBoneDegrees * jawW), glm::vec3(1, 0, 0)));
    }
    // Any remaining curves with no keys get a flat zero so exporters see a full set.
    for (auto& c : clip.blendCurves) if (c.empty()) { c.addKey(0.0f, 0.0f); c.addKey(clip.duration, 0.0f); }
    if (jawBone >= 0) clip.boneRotations.push_back(jawRot);
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

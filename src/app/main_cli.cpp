// Headless command-line entry point. This is the surface the Arena agent drives:
//   fr_cli --model character.obj --audio dialogue.wav --output scene --format fbx
//          --variation "Increase smile" --variation "Raise eyebrows"
#include "app/pipeline.h"
#include "core/obj_io.h"
#include "export/exporter.h"
#include <cctype>
#include "audio/live_capture.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include "app/project.h"
#include "export/arkit_livelink.h"

using namespace fr;

static void usage() {
    std::puts(R"(fr_cli - FacialRigging headless pipeline
Options:
  --model <file.obj>        3D face model (default: procedural head)
  --audio <file.wav>        speech clip (default: synthetic test speech)
  --output <pattern>        output base name (default: out/scene)
  --format <fbx|glb|gltf|json|csv>  export format (default: glb; fbx falls back to glb without the SDK; json = mesh-free curves; csv = ARKit 52-coefficient mocap sheet)
  --project <file.frproj>   open a saved session (model, audio, rig edits, settings, clip) instead of --model/--audio
  --save-project <file>     write the session as a project file after generating
  --no-audio-sidecar        don't write <out>.wav + offsets next to exports
  --embed-audio             pack the WAV into .glb (asset.extras.audio.bufferView)
  --livelink <host:port> [<f>] stream the clip as ARKit Live Link UDP packets at real time (default speed 1.0), then exit
  --variation <text>        add a variation export (repeatable), e.g. "Increase smile", "intensity=1.3"
  --variations <n>          shorthand: n default variations (smile, brows, subtle, exaggerated)
  --mapper <rules|ml>       viseme mapper (default rules; ml needs a LibTorch build)
  --model-pt <file.pt>      TorchScript model for --mapper ml (default: built-in MLP)
  --up <auto|y|z>           up axis of the OBJ (default auto-detect; scans are often Z-up)
  --list-devices            list PortAudio input devices and exit
  --live-test <dev> <sec>   run live capture from device index (-1 default, -2 built-in test signal) and print visemes
  --fps <n>                 bake frame rate (default 30)
  --intensity <f>           global mouth intensity (default 1.0)
  --emotion <name> [<f>]    performance layer: neutral|happy|sad|angry|surprised|disgusted, amount 0..1 (default 0.8)
  --head-motion <f>         audio-driven head nods / sway 0..1 (default 0.5)
  --gaze-motion <f>         eye saccades 0..1, needs eyeball parts (default 0.5)
  --transcript "<text>"     force-align the spoken text (CMUdict + rules, [ARPAbet] allowed) to the audio
  --clip-in <file.json>     skip generation; load a clip JSON (from --format json) and export it
  --save-audio <file.wav>   write the (synthetic) audio next to the export
  --save-model <file.obj>   write the (procedural) bind mesh as OBJ
  --dump-features           print per-frame features to stdout
  -h, --help
)");
}

int main(int argc, char** argv) {
    std::string model, audioPath, output = "out/scene", format = "glb", saveAudio, saveModel, clipIn, project, saveProjectPath, liveLink; float liveSpeed = 1.0f;
    std::vector<std::string> variationTexts;
    bool dump = false;
    Pipeline pipe;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--model") model = next();
        else if (a == "--audio") audioPath = next();
        else if (a == "--output") output = next();
        else if (a == "--format") format = next();
        else if (a == "--variation") variationTexts.push_back(next());
        else if (a == "--variations") {
            int n = std::atoi(next().c_str());
            const char* defaults[] = {"Increase smile", "Raise eyebrows", "Subtle", "Exaggerated"};
            for (int k = 0; k < n && k < 4; ++k) variationTexts.push_back(defaults[k]);
        }
        else if (a == "--mapper") { std::string m = next(); pipe.mapperKind = (m == "ml") ? Pipeline::MapperKind::Ml : Pipeline::MapperKind::RuleBased; }
        else if (a == "--model-pt") pipe.mlModelPath = next();
        else if (a == "--up") { std::string u = next(); pipe.modelUpAxis = u == "z" ? Pipeline::UpAxis::Z : u == "y" ? Pipeline::UpAxis::Y : Pipeline::UpAxis::Auto; }
        else if (a == "--list-devices") {
            std::string err; auto devs = LiveCapture::listInputDevices(&err);
            std::printf("%s\n", LiveCapture::backendInfo().c_str());
            if (!err.empty()) std::printf("note: %s\n", err.c_str());
            for (auto& d : devs) std::printf("[%d] %s (%d ch, %.0f Hz)\n", d.index, d.name.c_str(), d.maxInputChannels, d.defaultSampleRate);
            return 0;
        }
        else if (a == "--live-test") {
            // Capture from a device (default: -2 test signal) for N seconds and print the viseme stream.
            int dev = std::atoi(next().c_str()); double secs = std::atof(next().c_str()); if (secs <= 0) secs = 3;
            LiveCapture lc; std::string err;
            lc.setMapper(pipe.makeMapper());
            if (!lc.start(dev, 16000, &err)) { std::fprintf(stderr, "live capture failed: %s\n", err.c_str()); return 1; }
            auto t0 = std::chrono::steady_clock::now(); int frames = 0, speaking = 0;
            while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
                auto f = lc.poll();
                if (f.valid) { ++frames; if (f.viseme.dominant() != Viseme::Silence) ++speaking;
                    std::printf("t=%6.2f level=%.2f loud=%.2f pitch=%5.0f  %s\n", f.features.time, lc.inputLevel(), f.features.loudness, f.features.pitchHz, visemeName(f.viseme.dominant())); }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            lc.stop();
            std::printf("captured %d analysis frames, %d non-silent\n", frames, speaking);
            return frames > 0 ? 0 : 1;
        }
        else if (a == "--fps") pipe.lipSync.frameRate = float(std::atof(next().c_str()));
        else if (a == "--intensity") pipe.lipSync.intensity = float(std::atof(next().c_str()));
        else if (a == "--emotion") { pipe.lipSync.emotion = next(); pipe.lipSync.emotionAmount = 0.8f; if (i + 1 < argc && std::isdigit((unsigned char)argv[i + 1][0])) pipe.lipSync.emotionAmount = float(std::atof(next().c_str())); }
        else if (a == "--head-motion") pipe.lipSync.headMotion = float(std::atof(next().c_str()));
        else if (a == "--gaze-motion") pipe.lipSync.gazeMotion = float(std::atof(next().c_str()));
        else if (a == "--clip-in") clipIn = next();
        else if (a == "--transcript") pipe.transcript = next();
        else if (a == "--save-audio") saveAudio = next();
        else if (a == "--project") project = next();
        else if (a == "--save-project") saveProjectPath = next();
        else if (a == "--no-audio-sidecar") pipe.exportAudioSidecar = false;
        else if (a == "--embed-audio") pipe.embedAudioInGlb = true;
        else if (a == "--livelink") { liveLink = next(); if (i + 1 < argc && argv[i + 1][0] != '-') liveSpeed = std::stof(next()); }
        else if (a == "--save-model") saveModel = next();
        else if (a == "--dump-features") dump = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }
    std::string err;
    if (!project.empty()) {
        if (!loadProject(project, pipe, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        if (pipe.clip.duration <= 0 && !pipe.audio.samples.empty() && !pipe.generateAnimation()) { std::fprintf(stderr, "error: animation generation failed\n"); return 1; }
    } else {
    if (!pipe.loadModel(model, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    pipe.buildDefaultRig();
    }
    if (!saveModel.empty()) saveObj(saveModel, pipe.rig.mesh, &err);
    if (!project.empty()) {
    } else if (!clipIn.empty()) {
        std::vector<AnimationClip> clips;
        if (!loadClipsJson(clipIn, clips, &err) || clips.empty()) { std::fprintf(stderr, "error: %s\n", err.empty() ? "no clips in file" : err.c_str()); return 1; }
        pipe.clip = clips[0]; std::printf("loaded clip '%s' (%.2f s, %d frames) from %s\n", pipe.clip.name.c_str(), pipe.clip.duration, pipe.clip.frameCount(), clipIn.c_str());
    } else {
        if (!pipe.loadAudio(audioPath, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        if (!saveAudio.empty()) saveWav(saveAudio, pipe.audio, &err);
        if (!pipe.generateAnimation()) { std::fprintf(stderr, "error: animation generation failed\n"); return 1; }
    }
    if (dump) {
        std::printf("time,rms,loudness,pitch,voicing,centroid,onset,mfcc0,mfcc1,mfcc2\n");
        for (auto& f : pipe.features.frames)
            std::printf("%.3f,%.4f,%.3f,%.1f,%.2f,%.0f,%d,%.2f,%.2f,%.2f\n", f.time, f.rms, f.loudness, f.pitchHz, f.voicing, f.spectralCentroid, int(f.onset), f.mfcc[0], f.mfcc[1], f.mfcc[2]);
    }
    if (!saveProjectPath.empty()) { if (saveProject(saveProjectPath, pipe, ProjectSaveOptions{}, &err)) std::printf("[fr] wrote project %s\n", saveProjectPath.c_str()); else std::fprintf(stderr, "error: %s\n", err.c_str()); }
    if (!liveLink.empty()) {
        auto colon = liveLink.find(':');
        LiveLinkSender::Settings s; s.host = liveLink.substr(0, colon); if (colon != std::string::npos) s.port = uint16_t(std::stoi(liveLink.substr(colon + 1)));
        LiveLinkSender sender; if (!sender.open(s, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        ArkitMapping map; map.build(pipe.rig, pipe.lipSync.jawBoneDegrees > 0 ? pipe.lipSync.jawBoneDegrees : 25.0f);
        std::printf("[fr] Live Link -> %s:%u, %d/52 shapes mapped, %.2fs clip at %.2fx\n", s.host.c_str(), unsigned(s.port), map.mappedCount(), pipe.clip.duration, liveSpeed);
        const float dtF = 1.0f / s.frameRate; uint32_t n = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (float t = 0; t <= pipe.clip.duration; t += dtF, ++n) {
            sender.send(arkitFrameFromClip(pipe.rig, map, pipe.clip, t), n);
            auto due = t0 + std::chrono::microseconds(int64_t(t / liveSpeed * 1e6f)); std::this_thread::sleep_until(due);
        }
        std::printf("[fr] sent %llu frames\n", (unsigned long long)sender.framesSent());
        return 0;
    }
    std::vector<Variation> vars;
    for (auto& t : variationTexts) vars.push_back(parseVariation(t));
    // ensure output directory exists (portable enough for our purposes)
    auto slash = output.find_last_of("/\\");
    if (slash != std::string::npos) { std::string dir = output.substr(0, slash); std::string cmd = "mkdir -p \"" + dir + "\""; (void)std::system(cmd.c_str()); }
    auto files = pipe.exportAll(output, format, vars, &err);
    for (auto& l : pipe.log) std::printf("[fr] %s\n", l.c_str());
    if (files.empty()) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    std::printf("[fr] wrote %zu file(s)\n", files.size());
    for (auto& f : files) std::printf("  %s\n", f.c_str());
    return 0;
}

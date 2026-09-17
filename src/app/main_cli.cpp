// Headless command-line entry point. This is the surface the Arena agent drives:
//   fr_cli --model character.obj --audio dialogue.wav --output scene --format fbx
//          --variation "Increase smile" --variation "Raise eyebrows"
#include "app/pipeline.h"
#include "core/obj_io.h"
#include "audio/live_capture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace fr;

static void usage() {
    std::puts(R"(fr_cli - FacialRigging headless pipeline
Options:
  --model <file.obj>        3D face model (default: procedural head)
  --audio <file.wav>        speech clip (default: synthetic test speech)
  --output <pattern>        output base name (default: out/scene)
  --format <fbx|glb|gltf>   export format (default: glb; fbx falls back to glb without the SDK)
  --variation <text>        add a variation export (repeatable), e.g. "Increase smile", "intensity=1.3"
  --variations <n>          shorthand: n default variations (smile, brows, subtle, exaggerated)
  --mapper <rules|ml>       viseme mapper (default rules; ml needs a LibTorch build)
  --model-pt <file.pt>      TorchScript model for --mapper ml (default: built-in MLP)
  --up <auto|y|z>           up axis of the OBJ (default auto-detect; scans are often Z-up)
  --list-devices            list PortAudio input devices and exit
  --fps <n>                 bake frame rate (default 30)
  --intensity <f>           global mouth intensity (default 1.0)
  --save-audio <file.wav>   write the (synthetic) audio next to the export
  --save-model <file.obj>   write the (procedural) bind mesh as OBJ
  --dump-features           print per-frame features to stdout
  -h, --help
)");
}

int main(int argc, char** argv) {
    std::string model, audioPath, output = "out/scene", format = "glb", saveAudio, saveModel;
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
            if (devs.empty()) std::printf("no input devices: %s\n", err.c_str());
            for (auto& d : devs) std::printf("[%d] %s (%d ch, %.0f Hz)\n", d.index, d.name.c_str(), d.maxInputChannels, d.defaultSampleRate);
            return 0;
        }
        else if (a == "--fps") pipe.lipSync.frameRate = float(std::atof(next().c_str()));
        else if (a == "--intensity") pipe.lipSync.intensity = float(std::atof(next().c_str()));
        else if (a == "--save-audio") saveAudio = next();
        else if (a == "--save-model") saveModel = next();
        else if (a == "--dump-features") dump = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }
    std::string err;
    if (!pipe.loadModel(model, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    pipe.buildDefaultRig();
    if (!saveModel.empty()) saveObj(saveModel, pipe.rig.mesh, &err);
    if (!pipe.loadAudio(audioPath, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (!saveAudio.empty()) saveWav(saveAudio, pipe.audio, &err);
    if (!pipe.generateAnimation()) { std::fprintf(stderr, "error: animation generation failed\n"); return 1; }
    if (dump) {
        std::printf("time,rms,loudness,pitch,voicing,centroid,onset,mfcc0,mfcc1,mfcc2\n");
        for (auto& f : pipe.features.frames)
            std::printf("%.3f,%.4f,%.3f,%.1f,%.2f,%.0f,%d,%.2f,%.2f,%.2f\n", f.time, f.rms, f.loudness, f.pitchHz, f.voicing, f.spectralCentroid, int(f.onset), f.mfcc[0], f.mfcc[1], f.mfcc[2]);
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

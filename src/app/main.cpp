#include "app/application.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    fr::Application::Options o;
    o.shaderDir = FR_SHADER_DIR; o.assetDir = FR_ASSET_DIR;
#ifdef FR_ICON_FONT
    o.iconFontPath = FR_ICON_FONT;
#endif
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--model") o.modelPath = next();
        else if (a == "--project") o.projectPath = next();
        else if (a == "--audio") o.audioPath = next();
        else if (a == "--generate") o.autoGenerate = true;
        else if (a == "--export") o.exportOnStart = next();
        else if (a == "--variation") o.variations.push_back(next());
        else if (a == "--shaders") o.shaderDir = next();
        else if (a == "--size") { std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height); }
        else if (a == "--headless") o.headless = true;
        else if (a == "--gles") o.useGLES = true;
        else if (a == "--render-frames") o.renderFrames = std::atoi(next().c_str());
        else if (a == "--frame-pattern") o.framePattern = next();
        else if (a == "--mapper") o.mapper = next();
        else if (a == "--model-pt") o.modelPt = next();
        else if (a == "--up") o.upAxis = next();
        else if (a == "--live") o.live = true;
        else if (a == "--live-device") o.liveDevice = std::stoi(next());
        else if (a == "--live-noise") o.liveTestNoiseDb = std::stof(next());
        else if (a == "--ui-scale") o.uiScale = std::stof(next());
        else if (a == "--step") o.startStep = std::stoi(next());
        else if (a == "--rig-tab") o.rigTab = std::stoi(next());
        else if (a == "--paint-bone") o.paintBone = std::stoi(next());
        else if (a == "--paint-demo") o.paintDemo = true;
        else if (a == "--zoom") o.zoom = std::stof(next());
        else if (a == "--video") o.videoOut = next();
        else if (a == "--reference") o.referencePath = next();
        else if (a == "--orbit") o.videoOrbit = std::stof(next());
        else if (a == "--video-size") { std::string v = next(); auto x = v.find('x'); if (x != std::string::npos) { o.videoW = std::stoi(v.substr(0, x)); o.videoH = std::stoi(v.substr(x + 1)); } }
        else if (a == "--video-seconds") o.videoSeconds = std::stof(next());
        else if (a == "--look-at") { o.lookAt.x = std::stof(next()); o.lookAt.y = std::stof(next()); o.lookAt.z = std::stof(next()); o.haveLookAt = true; }
        else if (a == "--clean") o.clean = true;
        else if (a == "--keys-demo") { o.keysDemo = true; if (i + 1 < argc && argv[i + 1][0] != '-') o.keysDemoChannel = std::stoi(next()); }
        else if (a == "--emotion") o.emotion = next();
        else if (a == "--landmarks") o.landmarks = next();
        else if (a == "--show-landmarks") o.showLandmarks = true;
        else if (a == "--transcript") o.transcript = next();
        else if (a == "--emotion-amount") o.emotionAmount = std::stof(next());
        else if (a == "--head-motion") o.headMotion = std::stof(next());
        else if (a == "--gaze-motion") o.gazeMotion = std::stof(next());
        else if (a == "--shade") o.shadeMode = std::stoi(next());
        else if (a == "--msaa") o.msaa = std::stoi(next());
        else if (a == "--gaze") { o.gazeYaw = std::stof(next()); o.gazePitch = std::stof(next()); }
        else if (a == "-h" || a == "--help") {
            std::puts("facial_rigging [--project s.frproj] [--model face.obj] [--audio speech.wav] [--video out.mp4 --orbit 360 --video-size 1280x720] [--generate] [--export out/scene.fbx]\n"
                      "               [--variation \"Increase smile\"]... [--size WxH] [--mapper rules|ml] [--model-pt model.pt] [--up auto|y|z] [--live] [--ui-scale F] [--step 0-4]\n"
                      "               [--emotion happy|sad|angry|surprised|disgusted] [--emotion-amount F] [--transcript \"text\"] [--head-motion F] [--gaze-motion F] [--shade 0-4] [--gaze YAW PITCH] [--msaa 0|2|4|8]\n"
                      "               [--headless] [--gles] [--render-frames N] [--frame-pattern out/frame_%03d.ppm]\n"
                      "  --headless: no window; GLFW null platform + EGL pbuffer (needs libEGL/libGLESv2 on the library path,\n"
                      "              e.g. SwiftShader or Mesa). Combine with --render-frames to dump rendered frames.");
            return 0;
        } else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    fr::Application app(std::move(o));
    return app.run();
}

#include "app/application.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    fr::Application::Options o;
    o.shaderDir = FR_SHADER_DIR; o.assetDir = FR_ASSET_DIR;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--model") o.modelPath = next();
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
        else if (a == "-h" || a == "--help") {
            std::puts("facial_rigging [--model face.obj] [--audio speech.wav] [--generate] [--export out/scene.fbx]\n"
                      "               [--variation \"Increase smile\"]... [--size WxH] [--mapper rules|ml] [--model-pt model.pt] [--up auto|y|z] [--live]\n"
                      "               [--headless] [--gles] [--render-frames N] [--frame-pattern out/frame_%03d.ppm]\n"
                      "  --headless: no window; GLFW null platform + EGL pbuffer (needs libEGL/libGLESv2 on the library path,\n"
                      "              e.g. SwiftShader or Mesa). Combine with --render-frames to dump rendered frames.");
            return 0;
        } else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    fr::Application app(std::move(o));
    return app.run();
}

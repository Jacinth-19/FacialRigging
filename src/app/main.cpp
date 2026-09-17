#include "app/application.h"
#include <cstdio>
#include <cstring>
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
        else if (a == "-h" || a == "--help") {
            std::puts("facial_rigging [--model face.obj] [--audio speech.wav] [--generate] [--export out/scene.glb] [--variation \"Increase smile\"]... [--size WxH]");
            return 0;
        } else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    fr::Application app(std::move(o));
    return app.run();
}

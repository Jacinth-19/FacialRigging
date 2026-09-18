#include "export/exporter.h"
#include "export/arkit_livelink.h"
#include "audio/wav_io.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fr {

std::string writeAudioSidecar(const std::string& path, const ExportOptions& opts, bool writeManifest, std::string* error) {
    if (!opts.audio || opts.audio->samples.empty()) return "";
    std::filesystem::path p(path);
    std::filesystem::path wav = p; wav.replace_extension(".wav");
    std::string err;
    if (!saveWav(wav.string(), *opts.audio, &err)) { if (error) *error = err; return ""; }
    if (writeManifest) {
        std::filesystem::path man = p; man.replace_extension(".audio.json");
        std::ofstream f(man.string());
        f << "{\"format\":\"fraudio\",\"version\":1,\"animation\":\"" << p.filename().string() << "\",\"audio\":{\"uri\":\"" << wav.filename().string()
          << "\",\"offset\":" << opts.audioOffset << ",\"sampleRate\":" << opts.audio->sampleRate << ",\"channels\":" << opts.audio->channels << ",\"duration\":" << opts.audio->duration() << "}}\n";
    }
    return wav.filename().string();
}

bool ArkitCsvExporter::exportScene(const Rig& rig, const std::vector<AnimationClip>& clips, const std::string& path, const ExportOptions& opts, std::string* error) {
    if (clips.empty()) { if (error) *error = "no clip to export"; return false; }
    ArkitCsvOptions o;
    if (!writeArkitCsv(path, rig, clips[0], o, error)) return false;
    writeAudioSidecar(path, opts, true, nullptr);
    return true;
}

std::unique_ptr<Exporter> makeExporterForPath(const std::string& path, std::string* note) {
    std::string ext;
    auto dot = path.find_last_of('.');
    if (dot != std::string::npos) { ext = path.substr(dot); for (auto& c : ext) c = char(std::tolower((unsigned char)c)); }
    if (ext == ".fbx") {
        auto fbx = std::make_unique<FbxExporter>();
        if (fbx->available()) return fbx;
        auto assimp = std::make_unique<AssimpFbxExporter>();
        if (assimp->available()) return assimp;
        if (note) *note = "no FBX writer compiled in; falling back to glTF (.glb)";
        return std::make_unique<GltfExporter>();
    }
    if (ext == ".json") return std::make_unique<JsonClipExporter>();
    if (ext == ".csv") return std::make_unique<ArkitCsvExporter>();
    return std::make_unique<GltfExporter>();
}

} // namespace fr

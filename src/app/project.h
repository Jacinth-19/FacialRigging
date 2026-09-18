#pragma once
// Project files: one JSON that restores a whole session - model + audio paths (relative to the
// project file when possible), transcript, model orientation fixes, every rig edit (control
// points, painted skin weights, user-baked blendshapes, correctives, bone poses), lip-sync /
// mapper settings and the baked clip. Heavy things that can be regenerated from the referenced
// files (mesh, authored .fbs shapes, procedural shapes) are NOT stored; only the deltas are.
#include <string>

namespace fr {

class Pipeline;

constexpr const char* kProjectExtension = ".frproj";

struct ProjectSaveOptions {
    bool includeClip = true;          ///< baked animation curves (can be regenerated from audio + settings)
    bool includeSkinWeights = true;   ///< only written when they differ from the default rig's weights
    bool relativePaths = true;        ///< store model/audio relative to the project file
};

bool saveProject(const std::string& path, const Pipeline& pipe, const ProjectSaveOptions& opts, std::string* error = nullptr);
/// Loads model + audio (re-running the same import steps), rebuilds the rig, then re-applies the
/// stored edits. `log` receives a summary; returns false only when the model can't be loaded.
bool loadProject(const std::string& path, Pipeline& pipe, std::string* error = nullptr);

} // namespace fr

#pragma once
// Turntable / playback video export: renders the clip offscreen at any resolution (own FBO, optional
// MSAA resolve), optionally orbits the camera, and pipes raw RGB frames into an `ffmpeg` child
// process that encodes H.264 MP4 (or whatever the extension asks for) and muxes the clip's audio.
// Falls back to a numbered PPM/PNG-less image sequence when ffmpeg is not on PATH.
#include "core/camera.h"
#include <functional>
#include <string>

namespace fr {

struct VideoSettings {
    int width = 1280, height = 720;
    float fps = 30.0f;
    int msaa = 4;
    float startSec = 0.0f, endSec = -1.0f;      ///< -1 = clip duration (or 4 s when there is no clip)
    float orbitDegrees = 0.0f;                  ///< camera yaw sweep over the video (360 = full turntable)
    float orbitPitchDegrees = 0.0f;             ///< optional pitch sweep (sinusoidal)
    bool loopClip = true;                       ///< when the orbit outlasts the clip
    bool includeAudio = true;
    std::string audioPath;                      ///< WAV muxed into the file (written next to it when empty and audio exists)
    int crf = 18;                               ///< x264 quality
    std::string ffmpeg = "ffmpeg";              ///< binary name / path
};

struct VideoProgress { int frame = 0, total = 0; bool cancelled = false; };

/// Renders and encodes; `renderFrame(t, view, proj, camPos)` must draw the scene into the currently
/// bound framebuffer at the given viewport. Returns false with `error` on failure.
/// `onProgress` may return false to cancel. When `imageSequencePattern` is non-empty (e.g. out/f_%04d.ppm)
/// frames go to files instead of ffmpeg.
bool exportVideo(const std::string& outPath, const VideoSettings& s, OrbitCamera camera, float clipDuration,
                 const std::function<void(float t, const OrbitCamera& cam, int w, int h)>& renderFrame,
                 const std::function<bool(const VideoProgress&)>& onProgress, std::string* error,
                 const std::string& imageSequencePattern = "");

bool ffmpegAvailable(const std::string& ffmpeg = "ffmpeg", std::string* version = nullptr);

} // namespace fr

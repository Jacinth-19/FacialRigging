#include "render/video_export.h"
#include "render/gl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <glm/gtc/constants.hpp>

namespace fr {

bool ffmpegAvailable(const std::string& ffmpeg, std::string* version) {
    std::string cmd = ffmpeg + " -version 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r"); if (!p) return false;
    char buf[256]; std::string first; if (std::fgets(buf, sizeof buf, p)) first = buf;
    while (std::fgets(buf, sizeof buf, p)) {}
    int rc = pclose(p);
    if (version) *version = first.substr(0, first.find('\n'));
    return rc == 0 && !first.empty();
}

namespace {
struct Target {
    GLuint fbo = 0, color = 0, depth = 0, resolveFbo = 0, resolveColor = 0; int w = 0, h = 0, samples = 0;
    bool create(int W, int H, int msaa) {
        w = W; h = H; GLint maxS = 0; glGetIntegerv(GL_MAX_SAMPLES, &maxS); samples = std::clamp(msaa, 0, int(maxS)); if (samples < 2) samples = 0;
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenRenderbuffers(1, &color); glBindRenderbuffer(GL_RENDERBUFFER, color);
        if (samples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h); else glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
        glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth);
        if (samples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h); else glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (ok && samples) {
            glGenFramebuffers(1, &resolveFbo); glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
            glGenRenderbuffers(1, &resolveColor); glBindRenderbuffer(GL_RENDERBUFFER, resolveColor); glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolveColor);
            ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
        glBindRenderbuffer(GL_RENDERBUFFER, 0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return ok;
    }
    void readback(std::vector<unsigned char>& rgb) {
        GLuint readFbo = fbo;
        if (samples) { glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo); glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST); readFbo = resolveFbo; }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        std::vector<unsigned char> rgba(size_t(w) * size_t(h) * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        rgb.resize(size_t(w) * size_t(h) * 3);
        for (int y = 0; y < h; ++y) { const unsigned char* src = &rgba[size_t(h - 1 - y) * size_t(w) * 4]; unsigned char* dst = &rgb[size_t(y) * size_t(w) * 3]; for (int x = 0; x < w; ++x) { dst[x * 3] = src[x * 4]; dst[x * 3 + 1] = src[x * 4 + 1]; dst[x * 3 + 2] = src[x * 4 + 2]; } }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    ~Target() { if (depth) glDeleteRenderbuffers(1, &depth); if (color) glDeleteRenderbuffers(1, &color); if (resolveColor) glDeleteRenderbuffers(1, &resolveColor); if (fbo) glDeleteFramebuffers(1, &fbo); if (resolveFbo) glDeleteFramebuffers(1, &resolveFbo); }
};
std::string shellQuote(const std::string& s) { std::string o = "'"; for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; } return o + "'"; }
} // namespace

bool exportVideo(const std::string& outPath, const VideoSettings& s, OrbitCamera camera, float clipDuration,
                 const std::function<void(float, const OrbitCamera&, int, int)>& renderFrame,
                 const std::function<bool(const VideoProgress&)>& onProgress, std::string* error,
                 const std::string& imageSequencePattern) {
    const int w = std::max(16, s.width & ~1), h = std::max(16, s.height & ~1);   // x264 needs even sizes
    const float end = s.endSec > 0 ? s.endSec : (clipDuration > 0 ? clipDuration : 4.0f);
    const float span = std::max(0.05f, end - s.startSec);
    const int total = std::max(1, int(std::round(span * s.fps)));
    Target target;
    if (!target.create(w, h, s.msaa)) { if (error) *error = "offscreen framebuffer incomplete"; return false; }
    FILE* pipe = nullptr;
    const bool sequence = !imageSequencePattern.empty();
    if (!sequence) {
        std::string cmd = s.ffmpeg + " -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " + std::to_string(w) + "x" + std::to_string(h) + " -framerate " + std::to_string(s.fps) + " -i -";
        const bool audio = s.includeAudio && !s.audioPath.empty();
        if (audio) cmd += " -ss " + std::to_string(s.startSec) + " -i " + shellQuote(s.audioPath) + (s.loopClip && clipDuration > 0 && span > clipDuration ? " -stream_loop -1" : "");
        std::string ext = outPath.size() > 4 ? outPath.substr(outPath.size() - 4) : "";
        if (ext == "webm") cmd += " -c:v libvpx-vp9 -b:v 0 -crf " + std::to_string(std::clamp(s.crf + 12, 10, 50)) + " -pix_fmt yuv420p";
        else if (ext == ".gif") cmd += " -vf \"fps=" + std::to_string(int(s.fps)) + ",split[a][b];[a]palettegen[p];[b][p]paletteuse\"";
        else cmd += " -c:v libx264 -preset medium -crf " + std::to_string(s.crf) + " -pix_fmt yuv420p -movflags +faststart";
        if (audio && ext != ".gif") cmd += " -c:a aac -b:a 192k -shortest";
        cmd += " " + shellQuote(outPath);
        pipe = popen(cmd.c_str(), "w");
        if (!pipe) { if (error) *error = "cannot start ffmpeg (" + cmd + ")"; return false; }
    }
    std::vector<unsigned char> rgb;
    const float yaw0 = camera.yaw, pitch0 = camera.pitch;
    VideoProgress prog; prog.total = total;
    bool ok = true;
    for (int i = 0; i < total && ok; ++i) {
        const float u = total > 1 ? float(i) / float(total - 1) : 0.0f;
        float t = s.startSec + u * span;
        if (clipDuration > 0 && t > clipDuration) t = s.loopClip ? std::fmod(t, clipDuration) : clipDuration;
        OrbitCamera cam = camera;
        cam.yaw = yaw0 + glm::radians(s.orbitDegrees) * u;
        cam.pitch = pitch0 + glm::radians(s.orbitPitchDegrees) * std::sin(u * glm::two_pi<float>());
        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo); glViewport(0, 0, w, h);
        renderFrame(t, cam, w, h);
        glFinish();
        target.readback(rgb);
        if (sequence) {
            char name[1024]; std::snprintf(name, sizeof name, imageSequencePattern.c_str(), i);
            FILE* f = std::fopen(name, "wb"); if (!f) { ok = false; if (error) *error = std::string("cannot write ") + name; break; }
            std::fprintf(f, "P6\n%d %d\n255\n", w, h); std::fwrite(rgb.data(), 1, rgb.size(), f); std::fclose(f);
        } else if (std::fwrite(rgb.data(), 1, rgb.size(), pipe) != rgb.size()) { ok = false; if (error) *error = "ffmpeg pipe closed early"; break; }
        prog.frame = i + 1;
        if (onProgress && !onProgress(prog)) { prog.cancelled = true; break; }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (pipe) { int rc = pclose(pipe); if (rc != 0 && ok && !prog.cancelled) { ok = false; if (error) *error = "ffmpeg exited with status " + std::to_string(rc); } }
    if (prog.cancelled) { if (error) *error = "cancelled"; return false; }
    return ok;
}

} // namespace fr

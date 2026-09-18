#include "render/reference.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

namespace fr {

ReferenceMedia::~ReferenceMedia() { clear(); }

void ReferenceMedia::clear() {
    if (tex_) glDeleteTextures(1, &tex_);
    tex_ = 0; w_ = h_ = frames_ = 0; shown_ = -1; fps_ = 0; data_.clear(); data_.shrink_to_fit(); path_.clear();
}

namespace { std::string lower(std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; } }

bool ReferenceMedia::load(const std::string& path, std::string* error, const std::string& ffmpeg) {
    clear();
    std::string ext = lower(path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.') + 1));
    static const char* imgExt[] = {"png", "jpg", "jpeg", "bmp", "tga", "gif", "psd"};
    bool ok = std::find(std::begin(imgExt), std::end(imgExt), ext) != std::end(imgExt) ? loadImage(path, error) : loadVideo(path, error, ffmpeg);
    if (ok) path_ = path;
    return ok;
}

void ReferenceMedia::upload(const uint8_t* rgb) {
    if (!tex_) {
        glGenTextures(1, &tex_); glBindTexture(GL_TEXTURE_2D, tex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w_, h_, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool ReferenceMedia::loadImage(const std::string& path, std::string* error) {
    int w = 0, h = 0, n = 0; stbi_uc* px = stbi_load(path.c_str(), &w, &h, &n, 3);
    if (!px) { if (error) *error = std::string("stb_image: ") + stbi_failure_reason(); return false; }
    w_ = w; h_ = h; frames_ = 1; fps_ = 0; upload(px); stbi_image_free(px); shown_ = 0;
    return true;
}

bool ReferenceMedia::loadVideo(const std::string& path, std::string* error, const std::string& ffmpeg) {
    // 1. probe: ffmpeg prints "Stream #0:0: Video: h264 ..., 1920x1080 [SAR..], 30 fps" and "Duration: 00:00:05.00"
    std::string cmd = ffmpeg + " -hide_banner -i \"" + path + "\" 2>&1";
    FILE* p = popen(cmd.c_str(), "r"); if (!p) { if (error) *error = "cannot run ffmpeg"; return false; }
    char line[1024]; int sw = 0, sh = 0; float fps = 0, dur = 0;
    while (std::fgets(line, sizeof line, p)) {
        if (const char* d = std::strstr(line, "Duration: ")) { int hh, mm; float ss; if (std::sscanf(d + 10, "%d:%d:%f", &hh, &mm, &ss) == 3) dur = hh * 3600.0f + mm * 60.0f + ss; }
        if (std::strstr(line, "Video:")) {
            // find "WxH" token and "fps"
            for (const char* c = line; *c; ++c) { int a, b; if ((c == line || !std::isdigit(static_cast<unsigned char>(c[-1]))) && std::sscanf(c, "%dx%d", &a, &b) == 2 && a > 15 && b > 15 && !sw) { sw = a; sh = b; } }
            if (const char* f = std::strstr(line, " fps")) { const char* s = f; while (s > line && (std::isdigit(static_cast<unsigned char>(s[-1])) || s[-1] == '.')) --s; fps = std::strtof(s, nullptr); }
        }
    }
    pclose(p);
    if (!sw || !sh) { if (error) *error = "ffmpeg could not read a video stream from " + path; return false; }
    if (fps <= 0) fps = 25.0f;
    if (dur <= 0) dur = 10.0f;
    // 2. choose decode size / rate within the memory budget
    float outFps = std::min(fps, 15.0f);
    int outW = std::min(sw, 640); int outH = int(std::lround(float(sh) * outW / sw)); outH -= outH & 1;
    auto bytes = [&](int w, int h) { return size_t(w) * size_t(h) * 3u * size_t(std::ceil(dur * outFps)); };
    while (bytes(outW, outH) > kMaxBytes && outW > 160) { outW = outW * 3 / 4; outH = int(std::lround(float(sh) * outW / sw)); outH -= outH & 1; }
    int maxFrames = int(kMaxBytes / (size_t(outW) * size_t(outH) * 3u));
    char vf[128]; std::snprintf(vf, sizeof vf, "fps=%.3f,scale=%d:%d", outFps, outW, outH);
    cmd = ffmpeg + " -hide_banner -loglevel error -i \"" + path + "\" -an -vf " + vf + " -f rawvideo -pix_fmt rgb24 - 2>/dev/null";
    p = popen(cmd.c_str(), "r"); if (!p) { if (error) *error = "cannot run ffmpeg"; return false; }
    const size_t frameBytes = size_t(outW) * size_t(outH) * 3u;
    data_.reserve(std::min(frameBytes * size_t(std::ceil(dur * outFps) + 2), kMaxBytes));
    std::vector<uint8_t> buf(frameBytes); int n = 0;
    while (n < maxFrames) {
        size_t got = std::fread(buf.data(), 1, frameBytes, p); if (got < frameBytes) break;
        data_.insert(data_.end(), buf.begin(), buf.end()); ++n;
    }
    pclose(p);
    if (n == 0) { if (error) *error = "ffmpeg decoded no frames from " + path; data_.clear(); return false; }
    w_ = outW; h_ = outH; frames_ = n; fps_ = outFps; shown_ = -1; show(0.0f);
    if (n == 1) { frames_ = 1; }
    return true;
}

int ReferenceMedia::show(float seconds, bool loop) {
    if (frames_ <= 1) { if (frames_ == 1 && shown_ != 0) shown_ = 0; return shown_; }
    int idx = int(std::floor(seconds * fps_));
    if (loop) { idx %= frames_; if (idx < 0) idx += frames_; } else idx = std::clamp(idx, 0, frames_ - 1);
    if (idx != shown_) { upload(data_.data() + size_t(idx) * size_t(w_) * size_t(h_) * 3u); shown_ = idx; }
    return idx;
}

} // namespace fr

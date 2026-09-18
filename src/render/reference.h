// Reference media (image or video) shown next to the 3D view for matching poses / lip shapes.
// Images load through stb_image; videos are decoded once through an ffmpeg pipe into a small
// in-memory RGB frame store (down-scaled, capped at kMaxBytes) so scrubbing is instant.
#pragma once
#include "render/gl.h"
#include <cstdint>
#include <string>
#include <vector>

namespace fr {

class ReferenceMedia {
public:
    ~ReferenceMedia();
    bool load(const std::string& path, std::string* error = nullptr, const std::string& ffmpeg = "ffmpeg");
    void clear();
    bool valid() const { return tex_ != 0; }
    bool isVideo() const { return frames_ > 1; }
    int width() const { return w_; } int height() const { return h_; }
    int frameCount() const { return frames_; } float fps() const { return fps_; }
    float duration() const { return frames_ > 1 ? float(frames_) / fps_ : 0.0f; }
    const std::string& path() const { return path_; }
    /// Upload the frame for `seconds` (video) - no-op for images. Returns the frame index shown.
    int show(float seconds, bool loop = true);
    GLuint texture() const { return tex_; }
    static constexpr size_t kMaxBytes = 192u << 20;   ///< in-memory budget for decoded video

private:
    bool loadImage(const std::string& path, std::string* error);
    bool loadVideo(const std::string& path, std::string* error, const std::string& ffmpeg);
    void upload(const uint8_t* rgb);
    GLuint tex_ = 0; int w_ = 0, h_ = 0, frames_ = 0, shown_ = -1; float fps_ = 0.0f;
    std::vector<uint8_t> data_; std::string path_;
};

} // namespace fr

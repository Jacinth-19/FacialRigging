#include "rig/landmark_cascade.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifndef FR_ASSET_DIR
#define FR_ASSET_DIR "assets"
#endif

namespace fr {

GeoImage GeoImage::rasterize(const Mesh& mesh, const std::vector<glm::vec3>& P, int res, glm::vec2 tl, float worldWidth, int onlyPart) {
    GeoImage r; r.res = res; r.originTopLeft = tl; r.unitsPerPixel = worldWidth / float(res);
    r.depth.assign(size_t(res) * res, -1e30f); r.normal.assign(size_t(res) * res, glm::vec3(0, 0, 1));
    const float inv = 1.0f / r.unitsPerPixel;
    auto toPix = [&](const glm::vec3& p) { return glm::vec2((p.x - tl.x) * inv, (tl.y - p.y) * inv); };
    uint32_t i0 = 0, i1 = uint32_t(mesh.indices.size());
    if (onlyPart >= 0 && onlyPart < int(mesh.parts.size())) { i0 = mesh.parts[size_t(onlyPart)].firstIndex; i1 = i0 + mesh.parts[size_t(onlyPart)].indexCount; }
    for (uint32_t t = i0; t + 2 < i1; t += 3) {
        const glm::vec3 &a = P[mesh.indices[t]], &b = P[mesh.indices[t + 1]], &c = P[mesh.indices[t + 2]];
        glm::vec3 n = glm::cross(b - a, c - a); float len = glm::length(n); if (len < 1e-20f) continue; n /= len;
        if (n.z <= 0.0f) continue;
        glm::vec2 pa = toPix(a), pb = toPix(b), pc = toPix(c);
        int x0 = std::max(0, int(std::floor(std::min({pa.x, pb.x, pc.x})))), x1 = std::min(res - 1, int(std::ceil(std::max({pa.x, pb.x, pc.x}))));
        int y0 = std::max(0, int(std::floor(std::min({pa.y, pb.y, pc.y})))), y1 = std::min(res - 1, int(std::ceil(std::max({pa.y, pb.y, pc.y}))));
        if (x0 > x1 || y0 > y1) continue;
        float area = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x); if (std::fabs(area) < 1e-12f) continue;
        float ia = 1.0f / area;
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            glm::vec2 p(x + 0.5f, y + 0.5f);
            float w0 = ((pb.x - p.x) * (pc.y - p.y) - (pb.y - p.y) * (pc.x - p.x)) * ia;
            float w1 = ((pc.x - p.x) * (pa.y - p.y) - (pc.y - p.y) * (pa.x - p.x)) * ia;
            float w2 = 1.0f - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
            float z = w0 * a.z + w1 * b.z + w2 * c.z; size_t i = size_t(y) * res + x;
            if (z > r.depth[i]) { r.depth[i] = z; r.normal[i] = n; }
        }
    }
    return r;
}

namespace {
inline void sampleGeo(const GeoImage& img, float px, float py, float refZ, float unit, float* out, int channels) {
    px -= 0.5f; py -= 0.5f;
    int x0 = int(std::floor(px)), y0 = int(std::floor(py)); float fx = px - x0, fy = py - y0;
    float acc[4] = {0, 0, 0, 0}, wsum = 0.0f;
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        int x = x0 + dx, y = y0 + dy; float w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy); if (w <= 0) continue;
        float d = -1.0f, nx = 0, ny = 0, nz = 0;
        if (x >= 0 && y >= 0 && x < img.res && y < img.res && !img.empty(x, y)) { size_t i = size_t(y) * img.res + x; d = std::clamp((img.depth[i] - refZ) / unit, -1.0f, 0.5f); nx = img.normal[i].x; ny = img.normal[i].y; nz = img.normal[i].z; }
        acc[0] += w * d; acc[1] += w * nx; acc[2] += w * ny; acc[3] += w * nz; wsum += w;
    }
    float iw = wsum > 0 ? 1.0f / wsum : 0.0f;
    for (int c = 0; c < channels; ++c) out[c] = acc[c] * iw;
}
} // namespace

void LandmarkCascade::features(const GeoImage& img, const GeoFaceBox& box, const std::vector<glm::vec2>& s, std::vector<float>& out) const {
    out.resize(size_t(featureCount()) + 1);
    const float pixPerUnit = float(img.res) / (1.0f + 2.0f * margin), half = 0.5f * float(patch - 1);
    float* o = out.data();
    for (int l = 0; l < kLm68; ++l) for (int py = 0; py < patch; ++py) for (int px = 0; px < patch; ++px) {
        float u = s[size_t(l)].x + (px - half) * spacing, v = s[size_t(l)].y + (py - half) * spacing;
        sampleGeo(img, (u + margin) * pixPerUnit, (v + margin) * pixPerUnit, box.noseTip.z, box.side, o, channels);
        o += channels;
    }
    *o = 1.0f;
}

std::vector<glm::vec2> LandmarkCascade::run(const GeoImage& img, const GeoFaceBox& box, std::vector<glm::vec2> s, std::vector<std::vector<glm::vec2>>* perStage) const {
    std::vector<float> f; const int F = featureCount() + 1, O = 2 * kLm68;
    for (const auto& R : stages) {
        features(img, box, s, f);
        std::vector<float> d(size_t(O), 0.0f);
        for (int i = 0; i < F; ++i) { float fi = f[size_t(i)]; if (fi == 0.0f) continue; const float* row = &R[size_t(i) * O]; for (int j = 0; j < O; ++j) d[size_t(j)] += fi * row[j]; }
        for (int l = 0; l < kLm68; ++l) { s[size_t(l)].x += d[size_t(2 * l)]; s[size_t(l)].y += d[size_t(2 * l + 1)]; }
        if (perStage) perStage->push_back(s);
    }
    return s;
}

bool LandmarkCascade::save(const std::string& path, std::string* error) const {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) { if (error) *error = "cannot write " + path; return false; }
    auto w32 = [&](int32_t v) { std::fwrite(&v, 4, 1, f); }; auto wf = [&](float v) { std::fwrite(&v, 4, 1, f); };
    std::fwrite("FRLM", 1, 4, f); w32(1); w32(res); w32(patch); w32(channels); wf(spacing); wf(margin);
    w32(int32_t(info.size())); std::fwrite(info.data(), 1, info.size(), f);
    for (const auto& p : meanShape) { wf(p.x); wf(p.y); }
    w32(int32_t(stages.size()));
    const int F = featureCount() + 1, O = 2 * kLm68; std::vector<int16_t> q(static_cast<size_t>(F), 0);
    for (const auto& R : stages) for (int j = 0; j < O; ++j) {
        float mx = 0; for (int i = 0; i < F; ++i) mx = std::max(mx, std::fabs(R[size_t(i) * O + j]));
        float sc = mx > 0 ? mx / 32767.0f : 1.0f; wf(sc);
        for (int i = 0; i < F; ++i) q[size_t(i)] = int16_t(std::lround(R[size_t(i) * O + j] / sc));
        std::fwrite(q.data(), 2, q.size(), f);
    }
    std::fclose(f); return true;
}

bool LandmarkCascade::load(const std::string& path, std::string* error) {
    FILE* f = std::fopen(path.c_str(), "rb"); if (!f) { if (error) *error = "cannot open " + path; return false; }
    char magic[4]; if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "FRLM", 4) != 0) { std::fclose(f); if (error) *error = "not a FRLM file: " + path; return false; }
    auto r32 = [&]() { int32_t v = -1; if (std::fread(&v, 4, 1, f) != 1) v = -1; return v; }; auto rf = [&]() { float v = 0; if (std::fread(&v, 4, 1, f) != 1) v = 0; return v; };
    if (r32() != 1) { std::fclose(f); if (error) *error = "unsupported FRLM version"; return false; }
    res = r32(); patch = r32(); channels = r32(); spacing = rf(); margin = rf();
    int n = r32(); if (n < 0 || n > 100000 || res <= 0 || patch <= 0 || channels <= 0 || channels > 4) { std::fclose(f); if (error) *error = "corrupt FRLM header"; return false; }
    info.assign(size_t(n), '\0'); if (n && std::fread(&info[0], 1, size_t(n), f) != size_t(n)) { std::fclose(f); return false; }
    meanShape.resize(kLm68); for (auto& p : meanShape) { p.x = rf(); p.y = rf(); }
    int ns = r32(); if (ns < 0 || ns > 32) { std::fclose(f); return false; }
    const int F = featureCount() + 1, O = 2 * kLm68;
    stages.assign(size_t(ns), std::vector<float>(size_t(F) * O)); std::vector<int16_t> q(static_cast<size_t>(F), 0);
    for (auto& R : stages) for (int j = 0; j < O; ++j) {
        float sc = rf(); if (std::fread(q.data(), 2, q.size(), f) != q.size()) { std::fclose(f); if (error) *error = "truncated FRLM"; return false; }
        for (int i = 0; i < F; ++i) R[size_t(i) * O + j] = q[size_t(i)] * sc;
    }
    std::fclose(f); return valid();
}

GeoFaceBox LandmarkCascade::detectBox(const Mesh& mesh, const std::vector<glm::vec3>& P, int facePart) {
    GeoFaceBox box; if (P.empty()) return box;
    glm::vec3 lo(1e30f), hi(-1e30f);
    if (facePart >= 0) { for (uint32_t v : mesh.partVertices(facePart)) { lo = glm::min(lo, P[v]); hi = glm::max(hi, P[v]); } }
    else for (const auto& p : P) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    const int res = 160; float w = std::max(hi.x - lo.x, hi.y - lo.y) * 1.05f;
    glm::vec2 tl(0.5f * (lo.x + hi.x) - 0.5f * w, 0.5f * (lo.y + hi.y) + 0.5f * w);
    GeoImage img = GeoImage::rasterize(mesh, P, res, tl, w, facePart);
    // nose tip = most forward pixel (skip the top 12%: hair / forehead bumps on scans); ties -> centre column
    int bx = -1, by = -1; float bz = -1e30f;
    for (int y = res / 8; y < res; ++y) for (int x = 0; x < res; ++x) { float z = img.depth[size_t(y) * res + x]; if (z > bz + 1e-7f || (z > -1e29f && std::fabs(z - bz) <= 1e-7f && std::fabs(x - res * 0.5f) < std::fabs(bx - res * 0.5f))) { bz = z; bx = x; by = y; } }
    if (bx < 0) return box;
    // cheek width: widest silhouette row between the nose and 15% of the image below it
    int l = res, r = -1;
    for (int y = by; y < std::min(res, by + res / 6); ++y) for (int x = 0; x < res; ++x) if (!img.empty(x, y)) { l = std::min(l, x); r = std::max(r, x); }
    if (r < l) return box;
    box.cheekWidth = float(r - l + 1) * img.unitsPerPixel;
    box.noseTip = glm::vec3(tl.x + (bx + 0.5f) * img.unitsPerPixel, tl.y - (by + 0.5f) * img.unitsPerPixel, bz);
    box.side = 1.25f * box.cheekWidth;
    box.topLeft = glm::vec2(box.noseTip.x - 0.5f * box.side, box.noseTip.y + 0.48f * box.side);
    box.valid = box.side > 1e-6f;
    return box;
}

GeoImage LandmarkCascade::renderBox(const Mesh& mesh, const std::vector<glm::vec3>& P, const GeoFaceBox& box, int facePart) const {
    float w = box.side * (1.0f + 2.0f * margin);
    return GeoImage::rasterize(mesh, P, res, glm::vec2(box.topLeft.x - margin * box.side, box.topLeft.y + margin * box.side), w, facePart);
}

bool LandmarkCascade::predict(const Mesh& mesh, const std::vector<glm::vec3>& P, GeoFaceBox& box, std::vector<glm::vec2>& shape, float* confidence, int facePart) const {
    if (!valid()) return false;
    box = detectBox(mesh, P, facePart); if (!box.valid) return false;
    GeoImage img = renderBox(mesh, P, box, facePart);
    std::vector<std::vector<glm::vec2>> st;
    shape = run(img, box, meanShape, &st);
    if (confidence) {
        float motion = 0.0f; if (st.size() >= 2) { for (int i = 0; i < kLm68; ++i) motion += glm::length(st.back()[size_t(i)] - st[st.size() - 2][size_t(i)]); motion /= kLm68; }
        static const int pairs[][2] = {{0, 16}, {3, 13}, {17, 26}, {19, 24}, {36, 45}, {39, 42}, {48, 54}, {31, 35}};
        float sym = 0.0f; const float cx = shape[30].x; for (auto& pr : pairs) sym += std::fabs((shape[size_t(pr[0])].x - cx) + (shape[size_t(pr[1])].x - cx)); sym /= 8.0f;
        *confidence = std::clamp(std::exp(-motion / 0.01f) * std::exp(-sym / 0.08f), 0.0f, 1.0f);
    }
    return true;
}

const int* LandmarkCascade::ictLandmarkVertices() {
    static const int ids[kLm68] = {1225, 1888, 1052, 367, 1719, 1722, 2199, 1447, 966, 3661, 4390, 3927, 3924, 2608, 3272, 4088, 3443, 268, 493, 1914, 2044, 1401, 3615, 4240, 4114, 2734, 2509, 978, 4527, 4942, 4857, 1140, 2075, 1147, 4269, 3360, 1507, 1542, 1537, 1528, 1518, 1511, 3742, 3751, 3756, 3721, 3725, 3732, 5708, 5695, 2081, 0, 4275, 6200, 6213, 6346, 6461, 5518, 5957, 5841, 5702, 5711, 5533, 6216, 6207, 6470, 5517, 5966};
    return ids;
}

const LandmarkCascade* LandmarkCascade::builtin(std::string* why) {
    static LandmarkCascade model; static bool tried = false, ok = false; static std::string err; static std::mutex mu;
    std::lock_guard<std::mutex> lock(mu);
    if (!tried) { tried = true; ok = model.load(std::string(FR_ASSET_DIR) + "/models/face_landmarks.frlm", &err); }
    if (!ok && why) *why = err;
    return ok ? &model : nullptr;
}

} // namespace fr

#include "rig/landmarks.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

#if FR_HAVE_DLIB
#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing.h>
#include <dlib/image_transforms.h>
#include <dlib/array2d.h>
#endif

namespace fr {

glm::vec2 FrontRender::pixelOf(const glm::vec3& p) const { return glm::vec2((p.x - origin.x) * scale, (origin.y - p.y) * scale); }

FrontRender renderFront(const Mesh& mesh, int size, int onlyPart) {
    FrontRender r; r.width = r.height = size;
    r.gray.assign(size_t(size) * size, 0); r.world.assign(size_t(size) * size, glm::vec4(0));
    std::vector<float> depth(size_t(size) * size, -std::numeric_limits<float>::infinity());
    if (mesh.positions.empty() || mesh.indices.size() < 3) return r;
    glm::vec3 lo = mesh.boundsMin(), hi = mesh.boundsMax();
    // frame with 8% margin around the x/y extent (square)
    float ext = std::max(hi.x - lo.x, hi.y - lo.y) * 1.16f;
    r.scale = float(size) / ext;
    glm::vec3 c = 0.5f * (lo + hi);
    r.origin = glm::vec3(c.x - 0.5f * ext, c.y + 0.5f * ext, 0);
    const glm::vec3 light = glm::normalize(glm::vec3(0.3f, 0.5f, 1.0f));
    const glm::vec3 fill = glm::normalize(glm::vec3(-0.6f, -0.2f, 0.6f));
    uint32_t i0 = 0, i1 = uint32_t(mesh.indices.size());
    if (onlyPart >= 0 && onlyPart < int(mesh.parts.size())) { i0 = mesh.parts[size_t(onlyPart)].firstIndex; i1 = i0 + mesh.parts[size_t(onlyPart)].indexCount; }
    for (uint32_t t = i0; t + 2 < i1; t += 3) {
        const glm::vec3& a = mesh.positions[mesh.indices[t]]; const glm::vec3& b = mesh.positions[mesh.indices[t + 1]]; const glm::vec3& cc = mesh.positions[mesh.indices[t + 2]];
        glm::vec3 n = glm::cross(b - a, cc - a); float nl = glm::length(n); if (nl < 1e-20f) continue; n /= nl;
        if (n.z <= 0.0f) continue; // back-facing (we look down -z)
        float shade = 0.15f + 0.65f * std::max(0.0f, glm::dot(n, light)) + 0.2f * std::max(0.0f, glm::dot(n, fill));
        unsigned char g = static_cast<unsigned char>(std::clamp(shade, 0.0f, 1.0f) * 255.0f);
        glm::vec2 pa = r.pixelOf(a), pb = r.pixelOf(b), pc = r.pixelOf(cc);
        int xmin = std::max(0, int(std::floor(std::min({pa.x, pb.x, pc.x})))), xmax = std::min(size - 1, int(std::ceil(std::max({pa.x, pb.x, pc.x}))));
        int ymin = std::max(0, int(std::floor(std::min({pa.y, pb.y, pc.y})))), ymax = std::min(size - 1, int(std::ceil(std::max({pa.y, pb.y, pc.y}))));
        float area = (pb.x - pa.x) * (pc.y - pa.y) - (pc.x - pa.x) * (pb.y - pa.y); if (std::fabs(area) < 1e-12f) continue;
        for (int y = ymin; y <= ymax; ++y) for (int x = xmin; x <= xmax; ++x) {
            glm::vec2 p(x + 0.5f, y + 0.5f);
            float w0 = ((pb.x - p.x) * (pc.y - p.y) - (pc.x - p.x) * (pb.y - p.y)) / area;
            float w1 = ((pc.x - p.x) * (pa.y - p.y) - (pa.x - p.x) * (pc.y - p.y)) / area;
            float w2 = 1.0f - w0 - w1;
            const float eps = -1e-4f; if (w0 < eps || w1 < eps || w2 < eps) continue;
            glm::vec3 wp = w0 * a + w1 * b + w2 * cc;
            size_t idx = size_t(y) * size + x;
            if (wp.z > depth[idx]) { depth[idx] = wp.z; r.gray[idx] = g; r.world[idx] = glm::vec4(wp, 1.0f); }
        }
    }
    return r;
}

bool savePgm(const FrontRender& r, const std::string& path, const std::vector<glm::vec2>* marks) {
    std::vector<unsigned char> g = r.gray;
    if (marks) for (const glm::vec2& m : *marks) for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
        int x = int(m.x) + dx, y = int(m.y) + dy; if (x < 0 || y < 0 || x >= r.width || y >= r.height) continue;
        g[size_t(y) * r.width + x] = 255;
    }
    std::ofstream f(path, std::ios::binary); if (!f) return false;
    f << "P5\n" << r.width << " " << r.height << "\n255\n"; f.write(reinterpret_cast<const char*>(g.data()), std::streamsize(g.size()));
    return bool(f);
}

namespace {
std::string defaultModelPath() { return std::string(FR_DATA_DIR) + "/models/shape_predictor_68_face_landmarks.dat"; }
bool fileExists(const std::string& p) { std::ifstream f(p, std::ios::binary); return bool(f); }

/// Surface point under a pixel: nearest hit within a small radius (landmarks on the lip line or
/// eye corners can fall on a background pixel between parts).
bool lift(const FrontRender& r, glm::vec2 px, glm::vec3& out, int radius = 6) {
    int cx = int(std::lround(px.x)), cy = int(std::lround(px.y));
    float best = 1e9f; bool ok = false;
    for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
        int x = cx + dx, y = cy + dy; if (x < 0 || y < 0 || x >= r.width || y >= r.height) continue;
        const glm::vec4& w = r.world[size_t(y) * r.width + x]; if (w.w < 0.5f) continue;
        float d = float(dx * dx + dy * dy); if (d < best) { best = d; out = glm::vec3(w); ok = true; }
    }
    return ok;
}
glm::vec3 mean(const std::vector<glm::vec3>& p, std::initializer_list<int> idx) { glm::vec3 s(0); for (int i : idx) s += p[size_t(i)]; return s / float(idx.size()); }
} // namespace

bool landmarkerAvailable(std::string* why, const std::string& modelPath) {
#if !FR_HAVE_DLIB
    if (why) *why = "built without dlib (FR_WITH_DLIB=OFF)";
    return false;
#else
    std::string mp = modelPath.empty() ? defaultModelPath() : modelPath;
    if (!fileExists(mp)) { if (why) *why = "shape predictor not found at " + mp + " (run tools/bootstrap.sh)"; return false; }
    return true;
#endif
}

FaceLandmarks proportionalLandmarks(const Mesh& mesh) {
    FaceLandmarks lm;
    const glm::vec3 lo = mesh.boundsMin(), hi = mesh.boundsMax(), size = hi - lo, centre = 0.5f * (lo + hi);
    const float H = size.y, W = size.x, frontZ = hi.z;
    lm.mouth = glm::vec3(centre.x, lo.y + 0.28f * H, frontZ);
    lm.browL = glm::vec3(centre.x - 0.20f * W, lo.y + 0.68f * H, frontZ); lm.browR = glm::vec3(centre.x + 0.20f * W, lo.y + 0.68f * H, frontZ);
    lm.eyeL = glm::vec3(centre.x - 0.20f * W, lo.y + 0.60f * H, frontZ); lm.eyeR = glm::vec3(centre.x + 0.20f * W, lo.y + 0.60f * H, frontZ);
    lm.cornerL = glm::vec3(centre.x - 0.16f * W, lm.mouth.y, frontZ); lm.cornerR = glm::vec3(centre.x + 0.16f * W, lm.mouth.y, frontZ);
    lm.chin = lm.mouth - glm::vec3(0, 0.10f * H, 0); lm.noseTip = glm::vec3(centre.x, lo.y + 0.45f * H, frontZ);
    lm.upperLip = lm.mouth + glm::vec3(0, 0.02f * H, 0); lm.lowerLip = lm.mouth - glm::vec3(0, 0.02f * H, 0);
    lm.eyeLOuter = lm.eyeL - glm::vec3(0.06f * W, 0, 0); lm.eyeLInner = lm.eyeL + glm::vec3(0.06f * W, 0, 0);
    lm.eyeROuter = lm.eyeR + glm::vec3(0.06f * W, 0, 0); lm.eyeRInner = lm.eyeR - glm::vec3(0.06f * W, 0, 0);
    lm.note = "proportional (bounding-box) landmarks";
    return lm;
}

void symmetrize(FaceLandmarks& lm, float cx) {
    auto mir = [&](glm::vec3 p) { p.x = 2 * cx - p.x; return p; };
    auto pair = [&](glm::vec3& l, glm::vec3& r) { glm::vec3 a = 0.5f * (l + mir(r)); l = a; r = mir(a); };
    pair(lm.cornerL, lm.cornerR); pair(lm.browL, lm.browR); pair(lm.eyeL, lm.eyeR); pair(lm.eyeLOuter, lm.eyeROuter); pair(lm.eyeLInner, lm.eyeRInner);
    for (glm::vec3* p : {&lm.mouth, &lm.chin, &lm.noseTip, &lm.upperLip, &lm.lowerLip}) p->x = cx;
}

FaceLandmarks detectLandmarks(const Mesh& mesh, const LandmarkOptions& opt) {
    FaceLandmarks lm;
#if !FR_HAVE_DLIB
    (void)opt; lm.note = "built without dlib"; return lm;
#else
    std::string mp = opt.modelPath.empty() ? defaultModelPath() : opt.modelPath;
    if (!fileExists(mp)) { lm.note = "shape predictor missing: " + mp; return lm; }
    try {
        // Render only the skin part when there is one so the detector isn't confused by eyeballs / teeth
        // rendering through gaps; the lift-to-surface then uses the full render.
        FrontRender full = renderFront(mesh, opt.renderSize);
        int facePart = -1; for (const char* n : {"Face", "face", "Head", "head", "skin"}) { facePart = mesh.findPart(n); if (facePart >= 0) break; }
        FrontRender det = facePart >= 0 ? renderFront(mesh, opt.renderSize, facePart) : full;
        lm.renderWidth = det.width; lm.renderHeight = det.height;
        dlib::array2d<unsigned char> img(det.height, det.width);
        for (int y = 0; y < det.height; ++y) for (int x = 0; x < det.width; ++x) img[y][x] = det.gray[size_t(y) * det.width + x];
        static dlib::frontal_face_detector detector = dlib::get_frontal_face_detector();
        static dlib::shape_predictor predictor; static std::string loadedModel;
        if (loadedModel != mp) { dlib::deserialize(mp) >> predictor; loadedModel = mp; }
        if (opt.upsample) dlib::pyramid_up(img);
        const float up = opt.upsample ? 2.0f : 1.0f;
        std::vector<std::pair<double, dlib::rectangle>> dets; std::vector<dlib::rectangle> rects; std::vector<double> scores; std::vector<unsigned long> idx;
        detector(img, dets, -0.5); // slightly permissive threshold: untextured grey renders score low
        if (dets.empty()) { lm.note = "dlib: no face found in the front render"; return lm; }
        auto best = std::max_element(dets.begin(), dets.end(), [](auto& a, auto& b) { return a.first < b.first; });
        dlib::full_object_detection shape = predictor(img, best->second);
        lm.confidence = float(best->first);
        if (shape.num_parts() != 68) { lm.note = "dlib: unexpected part count"; return lm; }
        lm.points68.resize(68); lm.pixels68.resize(68); int off = 0;
        for (unsigned long i = 0; i < 68; ++i) {
            glm::vec2 px(float(shape.part(i).x()) / up, float(shape.part(i).y()) / up); lm.pixels68[i] = px;
            glm::vec3 w; if (lift(full, px, w)) lm.points68[i] = w; else { ++off; lm.points68[i] = glm::vec3(full.origin.x + px.x / full.scale, full.origin.y - px.y / full.scale, mesh.boundsMax().z); }
        }
        const auto& P = lm.points68;
        // iBUG-68 indices: jaw 0-16 (8 = chin), brows 17-21 (viewer-left) / 22-26, nose 27-35 (30 = tip), eyes 36-41 / 42-47,
        // outer lips 48-59 (48 / 54 = corners, 51 top, 57 bottom), inner lips 60-67 (62 top, 66 bottom).
        // The render looks at the face, so image-left is the character's RIGHT side... except our rig names
        // "L" as -x (viewer left when the face looks down +z toward the camera). Keep -x = L consistently.
        auto leftOf = [&](int a, int b) { return P[size_t(a)].x < P[size_t(b)].x ? a : b; };
        int cL = leftOf(48, 54), cR = cL == 48 ? 54 : 48;
        lm.cornerL = P[size_t(cL)]; lm.cornerR = P[size_t(cR)];
        lm.upperLip = P[51]; lm.lowerLip = P[57]; lm.mouth = 0.5f * (P[62] + P[66]);
        lm.chin = P[8]; lm.noseTip = P[30];
        glm::vec3 browA = mean(P, {17, 18, 19, 20, 21}), browB = mean(P, {22, 23, 24, 25, 26});
        glm::vec3 eyeA = mean(P, {36, 37, 38, 39, 40, 41}), eyeB = mean(P, {42, 43, 44, 45, 46, 47});
        bool aIsLeft = browA.x < browB.x;
        lm.browL = aIsLeft ? browA : browB; lm.browR = aIsLeft ? browB : browA;
        lm.eyeL = aIsLeft ? eyeA : eyeB; lm.eyeR = aIsLeft ? eyeB : eyeA;
        if (aIsLeft) { lm.eyeLOuter = P[36]; lm.eyeLInner = P[39]; lm.eyeRInner = P[42]; lm.eyeROuter = P[45]; }
        else { lm.eyeROuter = P[36]; lm.eyeRInner = P[39]; lm.eyeLInner = P[42]; lm.eyeLOuter = P[45]; }
        // Eyelid target = upper lid mid-point (between the two upper-lid points), not the eye centre.
        if (aIsLeft) { lm.eyeL = 0.5f * (P[37] + P[38]); lm.eyeR = 0.5f * (P[43] + P[44]); } else { lm.eyeR = 0.5f * (P[37] + P[38]); lm.eyeL = 0.5f * (P[43] + P[44]); }
        lm.found = true;
        char buf[160]; std::snprintf(buf, sizeof buf, "dlib: face score %.2f, 68 landmarks, %d lifted from background, render %dx%d", lm.confidence, off, det.width, det.height);
        lm.note = buf;
    } catch (const std::exception& e) { lm.found = false; lm.note = std::string("dlib error: ") + e.what(); }
    return lm;
#endif
}

} // namespace fr

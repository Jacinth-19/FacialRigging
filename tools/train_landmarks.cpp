// fr_train_landmarks - trains the built-in 68-point landmark cascade (rig/landmark_cascade.h) on
// the ICT-FaceKit identity set and writes assets/models/face_landmarks.frlm.
//
//   fr_train_landmarks <ICT-FaceKit/FaceXModel> <assets/models/ict_face/ict_face.fbs> <out.frlm>
//                      [--stages 5] [--augs 8] [--inits 3] [--lambda 1.0] [--holdout 10] [--res 96] [--seed 1]
//
// Ground truth: the 68 Multi-PIE landmark vertices published with ICT-FaceKit, on each of the 100
// identities + the generic neutral, deformed by random mixes of the 53 expression shapes, rotated
// (yaw ±15°, pitch ±10°, roll ±8°) and rescaled so the cascade tolerates imperfect front alignment
// and arbitrary scene units. Face boxes come from the same nose-anchored detector used at run time
// (plus jitter); each stage is a ridge regression from depth/normal patch features to the residual.
#include "core/mesh.h"
#include "core/obj_io.h"
#include "rig/blendshape_io.h"
#include "rig/landmark_cascade.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace fr;

namespace {
struct Identity { Mesh mesh; std::vector<uint32_t> lmVertex; };   // split-vertex index per landmark
struct Sample { GeoImage img; GeoFaceBox box; std::vector<glm::vec2> gt; };
double nowSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

bool choleskySolve(std::vector<double>& A, int F, std::vector<double>& B, int O) {
    for (int j = 0; j < F; ++j) {
        double d = A[size_t(j) * F + j]; for (int k = 0; k < j; ++k) d -= A[size_t(j) * F + k] * A[size_t(j) * F + k];
        if (d <= 0) return false;
        d = std::sqrt(d); A[size_t(j) * F + j] = d; const double inv = 1.0 / d;
#pragma omp parallel for schedule(static)
        for (int i = j + 1; i < F; ++i) { double s = A[size_t(i) * F + j]; const double* ri = &A[size_t(i) * F]; const double* rj = &A[size_t(j) * F]; for (int k = 0; k < j; ++k) s -= ri[k] * rj[k]; A[size_t(i) * F + j] = s * inv; }
    }
#pragma omp parallel for schedule(static)
    for (int o = 0; o < O; ++o) {
        for (int i = 0; i < F; ++i) { double s = B[size_t(i) * O + o]; const double* ri = &A[size_t(i) * F]; for (int k = 0; k < i; ++k) s -= ri[k] * B[size_t(k) * O + o]; B[size_t(i) * O + o] = s / ri[i]; }
        for (int i = F - 1; i >= 0; --i) { double s = B[size_t(i) * O + o]; for (int k = i + 1; k < F; ++k) s -= A[size_t(k) * F + i] * B[size_t(k) * O + o]; B[size_t(i) * O + o] = s / A[size_t(i) * F + i]; }
    }
    return true;
}
float interOcular(const std::vector<glm::vec2>& gt) { glm::vec2 el(0), er(0); for (int i = 36; i <= 41; ++i) el += gt[size_t(i)]; for (int i = 42; i <= 47; ++i) er += gt[size_t(i)]; return glm::length(el - er) / 6.0f; }
float nme(const std::vector<glm::vec2>& a, const std::vector<glm::vec2>& gt) { float s = 0; for (size_t i = 0; i < a.size(); ++i) s += glm::length(a[i] - gt[i]); return s / float(a.size()) / std::max(interOcular(gt), 1e-6f); }
} // namespace

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: fr_train_landmarks <FaceXModel dir> <ict_face.fbs> <out.frlm> [--stages N --augs N --inits N --lambda x --holdout N --res N --seed N]\n"); return 1; }
    std::string dir = argv[1], fbs = argv[2], out = argv[3];
    int stages = 5, augs = 8, inits = 3, holdout = 10, res = 96, seed = 1; float lambda = 1.0f;
    for (int i = 4; i + 1 < argc; i += 2) { std::string a = argv[i]; if (a == "--stages") stages = std::atoi(argv[i + 1]); else if (a == "--augs") augs = std::atoi(argv[i + 1]); else if (a == "--inits") inits = std::atoi(argv[i + 1]); else if (a == "--lambda") lambda = float(std::atof(argv[i + 1])); else if (a == "--holdout") holdout = std::atoi(argv[i + 1]); else if (a == "--res") res = std::atoi(argv[i + 1]); else if (a == "--seed") seed = std::atoi(argv[i + 1]); }

    std::vector<Identity> ids; std::vector<std::string> files = {dir + "/generic_neutral_mesh.obj"};
    for (int i = 0; i < 100; ++i) { char b[64]; std::snprintf(b, sizeof b, "/identity%03d.obj", i); files.push_back(dir + b); }
    const int* lmv = LandmarkCascade::ictLandmarkVertices();
    double t0 = nowSec();
    for (const auto& f : files) {
        Identity id; std::string err; if (!loadObj(f, id.mesh, &err)) { if (ids.empty()) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; } continue; }
        std::vector<uint32_t> firstOf; for (uint32_t v = 0; v < id.mesh.sourceVertex.size(); ++v) { uint32_t s = id.mesh.sourceVertex[v]; if (s >= firstOf.size()) firstOf.resize(size_t(s) + 1, UINT32_MAX); if (firstOf[s] == UINT32_MAX) firstOf[s] = v; }
        id.lmVertex.resize(kLm68); for (int l = 0; l < kLm68; ++l) id.lmVertex[size_t(l)] = firstOf[size_t(lmv[l])];
        ids.push_back(std::move(id));
    }
    std::printf("loaded %zu identities in %.1fs (%zu verts)\n", ids.size(), nowSec() - t0, ids[0].mesh.vertexCount());
    std::vector<BlendShape> shapes; { std::string err; size_t srcCount = 0; for (uint32_t s : ids[0].mesh.sourceVertex) srcCount = std::max<size_t>(srcCount, s + 1); if (!loadBlendShapesFRBS(fbs, shapes, srcCount, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; } remapBlendShapesToMesh(shapes, ids[0].mesh); }
    std::printf("%zu expression shapes\n", shapes.size());
    const int facePartOf0 = ids[0].mesh.findPart("Face");   // ICT source OBJs have no named groups (-1): render everything, like an unknown scan
    (void)facePartOf0;

    LandmarkCascade model; model.res = res;
    std::mt19937 rng{static_cast<uint32_t>(seed)}; std::uniform_real_distribution<float> U(0.0f, 1.0f);
    auto makeSample = [&](const Identity& id, bool augment) {
        std::vector<glm::vec3> P = id.mesh.positions;
        if (augment) {
            int n = int(U(rng) * 5);
            for (int k = 0; k < n; ++k) { const BlendShape& bs = shapes[size_t(U(rng) * float(shapes.size())) % shapes.size()]; float w = 0.3f + 0.7f * U(rng); for (size_t i = 0; i < bs.indices.size(); ++i) P[bs.indices[i]] += w * bs.deltas[i]; }
            glm::vec3 lo(1e30f), hi(-1e30f); for (auto& p : P) { lo = glm::min(lo, p); hi = glm::max(hi, p); } glm::vec3 c = 0.5f * (lo + hi);
            glm::mat4 R = glm::rotate(glm::mat4(1.0f), glm::radians((U(rng) * 2 - 1) * 8.0f), glm::vec3(0, 0, 1)) * glm::rotate(glm::mat4(1.0f), glm::radians((U(rng) * 2 - 1) * 15.0f), glm::vec3(1, 0, 0)) * glm::rotate(glm::mat4(1.0f), glm::radians((U(rng) * 2 - 1) * 15.0f), glm::vec3(0, 1, 0));
            float sc = 0.02f + U(rng) * 5.0f;
            for (auto& p : P) p = c + sc * glm::vec3(R * glm::vec4(p - c, 1.0f));
        }
        std::vector<glm::vec3> L(kLm68); for (int l = 0; l < kLm68; ++l) L[size_t(l)] = P[id.lmVertex[size_t(l)]];
        Sample s; s.box = LandmarkCascade::detectBox(id.mesh, P);
        if (augment) { s.box.topLeft += glm::vec2((U(rng) * 2 - 1) * 0.08f, (U(rng) * 2 - 1) * 0.10f) * s.box.side; float k = 1.0f + (U(rng) * 2 - 1) * 0.15f; s.box.topLeft += 0.5f * (1.0f - k) * s.box.side * glm::vec2(1, -1); s.box.side *= k; }
        s.img = model.renderBox(id.mesh, P, s.box);
        s.gt.resize(kLm68); for (int l = 0; l < kLm68; ++l) s.gt[size_t(l)] = glm::vec2((L[size_t(l)].x - s.box.topLeft.x) / s.box.side, (s.box.topLeft.y - L[size_t(l)].y) / s.box.side);
        return s;
    };
    const size_t nTrainIds = ids.size() - size_t(holdout);
    std::vector<Sample> train, val; t0 = nowSec();
    for (size_t i = 0; i < ids.size(); ++i) { bool isVal = i >= nTrainIds; auto& dst = isVal ? val : train; dst.push_back(makeSample(ids[i], false)); for (int a = 0; a < (isVal ? std::max(1, augs / 2) : augs); ++a) dst.push_back(makeSample(ids[i], true)); }
    std::printf("rendered %zu train / %zu val samples in %.1fs\n", train.size(), val.size(), nowSec() - t0);
    model.meanShape.assign(kLm68, glm::vec2(0.0f)); int nm = 0;
    for (size_t i = 0; i < train.size(); i += size_t(augs + 1)) { for (int l = 0; l < kLm68; ++l) model.meanShape[size_t(l)] += train[i].gt[size_t(l)]; ++nm; }
    for (auto& p : model.meanShape) p /= float(nm);

    struct Cur { size_t sample; std::vector<glm::vec2> shape; };
    std::vector<Cur> cur; std::normal_distribution<float> N01(0.0f, 1.0f);
    for (size_t i = 0; i < train.size(); ++i) for (int k = 0; k < inits; ++k) {
        Cur c; c.sample = i; c.shape = model.meanShape;
        if (k > 0) { glm::vec2 t(N01(rng) * 0.04f, N01(rng) * 0.04f); float sc = 1.0f + N01(rng) * 0.06f; glm::vec2 cen(0.5f, 0.5f); for (auto& p : c.shape) p = cen + (p - cen) * sc + t; }
        cur.push_back(std::move(c));
    }
    std::vector<Cur> vcur; for (size_t i = 0; i < val.size(); ++i) vcur.push_back(Cur{i, model.meanShape});
    auto meanErr = [&](const std::vector<Cur>& cs, const std::vector<Sample>& ss) { double e = 0; for (auto& c : cs) e += nme(c.shape, ss[c.sample].gt); return float(e / double(cs.size())); };
    std::printf("stage 0 (mean shape): train NME %.4f  val NME %.4f\n", meanErr(cur, train), meanErr(vcur, val)); std::fflush(stdout);

    const int F = model.featureCount() + 1, O = 2 * kLm68; const size_t N = cur.size();
    std::vector<float> X(N * size_t(F)), Y(N * size_t(O));
    for (int st = 0; st < stages; ++st) {
        t0 = nowSec();
#pragma omp parallel for schedule(dynamic, 16)
        for (long long n = 0; n < (long long)N; ++n) {
            std::vector<float> f; const Cur& c = cur[size_t(n)]; const Sample& s = train[c.sample];
            model.features(s.img, s.box, c.shape, f); std::copy(f.begin(), f.end(), X.begin() + size_t(n) * F);
            for (int l = 0; l < kLm68; ++l) { Y[size_t(n) * O + 2 * l] = s.gt[size_t(l)].x - c.shape[size_t(l)].x; Y[size_t(n) * O + 2 * l + 1] = s.gt[size_t(l)].y - c.shape[size_t(l)].y; }
        }
        std::vector<double> A(size_t(F) * F, 0.0), B(size_t(F) * O, 0.0);
#pragma omp parallel for schedule(dynamic, 8)
        for (int i = 0; i < F; ++i) for (size_t n = 0; n < N; ++n) {
            const float xi = X[n * F + i]; if (xi == 0.0f) continue; const float* xr = &X[n * F];
            double* ar = &A[size_t(i) * F]; for (int j = 0; j <= i; ++j) ar[j] += double(xi) * xr[j];
            const float* yr = &Y[n * O]; double* br = &B[size_t(i) * O]; for (int j = 0; j < O; ++j) br[j] += double(xi) * yr[j];
        }
        for (int i = 0; i < F; ++i) { for (int j = 0; j < i; ++j) A[size_t(j) * F + i] = A[size_t(i) * F + j]; A[size_t(i) * F + i] += double(lambda) * double(N) / 1000.0 * (i == F - 1 ? 1e-3 : 1.0); }
        if (!choleskySolve(A, F, B, O)) { std::fprintf(stderr, "stage %d: not positive definite (raise --lambda)\n", st + 1); return 1; }
        std::vector<float> R(size_t(F) * O); for (size_t i = 0; i < R.size(); ++i) R[i] = float(B[i]);
        model.stages.push_back(std::move(R));
        auto apply = [&](std::vector<Cur>& cs, const std::vector<Sample>& ss) {
#pragma omp parallel for schedule(dynamic, 16)
            for (long long n = 0; n < (long long)cs.size(); ++n) { std::vector<float> f; const Sample& s = ss[cs[size_t(n)].sample]; model.features(s.img, s.box, cs[size_t(n)].shape, f); const auto& Rm = model.stages.back(); std::vector<float> d(size_t(O), 0.0f); for (int i = 0; i < F; ++i) { float fi = f[size_t(i)]; if (fi == 0.0f) continue; const float* row = &Rm[size_t(i) * O]; for (int j = 0; j < O; ++j) d[size_t(j)] += fi * row[j]; } for (int l = 0; l < kLm68; ++l) { cs[size_t(n)].shape[size_t(l)].x += d[size_t(2 * l)]; cs[size_t(n)].shape[size_t(l)].y += d[size_t(2 * l + 1)]; } }
        };
        apply(cur, train); apply(vcur, val);
        std::printf("stage %d: train NME %.4f  val NME %.4f  (%.0fs)\n", st + 1, meanErr(cur, train), meanErr(vcur, val), nowSec() - t0); std::fflush(stdout);
    }
    char buf[512]; std::snprintf(buf, sizeof buf, "SDM cascade: %d stages, %zu ICT identities x %d augs x %d inits, res %d patch %d spacing %.3f, lambda %.2f; val NME %.4f on %d held-out identities", stages, nTrainIds, augs, inits, res, model.patch, model.spacing, lambda, meanErr(vcur, val), holdout);
    model.info = buf;
    std::string err; if (!model.save(out, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    LandmarkCascade back; if (!back.load(out, &err)) { std::fprintf(stderr, "reload failed: %s\n", err.c_str()); return 1; }
    double e = 0; for (auto& s : val) e += nme(back.run(s.img, s.box, back.meanShape), s.gt);
    std::printf("wrote %s\n  %s\n  quantised val NME %.4f\n", out.c_str(), model.info.c_str(), e / double(val.size()));
    const char* names[] = {"jaw", "brows", "nose", "eyes", "mouth"}; const int ranges[][2] = {{0, 16}, {17, 26}, {27, 35}, {36, 47}, {48, 67}};
    for (int r = 0; r < 5; ++r) { double s = 0; int cnt = 0; for (auto& v : val) { auto sh = back.run(v.img, v.box, back.meanShape); float io = interOcular(v.gt); for (int i = ranges[r][0]; i <= ranges[r][1]; ++i) { s += glm::length(sh[size_t(i)] - v.gt[size_t(i)]) / io; ++cnt; } } std::printf("  %-6s NME %.4f\n", names[r], s / cnt); }
    return 0;
}

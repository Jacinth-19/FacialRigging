// fr_train_emotion - trains the utterance-level emotion classifier (EmotionClassifier) on CREMA-D
// (7442 clips, 91 actors, 6 emotions: ANG DIS FEA HAP NEU SAD). Files are named
// <actor>_<sentence>_<EMO>_<level>.wav. The descriptor is EmotionClassifier::describe() - the same
// pooled statistics the app computes at run time - so there is no train/serve skew. Split is
// speaker-disjoint (actors 1001..1091; the last `--holdout` fraction of a shuffled actor list is
// held out). Small MLP, Adam, weighted cross-entropy, cosine LR, early stopping on held-out UAR.
//
//   fr_train_emotion --data data/crema_d --out assets/models/emotion_mlp.frvm [--epochs 80] [--hidden 96] [--holdout 0.2]
#include "audio/emotion_classifier.h"
#include "audio/wav_io.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace fr;
namespace fs = std::filesystem;
struct Sample { std::vector<float> x; int y; std::string actor; };

int main(int argc, char** argv) {
    std::string data = "data/crema_d", out = "assets/models/emotion_mlp.frvm";
    int epochs = 80, hidden = 96; float lr = 1.5e-3f, wd = 3e-4f, holdout = 0.2f, dropout = 0.2f; unsigned seed = 7; int maxFiles = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i]; auto next = [&]() { return std::string(i + 1 < argc ? argv[++i] : ""); };
        if (a == "--data") data = next(); else if (a == "--out") out = next(); else if (a == "--epochs") epochs = std::stoi(next());
        else if (a == "--hidden") hidden = std::stoi(next()); else if (a == "--lr") lr = std::stof(next()); else if (a == "--wd") wd = std::stof(next());
        else if (a == "--holdout") holdout = std::stof(next()); else if (a == "--seed") seed = unsigned(std::stoul(next())); else if (a == "--dropout") dropout = std::stof(next());
        else if (a == "--max-files") maxFiles = std::stoi(next());
        else { std::printf("usage: fr_train_emotion --data DIR --out FILE.frvm [--epochs N] [--hidden H] [--lr F] [--wd F] [--holdout F] [--dropout F] [--max-files N]\n"); return a == "--help" ? 0 : 1; }
    }
    // ---- files
    std::vector<fs::path> files;
    for (auto& d : fs::directory_iterator(data)) if (d.is_regular_file() && d.path().extension() == ".wav") files.push_back(d.path());
    std::sort(files.begin(), files.end());
    std::mt19937 rng(seed);
    if (maxFiles > 0 && int(files.size()) > maxFiles) { std::shuffle(files.begin(), files.end(), rng); files.resize(size_t(maxFiles)); }
    if (files.empty()) { std::fprintf(stderr, "no .wav under %s\n", data.c_str()); return 1; }
    // ---- features
    const int K = EmotionClassifier::kClasses;
    std::vector<Sample> all; std::set<std::string> actors; std::vector<int> counts(size_t(K), 0);
    FeatureExtractor fx;
    size_t done = 0;
    for (auto& p : files) {
        std::string stem = p.stem().string(); // 1001_DFA_ANG_XX
        std::vector<std::string> parts; { std::stringstream ss(stem); std::string t; while (std::getline(ss, t, '_')) parts.push_back(t); }
        if (parts.size() < 4) continue;
        bool ok; Emotion e = emotionFromCremaCode(parts[2], &ok); if (!ok) continue;
        AudioBuffer a; std::string err; if (!loadWav(p.string(), a, &err)) { std::fprintf(stderr, "skip %s: %s\n", stem.c_str(), err.c_str()); continue; }
        FeatureTrack tr = fx.extract(a);
        all.push_back({EmotionClassifier::describe(tr), int(e), parts[0]}); actors.insert(parts[0]); ++counts[size_t(e)];
        if (++done % 500 == 0) { std::printf("  features %zu / %zu\r", done, files.size()); std::fflush(stdout); }
    }
    std::printf("\nclips %zu, actors %zu; class counts:", all.size(), actors.size());
    for (int k = 0; k < K; ++k) std::printf(" %s=%d", emotionName(Emotion(k)), counts[size_t(k)]); std::printf("\n");
    // ---- speaker-disjoint split
    std::vector<std::string> act(actors.begin(), actors.end()); std::shuffle(act.begin(), act.end(), rng);
    size_t nHold = std::max<size_t>(1, size_t(holdout * float(act.size()))); std::set<std::string> hold(act.begin(), act.begin() + long(nHold));
    std::vector<Sample> train, test; for (auto& s : all) (hold.count(s.actor) ? test : train).push_back(s);
    const int D = int(train.front().x.size());
    std::printf("train %zu clips (%zu actors)  held-out %zu clips (%zu actors)  descriptor %d\n", train.size(), act.size() - nHold, test.size(), nHold, D);
    // ---- standardise
    std::vector<float> mean(size_t(D), 0), var(size_t(D), 0), invStd(size_t(D), 1);
    for (auto& s : train) for (int i = 0; i < D; ++i) mean[size_t(i)] += s.x[size_t(i)];
    for (auto& m : mean) m /= float(train.size());
    for (auto& s : train) for (int i = 0; i < D; ++i) { float d = s.x[size_t(i)] - mean[size_t(i)]; var[size_t(i)] += d * d; }
    for (int i = 0; i < D; ++i) invStd[size_t(i)] = 1.0f / std::sqrt(var[size_t(i)] / float(train.size()) + 1e-6f);
    auto norm = [&](std::vector<Sample>& v) { for (auto& s : v) for (int i = 0; i < D; ++i) s.x[size_t(i)] = std::clamp((s.x[size_t(i)] - mean[size_t(i)]) * invStd[size_t(i)], -6.0f, 6.0f); };
    norm(train); norm(test);
    std::vector<float> cw(size_t(K), 1.0f); for (int k = 0; k < K; ++k) cw[size_t(k)] = counts[size_t(k)] ? 1.0f / float(counts[size_t(k)]) : 0; { float s = 0; for (auto w : cw) s += w; for (auto& w : cw) w *= float(K) / s; }
    // ---- MLP D -> hidden -> hidden -> K
    std::vector<int> sizes = {D, hidden, hidden, K}; const int L = 3;
    std::vector<std::vector<float>> W(L), B(L), mW(L), vW(L), mB(L), vB(L);
    std::normal_distribution<float> nd(0, 1);
    for (int l = 0; l < L; ++l) { W[size_t(l)].resize(size_t(sizes[size_t(l)] * sizes[size_t(l) + 1])); B[size_t(l)].assign(size_t(sizes[size_t(l) + 1]), 0); float sc = std::sqrt(2.0f / float(sizes[size_t(l)])); for (auto& w : W[size_t(l)]) w = nd(rng) * sc; mW[size_t(l)].assign(W[size_t(l)].size(), 0); vW[size_t(l)].assign(W[size_t(l)].size(), 0); mB[size_t(l)].assign(B[size_t(l)].size(), 0); vB[size_t(l)].assign(B[size_t(l)].size(), 0); }
    std::uniform_real_distribution<float> ud(0, 1);
    auto forward = [&](const std::vector<float>& x, std::vector<std::vector<float>>& acts, std::vector<std::vector<char>>* masks) {
        acts.assign(size_t(L + 1), {}); acts[0] = x;
        for (int l = 0; l < L; ++l) {
            int nIn = sizes[size_t(l)], nOut = sizes[size_t(l) + 1]; acts[size_t(l) + 1].assign(size_t(nOut), 0);
            for (int o = 0; o < nOut; ++o) { float a = B[size_t(l)][size_t(o)]; const float* w = &W[size_t(l)][size_t(o * nIn)]; for (int i = 0; i < nIn; ++i) a += w[i] * acts[size_t(l)][size_t(i)]; acts[size_t(l) + 1][size_t(o)] = l < L - 1 ? std::max(a, 0.0f) : a; }
            if (masks && l < L - 1 && dropout > 0) { (*masks)[size_t(l)].assign(size_t(nOut), 1); for (int o = 0; o < nOut; ++o) { if (ud(rng) < dropout) { (*masks)[size_t(l)][size_t(o)] = 0; acts[size_t(l) + 1][size_t(o)] = 0; } else acts[size_t(l) + 1][size_t(o)] /= (1 - dropout); } }
        }
    };
    auto evaluate = [&](const std::vector<Sample>& set, std::vector<std::vector<int>>* conf, float* uar) {
        int correct = 0; std::vector<std::vector<float>> acts; std::vector<std::vector<int>> c(size_t(K), std::vector<int>(size_t(K), 0));
        for (auto& s : set) { forward(s.x, acts, nullptr); int p = int(std::max_element(acts[size_t(L)].begin(), acts[size_t(L)].end()) - acts[size_t(L)].begin()); correct += p == s.y; c[size_t(s.y)][size_t(p)]++; }
        if (uar) { float u = 0; int n = 0; for (int k = 0; k < K; ++k) { int row = 0; for (int j = 0; j < K; ++j) row += c[size_t(k)][size_t(j)]; if (row) { u += float(c[size_t(k)][size_t(k)]) / float(row); ++n; } } *uar = n ? u / float(n) : 0; }
        if (conf) *conf = c;
        return set.empty() ? 0.0f : float(correct) / float(set.size());
    };
    const int batch = 32; const float b1 = 0.9f, b2 = 0.999f, eps = 1e-8f; long step = 0;
    std::vector<size_t> order(train.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::vector<std::vector<float>> gW(L), gB(L), acts; std::vector<std::vector<char>> masks; masks.resize(size_t(L));
    float bestUar = 0; auto bestW = W; auto bestB = B; int bestEpoch = 0;
    for (int ep = 1; ep <= epochs; ++ep) {
        std::shuffle(order.begin(), order.end(), rng);
        float lrE = lr * 0.5f * (1 + std::cos(float(M_PI) * float(ep - 1) / float(epochs)));
        double lossSum = 0;
        for (size_t start = 0; start < order.size(); start += size_t(batch)) {
            for (int l = 0; l < L; ++l) { gW[size_t(l)].assign(W[size_t(l)].size(), 0); gB[size_t(l)].assign(B[size_t(l)].size(), 0); }
            size_t end = std::min(order.size(), start + size_t(batch)); float wsum = 0;
            for (size_t bi = start; bi < end; ++bi) {
                const Sample& s = train[order[bi]];
                // light input noise augmentation
                std::vector<float> x = s.x; for (auto& v : x) v += 0.05f * nd(rng);
                forward(x, acts, &masks);
                std::vector<float> p = acts[size_t(L)]; float mx = *std::max_element(p.begin(), p.end()), sum = 0; for (auto& v : p) { v = std::exp(v - mx); sum += v; } for (auto& v : p) v /= sum;
                float w = cw[size_t(s.y)]; wsum += w; lossSum += -w * std::log(std::max(p[size_t(s.y)], 1e-9f));
                std::vector<float> delta(size_t(K), 0.0f); for (int k = 0; k < K; ++k) delta[size_t(k)] = w * (p[size_t(k)] - (k == s.y ? 1.0f : 0.0f));
                for (int l = L - 1; l >= 0; --l) {
                    int nIn = sizes[size_t(l)], nOut = sizes[size_t(l) + 1]; std::vector<float> prev(size_t(nIn), 0);
                    for (int o = 0; o < nOut; ++o) { float d = delta[size_t(o)]; gB[size_t(l)][size_t(o)] += d; float* gw = &gW[size_t(l)][size_t(o * nIn)]; const float* w2 = &W[size_t(l)][size_t(o * nIn)]; for (int i = 0; i < nIn; ++i) { gw[i] += d * acts[size_t(l)][size_t(i)]; prev[size_t(i)] += d * w2[i]; } }
                    if (l > 0) for (int i = 0; i < nIn; ++i) if (acts[size_t(l)][size_t(i)] <= 0 || (dropout > 0 && !masks[size_t(l - 1)][size_t(i)])) prev[size_t(i)] = 0;
                    delta.swap(prev);
                }
            }
            ++step; float inv = 1.0f / std::max(wsum, 1e-6f), c1 = 1 - std::pow(b1, float(step)), c2 = 1 - std::pow(b2, float(step));
            auto adam = [&](std::vector<float>& p, std::vector<float>& g, std::vector<float>& m, std::vector<float>& v, bool decay) { for (size_t i = 0; i < p.size(); ++i) { float gi = g[i] * inv + (decay ? wd * p[i] : 0); m[i] = b1 * m[i] + (1 - b1) * gi; v[i] = b2 * v[i] + (1 - b2) * gi * gi; p[i] -= lrE * (m[i] / c1) / (std::sqrt(v[i] / c2) + eps); } };
            for (int l = 0; l < L; ++l) { adam(W[size_t(l)], gW[size_t(l)], mW[size_t(l)], vW[size_t(l)], true); adam(B[size_t(l)], gB[size_t(l)], mB[size_t(l)], vB[size_t(l)], false); }
        }
        float uarTe = 0, uarTr = 0; float accTr = evaluate(train, nullptr, &uarTr), accTe = evaluate(test, nullptr, &uarTe);
        bool best = uarTe > bestUar; if (best) { bestUar = uarTe; bestW = W; bestB = B; bestEpoch = ep; }
        std::printf("epoch %3d  loss %.4f  train acc %.3f  held-out acc %.3f  UAR %.3f%s\n", ep, lossSum / double(train.size()), accTr, accTe, uarTe, best ? "  *" : ""); std::fflush(stdout);
    }
    W = bestW; B = bestB;
    std::vector<std::vector<int>> conf; float uar = 0; float acc = evaluate(test, &conf, &uar);
    std::printf("\nbest held-out: acc %.3f  UAR %.3f (epoch %d, chance %.3f). Confusion (rows = truth):\n%9s", acc, uar, bestEpoch, 1.0f / K, "");
    for (int k = 0; k < K; ++k) std::printf("%8s", emotionName(Emotion(k)));
    for (int r = 0; r < K; ++r) { std::printf("\n%9s", emotionName(Emotion(r))); for (int c = 0; c < K; ++c) std::printf("%8d", conf[size_t(r)][size_t(c)]); }
    std::printf("\n");
    VisemeMlpWeights model; model.inputs = D; model.context = 1; model.layerSizes = sizes; model.W = W; model.b = B; model.mean = mean; model.invStd = invStd;
    std::ostringstream info; info << "CREMA-D " << all.size() << " clips / " << actors.size() << " actors, speaker-disjoint hold-out " << nHold << " actors, MLP " << D << "-" << hidden << "-" << hidden << "-" << K << ", held-out acc " << int(acc * 1000) / 10.0f << "% UAR " << int(uar * 1000) / 10.0f << "%, epoch " << bestEpoch;
    model.info = info.str();
    std::string err; if (!model.save(out, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::printf("wrote %s (%s)\n", out.c_str(), model.info.c_str());
    return 0;
}

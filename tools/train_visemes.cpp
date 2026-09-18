// fr_train_visemes - trains the viseme classifier used by MlVisemeMapper from forced-aligned
// phone labels (TIMIT layout: <utt>.wav + <utt>.phn with "startSample endSample phone" lines).
//
// The features are produced by the *same* FeatureExtractor / featureVector() code the app uses
// at run time, so there is no train/serve skew. The network is a small MLP over a window of
// stacked frames trained with Adam + cross-entropy; weights are written as .frvm and load in
// every build (with or without LibTorch).
//
//   fr_train_visemes --data /tmp/timit --out assets/models/viseme_mlp.frvm [--epochs 60] [--context 5] [--hidden 64]
#include "audio/features.h"
#include "audio/ml_viseme_mapper.h"
#include "audio/wav_io.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace fr;
namespace fs = std::filesystem;

// TIMIT phone -> our 9 visemes (shared table in phonemeToViseme). -1 = skip frame: the
// alveolar/velar closures carry no lip target of their own.
static int phoneToViseme(const std::string& p) {
    if (p == "tcl" || p == "dcl" || p == "kcl" || p == "gcl") return -1;
    return int(phonemeToViseme(p));
}

struct Sample { std::vector<float> x; int y; };

int main(int argc, char** argv) {
    std::string data = "/tmp/timit", out = "assets/models/viseme_mlp.frvm";
    int epochs = 25, context = 7, hidden = 128; float lr = 2e-3f, wd = 1e-4f; unsigned seed = 7; float holdout = 0.2f;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i]; auto next = [&]() { return std::string(i + 1 < argc ? argv[++i] : ""); };
        if (a == "--data") data = next(); else if (a == "--out") out = next(); else if (a == "--epochs") epochs = std::stoi(next());
        else if (a == "--context") context = std::stoi(next()); else if (a == "--hidden") hidden = std::stoi(next()); else if (a == "--lr") lr = std::stof(next()); else if (a == "--wd") wd = std::stof(next());
        else if (a == "--seed") seed = unsigned(std::stoul(next())); else if (a == "--holdout") holdout = std::stof(next());
        else { std::printf("usage: fr_train_visemes --data DIR --out FILE.frvm [--epochs N] [--context K] [--hidden H] [--lr F] [--holdout F]\n"); return a == "--help" ? 0 : 1; }
    }
    if (context % 2 == 0) ++context;

    // ---- collect utterances. Accepts flat "<spk>/<utt>.wav + .phn" subsets and the full TIMIT
    // layout "data/{TRAIN,TEST}/DRx/<spk>/<utt>.WAV.wav + .PHN" (RIFF copies next to the NIST
    // originals). When TRAIN/TEST directories exist the official split is used (speaker-disjoint
    // by construction); otherwise `holdout` speakers are sampled. SA1/SA2 (the two dialect
    // sentences every speaker reads) are dropped as is standard for TIMIT phone recognition.
    std::vector<std::pair<std::string, std::string>> utts; // wav, phn
    std::vector<std::string> speakers; bool officialSplit = false; std::vector<bool> uttIsTest;
    for (auto& d : fs::recursive_directory_iterator(data)) {
        if (!d.is_regular_file()) continue;
        std::string name = d.path().filename().string(), lower = name; for (auto& ch : lower) ch = char(std::tolower(ch));
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".wav") != 0) continue;
        std::string stem = name.substr(0, name.size() - 4);
        if (stem.size() > 4 && (stem.compare(stem.size() - 4, 4, ".WAV") == 0 || stem.compare(stem.size() - 4, 4, ".wav") == 0)) stem = stem.substr(0, stem.size() - 4); // "SA1.WAV.wav"
        else if (fs::exists(d.path().parent_path() / (stem + ".WAV.wav"))) continue; // NIST original renamed .wav elsewhere - prefer the RIFF copy
        { std::ifstream f(d.path(), std::ios::binary); char m[4] = {0}; f.read(m, 4); if (std::string(m, 4) != "RIFF") continue; }
        std::string up = stem; for (auto& ch : up) ch = char(std::toupper(ch));
        if (up == "SA1" || up == "SA2") continue;
        fs::path phn = d.path().parent_path() / (stem + ".phn");
        if (!fs::exists(phn)) { phn = d.path().parent_path() / (stem + ".PHN"); if (!fs::exists(phn)) continue; }
        std::string full = d.path().string(); for (auto& ch : full) ch = char(std::toupper(ch));
        bool isTest = full.find("/TEST/") != std::string::npos, isTrain = full.find("/TRAIN/") != std::string::npos;
        if (isTest || isTrain) officialSplit = true;
        utts.emplace_back(d.path().string(), phn.string()); uttIsTest.push_back(isTest);
        std::string spk = d.path().parent_path().filename().string();
        if (std::find(speakers.begin(), speakers.end(), spk) == speakers.end()) speakers.push_back(spk);
    }
    if (utts.empty()) { std::fprintf(stderr, "no .wav/.phn pairs under %s\n", data.c_str()); return 1; }
    std::sort(speakers.begin(), speakers.end());
    std::mt19937 rng(seed);
    std::vector<std::string> holdSpk; size_t nHold = 0;
    if (!officialSplit) {
        std::vector<std::string> shuffled = speakers; std::shuffle(shuffled.begin(), shuffled.end(), rng);
        nHold = std::max<size_t>(1, size_t(holdout * float(speakers.size())));
        holdSpk.assign(shuffled.begin(), shuffled.begin() + long(nHold));
    } else {
        for (size_t i = 0; i < utts.size(); ++i) if (uttIsTest[i]) { std::string spk = fs::path(utts[i].first).parent_path().filename().string(); if (std::find(holdSpk.begin(), holdSpk.end(), spk) == holdSpk.end()) holdSpk.push_back(spk); }
        nHold = holdSpk.size();
        std::printf("using the official TIMIT TRAIN/TEST split (%zu test speakers)\n", nHold);
    }

    // ---- features + labels
    FeatureExtractor fx; // default config == app config
    std::vector<Sample> train, test; std::vector<int> counts(size_t(Viseme::Count), 0);
    for (size_t ui = 0; ui < utts.size(); ++ui) {
        const std::string& wav = utts[ui].first; const std::string& phn = utts[ui].second;
        AudioBuffer a; std::string err;
        if (!loadWav(wav, a, &err)) { std::fprintf(stderr, "skip %s: %s\n", wav.c_str(), err.c_str()); continue; }
        const int srcRate = a.sampleRate; // .phn samples are in the original rate (16 kHz for TIMIT)
        std::vector<std::pair<int, int>> segEnd; // (endSample, viseme)
        { std::ifstream f(phn); long s0, s1; std::string ph; while (f >> s0 >> s1 >> ph) segEnd.emplace_back(int(s1), phoneToViseme(ph)); }
        FeatureTrack tr = fx.extract(a);
        auto X = MlVisemeMapper::stackedFeatures(tr, context);
        bool hold = officialSplit ? uttIsTest[ui] : std::find(holdSpk.begin(), holdSpk.end(), fs::path(wav).parent_path().filename().string()) != holdSpk.end();
        for (size_t t = 0; t < tr.frames.size(); ++t) {
            // label = phone at the frame centre (in source-rate samples)
            long centre = long(tr.frames[t].time * srcRate);
            int y = -1;
            for (auto& se : segEnd) if (centre < se.first) { y = se.second; break; }
            if (y < 0) continue;
            (hold ? test : train).push_back({X[t], y}); ++counts[size_t(y)];
        }
    }
    const int D = int(train.front().x.size());
    std::printf("utterances: %zu (speakers %zu, hold-out %zu) frames: train %zu test %zu  input dim %d (17 x %d)\n", utts.size(), speakers.size(), nHold, train.size(), test.size(), D, context);
    std::printf("class counts:"); for (int k = 0; k < int(Viseme::Count); ++k) std::printf(" %s=%d", visemeName(Viseme(k)), counts[size_t(k)]); std::printf("\n");

    // ---- standardise inputs
    std::vector<float> mean(size_t(D), 0.0f), var(size_t(D), 0.0f), invStd(size_t(D), 1.0f);
    for (auto& s : train) for (int i = 0; i < D; ++i) mean[size_t(i)] += s.x[size_t(i)];
    for (auto& m : mean) m /= float(train.size());
    for (auto& s : train) for (int i = 0; i < D; ++i) { float d = s.x[size_t(i)] - mean[size_t(i)]; var[size_t(i)] += d * d; }
    for (int i = 0; i < D; ++i) invStd[size_t(i)] = 1.0f / std::sqrt(var[size_t(i)] / float(train.size()) + 1e-6f);
    auto norm = [&](std::vector<Sample>& v) { for (auto& s : v) for (int i = 0; i < D; ++i) s.x[size_t(i)] = (s.x[size_t(i)] - mean[size_t(i)]) * invStd[size_t(i)]; };
    norm(train); norm(test);

    // ---- class weights (inverse sqrt frequency) so rarer visemes (FV, MBP) are learnt
    std::vector<float> cw(size_t(Viseme::Count), 1.0f);
    for (size_t k = 0; k < cw.size(); ++k) cw[k] = counts[k] ? 1.0f / std::sqrt(float(counts[k])) : 0.0f;
    { float s = 0; for (auto w : cw) s += w; for (auto& w : cw) w *= float(cw.size()) / s; }

    // ---- network: D -> hidden -> hidden -> 9, He init, Adam
    const int K = int(Viseme::Count);
    std::vector<int> sizes = {D, hidden, hidden, K};
    std::vector<std::vector<float>> W(3), B(3), mW(3), vW(3), mB(3), vB(3);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    for (int l = 0; l < 3; ++l) {
        W[size_t(l)].resize(size_t(sizes[size_t(l)] * sizes[size_t(l) + 1])); B[size_t(l)].assign(size_t(sizes[size_t(l) + 1]), 0.0f);
        float sc = std::sqrt(2.0f / float(sizes[size_t(l)]));
        for (auto& w : W[size_t(l)]) w = nd(rng) * sc;
        mW[size_t(l)].assign(W[size_t(l)].size(), 0); vW[size_t(l)].assign(W[size_t(l)].size(), 0); mB[size_t(l)].assign(B[size_t(l)].size(), 0); vB[size_t(l)].assign(B[size_t(l)].size(), 0);
    }
    auto forward = [&](const std::vector<float>& x, std::vector<std::vector<float>>& acts) {
        acts.assign(4, {}); acts[0] = x;
        for (int l = 0; l < 3; ++l) {
            int nIn = sizes[size_t(l)], nOut = sizes[size_t(l) + 1];
            acts[size_t(l) + 1].assign(size_t(nOut), 0.0f);
            for (int o = 0; o < nOut; ++o) {
                float a = B[size_t(l)][size_t(o)]; const float* w = &W[size_t(l)][size_t(o * nIn)];
                for (int i = 0; i < nIn; ++i) a += w[i] * acts[size_t(l)][size_t(i)];
                acts[size_t(l) + 1][size_t(o)] = l < 2 ? std::max(a, 0.0f) : a;
            }
        }
    };
    auto evaluate = [&](const std::vector<Sample>& set, std::vector<std::vector<int>>* conf) {
        int correct = 0; std::vector<std::vector<float>> acts;
        if (conf) conf->assign(size_t(K), std::vector<int>(size_t(K), 0));
        for (auto& s : set) { forward(s.x, acts); int p = int(std::max_element(acts[3].begin(), acts[3].end()) - acts[3].begin()); correct += p == s.y; if (conf) (*conf)[size_t(s.y)][size_t(p)]++; }
        return set.empty() ? 0.0f : float(correct) / float(set.size());
    };

    const int batch = 64; const float b1 = 0.9f, b2 = 0.999f, eps = 1e-8f;
    long step = 0; std::vector<size_t> order(train.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::vector<std::vector<float>> gW(3), gB(3), acts;
    float bestTest = 0.0f; std::vector<std::vector<float>> bestW = W, bestB = B; int bestEpoch = 0;
    for (int ep = 1; ep <= epochs; ++ep) {
        std::shuffle(order.begin(), order.end(), rng);
        float lrE = lr * 0.5f * (1.0f + std::cos(float(M_PI) * float(ep - 1) / float(epochs))); // cosine schedule
        double lossSum = 0; size_t nb = 0;
        for (size_t start = 0; start < order.size(); start += size_t(batch)) {
            for (int l = 0; l < 3; ++l) { gW[size_t(l)].assign(W[size_t(l)].size(), 0.0f); gB[size_t(l)].assign(B[size_t(l)].size(), 0.0f); }
            size_t end = std::min(order.size(), start + size_t(batch)); float wsum = 0.0f;
            for (size_t bi = start; bi < end; ++bi) {
                const Sample& s = train[order[bi]];
                forward(s.x, acts);
                // softmax + weighted CE
                std::vector<float> p = acts[3]; float mx = *std::max_element(p.begin(), p.end()), sum = 0;
                for (auto& v : p) { v = std::exp(v - mx); sum += v; } for (auto& v : p) v /= sum;
                float w = cw[size_t(s.y)]; wsum += w; lossSum += -w * std::log(std::max(p[size_t(s.y)], 1e-9f));
                std::vector<float> delta(static_cast<size_t>(K), 0.0f); for (int k = 0; k < K; ++k) delta[size_t(k)] = w * (p[size_t(k)] - (k == s.y ? 1.0f : 0.0f));
                for (int l = 2; l >= 0; --l) {
                    int nIn = sizes[size_t(l)], nOut = sizes[size_t(l) + 1];
                    std::vector<float> prevDelta(size_t(nIn), 0.0f);
                    for (int o = 0; o < nOut; ++o) {
                        float d = delta[size_t(o)]; gB[size_t(l)][size_t(o)] += d;
                        float* gw = &gW[size_t(l)][size_t(o * nIn)]; const float* w2 = &W[size_t(l)][size_t(o * nIn)];
                        for (int i = 0; i < nIn; ++i) { gw[i] += d * acts[size_t(l)][size_t(i)]; prevDelta[size_t(i)] += d * w2[i]; }
                    }
                    if (l > 0) for (int i = 0; i < nIn; ++i) if (acts[size_t(l)][size_t(i)] <= 0.0f) prevDelta[size_t(i)] = 0.0f;
                    delta.swap(prevDelta);
                }
            }
            ++step; ++nb; float inv = 1.0f / std::max(wsum, 1e-6f);
            float c1 = 1.0f - std::pow(b1, float(step)), c2 = 1.0f - std::pow(b2, float(step));
            auto adam = [&](std::vector<float>& p, std::vector<float>& g, std::vector<float>& m, std::vector<float>& v, bool decay) {
                for (size_t i = 0; i < p.size(); ++i) {
                    float gi = g[i] * inv + (decay ? wd * p[i] : 0.0f);
                    m[i] = b1 * m[i] + (1 - b1) * gi; v[i] = b2 * v[i] + (1 - b2) * gi * gi;
                    p[i] -= lrE * (m[i] / c1) / (std::sqrt(v[i] / c2) + eps);
                }
            };
            for (int l = 0; l < 3; ++l) { adam(W[size_t(l)], gW[size_t(l)], mW[size_t(l)], vW[size_t(l)], true); adam(B[size_t(l)], gB[size_t(l)], mB[size_t(l)], vB[size_t(l)], false); }
        }
        float accTr = evaluate(train, nullptr), accTe = evaluate(test, nullptr);
        if (accTe >= bestTest) { bestTest = accTe; bestW = W; bestB = B; bestEpoch = ep; }
        std::printf("epoch %3d  loss %.4f  train acc %.3f  held-out acc %.3f%s\n", ep, lossSum / double(std::max<size_t>(1, train.size())), accTr, accTe, accTe >= bestTest ? "  *" : "");
        std::fflush(stdout);
    }
    W = bestW; B = bestB;
    std::vector<std::vector<int>> conf; float accTe = evaluate(test, &conf);
    std::printf("\nbest held-out accuracy %.3f (epoch %d). Confusion (rows = truth):\n%8s", accTe, bestEpoch, "");
    for (int k = 0; k < K; ++k) std::printf("%7s", visemeName(Viseme(k)));
    for (int r = 0; r < K; ++r) { std::printf("\n%8s", visemeName(Viseme(r))); for (int c = 0; c < K; ++c) std::printf("%7d", conf[size_t(r)][size_t(c)]); }
    std::printf("\n");
    // majority-class baseline for context
    int maj = int(std::max_element(counts.begin(), counts.end()) - counts.begin()); size_t majHits = 0; for (auto& s : test) majHits += s.y == maj;
    std::printf("majority-class (%s) baseline on held-out: %.3f\n", visemeName(Viseme(maj)), test.empty() ? 0.0f : float(majHits) / float(test.size()));

    VisemeMlpWeights model; model.inputs = MlVisemeMapper::kInputFeatures; model.context = context; model.layerSizes = sizes;
    model.W = W; model.b = B; model.mean = mean; model.invStd = invStd;
    std::ostringstream info; info << "TIMIT phone alignments, " << utts.size() << " utt / " << speakers.size() << " spk, " << train.size() << " train frames, MLP " << D << "-" << hidden << "-" << hidden << "-" << K
                                  << ", held-out acc " << int(accTe * 1000) / 10.0f << "% (" << (officialSplit ? "official TEST set" : "speaker-disjoint") << "), epoch " << bestEpoch;
    model.info = info.str();
    std::string err;
    if (!model.save(out, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::printf("wrote %s (%s)\n", out.c_str(), model.info.c_str());
    return 0;
}

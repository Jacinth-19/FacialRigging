// Evaluates transcript forced alignment against TIMIT hand-labelled phone times.
// For each utterance: read .TXT (orthography) -> G2P -> align to audio (MLP posteriors) ->
// compare the per-frame *viseme class* against the .PHN ground truth, and the boundary error
// of viseme changes. Usage: fr_eval_alignment <timit/TEST/DRx dir> [--rules] [--max N]
#include "app/pipeline.h"
#include "audio/ml_viseme_mapper.h"
#include "audio/phoneme_aligner.h"
#include "audio/wav_io.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
using namespace fr;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <TIMIT DRx dir> [--rules] [--max N] [--no-coart-baseline]\n", argv[0]); return 2; }
    std::string dir = argv[1]; bool rules = false, gtPhones = false; int maxN = 1 << 30; float temp = -1.0f; double typ = -1.0;
    for (int i = 2; i < argc; ++i) { std::string a = argv[i]; if (a == "--rules") rules = true; else if (a == "--gt-phones") gtPhones = true; else if (a == "--max" && i + 1 < argc) maxN = std::atoi(argv[++i]); else if (a == "--temp" && i + 1 < argc) temp = float(std::atof(argv[++i])); else if (a == "--typ" && i + 1 < argc) typ = std::atof(argv[++i]); }
    std::shared_ptr<VisemeMapper> mapper;
    if (rules) mapper = std::make_shared<VisemeMapper>();
    else { auto m = std::make_shared<MlVisemeMapper>(); std::string err; if (!m->loadDefault(FR_ASSET_DIR, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; } mapper = m; }
    FeatureExtractor fx; PhonemeAligner aligner;
    if (temp > 0) aligner.settings.temperature = temp;
    if (typ > 0) aligner.settings.typicalPhoneSec = typ;
    long framesTotal = 0, alignedCorrect = 0, frameCorrect = 0, speechFrames = 0, alignedSpeechCorrect = 0, frameSpeechCorrect = 0;
    std::vector<double> boundaryErr; int utts = 0; double lpSum = 0;
    for (auto& spk : fs::directory_iterator(dir)) {
        if (!spk.is_directory()) continue;
        for (auto& f : fs::directory_iterator(spk.path())) {
            if (f.path().extension() != ".PHN" || f.path().stem().string().rfind("SA", 0) == 0) continue; // skip SA1/SA2 (identical across speakers)
            if (utts >= maxN) break;
            std::string base = (spk.path() / f.path().stem()).string();
            AudioBuffer audio; std::string err;
            if (!loadWav(base + ".WAV.wav", audio, &err)) { continue; }
            std::ifstream txt(base + ".TXT"); std::string line; std::getline(txt, line);
            std::istringstream ls(line); long a, b; ls >> a >> b; std::string transcript; std::getline(ls, transcript);
            // ground truth per frame
            FeatureTrack track = fx.extract(audio); auto post = mapper->map(track);
            const double dt = track.frameInterval(); const int T = int(std::min(track.frames.size(), post.size()));
            std::vector<Viseme> gt(size_t(T), Viseme::Silence);
            std::ifstream phn(base + ".PHN"); long s0, s1; std::string ph; std::vector<std::pair<double, Viseme>> gtSegs;
            std::vector<TranscriptWord> gtWords; TranscriptWord cur;
            while (phn >> s0 >> s1 >> ph) {
                std::string P = ph; for (auto& c : P) c = char(std::toupper((unsigned char)c));
                if (P == "H#" || P == "PAU" || P == "EPI") P = "SIL";
                Viseme v = phonemeToViseme(P);
                if (P == "SIL") { if (!cur.phones.empty()) { gtWords.push_back(cur); cur = TranscriptWord{}; } } else cur.phones.push_back(P);
                int f0 = int(s0 / double(audio.sampleRate) / dt), f1 = int(s1 / double(audio.sampleRate) / dt);
                for (int k = std::max(0, f0); k < std::min(T, f1); ++k) gt[size_t(k)] = v;
                if (gtSegs.empty() || gtSegs.back().second != v) gtSegs.push_back({s0 / double(audio.sampleRate), v});
            }
            if (!cur.phones.empty()) gtWords.push_back(cur);
            AlignmentResult r = gtPhones ? aligner.alignPhones(gtWords, track, post) : aligner.align(transcript, track, post);
            if (r.phones.empty()) continue;
            std::vector<Viseme> al(size_t(T), Viseme::Silence);
            for (auto& p : r.phones) for (int k = std::max(0, int(p.start / dt + 1e-6)); k < std::min(T, int(p.end / dt + 1e-6)); ++k) al[size_t(k)] = p.viseme;
            for (int k = 0; k < T; ++k) {
                framesTotal++; bool isSpeech = gt[size_t(k)] != Viseme::Silence; speechFrames += isSpeech;
                bool ac = al[size_t(k)] == gt[size_t(k)], fc = post[size_t(k)].dominant() == gt[size_t(k)];
                alignedCorrect += ac; frameCorrect += fc; if (isSpeech) { alignedSpeechCorrect += ac; frameSpeechCorrect += fc; }
            }
            // boundary error: for each GT viseme change, nearest aligned change of the same class pair-independent
            std::vector<double> alB; for (size_t i = 1; i < r.segments.size(); ++i) alB.push_back(r.segments[i].start);
            for (size_t i = 1; i < gtSegs.size(); ++i) { double best = 1e9; for (double x : alB) best = std::min(best, std::abs(x - gtSegs[i].first)); if (best < 1e9) boundaryErr.push_back(best); }
            lpSum += r.meanLogPosterior; utts++;
        }
    }
    if (!utts) { std::fprintf(stderr, "no utterances found\n"); return 1; }
    std::sort(boundaryErr.begin(), boundaryErr.end());
    auto pct = [&](double q) { return boundaryErr.empty() ? 0.0 : boundaryErr[size_t(q * (boundaryErr.size() - 1))] * 1000.0; };
    std::printf("utterances: %d   mapper: %s   phones: %s\n", utts, rules ? "rules" : "trained MLP", gtPhones ? "TIMIT ground truth (no times)" : "G2P from .TXT");
    std::printf("frame viseme accuracy  (all / speech only)\n");
    std::printf("  per-frame classifier : %5.1f%% / %5.1f%%\n", 100.0 * frameCorrect / framesTotal, 100.0 * frameSpeechCorrect / speechFrames);
    std::printf("  transcript alignment : %5.1f%% / %5.1f%%\n", 100.0 * alignedCorrect / framesTotal, 100.0 * alignedSpeechCorrect / speechFrames);
    std::printf("viseme boundary error vs hand labels: median %.0f ms, p75 %.0f ms, p90 %.0f ms  (%zu boundaries)\n", pct(0.5), pct(0.75), pct(0.9), boundaryErr.size());
    std::printf("mean log posterior %.3f   pronunciation dictionary: %zu words\n", lpSum / utts, pronunciationDictionarySize());
    return 0;
}

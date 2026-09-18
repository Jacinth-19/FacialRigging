// fr_eval_speaker - measures what speaker calibration buys on TIMIT TEST speakers.
// For every speaker: calibrate on SA1 + SA2 (the two sentences everyone reads - ~6 s, with their
// transcript used for the supervised last-layer fine-tune), then score per-frame viseme accuracy of
// the trained MLP on the speaker's remaining SI/SX sentences (a) unadapted, (b) statistics-adapted
// only, (c) statistics + fine-tuned last layer. Ground truth = .PHN phones -> visemes.
//   fr_eval_speaker --data data/timit/TEST [--max-speakers N] [--dr DR1]
#include "audio/ml_viseme_mapper.h"
#include "audio/speaker_profile.h"
#include "audio/phoneme_aligner.h"
#include "audio/wav_io.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
using namespace fr; namespace fs = std::filesystem;

static AudioBuffer concat(const AudioBuffer& a, const AudioBuffer& b) { AudioBuffer o = a; std::vector<float> sil(size_t(0.3 * a.sampleRate), 0.0f); o.samples.insert(o.samples.end(), sil.begin(), sil.end()); o.samples.insert(o.samples.end(), b.samples.begin(), b.samples.end()); return o; }
static std::string readTxt(const fs::path& p) { std::ifstream f(p); std::string line; std::getline(f, line); size_t sp = line.find(' '); sp = line.find(' ', sp + 1); return sp == std::string::npos ? line : line.substr(sp + 1); }

int main(int argc, char** argv) {
    std::string data = "data/timit/TEST", dr; int maxSpk = 0; float strength = 0.3f, anchor = 1.0f, lr = 5e-4f; int steps = 20; bool quiet = false;
    for (int i = 1; i < argc; ++i) { std::string a = argv[i]; auto next = [&]() { return std::string(i + 1 < argc ? argv[++i] : ""); }; if (a == "--data") data = next(); else if (a == "--max-speakers") maxSpk = std::stoi(next()); else if (a == "--dr") dr = next(); else if (a == "--strength") strength = std::stof(next()); else if (a == "--anchor") anchor = std::stof(next()); else if (a == "--lr") lr = std::stof(next()); else if (a == "--steps") steps = std::stoi(next()); else if (a == "--quiet") quiet = true; }
    MlVisemeMapper base; std::string err; if (!base.loadDefault(FR_ASSET_DIR, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    FeatureExtractor fx; AlignmentSettings as;
    long tot[3] = {0, 0, 0}, correct[3] = {0, 0, 0}; int spkN = 0, spkBetter[2] = {0, 0}; double sumGain[2] = {0, 0};
    std::vector<fs::path> spkDirs;
    for (auto& d : fs::recursive_directory_iterator(data)) if (d.is_directory() && fs::exists(d.path() / "SA1.WAV.wav") && (dr.empty() || d.path().parent_path().filename() == dr)) spkDirs.push_back(d.path());
    std::sort(spkDirs.begin(), spkDirs.end());
    for (auto& sd : spkDirs) {
        if (maxSpk && spkN >= maxSpk) break;
        AudioBuffer sa1, sa2; if (!loadWav((sd / "SA1.WAV.wav").string(), sa1, &err) || !loadWav((sd / "SA2.WAV.wav").string(), sa2, &err)) continue;
        AudioBuffer cal = concat(sa1, sa2);
        std::string text = readTxt(sd / "SA1.TXT") + " " + readTxt(sd / "SA2.TXT");
        CalibrationStats st; SpeakerProfile prof = calibrateSpeaker(cal, sd.filename().string(), 3.0f, &st);
        if (!prof.valid) { std::printf("%s: calibration failed (%s)\n", sd.filename().c_str(), st.warning.c_str()); continue; }
        prof.strength = strength; SpeakerProfile statsOnly = prof;
        // fine-tune on the aligned calibration sentences
        FeatureTrack ctr = fx.extract(cal); auto post = base.map(ctr); PhonemeAligner al(as); AlignmentResult ar = al.align(text, ctr, post);
        std::vector<int> labels(ctr.frames.size(), -1);
        for (size_t t = 0; t < ctr.frames.size(); ++t) for (auto& s : ar.segments) if (ctr.frames[t].time >= s.start && ctr.frames[t].time < s.end) { labels[t] = int(s.viseme); break; }
        float b4 = 0, af = 0; fineTuneLastLayer(base.weights(), prof, ctr, labels, steps, lr, anchor, &b4, &af);
        MlVisemeMapper mStats, mFull; mStats.loadDefault(FR_ASSET_DIR); mFull.loadDefault(FR_ASSET_DIR); mStats.applySpeaker(statsOnly); mFull.applySpeaker(prof);
        const MlVisemeMapper* ms[3] = {&base, &mStats, &mFull};
        long sTot = 0, sCor[3] = {0, 0, 0};
        for (auto& f : fs::directory_iterator(sd)) {
            std::string n = f.path().filename().string();
            if (n.size() < 8 || n.compare(n.size() - 8, 8, ".WAV.wav") != 0 || n.rfind("SA", 0) == 0) continue;
            std::string stem = n.substr(0, n.size() - 8);
            AudioBuffer a; if (!loadWav(f.path().string(), a, &err)) continue;
            std::vector<std::pair<long, int>> segs; { std::ifstream phn(sd / (stem + ".PHN")); long s0, s1; std::string ph; while (phn >> s0 >> s1 >> ph) segs.emplace_back(s1, (ph == "tcl" || ph == "dcl" || ph == "kcl" || ph == "gcl") ? -1 : int(phonemeToViseme(ph))); }
            FeatureTrack tr = fx.extract(a);
            std::vector<std::vector<VisemeFrame>> outs; for (auto* m : ms) outs.push_back(m->map(tr));
            for (size_t t = 0; t < tr.frames.size(); ++t) {
                long c = long(tr.frames[t].time * a.sampleRate); int y = -1; for (auto& s : segs) if (c < s.first) { y = s.second; break; }
                if (y < 0) continue;
                ++sTot;
                for (int k = 0; k < 3; ++k) sCor[k] += int(outs[size_t(k)][t].dominant()) == y;
            }
        }
        if (!sTot) continue;
        ++spkN; for (int k = 0; k < 3; ++k) { tot[k] += sTot; correct[k] += sCor[k]; }
        double a0 = 100.0 * sCor[0] / sTot, a1 = 100.0 * sCor[1] / sTot, a2 = 100.0 * sCor[2] / sTot;
        spkBetter[0] += a1 > a0; spkBetter[1] += a2 > a0; sumGain[0] += a1 - a0; sumGain[1] += a2 - a0;
        if (!quiet) std::printf("%-6s  pitch %3.0f Hz  cal %.1fs  base %.1f%%  +stats %.1f%% (%+.1f)  +finetune %.1f%% (%+.1f)   [cal-sentence fit %.0f%%->%.0f%%]\n", sd.filename().c_str(), prof.pitchMedianHz, st.seconds, a0, a1, a1 - a0, a2, a2 - a0, b4 * 100, af * 100);
        std::fflush(stdout);
    }
    if (!spkN) { std::fprintf(stderr, "no speakers under %s\n", data.c_str()); return 1; }
    std::printf("\n%d speakers, %ld frames\n  unadapted            %.2f%%\n  + speaker statistics %.2f%%  (better for %d/%d speakers, mean gain %+.2f)\n  + last-layer finetune %.2f%%  (better for %d/%d speakers, mean gain %+.2f)\n",
                spkN, tot[0], 100.0 * correct[0] / tot[0], 100.0 * correct[1] / tot[1], spkBetter[0], spkN, sumGain[0] / spkN, 100.0 * correct[2] / tot[2], spkBetter[1], spkN, sumGain[1] / spkN);
    return 0;
}

#include "audio/mic_check.h"
#include "audio/fft.h"
#include "audio/mic_conditioner.h"
#include "audio/wav_io.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <thread>

namespace fr {

namespace {
float db(float lin) { return 20.0f * std::log10(std::max(lin, 1e-5f)); }   // floor -100 dBFS
void say(const MicCheckOptions& o, const char* s) { if (!o.quiet) { std::printf("%s\n", s); std::fflush(stdout); } }
double clock_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

MicCheckPhase analyseMicPhase(const std::string& name, const std::vector<float>& x, int sr) {
    MicCheckPhase p; p.name = name; p.seconds = double(x.size()) / sr;
    if (x.empty()) return p;
    double sum = 0, sq = 0; float peak = 0; size_t clipped = 0;
    for (float v : x) { sum += v; sq += double(v) * v; peak = std::max(peak, std::fabs(v)); if (std::fabs(v) >= 0.99f) ++clipped; }
    p.dcOffset = float(sum / double(x.size())); p.rmsDb = db(float(std::sqrt(sq / double(x.size())))); p.peakDb = db(peak); p.clipFraction = float(clipped) / float(x.size());
    // 20 ms block RMS -> noise floor (10th percentile), VAD share against floor + 8 dB
    const size_t blk = size_t(sr / 50); std::vector<float> blocks;
    for (size_t i = 0; i + blk <= x.size(); i += blk) blocks.push_back(MicConditioner::rmsOf(&x[i], blk));
    if (!blocks.empty()) {
        std::vector<float> sorted = blocks; std::sort(sorted.begin(), sorted.end());
        float floorLin = sorted[sorted.size() / 10]; p.noiseFloorDb = db(floorLin);
        size_t sp = 0; double spSq = 0; for (float b : blocks) if (db(b) > p.noiseFloorDb + 8.0f) { ++sp; spSq += double(b) * b; }
        p.speechFraction = float(sp) / float(blocks.size());
        p.snrDb = sp ? db(float(std::sqrt(spSq / double(sp)))) - p.noiseFloorDb : 0.0f;
    }
    // spectral balance + hum: average magnitude spectrum of 4096-point Hann frames
    const size_t N = 4096; if (x.size() >= N) {
        std::vector<float> acc(N / 2 + 1, 0.0f); int frames = 0; std::vector<float> win = hannWindow(N), frame(N);
        for (size_t i = 0; i + N <= x.size(); i += N / 2) { for (size_t k = 0; k < N; ++k) frame[k] = x[i + k] * win[k]; auto m = magnitudeSpectrum(frame, N); for (size_t k = 0; k < acc.size() && k < m.size(); ++k) acc[k] += m[k] * m[k]; ++frames; }
        const float hz = float(sr) / N; auto band = [&](float lo, float hi) { double e = 0; for (size_t k = 0; k < acc.size(); ++k) { float f = k * hz; if (f >= lo && f < hi) e += acc[k]; } return db(float(std::sqrt(e / std::max(1, frames))) / float(N) * 2.0f); };
        p.lowBandDb = band(0, 300); p.midBandDb = band(300, 3400); p.highBandDb = band(3400, float(sr) / 2);
        float best = -100.0f;
        for (float base : {50.0f, 60.0f}) for (int h = 1; h <= 4; ++h) { float f = base * h; size_t k = size_t(std::lround(f / hz)); if (k + 1 >= acc.size()) continue;
            float pk = std::max({acc[k - 1], acc[k], acc[k + 1]}); double nb = 0; int cnt = 0; for (int d = -8; d <= 8; ++d) if (std::abs(d) > 2) { nb += acc[k + d]; ++cnt; }
            best = std::max(best, 10.0f * std::log10(std::max(pk, 1e-12f) / std::max(float(nb / cnt), 1e-12f))); }
        p.humDb = best;
    }
    return p;
}

MicCheckResult runMicCheck(const MicCheckOptions& opt, std::shared_ptr<VisemeMapper> mapper) {
    MicCheckResult r; std::string err;
    LiveCapture lc; if (mapper) lc.setMapper(mapper);
    lc.conditioner.settings.gate = false; lc.conditioner.settings.agc = false;   // analyse the raw microphone; the conditioner's floor tracker still runs
    r.backend = LiveCapture::backendInfo();
    if (opt.device >= -1) { for (auto& d : LiveCapture::listInputDevices()) if (d.index == opt.device || (opt.device == -1 && r.deviceName.empty())) r.deviceName = d.name; }
    if (opt.device == LiveCapture::kTestSignalDevice) r.deviceName = "built-in test signal (synthetic speech; silence phase simulated at -60 dBFS)";
    if (!lc.start(opt.device, opt.sampleRate, &err)) { r.error = "capture failed: " + err; return r; }
    r.sampleRate = lc.sampleRate();
    std::vector<float> recording;
    struct Onset { double t; }; std::vector<double> clapDelays;
    auto phase = [&](const char* name, double secs, const char* prompt, bool wantVisemes, bool clapProbe) {
        if (secs <= 0) return;
        say(opt, prompt);
        double t0 = clock_s(); MicCheckPhase ph; std::map<std::string, int> hist; uint64_t seenSamples = 0; double lastLoudT = -1; float prevLevel = 0;
        std::vector<float> phaseAudio;
        while (clock_s() - t0 < secs) {
            auto f = lc.poll(); ++ph.polls;
            if (f.valid) { ++ph.framesWithVisemes; if (f.viseme.dominant() != Viseme::Silence) { ++ph.speakingFrames; if (wantVisemes) ++hist[visemeName(f.viseme.dominant())]; } }
            if (clapProbe) {
                // an onset = level jumps by >20 dB within one poll; the delay is measured from the sample
                // timestamp of the newest audio (where the clap physically sits) to the poll wall-clock
                float lvl = lc.inputLevel();
                if (db(lvl) > db(prevLevel) + 20.0f && db(lvl) > -30.0f && clock_s() - lastLoudT > 0.5) { lastLoudT = clock_s(); clapDelays.push_back(lc.latencyReport().measuredAgeMs); }
                prevLevel = lvl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::vector<float> chunk = lc.recent(secs); (void)seenSamples;
        if (opt.device == LiveCapture::kTestSignalDevice && std::string(name) == "silence") {
            // the synthetic source cannot keep quiet: simulate a -60 dBFS room for the noise-floor phase
            uint32_t rng = 99u; for (auto& v : chunk) { rng = rng * 1664525u + 1013904223u; v = (float(rng >> 8) / 16777216.0f - 0.5f) * 2.0f * 0.001f * 1.732f; }
        }
        recording.insert(recording.end(), chunk.begin(), chunk.end());
        MicCheckPhase a = analyseMicPhase(name, chunk, r.sampleRate);
        a.polls = ph.polls; a.framesWithVisemes = ph.framesWithVisemes; a.speakingFrames = ph.speakingFrames;
        for (auto& kv : hist) a.visemeHistogram.push_back(kv.first + ": " + std::to_string(kv.second));
        r.phases.push_back(a);
    };
    phase("silence", opt.silenceSeconds, "\n[1/3] Stay SILENT for a few seconds - measuring the noise floor...", false, false);
    phase("speech", opt.speechSeconds, "\n[2/3] READ ALOUD at normal volume until told to stop:\n      \"The quick brown fox jumps over the lazy dog. She sells sea shells by the sea shore.\"", true, false);
    phase("latency", opt.latencySeconds, "\n[3/3] CLAP or tap the desk 3 times, about a second apart - measuring latency...", false, true);
    r.latency = lc.latencyReport(); r.agcGainDb = lc.micStatus().agcGainDb;
    lc.stop();
    r.clapsHeard = int(clapDelays.size());
    if (!clapDelays.empty()) { std::sort(clapDelays.begin(), clapDelays.end()); r.measuredLatencyMs = float(clapDelays[clapDelays.size() / 2]) + r.latency.deviceMs; }
    // verdicts
    auto add = [&](const char* level, const std::string& s) { r.verdicts.push_back(std::string(level) + ": " + s); if (level[0] == 'W') ++r.warnings; if (level[0] == 'F') ++r.failures; };
    const MicCheckPhase* sil = nullptr; const MicCheckPhase* sp = nullptr;
    for (auto& p : r.phases) { if (p.name == "silence") sil = &p; if (p.name == "speech") sp = &p; }
    char b[256];
    if (sil) {
        std::snprintf(b, sizeof b, "noise floor %.1f dBFS", sil->rmsDb);
        if (sil->rmsDb > -35) add("FAIL", std::string(b) + " - very noisy or the mic is picking up playback; the VAD gate will chatter"); else if (sil->rmsDb > -50) add("WARN", std::string(b) + " - noisy room / high gain; raise the gate threshold (Mic panel) or move closer"); else add("OK", b);
        if (sil->humDb > 15) { std::snprintf(b, sizeof b, "mains hum %.0f dB above neighbouring bins (50/60 Hz) - ground loop / unshielded cable", sil->humDb); add("WARN", b); }
        if (std::fabs(sil->dcOffset) > 0.01f) { std::snprintf(b, sizeof b, "DC offset %.3f - the 80 Hz high-pass removes it, keep it on", sil->dcOffset); add("WARN", b); }
        if (sil->rmsDb <= -95) add("FAIL", "silence phase is digital zero - wrong device selected or the mic is muted in the OS mixer");
    }
    if (sp) {
        std::snprintf(b, sizeof b, "speech level %.1f dBFS (peak %.1f), SNR %.1f dB, %d%% of the time judged speech", sp->rmsDb, sp->peakDb, sp->snrDb, int(sp->speechFraction * 100));
        if (sp->speechFraction < 0.1f || sp->snrDb < 6) add("FAIL", std::string(b) + " - nothing intelligible captured (gain too low, muted, or wrong device)");
        else if (sp->snrDb < 15) add("WARN", std::string(b) + " - low SNR; visemes will flicker, AGC + gate should stay on");
        else add("OK", b);
        if (sp->clipFraction > 0.001f) { std::snprintf(b, sizeof b, "%.2f%% clipped samples - lower the OS input gain", sp->clipFraction * 100); add("WARN", b); }
        else if (sp->rmsDb < -40) add("WARN", "speech is quiet (< -40 dBFS RMS) - AGC will add up to +24 dB; consider raising the OS gain");
        if (sp->highBandDb < sp->midBandDb - 35) add("WARN", "almost no energy above 3.4 kHz - low-bandwidth device (Bluetooth HFP?) - fricatives (F/V, S) will be weak");
        if (sp->lowBandDb > sp->midBandDb + 6) add("WARN", "boomy low band (proximity effect / desk vibration) - the high-pass helps, or back off the mic");
        if (mapper && sp->framesWithVisemes > 0 && sp->visemeHistogram.size() < 3) add("WARN", "fewer than 3 distinct visemes while speaking - mapper is not tracking this voice; try --calibrate-speaker");
    }
    if (r.latency.overruns > 0) { std::snprintf(b, sizeof b, "%d input overruns - raise the block size (--live-block) or use latency preset 1", r.latency.overruns); add("WARN", b); }
    std::snprintf(b, sizeof b, "estimated pipeline latency %.0f ms (device %.0f + block %.0f + window %.0f + hop %.0f + smoothing %.0f)", r.latency.estimatedMs, r.latency.deviceMs, r.latency.blockMs, r.latency.windowMs, r.latency.hopMs, r.latency.smoothingMs);
    add(r.latency.estimatedMs > 150 ? "WARN" : "OK", b);
    if (opt.latencySeconds > 0) { if (r.clapsHeard == 0) add("WARN", "no clap onsets detected in the latency phase"); else { std::snprintf(b, sizeof b, "measured onset latency %.0f ms over %d claps", r.measuredLatencyMs, r.clapsHeard); add(r.measuredLatencyMs > 150 ? "WARN" : "OK", b); } }
    r.ok = r.failures == 0;
    if (opt.saveRecording || !opt.reportPath.empty()) { std::string e; writeMicReport(r, opt, recording, &e); if (!e.empty()) r.error = e; }
    return r;
}

std::string MicCheckResult::markdown() const {
    std::string s = "# FacialRigging microphone self-test\n\n";
    char b[512];
    std::snprintf(b, sizeof b, "- Device: **%s** @ %d Hz\n- Backend: %s\n- Result: **%s** (%d warnings, %d failures)\n\n", deviceName.c_str(), sampleRate, backend.c_str(), error.empty() ? (ok ? "PASS" : "FAIL") : error.c_str(), warnings, failures); s += b;
    s += "## Findings\n\n"; for (auto& v : verdicts) s += "- " + v + "\n";
    s += "\n## Phases\n\n| phase | s | RMS dBFS | peak | floor | SNR | speech % | clip % | DC | low/mid/high dB | hum dB | polls | viseme frames |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (auto& p : phases) { std::snprintf(b, sizeof b, "| %s | %.1f | %.1f | %.1f | %.1f | %.1f | %d | %.2f | %.3f | %.0f / %.0f / %.0f | %.0f | %d | %d (%d speaking) |\n", p.name.c_str(), p.seconds, p.rmsDb, p.peakDb, p.noiseFloorDb, p.snrDb, int(p.speechFraction * 100), p.clipFraction * 100, p.dcOffset, p.lowBandDb, p.midBandDb, p.highBandDb, p.humDb, p.polls, p.framesWithVisemes, p.speakingFrames); s += b; }
    for (auto& p : phases) if (!p.visemeHistogram.empty()) { s += "\nVisemes while speaking (" + p.name + "): "; for (size_t i = 0; i < p.visemeHistogram.size(); ++i) s += (i ? ", " : "") + p.visemeHistogram[i]; s += "\n"; }
    std::snprintf(b, sizeof b, "\n## Latency\n\n- estimated: %.0f ms (device %.1f, block %.1f, window %.1f, hop %.1f, smoothing %.1f)\n- measured ring age at poll: %.1f ms, callback interval %.1f ms, overruns %d\n- clap probe: %s\n", latency.estimatedMs, latency.deviceMs, latency.blockMs, latency.windowMs, latency.hopMs, latency.smoothingMs, latency.measuredAgeMs, latency.callbackIntervalMs, latency.overruns, clapsHeard ? (std::to_string(int(measuredLatencyMs)) + " ms median over " + std::to_string(clapsHeard) + " claps").c_str() : "not measured"); s += b;
    s += "\nPlease send this file (and the .json / .wav next to it) back with a note on OS, mic model and whether it is USB / 3.5 mm / Bluetooth.\n";
    return s;
}

std::string MicCheckResult::json() const {
    std::string s = "{\n"; char b[512];
    auto esc = [](const std::string& v) { std::string o; for (char c : v) { if (c == '"' || c == '\\') o += '\\'; if (c == '\n') { o += "\\n"; continue; } o += c; } return o; };
    std::snprintf(b, sizeof b, "  \"ok\": %s, \"device\": \"%s\", \"sampleRate\": %d, \"warnings\": %d, \"failures\": %d, \"error\": \"%s\",\n", ok ? "true" : "false", esc(deviceName).c_str(), sampleRate, warnings, failures, esc(error).c_str()); s += b;
    std::snprintf(b, sizeof b, "  \"latency\": {\"estimatedMs\": %.1f, \"deviceMs\": %.1f, \"blockMs\": %.1f, \"windowMs\": %.1f, \"hopMs\": %.1f, \"smoothingMs\": %.1f, \"measuredAgeMs\": %.1f, \"callbackIntervalMs\": %.1f, \"overruns\": %d, \"clapMs\": %.1f, \"claps\": %d},\n", latency.estimatedMs, latency.deviceMs, latency.blockMs, latency.windowMs, latency.hopMs, latency.smoothingMs, latency.measuredAgeMs, latency.callbackIntervalMs, latency.overruns, measuredLatencyMs, clapsHeard); s += b;
    s += "  \"phases\": [\n";
    for (size_t i = 0; i < phases.size(); ++i) { const auto& p = phases[i]; std::snprintf(b, sizeof b, "    {\"name\": \"%s\", \"seconds\": %.2f, \"rmsDb\": %.2f, \"peakDb\": %.2f, \"noiseFloorDb\": %.2f, \"snrDb\": %.2f, \"speechFraction\": %.3f, \"clipFraction\": %.5f, \"dcOffset\": %.4f, \"lowDb\": %.1f, \"midDb\": %.1f, \"highDb\": %.1f, \"humDb\": %.1f, \"polls\": %d, \"visemeFrames\": %d, \"speakingFrames\": %d}%s\n", p.name.c_str(), p.seconds, p.rmsDb, p.peakDb, p.noiseFloorDb, p.snrDb, p.speechFraction, p.clipFraction, p.dcOffset, p.lowBandDb, p.midBandDb, p.highBandDb, p.humDb, p.polls, p.framesWithVisemes, p.speakingFrames, i + 1 < phases.size() ? "," : ""); s += b; }
    s += "  ],\n  \"verdicts\": [";
    for (size_t i = 0; i < verdicts.size(); ++i) s += (i ? ", \"" : "\"") + esc(verdicts[i]) + "\"";
    s += "]\n}\n"; return s;
}

std::string writeMicReport(const MicCheckResult& r, const MicCheckOptions& opt, const std::vector<float>& rec, std::string* error) {
    std::string md = opt.reportPath.empty() ? "mic_report.md" : opt.reportPath;
    std::string stem = md; if (stem.size() > 3 && stem.compare(stem.size() - 3, 3, ".md") == 0) stem.resize(stem.size() - 3);
    { std::ofstream f(md); if (!f) { if (error) *error = "cannot write " + md; return ""; } f << r.markdown(); }
    { std::ofstream f(stem + ".json"); f << r.json(); }
    if (opt.saveRecording && !rec.empty()) { AudioBuffer a; a.sampleRate = r.sampleRate; a.channels = 1; a.samples = rec; std::string e; saveWav(stem + ".wav", a, &e); }
    return md;
}

} // namespace fr

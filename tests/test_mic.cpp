#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "audio/mic_conditioner.h"
#include "audio/live_capture.h"
#include <chrono>
#include <cmath>
#include <thread>
using namespace fr;

namespace {
std::vector<float> tone(float amp, float sec, int sr = 16000, float hz = 220.0f) { std::vector<float> v(size_t(sec * sr)); for (size_t i = 0; i < v.size(); ++i) v[i] = amp * std::sin(6.2831853f * hz * float(i) / sr); return v; }
std::vector<float> noise(float amp, float sec, int sr = 16000) { std::vector<float> v(size_t(sec * sr)); uint32_t r = 1; for (auto& x : v) { r = r * 1664525u + 1013904223u; x = (float(r >> 8) / 16777216.0f - 0.5f) * 2.0f * amp; } return v; }
float rms(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += double(v[i]) * v[i]; return float(std::sqrt(s / double(b - a))); }
void run(MicConditioner& c, std::vector<float>& v, size_t block = 256) { for (size_t i = 0; i < v.size(); i += block) c.process(v.data() + i, std::min(block, v.size() - i)); }
}

TEST_CASE("noise gate mutes background noise and opens for speech-level signal", "[mic]") {
    MicConditioner c; c.reset(16000); c.settings.agc = false;
    // 2 s of quiet noise (-50 dBFS), then 1 s tone at -20 dBFS, then 1 s noise again
    auto a = noise(0.003f, 2.0f), b = tone(0.1f, 1.0f), d = noise(0.003f, 1.0f);
    std::vector<float> v; v.insert(v.end(), a.begin(), a.end()); v.insert(v.end(), b.begin(), b.end()); v.insert(v.end(), d.begin(), d.end());
    run(c, v);
    const size_t sr = 16000;
    CHECK(rms(v, sr, 2 * sr) < 0.0005f);                       // noise muted (after the floor settled)
    CHECK(rms(v, size_t(2.2f * sr), size_t(2.9f * sr)) > 0.05f); // tone passes
    CHECK(rms(v, size_t(3.5f * sr), 4 * sr) < 0.0005f);        // closes again after hang time
    CHECK(c.status().noiseFloorDb < -40.0f);
}

TEST_CASE("gate hang time bridges short pauses", "[mic]") {
    MicConditioner c; c.reset(16000); c.settings.agc = false; c.settings.gateHangMs = 200.0f;
    auto q = noise(0.002f, 1.5f); run(c, q);
    auto t = tone(0.1f, 0.3f); run(c, t);
    REQUIRE(c.status().speech);
    std::vector<float> gap(size_t(0.1f * 16000), 0.0f); run(c, gap);
    CHECK(c.status().speech);        // 100 ms pause < 200 ms hang
    std::vector<float> longGap(size_t(0.5f * 16000), 0.0f); run(c, longGap);
    CHECK_FALSE(c.status().speech);
}

TEST_CASE("AGC brings a quiet voice up and a loud voice down toward the target", "[mic]") {
    for (float amp : {0.01f, 0.5f}) {
        MicConditioner c; c.reset(16000); c.settings.gate = false; c.settings.agcTargetDb = -18.0f;
        auto q = noise(0.0005f, 1.0f); run(c, q);
        auto t = tone(amp, 4.0f); run(c, t);
        float outDb = MicConditioner::dbOf(rms(t, size_t(3.0f * 16000), size_t(4.0f * 16000)));
        INFO("amp " << amp << " -> " << outDb << " dB, gain " << c.status().agcGainDb);
        CHECK(std::abs(outDb - (-18.0f)) < 3.0f);
        if (amp < 0.1f) CHECK(c.status().agcGainDb > 6.0f); else CHECK(c.status().agcGainDb < -3.0f);
    }
}

TEST_CASE("high-pass removes DC offset", "[mic]") {
    MicConditioner c; c.reset(16000); c.settings.gate = false; c.settings.agc = false;
    std::vector<float> v(16000, 0.3f); run(c, v);
    float mean = 0; for (size_t i = 8000; i < 16000; ++i) mean += v[i]; mean /= 8000;
    CHECK(std::abs(mean) < 0.01f);
}

TEST_CASE("live capture reports a latency budget and the gate suppresses the test-signal gaps", "[mic][live]") {
    LiveCapture lc; std::string err;
    lc.latency.blockFrames = 128; lc.latency.smoothing = 0.3f; lc.testNoiseDb = -45.0f; lc.testGapSeconds = 1.0f;
    REQUIRE(lc.start(LiveCapture::kTestSignalDevice, 16000, &err));
    int polls = 0, speechPolls = 0, silentGated = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 4.5) {
        auto f = lc.poll();
        if (f.valid) { ++polls; MicStatus s = lc.micStatus(); if (s.speech) ++speechPolls; else if (f.viseme.dominant() == Viseme::Silence) ++silentGated; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto rep = lc.latencyReport();
    lc.stop();
    INFO("polls " << polls << " speech " << speechPolls << " gated-silent " << silentGated << " est " << rep.estimatedMs << " ms, cb " << rep.callbackIntervalMs << " ms");
    CHECK(polls > 40);
    CHECK(speechPolls > 10);
    CHECK(silentGated > 5);                       // the 1 s gaps come out as Silence, not noise-driven mouth flutter
    CHECK(rep.blockMs == Catch::Approx(8.0f));    // 128 / 16 kHz
    CHECK(rep.estimatedMs > rep.blockMs);
    CHECK(rep.estimatedMs < 200.0f);
    CHECK(rep.callbackIntervalMs > 4.0f); CHECK(rep.callbackIntervalMs < 40.0f);
    CHECK(rep.overruns == 0);
}

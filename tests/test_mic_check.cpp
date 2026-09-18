#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <fstream>
#include <vector>
using Catch::Approx;
#include "audio/mic_check.h"
#include "audio/live_capture.h"
using namespace fr;
TEST_CASE("mic self-test analysis measures floor, SNR, clipping, hum and bandwidth", "[audio][mic]") {
    const int sr = 16000; std::vector<float> x(sr * 3, 0.0f);
    uint32_t rng = 1; auto noise = [&]() { rng = rng * 1664525u + 1013904223u; return (float(rng >> 8) / 16777216.0f - 0.5f) * 2.0f; };
    for (size_t i = 0; i < x.size(); ++i) x[i] = 0.003f * noise() + 0.05f * std::sin(2.0f * 3.14159f * 60.0f * float(i) / sr) + 0.02f;   // -50 dB noise + strong 60 Hz hum + DC
    for (size_t i = sr; i < size_t(sr * 2); ++i) x[i] += 0.5f * std::sin(2.0f * 3.14159f * 220.0f * float(i) / sr) * (0.5f + 0.5f * std::sin(2.0f * 3.14159f * 4.0f * float(i) / sr));   // 1 s "speech" burst
    for (size_t i = 0; i < 100; ++i) x[sr + i] = 1.0f;   // clipped run
    MicCheckPhase p = analyseMicPhase("t", x, sr);
    REQUIRE(p.seconds == Approx(3.0));
    REQUIRE(p.dcOffset == Approx(0.02f).margin(0.003f));
    REQUIRE(p.clipFraction > 0.0f);
    REQUIRE(p.speechFraction > 0.2f); REQUIRE(p.speechFraction < 0.5f);
    REQUIRE(p.snrDb > 10.0f);
    REQUIRE(p.humDb > 10.0f);
    REQUIRE(p.lowBandDb > p.highBandDb);   // hum + 220 Hz vs nothing above 3.4 kHz but noise
    MicCheckPhase z = analyseMicPhase("zero", std::vector<float>(sr, 0.0f), sr);
    REQUIRE(z.rmsDb <= -95.0f);
}

TEST_CASE("mic self-test runs end to end on the built-in test signal and writes a report", "[audio][mic][slow]") {
    MicCheckOptions o; o.device = LiveCapture::kTestSignalDevice; o.silenceSeconds = 1.0; o.speechSeconds = 2.0; o.latencySeconds = 1.0; o.quiet = true;
    o.reportPath = "/tmp/fr_mic_test_report.md";
    MicCheckResult r = runMicCheck(o, nullptr);
    REQUIRE(r.error.empty());
    REQUIRE(r.phases.size() == 3);
    REQUIRE(r.phases[0].rmsDb < -50.0f);          // simulated quiet room
    REQUIRE(r.phases[1].speechFraction > 0.2f);   // synthetic speech was heard
    REQUIRE(r.ok);
    REQUIRE(r.markdown().find("PASS") != std::string::npos);
    REQUIRE(r.json().find("\"phases\"") != std::string::npos);
    std::ifstream f("/tmp/fr_mic_test_report.json"); REQUIRE(bool(f));
}

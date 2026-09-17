#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "audio/fft.h"
#include "audio/features.h"
#include "audio/viseme_mapper.h"
#include "audio/wav_io.h"
#include <algorithm>
#include <cmath>
using namespace fr;
using Catch::Approx;

TEST_CASE("FFT of a pure tone peaks at the right bin") {
    const size_t N = 1024; const int sr = 8000; const float f0 = 1000.0f;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i) x[i] = std::sin(2 * M_PI * f0 * i / sr);
    auto mag = magnitudeSpectrum(x);
    size_t peak = std::max_element(mag.begin(), mag.end()) - mag.begin();
    CHECK(peak == size_t(std::lround(f0 * N / sr)));
    // Parseval-ish sanity: forward+inverse restores input
    std::vector<std::complex<float>> c(x.begin(), x.end());
    fft(c); fft(c, true);
    for (size_t i = 0; i < N; ++i) CHECK(c[i].real() == Approx(x[i]).margin(1e-4));
}

TEST_CASE("WAV round trip preserves samples (16-bit)") {
    AudioBuffer a; a.sampleRate = 16000; a.channels = 2;
    for (int i = 0; i < 1000; ++i) { a.samples.push_back(std::sin(i * 0.05f)); a.samples.push_back(std::cos(i * 0.05f)); }
    std::string err;
    REQUIRE(saveWav("/tmp/fr_test.wav", a, &err));
    AudioBuffer b;
    REQUIRE(loadWav("/tmp/fr_test.wav", b, &err));
    REQUIRE(b.channels == 2); REQUIRE(b.sampleRate == 16000); REQUIRE(b.frames() == 1000);
    for (size_t i = 0; i < a.samples.size(); ++i) CHECK(b.samples[i] == Approx(a.samples[i]).margin(1e-3));
    CHECK(b.mono().size() == 1000);
}

TEST_CASE("YIN pitch estimate on a harmonic signal") {
    const int sr = 22050; const size_t N = 2048; const float f0 = 180.0f;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i) { float t = float(i) / sr; x[i] = std::sin(2 * M_PI * f0 * t) + 0.5f * std::sin(4 * M_PI * f0 * t) + 0.25f * std::sin(6 * M_PI * f0 * t); }
    float conf = 0;
    float p = FeatureExtractor::pitchYin(x.data(), N, sr, 60, 400, &conf);
    CHECK(p == Approx(f0).epsilon(0.02));
    CHECK(conf > 0.8f);
    std::vector<float> noise(N); unsigned s = 1; for (auto& v : noise) { s = s * 1664525u + 1013904223u; v = (s >> 9) / float(1u << 23) - 1.0f; }
    FeatureExtractor::pitchYin(noise.data(), N, sr, 60, 400, &conf);
    CHECK(conf < 0.6f);
}

TEST_CASE("MFCC of a low tone vs bright noise differ in expected direction") {
    FeatureExtractor fx;
    const int sr = 16000; const size_t N = 1024;
    std::vector<float> low(N), bright(N);
    unsigned s = 3;
    for (size_t i = 0; i < N; ++i) { low[i] = std::sin(2 * M_PI * 150.0f * i / sr); s = s * 1664525u + 1013904223u; bright[i] = (s >> 9) / float(1u << 23) - 1.0f; }
    auto win = hannWindow(N);
    for (size_t i = 0; i < N; ++i) { low[i] *= win[i]; bright[i] *= win[i]; }
    auto cLow = fx.mfcc(magnitudeSpectrum(low), sr), cBright = fx.mfcc(magnitudeSpectrum(bright), sr);
    REQUIRE(cLow.size() == 13);
    CHECK(cLow[1] > cBright[1]); // c1 tracks spectral tilt: positive for low-passed spectra
    CHECK(FeatureExtractor::hzToMel(1000.0f) == Approx(1000.0f).epsilon(0.001));
    CHECK(FeatureExtractor::melToHz(FeatureExtractor::hzToMel(440.0f)) == Approx(440.0f).epsilon(1e-3));
}

TEST_CASE("feature track over synthetic speech has sensible dynamics") {
    AudioBuffer a = synthesizeTestSpeech(2.0, 16000);
    FeatureTrack t = FeatureExtractor().extract(a);
    REQUIRE(t.frames.size() > 40);
    float maxLoud = 0; int voiced = 0, silent = 0, onsets = 0;
    for (auto& f : t.frames) { maxLoud = std::max(maxLoud, f.loudness); voiced += f.voicing > 0.5f; silent += f.loudness < 0.05f; onsets += f.onset; REQUIRE(f.mfcc.size() == 13); }
    CHECK(maxLoud == Approx(1.0f));
    CHECK(voiced > 10);
    CHECK(silent > 5);
    CHECK(onsets >= 2);
    CHECK(t.at(0.0)->time < 0.1);
    CHECK(t.at(100.0) == &t.frames.back());
}

TEST_CASE("viseme mapper: silence -> Silence, weights sum to one, dictionary works") {
    AudioBuffer a; a.sampleRate = 16000; a.samples.assign(16000, 0.0f);
    for (int i = 4000; i < 12000; ++i) a.samples[i] = 0.5f * std::sin(2 * M_PI * 200.0f * i / 16000.0f) + 0.2f * std::sin(2 * M_PI * 1600.0f * i / 16000.0f);
    auto vis = VisemeMapper().map(FeatureExtractor().extract(a));
    REQUIRE(vis.size() > 10);
    CHECK(vis.front().dominant() == Viseme::Silence);
    bool anyMouth = false;
    for (auto& v : vis) { float s = 0; for (float w : v.weights) s += w; CHECK(s == Approx(1.0f).margin(1e-3)); if (v.dominant() != Viseme::Silence) anyMouth = true; }
    CHECK(anyMouth);
    CHECK(phonemeToViseme("AA1") == Viseme::AA);
    CHECK(phonemeToViseme("m") == Viseme::MBP);
    CHECK(phonemeToViseme("F") == Viseme::FV);
    CHECK(phonemeToViseme("sil") == Viseme::Silence);
}

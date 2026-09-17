#include "audio/fft.h"
#include <cmath>

namespace fr {

size_t nextPow2(size_t n) { size_t p = 1; while (p < n) p <<= 1; return p; }

void fft(std::vector<std::complex<float>>& a, bool inverse) {
    size_t n = a.size();
    if (n < 2) return;
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = 2.0f * float(M_PI) / float(len) * (inverse ? 1.0f : -1.0f);
        std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= float(n);
}

std::vector<float> magnitudeSpectrum(const std::vector<float>& frame, size_t fftSize) {
    size_t n = fftSize ? fftSize : nextPow2(frame.size());
    std::vector<std::complex<float>> buf(n, 0.0f);
    for (size_t i = 0; i < frame.size() && i < n; ++i) buf[i] = frame[i];
    fft(buf);
    std::vector<float> mag(n / 2 + 1);
    for (size_t i = 0; i < mag.size(); ++i) mag[i] = std::abs(buf[i]);
    return mag;
}

std::vector<float> hannWindow(size_t n) {
    std::vector<float> w(n);
    for (size_t i = 0; i < n; ++i) w[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / float(n > 1 ? n - 1 : 1));
    return w;
}
std::vector<float> hammingWindow(size_t n) {
    std::vector<float> w(n);
    for (size_t i = 0; i < n; ++i) w[i] = 0.54f - 0.46f * std::cos(2.0f * float(M_PI) * i / float(n > 1 ? n - 1 : 1));
    return w;
}

} // namespace fr

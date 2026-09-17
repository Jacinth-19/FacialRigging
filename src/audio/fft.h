#pragma once
#include <complex>
#include <vector>

namespace fr {

/// In-place iterative radix-2 FFT. `data.size()` must be a power of two.
void fft(std::vector<std::complex<float>>& data, bool inverse = false);
/// Magnitude spectrum (N/2+1 bins) of a real frame, zero-padded to the next power of two.
std::vector<float> magnitudeSpectrum(const std::vector<float>& frame, size_t fftSize = 0);
std::vector<float> hannWindow(size_t n);
std::vector<float> hammingWindow(size_t n);
size_t nextPow2(size_t n);

} // namespace fr

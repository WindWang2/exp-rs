// fft2d.h — F13 deterministic 2D FFT primitives for phase correlation.
//
// Power-of-two complex FFT only; callers pad real images to the next power
// of two per axis. All arithmetic is plain double arithmetic evaluated in a
// fixed order (iterative Cooley-Tukey), so results are bit-reproducible for
// identical inputs on a given platform. No third-party FFT dependency.
//
// Convention (matches the phase-correlation literature used by
// multimodal_matcher):
//   forward:  F(u) = Σ x(n) · exp(-2πi·un/N)
//   inverse:  un-normalized (divide by N·N yourself, or use
//             normalizeInverse2d).
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace sicnu::registration {

/// True when n >= 1 and n is an exact power of two.
[[nodiscard]] bool isPowerOfTwo(std::size_t n) noexcept;

/// Smallest power of two >= n (n >= 1). Returns 0 on overflow.
[[nodiscard]] std::size_t nextPowerOfTwo(std::size_t n) noexcept;

/// In-place iterative radix-2 FFT over one dimension. a.size() must be a
/// power of two; inverse applies conjugate twiddles without normalization.
void fft1d(std::vector<std::complex<double>>& a, bool inverse);

/// Row-major 2D FFT (rows x cols), both power of two. In-place on the
/// interleaved row-major buffer.
void fft2d(std::vector<std::complex<double>>& a, std::size_t rows, std::size_t cols,
           bool inverse);

/// Scales every element by 1/(rows*cols) — the missing 2D inverse
/// normalization.
void normalizeInverse2d(std::vector<std::complex<double>>& a, std::size_t rows,
                        std::size_t cols);

} // namespace sicnu::registration

// multimodal_matcher.cpp — F13 Packages A + B implementation.
//
// Determinism: fixed iteration order everywhere; the only randomness is
// inside rs::algorithms' RANSAC which is seeded (mt19937 seed 42).
// NaN semantics: NaN pixels are NoData. Windows with valid fraction below
// MultimodalMatchOptions::minValidFraction are skipped; otherwise NaN
// samples are replaced by the window's valid mean before FFT/MI/NCC so
// spectral leakage stays bounded (documented approximation, see docs).
#include "multimodal_matcher.h"

#include "algorithms/feature_matcher.h"
#include "fft2d.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace sicnu::registration {

namespace {

constexpr double kEps = 1e-12;
// M_PI is not portable (MSVC); repo convention is a local constant.
constexpr double kPi = 3.14159265358979323846;
// Peak-to-median surface ratio that maps to a per-point score of 1.0.
constexpr double kSnrScoreNorm = 20.0;

struct FloatLevel {
    std::vector<float> data;
    int width{0};
    int height{0};
};

double medianOf(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid),
                     values.end());
    return values[mid];
}

/// Separable [1,2,1]/4 half-scale downsample. NaN-aware: weights of invalid
/// neighbors are dropped and the result renormalized; all-invalid -> NaN.
FloatLevel downsampleLevel(const FloatLevel& up)
{
    FloatLevel down;
    down.width = std::max(1, up.width / 2);
    down.height = std::max(1, up.height / 2);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    down.data.assign(static_cast<std::size_t>(down.width) * down.height, nan);
    // Vertical smoothing into a temp, then horizontal decimation.
    std::vector<float> tmp(static_cast<std::size_t>(up.width) * down.height, nan);
    for (int y = 0; y < down.height; ++y) {
        for (int x = 0; x < up.width; ++x) {
            double acc = 0.0;
            double wsum = 0.0;
            for (int k = -1; k <= 1; ++k) {
                const int sy = 2 * y + k;
                if (sy < 0 || sy >= up.height)
                    continue;
                const float v = up.data[static_cast<std::size_t>(sy) * up.width + x];
                if (std::isnan(v))
                    continue;
                acc += (k == 0 ? 2.0 : 1.0) * static_cast<double>(v);
                wsum += (k == 0 ? 2.0 : 1.0);
            }
            tmp[static_cast<std::size_t>(y) * up.width + x] =
                wsum > 0.0 ? static_cast<float>(acc / wsum) : nan;
        }
    }
    for (int y = 0; y < down.height; ++y) {
        for (int x = 0; x < down.width; ++x) {
            double acc = 0.0;
            double wsum = 0.0;
            for (int k = -1; k <= 1; ++k) {
                const int sx = 2 * x + k;
                if (sx < 0 || sx >= up.width)
                    continue;
                const float v = tmp[static_cast<std::size_t>(y) * up.width + sx];
                if (std::isnan(v))
                    continue;
                acc += (k == 0 ? 2.0 : 1.0) * static_cast<double>(v);
                wsum += (k == 0 ? 2.0 : 1.0);
            }
            down.data[static_cast<std::size_t>(y) * down.width + x] =
                wsum > 0.0 ? static_cast<float>(acc / wsum) : nan;
        }
    }
    return down;
}

struct Window {
    std::vector<float> data; // side*side, NaN-free (filled with valid mean)
    int side{0};
    double validFraction{0.0};
    double variance{0.0};
    bool ok{false};
};

Window extractWindow(const FloatLevel& img, int cx, int cy, int side)
{
    Window win;
    win.side = side;
    const int half = side / 2;
    if (side < 4 || cx - half < 0 || cy - half < 0 || cx - half + side > img.width
        || cy - half + side > img.height)
        return win;
    const std::size_t n = static_cast<std::size_t>(side) * side;
    win.data.resize(n);
    double sum = 0.0;
    std::size_t valid = 0;
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const float v =
                img.data[static_cast<std::size_t>(cy - half + y) * img.width + (cx - half + x)];
            win.data[static_cast<std::size_t>(y) * side + x] = v;
            if (!std::isnan(v)) {
                sum += v;
                ++valid;
            }
        }
    }
    win.validFraction = static_cast<double>(valid) / static_cast<double>(n);
    if (valid == 0)
        return win;
    const float mean = static_cast<float>(sum / static_cast<double>(valid));
    double varAcc = 0.0;
    for (auto& v : win.data) {
        if (std::isnan(v))
            v = mean;
        varAcc += (static_cast<double>(v) - mean) * (static_cast<double>(v) - mean);
    }
    win.variance = varAcc / static_cast<double>(n);
    win.ok = true;
    return win;
}

struct PhaseResult {
    double dx{0.0};
    double dy{0.0};
    double snr{0.0};
    bool ok{false};
};

/// Windowed phase correlation. Returns d such that winDst(x) ≈ winSrc(x - d):
/// the src window content re-appears at dst position c_dst + d.
PhaseResult phaseCorrelate(const Window& srcWin, const Window& dstWin)
{
    PhaseResult res;
    const std::size_t side = nextPowerOfTwo(static_cast<std::size_t>(srcWin.side));
    if (side < 4)
        return res;
    // Periodic Hann window (deterministic).
    std::vector<double> hann(static_cast<std::size_t>(srcWin.side));
    for (int i = 0; i < srcWin.side; ++i) {
        hann[static_cast<std::size_t>(i)] =
            0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i)
                                 / static_cast<double>(srcWin.side));
    }

    std::vector<std::complex<double>> fa(side * side, {0.0, 0.0});
    std::vector<std::complex<double>> fb(side * side, {0.0, 0.0});
    for (int y = 0; y < srcWin.side; ++y) {
        for (int x = 0; x < srcWin.side; ++x) {
            const double w = hann[static_cast<std::size_t>(x)] * hann[static_cast<std::size_t>(y)];
            const std::size_t idx = static_cast<std::size_t>(y) * side + x;
            fa[idx] = {w * srcWin.data[static_cast<std::size_t>(y) * srcWin.side + x], 0.0};
            fb[idx] = {w * dstWin.data[static_cast<std::size_t>(y) * srcWin.side + x], 0.0};
        }
    }
    fft2d(fa, side, side, false);
    fft2d(fb, side, side, false);

    // Cross-power spectrum with magnitude whitening (the classic phase-
    // correlation normalization): R = Fa·conj(Fb) / |Fa·conj(Fb)|. Without
    // the division the surface is dominated by low-frequency energy and the
    // correlation peak drowns. Bins with negligible magnitude are zeroed
    // (regularized whitening) so NoData-filled or flat spectra stay quiet.
    std::vector<std::complex<double>> cross(side * side);
    double maxDenom = 0.0;
    for (std::size_t k = 0; k < cross.size(); ++k)
        maxDenom = std::max(maxDenom, std::abs(fa[k] * std::conj(fb[k])));
    for (std::size_t k = 0; k < cross.size(); ++k) {
        const std::complex<double> c = fa[k] * std::conj(fb[k]);
        const double denom = std::abs(c);
        cross[k] = denom > maxDenom * 1e-6 ? c / denom : std::complex<double>{0.0, 0.0};
    }
    fft2d(cross, side, side, true);
    normalizeInverse2d(cross, side, side);

    const int N = static_cast<int>(side);
    std::size_t bestIdx = 0;
    double bestMag = -1.0;
    std::vector<double> mags(cross.size());
    for (std::size_t k = 0; k < cross.size(); ++k) {
        mags[k] = std::abs(cross[k]);
        if (mags[k] > bestMag) {
            bestMag = mags[k];
            bestIdx = k;
        }
    }
    if (!(bestMag > kEps))
        return res;
    res.snr = bestMag / std::max(medianOf(mags), kEps);

    // Unwrap peak into [-N/2, N/2) and refine with a wrapped parabola.
    // Convention: cross = Fa·conj(Fb) peaks at MINUS the shift — the dst
    // window content equals the src window shifted by -peak. Negate here so
    // the returned (dx, dy) is the true shift d with winDst(x) ≈ winSrc(x-d).
    const int iy0 = static_cast<int>(bestIdx / N);
    const int ix0 = static_cast<int>(bestIdx % N);
    auto unwrap = [N](int v) { return v >= N / 2 ? v - N : v; };
    const int iy = -unwrap(iy0);
    const int ix = -unwrap(ix0);
    auto at = [N, &mags](int y, int x) {
        const int yy = (y % N + N) % N;
        const int xx = (x % N + N) % N;
        return mags[static_cast<std::size_t>(yy) * static_cast<std::size_t>(N)
                    + static_cast<std::size_t>(xx)];
    };
    // Parabola around the measured peak (unwrapped negative-space is
    // symmetric, so evaluate neighbors around (-ix, -iy) on the surface).
    const double ax = at(-iy, -ix - 1);
    const double bx = at(-iy, -ix);
    const double cxx = at(-iy, -ix + 1);
    const double ay = at(-iy - 1, -ix);
    const double by = at(-iy, -ix);
    const double cy = at(-iy + 1, -ix);
    const double denomX = ax - 2.0 * bx + cxx;
    const double denomY = ay - 2.0 * by + cy;
    const double subX = std::abs(denomX) > kEps ? 0.5 * (ax - cxx) / denomX : 0.0;
    const double subY = std::abs(denomY) > kEps ? 0.5 * (ay - cy) / denomY : 0.0;
    res.dx = static_cast<double>(ix) - std::max(-1.0, std::min(1.0, subX));
    res.dy = static_cast<double>(iy) - std::max(-1.0, std::min(1.0, subY));
    res.ok = true;
    return res;
}

struct ScanResult {
    double dx{0.0};
    double dy{0.0};
    double score{0.0};
    bool ok{false};
};

/// 16-bin robust quantization bounds (2nd/98th percentile of the window).
void quantizeBounds(const Window& win, float& lo, float& hi)
{
    std::vector<float> s = win.data;
    std::sort(s.begin(), s.end());
    const auto pick = [&s](double q) {
        const auto idx = static_cast<std::size_t>(q * static_cast<double>(s.size() - 1));
        return s[idx];
    };
    lo = pick(0.02);
    hi = pick(0.98);
    if (hi - lo < 1e-6f)
        hi = lo + 1e-6f;
}

double normalizedMutualInformation(const Window& a, const Window& b, int dy, int dx, int margin,
                                   float aLo, float aHi, float bLo, float bHi, int stride)
{
    constexpr int kBins = 16;
    std::vector<double> joint(static_cast<std::size_t>(kBins) * kBins, 0.0);
    std::vector<double> ha(kBins, 0.0), hb(kBins, 0.0);
    double count = 0.0;
    const int side = a.side;
    for (int y = margin; y < side - margin; y += stride) {
        for (int x = margin; x < side - margin; x += stride) {
            const int by = y + dy;
            const int bx = x + dx;
            if (by < 0 || by >= side || bx < 0 || bx >= side)
                continue;
            const double av = a.data[static_cast<std::size_t>(y) * side + x];
            const double bv = b.data[static_cast<std::size_t>(by) * side + bx];
            if (std::isnan(av) || std::isnan(bv))
                continue; // cannot happen post-extraction; belt and braces
            int ib = static_cast<int>((av - aLo) / (aHi - aLo) * (kBins - 1));
            int jb = static_cast<int>((bv - bLo) / (bHi - bLo) * (kBins - 1));
            ib = std::max(0, std::min(kBins - 1, ib));
            jb = std::max(0, std::min(kBins - 1, jb));
            joint[static_cast<std::size_t>(ib) * kBins + jb] += 1.0;
            ha[static_cast<std::size_t>(ib)] += 1.0;
            hb[static_cast<std::size_t>(jb)] += 1.0;
            count += 1.0;
        }
    }
    if (count < 16.0)
        return 0.0;
    auto entropy = [&count](const std::vector<double>& p) {
        double h = 0.0;
        for (double v : p) {
            if (v <= 0.0)
                continue;
            const double pr = v / count;
            h -= pr * std::log2(pr);
        }
        return h;
    };
    double mi = 0.0;
    for (int i = 0; i < kBins; ++i) {
        if (ha[static_cast<std::size_t>(i)] <= 0.0)
            continue;
        for (int j = 0; j < kBins; ++j) {
            const double pij = joint[static_cast<std::size_t>(i) * kBins + j];
            if (pij <= 0.0)
                continue;
            mi += (pij / count)
                  * std::log2((pij / count)
                              / ((ha[static_cast<std::size_t>(i)] / count)
                                 * (hb[static_cast<std::size_t>(j)] / count)));
        }
    }
    const double hMin = std::min(entropy(ha), entropy(hb));
    if (hMin < kEps)
        return 0.0;
    return mi / hMin;
}

/// Exhaustive local scan of normalized MI around the predicted offset.
ScanResult mutualInfoScan(const Window& srcWin, const Window& dstWin, int radius)
{
    ScanResult res;
    const int margin = radius + 1;
    if (2 * margin >= srcWin.side)
        return res;
    float aLo = 0.f, aHi = 1.f, bLo = 0.f, bHi = 1.f;
    quantizeBounds(srcWin, aLo, aHi);
    quantizeBounds(dstWin, bLo, bHi);
    const int stride = srcWin.side > 48 ? 2 : 1;
    double best = -1.0;
    int bestDx = 0, bestDy = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const double mi = normalizedMutualInformation(srcWin, dstWin, dy, dx, margin, aLo,
                                                          aHi, bLo, bHi, stride);
            if (mi > best) {
                best = mi;
                bestDx = dx;
                bestDy = dy;
            }
        }
    }
    if (best < 0.0)
        return res;
    res.dx = static_cast<double>(bestDx);
    res.dy = static_cast<double>(bestDy);
    res.score = std::max(0.0, std::min(1.0, best));
    res.ok = true;
    return res;
}

double zeroMeanNcc(const Window& a, const Window& b, int dy, int dx, int margin)
{
    const int side = a.side;
    double sa = 0.0, sb = 0.0;
    int n = 0;
    for (int y = margin; y < side - margin; ++y) {
        for (int x = margin; x < side - margin; ++x) {
            sa += a.data[static_cast<std::size_t>(y) * side + x];
            sb += b.data[static_cast<std::size_t>(y + dy) * side + (x + dx)];
            ++n;
        }
    }
    if (n == 0)
        return -1.0;
    const double ma = sa / n, mb = sb / n;
    double num = 0.0, da = 0.0, dbb = 0.0;
    for (int y = margin; y < side - margin; ++y) {
        for (int x = margin; x < side - margin; ++x) {
            const double va = a.data[static_cast<std::size_t>(y) * side + x] - ma;
            const double vb = b.data[static_cast<std::size_t>(y + dy) * side + (x + dx)] - mb;
            num += va * vb;
            da += va * va;
            dbb += vb * vb;
        }
    }
    const double den = std::sqrt(da * dbb);
    return den > kEps ? num / den : -1.0;
}

ScanResult nccScan(const Window& srcWin, const Window& dstWin, int radius)
{
    ScanResult res;
    const int margin = radius + 1;
    if (2 * margin >= srcWin.side)
        return res;
    double best = -2.0;
    int bestDx = 0, bestDy = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const double ncc = zeroMeanNcc(srcWin, dstWin, dy, dx, margin);
            if (ncc > best) {
                best = ncc;
                bestDx = dx;
                bestDy = dy;
            }
        }
    }
    if (best < -1.5)
        return res;
    res.dx = static_cast<double>(bestDx);
    res.dy = static_cast<double>(bestDy);
    res.score = std::max(0.0, best); // NCC coefficient; chance level is 0
    res.ok = true;
    return res;
}

} // namespace

double MultimodalMatcher::estimateScratchMiB(int srcWidth, int srcHeight, int dstWidth,
                                             int dstHeight, const MultimodalMatchOptions& options)
{
    const int side = std::max(16, std::min(256, options.windowSize));
    const std::size_t padded = nextPowerOfTwo(static_cast<std::size_t>(side));
    // Two FFT input buffers + cross-surface + magnitude surface, 16 B each.
    const double fftMiB = 4.0 * static_cast<double>(padded * padded) * 16.0 / (1024.0 * 1024.0);
    // Pyramid copies: geometric series bounded by 2x the level-0 size.
    const double pyrMiB = (static_cast<double>(srcWidth) * srcHeight
                           + static_cast<double>(dstWidth) * dstHeight)
                          * 4.0 * 2.0 / (1024.0 * 1024.0);
    return fftMiB + pyrMiB;
}

MultimodalMatchReport MultimodalMatcher::matchImages(const float* srcData, int srcWidth,
                                                     int srcHeight, const float* dstData,
                                                     int dstWidth, int dstHeight,
                                                     const MultimodalMatchOptions& options,
                                                     const std::atomic_bool* cancel)
{
    if (!srcData || !dstData || srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0
        || dstHeight <= 0)
        throw std::invalid_argument("multimodal matcher: invalid image buffers");

    MultimodalMatchReport report;

    const int windowSize = std::max(16, std::min(256, options.windowSize));
    if (estimateScratchMiB(srcWidth, srcHeight, dstWidth, dstHeight, options)
        > options.bounds.maxWorkingMiB) {
        report.reason = QStringLiteral("cap_exhausted");
        return report;
    }

    // ---- Pyramids ------------------------------------------------------
    std::vector<FloatLevel> srcPyr;
    srcPyr.push_back({std::vector<float>(srcData,
                                         srcData + static_cast<std::size_t>(srcWidth) * srcHeight),
                      srcWidth, srcHeight});
    std::vector<FloatLevel> dstPyr;
    dstPyr.push_back({std::vector<float>(dstData,
                                         dstData + static_cast<std::size_t>(dstWidth) * dstHeight),
                      dstWidth, dstHeight});
    // Descend only while the next level is still strictly larger than the
    // window: a coarse level equal to the window side has an empty window-
    // center range and could never place a cell.
    auto canDescend = [&windowSize](const FloatLevel& lvl) {
        const int nw = std::max(1, lvl.width / 2);
        const int nh = std::max(1, lvl.height / 2);
        return std::min(nw, nh) > windowSize;
    };
    while (srcPyr.size() < static_cast<std::size_t>(std::max(1, options.bounds.maxPyramidLevels))
           && canDescend(srcPyr.back()) && canDescend(dstPyr.back())) {
        srcPyr.push_back(downsampleLevel(srcPyr.back()));
        dstPyr.push_back(downsampleLevel(dstPyr.back()));
    }

    const int coarsest = static_cast<int>(srcPyr.size()) - 1;

    struct LevelTie {
        double sx;
        double sy;
        double dx;
        double dy;
        double score;
    };
    // Ties from the previous (coarser) level, used for per-cell offset
    // propagation: a fine cell inherits 2x the offset of its nearest coarse
    // cell (global-median propagation alone cannot follow rotating/scaling
    // scenes where the displacement field varies across the image).
    std::vector<LevelTie> prevTies;
    double prevSpacing = 1.0;
    double prevMedianOx = 0.0;
    double prevMedianOy = 0.0;

    for (int level = coarsest; level >= 0; --level) {
        if (cancel && cancel->load()) {
            report = MultimodalMatchReport{};
            report.reason = QStringLiteral("cancelled");
            return report;
        }
        const FloatLevel& srcL = srcPyr[static_cast<std::size_t>(level)];
        const FloatLevel& dstL = dstPyr[static_cast<std::size_t>(level)];
        // Coarse levels use half-size windows: window density (not size) is
        // what beats periodic-texture sidelobes in the per-level median.
        int minDim = std::min(srcL.width, srcL.height);
        int win = level == 0 ? windowSize
                             : std::max(32, std::min(windowSize / 2, minDim - 2));
        win = std::min(win, minDim);
        // A window as large as the image side leaves an empty center range;
        // shrink it below the side so at least one cell can be placed.
        if (win >= minDim)
            win = minDim - 2;
        if (win < 16)
            break;
        const int halfL = win / 2;
        const int spacingL = std::max(1, halfL);
        const int radiusL = level == 0 ? std::max(1, options.searchRadius)
                                       : std::max(2, options.searchRadius);

        // Coarse levels always seed with phase correlation (radiometric gain
        // invariant, translation robust). The finest level uses the requested
        // metric; Auto resolves to mutual information (cross-modal default).
        const MatchMetric fineMetric = options.metric == MatchMetric::Auto
                                           ? MatchMetric::MutualInformation
                                           : options.metric;
        const bool phaseAtThisLevel =
            level > 0 || fineMetric == MatchMetric::PhaseCorrelation;

        std::vector<LevelTie> ties;

        // Per-cell offset prediction from the previous level. The query
        // point is expressed in the COARSE level's pixel scale (cx/2).
        auto predictedOffset = [&](double cx, double cy) {
            if (prevTies.empty())
                return std::pair<double, double>{0.0, 0.0};
            const double qx = cx / 2.0;
            const double qy = cy / 2.0;
            double bestDist = std::numeric_limits<double>::infinity();
            double ox = 2.0 * prevMedianOx;
            double oy = 2.0 * prevMedianOy;
            for (const auto& t : prevTies) {
                const double d = std::hypot(t.sx - qx, t.sy - qy);
                if (d < bestDist) {
                    bestDist = d;
                    ox = 2.0 * (t.dx - t.sx);
                    oy = 2.0 * (t.dy - t.sy);
                }
            }
            if (bestDist > prevSpacing) // nearest coarse cell too far: median
                return std::pair<double, double>{2.0 * prevMedianOx, 2.0 * prevMedianOy};
            return std::pair<double, double>{ox, oy};
        };

        for (int cy = halfL; cy < srcL.height - halfL; cy += spacingL) {
            for (int cx = halfL; cx < srcL.width - halfL; cx += spacingL) {
                if (cancel && cancel->load()) {
                    report = MultimodalMatchReport{};
                    report.reason = QStringLiteral("cancelled");
                    return report;
                }
                const Window srcWin = extractWindow(srcL, cx, cy, win);
                if (!srcWin.ok || srcWin.validFraction < options.minValidFraction
                    || srcWin.variance < kEps) {
                    ++report.rejectedFlatWindows;
                    continue;
                }
                const auto [offX, offY] = predictedOffset(cx, cy);
                const int predX = cx + static_cast<int>(std::lround(offX));
                const int predY = cy + static_cast<int>(std::lround(offY));
                if (predX - halfL < 0 || predY - halfL < 0 || predX - halfL + win > dstL.width
                    || predY - halfL + win > dstL.height)
                    continue;
                const Window dstWin = extractWindow(dstL, predX, predY, win);
                if (!dstWin.ok || dstWin.validFraction < options.minValidFraction
                    || dstWin.variance < kEps) {
                    ++report.rejectedFlatWindows;
                    continue;
                }

                if (phaseAtThisLevel) {
                    const PhaseResult ph = phaseCorrelate(srcWin, dstWin);
                    if (ph.ok && ph.snr >= options.minPeakSnr) {
                        LevelTie tie;
                        tie.sx = static_cast<double>(cx);
                        tie.sy = static_cast<double>(cy);
                        tie.dx = static_cast<double>(predX) + ph.dx;
                        tie.dy = static_cast<double>(predY) + ph.dy;
                        tie.score = std::min(1.0, ph.snr / kSnrScoreNorm);
                        ties.push_back(tie);
                        continue;
                    }
                    // Phase failed: at coarse levels fall through to the
                    // metric scan (cross-modal pairs can defeat the phase
                    // peak while the window still carries rank structure) —
                    // unless the caller pinned the phase-only metric.
                    if (fineMetric == MatchMetric::PhaseCorrelation) {
                        ++report.rejectedLowSnrWindows;
                        continue;
                    }
                }
                {
                    const ScanResult scan =
                        fineMetric == MatchMetric::NormalizedCrossCorrelation
                            ? nccScan(srcWin, dstWin, radiusL)
                            : mutualInfoScan(srcWin, dstWin, radiusL);
                    if (!scan.ok || scan.score < options.minScore) {
                        ++report.rejectedLowSnrWindows;
                        continue;
                    }
                    LevelTie tie;
                    tie.sx = static_cast<double>(cx);
                    tie.sy = static_cast<double>(cy);
                    tie.dx = static_cast<double>(predX) + scan.dx;
                    tie.dy = static_cast<double>(predY) + scan.dy;
                    tie.score = scan.score;
                    ties.push_back(tie);
                }
            }
        }

        PyramidStageEvidence stage;
        stage.level = level;
        stage.sourceWidth = srcL.width;
        stage.sourceHeight = srcL.height;
        stage.accepted = static_cast<int>(ties.size());
        if (!ties.empty()) {
            std::vector<double> scores;
            scores.reserve(ties.size());
            for (const auto& t : ties)
                scores.push_back(t.score);
            stage.medianScore = medianOf(scores);
        }
        report.stages.push_back(stage);

        if (ties.empty()) {
            // A coarse level with no accepted windows is not fatal: finer
            // levels carry more detail. Reset the prediction and keep going;
            // only a finest-level failure is a structural refusal.
            if (level > 0) {
                prevTies.clear();
                prevMedianOx = 0.0;
                prevMedianOy = 0.0;
                continue;
            }
            report.reason = report.rejectedFlatWindows > 0 && report.rejectedLowSnrWindows == 0
                                ? QStringLiteral("flat_region")
                                : QStringLiteral("low_peak_snr");
            report.status = RegistrationStatus::Refused;
            return report;
        }

        // Robust per-level offset bookkeeping for the next finer level.
        std::vector<double> ox, oy;
        ox.reserve(ties.size());
        oy.reserve(ties.size());
        for (const auto& t : ties) {
            ox.push_back(t.dx - t.sx);
            oy.push_back(t.dy - t.sy);
        }
        const double medOx = medianOf(ox);
        const double medOy = medianOf(oy);
        prevTies = std::move(ties);
        prevSpacing = spacingL;
        prevMedianOx = medOx;
        prevMedianOy = medOy;
        if (level == 0) {
            std::vector<RegistrationPoint> pts;
            pts.reserve(prevTies.size());
            for (const auto& t : prevTies) {
                RegistrationPoint p;
                p.srcX = t.sx;
                p.srcY = t.sy;
                p.dstX = t.dx;
                p.dstY = t.dy;
                p.score = t.score;
                p.inlier = false;
                pts.push_back(p);
            }
            const std::size_t cap =
                static_cast<std::size_t>(std::max(8, options.bounds.maxMatches));
            if (pts.size() > cap) {
                std::stable_sort(pts.begin(), pts.end(),
                                 [](const RegistrationPoint& a, const RegistrationPoint& b) {
                                     return a.score > b.score;
                                 });
                pts.resize(cap);
            }
            report.points = std::move(pts);
        }
    }

    // ---- Consensus + trust decision ------------------------------------
    if (report.points.size() < static_cast<std::size_t>(options.minMatches)) {
        report.status = RegistrationStatus::Refused;
        report.reason = report.points.empty() && report.rejectedFlatWindows > 0
                            ? QStringLiteral("flat_region")
                            : QStringLiteral("too_few_matches");
        return report;
    }

    std::vector<std::pair<double, double>> srcPts, dstPts;
    srcPts.reserve(report.points.size());
    dstPts.reserve(report.points.size());
    for (const auto& p : report.points) {
        srcPts.emplace_back(p.srcX, p.srcY);
        dstPts.emplace_back(p.dstX, p.dstY);
    }
    auto [H, mask] = rs::algorithms::FeatureMatcher::estimateHomographyRansac(
        srcPts, dstPts, options.ransacReprojThreshold, options.ransacMaxIters,
        options.ransacConfidence);

    int inliers = 0;
    double sqAcc = 0.0;
    for (std::size_t i = 0; i < report.points.size(); ++i) {
        const bool in = i < mask.size() && mask[i];
        report.points[i].inlier = in;
        if (!in)
            continue;
        ++inliers;
        const double w = H[6] * report.points[i].srcX + H[7] * report.points[i].srcY + H[8];
        if (std::abs(w) < kEps)
            continue;
        const double hx = (H[0] * report.points[i].srcX + H[1] * report.points[i].srcY + H[2]) / w;
        const double hy = (H[3] * report.points[i].srcX + H[4] * report.points[i].srcY + H[5]) / w;
        const double ex = hx - report.points[i].dstX;
        const double ey = hy - report.points[i].dstY;
        sqAcc += ex * ex + ey * ey;
        report.points[i].residual = std::sqrt(ex * ex + ey * ey);
    }
    report.inlierCount = inliers;
    report.inlierRatio =
        report.points.empty()
            ? 0.0
            : static_cast<double>(inliers) / static_cast<double>(report.points.size());
    report.inlierRmse = inliers > 0 ? std::sqrt(sqAcc / static_cast<double>(inliers)) : 0.0;
    report.consensusHomography.assign(H.begin(), H.end());

    // Spatial coverage over the FULL source extent (Oracle: a spatially
    // clustered match set must not score as high quality — normalizing by the
    // candidate footprint would inflate a tight cluster). populated coverage
    // cells / total coverage cells.
    {
        const int g = std::max(1, options.coverageGrid);
        const double cellW = std::max(1e-9, static_cast<double>(srcWidth) / g);
        const double cellH = std::max(1e-9, static_cast<double>(srcHeight) / g);
        int populated = 0;
        for (int gy = 0; gy < g; ++gy) {
            for (int gx = 0; gx < g; ++gx) {
                const double x0 = gx * cellW;
                const double y0 = gy * cellH;
                const double x1 = (gx == g - 1) ? srcWidth + 1e-9 : x0 + cellW;
                const double y1 = (gy == g - 1) ? srcHeight + 1e-9 : y0 + cellH;
                const bool anyInlier = std::any_of(
                    report.points.begin(), report.points.end(), [&](const RegistrationPoint& p) {
                        return p.inlier && p.srcX >= x0 && p.srcX < x1 && p.srcY >= y0
                               && p.srcY < y1;
                    });
                if (anyInlier)
                    ++populated;
            }
        }
        report.coverageRatio = static_cast<double>(populated) / static_cast<double>(g * g);
    }

    if (report.inlierCount < options.minMatches) {
        report.status = RegistrationStatus::Refused;
        report.reason = QStringLiteral("too_few_matches");
        return report;
    }
    if (report.coverageRatio < options.minCoverageRatio) {
        report.status = RegistrationStatus::LowConfidence;
        report.reason = QStringLiteral("insufficient_coverage");
        return report;
    }
    // A minority consensus is flagged as low confidence — unless it is both
    // large in absolute terms and tightly agreeing, which points at genuine
    // scene evidence (e.g. rotated pairs where many candidate windows lose
    // correlation but the surviving consensus is exact).
    if (report.inlierRatio < 0.5
        && !(report.inlierRmse <= 1.0 && report.inlierCount >= 20)) {
        report.status = RegistrationStatus::LowConfidence;
        report.reason = QStringLiteral("low_consensus");
        return report;
    }

    report.status = RegistrationStatus::Success;
    return report;
}

} // namespace sicnu::registration

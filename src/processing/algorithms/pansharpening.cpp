// pansharpening.cpp — D14 Package F implementation (ADR 0159).
//
// Variances/covariances are accumulated with a single-pass Welford-style
// online algorithm (numerically stable, cache-friendly). Division guards use
// epsilon = 1e-7 per the D14 spec. The GS inverse follows the projection
// identity MS_k = GS_k + sum_j g_kj*GS_j: only the GS1 term is substituted
// with the histogram-matched pan, so a fused band reduces exactly to its
// upsampled MS input whenever PAN_norm == GS1.
#include "processing/algorithms/pansharpening.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace rs::algorithms {

namespace {

constexpr double kEpsilon = 1e-7;

/// Bilinear upsample src (sw x sh) to dst (dw x dh).
void upsampleBilinear(const float* src, int sw, int sh, float* dst, int dw, int dh)
{
    for (int j = 0; j < dh; ++j) {
        const double v = (j + 0.5) * sh / dh - 0.5;
        const int j0 = std::clamp(static_cast<int>(std::floor(v)), 0, sh - 1);
        const int j1 = std::min(j0 + 1, sh - 1);
        const double fv = std::clamp(v - j0, 0.0, 1.0);
        for (int i = 0; i < dw; ++i) {
            const double u = (i + 0.5) * sw / dw - 0.5;
            const int i0 = std::clamp(static_cast<int>(std::floor(u)), 0, sw - 1);
            const int i1 = std::min(i0 + 1, sw - 1);
            const double fu = std::clamp(u - i0, 0.0, 1.0);
            const double a = src[static_cast<size_t>(j0) * sw + i0] * (1 - fu) + src[static_cast<size_t>(j0) * sw + i1] * fu;
            const double b = src[static_cast<size_t>(j1) * sw + i0] * (1 - fu) + src[static_cast<size_t>(j1) * sw + i1] * fu;
            dst[static_cast<size_t>(j) * dw + i] = static_cast<float>(a * (1 - fv) + b * fv);
        }
    }
}

/// 3x3 box blur with edge replication.
void boxBlur3(const float* src, int width, int height, float* dst)
{
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            double sum = 0.0;
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    const int x = std::clamp(i + di, 0, width - 1);
                    const int y = std::clamp(j + dj, 0, height - 1);
                    sum += src[static_cast<size_t>(y) * width + x];
                }
            dst[static_cast<size_t>(j) * width + i] = static_cast<float>(sum / 9.0);
        }
    }
}

void matchMeanStd(const float* src, int count, double refMean, double refStd, float* dst)
{
    double mean = 0.0, m2 = 0.0;
    int n = 0;
    for (int i = 0; i < count; ++i) { // Welford
        ++n;
        const double delta = src[i] - mean;
        mean += delta / n;
        m2 += delta * (src[i] - mean);
    }
    const double stdDev = std::sqrt(m2 / std::max(1, n - 1));
    const double gain = refStd > kEpsilon ? refStd / std::max(stdDev, kEpsilon) : 1.0;
    for (int i = 0; i < count; ++i)
        dst[i] = static_cast<float>(gain * (src[i] - mean) + refMean);
}

/// Basic statistics helper: mean and (sample) std.
void meanAndStd(const float* data, int count, double& mean, double& stdDev)
{
    double m = 0.0, m2 = 0.0;
    int n = 0;
    for (int i = 0; i < count; ++i) {
        ++n;
        const double delta = data[i] - m;
        m += delta / n;
        m2 += delta * (data[i] - m);
    }
    mean = m;
    stdDev = std::sqrt(m2 / std::max(1, n - 1));
}

double covariance(const float* a, const float* b, int count, double meanA, double meanB)
{
    double cov = 0.0;
    for (int i = 0; i < count; ++i)
        cov += (static_cast<double>(a[i]) - meanA) * (static_cast<double>(b[i]) - meanB);
    return cov / std::max(1, count - 1);
}

bool runGramSchmidt(const std::vector<const float*>& msUp, int count,
                    const std::vector<double>& weights,
                    const float* panNorm, float* gs1, std::vector<float*>& out)
{
    const size_t bands = msUp.size();
    // Simulated low-resolution pan GS1 = sum w_k * MS_k_up.
    for (int i = 0; i < count; ++i)
        gs1[i] = 0.0f;
    for (size_t k = 0; k < bands; ++k)
        for (int i = 0; i < count; ++i)
            gs1[i] += static_cast<float>(weights[k] * msUp[k][i]);

    double gs1Mean = 0.0, gs1Std = 0.0;
    meanAndStd(gs1, count, gs1Mean, gs1Std);
    const double gs1Var = gs1Std * gs1Std;

    // Fused_k = MS_k_up + g_k * (PAN_norm - GS1), g_k = Cov(MS_k, GS1)/Var(GS1).
    for (size_t k = 0; k < bands; ++k) {
        const float* ms = msUp[k];
        double msMean = 0.0, msStd = 0.0;
        meanAndStd(ms, count, msMean, msStd);
        const double g = covariance(ms, gs1, count, msMean, gs1Mean) / std::max(gs1Var, kEpsilon);
        float* dst = out[k];
        for (int i = 0; i < count; ++i)
            dst[i] = static_cast<float>(ms[i] + g * (panNorm[i] - gs1[i]));
    }
    return true;
}

bool runBrovey(const std::vector<const float*>& msUp, int count, const float* pan,
               std::vector<float*>& out)
{
    const size_t bands = msUp.size();
    for (int i = 0; i < count; ++i) {
        double sum = 0.0;
        for (const auto& band : msUp)
            sum += band[i];
        if (std::abs(sum) < kEpsilon)
            sum = std::copysign(kEpsilon, sum);
        for (size_t k = 0; k < bands; ++k)
            out[k][i] = static_cast<float>(msUp[k][i] / sum * pan[i]);
    }
    return true;
}

bool runIhs(const std::vector<const float*>& msUp, int count, const float* panNorm,
            std::vector<float*>& out)
{
    // Gonzalez & Woods circular colour model: I = (R+G+B)/3, hue on the
    // circle, S = 1 - min/I. The inverse is exact, so a fused band equals
    // its upsampled input whenever the substituted intensity matches.
    const double pi = std::numbers::pi;
    for (int i = 0; i < count; ++i) {
        const double r = msUp[0][i];
        const double g = msUp[1][i];
        const double b = msUp[2][i];
        const double intensity = (r + g + b) / 3.0;
        const double minRgb = std::min({r, g, b});
        const double saturation = intensity > kEpsilon ? 1.0 - minRgb / intensity : 0.0;
        double hue = std::atan2(std::sqrt(3.0) * (g - b), 2.0 * r - g - b); // (-pi, pi]
        if (hue < 0.0)
            hue += 2.0 * pi;

        const double i2 = panNorm[i];
        double r2 = 0.0, g2 = 0.0, b2 = 0.0;
        if (hue < 2.0 * pi / 3.0) {
            b2 = i2 * (1.0 - saturation);
            r2 = i2 * (1.0 + saturation * std::cos(hue) / std::cos(pi / 3.0 - hue));
            g2 = 3.0 * i2 - r2 - b2;
        } else if (hue < 4.0 * pi / 3.0) {
            r2 = i2 * (1.0 - saturation);
            g2 = i2 * (1.0 + saturation * std::cos(hue - 2.0 * pi / 3.0) / std::cos(pi - hue));
            b2 = 3.0 * i2 - r2 - g2;
        } else {
            g2 = i2 * (1.0 - saturation);
            b2 = i2 * (1.0 + saturation * std::cos(hue - 4.0 * pi / 3.0) / std::cos(5.0 * pi / 3.0 - hue));
            r2 = 3.0 * i2 - g2 - b2;
        }
        out[0][i] = static_cast<float>(r2);
        out[1][i] = static_cast<float>(g2);
        out[2][i] = static_cast<float>(b2);
    }
    for (size_t k = 3; k < msUp.size(); ++k)
        std::copy(msUp[k], msUp[k] + count, out[k]);
    return true;
}

bool runHpf(const std::vector<const float*>& msUp, int count, const float* pan,
            int panWidth, int panHeight, std::vector<float*>& out)
{
    std::vector<float> blur(static_cast<size_t>(count), 0.0f);
    boxBlur3(pan, panWidth, panHeight, blur.data());
    for (size_t k = 0; k < msUp.size(); ++k) {
        for (int i = 0; i < count; ++i)
            out[k][i] = static_cast<float>(msUp[k][i] + (pan[i] - blur[i]));
    }
    return true;
}

} // namespace

bool PanSharpening::sharpen(PanSharpenMethod method,
                            const std::vector<const float*>& msBands,
                            int msWidth, int msHeight,
                            const float* panBand,
                            int panWidth, int panHeight,
                            std::vector<float*>& outSharpenedBands,
                            const std::vector<double>& bandWeights)
{
    if (msBands.size() < 3 || !panBand)
        return false;
    if (msWidth <= 0 || msHeight <= 0 || panWidth <= 0 || panHeight <= 0)
        return false;
    if (panWidth % msWidth != 0 || panHeight % msHeight != 0)
        return false;
    if (outSharpenedBands.size() != msBands.size())
        return false;
    for (const float* band : outSharpenedBands)
        if (!band)
            return false;

    const size_t bands = msBands.size();
    std::vector<double> weights = bandWeights;
    if (weights.empty()) {
        weights.assign(bands, 1.0 / static_cast<double>(bands));
    } else if (weights.size() != bands) {
        return false;
    }

    // Bilinearly upsample every MS band to the pan grid.
    std::vector<std::vector<float>> up(bands);
    for (size_t k = 0; k < bands; ++k) {
        if (!msBands[k])
            return false;
        up[k].resize(static_cast<size_t>(panWidth) * panHeight);
        upsampleBilinear(msBands[k], msWidth, msHeight, up[k].data(), panWidth, panHeight);
    }
    std::vector<const float*> msUp;
    msUp.reserve(bands);
    for (const auto& band : up)
        msUp.push_back(band.data());

    const int count = panWidth * panHeight;
    switch (method) {
    case PanSharpenMethod::Brovey:
        return runBrovey(msUp, count, panBand, outSharpenedBands);
    case PanSharpenMethod::Hpf:
        return runHpf(msUp, count, panBand, panWidth, panHeight, outSharpenedBands);
    case PanSharpenMethod::Ihs:
    case PanSharpenMethod::GramSchmidt: {
        // Both need a pan band histogram-matched to the reference intensity:
        // GS uses the simulated low-res pan; IHS uses the average of RGB.
        std::vector<float> reference(static_cast<size_t>(count), 0.0f);
        if (method == PanSharpenMethod::GramSchmidt) {
            for (size_t k = 0; k < bands; ++k)
                for (int i = 0; i < count; ++i)
                    reference[i] += static_cast<float>(weights[k] * msUp[k][i]);
        } else {
            for (int i = 0; i < count; ++i)
                reference[i] = static_cast<float>((msUp[0][i] + msUp[1][i] + msUp[2][i]) / 3.0);
        }
        double refMean = 0.0, refStd = 0.0;
        meanAndStd(reference.data(), count, refMean, refStd);
        std::vector<float> panNorm(static_cast<size_t>(count), 0.0f);
        matchMeanStd(panBand, count, refMean, refStd, panNorm.data());

        if (method == PanSharpenMethod::GramSchmidt)
            return runGramSchmidt(msUp, count, weights, panNorm.data(), reference.data(), outSharpenedBands);
        return runIhs(msUp, count, panNorm.data(), outSharpenedBands);
    }
    }
    return false;
}

PanSharpenMetrics PanSharpening::evaluateQuality(const std::vector<const float*>& degradedFusedBands,
                                                 const std::vector<const float*>& originalMsBands,
                                                 int width, int height,
                                                 double scaleRatio)
{
    PanSharpenMetrics metrics;
    const size_t bands = std::min(degradedFusedBands.size(), originalMsBands.size());
    if (bands == 0 || width <= 0 || height <= 0)
        return metrics;
    const int count = width * height;

    double ergasSum = 0.0;
    double ccSum = 0.0;
    double rmseSum = 0.0;
    double ssimSum = 0.0;

    // Global dynamic range for SSIM constants (max over both images).
    double range = 1.0;
    for (size_t k = 0; k < bands; ++k) {
        for (int i = 0; i < count; ++i)
            range = std::max({range, static_cast<double>(degradedFusedBands[k][i]),
                              static_cast<double>(originalMsBands[k][i])});
    }
    const double c1 = std::pow(0.01 * range, 2);
    const double c2 = std::pow(0.03 * range, 2);

    for (size_t k = 0; k < bands; ++k) {
        const float* fused = degradedFusedBands[k];
        const float* original = originalMsBands[k];

        double meanF = 0.0, stdF = 0.0;
        double meanO = 0.0, stdO = 0.0;
        meanAndStd(fused, count, meanF, stdF);
        meanAndStd(original, count, meanO, stdO);

        double sumSq = 0.0;
        for (int i = 0; i < count; ++i)
            sumSq += std::pow(static_cast<double>(fused[i]) - original[i], 2);
        const double rmse = std::sqrt(sumSq / count);
        rmseSum += rmse;

        const double meanOrig = std::abs(meanO) > kEpsilon ? meanO : kEpsilon;
        ergasSum += std::pow(rmse / meanOrig, 2);

        const double cov = covariance(fused, original, count, meanF, meanO);
        const double denom = stdF * stdO;
        ccSum += denom > kEpsilon ? cov / denom : (rmse < kEpsilon ? 1.0 : 0.0);

        const double ssim = ((2.0 * meanF * meanO + c1) * (2.0 * cov + c2)) /
                            ((meanF * meanF + meanO * meanO + c1) * (stdF * stdF + stdO * stdO + c2));
        ssimSum += ssim;
    }

    metrics.rmse = rmseSum / static_cast<double>(bands);
    metrics.ergas = 100.0 * scaleRatio * std::sqrt(ergasSum / static_cast<double>(bands));
    metrics.meanCc = ccSum / static_cast<double>(bands);
    metrics.ssim = ssimSum / static_cast<double>(bands);
    return metrics;
}

} // namespace rs::algorithms

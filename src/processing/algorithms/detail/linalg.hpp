// linalg.hpp — shared dense linear algebra for the D14 geometric modules
// (ADR 0159). Header-only, dependency-free, no external matrix library.
//
// One-sided Jacobi SVD: numerically robust for the small dense systems used
// by transform solving, homography estimation and RANSAC hypothesis fitting.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace rs::algorithms::detail {

inline constexpr double kSvdRelTruncation = 1e-12;

struct Matrix {
    int rows{0};
    int cols{0};
    std::vector<double> a;

    double& at(int r, int c) { return a[static_cast<size_t>(r) * cols + c]; }
    [[nodiscard]] double at(int r, int c) const { return a[static_cast<size_t>(r) * cols + c]; }
};

/// One-sided Jacobi SVD of an (rows x cols) matrix with rows >= cols.
/// On return sigma holds the singular values (descending) and v holds the
/// cols x cols right singular vectors (row-major: v[k*cols+c] is row k of
/// right singular vector c). The rotated input columns are the left singular
/// vectors up to normalization. Returns false when rows < cols.
inline bool jacobiSvd(Matrix& a, std::vector<double>& sigma, std::vector<double>& v)
{
    const int m = a.rows;
    const int n = a.cols;
    if (m < n)
        return false;
    sigma.assign(n, 0.0);
    v.assign(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i) * n + i] = 1.0;

    constexpr double eps = std::numeric_limits<double>::epsilon();
    constexpr int kMaxSweeps = 30;
    for (int sweep = 0; sweep < kMaxSweeps; ++sweep) {
        double offDiagonal = 0.0;
        for (int p = 0; p < n - 1; ++p) {
            for (int q = p + 1; q < n; ++q) {
                double alpha = 0.0, beta = 0.0, gamma = 0.0;
                for (int i = 0; i < m; ++i) {
                    const double ap = a.at(i, p);
                    const double aq = a.at(i, q);
                    alpha += ap * ap;
                    beta += aq * aq;
                    gamma += ap * aq;
                }
                offDiagonal = std::max(offDiagonal, std::abs(gamma) / std::sqrt(alpha * beta + 1e-300));
                if (std::abs(gamma) <= eps * std::sqrt(alpha * beta))
                    continue;
                const double zeta = (beta - alpha) / (2.0 * gamma);
                const double t = std::copysign(1.0, zeta) /
                                 (std::abs(zeta) + std::sqrt(1.0 + zeta * zeta));
                const double c = 1.0 / std::sqrt(1.0 + t * t);
                const double s = c * t;
                for (int i = 0; i < m; ++i) {
                    const double ap = a.at(i, p);
                    const double aq = a.at(i, q);
                    a.at(i, p) = c * ap - s * aq;
                    a.at(i, q) = s * ap + c * aq;
                }
                for (int k = 0; k < n; ++k) {
                    const double vp = v[static_cast<size_t>(k) * n + p];
                    const double vq = v[static_cast<size_t>(k) * n + q];
                    v[static_cast<size_t>(k) * n + p] = c * vp - s * vq;
                    v[static_cast<size_t>(k) * n + q] = s * vp + c * vq;
                }
            }
        }
        if (offDiagonal <= eps)
            break;
    }

    for (int j = 0; j < n; ++j) {
        double norm = 0.0;
        for (int i = 0; i < m; ++i)
            norm += a.at(i, j) * a.at(i, j);
        sigma[j] = std::sqrt(norm);
    }
    // Sort descending (selection sort — n is tiny). Keep sigma, the rotated
    // input columns (U·sigma) and V in sync: every consumer pairs them by
    // column index.
    for (int i = 0; i < n; ++i) {
        int best = i;
        for (int j = i + 1; j < n; ++j)
            if (sigma[j] > sigma[best])
                best = j;
        if (best != i) {
            std::swap(sigma[i], sigma[best]);
            for (int r = 0; r < m; ++r)
                std::swap(a.at(r, i), a.at(r, best));
            for (int k = 0; k < n; ++k)
                std::swap(v[static_cast<size_t>(k) * n + i], v[static_cast<size_t>(k) * n + best]);
        }
    }
    return true;
}

/// Least-squares solve A x = b via the Jacobi SVD pseudo-inverse.
/// Returns false when A is rank-deficient; kappa receives the condition
/// number of the nonzero spectrum.
inline bool solveLeastSquares(const Matrix& aMat, const std::vector<double>& b,
                              std::vector<double>& x, double& kappa)
{
    Matrix work = aMat;
    std::vector<double> sigma;
    std::vector<double> v;
    if (!jacobiSvd(work, sigma, v))
        return false;
    const int m = aMat.rows;
    const int n = aMat.cols;

    const double sigmaMax = sigma.empty() ? 0.0 : sigma[0];
    if (sigmaMax <= 0.0)
        return false;
    // Rank-deficient systems (any singular value below the truncation floor)
    // are rejected outright: the caller reports them as unsolvable geometry
    // instead of silently returning a minimum-norm pseudo-solution.
    for (int j = 0; j < n; ++j) {
        if (sigma[j] <= kSvdRelTruncation * sigmaMax)
            return false;
    }
    const double sigmaMin = sigma[n - 1];
    kappa = sigmaMax / sigmaMin;

    // work's columns are U·sigma (rotated input), so U_jᵀ b = (col_j·b)/sigma_j
    // and the pseudo-inverse needs Sigma⁻¹ of that: divide by sigma_j twice.
    std::vector<double> utb(n, 0.0);
    for (int j = 0; j < n; ++j) {
        if (sigma[j] <= kSvdRelTruncation * sigmaMax)
            continue;
        double dot = 0.0;
        for (int i = 0; i < m; ++i)
            dot += work.at(i, j) * b[i];
        utb[j] = dot / (sigma[j] * sigma[j]);
    }
    x.assign(n, 0.0);
    for (int j = 0; j < n; ++j) {
        if (sigma[j] <= kSvdRelTruncation * sigmaMax)
            continue;
        const double scale = utb[j];
        for (int k = 0; k < n; ++k)
            x[k] += scale * v[static_cast<size_t>(k) * n + j];
    }
    return true;
}

/// Hartley point normalization: translate the centroid to the origin and
/// scale the mean Euclidean distance to sqrt(2).
struct HartleyNormalizer {
    double cx{0.0};
    double cy{0.0};
    double scale{1.0}; // p_norm = scale * (p - centroid)

    static HartleyNormalizer fit(const std::vector<std::pair<double, double>>& pts)
    {
        HartleyNormalizer n;
        const double count = static_cast<double>(pts.size());
        for (const auto& [x, y] : pts) {
            n.cx += x / count;
            n.cy += y / count;
        }
        double meanDist = 0.0;
        for (const auto& [x, y] : pts)
            meanDist += std::hypot(x - n.cx, y - n.cy) / count;
        n.scale = meanDist > 1e-12 ? std::sqrt(2.0) / meanDist : 1.0;
        return n;
    }
    std::pair<double, double> apply(double x, double y) const
    {
        return {scale * (x - cx), scale * (y - cy)};
    }
};

/// Smallest-singular-vector DLT homography over the given correspondences.
/// Returns the 3x3 matrix row-major with h33 normalized to 1, plus kappa.
inline bool dltHomography(const std::vector<std::pair<double, double>>& src,
                          const std::vector<std::pair<double, double>>& dst,
                          std::array<double, 9>& h, double& kappa)
{
    const size_t n = src.size();
    if (n < 4)
        return false;

    HartleyNormalizer srcNorm = HartleyNormalizer::fit(src);
    HartleyNormalizer dstNorm = HartleyNormalizer::fit(dst);

    Matrix m;
    m.rows = std::max(static_cast<int>(2 * n), 9); // pad the minimal 4-pair case to 9 rows
    m.cols = 9;
    m.a.assign(static_cast<size_t>(m.rows) * 9, 0.0);
    for (size_t i = 0; i < n; ++i) {
        const auto [u, v] = srcNorm.apply(src[i].first, src[i].second);
        const auto [x, y] = dstNorm.apply(dst[i].first, dst[i].second);
        double* r0 = &m.a[(2 * i) * 9];
        double* r1 = &m.a[(2 * i + 1) * 9];
        r0[0] = u; r0[1] = v; r0[2] = 1.0; r0[6] = -x * u; r0[7] = -x * v; r0[8] = -x;
        r1[3] = u; r1[4] = v; r1[5] = 1.0; r1[6] = -y * u; r1[7] = -y * v; r1[8] = -y;
    }

    std::vector<double> sigma;
    std::vector<double> v;
    if (!jacobiSvd(m, sigma, v))
        return false;
    const double sigmaMax = sigma[0];
    // For correspondences taken from a genuine homography the 9-column DLT
    // matrix always has the true h in its null space, so its rank is at most
    // 8 (and exactly 8 for the minimal padded 4-pair case). Degenerate
    // configurations (collinear triplets, duplicated points) drop the rank
    // below 8 — fail closed there, and report kappa over the first 8 values.
    int nonZeroSingularValues = 0;
    for (int j = 0; j < 8; ++j) {
        if (sigma[j] > kSvdRelTruncation * sigmaMax)
            ++nonZeroSingularValues;
    }
    if (nonZeroSingularValues < 8)
        return false;
    kappa = sigmaMax / sigma[7];

    std::array<double, 9> hNorm{};
    for (int k = 0; k < 9; ++k)
        hNorm[k] = v[static_cast<size_t>(k) * 9 + 8];
    if (std::abs(hNorm[8]) < 1e-300)
        return false;
    for (double& entry : hNorm)
        entry /= hNorm[8];

    // Denormalize: H = T_dst⁻¹ · H_norm · T_src.
    const auto sim = [](const HartleyNormalizer& nrm, bool inverse) {
        std::array<double, 9> t{};
        if (!inverse)
            t = {nrm.scale, 0, -nrm.scale * nrm.cx, 0, nrm.scale, -nrm.scale * nrm.cy, 0, 0, 1};
        else
            t = {1.0 / nrm.scale, 0, nrm.cx, 0, 1.0 / nrm.scale, nrm.cy, 0, 0, 1};
        return t;
    };
    const auto mul3 = [](const std::array<double, 9>& A, const std::array<double, 9>& B) {
        std::array<double, 9> C{};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                double sum = 0.0;
                for (int k = 0; k < 3; ++k)
                    sum += A[r * 3 + k] * B[k * 3 + c];
                C[r * 3 + c] = sum;
            }
        return C;
    };
    const std::array<double, 9> H = mul3(mul3(sim(dstNorm, true), hNorm), sim(srcNorm, false));
    if (std::abs(H[8]) < 1e-300)
        return false;
    h = H;
    for (double& entry : h)
        entry /= h[8];
    return true;
}

/// Forward reprojection error of a homography.
inline double homographyReprojection(const std::array<double, 9>& h,
                                     const std::pair<double, double>& src,
                                     const std::pair<double, double>& dst)
{
    const double w = h[6] * src.first + h[7] * src.second + h[8];
    if (std::abs(w) < 1e-12)
        return std::numeric_limits<double>::infinity();
    const double x = (h[0] * src.first + h[1] * src.second + h[2]) / w;
    const double y = (h[3] * src.first + h[4] * src.second + h[5]) / w;
    return std::hypot(x - dst.first, y - dst.second);
}

} // namespace rs::algorithms::detail

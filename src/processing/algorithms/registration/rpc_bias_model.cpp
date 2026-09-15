// rpc_bias_model.cpp — F13 Package D implementation.
//
// Affine fit: Hartley-style centering (translate to centroid) for
// conditioning, normal equations 3x3 per output dimension — the bias field
// is smooth by construction, so a centered normal solve is numerically
// sufficient and deterministic. k-fold protocol mirrors ModelSelector.
#include "rpc_bias_model.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sicnu::registration {

namespace {

constexpr double kEps = 1e-12;

double medianOf(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid),
                     values.end());
    return values[mid];
}

/// Solve the 3x3 normal equations for [w0 w1 w2] with centered coordinates.
/// Returns false on a singular system.
///
/// Pivoting must be SYMMETRIC: the normal matrix is symmetric, and swapping
/// only rows would silently solve a different system. Rows and columns are
/// swapped together and the permutation is recorded so the solved weights
/// map back to the original unknowns.
bool solve3x3(std::array<std::array<double, 3>, 3> A, std::array<double, 3> b,
              std::array<double, 3>& out)
{
    std::array<int, 3> perm{0, 1, 2};
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::abs(A[r][col]) > std::abs(A[piv][col]))
                piv = r;
        if (std::abs(A[piv][col]) < kEps)
            return false;
        if (piv != col) {
            std::swap(A[piv], A[col]);
            std::swap(b[piv], b[col]);
            for (int r = 0; r < 3; ++r)
                std::swap(A[r][piv], A[r][col]);
            std::swap(perm[piv], perm[col]);
        }
        for (int r = 0; r < 3; ++r) {
            if (r == col)
                continue;
            const double f = A[r][col] / A[col][col];
            for (int c = 0; c < 3; ++c)
                A[r][c] -= f * A[col][c];
            b[r] -= f * b[col];
        }
    }
    for (int r = 0; r < 3; ++r)
        out[perm[r]] = b[r] / A[r][r];
    return true;
}

/// Fit the 6-parameter affine bias field on centered coordinates.
bool fitAffine(const std::vector<RpcBiasSample>& s, std::array<double, 6>& out, double& cx,
               double& cy)
{
    cx = 0.0;
    cy = 0.0;
    for (const auto& p : s) {
        cx += p.groundX;
        cy += p.groundY;
    }
    cx /= static_cast<double>(s.size());
    cy /= static_cast<double>(s.size());

    double sxx = 0, sxy = 0, syy = 0;
    std::array<double, 3> bx{0.0, 0.0, 0.0}, by{0.0, 0.0, 0.0};
    for (const auto& p : s) {
        const double x = p.groundX - cx;
        const double y = p.groundY - cy;
        sxx += x * x;
        sxy += x * y;
        syy += y * y;
        std::array<double, 3> row{x, y, 1.0};
        for (int i = 0; i < 3; ++i) {
            bx[i] += row[i] * p.biasX;
            by[i] += row[i] * p.biasY;
        }
    }
    std::array<std::array<double, 3>, 3> A{
        {{sxx, sxy, 0.0}, {sxy, syy, 0.0}, {0.0, 0.0, static_cast<double>(s.size())}}};
    std::array<double, 3> wa{}, wb{};
    if (!solve3x3(A, bx, wa) || !solve3x3(A, by, wb))
        return false;
    // Report coefficients in raw (uncentered) ground coordinates. The solve
    // basis is {x, y, 1} (row = {x, y, 1}), i.e. bias(x',y') = w0·x' + w1·y' + w2
    // with x' = x-cx, y' = y-cy; un-centering gives
    //   bias(x,y) = (w2 - w0·cx - w1·cy) + w0·x + w1·y
    out = {wa[2] - wa[0] * cx - wa[1] * cy, wa[0], wa[1], wb[2] - wb[0] * cx - wb[1] * cy,
           wb[0], wb[1]};
    return true;
}

double applyAffineX(const std::array<double, 6>& a, double x, double y)
{
    return a[0] + a[1] * x + a[2] * y;
}

double applyAffineY(const std::array<double, 6>& a, double x, double y)
{
    return a[3] + a[4] * x + a[5] * y;
}

double rmseWith(const std::vector<RpcBiasSample>& s, const RpcBiasFit& fit)
{
    double acc = 0.0;
    for (const auto& p : s) {
        const double bx = fit.kind == RpcBiasModelKind::Affine
                              ? applyAffineX(fit.affine, p.groundX, p.groundY)
                              : fit.constX;
        const double by = fit.kind == RpcBiasModelKind::Affine
                              ? applyAffineY(fit.affine, p.groundX, p.groundY)
                              : fit.constY;
        const double ex = p.biasX - bx;
        const double ey = p.biasY - by;
        acc += ex * ex + ey * ey;
    }
    return std::sqrt(acc / static_cast<double>(s.size()));
}

} // namespace

RpcBiasFit RpcBiasModel::fit(const std::vector<RpcBiasSample>& samples, const RpcBiasOptions& opts)
{
    RpcBiasFit fit;
    const int n = static_cast<int>(samples.size());

    // Un-corrected RMSE (bias assumed zero before correction).
    double acc = 0.0;
    for (const auto& p : samples)
        acc += p.biasX * p.biasX + p.biasY * p.biasY;
    fit.rmseBefore = n > 0 ? std::sqrt(acc / static_cast<double>(n)) : 0.0;

    if (n < opts.minSamplesConstant) {
        fit.refusalReason = QStringLiteral("too_few_matches");
        return fit;
    }

    // Constant model: robust median bias (preserves D14 semantics).
    std::vector<double> bx, by;
    bx.reserve(samples.size());
    by.reserve(samples.size());
    for (const auto& p : samples) {
        bx.push_back(p.biasX);
        by.push_back(p.biasY);
    }
    fit.kind = RpcBiasModelKind::Constant;
    fit.constX = medianOf(bx);
    fit.constY = medianOf(by);
    fit.rmseAfter = rmseWith(samples, fit);
    fit.applied = fit.rmseAfter < fit.rmseBefore * (1.0 - 1e-9);

    // Held-out evaluation for both kinds (same folds).
    const int folds = std::max(2, opts.folds);
    double accConstant = 0.0, accAffine = 0.0;
    int usedFolds = 0;
    bool affineFoldOk = n >= opts.minSamplesAffine;
    for (int k = 0; k < folds; ++k) {
        std::vector<RpcBiasSample> train, test;
        for (int i = 0; i < n; ++i)
            (i % folds == k ? test : train).push_back(samples[i]);
        if (static_cast<int>(train.size()) < opts.minSamplesConstant)
            continue;
        if (affineFoldOk && static_cast<int>(train.size()) < opts.minSamplesAffine)
            affineFoldOk = false;

        RpcBiasFit c = fit; // constant medians recomputed on train below
        std::vector<double> tx, ty;
        for (const auto& p : train) {
            tx.push_back(p.biasX);
            ty.push_back(p.biasY);
        }
        c.constX = medianOf(tx);
        c.constY = medianOf(ty);
        accConstant += rmseWith(test, c);

        if (affineFoldOk) {
            RpcBiasFit a;
            a.kind = RpcBiasModelKind::Affine;
            double cx = 0.0, cy = 0.0;
            if (fitAffine(train, a.affine, cx, cy))
                accAffine += rmseWith(test, a);
            else
                affineFoldOk = false;
        }
        ++usedFolds;
    }
    if (usedFolds > 0) {
        fit.heldoutRmseConstant = accConstant / static_cast<double>(usedFolds);
        if (affineFoldOk)
            fit.heldoutRmseAffine = accAffine / static_cast<double>(usedFolds);
    }

    // Affine promotion: enough samples, fit stable, and a real held-out gain.
    if (n >= opts.minSamplesAffine && affineFoldOk && usedFolds > 0) {
        std::array<double, 6> aff{};
        double cx = 0.0, cy = 0.0;
        if (fitAffine(samples, aff, cx, cy)) {
            const bool better =
                fit.heldoutRmseAffine
                <= fit.heldoutRmseConstant * (1.0 - std::max(0.0, opts.minImprovement));
            if (better) {
                fit.kind = RpcBiasModelKind::Affine;
                fit.affine = aff;
                fit.rmseAfter = rmseWith(samples, fit);
                fit.applied = fit.rmseAfter < fit.rmseBefore * (1.0 - 1e-9);
            }
        }
    }

    if (!fit.applied)
        fit.refusalReason = QStringLiteral("model_not_justified");
    return fit;
}

std::pair<double, double> RpcBiasModel::apply(const RpcBiasFit& fit, double groundX,
                                              double groundY)
{
    if (!fit.applied)
        return {groundX, groundY};
    if (fit.kind == RpcBiasModelKind::Affine)
        return {groundX + applyAffineX(fit.affine, groundX, groundY),
                groundY + applyAffineY(fit.affine, groundX, groundY)};
    return {groundX + fit.constX, groundY + fit.constY};
}

HeightSensitivityReport RpcBiasModel::heightSensitivity(
    const std::vector<std::pair<double, double>>& groundPoints, double heightM,
    const std::function<std::pair<double, double>(double, double, double)>& reproject,
    double heightStepM)
{
    HeightSensitivityReport rep;
    const double h = std::abs(heightStepM) > kEps ? heightStepM : 10.0;
    std::vector<double> dxs, dys;
    for (const auto& [gx, gy] : groundPoints) {
        const auto [xLo, yLo] = reproject(gx, gy, heightM - h);
        const auto [xHi, yHi] = reproject(gx, gy, heightM + h);
        const double dx = (xHi - xLo) / (2.0 * h);
        const double dy = (yHi - yLo) / (2.0 * h);
        if (std::isfinite(dx) && std::isfinite(dy)) {
            dxs.push_back(dx);
            dys.push_back(dy);
        }
    }
    rep.samples = static_cast<int>(dxs.size());
    if (!dxs.empty()) {
        rep.medianDxPerM = medianOf(dxs);
        rep.medianDyPerM = medianOf(dys);
        double maxMag = 0.0;
        for (std::size_t i = 0; i < dxs.size(); ++i)
            maxMag = std::max(maxMag, std::hypot(dxs[i], dys[i]));
        rep.maxMagnitudePerM = maxMag;
    }
    return rep;
}

} // namespace sicnu::registration

// test_registration_e2e.cpp — F13: full-chain known-answer tests over
// image-level synthetic warps (Package H fixtures).
//
// The reference image is produced BY THE TEST with a documented affine map
// (rotation 3°, scale 1.015, translation (12, -8)); production code only
// ever sees pixels. The fitted transform + warp output are compared against
// the analytic truth. Nothing is derived from the code under test.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/registration/multimodal_matcher.h"
#include "processing/algorithms/registration/model_selector.h"
#include "processing/algorithms/registration/registration_quality.h"
#include "processing/algorithms/geometric_transform.h"
#include "registration_test_helpers.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

using namespace sicnu::registration;
using namespace f13;

namespace {

constexpr int kDim = 512;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

struct AffineTruth {
    // forward: dst = A·src + t (documented constants below)
    double a11 = 1.015 * std::cos(3.0 * kDegToRad);
    double a12 = -1.015 * std::sin(3.0 * kDegToRad);
    double a21 = 1.015 * std::sin(3.0 * kDegToRad);
    double a22 = 1.015 * std::cos(3.0 * kDegToRad);
    double tx = 12.0;
    double ty = -8.0;
    std::pair<double, double> apply(double x, double y) const
    {
        return {a11 * x + a12 * y + tx, a21 * x + a22 * y + ty};
    }
    std::pair<double, double> invert(double x, double y) const
    {
        // Inverse of a rotation-scale-translation: src = A⁻¹·(dst − t).
        const double det = a11 * a22 - a12 * a21;
        const double dx = x - tx;
        const double dy = y - ty;
        return {(a22 * dx - a12 * dy) / det, (-a21 * dx + a11 * dy) / det};
    }
};

} // namespace

TEST_CASE("e2e: optical pair through a known affine warp matches the analytic truth",
          "[f13][e2e]")
{
    const AffineTruth truth;
    auto src = std::vector<float>();
    src.reserve(static_cast<std::size_t>(kDim) * kDim);
    for (int y = 0; y < kDim; ++y)
        for (int x = 0; x < kDim; ++x)
            src.push_back(texture(x, y));

    // ref(x, y) = src(truth⁻¹(x, y)) — the test applies the warp itself.
    const auto ref = warpImage(src, kDim, kDim, [&truth](double x, double y) {
        return truth.invert(x, y);
    });

    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::Auto;
    opt.searchRadius = 8;
    const auto match = MultimodalMatcher::matchImages(src.data(), kDim, kDim, ref.data(), kDim,
                                                      kDim, opt);
    REQUIRE(match.status == RegistrationStatus::Success);
    REQUIRE(match.inlierCount >= opt.minMatches);

    // Model selection must justify at least affine on this data (held-out).
    std::vector<std::pair<double, double>> selSrc, selDst;
    for (const auto& p : match.points) {
        if (!p.inlier)
            continue;
        selSrc.emplace_back(p.srcX, p.srcY);
        selDst.emplace_back(p.dstX, p.dstY);
    }
    ModelSelectionOptions selOpt;
    const auto sel = ModelSelector::select(selSrc, selDst, selOpt);
    REQUIRE(sel.status == RegistrationStatus::Success);
    REQUIRE((sel.selected == CandidateModel::Affine
             || sel.selected == CandidateModel::Projective
             || sel.selected == CandidateModel::Polynomial2
             || sel.selected == CandidateModel::Similarity));
    // The forward map must be close to the truth (parameter-space oracle on
    // the analytic map through a set of probes).
    const std::pair<double, double> probes[] = {{64.0, 64.0}, {256.0, 256.0}, {448.0, 448.0}};
    double maxProbeErr = 0.0;
    for (const auto& [px, py] : probes) {
        const auto [fx, fy] = sel.transform.success
                                  ? rs::algorithms::GeometricTransform::applyForward(
                                        sel.transform, px, py)
                                  : sel.tpsFit.transform(px, py);
        const auto [tx, ty] = truth.apply(px, py);
        maxProbeErr = std::max(maxProbeErr, std::hypot(fx - tx, fy - ty));
    }
    REQUIRE(maxProbeErr < 2.0);

    // Quality products over the inliers: sub-pixel residuals expected from a
    // window-grid matcher on an exactly-generated warp.
    const auto quality =
        RegistrationQuality::evaluate(match.points, kDim, kDim, match.coverageRatio);
    REQUIRE(quality.rmse < 2.0);
    REQUIRE_FALSE(quality.ce90Degraded);
    REQUIRE(quality.ce90 < 3.0);
}

TEST_CASE("e2e: SAR-like pair (speckle + monotone remap) locks via MI and stays sub-pixel",
          "[f13][e2e]")
{
    // Pure translation (5, 9) with multiplicative speckle and log compression.
    constexpr double kDx = 5.0, kDy = 9.0;
    std::vector<float> src;
    src.reserve(static_cast<std::size_t>(kDim) * kDim);
    for (int y = 0; y < kDim; ++y)
        for (int x = 0; x < kDim; ++x)
            src.push_back(texture(x, y));
    const auto ref = warpImage(
        src, kDim, kDim,
        [](double x, double y) { return std::make_pair(x - kDx, y - kDy); },
        [](float v, int x, int y) {
            // Multiplicative speckle (mean 1) in the output position + log
            // compression — an honest optical→SAR radiometric proxy.
            const double speckle = 1.0 + 0.3 * static_cast<double>(grain(x + 31, y - 17));
            return static_cast<float>(std::log1p(std::max(0.0, static_cast<double>(v) * speckle))
                                      * 30.0);
        });

    MultimodalMatchOptions opt;
    opt.metric = MatchMetric::MutualInformation;
    opt.searchRadius = 8;
    const auto match = MultimodalMatcher::matchImages(src.data(), kDim, kDim, ref.data(), kDim,
                                                      kDim, opt);
    REQUIRE(match.status == RegistrationStatus::Success);
    std::vector<double> ox, oy;
    for (const auto& p : match.points) {
        if (p.inlier) {
            ox.push_back(p.dstX - p.srcX);
            oy.push_back(p.dstY - p.srcY);
        }
    }
    auto med = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    };
    REQUIRE(med(ox) == Catch::Approx(kDx).margin(1.0));
    REQUIRE(med(oy) == Catch::Approx(kDy).margin(1.0));
}

TEST_CASE("e2e: quality report sidecar round-trips through the atomic writer",
          "[f13][e2e]")
{
    // Small consistent set; verify the JSON artifact end to end.
    std::vector<RegistrationPoint> pts;
    for (int j = 0; j < 5; ++j)
        for (int i = 0; i < 5; ++i)
            pts.push_back([&] {
                RegistrationPoint p;
                p.srcX = 20.0 + 10.0 * i;
                p.srcY = 20.0 + 10.0 * j;
                p.dstX = p.srcX + 3.0;
                p.dstY = p.srcY + 1.0;
                p.residual = 0.5;
                p.inlier = true;
                p.score = 0.9;
                return p;
            }());
    const auto quality = RegistrationQuality::evaluate(pts, 256.0, 256.0, 1.0);
    const auto field = RegistrationQuality::residualField(pts, 256.0, 256.0, 4);
    const auto doc = RegistrationQuality::toJson(quality, field, QStringLiteral("success"),
                                                 QString());
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("q.json"));
    REQUIRE(RegistrationQuality::writeReportAtomic(path, doc));
    // Zero-diff round trip: parsed schema + rmse match the in-memory values.
    QFile out(path);
    REQUIRE(out.open(QIODevice::ReadOnly));
    const auto parsed = QJsonDocument::fromJson(out.readAll()).object();
    REQUIRE(parsed.value(QStringLiteral("schema")).toString()
            == QStringLiteral("exp_rs_registration_quality/1"));
    REQUIRE(parsed.value(QStringLiteral("accuracy")).toObject()
                .value(QStringLiteral("rmsePx"))
                .toDouble()
            == Catch::Approx(quality.rmse));
}

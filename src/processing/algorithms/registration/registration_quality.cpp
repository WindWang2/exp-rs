// registration_quality.cpp — F13 Package F implementation.
#include "registration_quality.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace sicnu::registration {

namespace {
// Rayleigh 90% quantile factor for bivariate normal errors: sqrt(-2 ln 0.1).
constexpr double kCe90NormalFactor = 2.1460183666010975;
} // namespace

RegistrationQualityReport RegistrationQuality::evaluate(const std::vector<RegistrationPoint>& points,
                                                        double imageWidth, double imageHeight,
                                                        double coverageRatio,
                                                        const RegistrationQualityOptions& options)
{
    RegistrationQualityReport rep;
    rep.coverageRatio = coverageRatio;

    std::vector<double> radial;
    double sumX = 0.0, sumY = 0.0, sqAcc = 0.0;
    for (const auto& p : points) {
        if (!p.inlier)
            continue;
        const double r = p.residual; // callers fill residual from the consensus fit
        radial.push_back(r);
        sqAcc += r * r;
        ++rep.inlierCount;
    }
    rep.candidateCount = static_cast<int>(points.size());
    if (rep.inlierCount == 0) {
        rep.localConfidence.assign(points.size(), 0.0);
        return rep;
    }
    rep.rmse = std::sqrt(sqAcc / static_cast<double>(rep.inlierCount));

    // Mean residual vector needs signed components; RegistrationPoint carries
    // only magnitudes, so recompute from the homography-free definition:
    // callers store residual as magnitude; direction recovery uses dst-src
    // minus the robust median shift (the "internal" residual direction).
    {
        std::vector<double> ox, oy;
        for (const auto& p : points) {
            if (!p.inlier)
                continue;
            ox.push_back(p.dstX - p.srcX);
            oy.push_back(p.dstY - p.srcY);
        }
        auto med = [](std::vector<double> v) {
            const std::size_t mid = v.size() / 2;
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
            return v.empty() ? 0.0 : v[mid];
        };
        const double mx = med(ox), my = med(oy);
        double sx = 0.0, sy = 0.0;
        for (const auto& p : points) {
            if (!p.inlier)
                continue;
            sx += (p.dstX - p.srcX) - mx;
            sy += (p.dstY - p.srcY) - my;
        }
        rep.meanResidualX = sx / rep.inlierCount;
        rep.meanResidualY = sy / rep.inlierCount;
    }

    std::sort(radial.begin(), radial.end());
    if (rep.inlierCount >= options.ce90MinSamples) {
        // Nearest-rank empirical quantile: smallest radius containing 90% of
        // the inliers (the standard CE definition — no distribution
        // assumption, no interpolation across order statistics).
        const auto rank =
            static_cast<std::size_t>(std::ceil(0.90 * static_cast<double>(rep.inlierCount)));
        rep.ce90 = radial[std::min(rank, radial.size()) - 1];
    } else {
        rep.ce90Degraded = true;
        rep.ce90 = radial.back(); // honest: worst observed error
    }
    // Rayleigh reference: sigma is the per-axis normal sigma (MLE).
    const double sigma = std::sqrt(rep.rmse * rep.rmse / 2.0);
    rep.ce90NormalReference = kCe90NormalFactor * sigma;

    // ---- Local confidence ------------------------------------------------
    const double diag = std::hypot(std::max(1e-9, imageWidth), std::max(1e-9, imageHeight));
    const double supportRadius = std::max(1e-9, options.supportRadiusFraction * diag);
    const double residualScale = std::max(rep.rmse, 1e-6);
    rep.localConfidence.resize(points.size(), 0.0);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        if (!p.inlier) {
            rep.localConfidence[i] = 0.0;
            continue;
        }
        int support = 0;
        for (const auto& q : points) {
            if (!q.inlier)
                continue;
            if (std::hypot(q.srcX - p.srcX, q.srcY - p.srcY) <= supportRadius)
                ++support;
        }
        support = std::max(0, support - 1); // exclude self
        const double supportFactor =
            std::min(1.0, static_cast<double>(support)
                              / static_cast<double>(std::max(1, options.supportTargetCount)));
        const double residualFactor = std::exp(-p.residual / residualScale);
        const double scoreFactor = std::max(0.0, std::min(1.0, p.score));
        const double coverageFactor = std::max(0.0, std::min(1.0, coverageRatio));
        rep.localConfidence[i] =
            std::max(0.0, std::min(1.0, scoreFactor * residualFactor * supportFactor
                                         * coverageFactor));
    }
    std::vector<double> conf = rep.localConfidence;
    conf.erase(std::remove_if(conf.begin(), conf.end(),
                              [](double v) { return !(v > 0.0); }),
               conf.end());
    if (!conf.empty()) {
        const std::size_t mid = conf.size() / 2;
        std::nth_element(conf.begin(), conf.begin() + static_cast<std::ptrdiff_t>(mid),
                         conf.end());
        rep.medianConfidence = conf[mid];
    }
    return rep;
}

ResidualVectorField RegistrationQuality::residualField(const std::vector<RegistrationPoint>& points,
                                                       double imageWidth, double imageHeight,
                                                       int grid)
{
    ResidualVectorField field;
    field.grid = std::max(1, grid);
    field.extentMinX = 0.0;
    field.extentMinY = 0.0;
    field.cellWidth = std::max(1e-9, imageWidth / field.grid);
    field.cellHeight = std::max(1e-9, imageHeight / field.grid);
    field.cells.assign(static_cast<std::size_t>(field.grid) * field.grid, {});

    // First pass: median shift (robust reference), then per-cell signed means.
    std::vector<double> ox, oy;
    for (const auto& p : points) {
        if (!p.inlier)
            continue;
        ox.push_back(p.dstX - p.srcX);
        oy.push_back(p.dstY - p.srcY);
    }
    if (ox.empty())
        return field;
    auto med = [](std::vector<double> v) {
        const std::size_t mid = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
        return v[mid];
    };
    const double mx = med(ox), my = med(oy);

    std::vector<double> sumX(field.cells.size(), 0.0), sumY(field.cells.size(), 0.0);
    std::vector<int> counts(field.cells.size(), 0);
    for (const auto& p : points) {
        if (!p.inlier)
            continue;
        int gx = static_cast<int>(p.srcX / field.cellWidth);
        int gy = static_cast<int>(p.srcY / field.cellHeight);
        gx = std::max(0, std::min(field.grid - 1, gx));
        gy = std::max(0, std::min(field.grid - 1, gy));
        const auto idx = static_cast<std::size_t>(gy) * field.grid + gx;
        sumX[idx] += (p.dstX - p.srcX) - mx;
        sumY[idx] += (p.dstY - p.srcY) - my;
        ++counts[idx];
    }
    for (std::size_t i = 0; i < field.cells.size(); ++i) {
        if (counts[i] == 0)
            continue;
        field.cells[i].meanDx = sumX[i] / counts[i];
        field.cells[i].meanDy = sumY[i] / counts[i];
        field.cells[i].count = counts[i];
    }
    return field;
}

QJsonObject RegistrationQuality::toJson(const RegistrationQualityReport& report,
                                        const ResidualVectorField& field, const QString& status,
                                        const QString& reason)
{
    QJsonObject doc;
    doc.insert(QStringLiteral("schema"), QStringLiteral("exp_rs_registration_quality/1"));
    doc.insert(QStringLiteral("status"), status);
    doc.insert(QStringLiteral("reason"), reason);
    QJsonObject acc;
    acc.insert(QStringLiteral("rmsePx"), report.rmse);
    acc.insert(QStringLiteral("ce90Px"), report.ce90);
    acc.insert(QStringLiteral("ce90Degraded"), report.ce90Degraded);
    acc.insert(QStringLiteral("ce90NormalReferencePx"), report.ce90NormalReference);
    acc.insert(QStringLiteral("inlierCount"), report.inlierCount);
    acc.insert(QStringLiteral("candidateCount"), report.candidateCount);
    acc.insert(QStringLiteral("coverageRatio"), report.coverageRatio);
    acc.insert(QStringLiteral("medianConfidence"), report.medianConfidence);
    doc.insert(QStringLiteral("accuracy"), acc);

    QJsonObject vf;
    vf.insert(QStringLiteral("grid"), field.grid);
    vf.insert(QStringLiteral("cellWidth"), field.cellWidth);
    vf.insert(QStringLiteral("cellHeight"), field.cellHeight);
    QJsonArray cells;
    for (const auto& c : field.cells) {
        QJsonObject cell;
        cell.insert(QStringLiteral("dx"), c.meanDx);
        cell.insert(QStringLiteral("dy"), c.meanDy);
        cell.insert(QStringLiteral("count"), c.count);
        cells.append(cell);
    }
    vf.insert(QStringLiteral("cells"), cells);
    doc.insert(QStringLiteral("residualField"), vf);
    return doc;
}

bool RegistrationQuality::writeReportAtomic(const QString& filePath, const QJsonObject& doc)
{
    if (filePath.isEmpty())
        return false;
    const QJsonDocument json(doc);
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(json.toJson(QJsonDocument::Indented)) < 0) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

} // namespace sicnu::registration

// registration_quality.h — F13 Package F: trusted registration quality
// products.
//
// Products:
//   - Accuracy stats: radial RMSE plus CE90. CE90 is the empirical 90th
//     percentile (nearest-rank) of radial residuals (no distribution
//     assumption, DECISIONS D-009); with fewer than
//     RegistrationQualityOptions::ce90MinSamples points it is flagged
//     degraded and a Rayleigh reference value 2.146·σ is reported alongside.
//   - Residual vector field: source-extent grid of mean residual vectors —
//     the reviewer-facing "which way and how strongly is each region off".
//   - Local confidence: per-point trust in [0,1] combining match score,
//     residual scale, local inlier support, and the global coverage ratio,
//     so a spatially clustered match set cannot produce high per-point
//     confidence (Oracle #2).
//   - A schema-versioned JSON report ("exp_rs_registration_quality/1")
//     written atomically (QSaveFile) for sidecar consumption.
#pragma once

#include "registration_types.h"

#include <QJsonObject>
#include <QString>
#include <vector>

namespace sicnu::registration {

struct RegistrationQualityOptions {
    int ce90MinSamples{20};      // below: empirical CE90 flagged degraded
    int fieldGrid{8};            // residual vector field grid side
    double supportRadiusFraction{0.25}; // neighbor radius as fraction of the diagonal
    int supportTargetCount{4};   // neighbors needed for full support credit
    double coverageGrid{0.0};    // reserved; coverage comes from the matcher report
};

struct RegistrationQualityReport {
    double rmse{0.0};            // radial RMSE over inliers (px)
    double ce90{0.0};            // empirical 90th percentile radial error (px)
    bool ce90Degraded{false};    // sample count below ce90MinSamples
    double ce90NormalReference{0.0}; // Rayleigh reference 2.146·σ (px)
    double meanResidualX{0.0};
    double meanResidualY{0.0};
    int inlierCount{0};
    int candidateCount{0};
    double coverageRatio{0.0};   // passthrough from the matcher (cluster oracle)
    double medianConfidence{0.0};
    // Local confidence per input point (same order); 0 for non-inliers.
    std::vector<double> localConfidence;
};

struct ResidualVectorField {
    int grid{0};
    double extentMinX{0.0};
    double extentMinY{0.0};
    double cellWidth{0.0};
    double cellHeight{0.0};
    struct Cell {
        double meanDx{0.0};
        double meanDy{0.0};
        int count{0};
    };
    std::vector<Cell> cells; // grid*grid, row-major over the source extent
};

class RegistrationQuality {
  public:
    /// Compute quality products from matcher points (inliers only feed the
    /// stats; candidates feed the counts). imageWidth/Height anchor the
    /// vector field and the support radius. coverageRatio is the matcher's
    /// full-extent coverage — the clustered-GCP oracle input.
    static RegistrationQualityReport evaluate(const std::vector<RegistrationPoint>& points,
                                              double imageWidth, double imageHeight,
                                              double coverageRatio,
                                              const RegistrationQualityOptions& options = {});

    /// Residual vector field over the full source extent (grid² cells).
    static ResidualVectorField residualField(const std::vector<RegistrationPoint>& points,
                                             double imageWidth, double imageHeight,
                                             int grid = 8);

    /// Schema-versioned JSON document for sidecar/agent consumption.
    static QJsonObject
    toJson(const RegistrationQualityReport& report, const ResidualVectorField& field,
           const QString& status, const QString& reason);

    /// Atomic JSON sidecar write (QSaveFile: temp + rename in directory).
    /// Returns false on any open/write/commit failure without leaving a
    /// partial file.
    static bool writeReportAtomic(const QString& filePath, const QJsonObject& doc);
};

} // namespace sicnu::registration

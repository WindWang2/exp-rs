// spatial_compare_projection.h — Spatial Comparison VM (Product C).
// Reuses study_spatial; grid mismatch = typed refusal, never silent resample.
#pragma once

#include "experiment_studio/studio_errors.h"
#include "experiment_studio/studio_types.h"
#include "study/study_spatial.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::experiment_studio
{

enum class SpatialCompareMode
{
    SideBySide,
    Swipe,
    DiffSummary,
    ClassTransition, ///< categorical transition table when both are class maps
};

QString spatialCompareModeToString( SpatialCompareMode mode );

struct SpatialCompareViewModel
{
    QString schema = QString::fromUtf8( kSpatialCompareVmSchema );
    QString leftPointId;
    QString rightPointId;
    QString leftPath;
    QString rightPath;
    SpatialCompareMode mode = SpatialCompareMode::DiffSummary;
    bool ok = false;
    bool gridMismatch = false;
    QString refuseCode;    ///< e.g. study.spatial_mismatch / experiment_studio.*
    QString refuseMessage;
    QStringList alignmentWorkflowHints; ///< explicit next steps when mismatched
    sicnu::study::SpatialDifferenceSummary summary;
    /// Sampled preview metadata only (never full-pixel UI scan).
    int previewSampleCap = 65536;
    bool previewUsedSampling = false;
    QStringList issues;

    QJsonObject toJson() const;
};

/// Project a successful spatial summary into the VM.
SpatialCompareViewModel projectSpatialSummary( const QString &leftPointId,
                                               const QString &rightPointId,
                                               const QString &leftPath,
                                               const QString &rightPath,
                                               const sicnu::study::SpatialDifferenceSummary &summary,
                                               SpatialCompareMode mode = SpatialCompareMode::DiffSummary,
                                               const StudioResourcePolicy &policy = defaultResourcePolicy() );

/// Project a typed refusal (grid mismatch or other). Never invents a resampled overlay.
SpatialCompareViewModel projectSpatialRefusal( const QString &leftPointId,
                                               const QString &rightPointId,
                                               const QString &leftPath,
                                               const QString &rightPath,
                                               const QString &code,
                                               const QString &message,
                                               SpatialCompareMode mode = SpatialCompareMode::DiffSummary );

/// Invoke summarizer port; maps study.spatial_mismatch to refusal VM.
Result<SpatialCompareViewModel> compareSpatialOutputs(
    const QString &leftPointId,
    const QString &rightPointId,
    const QString &leftPath,
    const QString &rightPath,
    const sicnu::study::ISpatialDifferenceSummarizer &summarizer,
    SpatialCompareMode mode = SpatialCompareMode::DiffSummary,
    const StudioResourcePolicy &policy = defaultResourcePolicy() );

} // namespace sicnu::experiment_studio

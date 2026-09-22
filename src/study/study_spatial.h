// study_spatial.h — spatial difference summaries for study runs (RS14-07).
//
// Teaching intent: when a parameter changes, students must see HOW the spatial
// result moved — not only the scalar metrics. This module produces a bounded,
// versioned summary of run-output vs baseline-output rasters.
//
// Layer honesty:
//   - the change-detection KERNELS remain the authority of
//     src/processing/algorithms (signed/normalized/ratio/CVA…); those live
//     behind the heavy sicnu_processing layer (qgis_core). This module
//     computes a REPORT-LEVEL absolute-difference summary (share of changed
//     pixels, mean/max/rms |a−b|) — it never feeds an operator chain and
//     never claims to be a change-detection product;
//   - NaN in either input marks a pixel invalid (IEEE 754 propagation,
//     matching the house kernel convention);
//   - mismatched grids are TYPED refusals (study.spatial_mismatch), never a
//     silent best-effort overlay.
#pragma once

#include "data/data_result.h"

#include <QJsonObject>
#include <QString>

namespace sicnu::study
{

using sicnu::data::Diagnostic;
using sicnu::data::Result;

/// Bump when the serialized summary layout changes.
inline constexpr int kSpatialSummarySchemaVersion = 1;

/// Upper bound on pixels the GDAL summarizer buffers whole (two float
/// vectors = 8 bytes/pixel ≈ 2 GiB at the cap). Beyond it the module refuses
/// with study.spatial_too_large instead of throwing bad_alloc past the
/// Result model; larger mosaics are a tiling task, not a bigger buffer.
inline constexpr qint64 kMaxSpatialComparePixels = 1LL << 28;

struct SpatialDifferenceSummary
{
    QString baselinePath;
    QString runPath;
    qint64 totalPixels = 0;
    qint64 validPixels = 0;    ///< finite in BOTH rasters
    qint64 changedPixels = 0;  ///< valid and |a−b| > epsilon
    double changedPercent = 0.0; ///< 100 * changed / valid (0 when no valid pixels)
    double meanAbsDiff = 0.0;    ///< mean |a−b| over valid pixels
    double maxAbsDiff = 0.0;     ///< max |a−b| over valid pixels
    double rmsDiff = 0.0;        ///< sqrt(mean (a−b)²) over valid pixels

    QJsonObject toJson() const;
    static Result<SpatialDifferenceSummary> fromJson( const QJsonObject &json );

    friend bool operator==( const SpatialDifferenceSummary &,
                            const SpatialDifferenceSummary & ) = default;
};

/// Buffer-level summary (pure; NaN marks invalid). Both buffers hold exactly
/// @p count samples.
SpatialDifferenceSummary summarizeBufferDifference( const QString &baselinePath,
                                                    const QString &runPath,
                                                    const float *baseline,
                                                    const float *run, qint64 count,
                                                    double epsilon );

/// The summarizer port. The report composes these summaries; tests inject
/// fakes; production reads GeoTIFFs (see GdalRasterDifferenceSummarizer).
class ISpatialDifferenceSummarizer
{
  public:
    virtual ~ISpatialDifferenceSummarizer() = default;

    virtual Result<SpatialDifferenceSummary> summarize( const QString &baselinePath,
                                                        const QString &runPath ) const = 0;
};

/// Production adapter: band 1 of each raster, GDAL nodata (and any non-finite)
/// treated as invalid. Refuses with study.spatial_mismatch when the grids
/// differ (dimensions, geotransform or CRS) — realignment is a registration
/// task, not a study-side guess.
class GdalRasterDifferenceSummarizer : public ISpatialDifferenceSummarizer
{
  public:
    explicit GdalRasterDifferenceSummarizer( double epsilon = 0.0 );

    Result<SpatialDifferenceSummary> summarize( const QString &baselinePath,
                                                const QString &runPath ) const override;

  private:
    double m_epsilon = 0.0;
};

} // namespace sicnu::study

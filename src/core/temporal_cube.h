/***************************************************************************
  core/temporal_cube.h
  Temporal Phenology Timeline Studio (D16) — regularized temporal virtual
  cube over per-scene acquisitions.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  A lazy, out-of-core time cube (ADR 0161): scenes stay on disk; the cube
  owns a 16-day regular calendar (t_k = epoch + k·Δt), a bounded LRU pool of
  256×256 spatial tiles (≤ 10 composed tiles resident, ≈ 12 MB each), and a
  best-pixel / weighted-mean compositing policy over the observations inside
  each node's ±maxWindowDays window:

      Q_i = (1 − cloud_i) · exp( −(t_i − t_k)² / 2σ_t² ),  σ_t = W/2

  No-fake-interpolation rule (ADR 0161): a grid node with no valid observation
  inside its window, or whose bracketing acquisitions are more than 45 days
  apart, is NaN — never interpolated across the hole.

  Scene instants resolve from canonical metadata (`acquisitionTime`, ISO-8601)
  and fall back to the first ISO date in the file name (YYYY-MM-DD, YYYYMMDD,
  YYYYDDD). Scenes without a resolvable instant, with mismatched grids, or
  unsupported band layouts are refused at open (typed error text, null cube).

  Pixel access goes exclusively through the Qt-free geospatial foundation
  (`RasterReader` window reads, stored values). Band 1 carries the measurement;
  band 2, when present, is the per-pixel cloud fraction (0 = clear … 1 = opaque);
  single-band scenes are treated as clear. NoData pixels (declared or NaN) are
  never candidates.

  Include rule (ADR 0161 / DECISIONS D-160-4): this family is overload-adjacent
  to processing/algorithms/temporal/temporal_fit.h — do not include both
  headers in one translation unit.
 ***************************************************************************/

#ifndef SICNU_CORE_TEMPORAL_CUBE_H
#define SICNU_CORE_TEMPORAL_CUBE_H

#include <QDate>
#include <QString>

#include <cstddef>
#include <memory>
#include <vector>

namespace sicnu::temporal
{

/// One node of the cube's regular calendar: day offset from the epoch and
/// the corresponding ISO-8601 date (YYYY-MM-DD).
struct CalendarPoint
{
    double tDays = 0.0;  ///< day offset relative to the epoch (t_k − t0)
    QString isoDate;     ///< ISO-8601 date of the node
};

struct TemporalCubeConfig
{
    QString epochIsoDate = "2020-01-01"; ///< calendar epoch t0 (grid origin)
    int cadenceDays = 16;                ///< regular composition step Δt (days)
    double maxWindowDays = 32.0;         ///< composition window half-width W (days)
    std::size_t maxMemoryBudgetBytes = 1024ull * 1024 * 1024; ///< tile-pool budget
};

class TemporalCube
{
  public:
    /// Opens the scene set and builds the out-of-core calendar index without
    /// reading any pixel. Returns null on refusal; when @a why is non-null it
    /// receives a typed, human-readable reason. @a scenePaths order is the
    /// deterministic tie-break order for BestPixel compositing.
    static std::shared_ptr<TemporalCube> open( const std::vector<QString> &scenePaths,
                                               const TemporalCubeConfig &config,
                                               QString *why = nullptr );

    virtual ~TemporalCube() = default;

    /// The regular calendar nodes covering [tMin, tMax] of the scene set:
    /// k from ceil((tMin − t0)/Δt) through floor((tMax − t0)/Δt).
    virtual std::vector<CalendarPoint> timeline() const = 0;
    virtual int sliceCount() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;

    /// Out-of-core 3-D chunk read: time-major float plane sequence
    /// [tCount × height × width]. Non-finite values are NaN (gap nodes).
    /// Returns an empty vector when the geometry is invalid (typed contract:
    /// callers check emptiness, never out-of-range indexing).
    virtual std::vector<float> readChunk( int x, int y, int width, int height,
                                          int tStart, int tCount ) = 0;

    /// Full-regular-calendar series of one pixel (sliceCount() floats, NaN
    /// where the gap guard fires).
    virtual std::vector<float> extractPixelSeries( int x, int y ) = 0;
};

} // namespace sicnu::temporal

#endif // SICNU_CORE_TEMPORAL_CUBE_H

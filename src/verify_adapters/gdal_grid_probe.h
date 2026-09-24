// src/verify_adapters/gdal_grid_probe.h — real IGridProbe over GDAL (ADR
// 0172 provider seam, verify_context.h), built on the geospatial I/O
// foundation's RasterReader so the grid facts share the platform's one
// NoData authority (bandSentinelMatches) and its read-only open discipline.
//
// Sampling contract (the engine never walks pixels; the probe owns the
// budget): band 1 is sampled over a 3x3 lattice of native-resolution
// windows (no overview, no resampling — readWindowResampled is never used),
// each window at most samplingAxis×samplingAxis native pixels. The
// fractions are estimates over the sampled points; they are the probe's
// declared bounded observation, not a full-dataset statistic.
//
// Honoring the closed questions the engine can ask:
//   - open failure (missing file, unsupported/remote source that GDAL
//     cannot serve, VRT whose sources are gone) -> nullopt, which the
//     engine renders as Indeterminate (verify:i_artifact_unreadable);
//   - a CRS-less or authority-less raster projects crs "" — the engine
//     fails any pinned crs (a real mismatch answer, not a capability gap);
//   - sampling that cannot run (complex pixel types) yields nullopt rather
//     than NaN fractions — a non-finite fraction would poison the whole
//     report digest (the canonical writer's refusal sentinel).
//
// The probe never writes, never reprojects, never resamples, never applies
// scale/offset, and never follows a nodata "fix" — stored values only.
#pragma once

#include "verify/verify_context.h"

#include <optional>
#include <string>

namespace sicnu::verify_adapters
{

class GdalGridProbe final : public sicnu::verify::IGridProbe
{
  public:
    /// Per-axis native-pixel cap of each of the 9 sample windows.
    static constexpr int kDefaultSamplingAxis = 32;

    explicit GdalGridProbe( int samplingAxis = kDefaultSamplingAxis );

    std::optional<sicnu::verify::GridInfo> grid( const std::string &path ) override;

  private:
    int mSamplingAxis;
};

} // namespace sicnu::verify_adapters

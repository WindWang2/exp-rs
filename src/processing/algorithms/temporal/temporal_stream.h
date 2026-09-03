// src/processing/algorithms/temporal/temporal_stream.h
// TemporalTileReader — bounded-memory streaming access to a temporal
// collection (goal §12/§13).
//
// Execution shape for every temporal algorithm:
//
//   for each spatial tile (default 256x256):
//       for each scene (one date at a time):
//           readSceneBandTile() -> one tile of one band of one date
//           fold into per-pixel accumulators, DISCARD the buffer
//       write output tile bands
//
// Peak working set is O(tilePixels × activeVariables) and is INDEPENDENT of
// the date count: no T×H×W cube is ever materialized. Dataset handles are
// opened once per execution and reused across tiles (mosaic precedent); the
// reader is single-threaded by contract (GDAL dataset handles are not shared
// across threads).
//
// The reader is the single NoData/validity normalization point (goal §27):
// every value it returns is either a finite valid sample or quiet NaN —
// declared finite NoData, non-finite samples, uniformly declared scale/offset
// (applied as an explicit, preflight-checked normalization), and QA/cloud
// masked pixels (Landsat QA_PIXEL / Sentinel-2 SCL via the shared QaMask
// kernels) all collapse to NaN before any algorithm sees them.
#pragma once

#include "temporal_collection.h"
#include "temporal_preflight.h"

#include <QString>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sicnu::temporal
{

struct TemporalStreamOptions
{
  int tileWidth = 256;
  int tileHeight = 256;
  /// Apply the preflight-verified uniform scale/offset on read.
  bool applyScaleOffset = true;
  /// Fold QA/SCL cloud masks into validity (per-scene mask band when present).
  bool applyQaMasking = true;
};

/// QA bits/classes masked by default (documented quality contract):
/// Landsat QA_PIXEL fill / dilated cloud / cirrus / cloud / cloud shadow;
/// Sentinel-2 SCL no-data / saturated / cloud shadow / cloud medium /
/// cloud high / thin cirrus.
namespace temporal_mask_defaults
{
constexpr std::uint32_t kLandsatFlags = 1u << 0 | 1u << 1 | 1u << 2 | 1u << 3 | 1u << 4;
constexpr int kSclMaskedClasses[] = { 0, 1, 3, 8, 9, 10 };
} // namespace temporal_mask_defaults

class TemporalTileReader
{
public:
  /// Opens every scene once and keeps the handles for the whole execution.
  /// @a radiometry comes from runPreflight() (mask bands, scale/offset facts).
  TemporalTileReader( const TemporalCollection &collection,
                      const TemporalPreflightReport &radiometry,
                      const TemporalStreamOptions &options,
                      QString *errorMessage );
  ~TemporalTileReader();

  TemporalTileReader( const TemporalTileReader & ) = delete;
  TemporalTileReader &operator=( const TemporalTileReader & ) = delete;

  /// Closes all scene datasets (idempotent; also run by the destructor).
  void close();

  /// True when construction opened every scene successfully. A failed reader
  /// reports the reason through the constructor's errorMessage and must not
  /// be read from.
  bool isValid() const { return m_valid; }

  // --- reference grid (scene 0 of the sorted collection) ---
  int width() const { return m_width; }
  int height() const { return m_height; }
  std::array<double, 6> geoTransform() const { return m_geoTransform; }
  QString projection() const { return m_projection; }

  // --- tiling ---
  int tileWidth() const { return m_options.tileWidth; }
  int tileHeight() const { return m_options.tileHeight; }
  int tileCountX() const;
  int tileCountY() const;
  int totalTileCount() const;
  /// Tile rect in output-grid pixels for @a tileIndex (row-major).
  void tileRect( int tileIndex, int *x, int *y, int *w, int *h ) const;

  // --- scenes ---
  int sceneCount() const { return static_cast<int>( m_scenes.size() ); }
  const TemporalSceneRef &scene( int sceneIndex ) const { return m_scenes.at( sceneIndex ); }
  /// Real time interval: days from the collection reference epoch (scene 0
  /// of the sorted collection) to scene @a sceneIndex. Never array indices.
  double sceneDayOffset( int sceneIndex ) const;

  /// Resolves a role for a scene (explicit override > SICNU_BAND_ROLE >
  /// positional fallback). Returns 0 when unresolvable. Sets @a usedFallback
  /// when the positional fallback fired (callers warn once per run).
  int bandForRole( int sceneIndex, const QString &roleId, int overrideBand = 0,
                   bool *usedFallback = nullptr );

  /// Reads one tile of one band of one scene into @a out (tileWidth()×
  /// tileHeight() floats). Values are normalized per the class contract
  /// (finite-or-NaN). @a skipMasking disables QA masking for this read
  /// (e.g. when reading the QA band itself). @a skipScaleOffset disables the
  /// uniform scale/offset normalization for this read — required when the
  /// band is NOT the analysis band (GDAL scale/offset is declared PER BAND,
  /// so rescaling a raw-DN quality band with the analysis band's factors
  /// writes it in the wrong units).
  bool readSceneBandTile( int sceneIndex, int band, int tileIndex, float *out,
                          bool skipMasking = false, bool skipScaleOffset = false );

  /// Reads an ARBITRARY window (not tile-aligned) of one band of one scene
  /// into @a out (w×h floats) with the identical normalization contract.
  /// Used by ROI extraction (bbox windows). Scratch buffers grow if the
  /// window exceeds the tile size (reflected in peakSlots()).
  bool readSceneBandWindow( int sceneIndex, int band, int xOff, int yOff, int w, int h,
                            float *out, bool skipMasking = false,
                            bool skipScaleOffset = false );

  /// Reads a single pixel of one band of one scene (point time series).
  /// Same normalization contract as readSceneBandTile(). An unreadable mask
  /// sample fails closed (NaN), matching the window path.
  bool readSceneBandPixel( int sceneIndex, int band, int x, int y, float *out,
                           bool skipMasking = false, bool skipScaleOffset = false );

  // --- instrumentation (goal §48: buffer accounting, never OOM roulette) ---
  /// Float slots held inside the reader (scratch tile + native QA buffers).
  std::uint64_t internalFloatSlots() const;
  /// Maximum scratch slots ever required (tile or oversized ROI window).
  std::uint64_t peakSlots() const { return m_peakScratchSlots; }
  /// Overflow-safe working-set estimate for a tile loop with @a buffersPerPixel
  /// caller-side tile buffers plus @a accumulatorFloatsPerPixel per pixel.
  static std::uint64_t estimateWorkingSetBytes( int tileWidth, int tileHeight,
                                                std::uint64_t buffersPerPixel,
                                                std::uint64_t accumulatorFloatsPerPixel );

  const TemporalPreflightReport &preflight() const { return m_report; }

private:
  bool normalizeAndMask( int sceneIndex, int band, int x, int y, int w, int h,
                         float *values, bool skipMasking, bool skipScaleOffset );

  QVector<TemporalSceneRef> m_scenes;
  std::vector<std::unique_ptr<GdalDatasetWrapper>> m_datasets; // owning: member dtors run even when the constructor throws
  QVector<SceneRadiometry> m_radiometry;
  TemporalStreamOptions m_options;
  TemporalPreflightReport m_report;
  int m_width = 0;
  int m_height = 0;
  std::array<double, 6> m_geoTransform{};
  QString m_projection;
  AcquisitionTime m_referenceTime;
  // scratch buffers (the reader's entire steady-state memory)
  std::vector<float> m_maskFloat; // mask band read (GDAL converts any numeric dtype)
  std::vector<std::uint8_t> m_maskBytes;
  std::vector<std::uint16_t> m_qaU16;
  std::vector<std::uint8_t> m_sclU8;
  std::uint64_t m_peakScratchSlots = 0;
  bool m_valid = false;

  void ensureScratch( size_t samples, size_t maskElemSize = 2 );
};

} // namespace sicnu::temporal

/***************************************************************************
  geospatial/convert/raster_convert.h
  Geospatial I/O Foundation 4.0 — GDAL-backed conversion kernels.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Everything here delegates to the authoritative GDAL utilities
  (GDALTranslate / GDALWarp / GDALBuildOverviews / COG CreateCopy) — this
  layer owns STAGING + VALIDATION + PUBLISH and parameter policy, never the
  resampling math itself. All outputs publish atomically (staged beside the
  target, validated, then renamed).
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_RASTER_CONVERT_H
#define SICNU_GEOSPATIAL_RASTER_CONVERT_H

#include "geospatial/common.h"
#include "geospatial/cog/cog_presets.h"

#include <atomic>
#include <functional>
#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// Progress/cancel sink shared by all conversions. Cancel is cooperative:
/// the underlying GDAL utility aborts when the callback reports 0.
struct ConvertProgress
{
    std::function<void( double fraction, const std::string &message )> report;
    std::atomic<bool> *cancelFlag = nullptr;
};

struct TranslateOptions
{
    std::string outputFormat = "GTiff";
    std::vector<std::string> creationOptions;      ///< -co pairs ("NAME=VALUE")
    std::vector<int> bands;                        ///< 1-based subset; empty = all
    std::string resampling;                        ///< for -r (e.g. "bilinear") when shrinking
    int targetWidth = 0;                           ///< 0 = keep
    int targetHeight = 0;
    std::string exportCrs;                         ///< -a_srs (explicit; empty = keep, never guess)
    bool strictNoGuess = true;                     ///< anchors the no-guessing policy
};

struct WarpOptions
{
    std::string targetCrs;                         ///< REQUIRED for reproject (no guessing)
    std::string resampling = "near";
    double targetResolutionX = 0;                  ///< 0 = derive (documented in result)
    double targetResolutionY = 0;
    std::vector<double> targetBounds;              ///< minX,minY,maxX,maxY in target CRS; empty = auto
    std::string targetAlignedPixels = "NO";        ///< "YES" = -tap
    std::vector<std::string> creationOptions;
    /// Warp memory budget in bytes (mirrors -wm); 0 = GDAL default.
    std::size_t warpMemoryLimitBytes = 0;
    int multithread = 0;                           ///< 1 = -multi
};

struct TranslateResult
{
    std::string output;
    int width = 0;
    int height = 0;
    int bandCount = 0;
    Json::Value warnings;
    Json::Value toJson() const;
};

/// Format conversion / subsetting / dtype policy (gdal_translate semantics).
/// Staged + validated + atomically published.
TranslateResult translateRaster( const std::string &inputPath, const std::string &targetPath,
                                 const TranslateOptions &options, ConvertProgress *progress = nullptr );

/// Reprojection / grid alignment (gdalwarp semantics). targetCrs must be a
/// resolvable CRS string — a missing target CRS is an error, never a guess.
TranslateResult warpRaster( const std::string &inputPath, const std::string &targetPath,
                            const WarpOptions &options, ConvertProgress *progress = nullptr );

/// In-place overview building (gdaladdo semantics). This is the ONE sanctioned
/// in-place operation: overviews are derived data and a crash merely loses
/// them, never base pixels. Returns the built level count.
int buildOverviews( const std::string &path, const std::vector<int> &levels,
                    const std::string &resampling = "GAUSS", ConvertProgress *progress = nullptr );

/// COG production: CreateCopy through the COG driver with a safe preset,
/// staged + COG-validated + atomically published.
TranslateResult makeCog( const std::string &inputPath, const std::string &targetPath,
                         CogPreset preset, const std::vector<std::string> &extraCreationOptions = {},
                         ConvertProgress *progress = nullptr );

/// Vector conversion (ogr2ogr semantics through GDALVectorTranslate): format,
/// attribute filter, spatial clip box, target CRS. Atomic publish.
Json::Value vectorConvert( const std::string &inputPath, const std::string &targetPath,
                           const std::string &driverName, const std::string &layerName,
                           const std::string &targetCrs, const std::string &whereClause,
                           const std::vector<double> &clipBounds, ConvertProgress *progress = nullptr );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_RASTER_CONVERT_H

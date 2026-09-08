/***************************************************************************
 * io_operators.h — Geospatial I/O Foundation 4.0: authoritative conversion
 * and inspection operator family (`io:*`).
 *
 * Every operator is a thin JSON adapter over the Qt-free `sicnu_geospatial`
 * core (GDAL utility kernels + atomic publish + CRS/NoData policy). No GDAL
 * calls live here beyond the core's API; warp/reproject math is GDAL's own.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::io
{

/// Shared plumbing: parameter extraction, GeoError translation, progress.
class IoOperatorBase : public RSOperator
{
  public:
    std::string group() const override { return "io"; }

  protected:
    /// Translates foundation-layer GeoError into the operator error model.
    static void translateExceptions();
};

/**
 * io:translate — format conversion / subsetting (gdal_translate semantics).
 * Params: input, output, driver(="GTiff"), creationOptions[], bands[],
 *         width/height, targetCrs(=-a_srs explicit), resampling
 * Determinism: bit-exact.
 */
class IoTranslateOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:translate"; }
    std::string displayName() const override { return "Translate Raster"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:warp — reprojection / grid alignment (gdalwarp semantics).
 * Params: input, output, targetCrs (mandatory), resampling, resolution,
 *         bounds[minX,minY,maxX,maxY], targetAlignedPixels, creationOptions[]
 * Determinism: tolerance (float resampling).
 */
class IoWarpOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:warp"; }
    std::string displayName() const override { return "Warp Raster"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "tolerance"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:reproject — reproject to a declared target CRS (explicit CRS contract;
 * refuses missing dataset CRS unless the caller declares srcCrsOverride).
 */
class IoReprojectOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:reproject"; }
    std::string displayName() const override { return "Reproject Raster"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "tolerance"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:clip — crop to a declared extent (source CRS; refuses when the dataset
 * carries no CRS and no fallback policy is declared).
 */
class IoClipOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:clip"; }
    std::string displayName() const override { return "Clip Raster"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:convert_format — dataset group conversion: raster through the translate
 * kernel, vector through the streaming reader→writer contract (detected).
 */
class IoConvertFormatOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:convert_format"; }
    std::string displayName() const override { return "Convert Format"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:build_overviews — gdaladdo semantics (in-place derived data).
 */
class IoBuildOverviewsOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:build_overviews"; }
    std::string displayName() const override { return "Build Overviews"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "tolerance"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:make_cog — COG production through the COG driver + safe preset +
 * pre-publish validation. Categorical scientific products default lossless.
 */
class IoMakeCogOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:make_cog"; }
    std::string displayName() const override { return "Make COG"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:vector_convert — vector conversion with declared CRS transform,
 * attribute filter and clip box through the streaming contract.
 */
class IoVectorConvertOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:vector_convert"; }
    std::string displayName() const override { return "Convert Vector"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:inspect — canonical metadata inspection (read-only, no full scan).
 */
class IoInspectOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:inspect"; }
    std::string displayName() const override { return "Inspect Dataset"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::UnsupportedForLargeRaster; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/**
 * io:doctor — read-only structured diagnostics (readability, CRS, nodata,
 * overviews, sidecars, remote accessibility).
 */
class IoDoctorOperator final : public IoOperatorBase
{
  public:
    std::string name() const override { return "io:doctor"; }
    std::string displayName() const override { return "Data Doctor"; }
    std::string description() const override;
    std::string determinismGrade() const override { return "bit-exact"; }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::UnsupportedForLargeRaster; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::io

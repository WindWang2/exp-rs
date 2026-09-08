/***************************************************************************
  geospatial/cog/cog_validator.cpp
  Geospatial I/O Foundation 4.0 — COG structural validation.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/cog/cog_validator.h"

#include "geospatial/gdal_guard.h"

#include <gdal.h>

#include <cmath>
#include <cstring>

namespace sicnu::geo
{
namespace
{

bool isPowerOfTwo( int value )
{
  return value > 0 && ( value & ( value - 1 ) ) == 0;
}

void addCheck( Json::Value &checks, const char *name, bool pass, const std::string &detail )
{
  Json::Value check;
  check["check"] = name;
  check["verdict"] = pass ? "pass" : "fail";
  check["detail"] = detail;
  checks.append( check );
}

} // namespace

Json::Value CogValidationReport::toJson() const
{
  Json::Value json;
  json["format_version"] = 1;
  json["kind"] = "cog_validation";
  json["path"] = path;
  json["is_cog"] = isCog;
  json["checks"] = checks;
  return json;
}

CogValidationReport validateCog( const std::string &path )
{
  if ( path.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "validateCog: empty path" );

  CogValidationReport report;
  report.path = path;

  ensureGdalRegistered();
  QuietCplErrors quiet;
  GDALDatasetH handle = GDALOpenEx( path.c_str(), GDAL_OF_READONLY | GDAL_OF_RASTER, nullptr, nullptr, nullptr );
  if ( !handle )
  {
    Json::Value details;
    details["path"] = path;
    const char *lastError = CPLGetLastErrorMsg();
    if ( lastError && *lastError )
      details["gdal_error"] = lastError;
    throw GeoError( ErrorCode::OpenFailed, "validateCog: cannot open dataset: " + path, details );
  }
  GdalDatasetGuard guard( handle );

  bool allPass = true;

  GDALDriverH driver = GDALGetDatasetDriver( handle );
  const char *driverName = GDALGetDriverShortName( driver );
  const bool isTiff = driverName && std::strcmp( driverName, "GTiff" ) == 0;
  addCheck( report.checks, "gtiff_driver", isTiff,
            isTiff ? "GTiff" : std::string( "driver is " ) + ( driverName ? driverName : "?" ) );
  allPass = allPass && isTiff;

  const int width = GDALGetRasterXSize( handle );
  const int height = GDALGetRasterYSize( handle );
  const int bandCount = GDALGetRasterCount( handle );

  // Tiled base image with power-of-two block size.
  int blockX = 0;
  int blockY = 0;
  bool tiled = false;
  if ( bandCount > 0 )
  {
    GDALRasterBandH band = GDALGetRasterBand( handle, 1 );
    GDALGetBlockSize( band, &blockX, &blockY );
    tiled = isPowerOfTwo( blockX ) && isPowerOfTwo( blockY ) && blockX >= 128 && blockY >= 128;
  }
  // The COG spec permits strip layout only for images smaller than one tile.
  const bool tiny = width <= 512 && height <= 512;
  const bool tilingOk = tiled || tiny;
  addCheck( report.checks, "tiling", tilingOk,
            tiled ? "block " + std::to_string( blockX ) + "x" + std::to_string( blockY )
                  : "untiled but within the small-image allowance" );
  allPass = allPass && tilingOk;

  // Overviews down to roughly one tile, themselves tiled.
  int overviewCount = 0;
  bool overviewsTiled = true;
  int smallestX = width;
  int smallestY = height;
  if ( bandCount > 0 )
  {
    GDALRasterBandH band = GDALGetRasterBand( handle, 1 );
    overviewCount = GDALGetOverviewCount( band );
    for ( int i = 0; i < overviewCount; ++i )
    {
      GDALRasterBandH overview = GDALGetOverview( band, i );
      if ( !overview )
        continue;
      int ox = 0;
      int oy = 0;
      GDALGetBlockSize( overview, &ox, &oy );
      if ( !isPowerOfTwo( ox ) || !isPowerOfTwo( oy ) || ox < 128 || oy < 128 )
        overviewsTiled = false;
      smallestX = GDALGetRasterBandXSize( overview );
      smallestY = GDALGetRasterBandYSize( overview );
    }
  }
  const bool needsOverviews = width > 512 || height > 512;
  bool overviewsOk = true;
  std::string overviewDetail = "image within one tile, overviews optional";
  if ( needsOverviews )
  {
    const bool covered = overviewCount > 0 && ( smallestX <= 512 || smallestY <= 512 );
    overviewsOk = covered && overviewsTiled;
    overviewDetail = covered
                       ? ( overviewsTiled ? "levels down to one tile, all tiled"
                                          : "present but some overview levels are untiled" )
                       : "missing for a large image";
  }
  addCheck( report.checks, "overviews", overviewsOk, overviewDetail );
  allPass = allPass && overviewsOk;

  // Compression declared (COG driver always compresses; plain GTiff may not).
  const char *compression = GDALGetMetadataItem( handle, "COMPRESSION", "IMAGE_STRUCTURE" );
  const bool compressionOk = compression != nullptr && *compression != '\0';
  addCheck( report.checks, "compression", compressionOk,
            compressionOk ? compression : "no IMAGE_STRUCTURE COMPRESSION" );

  // Float bands benefit (COG spec recommends) from a predictor; detectable
  // via the PREDICTOR tag mirror in metadata when the producer set it.
  if ( bandCount > 0 )
  {
    GDALRasterBandH band = GDALGetRasterBand( handle, 1 );
    const GDALDataType dtype = GDALGetRasterDataType( band );
    const bool isFloat = dtype == GDT_Float32 || dtype == GDT_Float64;
    const char *predictor = GDALGetMetadataItem( handle, "PREDICTOR", "IMAGE_STRUCTURE" );
    if ( isFloat && ( !predictor || std::strcmp( predictor, "3" ) != 0 ) )
    {
      addCheck( report.checks, "float_predictor", false,
                predictor ? predictor : "float data without PREDICTOR=3 (recommended)" );
      // Advisory-level: the COG spec marks the predictor as recommended, so a
      // missing predictor does not by itself disqualify the file.
    }
    else
    {
      addCheck( report.checks, "float_predictor", true,
                predictor ? predictor : "not applicable" );
    }
  }

  report.isCog = allPass;
  return report;
}

} // namespace sicnu::geo

// src/agent/spatial_tools/raster_inspect_tool.cpp
#include "raster_inspect_tool.h"
#include <limits>
#include <vector>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <QFileInfo>

#include "qgsdatasourceresolver.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace sicnu::agent::spatial_tools {

namespace {

std::string bandMetadataItem( GDALRasterBand *band, const char *key )
{
  const char *value = band->GetMetadataItem( key );
  return value ? std::string( value ) : std::string();
}

std::string datasetMetadataItem( GDALDataset *ds, const char *key )
{
  const char *value = ds->GetMetadataItem( key );
  return value ? std::string( value ) : std::string();
}

/// Bounded decimated-window statistics read in the band's native sample type
/// (#701) and accumulated in double, so wide types keep full precision.
/// Returns an empty object when the read fails or every sample is masked.
template <typename T>
Json::Value decimatedStats( GDALRasterBand *band, GDALDataType eType,
                            int w, int h, int bufW, int bufH )
{
  Json::Value stats( Json::objectValue );
  std::vector<T> buf( static_cast<size_t>( bufW ) * bufH );
  if ( band->RasterIO( GF_Read, 0, 0, w, h, buf.data(), bufW, bufH,
                       eType, 0, 0 ) != CE_None )
    return stats;

  int hasNoData = 0;
  const double nd = band->GetNoDataValue( &hasNoData );
  double sum = 0.0, sumSq = 0.0;
  double mn = std::numeric_limits<double>::infinity();
  double mx = -std::numeric_limits<double>::infinity();
  size_t n = 0;
  for ( T v : buf )
  {
    const double d = static_cast<double>( v );
    if ( !std::isfinite( d ) || ( hasNoData && d == nd ) )
      continue;
    sum += d;
    sumSq += d * d;
    mn = std::min( mn, d );
    mx = std::max( mx, d );
    ++n;
  }
  if ( n == 0 )
    return stats;
  const double mean = sum / n;
  const double variance = std::max( 0.0, sumSq / n - mean * mean );
  stats["min"] = mn;
  stats["max"] = mx;
  stats["mean"] = mean;
  stats["stddev"] = std::sqrt( variance );
  stats["approximate"] = ( bufW != w || bufH != h );
  return stats;
}

Json::Value bandStats( GDALRasterBand *band )
{
  Json::Value stats( Json::objectValue );
  double minVal = 0.0, maxVal = 0.0, meanVal = 0.0, stdDev = 0.0;
  // force=false (#634): force=true computed exact statistics over every
  // pixel of a huge overview-less raster ON THE MAIN THREAD. Prefer the
  // cached/PAM statistics (or overview-derived approximations) - they are
  // the bounded-cost answer.
  CPLErr err = band->GetStatistics( /*approx_ok=*/true, /*force=*/false,
                                    &minVal, &maxVal, &meanVal, &stdDev );
  if ( err == CE_None )
  {
    stats["min"] = minVal;
    stats["max"] = maxVal;
    stats["mean"] = meanVal;
    stats["stddev"] = stdDev;
    // #701: cached/PAM statistics may be overview-derived; flag them the
    // same way the decimated path does so consumers see one honest contract.
    stats["approximate"] = true;
    return stats;
  }

  // Nothing cached: compute from a BOUNDED decimated window (<= 512x512
  // samples) instead of the full scene - documented as approximate.
  const int w = band->GetXSize();
  const int h = band->GetYSize();
  constexpr int kMaxSamples = 512;
  int bufW = w, bufH = h;
  if ( w > kMaxSamples || h > kMaxSamples )
  {
    const double scale = std::min( static_cast<double>( kMaxSamples ) / w,
                                   static_cast<double>( kMaxSamples ) / h );
    bufW = std::max( 1, static_cast<int>( w * scale ) );
    bufH = std::max( 1, static_cast<int>( h * scale ) );
  }

  // Read in the band's OWN data type (#701): forcing GDT_Float32 silently
  // narrowed Float64/Int16/UInt16/Int32 rasters (Float64 lost ~7 significant
  // digits before any statistics ran), so reported min/max/mean did not match
  // the data an operator would actually read.
  const GDALDataType eType = band->GetRasterDataType();
  switch ( eType )
  {
    case GDT_Byte: return decimatedStats<GByte>( band, eType, w, h, bufW, bufH );
    case GDT_UInt16: return decimatedStats<GUInt16>( band, eType, w, h, bufW, bufH );
    case GDT_Int16: return decimatedStats<GInt16>( band, eType, w, h, bufW, bufH );
    case GDT_UInt32: return decimatedStats<GUInt32>( band, eType, w, h, bufW, bufH );
    case GDT_Int32: return decimatedStats<GInt32>( band, eType, w, h, bufW, bufH );
    case GDT_Float32: return decimatedStats<float>( band, eType, w, h, bufW, bufH );
    case GDT_Float64: return decimatedStats<double>( band, eType, w, h, bufW, bufH );
    default:
      // Complex / future types: narrow to Float32 as before rather than fail.
      return decimatedStats<float>( band, GDT_Float32, w, h, bufW, bufH );
  }
}

} // namespace

std::string RasterInspectTool::description() const
{
  return "Inspect a raster file (GeoTIFF, ENVI, Sentinel-2/Landsat products, "
         "any GDAL-readable source) without loading pixels into memory: size, "
         "band count, CRS, pixel size, extent, per-band data type, nodata, "
         "semantic band roles (SICNU_BAND_ROLE), wavelength/FWHM, product "
         "metadata, radiometric state, and optional per-band statistics "
         "(stats=true). Use this before choosing algorithms or building "
         "workflows.";
}

std::vector<std::string> RasterInspectTool::tags() const
{
  return { "spatial", "raster", "metadata", "inspection", "bands", "crs", "resolution", "statistics" };
}

Json::Value RasterInspectTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );
  Json::Value path( Json::objectValue );
  path["type"] = "string";
  path["description"] = "Path or GDAL virtual path (/vsi...) of the raster to inspect";
  props["path"] = path;

  Json::Value stats( Json::objectValue );
  stats["type"] = "boolean";
  stats["description"] = "Compute approximate per-band min/max/mean/stddev (default false)";
  stats["default"] = false;
  props["stats"] = stats;

  schema["properties"] = props;
  Json::Value required( Json::arrayValue );
  required.append( "path" );
  schema["required"] = required;
  return schema;
}

Json::Value RasterInspectTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value driver( Json::objectValue );
  driver["type"] = "string";
  props["driver"] = driver;

  Json::Value size( Json::objectValue );
  size["type"] = "object";
  size["properties"] = Json::Value( Json::objectValue );
  props["size"] = size;

  Json::Value bands( Json::objectValue );
  bands["type"] = "array";
  props["bands"] = bands;

  Json::Value crs( Json::objectValue );
  crs["type"] = "object";
  props["crs"] = crs;

  schema["properties"] = props;
  return schema;
}

SpatialToolResult RasterInspectTool::execute( const Json::Value &input )
{
  const std::string path = input.isMember( "path" ) ? input["path"].asString() : std::string();
  if ( path.empty() )
    return SpatialToolResult::failure( "Missing required parameter: path" );

  if ( QgsDataSourceResolver::requiresLocalExistenceCheck( QString::fromStdString( path ) ) && !QFileInfo::exists( QString::fromStdString( path ) ) )
    return SpatialToolResult::failure( "Raster file not found: " + path, "local_file_not_found", "io", false );

  GDALDatasetUniquePtr ds( GDALDataset::Open( path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY ) );
  if ( !ds )
    return SpatialToolResult::failure( "GDAL could not open raster: " + path, "gdal_open_failed", "io", false );

  const bool withStats = input.isMember( "stats" ) && input["stats"].asBool();

  Json::Value out( Json::objectValue );
  out["path"] = path;
  out["driver"] = ds->GetDriverName();

  Json::Value size( Json::objectValue );
  size["width"] = ds->GetRasterXSize();
  size["height"] = ds->GetRasterYSize();
  out["size"] = size;

  const int bandCount = ds->GetRasterCount();
  out["bandCount"] = bandCount;

  double geoTransform[6] = { 0.0 };
  const bool hasGeoTransform = ds->GetGeoTransform( geoTransform ) == CE_None;
  if ( hasGeoTransform )
  {
    const double pixelX = std::fabs( geoTransform[1] );
    const double pixelY = std::fabs( geoTransform[5] );
    Json::Value pixelSize( Json::objectValue );
    pixelSize["x"] = pixelX;
    pixelSize["y"] = pixelY;
    out["pixelSize"] = pixelSize;

    // Corner-computed extent stays correct for flipped/rotated-ish transforms.
    const double cornerX[4] = { geoTransform[0],
                                geoTransform[0] + geoTransform[1] * ds->GetRasterXSize(),
                                geoTransform[0] + geoTransform[2] * ds->GetRasterYSize(),
                                geoTransform[0]
                                    + geoTransform[1] * ds->GetRasterXSize()
                                    + geoTransform[2] * ds->GetRasterYSize() };
    const double cornerY[4] = { geoTransform[3],
                                geoTransform[3] + geoTransform[4] * ds->GetRasterXSize(),
                                geoTransform[3] + geoTransform[5] * ds->GetRasterYSize(),
                                geoTransform[3]
                                    + geoTransform[4] * ds->GetRasterXSize()
                                    + geoTransform[5] * ds->GetRasterYSize() };
    Json::Value extent( Json::objectValue );
    extent["minX"] = *std::min_element( cornerX, cornerX + 4 );
    extent["maxX"] = *std::max_element( cornerX, cornerX + 4 );
    extent["minY"] = *std::min_element( cornerY, cornerY + 4 );
    extent["maxY"] = *std::max_element( cornerY, cornerY + 4 );
    out["extent"] = extent;
  }
  else
  {
    out["pixelSize"] = Json::Value();
    out["extent"] = Json::Value();
  }

  if ( const OGRSpatialReference *srs = ds->GetSpatialRef() )
  {
    Json::Value crs( Json::objectValue );
    const char *authid = srs->GetAuthorityName( nullptr );
    const char *code = srs->GetAuthorityCode( nullptr );
    if ( authid && code )
      crs["authid"] = std::string( authid ) + ":" + code;
    char *wkt = nullptr;
    if ( srs->exportToWkt( &wkt ) == OGRERR_NONE && wkt )
    {
      crs["wkt"] = wkt;
      CPLFree( wkt );
    }
    out["crs"] = crs;
  }
  else
  {
    out["crs"] = Json::Value();
  }

  // Product / processing metadata written by the unified product importer
  // and the calibration / atmospheric-correction operators.
  for ( const char *key : { "SICNU_PRODUCT_TYPE", "SICNU_PRODUCT_ID", "SICNU_SPACECRAFT",
                            "SICNU_PROCESSING_LEVEL", "SICNU_ACQUISITION_DATE",
                            "SICNU_RADIOMETRIC_STATE" } )
  {
    const std::string value = datasetMetadataItem( ds.get(), key );
    if ( !value.empty() )
      out[key] = value;
  }

  Json::Value bands( Json::arrayValue );
  for ( int i = 1; i <= bandCount; ++i )
  {
    GDALRasterBand *band = ds->GetRasterBand( i );
    if ( !band )
      continue;

    Json::Value b( Json::objectValue );
    b["index"] = i;
    b["dataType"] = GDALGetDataTypeName( band->GetRasterDataType() );

    const std::string role = bandMetadataItem( band, "SICNU_BAND_ROLE" );
    if ( !role.empty() )
      b["role"] = role;
    const std::string wavelength = bandMetadataItem( band, "WAVELENGTH" );
    if ( !wavelength.empty() )
    {
      b["wavelength"] = wavelength;
      const std::string units = bandMetadataItem( band, "WAVELENGTH_UNITS" );
      if ( !units.empty() )
        b["wavelengthUnits"] = units;
    }
    const std::string fwhm = bandMetadataItem( band, "FWHM" );
    if ( !fwhm.empty() )
      b["fwhm"] = fwhm;

    int hasNoData = 0;
    const double nodata = band->GetNoDataValue( &hasNoData );
    if ( hasNoData )
      b["nodata"] = nodata;

    const std::string description = band->GetDescription() ? band->GetDescription() : "";
    if ( !description.empty() )
      b["description"] = description;

    if ( withStats )
      b["stats"] = bandStats( band );

    bands.append( b );
  }
  out["bands"] = bands;

  return SpatialToolResult::ok( out );
}

} // namespace sicnu::agent::spatial_tools

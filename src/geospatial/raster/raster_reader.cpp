/***************************************************************************
  geospatial/raster/raster_reader.cpp
  Geospatial I/O Foundation 4.0 — streaming raster read contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/raster/raster_reader.h"

#include "geospatial/gdal_guard.h"
#include "geospatial/util/gdal_compat.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <gdalwarper.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>

namespace sicnu::geo
{
namespace
{

constexpr std::size_t kDoubleSize = sizeof( double );

GDALDataType requireRealDataType( GDALRasterBandH band, int bandIndex )
{
  const GDALDataType type = GDALGetRasterDataType( band );
  switch ( type )
  {
    case GDT_Byte:
    case GDT_UInt16:
    case GDT_Int16:
    case GDT_UInt32:
    case GDT_Int32:
    case GDT_Float32:
    case GDT_Float64:
      return type;
    // 64-bit integers are NOT claimed exact: doubles represent integers
    // exactly only up to 2^53, so a silent conversion would be a fidelity
    // lie for full-range Int64/UInt64 rasters.
#if SICNU_GDAL_INT64_DATATYPES
    case GDT_UInt64:
    case GDT_Int64:
#endif
      throw GeoError( ErrorCode::Unsupported,
                      "Band pixel type exceeds exact double representation (64-bit integers)",
                      Json::Value( GDALGetDataTypeName( type ) ? GDALGetDataTypeName( type ) : "" ) );
    default:
    {
      Json::Value details;
      details["band"] = bandIndex;
      details["gdal_type"] = GDALGetDataTypeName( type );
      throw GeoError( ErrorCode::Unsupported,
                      "Band pixel type has no exact double representation (complex types)", details );
    }
  }
}

GDALDatasetH datasetOf( const void *handle )
{
  return static_cast<GDALDatasetH>( const_cast<void *>( handle ) );
}

/// #874: NoData matching happens in the band's STORAGE precision. Window
/// reads widen every pixel to double exactly, but a Float32 band stores
/// float-quantized values — a declared NoData that is not float-exact
/// (e.g. -9999.9) never compares equal in double space, silently marking
/// sentinel pixels valid. Narrowing BOTH sides to the stored type matches
/// what is actually on disk. NaN sentinel handling is separate and exact.
bool sentinelMatches( const BandInfo &info, double value )
{
  if ( info.noDataIsNaN )
    return std::isnan( value );
  if ( info.dtype == "Float32" )
    return static_cast<float>( value ) == static_cast<float>( info.noDataValue );
  return value == info.noDataValue;
}

} // namespace

bool clampWindowToRaster( const RasterMetadata &metadata, RasterWindow &window )
{
  const int x0 = std::max( window.xOff, 0 );
  const int y0 = std::max( window.yOff, 0 );
  const int x1 = std::min( window.xOff + window.width, metadata.width );
  const int y1 = std::min( window.yOff + window.height, metadata.height );
  if ( x0 >= x1 || y0 >= y1 )
    return false;
  window.xOff = x0;
  window.yOff = y0;
  window.width = x1 - x0;
  window.height = y1 - y0;
  return true;
}

// ---------------------------------------------------------------------------
// 5.0: tile walk planning
// ---------------------------------------------------------------------------

TileSlice TilePlan::slice( int tileX, int tileY ) const
{
  TileSlice slice;
  slice.tileX = tileX;
  slice.tileY = tileY;
  slice.xOff = window.xOff + tileX * tileWidth;
  slice.yOff = window.yOff + tileY * tileHeight;
  slice.width = std::min( tileWidth, window.xOff + window.width - slice.xOff );
  slice.height = std::min( tileHeight, window.yOff + window.height - slice.yOff );
  return slice;
}

TilePlan planTileWalk( const RasterMetadata &metadata, const RasterWindow &requestedWindow,
                       int tileWidth, int tileHeight )
{
  if ( tileWidth <= 0 || tileHeight <= 0 )
    throw GeoError( ErrorCode::InvalidArgument, "planTileWalk: tile sizes must be positive" );

  RasterWindow window = requestedWindow;
  if ( !clampWindowToRaster( metadata, window ) )
  {
    Json::Value details;
    details["window_xoff"] = requestedWindow.xOff;
    details["window_yoff"] = requestedWindow.yOff;
    throw GeoError( ErrorCode::InvalidArgument, "planTileWalk: window does not intersect the raster", details );
  }

  TilePlan plan;
  plan.tileWidth = tileWidth;
  plan.tileHeight = tileHeight;
  plan.window = window;
  plan.tilesX = ( window.width + tileWidth - 1 ) / tileWidth;
  plan.tilesY = ( window.height + tileHeight - 1 ) / tileHeight;
  return plan;
}

RasterReader RasterReader::open( const std::string &path )
{
  if ( path.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "RasterReader::open: empty path" );

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
    throw GeoError( ErrorCode::OpenFailed, "Cannot open raster: " + path, details );
  }

  RasterReader reader;
  reader.mHandle = handle;
  reader.mMetadata = inspectRaster( path );
  return reader;
}

RasterReader::~RasterReader() { close(); }

RasterReader::RasterReader( RasterReader &&other ) noexcept
  : mHandle( std::exchange( other.mHandle, nullptr ) )
  , mMetadata( std::move( other.mMetadata ) )
{
}

RasterReader &RasterReader::operator=( RasterReader &&other ) noexcept
{
  if ( this != &other )
  {
    close();
    mHandle = std::exchange( other.mHandle, nullptr );
    mMetadata = std::move( other.mMetadata );
  }
  return *this;
}

void RasterReader::close()
{
  if ( mHandle )
    GDALClose( datasetOf( mHandle ) );
  mHandle = nullptr;
}

bool RasterReader::validateWindow( const RasterMetadata &metadata, const RasterWindow &window, std::string *error )
{
  if ( window.width <= 0 || window.height <= 0 )
  {
    if ( error )
      *error = "window has degenerate size";
    return false;
  }
  if ( window.xOff < 0 || window.yOff < 0 || window.xOff + window.width > metadata.width
       || window.yOff + window.height > metadata.height )
  {
    if ( error )
      *error = "window exceeds raster extent";
    return false;
  }
  return true;
}

std::size_t RasterReader::windowByteBudget( const RasterMetadata &metadata, const RasterWindow &window,
                                            const std::vector<int> &bands )
{
  const std::size_t pixels = static_cast<std::size_t>( window.width ) * static_cast<std::size_t>( window.height );
  const std::size_t bandCount = bands.empty() ? static_cast<std::size_t>( std::max( metadata.bandCount, 0 ) ) : bands.size();
  return pixels * bandCount * kDoubleSize;
}

std::vector<double> RasterReader::readWindow( const std::vector<int> &bands, const RasterWindow &window,
                                              std::size_t maxBytes ) const
{
  std::string validationError;
  if ( !validateWindow( mMetadata, window, &validationError ) )
  {
    Json::Value details;
    details["reason"] = validationError;
    details["xoff"] = window.xOff;
    details["yoff"] = window.yOff;
    details["width"] = window.width;
    details["height"] = window.height;
    throw GeoError( ErrorCode::InvalidArgument, "readWindow: " + validationError, details );
  }

  std::vector<int> effectiveBands = bands;
  if ( effectiveBands.empty() )
  {
    effectiveBands.resize( mMetadata.bandCount );
    for ( int i = 0; i < mMetadata.bandCount; ++i )
      effectiveBands[i] = i + 1;
  }
  if ( effectiveBands.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "readWindow: raster has no bands" );

  const std::size_t effectiveMaxBytes = ( maxBytes > 0 ) ? maxBytes : kDefaultWindowBudgetBytes;
  const std::size_t requiredBytes = windowByteBudget( mMetadata, window, effectiveBands );
  if ( requiredBytes > effectiveMaxBytes )
  {
    Json::Value details;
    details["required_bytes"] = static_cast<Json::UInt64>( requiredBytes );
    details["budget_bytes"] = static_cast<Json::UInt64>( effectiveMaxBytes );
    details["max_bytes"] = static_cast<Json::UInt64>( effectiveMaxBytes );
    details["hint"] = "reduce the window or stream with bounded tiles";
    throw GeoError( ErrorCode::Unsupported, "readWindow: window read exceeds the byte budget", details );
  }

  const std::size_t pixels = static_cast<std::size_t>( window.width ) * static_cast<std::size_t>( window.height );
  std::vector<double> out( pixels * effectiveBands.size() );

  ensureGdalRegistered();
  QuietCplErrors quiet;
  GDALDatasetH dataset = datasetOf( mHandle );
  for ( std::size_t b = 0; b < effectiveBands.size(); ++b )
  {
    GDALRasterBandH band = GDALGetRasterBand( dataset, effectiveBands[b] );
    if ( !band )
    {
      Json::Value details;
      details["band"] = effectiveBands[b];
      throw GeoError( ErrorCode::InvalidArgument, "readWindow: band index out of range", details );
    }
    requireRealDataType( band, effectiveBands[b] );
    double *dst = out.data() + b * pixels;
    const CPLErr error = GDALRasterIO( band, GF_Read, window.xOff, window.yOff, window.width, window.height,
                                       dst, window.width, window.height, GDT_Float64, 0, 0 );
    if ( error != CE_None )
    {
      const char *lastError = CPLGetLastErrorMsg();
      Json::Value details;
      details["band"] = effectiveBands[b];
      if ( lastError && *lastError )
        details["gdal_error"] = lastError;
      throw GeoError( ErrorCode::IoError, "readWindow: pixel read failed", details );
    }
  }
  return out;
}

std::vector<double> RasterReader::readFull( const std::vector<int> &bands, std::size_t maxBytes ) const
{
  RasterWindow full;
  full.xOff = 0;
  full.yOff = 0;
  full.width = mMetadata.width;
  full.height = mMetadata.height;
  const std::size_t required = windowByteBudget( mMetadata, full, bands );
  if ( required > maxBytes )
  {
    Json::Value details;
    details["required_bytes"] = static_cast<Json::UInt64>( required );
    details["budget_bytes"] = static_cast<Json::UInt64>( maxBytes );
    details["hint"] = "use readWindow streaming with bounded windows";
    throw GeoError( ErrorCode::Unsupported, "readFull: whole-raster read exceeds the declared byte budget", details );
  }
  // Route through the budgeted overload so a full read can never re-fail on
  // the (smaller) default budget after the caller's explicit check.
  return readWindow( bands, full, maxBytes );
}

std::vector<std::uint8_t> RasterReader::readMask( const RasterWindow &window, const std::vector<int> &bands ) const
{
  std::string validationError;
  if ( !validateWindow( mMetadata, window, &validationError ) )
    throw GeoError( ErrorCode::InvalidArgument, "readMask: " + validationError );

  std::vector<int> effectiveBands = bands;
  if ( effectiveBands.empty() )
  {
    effectiveBands.resize( mMetadata.bandCount );
    for ( int i = 0; i < mMetadata.bandCount; ++i )
      effectiveBands[i] = i + 1;
  }
  const std::size_t pixels = static_cast<std::size_t>( window.width ) * static_cast<std::size_t>( window.height );
  std::vector<std::uint8_t> mask( pixels, 255 );

  // Band-by-band streaming: peak memory is ONE band's window, not
  // bands × window — a full-extent mask must not be a silent whole-raster
  // load at 8 bytes/pixel.
  for ( std::size_t b = 0; b < effectiveBands.size(); ++b )
  {
    const BandInfo *info = nullptr;
    for ( const BandInfo &candidate : mMetadata.bands )
    {
      if ( candidate.index == effectiveBands[b] )
      {
        info = &candidate;
        break;
      }
    }
    if ( !info || !info->hasNoData )
      continue; // undeclared NoData → band contributes no invalid pixels
    const std::vector<double> bandValues = readWindow( { effectiveBands[b] }, window );
    for ( std::size_t p = 0; p < pixels; ++p )
    {
      // Deliberate divergence from the merged master fix (which ORs in an
      // absolute 1e-6 tolerance for EVERY dtype): an epsilon blind to value
      // magnitude mis-masks legitimate Float64 values within 1e-6 of the
      // sentinel, and float-casting large integer sentinels loses precision
      // past 2^24. Matching in the band's STORAGE precision is exact for
      // Float64 and correct for Float32 quantization — see sentinelMatches.
      if ( sentinelMatches( *info, bandValues[p] ) )
        mask[p] = 0;
    }
  }
  return mask;
}

double RasterReader::applyScaleOffset( const BandInfo &band, double storedValue )
{
  const double scale = band.hasScale ? band.scale : 1.0;
  const double offset = band.hasOffset ? band.offset : 0.0;
  return storedValue * scale + offset;
}

void *RasterReader::bandHandle( int bandIndex1Based ) const
{
  if ( !mHandle || bandIndex1Based < 1 || bandIndex1Based > mMetadata.bandCount )
    return nullptr;
  return GDALGetRasterBand( datasetOf( mHandle ), bandIndex1Based );
}

// ---------------------------------------------------------------------------
// 5.0: block / tile / overview contracts
// ---------------------------------------------------------------------------

std::pair<int, int> RasterReader::blockSize( int bandIndex1Based ) const
{
  if ( !mHandle || bandIndex1Based < 1 || bandIndex1Based > mMetadata.bandCount )
    return { 0, 0 };
  int blockX = 0;
  int blockY = 0;
  GDALGetBlockSize( GDALGetRasterBand( datasetOf( mHandle ), bandIndex1Based ), &blockX, &blockY );
  return { blockX, blockY };
}

std::vector<double> RasterReader::readBlock( int bandIndex1Based, int blockX, int blockY ) const
{
  const std::pair<int, int> size = blockSize( bandIndex1Based );
  if ( size.first <= 0 || size.second <= 0 )
    throw GeoError( ErrorCode::InvalidArgument, "readBlock: band out of range or closed reader" );

  // Clamp block coordinates against the raster's block grid.
  const int blocksX = ( mMetadata.width + size.first - 1 ) / size.first;
  const int blocksY = ( mMetadata.height + size.second - 1 ) / size.second;
  if ( blockX < 0 || blockY < 0 || blockX >= blocksX || blockY >= blocksY )
  {
    Json::Value details;
    details["block_x"] = blockX;
    details["block_y"] = blockY;
    details["blocks_x"] = blocksX;
    details["blocks_y"] = blocksY;
    throw GeoError( ErrorCode::InvalidArgument, "readBlock: block coordinates out of range", details );
  }

  const int winX = blockX * size.first;
  const int winY = blockY * size.second;
  const int winW = std::min( size.first, mMetadata.width - winX );
  const int winH = std::min( size.second, mMetadata.height - winY );

  RasterWindow window;
  window.xOff = winX;
  window.yOff = winY;
  window.width = winW;
  window.height = winH;

  const std::vector<double> stored = readWindow( { bandIndex1Based }, window );

  // #790: the contract is a full blockSize() buffer. At raster edges the
  // stored window is smaller; pad to the uniform block geometry with the
  // band's declared NoData (0.0 when none declared, matching GDAL's own
  // partial-block behavior) so callers indexing by blockSize() never walk
  // off a truncated buffer.
  if ( winW == size.first && winH == size.second )
    return stored;

  const BandInfo *info = nullptr;
  for ( const BandInfo &candidate : mMetadata.bands )
  {
    if ( candidate.index == bandIndex1Based )
    {
      info = &candidate;
      break;
    }
  }
  double padValue = 0.0;
  if ( info && info->hasNoData )
    padValue = info->noDataIsNaN ? std::numeric_limits<double>::quiet_NaN() : info->noDataValue;

  std::vector<double> out( static_cast<std::size_t>( size.first ) * size.second, padValue );
  for ( int row = 0; row < winH; ++row )
  {
    double *dstRow = out.data() + static_cast<std::size_t>( row ) * size.first;
    const double *srcRow = stored.data() + static_cast<std::size_t>( row ) * winW;
    std::copy( srcRow, srcRow + winW, dstRow );
  }
  return out;
}

void RasterReader::iterateTiles( const TilePlan &plan, const std::vector<int> &bands,
                                 const std::function<void( const TileSlice &, const std::vector<double> & )> &sink,
                                 const std::function<bool()> &cancelled ) const
{
  if ( !sink )
    throw GeoError( ErrorCode::InvalidArgument, "iterateTiles: missing sink" );

  for ( int tileY = 0; tileY < plan.tilesY; ++tileY )
  {
    for ( int tileX = 0; tileX < plan.tilesX; ++tileX )
    {
      if ( cancelled && cancelled() )
        throw GeoError( ErrorCode::Cancelled, "iterateTiles: cancelled by the caller" );
      const TileSlice slice = plan.slice( tileX, tileY );
      const std::vector<double> values = readWindow( bands, { slice.xOff, slice.yOff, slice.width, slice.height } );
      sink( slice, values );
    }
  }
}

int RasterReader::overviewCount( int bandIndex1Based ) const
{
  if ( !mHandle || bandIndex1Based < 1 || bandIndex1Based > mMetadata.bandCount )
    return 0;
  return GDALGetOverviewCount( GDALGetRasterBand( datasetOf( mHandle ), bandIndex1Based ) );
}

std::vector<int> RasterReader::overviewDimensions( int bandIndex1Based ) const
{
  std::vector<int> sizes;
  if ( !mHandle || bandIndex1Based < 1 || bandIndex1Based > mMetadata.bandCount )
    return sizes;
  GDALRasterBandH band = GDALGetRasterBand( datasetOf( mHandle ), bandIndex1Based );
  const int count = GDALGetOverviewCount( band );
  sizes.reserve( static_cast<std::size_t>( count ) * 2 );
  for ( int level = 0; level < count; ++level )
  {
    GDALRasterBandH overview = GDALGetOverview( band, level );
    if ( overview == nullptr )
      continue;
    sizes.push_back( GDALGetRasterBandXSize( overview ) );
    sizes.push_back( GDALGetRasterBandYSize( overview ) );
  }
  return sizes;
}

int RasterReader::selectOverview( int bandIndex1Based, int targetWidth, int targetHeight,
                                  OverviewPolicy policy ) const
{
  if ( policy == OverviewPolicy::Auto || policy == OverviewPolicy::Exact )
    return 0; // Exact reads native; Auto defers the level choice to GDAL at IO time

  if ( targetWidth <= 0 || targetHeight <= 0 )
    throw GeoError( ErrorCode::InvalidArgument, "selectOverview: target size must be positive" );

  GDALRasterBandH band = bandHandle( bandIndex1Based ) ? static_cast<GDALRasterBandH>( bandHandle( bandIndex1Based ) )
                                                       : nullptr;
  if ( band == nullptr )
    return 0;

  // Nearest: the smallest overview still covering the request — never a
  // level that would upscale beyond the target.
  const int count = GDALGetOverviewCount( band );
  int selected = 0;
  for ( int level = 0; level < count; ++level )
  {
    GDALRasterBandH overview = GDALGetOverview( band, level );
    if ( overview == nullptr )
      break;
    const int w = GDALGetRasterBandXSize( overview );
    const int h = GDALGetRasterBandYSize( overview );
    if ( w >= targetWidth && h >= targetHeight )
      selected = level + 1; // 1-based overview level
    else
      break;
  }
  return selected;
}

std::vector<double> RasterReader::readWindowResampled( const std::vector<int> &bands, const RasterWindow &window,
                                                       int dstWidth, int dstHeight, int overviewLevel,
                                                       OverviewPolicy policy, const std::string &resampling ) const
{
  std::string validationError;
  if ( !validateWindow( mMetadata, window, &validationError ) )
    throw GeoError( ErrorCode::InvalidArgument, "readWindowResampled: " + validationError );
  if ( dstWidth <= 0 || dstHeight <= 0 )
    throw GeoError( ErrorCode::InvalidArgument, "readWindowResampled: destination size must be positive" );
  if ( dstWidth > window.width || dstHeight > window.height )
    throw GeoError( ErrorCode::InvalidArgument,
                    "readWindowResampled: upsampling is refused (declare a higher-resolution source instead)" );

  std::vector<int> effectiveBands = bands;
  if ( effectiveBands.empty() )
  {
    effectiveBands.resize( mMetadata.bandCount );
    for ( int i = 0; i < mMetadata.bandCount; ++i )
      effectiveBands[i] = i + 1;
  }

  GDALDatasetH dataset = datasetOf( mHandle );
  GDALRasterBandH firstBand = GDALGetRasterBand( dataset, effectiveBands.front() );
  if ( firstBand == nullptr )
    throw GeoError( ErrorCode::InvalidArgument, "readWindowResampled: band out of range" );

  int level = overviewLevel;
  if ( policy == OverviewPolicy::Nearest && level == 0 )
    level = selectOverview( effectiveBands.front(), dstWidth, dstHeight, OverviewPolicy::Nearest );
  if ( level > 0 && GDALGetOverview( firstBand, level - 1 ) == nullptr )
  {
    Json::Value details;
    details["level"] = level;
    details["overview_count"] = GDALGetOverviewCount( firstBand );
    throw GeoError( ErrorCode::Unsupported, "readWindowResampled: overview level does not exist", details );
  }

  GDALRIOResampleAlg alg = GRIORA_NearestNeighbour;
  if ( resampling == "bilinear" )
    alg = GRIORA_Bilinear;
  else if ( resampling == "cubic" )
    alg = GRIORA_Cubic;
  else if ( resampling == "average" )
    alg = GRIORA_Average;
  else if ( resampling == "mode" )
    alg = GRIORA_Mode;
  else if ( !resampling.empty() && resampling != "nearest" )
    throw GeoError( ErrorCode::InvalidArgument, "readWindowResampled: unknown resampling method: " + resampling );

  GDALRasterIOExtraArg extra;
  INIT_RASTERIO_EXTRA_ARG( extra );
  extra.eResampleAlg = alg;

  const std::size_t pixels = static_cast<std::size_t>( dstWidth ) * static_cast<std::size_t>( dstHeight );
  std::vector<double> out( pixels * effectiveBands.size() );

  ensureGdalRegistered();
  QuietCplErrors quiet;
  for ( std::size_t b = 0; b < effectiveBands.size(); ++b )
  {
    // Every band reads from ITS OWN overview at the requested level —
    // mixing an overview band with native bands would give systematically
    // different grids across bands of the same request.
    GDALRasterBandH band = GDALGetRasterBand( dataset, effectiveBands[b] );
    if ( level > 0 && band != nullptr )
    {
      GDALRasterBandH overview = GDALGetOverview( band, level - 1 );
      if ( overview != nullptr )
        band = overview;
    }
    if ( band == nullptr )
      throw GeoError( ErrorCode::InvalidArgument, "readWindowResampled: band out of range" );
    requireRealDataType( band, effectiveBands[b] );

    // Overview windows are the window scaled into the level's grid.
    const int srcX = level > 0 ? static_cast<int>( static_cast<double>( window.xOff ) * GDALGetRasterBandXSize( band ) / mMetadata.width ) : window.xOff;
    const int srcY = level > 0 ? static_cast<int>( static_cast<double>( window.yOff ) * GDALGetRasterBandYSize( band ) / mMetadata.height ) : window.yOff;
    const int srcW = level > 0 ? static_cast<int>( static_cast<double>( window.width ) * GDALGetRasterBandXSize( band ) / mMetadata.width ) : window.width;
    const int srcH = level > 0 ? static_cast<int>( static_cast<double>( window.height ) * GDALGetRasterBandYSize( band ) / mMetadata.height ) : window.height;

    double *dst = out.data() + b * pixels;
    const CPLErr error = GDALRasterIOEx( band, GF_Read, srcX, srcY, std::max( srcW, 1 ), std::max( srcH, 1 ),
                                         dst, dstWidth, dstHeight, GDT_Float64, 0, 0, &extra );
    if ( error != CE_None )
    {
      Json::Value details;
      details["band"] = effectiveBands[b];
      details["overview_level"] = level;
      const char *lastError = CPLGetLastErrorMsg();
      if ( lastError && *lastError )
        details["gdal_error"] = lastError;
      throw GeoError( ErrorCode::IoError, "readWindowResampled: scaled read failed", details );
    }
  }
  return out;
}

} // namespace sicnu::geo

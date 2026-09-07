/***************************************************************************
  geospatial/raster/raster_reader.cpp
  Geospatial I/O Foundation 4.0 — streaming raster read contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/raster/raster_reader.h"

#include "geospatial/gdal_guard.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 5, 0 )
    case GDT_UInt64:
    case GDT_Int64:
#endif
      return type;
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

std::vector<double> RasterReader::readWindow( const std::vector<int> &bands, const RasterWindow &window ) const
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
  return readWindow( bands, full );
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

  const std::vector<double> values = readWindow( effectiveBands, window );
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
    const double *bandValues = values.data() + b * pixels;
    for ( std::size_t p = 0; p < pixels; ++p )
    {
      const double value = bandValues[p];
      const bool invalid = info->noDataIsNaN ? std::isnan( value ) : ( value == info->noDataValue );
      if ( invalid )
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

} // namespace sicnu::geo

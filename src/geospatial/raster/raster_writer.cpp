/***************************************************************************
  geospatial/raster/raster_writer.cpp
  Geospatial I/O Foundation 4.0 — atomic raster write contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/raster/raster_writer.h"

#include "geospatial/raster/raster_reader.h"

#include "geospatial/gdal_guard.h"
#include "geospatial/util/atomic_fs.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_srs_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace sicnu::geo
{
namespace
{

GDALDataType dataTypeFromName( const std::string &name, const std::string &context )
{
  const GDALDataType type = GDALGetDataTypeByName( name.c_str() );
  if ( type == GDT_Unknown || name.empty() )
  {
    Json::Value details;
    details["dtype"] = name;
    details["context"] = context;
    throw GeoError( ErrorCode::Unsupported, "Unknown raster data type: " + name, details );
  }
  if ( type == GDT_CFloat32 || type == GDT_CFloat64 || type == GDT_CInt16 || type == GDT_CInt32 )
  {
    Json::Value details;
    details["dtype"] = name;
    throw GeoError( ErrorCode::Unsupported, "Complex pixel types are not supported by the write contract", details );
  }
  return type;
}

GDALDatasetH datasetOf( void *handle ) { return static_cast<GDALDatasetH>( handle ); }

} // namespace

RasterWriter RasterWriter::create( const std::string &targetPath, int width, int height,
                                   const std::vector<RasterBandSpec> &bands, const RasterWriteOptions &options )
{
  if ( targetPath.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "RasterWriter::create: empty target path" );
  if ( width <= 0 || height <= 0 )
  {
    Json::Value details;
    details["width"] = width;
    details["height"] = height;
    throw GeoError( ErrorCode::InvalidArgument, "RasterWriter::create: degenerate raster size", details );
  }
  if ( bands.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "RasterWriter::create: no bands declared" );

  ensureGdalRegistered();

  if ( atomic_fs::fileExists( targetPath ) && !options.overwrite )
  {
    Json::Value details;
    details["path"] = targetPath;
    details["hint"] = "set overwrite=true to replace an existing output explicitly";
    throw GeoError( ErrorCode::IoError, "RasterWriter::create: target already exists: " + targetPath, details );
  }

  GDALDriverH driver = GDALGetDriverByName( options.driver.c_str() );
  if ( !driver )
  {
    Json::Value details;
    details["driver"] = options.driver;
    throw GeoError( ErrorCode::DriverMissing, "Raster driver not available: " + options.driver, details );
  }
  // Refuse read-only-only drivers early (e.g. opening a COG for write).
  if ( !GDALGetMetadataItem( driver, GDAL_DCAP_CREATE, nullptr )
       && !GDALGetMetadataItem( driver, GDAL_DCAP_CREATECOPY, nullptr ) )
  {
    Json::Value details;
    details["driver"] = options.driver;
    throw GeoError( ErrorCode::Unsupported, "Driver cannot create datasets: " + options.driver, details );
  }

  std::vector<const char *> creationOptions;
  creationOptions.reserve( options.creationOptions.size() + 1 );
  for ( const std::string &option : options.creationOptions )
    creationOptions.push_back( option.c_str() );
  creationOptions.push_back( nullptr );

  // Validate dtypes before staging so a bad spec never leaves files behind.
  std::vector<GDALDataType> types;
  types.reserve( bands.size() );
  for ( const RasterBandSpec &band : bands )
    types.push_back( dataTypeFromName( band.dtype, targetPath ) );

  // GDALCreate carries one dtype for all bands; per-band dtypes would need a
  // CreateCopy pipeline, which the atomic create contract does not stage.
  const bool uniform = std::all_of( types.begin(), types.end(), [ first = types[0] ]( GDALDataType t ) { return t == first; } );
  if ( !uniform )
  {
    Json::Value details;
    details["hint"] = "declare one uniform dtype, or write each dtype through a separate output";
    throw GeoError( ErrorCode::Unsupported, "RasterWriter::create: mixed per-band dtypes are not supported", details );
  }

  const std::string stagedPath = atomic_fs::stagedPathFor( targetPath );
  QuietCplErrors quiet;
  GDALDatasetH handle = GDALCreate( driver, stagedPath.c_str(), width, height, static_cast<int>( bands.size() ),
                                    types[0], const_cast<char **>( creationOptions.data() ) );

  if ( !handle )
  {
    atomic_fs::discardStaged( stagedPath );
    const char *lastError = CPLGetLastErrorMsg();
    Json::Value details;
    details["path"] = stagedPath;
    if ( lastError && *lastError )
      details["gdal_error"] = lastError;
    throw GeoError( ErrorCode::WriteFailed, "RasterWriter::create: dataset creation failed", details );
  }

  // Declared metadata goes in before pixels: the transaction carries fidelity.
  for ( std::size_t i = 0; i < bands.size(); ++i )
  {
    GDALRasterBandH band = GDALGetRasterBand( handle, static_cast<int>( i ) + 1 );
    if ( !band )
      continue;
    const RasterBandSpec &spec = bands[i];
    if ( !spec.description.empty() )
      GDALSetDescription( band, spec.description.c_str() );
    if ( spec.hasNoData )
    {
      if ( spec.noDataIsNaN )
        GDALSetRasterNoDataValue( band, std::numeric_limits<double>::quiet_NaN() );
      else
        GDALSetRasterNoDataValue( band, spec.noDataValue );
    }
    if ( spec.hasScale )
      GDALSetRasterScale( band, spec.scale );
    if ( spec.hasOffset )
      GDALSetRasterOffset( band, spec.offset );
    if ( !spec.unit.empty() )
      GDALSetRasterUnitType( band, spec.unit.c_str() );
    if ( !spec.role.empty() )
      GDALSetMetadataItem( band, "SICNU_BAND_ROLE", spec.role.c_str(), nullptr );
    if ( spec.hasWavelength )
      GDALSetMetadataItem( band, "WAVELENGTH", std::to_string( spec.wavelengthNm ).c_str(), nullptr );
    if ( spec.hasFwhm )
      GDALSetMetadataItem( band, "FWHM", std::to_string( spec.fwhmNm ).c_str(), nullptr );
    if ( !spec.colorInterpretation.empty() )
    {
      const GDALColorInterp interp = GDALGetColorInterpretationByName( spec.colorInterpretation.c_str() );
      if ( interp != GCI_Undefined || spec.colorInterpretation == "Undefined" )
        GDALSetRasterColorInterpretation( band, interp );
    }
  }

  RasterWriter writer;
  writer.mHandle = handle;
  writer.mTargetPath = targetPath;
  writer.mStagedPath = stagedPath;
  writer.mWidth = width;
  writer.mHeight = height;
  writer.mBandCount = static_cast<int>( bands.size() );
  return writer;
}

RasterWriter::~RasterWriter()
{
  if ( !mFinalized )
    cancel();
}

RasterWriter::RasterWriter( RasterWriter &&other ) noexcept
  : mHandle( std::exchange( other.mHandle, nullptr ) )
  , mTargetPath( std::move( other.mTargetPath ) )
  , mStagedPath( std::move( other.mStagedPath ) )
  , mWidth( other.mWidth )
  , mHeight( other.mHeight )
  , mBandCount( other.mBandCount )
  , mFinalized( other.mFinalized )
{
  other.mFinalized = true; // moved-from must not clean up
}

RasterWriter &RasterWriter::operator=( RasterWriter &&other ) noexcept
{
  if ( this != &other )
  {
    if ( !mFinalized )
      cancel();
    mHandle = std::exchange( other.mHandle, nullptr );
    mTargetPath = std::move( other.mTargetPath );
    mStagedPath = std::move( other.mStagedPath );
    mWidth = other.mWidth;
    mHeight = other.mHeight;
    mBandCount = other.mBandCount;
    mFinalized = std::exchange( other.mFinalized, true );
  }
  return *this;
}

void RasterWriter::setGeotransform( const std::array<double, 6> &geotransform )
{
  if ( !mHandle )
    throw GeoError( ErrorCode::InvalidArgument, "setGeotransform: writer is closed" );
  QuietCplErrors quiet;
  if ( GDALSetGeoTransform( datasetOf( mHandle ), const_cast<double *>( geotransform.data() ) ) != CE_None )
    throw GeoError( ErrorCode::WriteFailed, "setGeotransform failed: driver does not support georeferencing" );
}

void RasterWriter::setCrs( const Crs &crs )
{
  if ( !mHandle )
    throw GeoError( ErrorCode::InvalidArgument, "setCrs: writer is closed" );
  if ( !crs.isValid() )
    throw GeoError( ErrorCode::InvalidCrs, "setCrs: refusing to write an invalid CRS" );
  QuietCplErrors quiet;
  char *wkt = nullptr;
  if ( OSRExportToWkt( crs.handle(), &wkt ) != OGRERR_NONE || !wkt )
  {
    if ( wkt )
      CPLFree( wkt );
    throw GeoError( ErrorCode::InvalidCrs, "setCrs: WKT export failed" );
  }
  const CPLErr error = GDALSetProjection( datasetOf( mHandle ), wkt );
  CPLFree( wkt );
  if ( error != CE_None )
    throw GeoError( ErrorCode::WriteFailed, "setCrs: driver rejected the projection" );
}

void RasterWriter::setDatasetMetadataItem( const std::string &key, const std::string &value )
{
  if ( !mHandle )
    throw GeoError( ErrorCode::InvalidArgument, "setDatasetMetadataItem: writer is closed" );
  if ( key.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "setDatasetMetadataItem: empty key" );
  GDALSetMetadataItem( datasetOf( mHandle ), key.c_str(), value.c_str(), nullptr );
}

void RasterWriter::writeWindow( int band1Based, const RasterWindow &window, const double *values )
{
  if ( !mHandle )
    throw GeoError( ErrorCode::InvalidArgument, "writeWindow: writer is closed" );
  RasterMetadata shape;
  shape.width = mWidth;
  shape.height = mHeight;
  std::string validationError;
  if ( !RasterReader::validateWindow( shape, window, &validationError ) )
    throw GeoError( ErrorCode::InvalidArgument, "writeWindow: " + validationError );
  if ( band1Based < 1 || band1Based > mBandCount )
    throw GeoError( ErrorCode::InvalidArgument, "writeWindow: band index out of range" );
  if ( !values )
    throw GeoError( ErrorCode::InvalidArgument, "writeWindow: null value buffer" );

  QuietCplErrors quiet;
  GDALRasterBandH band = GDALGetRasterBand( datasetOf( mHandle ), band1Based );
  if ( !band )
    throw GeoError( ErrorCode::InvalidArgument, "writeWindow: band disappeared" );

  // Fidelity gate: GDAL silently clamps/saturates on double→integer writes,
  // so out-of-range and NaN values must be caught BEFORE the write — a
  // silently clamped raster is a corrupt scientific product.
  const GDALDataType dtype = GDALGetRasterDataType( band );
  if ( dtype != GDT_Float64 && dtype != GDT_Float32 )
  {
    double low = 0.0;
    double high = 0.0;
    bool isInteger = true;
    switch ( dtype )
    {
      case GDT_Byte: low = 0; high = 255; break;
      case GDT_UInt16: low = 0; high = 65535; break;
      case GDT_Int16: low = -32768; high = 32767; break;
      case GDT_UInt32: low = 0; high = 4294967295.0; break;
      case GDT_Int32: low = -2147483648.0; high = 2147483647.0; break;
      default: isInteger = false; break;
    }
    if ( isInteger )
    {
      const std::size_t count = static_cast<std::size_t>( window.width ) * window.height;
      for ( std::size_t i = 0; i < count; ++i )
      {
        const double value = values[i];
        if ( std::isnan( value ) || value < low || value > high )
        {
          Json::Value details;
          details["band"] = band1Based;
          details["index"] = static_cast<Json::UInt64>( i );
          details["value"] = value;
          details["dtype"] = GDALGetDataTypeName( dtype );
          throw GeoError( ErrorCode::FidelityLoss,
                          "writeWindow: value not representable in the band's integer dtype "
                          "(stored pixels are never silently clamped)",
                          details );
        }
      }
    }
  }

  // Float32 bands: |v| beyond the float range silently narrows to ±inf (GDAL
  // narrows without error) — a sign-flipped corrupt product. Overflow is a
  // typed fidelity failure; precision loss WITHIN the float range is the
  // declared storage choice and is not gated.
  if ( dtype == GDT_Float32 )
  {
    const double float32Max = static_cast<double>( std::numeric_limits<float>::max() );
    const std::size_t count = static_cast<std::size_t>( window.width ) * window.height;
    for ( std::size_t i = 0; i < count; ++i )
    {
      const double value = values[i];
      if ( !std::isnan( value ) && ( value > float32Max || value < -float32Max ) )
      {
        Json::Value details;
        details["band"] = band1Based;
        details["index"] = static_cast<Json::UInt64>( i );
        details["value"] = value;
        details["dtype"] = GDALGetDataTypeName( dtype );
        details["float32_max"] = float32Max;
        throw GeoError( ErrorCode::FidelityLoss,
                        "writeWindow: value overflows the band's Float32 range "
                        "(stored pixels would silently become ±inf)",
                        details );
      }
    }
  }

  const CPLErr error = GDALRasterIO( band, GF_Write, window.xOff, window.yOff, window.width, window.height,
                                     const_cast<double *>( values ), window.width, window.height, GDT_Float64, 0, 0 );
  if ( error != CE_None )
  {
    const char *lastError = CPLGetLastErrorMsg();
    Json::Value details;
    details["band"] = band1Based;
    if ( lastError && *lastError )
      details["gdal_error"] = lastError;
    throw GeoError( ErrorCode::WriteFailed, "writeWindow: pixel write failed (dtype range?)", details );
  }
}

void RasterWriter::cancel()
{
  if ( mHandle )
  {
    GDALClose( datasetOf( mHandle ) );
    mHandle = nullptr;
  }
  if ( !mStagedPath.empty() )
  {
    atomic_fs::discardStaged( mStagedPath );
    mStagedPath.clear();
  }
  mFinalized = true;
}

void RasterWriter::finalize()
{
  if ( mFinalized )
    throw GeoError( ErrorCode::InvalidArgument, "finalize: writer already closed" );
  if ( !mHandle )
    throw GeoError( ErrorCode::InvalidArgument, "finalize: writer is closed" );

  GDALDatasetH dataset = datasetOf( mHandle );
  // Flush errors must fail the transaction (truncated output stays staged).
  if ( GDALFlushCache( dataset ) != CE_None )
  {
    GDALClose( dataset );
    mHandle = nullptr;
    atomic_fs::discardStaged( mStagedPath );
    throw GeoError( ErrorCode::WriteFailed, "finalize: flush failed; output discarded" );
  }
  GDALClose( dataset );
  mHandle = nullptr;

  try
  {
    atomic_fs::fsyncFile( mStagedPath );

    // Validate by reopening: a readable dataset with the declared shape.
    {
      QuietCplErrors quiet;
      GDALDatasetH probe = GDALOpenEx( mStagedPath.c_str(), GDAL_OF_READONLY | GDAL_OF_RASTER, nullptr, nullptr, nullptr );
      if ( !probe )
      {
        const char *lastError = CPLGetLastErrorMsg();
        Json::Value details;
        details["path"] = mStagedPath;
        if ( lastError && *lastError )
          details["gdal_error"] = lastError;
        throw GeoError( ErrorCode::WriteFailed, "finalize: staged output failed validation (unreadable)", details );
      }
      const bool shapeOk = GDALGetRasterXSize( probe ) == mWidth && GDALGetRasterYSize( probe ) == mHeight
                            && GDALGetRasterCount( probe ) == mBandCount;
      GDALClose( probe );
      if ( !shapeOk )
        throw GeoError( ErrorCode::WriteFailed, "finalize: staged output failed validation (shape mismatch)" );
    }

    atomic_fs::publishStagedGroup( mStagedPath, mTargetPath );
    mStagedPath.clear();
    mFinalized = true;
  }
  catch ( ... )
  {
    atomic_fs::discardStaged( mStagedPath );
    throw;
  }
}

} // namespace sicnu::geo

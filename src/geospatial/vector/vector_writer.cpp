/***************************************************************************
  geospatial/vector/vector_writer.cpp
  Geospatial I/O Foundation 4.0 — atomic vector write contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/vector/vector_writer.h"

#include "geospatial/gdal_guard.h"
#include "geospatial/util/atomic_fs.h"

#include <gdal.h>
#include <ogr_api.h>
#include <ogr_geometry.h>
#include <ogr_srs_api.h>

#include <map>
#include <utility>

namespace sicnu::geo
{
namespace
{

GDALDatasetH datasetOf( void *handle ) { return static_cast<GDALDatasetH>( handle ); }
OGRLayerH layerOf( void *handle ) { return static_cast<OGRLayerH>( handle ); }

OGRwkbGeometryType geometryTypeFromName( const std::string &name )
{
  struct Mapping
  {
    const char *name;
    OGRwkbGeometryType type;
  };
  static const Mapping kMappings[] = {
    { "Point", wkbPoint },
    { "LineString", wkbLineString },
    { "Polygon", wkbPolygon },
    { "MultiPoint", wkbMultiPoint },
    { "MultiLineString", wkbMultiLineString },
    { "MultiPolygon", wkbMultiPolygon },
    { "GeometryCollection", wkbGeometryCollection },
    { "Unknown", wkbUnknown },
    { "None", wkbNone },
  };
  for ( const Mapping &mapping : kMappings )
  {
    if ( name == mapping.name )
      return mapping.type;
  }
  return wkbUnknown;
}

OGRFieldType fieldTypeFromName( const std::string &name, const VectorFieldSpec &spec )
{
  if ( name == "Integer" )
    return OFTInteger;
  if ( name == "Integer64" )
    return OFTInteger64;
  if ( name == "Real" )
    return OFTReal;
  if ( name == "String" )
    return OFTString;
  if ( name == "Date" )
    return OFTDate;
  Json::Value details;
  details["field"] = spec.name;
  details["type"] = name;
  throw GeoError( ErrorCode::Unsupported, "Unsupported vector field type", details );
}

} // namespace

VectorWriter VectorWriter::create( const std::string &targetPath, const std::string &layerName,
                                   const std::string &geometryTypeName, const std::vector<VectorFieldSpec> &fields,
                                   const Crs &crs, const VectorWriteOptions &options )
{
  if ( targetPath.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "VectorWriter::create: empty target path" );
  if ( layerName.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "VectorWriter::create: empty layer name" );

  const OGRwkbGeometryType geometryType = geometryTypeFromName( geometryTypeName );
  if ( geometryType == wkbUnknown )
  {
    Json::Value details;
    details["geometry_type"] = geometryTypeName;
    throw GeoError( ErrorCode::InvalidArgument, "VectorWriter::create: unknown geometry type", details );
  }

  ensureGdalRegistered();

  if ( atomic_fs::fileExists( targetPath ) && !options.overwrite )
  {
    Json::Value details;
    details["path"] = targetPath;
    details["hint"] = "set overwrite=true to replace an existing output explicitly";
    throw GeoError( ErrorCode::IoError, "VectorWriter::create: target already exists: " + targetPath, details );
  }

  GDALDriverH driver = GDALGetDriverByName( options.driver.c_str() );
  if ( !driver )
  {
    Json::Value details;
    details["driver"] = options.driver;
    throw GeoError( ErrorCode::DriverMissing, "Vector driver not available: " + options.driver, details );
  }
  if ( !GDALGetMetadataItem( driver, GDAL_DCAP_CREATE, nullptr ) )
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

  const std::string stagedPath = atomic_fs::stagedPathFor( targetPath );
  QuietCplErrors quiet;
  GDALDatasetH handle = GDALCreate( driver, stagedPath.c_str(), 0, 0, 0, GDT_Unknown,
                                    const_cast<char **>( creationOptions.data() ) );
  if ( !handle )
  {
    atomic_fs::discardStaged( stagedPath );
    const char *lastError = CPLGetLastErrorMsg();
    Json::Value details;
    details["path"] = stagedPath;
    if ( lastError && *lastError )
      details["gdal_error"] = lastError;
    throw GeoError( ErrorCode::WriteFailed, "VectorWriter::create: dataset creation failed", details );
  }

  OGRSpatialReferenceH layerSrs = nullptr;
  OGRSpatialReferenceH srsClone = nullptr;
  if ( crs.isValid() )
  {
    srsClone = static_cast<OGRSpatialReferenceH>( OSRClone( crs.handle() ) );
    layerSrs = srsClone;
  }

  OGRLayerH layer = GDALDatasetCreateLayer( handle, layerName.c_str(), layerSrs, geometryType, nullptr );
  OSRDestroySpatialReference( srsClone );
  if ( !layer )
  {
    GDALClose( handle );
    atomic_fs::discardStaged( stagedPath );
    throw GeoError( ErrorCode::WriteFailed, "VectorWriter::create: layer creation failed" );
  }

  for ( const VectorFieldSpec &spec : fields )
  {
    if ( spec.name.empty() )
    {
      GDALClose( handle );
      atomic_fs::discardStaged( stagedPath );
      throw GeoError( ErrorCode::InvalidArgument, "VectorWriter::create: empty field name" );
    }
    OGRFieldDefnH fieldDefn = OGR_Fld_Create( spec.name.c_str(), fieldTypeFromName( spec.typeName, spec ) );
    if ( spec.width > 0 )
      OGR_Fld_SetWidth( fieldDefn, spec.width );
    if ( spec.precision > 0 )
      OGR_Fld_SetPrecision( fieldDefn, spec.precision );
    const OGRErr error = OGR_L_CreateField( layer, fieldDefn, 1 );
    OGR_Fld_Destroy( fieldDefn );
    if ( error != OGRERR_NONE )
    {
      GDALClose( handle );
      atomic_fs::discardStaged( stagedPath );
      throw GeoError( ErrorCode::WriteFailed, "VectorWriter::create: field creation failed for " + spec.name );
    }
  }

  // Batch the feature stream in one transaction: drivers with transaction
  // support (GPKG, PG) otherwise autocommit per feature — a 100x penalty on
  // bulk writes. Drivers without transactions (Shapefile, GeoJSON) keep the
  // unbatched path.
  VectorWriter writer;
  writer.mHandle = handle;
  writer.mLayer = layer;
  writer.mTargetPath = targetPath;
  writer.mStagedPath = stagedPath;
  writer.mTransactionActive = GDALDatasetStartTransaction( handle, FALSE ) == OGRERR_NONE;
  return writer;
}

VectorWriter::~VectorWriter()
{
  if ( !mFinalized )
    cancel();
}

VectorWriter::VectorWriter( VectorWriter &&other ) noexcept
  : mHandle( std::exchange( other.mHandle, nullptr ) )
  , mLayer( std::exchange( other.mLayer, nullptr ) )
  , mTargetPath( std::move( other.mTargetPath ) )
  , mStagedPath( std::move( other.mStagedPath ) )
  , mTransactionActive( std::exchange( other.mTransactionActive, false ) )
  , mFinalized( other.mFinalized )
{
  other.mFinalized = true;
}

VectorWriter &VectorWriter::operator=( VectorWriter &&other ) noexcept
{
  if ( this != &other )
  {
    if ( !mFinalized )
      cancel();
    mHandle = std::exchange( other.mHandle, nullptr );
    mLayer = std::exchange( other.mLayer, nullptr );
    mTargetPath = std::move( other.mTargetPath );
    mStagedPath = std::move( other.mStagedPath );
    mTransactionActive = std::exchange( other.mTransactionActive, false );
    mFinalized = std::exchange( other.mFinalized, true );
  }
  return *this;
}

void VectorWriter::writeFeature( const Json::Value &attributes, const std::string &geometryWkt )
{
  if ( !mLayer )
    throw GeoError( ErrorCode::InvalidArgument, "writeFeature: writer is closed" );
  if ( !attributes.isNull() && !attributes.isObject() )
    throw GeoError( ErrorCode::InvalidArgument, "writeFeature: attributes must be an object" );

  OGRLayerH layer = layerOf( mLayer );
  OGRFeatureDefnH defn = OGR_L_GetLayerDefn( layer );

  OGRFeatureH feature = OGR_F_Create( defn );
  if ( !feature )
    throw GeoError( ErrorCode::WriteFailed, "writeFeature: feature allocation failed" );
  bool ok = false;
  try
  {
    if ( attributes.isObject() )
    {
      for ( const std::string &key : attributes.getMemberNames() )
      {
        const int index = OGR_F_GetFieldIndex( feature, key.c_str() );
        if ( index < 0 )
        {
          Json::Value details;
          details["field"] = key;
          throw GeoError( ErrorCode::InvalidArgument, "writeFeature: unknown field", details );
        }
        const Json::Value &value = attributes[key];
        if ( value.isNull() )
        {
          OGR_F_SetFieldNull( feature, index );
        }
        else if ( value.isIntegral() )
        {
          OGR_F_SetFieldInteger64( feature, index, value.asInt64() );
        }
        else if ( value.isDouble() )
        {
          OGR_F_SetFieldDouble( feature, index, value.asDouble() );
        }
        else if ( value.isString() )
        {
          OGR_F_SetFieldString( feature, index, value.asString().c_str() );
        }
        else
        {
          Json::Value details;
          details["field"] = key;
          throw GeoError( ErrorCode::InvalidArgument, "writeFeature: unsupported attribute value type", details );
        }
      }
    }

    if ( !geometryWkt.empty() )
    {
      OGRGeometryH geometry = nullptr;
      {
        QuietCplErrors quiet;
        char *wktMutable = const_cast<char *>( geometryWkt.c_str() );
        if ( OGR_G_CreateFromWkt( &wktMutable, nullptr, &geometry ) != OGRERR_NONE || !geometry )
        {
          Json::Value details;
          details["wkt_prefix"] = geometryWkt.substr( 0, 80 );
          throw GeoError( ErrorCode::InvalidArgument, "writeFeature: invalid geometry WKT", details );
        }
      }
      const OGRErr error = OGR_F_SetGeometryDirectly( feature, geometry );
      if ( error != OGRERR_NONE )
      {
        OGR_G_DestroyGeometry( geometry );
        throw GeoError( ErrorCode::WriteFailed, "writeFeature: geometry attach failed" );
      }
    }

    {
      QuietCplErrors quiet;
      ok = OGR_L_CreateFeature( layer, feature ) == OGRERR_NONE;
    }
    if ( !ok )
    {
      const char *lastError = CPLGetLastErrorMsg();
      Json::Value details;
      if ( lastError && *lastError )
        details["gdal_error"] = lastError;
      throw GeoError( ErrorCode::WriteFailed, "writeFeature: driver rejected the feature", details );
    }
  }
  catch ( ... )
  {
    OGR_F_Destroy( feature );
    throw;
  }
  OGR_F_Destroy( feature );
}

void VectorWriter::cancel()
{
  if ( mHandle )
  {
    GDALClose( datasetOf( mHandle ) );
    mHandle = nullptr;
    mLayer = nullptr;
  }
  if ( !mStagedPath.empty() )
  {
    atomic_fs::discardStaged( mStagedPath );
    mStagedPath.clear();
  }
  mTransactionActive = false;
  mFinalized = true;
}

void VectorWriter::finalize()
{
  if ( mFinalized )
    throw GeoError( ErrorCode::InvalidArgument, "finalize: writer already closed" );
  if ( !mHandle )
    throw GeoError( ErrorCode::InvalidArgument, "finalize: writer is closed" );

  GDALDatasetH dataset = datasetOf( mHandle );
  if ( mTransactionActive && GDALDatasetCommitTransaction( dataset ) != OGRERR_NONE )
  {
    GDALClose( dataset );
    mHandle = nullptr;
    mLayer = nullptr;
    atomic_fs::discardStaged( mStagedPath );
    throw GeoError( ErrorCode::WriteFailed, "finalize: transaction commit failed; output discarded" );
  }
  mTransactionActive = false;
  if ( GDALFlushCache( dataset ) != CE_None )
  {
    GDALClose( dataset );
    mHandle = nullptr;
    mLayer = nullptr;
    atomic_fs::discardStaged( mStagedPath );
    throw GeoError( ErrorCode::WriteFailed, "finalize: flush failed; output discarded" );
  }
  // Sync the feature count / close any transactional buffering before fsync.
  OGR_L_SyncToDisk( layerOf( mLayer ) );
  GDALClose( dataset );
  mHandle = nullptr;
  mLayer = nullptr;

  try
  {
    atomic_fs::fsyncFile( mStagedPath );

    {
      QuietCplErrors quiet;
      GDALDatasetH probe = GDALOpenEx( mStagedPath.c_str(), GDAL_OF_READONLY | GDAL_OF_VECTOR, nullptr, nullptr, nullptr );
      if ( !probe )
        throw GeoError( ErrorCode::WriteFailed, "finalize: staged output failed validation (unreadable)" );
      GDALClose( probe );
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

/***************************************************************************
  geospatial/vector/vector_reader.cpp
  Geospatial I/O Foundation 4.0 — streaming vector read contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/vector/vector_reader.h"

#include "geospatial/gdal_guard.h"

#include <gdal.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include <cpl_conv.h>
#include <cpl_string.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <utility>

namespace sicnu::geo
{
namespace
{

GDALDatasetH datasetOf( void *handle ) { return static_cast<GDALDatasetH>( handle ); }
OGRLayerH layerOf( void *handle ) { return static_cast<OGRLayerH>( handle ); }

} // namespace

VectorReader VectorReader::open( const std::string &path, const std::string &layerSelector )
{
  if ( path.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "VectorReader::open: empty path" );

  ensureGdalRegistered();
  QuietCplErrors quiet;
  GDALDatasetH handle = GDALOpenEx( path.c_str(), GDAL_OF_READONLY | GDAL_OF_VECTOR, nullptr, nullptr, nullptr );
  if ( !handle )
  {
    Json::Value details;
    details["path"] = path;
    const char *lastError = CPLGetLastErrorMsg();
    if ( lastError && *lastError )
      details["gdal_error"] = lastError;
    throw GeoError( ErrorCode::OpenFailed, "Cannot open vector source: " + path, details );
  }

  OGRLayerH layer = nullptr;
  if ( layerSelector.empty() )
  {
    layer = GDALDatasetGetLayer( handle, 0 );
  }
  else
  {
    // Numeric selector = index, anything else = name.
    bool allDigits = !layerSelector.empty();
    for ( const char c : layerSelector )
      allDigits = allDigits && std::isdigit( static_cast<unsigned char>( c ) );
    if ( allDigits )
      layer = GDALDatasetGetLayer( handle, std::atoi( layerSelector.c_str() ) );
    else
      layer = GDALDatasetGetLayerByName( handle, layerSelector.c_str() );
  }
  if ( !layer )
  {
    GDALClose( handle );
    throw GeoError( ErrorCode::OpenFailed, "Vector layer not found: " + layerSelector + " in " + path );
  }

  VectorReader reader;
  reader.mHandle = handle;
  reader.mLayer = layer;
  reader.mDatasetInfo = inspectVector( path );
  for ( const VectorLayerInfo &candidate : reader.mDatasetInfo.layers )
  {
    const char *name = OGR_L_GetName( layer );
    if ( candidate.name == ( name ? name : "" ) )
    {
      reader.mLayerInfo = candidate;
      break;
    }
  }
  OGR_L_ResetReading( layer );
  return reader;
}

VectorReader::~VectorReader() { close(); }

VectorReader::VectorReader( VectorReader &&other ) noexcept
  : mHandle( std::exchange( other.mHandle, nullptr ) )
  , mLayer( std::exchange( other.mLayer, nullptr ) )
  , mTransform( std::move( other.mTransform ) )
  , mDatasetInfo( std::move( other.mDatasetInfo ) )
  , mLayerInfo( std::move( other.mLayerInfo ) )
  , mProjection( std::move( other.mProjection ) )
  , mStreamExhausted( other.mStreamExhausted )
{
}

VectorReader &VectorReader::operator=( VectorReader &&other ) noexcept
{
  if ( this != &other )
  {
    close();
    mHandle = std::exchange( other.mHandle, nullptr );
    mLayer = std::exchange( other.mLayer, nullptr );
    mTransform = std::move( other.mTransform );
    mDatasetInfo = std::move( other.mDatasetInfo );
    mLayerInfo = std::move( other.mLayerInfo );
    mProjection = std::move( other.mProjection );
    mStreamExhausted = other.mStreamExhausted;
  }
  return *this;
}

void VectorReader::close()
{
  mTransform = CrsTransform();
  if ( mHandle )
  {
    GDALClose( datasetOf( mHandle ) );
    mHandle = nullptr;
    mLayer = nullptr; // owned by dataset
  }
}

void VectorReader::setAttributeProjection( const std::vector<std::string> &fieldNames )
{
  if ( !mLayer )
    throw GeoError( ErrorCode::InvalidArgument, "setAttributeProjection: reader is closed" );
  std::set<std::string> known;
  for ( const FieldInfo &field : mLayerInfo.fields )
    known.insert( field.name );
  for ( const std::string &requested : fieldNames )
  {
    if ( !known.count( requested ) )
    {
      Json::Value details;
      details["field"] = requested;
      details["layer"] = mLayerInfo.name;
      throw GeoError( ErrorCode::InvalidArgument, "Attribute projection references unknown field", details );
    }
  }
  mProjection = fieldNames;
}

void VectorReader::setAttributeFilter( const std::string &whereClause )
{
  if ( !mLayer )
    throw GeoError( ErrorCode::InvalidArgument, "setAttributeFilter: reader is closed" );
  QuietCplErrors quiet;
  const OGRErr error = OGR_L_SetAttributeFilter( layerOf( mLayer ),
                                                 whereClause.empty() ? nullptr : whereClause.c_str() );
  if ( error != OGRERR_NONE )
  {
    Json::Value details;
    details["where"] = whereClause;
    const char *lastError = CPLGetLastErrorMsg();
    if ( lastError && *lastError )
      details["gdal_error"] = lastError;
    throw GeoError( ErrorCode::InvalidArgument, "Attribute filter rejected by the driver", details );
  }
  mStreamExhausted = false;
}

void VectorReader::setSpatialFilter( const CrsBoundingBox &box )
{
  if ( !mLayer )
    throw GeoError( ErrorCode::InvalidArgument, "setSpatialFilter: reader is closed" );
  OGR_L_SetSpatialFilterRect( layerOf( mLayer ), box.minX, box.minY, box.maxX, box.maxY );
  mStreamExhausted = false;
}

void VectorReader::setTargetCrs( const Crs &target )
{
  mTransform = CrsTransform();
  if ( !target.isValid() )
    return; // clears the transform
  if ( !mLayerInfo.crs.valid )
    throw GeoError( ErrorCode::MissingCrs,
                    "setTargetCrs: layer carries no CRS; refusing to transform without declared source" );
  const Crs source = Crs::fromDatasetInfo( mLayerInfo.crs );
  mTransform = CrsTransform::create( source, target );
}

bool VectorReader::nextBatch( std::vector<VectorFeature> &out, std::size_t maxFeatures )
{
  if ( !mLayer || mStreamExhausted || maxFeatures == 0 )
    return false;
  OGRLayerH layer = layerOf( mLayer );
  std::size_t taken = 0;
  while ( taken < maxFeatures )
  {
    OGRFeatureH feature = OGR_L_GetNextFeature( layer );
    if ( !feature )
    {
      mStreamExhausted = true;
      return taken > 0;
    }
    VectorFeature item;
    item.fid = OGR_F_GetFID( feature );

    OGRFeatureDefnH defn = OGR_F_GetDefnRef( feature );
    const int fieldCount = defn ? OGR_FD_GetFieldCount( defn ) : 0;
    for ( int i = 0; i < fieldCount; ++i )
    {
      OGRFieldDefnH fieldDefn = OGR_FD_GetFieldDefn( defn, i );
      const char *name = OGR_Fld_GetNameRef( fieldDefn );
      if ( !name )
        continue;
      if ( !mProjection.empty() && std::find( mProjection.begin(), mProjection.end(), name ) == mProjection.end() )
        continue;
      if ( !OGR_F_IsFieldSet( feature, i ) || OGR_F_IsFieldNull( feature, i ) )
      {
        item.attributes[name] = Json::Value();
        continue;
      }
      switch ( OGR_Fld_GetType( fieldDefn ) )
      {
        case OFTInteger:
          item.attributes[name] = OGR_F_GetFieldAsInteger( feature, i );
          break;
        case OFTInteger64:
          item.attributes[name] = static_cast<Json::Int64>( OGR_F_GetFieldAsInteger64( feature, i ) );
          break;
        case OFTReal:
          item.attributes[name] = OGR_F_GetFieldAsDouble( feature, i );
          break;
        default:
        {
          // String-ish types (String, Date, Time, DateTime, lists): the C
          // API's string form is the canonical lossless-enough view here.
          const char *value = OGR_F_GetFieldAsString( feature, i );
          item.attributes[name] = value ? value : "";
          break;
        }
      }
    }

    OGRGeometryH geometry = OGR_F_GetGeometryRef( feature );
    if ( geometry && !OGR_G_IsEmpty( geometry ) )
    {
      if ( mTransform.forwardHandle() )
      {
        if ( OGR_G_Transform( geometry, static_cast<OGRCoordinateTransformationH>( mTransform.forwardHandle() ) )
             != OGRERR_NONE )
        {
          OGR_F_Destroy( feature );
          throw GeoError( ErrorCode::TransformFailed, "nextBatch: geometry transform failed" );
        }
      }
      char *wkt = nullptr;
      if ( OGR_G_ExportToWkt( geometry, &wkt ) == OGRERR_NONE && wkt )
      {
        item.geometryWkt = wkt;
        CPLFree( wkt );
      }
    }

    OGR_F_Destroy( feature );
    out.push_back( std::move( item ) );
    ++taken;
  }
  return true;
}

void VectorReader::resetStream()
{
  if ( !mLayer )
    return;
  OGR_L_ResetReading( layerOf( mLayer ) );
  mStreamExhausted = false;
}

std::int64_t VectorReader::exactFeatureCount()
{
  if ( !mLayer )
    throw GeoError( ErrorCode::InvalidArgument, "exactFeatureCount: reader is closed" );
  // bForce=TRUE: may scan; that is the documented point of this call.
  const GIntBig count = OGR_L_GetFeatureCount( layerOf( mLayer ), 1 );
  return count < 0 ? -1 : count;
}

} // namespace sicnu::geo

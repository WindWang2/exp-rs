/***************************************************************************
  geospatial/vector/vector_reader.cpp
  Geospatial I/O Foundation 4.0 — streaming vector read contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/vector/vector_reader.h"

#include <cpl_error.h>

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

/// SQL identifier quoting (embedded quotes doubled) — field and layer names
/// in aggregate SQL must never break out of the identifier.
std::string quoteSqlIdentifier( const std::string &name )
{
  std::string out = "\"";
  for ( const char c : name )
  {
    out += c;
    if ( c == '"' )
      out += '"';
  }
  return out + "\"";
}

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
      // NULL means end-of-stream OR a mid-iteration driver error; the two
      // must never be conflated (a partial stream is not a complete one).
      if ( CPLGetLastErrorType() == CE_Failure || CPLGetLastErrorType() == CE_Fatal )
        throw GeoError( ErrorCode::IoError,
                        "nextBatch: feature iteration failed",
                        Json::Value( CPLGetLastErrorMsg() ? CPLGetLastErrorMsg() : "" ) );
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

Json::Value VectorExtent::toJson() const
{
  Json::Value json;
  json["valid"] = valid;
  json["exact"] = exact;
  if ( valid )
  {
    json["min_x"] = minX;
    json["min_y"] = minY;
    json["max_x"] = maxX;
    json["max_y"] = maxY;
  }
  return json;
}

Json::Value VectorFieldStatistics::toJson() const
{
  Json::Value json;
  json["field"] = field;
  json["non_null_count"] = static_cast<Json::Int64>( nonNullCount );
  json["has_min"] = hasMin;
  if ( hasMin )
    json["min"] = minValue;
  json["has_max"] = hasMax;
  if ( hasMax )
    json["max"] = maxValue;
  json["has_sum"] = hasSum;
  if ( hasSum )
    json["sum"] = sum;
  json["has_mean"] = hasMean;
  if ( hasMean )
    json["mean"] = mean;
  return json;
}

VectorExtent VectorReader::extent( bool allowScan ) const
{
  VectorExtent out;
  if ( !mLayer )
    throw GeoError( ErrorCode::InvalidArgument, "extent: reader is closed" );
  OGREnvelope envelope;
  envelope.MinX = envelope.MinY = envelope.MaxX = envelope.MaxY = 0.0;
  QuietCplErrors quiet;
  // bForce per the declared contract: without it, a driver without a cheap
  // extent simply reports one — the caller decides whether a scan is bought.
  if ( OGR_L_GetExtent( layerOf( mLayer ), &envelope, allowScan ? 1 : 0 ) == OGRERR_NONE )
  {
    out.valid = true;
    out.exact = true; // a returned envelope is geometry-derived, not a guess
    out.minX = envelope.MinX;
    out.minY = envelope.MinY;
    out.maxX = envelope.MaxX;
    out.maxY = envelope.MaxY;
    return out;
  }
  // Fall back to the metadata envelope the open captured (a declared
  // envelope; honest about its provenance).
  const VectorLayerInfo &info = mLayerInfo;
  if ( info.hasExtent )
  {
    out.valid = true;
    out.exact = info.extentExact;
    out.minX = info.minX;
    out.minY = info.minY;
    out.maxX = info.maxX;
    out.maxY = info.maxY;
  }
  return out;
}

VectorFieldStatistics VectorReader::fieldStatistics( const std::string &fieldName,
                                                     const std::string &whereClause ) const
{
  if ( !mHandle || !mLayer )
    throw GeoError( ErrorCode::InvalidArgument, "fieldStatistics: reader is closed" );

  // Numeric-only contract, checked against the captured schema.
  bool numeric = false;
  for ( const FieldInfo &field : mLayerInfo.fields )
  {
    if ( field.name == fieldName )
    {
      numeric = field.typeName != "String" && field.typeName != "Date"
                && field.typeName != "Time" && field.typeName != "DateTime"
                && field.typeName != "Binary";
      if ( !numeric )
      {
        Json::Value details;
        details["field"] = fieldName;
        details["type"] = field.typeName;
        throw GeoError( ErrorCode::Unsupported, "fieldStatistics: field is not numeric", details );
      }
      break;
    }
  }
  ( void )numeric; // an unknown field below also errors (through SQL failure — checked explicitly next)
  bool known = false;
  for ( const FieldInfo &field : mLayerInfo.fields )
    known = known || field.name == fieldName;
  if ( !known )
  {
    Json::Value details;
    details["field"] = fieldName;
    throw GeoError( ErrorCode::InvalidArgument, "fieldStatistics: unknown field", details );
  }

  // Quoted identifiers; the WHERE clause is caller-supplied OGR SQL,
  // evaluated by the driver as declared in the contract.
  const std::string quote = quoteSqlIdentifier( fieldName );
  std::string sql = "SELECT COUNT(" + quote + "), MIN(" + quote + "), MAX(" + quote
                    + "), SUM(" + quote + "), AVG(" + quote + ") FROM "
                    + quoteSqlIdentifier( mLayerInfo.name );
  if ( !whereClause.empty() )
    sql += " WHERE " + whereClause;

  QuietCplErrors quiet;
  OGRLayerH result = GDALDatasetExecuteSQL( datasetOf( mHandle ), sql.c_str(), nullptr, nullptr );
  if ( !result )
  {
    const char *lastError = CPLGetLastErrorMsg();
    Json::Value details;
    if ( lastError && *lastError )
      details["gdal_error"] = lastError;
    throw GeoError( ErrorCode::InvalidArgument, "fieldStatistics: aggregate evaluation failed", details );
  }
  VectorFieldStatistics stats;
  stats.field = fieldName;
  OGRFeatureH row = OGR_L_GetNextFeature( result );
  if ( row != nullptr )
  {
    const auto isNull = [ row ]( int index ) { return OGR_F_IsFieldSetAndNotNull( row, index ) == 0; };
    stats.nonNullCount = static_cast<std::int64_t>( OGR_F_GetFieldAsInteger64( row, 0 ) );
    if ( !isNull( 1 ) ) { stats.hasMin = true; stats.minValue = OGR_F_GetFieldAsDouble( row, 1 ); }
    if ( !isNull( 2 ) ) { stats.hasMax = true; stats.maxValue = OGR_F_GetFieldAsDouble( row, 2 ); }
    if ( !isNull( 3 ) ) { stats.hasSum = true; stats.sum = OGR_F_GetFieldAsDouble( row, 3 ); }
    if ( !isNull( 4 ) ) { stats.hasMean = true; stats.mean = OGR_F_GetFieldAsDouble( row, 4 ); }
    OGR_F_Destroy( row );
  }
  GDALDatasetReleaseResultSet( datasetOf( mHandle ), result );
  return stats;
}

} // namespace sicnu::geo

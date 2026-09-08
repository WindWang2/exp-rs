/***************************************************************************
  geospatial/crs/crs_policy.cpp
  Geospatial I/O Foundation 4.0 — explicit CRS resolution & transform policy.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/crs/crs_policy.h"

#include "geospatial/gdal_guard.h"

#include <cpl_conv.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

namespace sicnu::geo
{
namespace
{

std::string lastCplError()
{
  const char *message = CPLGetLastErrorMsg();
  return ( message && *message ) ? std::string( message ) : std::string();
}

bool isNumericOnly( const std::string &text )
{
  if ( text.empty() )
    return false;
  for ( const char c : text )
  {
    if ( !( std::isdigit( static_cast<unsigned char>( c ) ) || c == '+' || c == '-' || c == '.' ) )
      return false;
  }
  return true;
}

OGRSpatialReferenceH importAndValidate( OGRSpatialReferenceH handle, ErrorCode failureCode,
                                        const std::string &failureMessage, const std::string &input )
{
  if ( !handle )
    throw GeoError( failureCode, failureMessage, [ & ] {
      Json::Value details;
      details["input"] = input;
      const std::string error = lastCplError();
      if ( !error.empty() )
        details["gdal_error"] = error;
      return details;
    }() );
  if ( static_cast< OGRSpatialReference * >( handle )->IsEmpty() )
  {
    OSRDestroySpatialReference( handle );
    Json::Value details;
    details["input"] = input;
    throw GeoError( failureCode, failureMessage + " (resolved to an empty CRS)", details );
  }
  return handle;
}

} // namespace

const char *axisOrderName( AxisOrder order )
{
  return order == AxisOrder::Authority ? "authority" : "traditional_gis";
}

AxisOrder axisOrderFromName( const std::string &name )
{
  if ( name == "authority" )
    return AxisOrder::Authority;
  if ( name == "traditional_gis" || name.empty() )
    return AxisOrder::TraditionalGis;
  throw GeoError( ErrorCode::InvalidArgument, "Unknown axis order: " + name,
                  Json::Value( "expected \"authority\" or \"traditional_gis\"" ) );
}

// ─── Crs ─────────────────────────────────────────────────────────────────────

Crs::Crs( OGRSpatialReferenceH handle ) : mHandle( handle ) {}

Crs::~Crs()
{
  if ( mHandle )
    OSRDestroySpatialReference( mHandle );
}

Crs::Crs( const Crs &other )
  : mHandle( other.mHandle ? static_cast<OGRSpatialReferenceH>( OSRClone( other.mHandle ) ) : nullptr )
{
}

Crs &Crs::operator=( const Crs &other )
{
  if ( this == &other )
    return *this;
  if ( mHandle )
    OSRDestroySpatialReference( mHandle );
  mHandle = other.mHandle ? static_cast<OGRSpatialReferenceH>( OSRClone( other.mHandle ) ) : nullptr;
  return *this;
}

Crs::Crs( Crs &&other ) noexcept : mHandle( std::exchange( other.mHandle, nullptr ) ) {}

Crs &Crs::operator=( Crs &&other ) noexcept
{
  if ( this != &other )
  {
    if ( mHandle )
      OSRDestroySpatialReference( mHandle );
    mHandle = std::exchange( other.mHandle, nullptr );
  }
  return *this;
}

Crs Crs::fromAuthid( const std::string &authid )
{
  if ( authid.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "fromAuthid: empty authority id" );
  Json::Value details;
  details["authid"] = authid;

  OGRSpatialReferenceH handle = OSRNewSpatialReference( nullptr );
  OGRErr error = OGRERR_FAILURE;
  if ( authid.size() > 5 && authid.compare( 0, 5, "EPSG:" ) == 0 && isNumericOnly( authid.substr( 5 ) ) )
  {
    error = OSRImportFromEPSG( handle, std::atoi( authid.c_str() + 5 ) );
  }
  else
  {
    error = OSRSetFromUserInput( handle, authid.c_str() );
  }
  if ( error != OGRERR_NONE )
  {
    OSRDestroySpatialReference( handle );
    const std::string gdalError = lastCplError();
    if ( !gdalError.empty() )
      details["gdal_error"] = gdalError;
    throw GeoError( ErrorCode::InvalidCrs, "Cannot resolve CRS authority id: " + authid, details );
  }
  return Crs( importAndValidate( handle, ErrorCode::InvalidCrs, "Cannot resolve CRS authority id: " + authid, authid ) );
}

Crs Crs::fromWkt( const std::string &wkt )
{
  if ( wkt.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "fromWkt: empty WKT" );
  OGRSpatialReferenceH handle = OSRNewSpatialReference( nullptr );
  char *wktMutable = const_cast<char *>( wkt.c_str() );
  const OGRErr error = OSRImportFromWkt( handle, &wktMutable );
  if ( error != OGRERR_NONE )
  {
    OSRDestroySpatialReference( handle );
    Json::Value details;
    details["wkt_prefix"] = wkt.substr( 0, 80 );
    const std::string gdalError = lastCplError();
    if ( !gdalError.empty() )
      details["gdal_error"] = gdalError;
    throw GeoError( ErrorCode::InvalidCrs, "Cannot import CRS from WKT", details );
  }
  return Crs( importAndValidate( handle, ErrorCode::InvalidCrs, "Cannot import CRS from WKT", wkt ) );
}

Crs Crs::fromUserInput( const std::string &text )
{
  if ( text.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "fromUserInput: empty CRS text" );
  OGRSpatialReferenceH handle = OSRNewSpatialReference( nullptr );
  const OGRErr error = OSRSetFromUserInput( handle, text.c_str() );
  if ( error != OGRERR_NONE )
  {
    OSRDestroySpatialReference( handle );
    Json::Value details;
    details["input_prefix"] = text.substr( 0, 80 );
    const std::string gdalError = lastCplError();
    if ( !gdalError.empty() )
      details["gdal_error"] = gdalError;
    throw GeoError( ErrorCode::InvalidCrs, "Cannot resolve CRS from user input", details );
  }
  return Crs( importAndValidate( handle, ErrorCode::InvalidCrs, "Cannot resolve CRS from user input", text ) );
}

Crs Crs::fromDatasetInfo( const CrsInfo &info )
{
  if ( !info.valid || ( info.wkt.empty() && info.authid.empty() ) )
  {
    throw GeoError( ErrorCode::MissingCrs,
                    "Dataset carries no coordinate reference system; refusing to guess. "
                    "Declare an explicit destination CRS or enable a declared fallback policy." );
  }
  if ( !info.wkt.empty() )
  {
    try
    {
      return fromWkt( info.wkt );
    }
    catch ( const GeoError &wktError )
    {
      if ( info.authid.empty() )
        throw;
      // Broken WKT with a usable authority id is still resolvable; this is
      // driver data repair, not a guess (the id comes from the dataset).
      try
      {
        return fromAuthid( info.authid );
      }
      catch ( const GeoError & )
      {
        throw wktError;
      }
    }
  }
  return fromAuthid( info.authid );
}

bool Crs::isValid() const { return mHandle && !static_cast< OGRSpatialReference * >( mHandle )->IsEmpty(); }

std::string Crs::wkt() const
{
  if ( !mHandle )
    return std::string();
  char *text = nullptr;
  if ( OSRExportToWkt( mHandle, &text ) != OGRERR_NONE || !text )
  {
    if ( text )
      CPLFree( text );
    return std::string();
  }
  std::string result( text );
  CPLFree( text );
  return result;
}

std::string Crs::authid() const
{
  if ( !mHandle )
    return std::string();
  const char *authority = OSRGetAuthorityName( mHandle, nullptr );
  const char *code = OSRGetAuthorityCode( mHandle, nullptr );
  if ( authority && code )
    return std::string( authority ) + ":" + code;
  return std::string();
}

bool Crs::isGeographic() const { return mHandle && OSRIsGeographic( mHandle ) == TRUE; }
bool Crs::isProjected() const { return mHandle && OSRIsProjected( mHandle ) == TRUE; }

double Crs::coordinateEpoch() const
{
  return mHandle ? OSRGetCoordinateEpoch( mHandle ) : 0.0;
}

Json::Value Crs::toJson() const
{
  Json::Value json;
  json["valid"] = isValid();
  json["wkt"] = wkt();
  json["authid"] = authid();
  json["is_geographic"] = isGeographic();
  json["is_projected"] = isProjected();
  const double epoch = coordinateEpoch();
  json["has_coordinate_epoch"] = epoch > 0.0;
  if ( epoch > 0.0 )
    json["coordinate_epoch"] = epoch;
  return json;
}

CrsInfo Crs::toCrsInfo() const
{
  CrsInfo info;
  info.valid = isValid();
  info.wkt = wkt();
  info.authid = authid();
  info.isGeographic = isGeographic();
  info.isProjected = isProjected();
  const double epoch = coordinateEpoch();
  info.hasCoordinateEpoch = epoch > 0.0;
  info.coordinateEpoch = epoch > 0.0 ? epoch : 0.0;
  return info;
}

// ─── CrsTransform ────────────────────────────────────────────────────────────

CrsTransform::CrsTransform( OGRCoordinateTransformationH transformation, TransformDiagnostics diagnostics )
  : mForward( transformation ), mDiagnostics( std::move( diagnostics ) )
{
}

CrsTransform::~CrsTransform()
{
  if ( mForward )
    OCTDestroyCoordinateTransformation( mForward );
  if ( mInverse )
    OCTDestroyCoordinateTransformation( mInverse );
}

CrsTransform::CrsTransform( CrsTransform &&other ) noexcept
  : mForward( std::exchange( other.mForward, nullptr ) )
  , mInverse( std::exchange( other.mInverse, nullptr ) )
  , mDiagnostics( std::move( other.mDiagnostics ) )
{
}

CrsTransform &CrsTransform::operator=( CrsTransform &&other ) noexcept
{
  if ( this != &other )
  {
    if ( mForward )
      OCTDestroyCoordinateTransformation( mForward );
    if ( mInverse )
      OCTDestroyCoordinateTransformation( mInverse );
    mForward = std::exchange( other.mForward, nullptr );
    mInverse = std::exchange( other.mInverse, nullptr );
    mDiagnostics = std::move( other.mDiagnostics );
  }
  return *this;
}

CrsTransform CrsTransform::create( const Crs &source, const Crs &target, AxisOrder workingOrder )
{
  if ( !source.isValid() )
    throw GeoError( ErrorCode::InvalidCrs, "CrsTransform: source CRS is not valid" );
  if ( !target.isValid() )
    throw GeoError( ErrorCode::InvalidCrs, "CrsTransform: target CRS is not valid" );

  QuietCplErrors quiet;

  // The mapping strategy lives on the CRS handles; clone so we never mutate
  // caller-owned CRS objects.
  OGRSpatialReferenceH sourceHandle = static_cast<OGRSpatialReferenceH>( OSRClone( source.handle() ) );
  OGRSpatialReferenceH targetHandle = static_cast<OGRSpatialReferenceH>( OSRClone( target.handle() ) );
  const OSRAxisMappingStrategy strategy = workingOrder == AxisOrder::Authority
                                            ? OAMS_AUTHORITY_COMPLIANT
                                            : OAMS_TRADITIONAL_GIS_ORDER;
  OSRSetAxisMappingStrategy( sourceHandle, strategy );
  OSRSetAxisMappingStrategy( targetHandle, strategy );
  const double sourceEpoch = source.coordinateEpoch();
  if ( sourceEpoch > 0.0 )
    OSRSetCoordinateEpoch( sourceHandle, sourceEpoch );
  const double targetEpoch = target.coordinateEpoch();
  if ( targetEpoch > 0.0 )
    OSRSetCoordinateEpoch( targetHandle, targetEpoch );

  OGRCoordinateTransformationH forward = OCTNewCoordinateTransformation( sourceHandle, targetHandle );
  OGRCoordinateTransformationH inverse = nullptr;
  if ( forward )
    inverse = OCTNewCoordinateTransformation( targetHandle, sourceHandle );
  OSRDestroySpatialReference( sourceHandle );
  OSRDestroySpatialReference( targetHandle );

  if ( !forward || !inverse )
  {
    if ( forward )
      OCTDestroyCoordinateTransformation( forward );
    if ( inverse )
      OCTDestroyCoordinateTransformation( inverse );
    Json::Value details;
    details["source"] = source.authid().empty() ? source.wkt().substr( 0, 80 ) : source.authid();
    details["target"] = target.authid().empty() ? target.wkt().substr( 0, 80 ) : target.authid();
    details["axis_order"] = axisOrderName( workingOrder );
    const std::string gdalError = lastCplError();
    if ( !gdalError.empty() )
      details["gdal_error"] = gdalError;
    throw GeoError( ErrorCode::TransformFailed,
                    "Cannot build coordinate transformation between the declared CRS", details );
  }

  TransformDiagnostics diagnostics;
  diagnostics.sourceAuthid = source.authid();
  diagnostics.targetAuthid = target.authid();
  diagnostics.workingOrder = workingOrder;
  // PROJ surfaces "ballpark" / caution through CPL messages at creation time.
  const std::string caution = lastCplError();
  diagnostics.ballpark = caution.find( "Ballpark" ) != std::string::npos
                          || caution.find( "ballpark" ) != std::string::npos;
  diagnostics.cautionNotes = caution;

  CrsTransform result( forward, std::move( diagnostics ) );
  result.mInverse = inverse;
  result.mTargetGeographic = target.isGeographic();
  return result;
}

CrsPoint CrsTransform::transform( OGRCoordinateTransformationH transformation, const CrsPoint &point,
                                  const char *direction ) const
{
  if ( !transformation )
    throw GeoError( ErrorCode::TransformFailed, "CrsTransform: transformation not initialized" );
  double x = point.x;
  double y = point.y;
  double z = 0.0;
  int success = 0;
  QuietCplErrors quiet;
  if ( !OCTTransformEx( transformation, 1, &x, &y, &z, &success ) || !success )
  {
    Json::Value details;
    details["direction"] = direction;
    details["x"] = point.x;
    details["y"] = point.y;
    const std::string gdalError = lastCplError();
    if ( !gdalError.empty() )
      details["gdal_error"] = gdalError;
    throw GeoError( ErrorCode::TransformFailed,
                    std::string( "Coordinate transformation failed (" ) + direction + ")", details );
  }
  return CrsPoint{ x, y };
}

CrsPoint CrsTransform::forward( const CrsPoint &point ) const
{
  return transform( mForward, point, "forward" );
}

CrsPoint CrsTransform::inverse( const CrsPoint &point ) const
{
  return transform( mInverse, point, "inverse" );
}

CrsBoundingBox CrsTransform::forwardBounds( const CrsBoundingBox &box, int edgeSamples ) const
{
  if ( edgeSamples < 2 )
    edgeSamples = 2;
  const int samplesPerEdge = edgeSamples;
  const int totalSamples = samplesPerEdge * 4;
  std::vector<double> xs;
  std::vector<double> ys;
  xs.reserve( totalSamples );
  ys.reserve( totalSamples );
  for ( int i = 0; i < samplesPerEdge; ++i )
  {
    const double t = static_cast<double>( i ) / ( samplesPerEdge - 1 );
    xs.push_back( box.minX + t * ( box.maxX - box.minX ) );
    ys.push_back( box.minY );
  }
  for ( int i = 0; i < samplesPerEdge; ++i )
  {
    const double t = static_cast<double>( i ) / ( samplesPerEdge - 1 );
    xs.push_back( box.minX + t * ( box.maxX - box.minX ) );
    ys.push_back( box.maxY );
  }
  for ( int i = 1; i < samplesPerEdge - 1; ++i )
  {
    const double t = static_cast<double>( i ) / ( samplesPerEdge - 1 );
    xs.push_back( box.minX );
    ys.push_back( box.minY + t * ( box.maxY - box.minY ) );
    xs.push_back( box.maxX );
    ys.push_back( box.minY + t * ( box.maxY - box.minY ) );
  }

  QuietCplErrors quiet;
  const int count = static_cast<int>( xs.size() );
  std::vector<double> zs( count, 0.0 );
  std::vector<int> success( count, 0 );
  if ( !OCTTransformEx( mForward, count, xs.data(), ys.data(), zs.data(), success.data() ) )
  {
    throw GeoError( ErrorCode::TransformFailed, "Coordinate transformation failed (bounds)" );
  }
  std::vector<CrsPoint> transformed;
  transformed.reserve( count );
  for ( int i = 0; i < count; ++i )
  {
    if ( !success[i] )
      continue;
    double x = xs[i];
    if ( mTargetGeographic )
    {
      // Normalize wrapped longitudes into [-180, 180] before aggregating so a
      // genuine antimeridian crossing becomes visible as a ~360° span.
      x = std::fmod( x + 180.0, 360.0 );
      if ( x < 0.0 )
        x += 360.0;
      x -= 180.0;
    }
    transformed.push_back( CrsPoint{ x, ys[i] } );
  }
  if ( transformed.empty() )
    throw GeoError( ErrorCode::TransformFailed, "Coordinate transformation failed (bounds): no sample transformed" );

  CrsBoundingBox result;
  result.minX = result.maxX = transformed.front().x;
  result.minY = result.maxY = transformed.front().y;
  for ( const CrsPoint &point : transformed )
  {
    result.minX = std::min( result.minX, point.x );
    result.maxX = std::max( result.maxX, point.x );
    result.minY = std::min( result.minY, point.y );
    result.maxY = std::max( result.maxY, point.y );
  }

  // Antimeridian detection: only meaningful for geographic targets, where a
  // dense sampled edge crossing ±180 shows up as an x-range near 360°.
  if ( mTargetGeographic && ( result.maxX - result.minX ) > 340.0 )
    result.crossesAntimeridian = true;
  return result;
}

// ─── Policy resolution ───────────────────────────────────────────────────────

ResolvedCrs resolveDatasetCrs( const CrsInfo &info, const CrsPolicy &policy, const std::string &context )
{
  ResolvedCrs resolved;
  const bool missing = !info.valid || ( info.wkt.empty() && info.authid.empty() );
  if ( missing )
  {
    if ( policy.allowDeclaredFallback && policy.fallbackCrs.valid
         && ( !policy.fallbackCrs.wkt.empty() || !policy.fallbackCrs.authid.empty() ) )
    {
      resolved.crs = policy.fallbackCrs.wkt.empty() ? Crs::fromAuthid( policy.fallbackCrs.authid )
                                                    : Crs::fromWkt( policy.fallbackCrs.wkt );
      resolved.usedDeclaredFallback = true;
      return resolved;
    }
    Json::Value details;
    if ( !context.empty() )
      details["context"] = context;
    details["policy"] = "missing CRS is an error unless a declared fallback is enabled";
    throw GeoError( ErrorCode::MissingCrs,
                    "Dataset carries no CRS and no declared fallback policy: " + context, details );
  }

  resolved.crs = Crs::fromDatasetInfo( info );
  resolved.usedDeclaredFallback = false;
  return resolved;
}

} // namespace sicnu::geo

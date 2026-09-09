/***************************************************************************
  geospatial/crs/grid_descriptor.cpp — generalized grid classification.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/crs/grid_descriptor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::geo
{

namespace
{

constexpr double kEpsilon = 1e-12;

bool isZero( double value ) { return std::fabs( value ) < kEpsilon; }

} // namespace

const char *gridKindName( GridKind kind )
{
  switch ( kind )
  {
    case GridKind::NorthUp: return "north_up";
    case GridKind::Rotated: return "rotated";
    case GridKind::Flipped: return "flipped";
    case GridKind::GcpBased: return "gcp_based";
    case GridKind::RpcBased: return "rpc_based";
    case GridKind::Unknown: return "unknown";
  }
  return "unknown";
}

const char *resamplingCategoryName( ResamplingCategory category )
{
  return category == ResamplingCategory::Categorical ? "categorical" : "continuous";
}

GridDescriptor GridDescriptor::fromMetadata( const RasterMetadata &metadata )
{
  GridDescriptor descriptor;
  descriptor.crs = metadata.crs;
  descriptor.width = metadata.width;
  descriptor.height = metadata.height;
  descriptor.hasGcps = metadata.hasGcps;
  descriptor.hasRpcs = metadata.hasRpc;
  descriptor.geotransform = metadata.geotransform;

  const bool hasAffine = metadata.hasGeotransform;
  const double &a = descriptor.geotransform[1]; // x pixel size
  const double &e = descriptor.geotransform[5]; // y pixel size
  const double &b = descriptor.geotransform[2]; // x rotation term
  const double &d = descriptor.geotransform[4]; // y rotation term

  descriptor.flippedX = hasAffine && a < -kEpsilon;
  descriptor.flippedY = hasAffine && e > kEpsilon; // north-up convention: e < 0

  if ( hasAffine && ( !isZero( b ) || !isZero( d ) ) )
  {
    descriptor.kind = GridKind::Rotated;
    // Signed rotation of the pixel x axis against the world x axis.
    descriptor.rotationDegrees = std::atan2( d, a ) * 180.0 / 3.14159265358979323846;
  }
  else if ( hasAffine && ( descriptor.flippedX || descriptor.flippedY ) )
  {
    descriptor.kind = GridKind::Flipped;
  }
  else if ( hasAffine )
  {
    descriptor.kind = GridKind::NorthUp;
  }
  else if ( metadata.hasRpc )
  {
    descriptor.kind = GridKind::RpcBased;
  }
  else if ( metadata.hasGcps )
  {
    descriptor.kind = GridKind::GcpBased;
  }
  else
  {
    descriptor.kind = GridKind::Unknown;
  }

  // RPC/GCP placement outranks a degenerate affine classification.
  if ( metadata.hasRpc && !hasAffine )
    descriptor.kind = GridKind::RpcBased;
  else if ( metadata.hasGcps && !hasAffine && descriptor.kind == GridKind::Unknown )
    descriptor.kind = GridKind::GcpBased;

  return descriptor;
}

CrsPoint GridDescriptor::pixelToWorld( double column, double row ) const
{
  const std::array<double, 6> &gt = geotransform;
  CrsPoint point;
  point.x = gt[0] + gt[1] * column + gt[2] * row;
  point.y = gt[3] + gt[4] * column + gt[5] * row;
  return point;
}

CrsPoint GridDescriptor::worldToPixel( const CrsPoint &world ) const
{
  const std::array<double, 6> &gt = geotransform;
  const double determinant = gt[1] * gt[5] - gt[2] * gt[4];
  if ( std::fabs( determinant ) < kEpsilon )
    throw GeoError( ErrorCode::TransformFailed,
                    "worldToPixel: singular affine (degenerate pixel size)" );
  const double dx = world.x - gt[0];
  const double dy = world.y - gt[3];
  CrsPoint pixel;
  pixel.x = ( gt[5] * dx - gt[2] * dy ) / determinant;
  pixel.y = ( -gt[4] * dx + gt[1] * dy ) / determinant;
  return pixel;
}

CrsBoundingBox GridDescriptor::approximateExtent() const
{
  if ( !hasAffinePlacement() )
    throw GeoError( ErrorCode::Unsupported, "approximateExtent: no affine placement" );
  const CrsPoint c0 = pixelToWorld( 0, 0 );
  const CrsPoint c1 = pixelToWorld( width, 0 );
  const CrsPoint c2 = pixelToWorld( width, height );
  const CrsPoint c3 = pixelToWorld( 0, height );
  CrsBoundingBox box;
  box.minX = std::min( { c0.x, c1.x, c2.x, c3.x } );
  box.maxX = std::max( { c0.x, c1.x, c2.x, c3.x } );
  box.minY = std::min( { c0.y, c1.y, c2.y, c3.y } );
  box.maxY = std::max( { c0.y, c1.y, c2.y, c3.y } );
  return box;
}

std::vector<CrsPoint> GridDescriptor::footprintPolygon( int edgeSamples ) const
{
  if ( !hasAffinePlacement() )
    throw GeoError( ErrorCode::Unsupported, "footprintPolygon: no affine placement" );
  const int samples = std::max( 1, edgeSamples );
  std::vector<CrsPoint> polygon;
  polygon.reserve( static_cast<std::size_t>( samples ) * 4 );
  for ( int i = 0; i < samples; ++i )
  {
    const double t = static_cast<double>( i ) / samples;
    polygon.push_back( pixelToWorld( t * width, 0 ) );            // top edge
    polygon.push_back( pixelToWorld( width, t * height ) );       // right edge
    polygon.push_back( pixelToWorld( width - t * width, height ) ); // bottom edge
    polygon.push_back( pixelToWorld( 0, height - t * height ) );  // left edge
  }
  return polygon;
}

GridDescriptor::MatchResult GridDescriptor::matchesReference( const GridDescriptor &reference,
                                                              double tolerance ) const
{
  MatchResult result;
  result.maxDeviation = std::numeric_limits<double>::infinity();

  // Explicit refusal: axis-grid matching is meaningless for control-point
  // placements — the caller gets a reason, never a bare false.
  if ( !hasAffinePlacement() || !reference.hasAffinePlacement() )
  {
    result.note = "non-affine placement (gcp/rpc/unknown) is not axis-matchable";
    return result;
  }

  if ( width != reference.width || height != reference.height )
  {
    result.note = "raster dimensions differ";
    return result;
  }
  if ( kind != reference.kind )
  {
    result.note = "placement kinds differ (" + std::string( gridKindName( kind ) ) + " vs " +
                  gridKindName( reference.kind ) + ")";
    return result;
  }

  const std::array<double, 6> &a = geotransform;
  const std::array<double, 6> &b = reference.geotransform;

  // Pixel size and rotation terms are STRUCTURAL: they define the grid
  // itself, so a relative mismatch is a hard fail regardless of tolerance —
  // a resampled or re-rotated raster is never the same reference grid.
  for ( const int i : { 1, 2, 4, 5 } )
  {
    const double scale = std::max( std::fabs( b[i] ), 1.0 );
    if ( std::fabs( a[i] - b[i] ) / scale > 1e-6 )
    {
      result.note = "pixel size or rotation differs (structural)";
      result.maxDeviation = std::fabs( a[i] - b[i] );
      return result;
    }
  }
  // The ORIGIN carries the caller's tolerance (georeferencing jitter).
  const double originDeviation =
    std::max( std::fabs( a[0] - b[0] ), std::fabs( a[3] - b[3] ) );
  result.maxDeviation = originDeviation;
  result.matches = originDeviation <= tolerance;
  result.note = result.matches ? "grids agree within tolerance" : "origin deviation beyond tolerance";
  return result;
}

ResamplingCategory resamplingCategoryFor( const RasterMetadata &metadata )
{
  for ( const BandInfo &band : metadata.bands )
  {
    const bool palette =
      band.hasColorTable || band.colorInterpretation == "Palette";
    const bool classification =
      band.role == "QA" || band.role == "SceneClassification" || band.role == "Classification";
    const bool categoricalByte = band.dtype == "Byte" && ( palette || classification );
    if ( palette || classification || categoricalByte )
      return ResamplingCategory::Categorical;
  }
  return ResamplingCategory::Continuous;
}

void checkResamplingPolicy( const RasterMetadata &source, const std::string &resampling )
{
  if ( resamplingCategoryFor( source ) != ResamplingCategory::Categorical )
    return;
  std::string lowered = resampling;
  std::transform( lowered.begin(), lowered.end(), lowered.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  const bool classSafe = lowered.empty() || lowered == "near" || lowered == "nearest" ||
                         lowered == "mode";
  if ( !classSafe )
  {
    Json::Value details;
    details["resampling"] = resampling;
    details["category"] = resamplingCategoryName( ResamplingCategory::Categorical );
    throw GeoError( ErrorCode::FidelityLoss,
                    "Categorical source refuses interpolated resampling — averaging "
                    "classes fabricates values that exist nowhere; use near/nearest/mode",
                    details );
  }
}

} // namespace sicnu::geo

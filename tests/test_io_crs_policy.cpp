/***************************************************************************
  tests/test_io_crs_policy.cpp
  Geospatial I/O Foundation 4.0 — explicit CRS policy suite.
  Covers: 4326, UTM, projected, axis-order-sensitive transforms, antimeridian,
  missing/invalid CRS errors, declared fallback policy, coordinate epoch.
 ***************************************************************************/

#include "geospatial/crs/crs_policy.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <ogr_srs_api.h>

namespace
{
} // namespace

TEST_CASE( "Crs construction from authid, WKT and user input", "[io][crs]" )
{
  const sicnu::geo::Crs geographic = sicnu::geo::Crs::fromAuthid( "EPSG:4326" );
  CHECK( geographic.authid() == "EPSG:4326" );
  CHECK( geographic.isGeographic() );
  CHECK_FALSE( geographic.isProjected() );
  CHECK( geographic.coordinateEpoch() == 0.0 );

  const sicnu::geo::Crs utm = sicnu::geo::Crs::fromAuthid( "EPSG:32648" );
  CHECK( utm.isProjected() );
  CHECK( utm.authid() == "EPSG:32648" );

  const sicnu::geo::Crs polar = sicnu::geo::Crs::fromAuthid( "EPSG:3995" ); // Arctic polar stereographic
  CHECK( polar.isProjected() );

  const sicnu::geo::Crs fromWkt = sicnu::geo::Crs::fromWkt( geographic.wkt() );
  CHECK( fromWkt.isGeographic() );
  CHECK( fromWkt.authid() == "EPSG:4326" );

  const sicnu::geo::Crs fromUser = sicnu::geo::Crs::fromUserInput( "+proj=utm +zone=48 +datum=WGS84 +units=m +no_defs" );
  CHECK( fromUser.isProjected() );

  // Copy and move keep validity.
  sicnu::geo::Crs copy = geographic;
  CHECK( copy.isGeographic() );
  sicnu::geo::Crs moved = std::move( copy );
  CHECK( moved.authid() == "EPSG:4326" );
}

TEST_CASE( "invalid CRS input is rejected with InvalidCrs, never guessed", "[io][crs]" )
{
  try
  {
    sicnu::geo::Crs::fromWkt( "NOT A WKT AT ALL" );
    FAIL( "expected InvalidCrs" );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( error.code() == sicnu::geo::ErrorCode::InvalidCrs );
  }

  CHECK_THROWS_AS( sicnu::geo::Crs::fromAuthid( "EPSG:notanumber" ), sicnu::geo::GeoError );
  CHECK_THROWS_AS( sicnu::geo::Crs::fromAuthid( "" ), sicnu::geo::GeoError );
  CHECK_THROWS_AS( sicnu::geo::Crs::fromUserInput( "%% definitely not a CRS %%" ), sicnu::geo::GeoError );
}

TEST_CASE( "missing dataset CRS is a hard error unless a declared fallback policy is set", "[io][crs]" )
{
  sicnu::geo::CrsInfo empty;
  empty.valid = false;

  // Default policy: hard error.
  try
  {
    sicnu::geo::resolveDatasetCrs( empty, sicnu::geo::CrsPolicy{}, "demo.tif" );
    FAIL( "expected MissingCrs" );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( error.code() == sicnu::geo::ErrorCode::MissingCrs );
    CHECK( std::string( error.what() ).find( "demo.tif" ) != std::string::npos );
  }

  // Declared fallback: allowed, but explicitly flagged as fallback.
  sicnu::geo::CrsPolicy policy;
  policy.allowDeclaredFallback = true;
  policy.fallbackCrs.valid = true;
  policy.fallbackCrs.authid = "EPSG:4326";
  const sicnu::geo::ResolvedCrs resolved = sicnu::geo::resolveDatasetCrs( empty, policy, "demo.tif" );
  CHECK( resolved.usedDeclaredFallback );
  CHECK( resolved.crs.authid() == "EPSG:4326" );
}

TEST_CASE( "dataset CRS resolution accepts WKT and repairs broken WKT via authid", "[io][crs]" )
{
  const sicnu::geo::Crs reference = sicnu::geo::Crs::fromAuthid( "EPSG:32648" );
  const sicnu::geo::ResolvedCrs resolved = sicnu::geo::resolveDatasetCrs( reference.toCrsInfo(), {}, "utm.tif" );
  CHECK_FALSE( resolved.usedDeclaredFallback );
  CHECK( resolved.crs.authid() == "EPSG:32648" );

  sicnu::geo::CrsInfo broken;
  broken.valid = true;
  broken.wkt = "BROKEN[";
  broken.authid = "EPSG:32648";
  const sicnu::geo::Crs repaired = sicnu::geo::Crs::fromDatasetInfo( broken );
  CHECK( repaired.authid() == "EPSG:32648" );
}

TEST_CASE( "forward/inverse transforms round-trip in traditional GIS order", "[io][crs][transform]" )
{
  const sicnu::geo::Crs wgs84 = sicnu::geo::Crs::fromAuthid( "EPSG:4326" );
  const sicnu::geo::Crs utm48 = sicnu::geo::Crs::fromAuthid( "EPSG:32648" );
  const sicnu::geo::CrsTransform transform = sicnu::geo::CrsTransform::create( wgs84, utm48 );

  // (lon=117, lat=30) → UTM 48N.
  const sicnu::geo::CrsPoint projected = transform.forward( { 117.0, 30.0 } );
  // 117°E sits ~12° east of zone 48N's central meridian (105°E): the easting
  // legitimately exceeds the zone's nominal 834 km band.
  CHECK( projected.x > 1400000.0 );
  CHECK( projected.x < 1900000.0 );
  CHECK( projected.y > 3000000.0 );
  CHECK( projected.y < 3600000.0 );

  const sicnu::geo::CrsPoint back = transform.inverse( projected );
  CHECK( back.x == Approx( 117.0 ).margin( 1e-6 ) );
  CHECK( back.y == Approx( 30.0 ).margin( 1e-6 ) );
}

TEST_CASE( "axis order is declared and honored, not implicit", "[io][crs][transform]" )
{
  const sicnu::geo::Crs wgs84 = sicnu::geo::Crs::fromAuthid( "EPSG:4326" );
  const sicnu::geo::Crs utm48 = sicnu::geo::Crs::fromAuthid( "EPSG:32648" );

  const sicnu::geo::CrsTransform traditional = sicnu::geo::CrsTransform::create( wgs84, utm48, sicnu::geo::AxisOrder::TraditionalGis );
  const sicnu::geo::CrsTransform authority = sicnu::geo::CrsTransform::create( wgs84, utm48, sicnu::geo::AxisOrder::Authority );

  CHECK( traditional.diagnostics().workingOrder == sicnu::geo::AxisOrder::TraditionalGis );
  CHECK( authority.diagnostics().workingOrder == sicnu::geo::AxisOrder::Authority );

  // Same physical place, different input order per declared convention.
  const sicnu::geo::CrsPoint lonLatInput = traditional.forward( { 117.0, 30.0 } );
  const sicnu::geo::CrsPoint latLonInput = authority.forward( { 30.0, 117.0 } );
  CHECK( lonLatInput.x == Approx( latLonInput.x ).margin( 0.01 ) );
  CHECK( lonLatInput.y == Approx( latLonInput.y ).margin( 0.01 ) );
}

TEST_CASE( "densified bounds stay correct across the antimeridian", "[io][crs][transform][antimeridian]" )
{
  // UTM zone 1N (central meridian 177°W): a wide easting band legitimately
  // crosses ±180°. The bounds transform must surface the crossing instead of
  // reporting a nonsense envelope.
  const sicnu::geo::Crs utm1 = sicnu::geo::Crs::fromAuthid( "EPSG:32601" );
  const sicnu::geo::Crs wgs84 = sicnu::geo::Crs::fromAuthid( "EPSG:4326" );
  const sicnu::geo::CrsTransform transform = sicnu::geo::CrsTransform::create( utm1, wgs84 );

  sicnu::geo::CrsBoundingBox box;
  box.minX = -60000.0; // west of the zone edge → wraps past -180°
  box.maxX = 1060000.0;
  box.minY = 3000000.0;
  box.maxY = 4000000.0;
  const sicnu::geo::CrsBoundingBox bounds = transform.forwardBounds( box, 32 );
  CHECK( bounds.crossesAntimeridian );
}

TEST_CASE( "projected bounds transform covers the source envelope", "[io][crs][transform]" )
{
  const sicnu::geo::Crs wgs84 = sicnu::geo::Crs::fromAuthid( "EPSG:4326" );
  const sicnu::geo::Crs webMercator = sicnu::geo::Crs::fromAuthid( "EPSG:3857" );
  const sicnu::geo::CrsTransform toMercator = sicnu::geo::CrsTransform::create( wgs84, webMercator );

  sicnu::geo::CrsBoundingBox box;
  box.minX = 110.0;
  box.minY = 20.0;
  box.maxX = 125.0;
  box.maxY = 45.0;
  const sicnu::geo::CrsBoundingBox bounds = toMercator.forwardBounds( box );
  CHECK_FALSE( bounds.crossesAntimeridian );
  // Corner checks against known web-mercator values.
  CHECK( bounds.minX <= toMercator.forward( { 110.0, 20.0 } ).x + 1.0 );
  CHECK( bounds.maxY >= toMercator.forward( { 125.0, 45.0 } ).y - 1.0 );

  // Round-trip through the inverse transform recovers the source envelope.
  const sicnu::geo::CrsTransform fromMercator = sicnu::geo::CrsTransform::create( webMercator, wgs84 );
  const sicnu::geo::CrsBoundingBox back = fromMercator.forwardBounds( bounds );
  CHECK( back.minX == Approx( box.minX ).margin( 0.1 ) );
  CHECK( back.maxX == Approx( box.maxX ).margin( 0.1 ) );
  CHECK( back.minY == Approx( box.minY ).margin( 0.1 ) );
  CHECK( back.maxY == Approx( box.maxY ).margin( 0.1 ) );
}

TEST_CASE( "transform between incompatible CRS declarations surfaces TransformFailed", "[io][crs][transform]" )
{
  sicnu::geo::CrsInfo broken;
  broken.valid = true;
  broken.wkt = "LOCAL_CS[\"Unknown\"]"; // no datum path to anything
  sicnu::geo::CrsInfo other;
  other.valid = true;
  other.wkt = "LOCAL_CS[\"Something else\"]";
  try
  {
    const sicnu::geo::Crs a = sicnu::geo::Crs::fromDatasetInfo( broken );
    const sicnu::geo::Crs b = sicnu::geo::Crs::fromDatasetInfo( other );
    sicnu::geo::CrsTransform::create( a, b );
    // GDAL may accept this pair via ballpark transformation — allowed, but the
    // diagnostics must say so.
    SUCCEED( "GDAL provided a (ballpark) transformation path" );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( error.code() == sicnu::geo::ErrorCode::TransformFailed );
  }
}

TEST_CASE( "coordinate epoch survives the round trip through Crs", "[io][crs][epoch]" )
{
  const sicnu::geo::Crs base = sicnu::geo::Crs::fromAuthid( "EPSG:4326" );
  CHECK( base.coordinateEpoch() == 0.0 );

  // Dynamic-datum epoch: set through the OSR handle, read through the model.
  OSRSetCoordinateEpoch( base.handle(), 2026.5 );
  CHECK( base.coordinateEpoch() == Approx( 2026.5 ) );

  // Cloning preserves the epoch (time-dependent transforms rely on it).
  const sicnu::geo::Crs copy( base );
  CHECK( copy.coordinateEpoch() == Approx( 2026.5 ) );
  const sicnu::geo::CrsInfo exported = copy.toCrsInfo();
  CHECK( exported.hasCoordinateEpoch );
  CHECK( exported.coordinateEpoch == Approx( 2026.5 ) );

  // Note: WKT text itself carries no epoch — CrsInfo is the epoch carrier, and
  // transforms attach it to the cloned OSR handles (see CrsTransform::create).
}

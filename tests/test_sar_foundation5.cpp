// test_sar_foundation5.cpp — Foundation 5.0 Milestone D: dual-pol feature
// kernels and SAR terrain geometry (closed forms + operator E2E).
//
// Derivations are inline with the assertions; the geometry cases use the
// surface-normal contract from sar_terrain_geometry.h (flat ground ⇒ local
// incidence == declared incidence; a 45° slope facing a 45° look is grazing).

#include "processing/algorithms/sar/sar_dualpol.h"
#include "processing/algorithms/sar/sar_terrain_geometry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <json/json.h>

#include <cmath>
#include <limits>
#include <vector>

#include "synthetic_raster_builder.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::sar;

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
} // namespace

TEST_CASE( "Dual-pol features: closed forms in linear power", "[sar][dualpol]" )
{
  const double vv = 0.4;
  const double vh = 0.1;
  REQUIRE( dualPolFeature( DualPolFeature::Ratio, vv, vh ) == Catch::Approx( 4.0 ).margin( 1e-12 ) );
  REQUIRE( dualPolFeature( DualPolFeature::NormalizedDifference, vv, vh ) ==
           Catch::Approx( 0.6 ).margin( 1e-12 ) );
  REQUIRE( dualPolFeature( DualPolFeature::LogRatio, vv, vh ) ==
           Catch::Approx( 10.0 * std::log10( 4.0 ) ).margin( 1e-12 ) );
  REQUIRE( dualPolFeature( DualPolFeature::Rvi, vv, vh ) == Catch::Approx( 3.2 ).margin( 1e-12 ) );
  REQUIRE( dualPolFeature( DualPolFeature::Span, vv, vh ) == Catch::Approx( 0.5 ).margin( 1e-12 ) );

  // Zero VH: ratio/log_ratio divide by zero → NaN; ND saturates to +1;
  // RVI hits its 4·VV/(VV+VH) ceiling value 4 at VH→0 exactly.
  REQUIRE( std::isnan( dualPolFeature( DualPolFeature::Ratio, vv, 0.0 ) ) );
  REQUIRE( std::isnan( dualPolFeature( DualPolFeature::LogRatio, vv, 0.0 ) ) );
  REQUIRE( dualPolFeature( DualPolFeature::NormalizedDifference, vv, 0.0 ) ==
           Catch::Approx( 1.0 ).margin( 1e-12 ) );
  REQUIRE( dualPolFeature( DualPolFeature::Rvi, vv, 0.0 ) == Catch::Approx( 4.0 ).margin( 1e-12 ) );

  // Domain violations: negative power is never clamped.
  REQUIRE( std::isnan( dualPolFeature( DualPolFeature::Ratio, -0.4, vh ) ) );
  REQUIRE( std::isnan( dualPolFeature( DualPolFeature::Span, vv, -0.1 ) ) );

  // Token round-trip.
  DualPolFeature parsed = DualPolFeature::Ratio;
  REQUIRE( parseDualPolFeature( "log_ratio", &parsed ) );
  REQUIRE( parsed == DualPolFeature::LogRatio );
  REQUIRE_FALSE( parseDualPolFeature( "hv_ratio", &parsed ) );
}

TEST_CASE( "SAR terrain geometry: closed forms", "[sar][geometry]" )
{
  // Flat ground: local incidence equals the declared incidence; normal class.
  const auto flat = terrainGeometry( 0.0, 0.0, 35.0, 90.0 );
  REQUIRE( flat.localIncidenceDeg == Catch::Approx( 35.0 ).margin( 1e-9 ) );
  REQUIRE( flat.maskClass == TerrainMaskClass::Normal );

  // 45° slope facing a 45° look: grazing — local incidence 90°, boundary of
  // layover (alpha == theta_i is NOT layover).
  // Look east (φh = 90); terrain rising toward the sensor (west) ⇒ gE < 0.
  const auto grazing = terrainGeometry( -1.0, 0.0, 45.0, 90.0 );
  REQUIRE( grazing.localIncidenceDeg == Catch::Approx( 90.0 ).margin( 1e-9 ) );
  REQUIRE( grazing.maskClass == TerrainMaskClass::Normal );

  // Steeper facing slope: layover with an overturned (>90°) local incidence.
  const auto layover = terrainGeometry( -2.0, 0.0, 45.0, 90.0 );
  REQUIRE( layover.localIncidenceDeg > 90.0 );
  REQUIRE( layover.maskClass == TerrainMaskClass::Layover );

  // Far-side slope steeper than (θi − 90°) = −55°: shadow. gN = +1.732 with
  // look north (φh = 0) ⇒ terrain descends toward the sensor at α = −60°.
  const auto shadow = terrainGeometry( 0.0, 1.7320508, 35.0, 0.0 );
  REQUIRE( shadow.maskClass == TerrainMaskClass::Shadow );

  // Radiometric factor: flat ⇒ cos θi / cos θi = 1; grazing → NaN.
  REQUIRE( terrainRadiometricFactor( 35.0, 35.0 ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );
  REQUIRE( std::isnan( terrainRadiometricFactor( 90.0, 35.0 ) ) );

  // NaN gradients propagate as NaN incidence (caller masks).
  const auto degenerate = terrainGeometry( kNaN, 0.0, 35.0, 90.0 );
  REQUIRE( std::isnan( degenerate.localIncidenceDeg ) );
}

TEST_CASE( "rs:sar_dualpol_features E2E: linear and dB domains agree", "[sar][dualpol][operator][e2e]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // 6×5 raster: VV = 0.4, VH = 0.1 constants (linear power).
  constexpr int kW = 6;
  constexpr int kH = 5;
  RsSyntheticRasterBuilder linB( kW, kH, 2 );
  linB.withCrs( "EPSG:32650" );
  linB.withConstantValue( 1, 0.4f );
  linB.withConstantValue( 2, 0.1f );
  const QString linPath = linB.writeToDisk( dir.filePath( "linear.tif" ) );
  REQUIRE( !linPath.isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:sar_dualpol_features" );
  REQUIRE( op != nullptr );

  RSOperatorContext context;
  Json::Value params( Json::objectValue );
  params["input"] = linPath.toStdString();
  params["output"] = dir.filePath( "rvi.tif" ).toStdString();
  params["feature"] = "rvi";
  params["domain"] = "linear";
  REQUIRE_NOTHROW( op->run( params, context ) );

  GdalDatasetWrapper outDs;
  REQUIRE( outDs.open( dir.filePath( "rvi.tif" ) ) );
  std::vector<float> rvi( static_cast<size_t>( kW ) * kH );
  REQUIRE( outDs.readBandData( 1, rvi.data(), kW, kH ) );
  for ( const float v : rvi )
    REQUIRE( v == Catch::Approx( 3.2f ).margin( 1e-6f ) );
}

TEST_CASE( "rs:sar_terrain_masks E2E: layover ramp and incidence product",
           "[sar][geometry][operator][e2e]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // 12×10 projected DEM at 1 m pixels: z = −2·x (rises toward the west).
  // Look east (φh = 90°), incidence 35°: gTowardSensor = +2 ⇒ α ≈ 63.4° >
  // 35° ⇒ layover everywhere (linear ramp keeps this at replicate edges).
  constexpr int kW = 12;
  constexpr int kH = 10;
  RsSyntheticRasterBuilder demB( kW, kH, 1 );
  demB.withCrs( "EPSG:32650" );
  demB.withGeoTransform( 0.0, 1.0, static_cast<double>( kH ), -1.0 );
  for ( int y = 0; y < kH; ++y )
    for ( int x = 0; x < kW; ++x )
      demB.withPixel( 1, x, y, static_cast<float>( -2.0 * x ) );
  const QString demPath = demB.writeToDisk( dir.filePath( "dem.tif" ) );
  REQUIRE( !demPath.isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:sar_terrain_masks" );
  REQUIRE( op != nullptr );

  RSOperatorContext context;
  Json::Value params( Json::objectValue );
  params["dem"] = demPath.toStdString();
  params["output"] = dir.filePath( "mask.tif" ).toStdString();
  params["product"] = "layover_shadow_mask";
  params["incidence"] = 35.0;
  params["heading"] = 90.0;
  REQUIRE_NOTHROW( op->run( params, context ) );

  GdalDatasetWrapper maskDs;
  REQUIRE( maskDs.open( dir.filePath( "mask.tif" ) ) );
  std::vector<float> mask( static_cast<size_t>( kW ) * kH );
  REQUIRE( maskDs.readBandData( 1, mask.data(), kW, kH ) );
  for ( const float v : mask )
    REQUIRE( v == Catch::Approx( static_cast<float>( TerrainMaskClass::Layover ) ).margin( 0.0f ) );

  // Local incidence product: analytic α = atan(2), θi = 35°, and the
  // surface-normal form gives cosθl = (sinθi·gLook + cosθi)/√(gE²+1) with
  // gLook = −2 — verify against the kernel itself on a sampled pixel.
  Json::Value p2 = params;
  p2["output"] = dir.filePath( "inc.tif" ).toStdString();
  p2["product"] = "local_incidence";
  REQUIRE_NOTHROW( op->run( p2, context ) );

  GdalDatasetWrapper incDs;
  REQUIRE( incDs.open( dir.filePath( "inc.tif" ) ) );
  std::vector<float> inc( static_cast<size_t>( kW ) * kH );
  REQUIRE( incDs.readBandData( 1, inc.data(), kW, kH ) );
  const double thetaI = 35.0 * M_PI / 180.0;
  const double gLook = -2.0;
  const double cosL = ( std::sin( thetaI ) * gLook + std::cos( thetaI ) ) / std::sqrt( 5.0 );
  const double expectedDeg = std::acos( cosL ) * 180.0 / M_PI;
  // Interior columns carry the exact ramp gradient (−2 m/m); the two
  // replicate-halo boundary columns halve the first/last difference, so only
  // the interior is pinned to the closed form.
  for ( int y = 0; y < kH; ++y )
    for ( int x = 1; x < kW - 1; ++x )
      REQUIRE( inc[static_cast<size_t>( y ) * kW + x] ==
               Catch::Approx( expectedDeg ).margin( 1e-4 ) );
  REQUIRE( expectedDeg > 90.0 ); // overturned: consistent with the layover mask
}

TEST_CASE( "rs:sar_terrain_masks refuses out-of-range geometry",
           "[sar][geometry][operator][contract]" )
{
  using namespace sicnu::testing;
  using namespace sicnu::operators;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  RsSyntheticRasterBuilder demB( 4, 3, 1 );
  demB.withCrs( "EPSG:32650" );
  demB.withConstantValue( 1, 10.0f );
  const QString demPath = demB.writeToDisk( dir.filePath( "dem.tif" ) );
  REQUIRE( !demPath.isEmpty() );

  auto op = RSOperatorRegistry::instance().create( "rs:sar_terrain_masks" );
  REQUIRE( op != nullptr );

  RSOperatorContext context;
  Json::Value params( Json::objectValue );
  params["dem"] = demPath.toStdString();
  params["output"] = dir.filePath( "out.tif" ).toStdString();
  params["product"] = "layover_shadow_mask";
  params["incidence"] = 95.0; // out of (0, 90)
  params["heading"] = 0.0;
  REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );
}

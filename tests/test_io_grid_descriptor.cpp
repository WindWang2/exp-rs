/***************************************************************************
  tests/test_io_grid_descriptor.cpp — Cloud-Native Geospatial I/O 7.0 (M6):
  generalized grid classification & placement:
    * north-up / rotated / flipped / GCP / RPC classification
    * full-affine pixel↔world round-trips (rotation terms included)
    * densified footprint vs axis-aligned extent on a rotated grid
    * reference-grid matching with tolerances and explicit refusals
    * categorical resampling policy (palette/classification refusal)
    * a rotated geotransform survives a real file round-trip
 ***************************************************************************/

#include "geospatial/crs/grid_descriptor.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <cmath>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace
{

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_grid" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

sicnu::geo::RasterMetadata northUpMetadata( int width = 10, int height = 6 )
{
  sicnu::geo::RasterMetadata metadata;
  metadata.width = width;
  metadata.height = height;
  metadata.bandCount = 1;
  metadata.hasGeotransform = true;
  metadata.geotransform = { 100.0, 10.0, 0.0, 5000.0, 0.0, -10.0 };
  metadata.crs.authid = "EPSG:32633";
  metadata.crs.valid = true;
  sicnu::geo::BandInfo band;
  band.index = 1;
  band.dtype = "Float32";
  metadata.bands.push_back( band );
  return metadata;
}

} // namespace

TEST_CASE( "north-up grids classify and place through the full affine",
           "[io][grid]" )
{
  const sicnu::geo::GridDescriptor descriptor =
    sicnu::geo::GridDescriptor::fromMetadata( northUpMetadata() );
  CHECK( descriptor.kind == sicnu::geo::GridKind::NorthUp );
  CHECK( descriptor.isNorthUp() );
  CHECK_FALSE( descriptor.flippedX );
  CHECK( descriptor.flippedY == false );
  CHECK( descriptor.rotationDegrees == Approx( 0.0 ).margin( 1e-9 ) );

  // Pixel (0,0) is the origin; pixel (w,h) is the far corner.
  const sicnu::geo::CrsPoint origin = descriptor.pixelToWorld( 0, 0 );
  CHECK( origin.x == Approx( 100.0 ) );
  CHECK( origin.y == Approx( 5000.0 ) );
  const sicnu::geo::CrsPoint corner = descriptor.pixelToWorld( 10, 6 );
  CHECK( corner.x == Approx( 200.0 ) );
  CHECK( corner.y == Approx( 4940.0 ) );

  // Round-trip through the inverse affine.
  const sicnu::geo::CrsPoint back = descriptor.worldToPixel( corner );
  CHECK( back.x == Approx( 10.0 ) );
  CHECK( back.y == Approx( 6.0 ) );
}

TEST_CASE( "rotated grids classify, rotate and footprint honestly",
           "[io][grid][rotated]" )
{
  sicnu::geo::RasterMetadata metadata = northUpMetadata();
  // 90-degree rotation: the pixel x axis runs along -y.
  metadata.geotransform = { 5000.0, 0.0, -10.0, 100.0, 10.0, 0.0 };
  const sicnu::geo::GridDescriptor descriptor =
    sicnu::geo::GridDescriptor::fromMetadata( metadata );
  CHECK( descriptor.kind == sicnu::geo::GridKind::Rotated );
  CHECK( descriptor.rotationDegrees == Approx( 90.0 ).margin( 1e-6 ) );

  // Full-affine placement includes the rotation terms.
  const sicnu::geo::CrsPoint placed = descriptor.pixelToWorld( 3, 2 );
  CHECK( placed.x == Approx( 5000.0 + 2 * ( -10.0 ) ) );
  CHECK( placed.y == Approx( 100.0 + 3 * 10.0 ) );

  // Round-trip through the inverse affine.
  const sicnu::geo::CrsPoint back = descriptor.worldToPixel( placed );
  CHECK( back.x == Approx( 3.0 ) );
  CHECK( back.y == Approx( 2.0 ) );

  // A rotated grid's true footprint is densified; its axis-aligned extent
  // is only a lower bound (strictly smaller area than the honest footprint
  // would suggest for a 90-degree rotated rectangle... the bbox here is
  // exact for 90 degrees, so rotate by 30 degrees to see the difference).
  sicnu::geo::RasterMetadata tilted = northUpMetadata( 100, 50 );
  const double cos30 = std::sqrt( 3.0 ) / 2.0;
  const double sin30 = 0.5;
  tilted.geotransform = { 0.0, 10.0 * cos30, -10.0 * sin30, 0.0, 10.0 * sin30, -10.0 * cos30 };
  const sicnu::geo::GridDescriptor tiltedDescriptor =
    sicnu::geo::GridDescriptor::fromMetadata( tilted );
  CHECK( tiltedDescriptor.kind == sicnu::geo::GridKind::Rotated );
  CHECK( tiltedDescriptor.rotationDegrees == Approx( 30.0 ).margin( 1e-6 ) );

  const sicnu::geo::CrsBoundingBox extent = tiltedDescriptor.approximateExtent();
  const auto footprint = tiltedDescriptor.footprintPolygon( 64 );
  REQUIRE_FALSE( footprint.empty() );
  double footprintMinY = footprint.front().y;
  double footprintMaxY = footprint.front().y;
  for ( const auto &point : footprint )
  {
    footprintMinY = std::min( footprintMinY, point.y );
    footprintMaxY = std::max( footprintMaxY, point.y );
  }
  // Densified footprint edges sample the true rotated boundary: its y range
  // matches the analytic 30-degree extent (height*cos30 + width*sin30 scaled).
  const double analyticHeight = ( 50.0 * 10.0 * cos30 ) + ( 100.0 * 10.0 * sin30 );
  CHECK( ( footprintMaxY - footprintMinY ) == Approx( analyticHeight ).margin( 1.0 ) );
  CHECK( ( extent.maxY - extent.minY ) == Approx( analyticHeight ).margin( 1.0 ) );
}

TEST_CASE( "flipped axes classify as flipped, not north-up",
           "[io][grid][flipped]" )
{
  sicnu::geo::RasterMetadata metadata = northUpMetadata();
  metadata.geotransform[5] = 10.0; // y axis runs downward->upward reversed
  const sicnu::geo::GridDescriptor descriptor =
    sicnu::geo::GridDescriptor::fromMetadata( metadata );
  CHECK( descriptor.kind == sicnu::geo::GridKind::Flipped );
  CHECK( descriptor.flippedY );
  CHECK_FALSE( descriptor.flippedX );

  sicnu::geo::RasterMetadata flippedX = northUpMetadata();
  flippedX.geotransform[1] = -10.0;
  CHECK( sicnu::geo::GridDescriptor::fromMetadata( flippedX ).kind ==
         sicnu::geo::GridKind::Flipped );
}

TEST_CASE( "GCP and RPC placements classify and refuse axis matching",
           "[io][grid][gcp][rpc]" )
{
  sicnu::geo::RasterMetadata gcpMeta = northUpMetadata();
  gcpMeta.hasGeotransform = false;
  gcpMeta.hasGcps = true;
  gcpMeta.gcpCount = 4;
  const sicnu::geo::GridDescriptor gcpDescriptor =
    sicnu::geo::GridDescriptor::fromMetadata( gcpMeta );
  CHECK( gcpDescriptor.kind == sicnu::geo::GridKind::GcpBased );

  sicnu::geo::RasterMetadata rpcMeta = northUpMetadata();
  rpcMeta.hasGeotransform = false;
  rpcMeta.hasRpc = true;
  CHECK( sicnu::geo::GridDescriptor::fromMetadata( rpcMeta ).kind ==
         sicnu::geo::GridKind::RpcBased );

  // Axis matching refuses non-affine placements with a reason.
  const sicnu::geo::GridDescriptor reference = sicnu::geo::GridDescriptor::fromMetadata( northUpMetadata() );
  const auto gcpMatch = gcpDescriptor.matchesReference( reference, 0.5 );
  CHECK_FALSE( gcpMatch.matches );
  CHECK( gcpMatch.note.find( "not axis-matchable" ) != std::string::npos );

  // Degenerate (singular) affine refuses the inverse placement.
  sicnu::geo::RasterMetadata degenerate = northUpMetadata();
  degenerate.geotransform = { 100.0, 0.0, 0.0, 5000.0, 0.0, 0.0 };
  const sicnu::geo::GridDescriptor degenerateDescriptor =
    sicnu::geo::GridDescriptor::fromMetadata( degenerate );
  CHECK_THROWS_AS( degenerateDescriptor.worldToPixel( { 150.0, 4950.0 } ), sicnu::geo::GeoError );
}

TEST_CASE( "reference-grid matching honors tolerance and detects shifts",
           "[io][grid][matching]" )
{
  const sicnu::geo::GridDescriptor reference =
    sicnu::geo::GridDescriptor::fromMetadata( northUpMetadata() );

  // Identical grid matches exactly.
  const auto identical = reference.matchesReference( reference, 0.25 );
  CHECK( identical.matches );
  CHECK( identical.maxDeviation == Approx( 0.0 ).margin( 1e-12 ) );

  // A half-pixel shift is within a 5-unit tolerance…
  sicnu::geo::RasterMetadata shifted = northUpMetadata();
  shifted.geotransform[0] += 4.0;
  shifted.geotransform[3] -= 3.0;
  const auto within = reference.matchesReference(
    sicnu::geo::GridDescriptor::fromMetadata( shifted ), 5.0 );
  CHECK( within.matches );
  CHECK( within.maxDeviation == Approx( 4.0 ) );
  // …but outside a 2-unit tolerance.
  CHECK_FALSE( reference.matchesReference(
                 sicnu::geo::GridDescriptor::fromMetadata( shifted ), 2.0 ).matches );

  // A pixel-size difference is a hard mismatch regardless of tolerance.
  sicnu::geo::RasterMetadata resampled = northUpMetadata();
  resampled.geotransform[1] = 20.0;
  const auto different = reference.matchesReference(
    sicnu::geo::GridDescriptor::fromMetadata( resampled ), 100.0 );
  CHECK_FALSE( different.matches );

  // Different dimensions never match.
  sicnu::geo::RasterMetadata resized = northUpMetadata( 11, 6 );
  CHECK_FALSE( reference.matchesReference(
                 sicnu::geo::GridDescriptor::fromMetadata( resized ), 10.0 ).matches );

  // Kind differences refuse with a reason.
  sicnu::geo::RasterMetadata rotated = northUpMetadata();
  rotated.geotransform = { 5000.0, 0.0, -10.0, 100.0, 10.0, 0.0 };
  const auto rotatedMatch = reference.matchesReference(
    sicnu::geo::GridDescriptor::fromMetadata( rotated ), 10.0 );
  CHECK_FALSE( rotatedMatch.matches );
  CHECK( rotatedMatch.note.find( "placement kinds differ" ) != std::string::npos );
}

TEST_CASE( "categorical sources refuse interpolated resampling",
           "[io][grid][categorical]" )
{
  // Palette band → categorical.
  sicnu::geo::RasterMetadata palette = northUpMetadata();
  palette.bands[0].dtype = "Byte";
  palette.bands[0].hasColorTable = true;
  CHECK( sicnu::geo::resamplingCategoryFor( palette ) ==
         sicnu::geo::ResamplingCategory::Categorical );
  CHECK_THROWS_AS( sicnu::geo::checkResamplingPolicy( palette, "bilinear" ),
                   sicnu::geo::GeoError );
  CHECK_THROWS_AS( sicnu::geo::checkResamplingPolicy( palette, "cubic" ),
                   sicnu::geo::GeoError );
  CHECK_NOTHROW( sicnu::geo::checkResamplingPolicy( palette, "near" ) );
  CHECK_NOTHROW( sicnu::geo::checkResamplingPolicy( palette, "mode" ) );

  // Classification role → categorical even without a color table.
  sicnu::geo::RasterMetadata classification = northUpMetadata();
  classification.bands[0].dtype = "Byte";
  classification.bands[0].role = "SceneClassification";
  CHECK( sicnu::geo::resamplingCategoryFor( classification ) ==
         sicnu::geo::ResamplingCategory::Categorical );
  CHECK_THROWS_AS( sicnu::geo::checkResamplingPolicy( classification, "average" ),
                   sicnu::geo::GeoError );

  // Continuous bands keep every kernel.
  sicnu::geo::RasterMetadata continuous = northUpMetadata();
  CHECK( sicnu::geo::resamplingCategoryFor( continuous ) ==
         sicnu::geo::ResamplingCategory::Continuous );
  CHECK_NOTHROW( sicnu::geo::checkResamplingPolicy( continuous, "bilinear" ) );
}

TEST_CASE( "a rotated geotransform survives a real file round-trip",
           "[io][grid][rotated]" )
{
  const std::string dir = scratch( "rotated_file" );
  const std::string path = dir + "/rotated.tif";
  const std::array<double, 6> rotatedGt = { 5000.0, 6.0, 8.0, 100.0, 8.0, -6.0 };
  {
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
      path, 20, 10, { sicnu::geo::RasterBandSpec {} },
      { "GTiff", { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" }, true } );
    writer.setGeotransform( rotatedGt );
    std::vector<double> values( 200 );
    for ( std::size_t i = 0; i < values.size(); ++i )
      values[i] = static_cast<double>( i );
    writer.writeWindow( 1, { 0, 0, 20, 10 }, values.data() );
    writer.finalize();
  }

  const sicnu::geo::RasterMetadata metadata = sicnu::geo::inspectRaster( path );
  REQUIRE( metadata.hasGeotransform );
  for ( int i = 0; i < 6; ++i )
    CHECK( metadata.geotransform[i] == Approx( rotatedGt[i] ) );

  const sicnu::geo::GridDescriptor descriptor =
    sicnu::geo::GridDescriptor::fromMetadata( metadata );
  CHECK( descriptor.kind == sicnu::geo::GridKind::Rotated );
  CHECK( descriptor.rotationDegrees == Approx( std::atan2( 8.0, 6.0 ) * 180.0 / 3.14159265358979323846 )
           .margin( 1e-6 ) );

  // The round-tripped file places pixels exactly where the affine says.
  const sicnu::geo::CrsPoint placed = descriptor.pixelToWorld( 20, 10 );
  CHECK( placed.x == Approx( rotatedGt[0] + rotatedGt[1] * 20 + rotatedGt[2] * 10 ) );
  CHECK( placed.y == Approx( rotatedGt[3] + rotatedGt[4] * 20 + rotatedGt[5] * 10 ) );

  // Reference matching recognizes the round-tripped grid as itself.
  const auto match = descriptor.matchesReference( descriptor, 1e-9 );
  CHECK( match.matches );
}

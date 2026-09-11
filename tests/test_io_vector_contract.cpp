/***************************************************************************
  tests/test_io_vector_contract.cpp
  Geospatial I/O Foundation 4.0 — vector streaming read/write contract suite.
  Includes the 100k-feature bounded-streaming acceptance case.
 ***************************************************************************/

#include "geospatial/vector/vector_reader.h"
#include "geospatial/vector/vector_writer.h"
#include "geospatial/util/atomic_fs.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

std::string scratchDir( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_vector" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}

std::vector<sicnu::geo::VectorFieldSpec> demoFields()
{
  return {
    { "name", "String", 32 },
    { "value", "Real" },
    { "code", "Integer64" },
  };
}

} // namespace

TEST_CASE( "vector writer streams features and publishes atomically", "[io][vector][contract]" )
{
  const std::string target = ( fs::path( scratchDir( "write" ) ) / "cities.gpkg" ).string();
  {
    // A cancelled staging attempt must leave nothing behind.
    sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
      target, "cities", "Point", demoFields(), sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
    writer.cancel();
  }

  // The cancelled attempt left nothing behind.
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target ) );

  sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
    target, "cities", "Point", demoFields(), sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
  Json::Value attrs( Json::objectValue );
  attrs["name"] = "Chengdu";
  attrs["value"] = 500.0;
  attrs["code"] = static_cast<Json::Int64>( 2701 );
  writer.writeFeature( attrs, "POINT (104.0668 30.5728)" );

  Json::Value attrs2( Json::objectValue );
  attrs2["name"] = "Beijing";
  attrs2["value"] = 1000.0;
  attrs2["code"] = static_cast<Json::Int64>( 1100 );
  writer.writeFeature( attrs2, "POINT (116.4074 39.9042)" );
  writer.finalize();

  sicnu::geo::VectorReader reader = sicnu::geo::VectorReader::open( target );
  CHECK( reader.layerInfo().featureCount == 2 );
  CHECK( reader.layerInfo().crs.authid == "EPSG:4326" );
  REQUIRE( reader.layerInfo().fields.size() == 3 );
  CHECK( reader.layerInfo().fields[0].name == "name" );

  std::vector<sicnu::geo::VectorFeature> batch;
  REQUIRE( reader.nextBatch( batch, 100 ) );
  REQUIRE( batch.size() == 2 );
  CHECK( batch[0].attributes["name"].asString() == "Chengdu" );
  CHECK( batch[0].attributes["value"].asDouble() == Approx( 500.0 ) );
  CHECK( batch[0].geometryWkt.find( "104.06" ) != std::string::npos );

  reader.resetStream();
  std::vector<sicnu::geo::VectorFeature> again;
  CHECK( reader.nextBatch( again, 2 ) ); // restart replays the same features
  REQUIRE( again.size() == 2 );
  CHECK( again[0].attributes["name"].asString() == "Chengdu" );
  std::vector<sicnu::geo::VectorFeature> tail;
  CHECK_FALSE( reader.nextBatch( tail, 2 ) ); // then exhausts
  CHECK( tail.empty() );
}

TEST_CASE( "attribute projection and filters stream bounded batches", "[io][vector][contract]" )
{
  const std::string target = ( fs::path( scratchDir( "filters" ) ) / "cities.gpkg" ).string();
  sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
    target, "cities", "Point", demoFields(), sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
  for ( int i = 0; i < 20; ++i )
  {
    Json::Value attrs( Json::objectValue );
    attrs["name"] = ( i % 2 == 0 ) ? "east" : "west";
    attrs["value"] = static_cast<double>( i );
    attrs["code"] = static_cast<Json::Int64>( i );
    writer.writeFeature( attrs, "POINT (100 30)" );
  }
  writer.finalize();

  sicnu::geo::VectorReader reader = sicnu::geo::VectorReader::open( target );
  reader.setAttributeProjection( { "name", "value" } );

  std::vector<sicnu::geo::VectorFeature> batch;
  std::size_t total = 0;
  std::size_t batches = 0;
  while ( reader.nextBatch( batch, 7 ) || !batch.empty() )
  {
    total += batch.size();
    ++batches;
    CHECK( batch.size() <= 7 );
    for ( const sicnu::geo::VectorFeature &feature : batch )
    {
      // Projection kept exactly the requested fields.
      CHECK( feature.attributes.isMember( "name" ) );
      CHECK( feature.attributes.isMember( "value" ) );
      CHECK_FALSE( feature.attributes.isMember( "code" ) );
    }
    batch.clear();
    if ( batches > 10 )
      FAIL( "streaming did not terminate" );
  }
  CHECK( total == 20 );
  CHECK( batches == 3 ); // ceil(20/7)

  // Attribute filter is driver-evaluated.
  sicnu::geo::VectorReader filtered = sicnu::geo::VectorReader::open( target );
  filtered.setAttributeFilter( "name = 'east'" );
  std::vector<sicnu::geo::VectorFeature> east;
  while ( filtered.nextBatch( east, 100 ) || !east.empty() )
  {
    for ( const sicnu::geo::VectorFeature &feature : east )
      CHECK( feature.attributes["name"].asString() == "east" );
    east.clear();
  }

  // Spatial filter in layer coordinates.
  sicnu::geo::VectorReader boxed = sicnu::geo::VectorReader::open( target );
  sicnu::geo::CrsBoundingBox box;
  box.minX = 0.0;
  box.minY = 0.0;
  box.maxX = 50.0;
  box.maxY = 50.0;
  boxed.setSpatialFilter( box );
  std::vector<sicnu::geo::VectorFeature> inside;
  while ( boxed.nextBatch( inside, 100 ) || !inside.empty() )
    inside.clear();

  // Unknown projection field is a structured error.
  sicnu::geo::VectorReader bad = sicnu::geo::VectorReader::open( target );
  CHECK_THROWS_AS( bad.setAttributeProjection( { "nope" } ), sicnu::geo::GeoError );
}

TEST_CASE( "declared CRS transform applies to streamed geometry", "[io][vector][contract]" )
{
  const std::string target = ( fs::path( scratchDir( "crs" ) ) / "points.geojson" ).string();
  sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
    target, "pts", "Point", {}, sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
  writer.writeFeature( Json::Value( Json::objectValue ), "POINT (117 30)" );
  writer.finalize();

  sicnu::geo::VectorReader reader = sicnu::geo::VectorReader::open( target );
  reader.setTargetCrs( sicnu::geo::Crs::fromAuthid( "EPSG:32648" ) );
  std::vector<sicnu::geo::VectorFeature> batch;
  REQUIRE( reader.nextBatch( batch, 10 ) );
  REQUIRE( batch.size() == 1 );
  // UTM 48N easting/northing for (117E, 30N).
  CHECK( batch[0].geometryWkt.find( "POINT (" ) != std::string::npos );
  const std::string &wkt = batch[0].geometryWkt;
  const auto openParen = wkt.find( '(' );
  const auto space = wkt.find( ' ', openParen );
  const double easting = std::stod( wkt.substr( openParen + 1, space - openParen - 1 ) );
  // 117°E is ~12° east of zone 48N's central meridian: easting ~1.66M m.
  CHECK( easting > 1400000.0 );
  CHECK( easting < 1900000.0 );
}

TEST_CASE( "100k features stream with bounded batches (no whole-table materialize)", "[io][vector][acceptance][100k]" )
{
  const std::string target = ( fs::path( scratchDir( "bulk" ) ) / "bulk.gpkg" ).string();
  {
    sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
      target, "bulk", "Point", demoFields(), sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
    constexpr int kCount = 100000;
    for ( int i = 0; i < kCount; ++i )
    {
      Json::Value attrs( Json::objectValue );
      attrs["name"] = "p" + std::to_string( i );
      attrs["value"] = static_cast<double>( i % 997 );
      attrs["code"] = static_cast<Json::Int64>( i );
      const double x = 100.0 + ( i % 300 ) * 0.01;
      const double y = 20.0 + ( i % 200 ) * 0.01;
      writer.writeFeature( attrs, "POINT (" + std::to_string( x ) + " " + std::to_string( y ) + ")" );
    }
    writer.finalize();
  }

  // The metadata path reports a cheap count without materializing features.
  sicnu::geo::VectorReader reader = sicnu::geo::VectorReader::open( target );
  CHECK( reader.layerInfo().featureCount == 100000 );

  // Streamed scan: bounded batches, order preserved, exact total.
  std::vector<sicnu::geo::VectorFeature> batch;
  std::int64_t total = 0;
  std::int64_t lastCode = -1;
  while ( reader.nextBatch( batch, 500 ) || !batch.empty() )
  {
    CHECK( batch.size() <= 500 );
    for ( const sicnu::geo::VectorFeature &feature : batch )
    {
      const std::int64_t code = feature.attributes["code"].asInt64();
      CHECK( code == lastCode + 1 );
      lastCode = code;
      ++total;
    }
    batch.clear();
  }
  CHECK( total == 100000 );

  // Explicit exact count agrees.
  reader.resetStream();
  CHECK( reader.exactFeatureCount() == 100000 );
}

TEST_CASE( "unknown fields and malformed geometry are structured errors", "[io][vector][contract]" )
{
  const std::string target = ( fs::path( scratchDir( "errors" ) ) / "pts.gpkg" ).string();
  sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
    target, "pts", "Point", { { "name", "String" } }, {}, {} );
  Json::Value bad( Json::objectValue );
  bad["unknown_field"] = "x";
  CHECK_THROWS_AS( writer.writeFeature( bad, "POINT (1 1)" ), sicnu::geo::GeoError );
  Json::Value good( Json::objectValue );
  good["name"] = "ok";
  CHECK_THROWS_AS( writer.writeFeature( good, "NOT A WKT" ), sicnu::geo::GeoError );
  writer.cancel();

  // GeoJSON never accepts a GPKG driver name — driver missing is structured.
  sicnu::geo::VectorWriteOptions missing;
  missing.driver = "NoSuchDriver";
  CHECK_THROWS_AS( sicnu::geo::VectorWriter::create( ( fs::path( scratchDir( "errors" ) ) / "x.gpkg" ).string(),
                                                     "l", "Point", {}, {}, missing ),
                   sicnu::geo::GeoError );
}

TEST_CASE( "VectorWriter move constructor and move assignment preserve active transaction (#850)",
           "[io][vector][contract][issue850]" )
{
  const std::string moveTarget = ( fs::path( scratchDir( "move_tx" ) ) / "moved_ctor.gpkg" ).string();
  {
    auto writer1 = sicnu::geo::VectorWriter::create(
      moveTarget, "move_layer", "Point", demoFields(), sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
    sicnu::geo::VectorWriter writer2( std::move( writer1 ) );

    Json::Value attrs( Json::objectValue );
    attrs["name"] = "Chengdu";
    attrs["value"] = 500.0;
    attrs["code"] = static_cast<Json::Int64>( 2701 );
    writer2.writeFeature( attrs, "POINT (104.0668 30.5728)" );
    writer2.finalize();
  }

  // Feature must be committed and persisted on disk, NOT rolled back
  {
    auto reader = sicnu::geo::VectorReader::open( moveTarget );
    CHECK( reader.layerInfo().featureCount == 1 );
  }

  const std::string assignTarget = ( fs::path( scratchDir( "assign_tx" ) ) / "moved_assign.gpkg" ).string();
  {
    auto writer1 = sicnu::geo::VectorWriter::create(
      assignTarget, "assign_layer", "Point", demoFields(), sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
    sicnu::geo::VectorWriter writer2;
    writer2 = std::move( writer1 );

    Json::Value attrs( Json::objectValue );
    attrs["name"] = "Beijing";
    attrs["value"] = 1000.0;
    attrs["code"] = static_cast<Json::Int64>( 1100 );
    writer2.writeFeature( attrs, "POINT (116.4074 39.9042)" );
    writer2.finalize();
  }

  // Feature must be committed and persisted on disk, NOT rolled back
  {
    auto reader = sicnu::geo::VectorReader::open( assignTarget );
    CHECK( reader.layerInfo().featureCount == 1 );
  }
}


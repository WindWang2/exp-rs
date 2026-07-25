// test_remote_map_structure.cpp - RemoteMapStructure value type + AssetStructure arm (#61)
//
// RemoteMapStructure is the data-layer model extension for the Remote Map Assets
// wave: it carries only the metadata a web-map service can honestly report
// (declared layers, CRS list, extent, format, tile-matrix resolution, z-range)
// and never the pixel/statistics shape a remote map cannot honor. It joins the
// AssetStructure variant alongside RasterStructure/VectorStructure, and
// structuresCompatible gains a branch keyed on service+provider+layers.
//
// This ticket is the MODEL only — no source provider populates the structure
// yet (that is #62/#63); the structure is exercised directly and through the
// structuresCompatible relocation seam.
#include <catch2/catch_test_macros.hpp>

#include <QJsonDocument>
#include <QJsonObject>

#include "data/data_asset.h"
#include "data/asset_types.h"

using namespace sicnu::data;

namespace
{

RemoteMapStructure makeSample()
{
  RemoteMapStructure s;
  s.service = RemoteMapService::Wms;
  s.layerNames = QStringList{ QStringLiteral( "imagery" ), QStringLiteral( "labels" ) };
  s.crsList = QStringList{ QStringLiteral( "EPSG:4326" ), QStringLiteral( "EPSG:3857" ) };
  s.extent = SpatialExtent{ -180.0, -90.0, 180.0, 90.0, true };
  s.imageFormat = QStringLiteral( "image/png" );
  s.pixelSizeX = 0.5;
  s.pixelSizeY = 0.5;
  s.zMin = 0;
  s.zMax = 12;
  s.valid = true;
  return s;
}

} // namespace

TEST_CASE( "RemoteMapStructure defaults to an invalid, service-less shape",
           "[remote_map][structure]" )
{
  RemoteMapStructure s;
  CHECK( s.service == RemoteMapService::Wms );
  CHECK( s.layerNames.isEmpty() );
  CHECK( s.crsList.isEmpty() );
  CHECK_FALSE( s.extent.valid );
  CHECK( s.imageFormat.isEmpty() );
  CHECK_FALSE( s.pixelSizeX.has_value() );
  CHECK_FALSE( s.pixelSizeY.has_value() );
  CHECK( s.zMin == 0 );
  CHECK( s.zMax == 0 );
  CHECK_FALSE( s.valid );
}

TEST_CASE( "RemoteMapStructure round-trips losslessly through JSON",
           "[remote_map][structure][json]" )
{
  const RemoteMapStructure original = makeSample();
  const QJsonObject json = original.toJson();
  const Result<RemoteMapStructure> restored =
    RemoteMapStructure::fromJson( json );

  REQUIRE( restored );
  const RemoteMapStructure &r = restored.value();
  CHECK( r.service == original.service );
  CHECK( r.layerNames == original.layerNames );
  CHECK( r.crsList == original.crsList );
  CHECK( r.extent.minimumX == original.extent.minimumX );
  CHECK( r.extent.minimumY == original.extent.minimumY );
  CHECK( r.extent.maximumX == original.extent.maximumX );
  CHECK( r.extent.maximumY == original.extent.maximumY );
  CHECK( r.extent.valid == original.extent.valid );
  CHECK( r.imageFormat == original.imageFormat );
  REQUIRE( r.pixelSizeX.has_value() );
  CHECK( r.pixelSizeX.value() == original.pixelSizeX.value() );
  REQUIRE( r.pixelSizeY.has_value() );
  CHECK( r.pixelSizeY.value() == original.pixelSizeY.value() );
  CHECK( r.zMin == original.zMin );
  CHECK( r.zMax == original.zMax );
  CHECK( r.valid == original.valid );
}

TEST_CASE( "RemoteMapStructure JSON defaults absent optional fields",
           "[remote_map][structure][json]" )
{
  // A minimal JSON object (only the service, no tile-matrix / extent) must
  // round-trip to the struct defaults rather than failing — absent optional
  // keys fall back, mirroring the lenient-absent-key rule of the recipe.
  QJsonObject json;
  json.insert( QStringLiteral( "service" ), QStringLiteral( "xyz" ) );
  json.insert( QStringLiteral( "valid" ), true );

  const Result<RemoteMapStructure> restored =
    RemoteMapStructure::fromJson( json );
  REQUIRE( restored );
  const RemoteMapStructure &r = restored.value();
  CHECK( r.service == RemoteMapService::Xyz );
  CHECK( r.layerNames.isEmpty() );
  CHECK_FALSE( r.extent.valid );
  CHECK_FALSE( r.pixelSizeX.has_value() );
  CHECK_FALSE( r.pixelSizeY.has_value() );
  CHECK( r.valid );
}

TEST_CASE( "RemoteMapStructure rejects an unknown service kind",
           "[remote_map][structure][json]" )
{
  QJsonObject json;
  json.insert( QStringLiteral( "service" ), QStringLiteral( "wfs" ) );
  const Result<RemoteMapStructure> restored =
    RemoteMapStructure::fromJson( json );
  REQUIRE_FALSE( restored );
  CHECK( restored.diagnostics().first().code == QStringLiteral( "remote_map.service_invalid" ) );
}

TEST_CASE( "RemoteMapStructure equality is value-based",
           "[remote_map][structure]" )
{
  const RemoteMapStructure a = makeSample();
  RemoteMapStructure b = a;
  CHECK( a == b );
  b.zMax = 13;
  CHECK_FALSE( a == b );
}

TEST_CASE( "AssetStructure carries the RemoteMap arm without disturbing raster/vector",
           "[remote_map][structure]" )
{
  const RemoteMapStructure remote = makeSample();
  const AssetStructure variant = remote;
  REQUIRE( std::holds_alternative<RemoteMapStructure>( variant ) );
  const auto *back = std::get_if<RemoteMapStructure>( &variant );
  REQUIRE( back != nullptr );
  CHECK( back->service == remote.service );
  CHECK( back->layerNames == remote.layerNames );

  // Existing arms are untouched.
  RasterStructure raster;
  raster.bandCount = 3;
  CHECK( std::holds_alternative<RasterStructure>(
    AssetStructure{ raster } ) );
  CHECK( std::holds_alternative<std::monostate>( AssetStructure{} ) );
}

TEST_CASE( "structuresCompatible matches remote maps on service+provider+layers",
           "[remote_map][structure][compatibility]" )
{
  const RemoteMapStructure a = makeSample();
  RemoteMapStructure b = a;
  CHECK( structuresCompatible( a, b ) );

  // A different declared layer set is an incompatible relocation target.
  b.layerNames = QStringList{ QStringLiteral( "imagery" ) };
  CHECK_FALSE( structuresCompatible( a, b ) );

  // A different service is incompatible even with the same layers.
  b = a;
  b.service = RemoteMapService::Wmts;
  CHECK_FALSE( structuresCompatible( a, b ) );

  // Structure-content drift that does not change identity (a service
  // advertising a new CRS) stays compatible — relocate surfaces it via
  // assetChanged rather than refusing the move.
  b = a;
  b.crsList.append( QStringLiteral( "EPSG:3395" ) );
  CHECK( structuresCompatible( a, b ) );
}

TEST_CASE( "structuresCompatible refuses to cross remote-map/raster kinds",
           "[remote_map][structure][compatibility]" )
{
  const RemoteMapStructure remote = makeSample();
  RasterStructure raster;
  raster.bandCount = 1;
  // A remote map cannot be relocated onto a raster's structure (or vice versa).
  CHECK_FALSE( structuresCompatible( AssetStructure{ remote },
                                     AssetStructure{ raster } ) );
  CHECK_FALSE( structuresCompatible( AssetStructure{ raster },
                                     AssetStructure{ remote } ) );
}

TEST_CASE( "structuresCompatible keeps the monostate escape hatch for remote maps",
           "[remote_map][structure][compatibility]" )
{
  // A remote map currently unresolved (monostate current) may relocate to a
  // resolvable remote map; a resolved remote map may not be downgraded to
  // monostate. Mirrors the raster/vector rule.
  const RemoteMapStructure remote = makeSample();
  CHECK( structuresCompatible( AssetStructure{}, AssetStructure{ remote } ) );
  CHECK_FALSE( structuresCompatible( AssetStructure{ remote }, AssetStructure{} ) );
}

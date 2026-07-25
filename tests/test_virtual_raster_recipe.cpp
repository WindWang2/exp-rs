// test_virtual_raster_recipe.cpp - VirtualRasterRecipe value type (#55)
//
// The recipe is the identity of a Virtual Raster Asset: ordered BandRef
// inputs, target CRS/grid, extent policy, resampling, and NoData policy. It
// serializes to/from JSON losslessly (mirroring DerivationRecord) and rejects
// invalid shapes strictly.
#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonObject>

#include "data/asset_types.h"
#include "data/virtual_raster_recipe.h"

using namespace sicnu::data;

namespace
{

VirtualRasterRecipe sampleRecipe()
{
  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ AssetId::generate(), 1 },
                    BandRef{ AssetId::generate(), 2 },
                    BandRef{ AssetId::generate(), 3 } };
  recipe.targetCrs = QStringLiteral( "EPSG:32648" );
  recipe.targetResolutionX = 30.0;
  recipe.targetResolutionY = 30.0;
  recipe.extentPolicy = ExtentPolicy::Intersection;
  recipe.resampling = ResamplingMethod::Bilinear;
  recipe.noDataPolicy = NoDataPolicy::Preserve;
  recipe.noDataFillValue = -9999.0;
  return recipe;
}

} // namespace

TEST_CASE( "A recipe round-trips through JSON losslessly",
           "[virtual_raster_recipe]" )
{
  const VirtualRasterRecipe recipe = sampleRecipe();

  const QJsonObject json = recipe.toJson();
  const Result<VirtualRasterRecipe> restored = VirtualRasterRecipe::fromJson( json );

  REQUIRE( restored );
  CHECK( restored.value() == recipe );
}

TEST_CASE( "Defaults: intersection extent, bilinear resampling, preserve nodata",
           "[virtual_raster_recipe]" )
{
  const VirtualRasterRecipe recipe;

  CHECK( recipe.extentPolicy == ExtentPolicy::Intersection );
  CHECK( recipe.resampling == ResamplingMethod::Bilinear );
  CHECK( recipe.noDataPolicy == NoDataPolicy::Preserve );
  CHECK( recipe.targetResolutionX == 0.0 );
  CHECK( recipe.targetResolutionY == 0.0 );
  CHECK( recipe.targetCrs.isEmpty() );
}

TEST_CASE( "fromJson rejects an empty inputs list",
           "[virtual_raster_recipe]" )
{
  SECTION( "missing key" )
  {
    QJsonObject json = sampleRecipe().toJson();
    json.remove( QStringLiteral( "inputs" ) );

    const Result<VirtualRasterRecipe> result = VirtualRasterRecipe::fromJson( json );

    REQUIRE_FALSE( result );
    CHECK( result.diagnostics().first().code ==
           QStringLiteral( "recipe.invalid" ) );
  }
  SECTION( "explicit empty array" )
  {
    QJsonObject json = sampleRecipe().toJson();
    json.insert( QStringLiteral( "inputs" ), QJsonArray() );

    const Result<VirtualRasterRecipe> result = VirtualRasterRecipe::fromJson( json );

    REQUIRE_FALSE( result );
    CHECK( result.diagnostics().first().code ==
           QStringLiteral( "recipe.invalid" ) );
  }
}

TEST_CASE( "Absent enum keys fall back to the struct defaults",
           "[virtual_raster_recipe]" )
{
  // Hand-authored or forward/backward-compatible JSON may omit the enum keys;
  // absence means "default" (mirroring DerivationRecord's leniency for absent
  // scalars). Only an explicitly present unknown spelling is rejected.
  QJsonObject json = sampleRecipe().toJson();
  json.remove( QStringLiteral( "extentPolicy" ) );
  json.remove( QStringLiteral( "resampling" ) );
  json.remove( QStringLiteral( "noDataPolicy" ) );

  const Result<VirtualRasterRecipe> result = VirtualRasterRecipe::fromJson( json );

  REQUIRE( result );
  CHECK( result.value().extentPolicy == ExtentPolicy::Intersection );
  CHECK( result.value().resampling == ResamplingMethod::Bilinear );
  CHECK( result.value().noDataPolicy == NoDataPolicy::Preserve );
}

TEST_CASE( "A default-valued recipe round-trips through JSON",
           "[virtual_raster_recipe]" )
{
  // Empty targetCrs / zero resolutions / default policies must survive the
  // round-trip exactly (defaults are written, not elided).
  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ AssetId::generate(), 1 } };

  const Result<VirtualRasterRecipe> restored =
    VirtualRasterRecipe::fromJson( recipe.toJson() );

  REQUIRE( restored );
  CHECK( restored.value() == recipe );
}

TEST_CASE( "fromJson rejects a band number below 1",
           "[virtual_raster_recipe]" )
{
  VirtualRasterRecipe recipe = sampleRecipe();
  recipe.inputs[1].bandNumber = 0;
  const QJsonObject json = recipe.toJson();

  const Result<VirtualRasterRecipe> result = VirtualRasterRecipe::fromJson( json );

  REQUIRE_FALSE( result );
  CHECK( result.diagnostics().first().code ==
         QStringLiteral( "recipe.invalid" ) );
}

TEST_CASE( "fromJson rejects an invalid AssetId string",
           "[virtual_raster_recipe]" )
{
  QJsonObject json = sampleRecipe().toJson();
  QJsonArray inputs = json.value( QStringLiteral( "inputs" ) ).toArray();
  QJsonObject first = inputs.first().toObject();
  first.insert( QStringLiteral( "assetId" ), QStringLiteral( "not-a-uuid" ) );
  inputs.replace( 0, first );
  json.insert( QStringLiteral( "inputs" ), inputs );

  const Result<VirtualRasterRecipe> result = VirtualRasterRecipe::fromJson( json );

  REQUIRE_FALSE( result );
  CHECK( result.diagnostics().first().code ==
         QStringLiteral( "recipe.invalid" ) );
}

TEST_CASE( "fromJson rejects unknown enum spellings",
           "[virtual_raster_recipe]" )
{
  SECTION( "extent policy" )
  {
    QJsonObject json = sampleRecipe().toJson();
    json.insert( QStringLiteral( "extentPolicy" ), QStringLiteral( "sideways" ) );
    const Result<VirtualRasterRecipe> result = VirtualRasterRecipe::fromJson( json );
    REQUIRE_FALSE( result );
    CHECK( result.diagnostics().first().code == QStringLiteral( "recipe.invalid" ) );
  }
  SECTION( "resampling" )
  {
    QJsonObject json = sampleRecipe().toJson();
    json.insert( QStringLiteral( "resampling" ), QStringLiteral( "magic" ) );
    const Result<VirtualRasterRecipe> result = VirtualRasterRecipe::fromJson( json );
    REQUIRE_FALSE( result );
    CHECK( result.diagnostics().first().code == QStringLiteral( "recipe.invalid" ) );
  }
  SECTION( "nodata policy" )
  {
    QJsonObject json = sampleRecipe().toJson();
    json.insert( QStringLiteral( "noDataPolicy" ), QStringLiteral( "whatever" ) );
    const Result<VirtualRasterRecipe> result = VirtualRasterRecipe::fromJson( json );
    REQUIRE_FALSE( result );
    CHECK( result.diagnostics().first().code == QStringLiteral( "recipe.invalid" ) );
  }
}

TEST_CASE( "Union extent and fill-value nodata round-trip",
           "[virtual_raster_recipe]" )
{
  VirtualRasterRecipe recipe = sampleRecipe();
  recipe.extentPolicy = ExtentPolicy::Union;
  recipe.noDataPolicy = NoDataPolicy::FillValue;
  recipe.noDataFillValue = -3.4e38;

  const Result<VirtualRasterRecipe> restored =
    VirtualRasterRecipe::fromJson( recipe.toJson() );

  REQUIRE( restored );
  CHECK( restored.value() == recipe );
  CHECK( restored.value().extentPolicy == ExtentPolicy::Union );
  CHECK( restored.value().noDataPolicy == NoDataPolicy::FillValue );
}

TEST_CASE( "BandRef equality and PreflightResult shape",
           "[virtual_raster_recipe]" )
{
  const AssetId id = AssetId::generate();
  CHECK( BandRef{ id, 1 } == BandRef{ id, 1 } );
  CHECK( BandRef{ id, 1 } != BandRef{ id, 2 } );

  // PreflightResult carries a verdict, a canCreate flag, and diagnostics.
  PreflightResult result;
  result.verdict = PreflightVerdict::NoOverlap;
  result.canCreate = false;
  CHECK( result.verdict == PreflightVerdict::NoOverlap );
  CHECK_FALSE( result.canCreate );
}

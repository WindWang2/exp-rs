// test_workbench_enum_provider.cpp — Workbench 9.0 M6: production enum provider
//
// Before M6 no shell-side SchemaEnumProvider existed: every x-ui-enum-source
// parameter degraded to free text and no producer annotated one. These tests
// pin the production resolution contract — authoritative sources (canvas
// layers, DataManager assets, ModelCatalog), the 200-entry cap with truthful
// truncation, and the unknown-source degradation that SchemaForm 4.0
// documented.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <gdal.h>

#include <json/json.h>

#include "app/shell/workbench_enum_provider.h"
#include "data/data_asset.h"
#include "data/data_manager.h"

using sicnu::app::WorkbenchEnumProvider;
using Choice = SchemaEnumProvider::Choice; // global-namespace provider base (8.0 seam)

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_workbench_enum_provider";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
  static QCoreApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QCoreApplication( fake_argc, fake_argv );
  return app;
}

} // namespace

TEST_CASE( "Provider resolves injected layer lists", "[m6][enum_provider]" )
{
  ensureApp();
  WorkbenchEnumProvider provider;

  SECTION( "unpopulated source resolves empty (free-text degradation)" )
  {
    const QVector<Choice> choices =
      provider.choicesFor( QStringLiteral( "layers:raster" ), Json::Value() );
    REQUIRE( choices.isEmpty() );
  }

  SECTION( "populated raster list resolves id/label pairs" )
  {
    provider.setRasterLayers( { QStringLiteral( "a1" ), QStringLiteral( "b2" ) },
                              { QStringLiteral( "Scene A" ), QString() } );
    const auto choices = provider.choicesFor( QStringLiteral( "layers:raster" ), Json::Value() );
    REQUIRE( choices.size() == 2 );
    CHECK( choices[0].id == QStringLiteral( "a1" ) );
    CHECK( choices[0].label == QStringLiteral( "Scene A" ) );
    // Empty name falls back to the id — never an unlabeled choice.
    CHECK( choices[1].label == QStringLiteral( "b2" ) );
  }

  SECTION( "raster and vector lists are independent" )
  {
    provider.setRasterLayers( { QStringLiteral( "r1" ) }, {} );
    provider.setVectorLayers( { QStringLiteral( "v1" ) }, { QStringLiteral( "Roads" ) } );
    CHECK( provider.choicesFor( QStringLiteral( "layers:raster" ), Json::Value() ).size() == 1 );
    const auto vectors = provider.choicesFor( QStringLiteral( "layers:vector" ), Json::Value() );
    REQUIRE( vectors.size() == 1 );
    CHECK( vectors[0].label == QStringLiteral( "Roads" ) );
  }
}

TEST_CASE( "Provider caps oversized sources with a truthful truncation label",
           "[m6][enum_provider][scale]" )
{
  ensureApp();
  WorkbenchEnumProvider provider;

  QStringList ids;
  ids.reserve( WorkbenchEnumProvider::kMaxChoices + 50 );
  for ( int i = 0; i < WorkbenchEnumProvider::kMaxChoices + 50; ++i )
    ids << QStringLiteral( "id%1" ).arg( i, 5, 10, QLatin1Char( '0' ) );
  provider.setRasterLayers( ids, ids );

  const auto choices = provider.choicesFor( QStringLiteral( "layers:raster" ), Json::Value() );
  REQUIRE( choices.size() == WorkbenchEnumProvider::kMaxChoices );
  // The UI never materializes an unbounded list — the last entry says so AND
  // carries no selectable value (review B6: the notice is a sentinel, not a
  // disguised choice).
  CHECK( choices.last().id.isEmpty() );
  CHECK( choices.last().label.contains( QStringLiteral( "截断" ) ) );
}

TEST_CASE( "Provider resolves assets from the authoritative DataManager",
           "[m6][enum_provider]" )
{
  ensureApp();
  WorkbenchEnumProvider provider;

  SECTION( "no manager attached → empty (degradation)" )
  {
    CHECK( provider.choicesFor( QStringLiteral( "assets" ), Json::Value() ).isEmpty() );
  }

  SECTION( "registered assets resolve with id/label pairs" )
  {
    sicnu::data::DataManager manager;
    provider.attachDataManager( &manager );

    // Register one synthetic raster asset (same shape as the project-context
    // tests; GDAL writes a real tiny file so the catalog entry is a genuine
    // asset, not a mock of itself).
    static QTemporaryDir dir;
    static int counter = 0;
    counter++;
    const QString finalPath =
      dir.filePath( QStringLiteral( "enum_asset_%1.tif" ).arg( counter ) );

    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, finalPath.toUtf8().constData(), 4, 4, 1,
                                  GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    GDALClose( ds );

    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = finalPath;
    sicnu::data::RegisterRequest request;
    request.source = source;
    const auto result = manager.registerSource( request );
    REQUIRE( !result.assetId.isNull() );

    const auto choices = provider.choicesFor( QStringLiteral( "assets" ), Json::Value() );
    REQUIRE( choices.size() == 1 );
    CHECK( choices[0].id == result.assetId.toString() );
    CHECK( choices[0].label.contains( QStringLiteral( "enum_asset_" ) ) );
  }
}

TEST_CASE( "applyEnumSourceAnnotations: shell-boundary schema annotation",
           "[m6][enum_provider][annotation]" )
{
  SECTION( "model/asset ports gain enum sources; others untouched" )
  {
    Json::Value schema( Json::objectValue );
    Json::Value props( Json::objectValue );
    Json::Value model( Json::objectValue );
    model["type"] = "string";
    model["x-ui-type"] = "model";
    Json::Value asset( Json::objectValue );
    asset["x-ui-type"] = "asset";
    asset["x-ui-enum-source"] = "custom"; // already annotated — respected
    Json::Value raster( Json::objectValue );
    raster["x-ui-type"] = "raster"; // stays on the push channel
    props["model"] = model;
    props["asset"] = asset;
    props["raster"] = raster;
    schema["properties"] = props;

    const Json::Value out = sicnu::app::applyEnumSourceAnnotations( schema );
    CHECK( out["properties"]["model"]["x-ui-enum-source"].asString() == "models" );
    CHECK( out["properties"]["asset"]["x-ui-enum-source"].asString() == "custom" );
    CHECK_FALSE( out["properties"]["raster"].isMember( "x-ui-enum-source" ) );
  }

  SECTION( "nested object schemas recurse" )
  {
    Json::Value schema( Json::objectValue );
    Json::Value group( Json::objectValue );
    Json::Value inner( Json::objectValue );
    inner["x-ui-type"] = "model";
    // properties CONTAIN the named port — the recursion walks this map.
    group["properties"]["inner"] = inner;
    Json::Value props( Json::objectValue );
    props["group"] = group;
    schema["properties"] = props;

    const Json::Value out = sicnu::app::applyEnumSourceAnnotations( schema );
    CHECK( out["properties"]["group"]["properties"]["inner"]["x-ui-enum-source"]
               .asString() == "models" );
  }

  SECTION( "non-object schemas pass through" )
  {
    Json::Value schema( Json::arrayValue );
    const Json::Value out = sicnu::app::applyEnumSourceAnnotations( schema );
    CHECK( out.isArray() );
  }
}

TEST_CASE( "Provider: unknown sources and the model catalog contract",
           "[m6][enum_provider]" )
{
  ensureApp();
  WorkbenchEnumProvider provider;

  SECTION( "unknown source is empty — the form degrades to free text" )
  {
    CHECK( provider.choicesFor( QStringLiteral( "operators:bogus" ), Json::Value() ).isEmpty() );
    CHECK( provider.choicesFor( QString(), Json::Value() ).isEmpty() );
    CHECK( provider.choicesFor( QStringLiteral( "layers:hyper" ), Json::Value() ).isEmpty() );
  }

  SECTION( "models source reads the live catalog (empty catalog → degradation)" )
  {
    // ModelCatalog is a singleton authority; in this harness it may hold no
    // models. Either way the provider must return a bounded list without
    // crashing — the choice CONTENT depends on the installed catalog and is
    // covered by the model platform's own conformance tests.
    const auto choices = provider.choicesFor( QStringLiteral( "models" ), Json::Value() );
    CHECK( choices.size() <= WorkbenchEnumProvider::kMaxChoices );
  }
}

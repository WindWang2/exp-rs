// Workbench 7.0 — Provenance Inspector section (goal §B)
//
// Covers: asset/governance/layer selection resolution, derivation record
// rendering, truthful "no provenance" paths, deleted-asset warnings and the
// governance verification block. All against the real DataManager +
// WorkspaceService (temp GTiff payloads + temp SQLite store) — the section is
// a thin projection, so the test proves the projection, not the stores.
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/provenance_section.h"

#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/governance/workspace_service.h"

#include <QApplication>
#include <QLabel>
#include <QDir>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsrasterlayer.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <vector>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_provenance_section";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
  {
    // QgsApplication (QApplication subclass, GUI off): the layer-resolution
    // cases need the real GDAL provider registry.
    app = new QgsApplication( fake_argc, fake_argv, false );
    QgsApplication::initQgis();
  }
  return app;
}

QString makeRaster( const QString &name )
{
  static QTemporaryDir dir;
  const QString path = QDir( dir.path() ).filePath( name );
  if ( QFileInfo::exists( path ) )
    return path;
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 8, H = 8;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 0.0, 1.0, 0.0, ( double ) H, 0.0, -1.0 };
  GDALSetGeoTransform( ds, gt );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( W, 1.5f );
  for ( int row = 0; row < H; ++row )
    GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
  GDALClose( ds );
  return path;
}

struct Fixture
{
  sicnu::data::DataManager manager;
  sicnu::workspace::WorkspaceService service;

  Fixture()
  {
    ensureApp();
    service.bindDataManager( &manager );
    static int instance = 0;
    const QString storeDir = QDir::temp().filePath(
      QStringLiteral( "wb7-provenance-%1-%2" ).arg( QCoreApplication::applicationPid() ).arg( ++instance ) );
    REQUIRE( QDir().mkpath( storeDir ) );
    const QString storePath = QDir( storeDir ).filePath( QStringLiteral( "gov.db" ) );
    REQUIRE( service.openStore( storePath ) );
  }
  ~Fixture() { service.closeStore(); }

  sicnu::data::RegisterResult registerRaster( const QString &name )
  {
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = makeRaster( name );
    return manager.registerSource( sicnu::data::RegisterRequest{ source } );
  }
};

} // namespace

TEST_CASE( "ProvenanceSection support rules", "[provenance][inspector]" )
{
  ensureApp();
  Fixture fx;
  sicnu::app::ProvenanceSection section(
    [&fx]() -> sicnu::data::DataManager * { return &fx.manager; },
    [&fx]() -> sicnu::workspace::WorkspaceService * { return &fx.service; } );

  sicnu::app::SelectionContextSnapshot empty;
  REQUIRE_FALSE( section.supports( empty ) );

  sicnu::app::SelectionContextSnapshot withLayer;
  withLayer.activeLayer = nullptr; // hasLayerSelection needs selectedLayers or active
  withLayer.selectedLayers.append( nullptr ); // presence, not liveness, decides here
  REQUIRE( withLayer.hasLayerSelection() );
  REQUIRE( section.supports( withLayer ) );

  sicnu::app::SelectionContextSnapshot withAsset;
  withAsset.selectedAssetIds.append( "not-a-uuid" );
  REQUIRE( section.supports( withAsset ) );

  sicnu::app::SelectionContextSnapshot withResult;
  withResult.selectedResultIds.append( "res-1" );
  REQUIRE( section.supports( withResult ) );
}

TEST_CASE( "ProvenanceSection renders derivation records for asset selection",
           "[provenance][inspector]" )
{
  ensureApp();
  Fixture fx;
  const auto base = fx.registerRaster( QStringLiteral( "prov-base.tif" ) );
  const auto derived = fx.registerRaster( QStringLiteral( "prov-derived.tif" ) );
  REQUIRE( !base.assetId.isNull() );
  REQUIRE( !derived.assetId.isNull() );

  sicnu::data::DerivationRecord record = sicnu::data::makeTaskDerivation(
    QStringLiteral( "rs.ndvi" ), QJsonObject{ { "red", 3 }, { "nir", 4 } },
    QStringLiteral( "task-42" ) );
  sicnu::data::DerivationInput input;
  input.assetId = base.assetId;
  input.revision = sicnu::data::AssetRevision::initial();
  record.inputs.append( input );
  record.executionFingerprint = QStringLiteral( "abc123" );
  REQUIRE( fx.manager.attachDerivationRecord( derived.assetId, record ).has_value() );

  sicnu::app::ProvenanceSection section(
    [&fx]() -> sicnu::data::DataManager * { return &fx.manager; },
    [&fx]() -> sicnu::workspace::WorkspaceService * { return &fx.service; } );

  sicnu::app::SelectionContextSnapshot snapshot;
  snapshot.selectedAssetIds.append( derived.assetId.toString() );
  REQUIRE( section.supports( snapshot ) );
  section.populate( snapshot );

  const QString text = section.findChild<QLabel *>( QStringLiteral( "rsInspectorProvenance" ) )->text();
  CAPTURE( text );
  CHECK( text.contains( "rs.ndvi" ) );                 // operator id
  CHECK( text.contains( "task-42" ) );                 // task reference
  CHECK( text.contains( "abc123" ) );                  // execution fingerprint
  CHECK( text.contains( QStringLiteral( "r1" ) ) );    // input revision rendered
  CHECK_FALSE( text.contains( QStringLiteral( "已从目录中删除" ) ) );
}

TEST_CASE( "ProvenanceSection resolves map layers through the catalog and warns for deleted assets",
           "[provenance][inspector]" )
{
  ensureApp();
  Fixture fx;
  const auto registered = fx.registerRaster( QStringLiteral( "prov-layer.tif" ) );
  REQUIRE( !registered.assetId.isNull() );

  sicnu::app::ProvenanceSection section(
    [&fx]() -> sicnu::data::DataManager * { return &fx.manager; },
    [&fx]() -> sicnu::workspace::WorkspaceService * { return &fx.service; } );

  // A real layer over the registered payload resolves via its source path.
  QgsRasterLayer layer( makeRaster( QStringLiteral( "prov-layer.tif" ) ), QStringLiteral( "prov" ) );
  sicnu::app::SelectionContextSnapshot snapshot;
  snapshot.activeLayer = &layer;
  section.populate( snapshot );
  QLabel *body = section.findChild<QLabel *>( QStringLiteral( "rsInspectorProvenance" ) );
  CHECK( body->text().contains( QStringLiteral( "无派生记录" ) ) ); // directly registered

  // The asset disappearing between selection and inspection must surface as a
  // truthful warning — never as invented provenance (goal: deleted layer in
  // inspector). A valid-format id unknown to the catalog exercises that path.
  sicnu::app::SelectionContextSnapshot gone;
  gone.selectedAssetIds.append( registered.assetId.toString() );
  // Simulate the catalog losing the asset: a fresh DataManager has no such id.
  {
    sicnu::data::DataManager emptyManager;
    sicnu::app::ProvenanceSection goneSection(
      [&emptyManager]() -> sicnu::data::DataManager * { return &emptyManager; },
      [&fx]() -> sicnu::workspace::WorkspaceService * { return &fx.service; } );
    goneSection.populate( gone );
    QLabel *goneBody = goneSection.findChild<QLabel *>( QStringLiteral( "rsInspectorProvenance" ) );
    CHECK( goneBody->text().contains( QStringLiteral( "已不在数据目录" ) ) );
  }

  // An unregistered layer path stays truthful too.
  QgsRasterLayer foreign( QStringLiteral( "/definitely/not/registered.tif" ), QStringLiteral( "x" ) );
  sicnu::app::SelectionContextSnapshot foreignSnapshot;
  foreignSnapshot.activeLayer = &foreign;
  section.populate( foreignSnapshot );
  CHECK( body->text().contains( QStringLiteral( "未注册到数据目录" ) ) );
}

TEST_CASE( "ProvenanceSection walks the derivation chain and renders governance verification",
           "[provenance][inspector]" )
{
  ensureApp();
  Fixture fx;
  const auto grand = fx.registerRaster( QStringLiteral( "prov-chain-0.tif" ) );
  const auto parent = fx.registerRaster( QStringLiteral( "prov-chain-1.tif" ) );
  const auto child = fx.registerRaster( QStringLiteral( "prov-chain-2.tif" ) );

  auto link = [&]( const sicnu::data::AssetId &output, const sicnu::data::AssetId &input ) {
    sicnu::data::DerivationRecord record = sicnu::data::makeTaskDerivation(
      QStringLiteral( "rs.chain" ), QJsonObject{}, QStringLiteral( "task-chain" ) );
    sicnu::data::DerivationInput in;
    in.assetId = input;
    in.revision = sicnu::data::AssetRevision::initial();
    record.inputs.append( in );
    REQUIRE( fx.manager.attachDerivationRecord( output, record ).has_value() );
  };
  link( parent.assetId, grand.assetId );
  link( child.assetId, parent.assetId );

  REQUIRE( fx.service.mirrorAsset( child.assetId ) );
  REQUIRE( fx.service.noteAssetVerified( child.assetId.toString(),
                                         QStringLiteral( "fp-1" ), 1024, 1000 ) );

  sicnu::app::ProvenanceSection section(
    [&fx]() -> sicnu::data::DataManager * { return &fx.manager; },
    [&fx]() -> sicnu::workspace::WorkspaceService * { return &fx.service; } );

  sicnu::app::SelectionContextSnapshot snapshot;
  snapshot.selectedAssetIds.append( child.assetId.toString() );
  section.populate( snapshot );

  const QString text = section.findChild<QLabel *>( QStringLiteral( "rsInspectorProvenance" ) )->text();
  CAPTURE( text );
  // Chain: child ← parent ← grand, rendered as one projection.
  CHECK( text.contains( QStringLiteral( "派生链" ) ) );
  CHECK( text.contains( QStringLiteral( "prov-chain-1" ) ) );
  CHECK( text.contains( QStringLiteral( "prov-chain-0" ) ) );
  // Governance enrichment: fingerprint + verification stamp, no stale warning.
  CHECK( text.contains( "fp-1" ) );
  CHECK( text.contains( QStringLiteral( "校验与治理" ) ) );
  CHECK_FALSE( text.contains( QStringLiteral( "从未校验" ) ) );
  CHECK_FALSE( text.contains( QStringLiteral( "已从目录中删除" ) ) );
}

TEST_CASE( "ProvenanceSection unresolved inputs and unverified assets surface as warnings",
           "[provenance][inspector]" )
{
  ensureApp();
  Fixture fx;
  const auto registered = fx.registerRaster( QStringLiteral( "prov-warn.tif" ) );
  REQUIRE( !registered.assetId.isNull() );

  sicnu::data::DerivationRecord record = sicnu::data::makeTaskDerivation(
    QStringLiteral( "rs.warn" ), QJsonObject{}, QStringLiteral( "task-warn" ) );
  record.unresolvedInputPaths.append( QStringLiteral( "/ghost/input.tif" ) );
  REQUIRE( fx.manager.attachDerivationRecord( registered.assetId, record ).has_value() );

  sicnu::app::ProvenanceSection section(
    [&fx]() -> sicnu::data::DataManager * { return &fx.manager; },
    [&fx]() -> sicnu::workspace::WorkspaceService * { return &fx.service; } );

  sicnu::app::SelectionContextSnapshot snapshot;
  snapshot.selectedAssetIds.append( registered.assetId.toString() );
  section.populate( snapshot );

  const QString text = section.findChild<QLabel *>( QStringLiteral( "rsInspectorProvenance" ) )->text();
  CAPTURE( text );
  CHECK( text.contains( "/ghost/input.tif" ) );
  CHECK( text.contains( QStringLiteral( "未能解析" ) ) );
  CHECK( text.contains( QStringLiteral( "从未校验" ) ) ); // no verification recorded
}

TEST_CASE( "ProvenanceSection without a service stays truthful",
           "[provenance][inspector]" )
{
  ensureApp();
  sicnu::app::ProvenanceSection section(
    []() -> sicnu::data::DataManager * { return nullptr; },
    []() -> sicnu::workspace::WorkspaceService * { return nullptr; } );

  sicnu::app::SelectionContextSnapshot snapshot;
  REQUIRE_FALSE( section.supports( snapshot ) ); // no service → nothing to show
  snapshot.selectedAssetIds.append( "12345678-1234-1234-1234-123456789abc" );
  REQUIRE_FALSE( section.supports( snapshot ) );

  section.populate( snapshot );
  const QString text = section.findChild<QLabel *>( QStringLiteral( "rsInspectorProvenance" ) )->text();
  CHECK( text.contains( QStringLiteral( "不可用" ) ) );
}

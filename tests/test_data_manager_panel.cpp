#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsmaplayerstore.h>
#include <qgsrasterlayer.h>

#include "app/display/qgis_display_manager.h"
#include "app/panels/data_manager_panel.h"
#include "data/data_manager.h"

using sicnu::data::AssetId;
using sicnu::data::AssetState;
using sicnu::data::CollectionCreateRequest;
using sicnu::data::CollectionId;
using sicnu::data::DataManager;
using sicnu::data::PersistencePolicy;
using sicnu::data::ProductMetadata;
using sicnu::data::RegisterRequest;
using sicnu::data::SourceDescriptor;
using sicnu::display::DisplayViewId;
using sicnu::display::DisplayViewSpec;
using sicnu::display::QgisDisplayManager;

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;

  static int argc = 1;
  static char applicationName[] = "test_data_manager_panel";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

QString fixturePath( const QString &relative )
{
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

SourceDescriptor gdalSource( const QString &path )
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  return source;
}

AssetId registerRaster( DataManager &manager, const QString &path,
                        PersistencePolicy persistence = PersistencePolicy::ProjectPersistent )
{
  RegisterRequest request;
  request.source = gdalSource( path );
  request.persistence = persistence;
  const auto registered = manager.registerSource( request );
  REQUIRE_FALSE( registered.assetId.isNull() );
  return registered.assetId;
}

DisplayViewId createView( QgisDisplayManager &manager, QgsMapCanvas &canvas,
                          QgsLayerTree &tree, QgsMapLayerStore &store )
{
  DisplayViewSpec spec;
  spec.canvas = &canvas;
  spec.layerTree = &tree;
  spec.layerStore = &store;
  const auto created = manager.createView( spec );
  REQUIRE( created );
  return created.value();
}

} // namespace

TEST_CASE( "The panel shows one row per Data Asset, not per Display Layer",
           "[data_manager_panel]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager( &dataManager );
  const DisplayViewId viewId = createView( displayManager, canvas, layerTree, layerStore );

  const AssetId first =
    registerRaster( dataManager, fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );
  const AssetId second =
    registerRaster( dataManager, fixturePath( QStringLiteral( "phr_xs.tif" ) ) );

  // Two Display Layers present the first asset; a separate asset is also shown.
  REQUIRE( displayManager.addLayer( viewId, first ) );
  REQUIRE( displayManager.addLayer( viewId, first ) );
  REQUIRE( displayManager.addLayer( viewId, second ) );

  sicnu::DataManagerPanel panel( &dataManager );

  // The panel projects assets, not display layers: two assets, two rows.
  CHECK( panel.rowCount() == 2 );
}

TEST_CASE( "Rows show status and persistence indicators", "[data_manager_panel]" )
{
  ensureQgisApplication();
  DataManager dataManager;

  const AssetId ready = registerRaster(
    dataManager, fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ),
    PersistencePolicy::ProjectPersistent );
  const AssetId missing = registerRaster(
    dataManager, fixturePath( QStringLiteral( "does-not-exist.tif" ) ),
    PersistencePolicy::SessionTemporary );

  sicnu::DataManagerPanel panel( &dataManager );

  CHECK( panel.rowText( ready, 2 ) == QStringLiteral( "就绪" ) );
  CHECK( panel.rowText( ready, 3 ) == QStringLiteral( "工程持久" ) );
  CHECK( panel.rowText( missing, 2 ) == QStringLiteral( "源缺失" ) );
  CHECK( panel.rowText( missing, 3 ) == QStringLiteral( "会话临时" ) );
}

TEST_CASE( "Double-clicking a row emits a display request for the Asset ID",
           "[data_manager_panel]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  const AssetId id =
    registerRaster( dataManager, fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );

  sicnu::DataManagerPanel panel( &dataManager );
  QSignalSpy displaySpy( &panel, &sicnu::DataManagerPanel::displayRequested );
  QSignalSpy unloadSpy( &panel, &sicnu::DataManagerPanel::unloadRequested );

  // The double-click / activation intent carries the asset to the Display Manager.
  panel.activateAsset( id );

  REQUIRE( displaySpy.count() == 1 );
  CHECK( displaySpy.first().first().value<AssetId>() == id );
  // Activation must not be confused with the remove intent.
  CHECK( unloadSpy.count() == 0 );
}

TEST_CASE( "The remove action emits an unload request, not a layer removal",
           "[data_manager_panel]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager( &dataManager );
  const DisplayViewId viewId = createView( displayManager, canvas, layerTree, layerStore );

  const AssetId id =
    registerRaster( dataManager, fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );
  const auto displayed = displayManager.addLayer( viewId, id );
  REQUIRE( displayed );

  sicnu::DataManagerPanel panel( &dataManager );
  QSignalSpy unloadSpy( &panel, &sicnu::DataManagerPanel::unloadRequested );

  panel.requestRemove( id );

  REQUIRE( unloadSpy.count() == 1 );
  CHECK( unloadSpy.first().first().value<AssetId>() == id );

  // The panel must not remove the display layer or touch the data itself.
  CHECK( displayManager.layer( displayed.value() ).has_value() );
  CHECK( dataManager.asset( id ).has_value() );
}

TEST_CASE( "Selecting a row does not change renderer state", "[data_manager_panel]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager( &dataManager );
  const DisplayViewId viewId = createView( displayManager, canvas, layerTree, layerStore );

  const AssetId id =
    registerRaster( dataManager, fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );
  const auto displayed = displayManager.addLayer( viewId, id );
  REQUIRE( displayed );

  QgsMapLayer *layer = displayManager.mapLayer( displayed.value() );
  REQUIRE( layer != nullptr );
  layer->setOpacity( 0.33 );

  sicnu::DataManagerPanel panel( &dataManager );
  panel.selectAsset( id );

  // Selection is a pure projection read: the display layer's renderer/opacity is
  // untouched.
  CHECK( layer->opacity() == 0.33 );
  CHECK( displayManager.mapLayer( displayed.value() ) == layer );
}

TEST_CASE( "The reference count reflects Display Layer view leases",
           "[data_manager_panel]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager( &dataManager );
  const DisplayViewId viewId = createView( displayManager, canvas, layerTree, layerStore );

  const AssetId id =
    registerRaster( dataManager, fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );

  sicnu::DataManagerPanel panel( &dataManager );
  CHECK( panel.rowText( id, 4 ) == QStringLiteral( "0" ) );

  REQUIRE( displayManager.addLayer( viewId, id ) );
  panel.refresh();
  CHECK( panel.rowText( id, 4 ) == QStringLiteral( "1" ) );

  REQUIRE( displayManager.addLayer( viewId, id ) );
  panel.refresh();
  CHECK( panel.rowText( id, 4 ) == QStringLiteral( "2" ) );
}

TEST_CASE( "The persistence column distinguishes all three policies",
           "[data_manager_panel]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  QTemporaryDir dir;

  // Three distinct source paths so the assets are not deduped to one.
  const auto stagedPath = [&dir]( const QString &name ) {
    const QString path = dir.filePath( name );
    REQUIRE( QFile::copy(
      fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path ) );
    return path;
  };

  const AssetId persistent =
    registerRaster( dataManager, stagedPath( QStringLiteral( "a.tif" ) ),
                    PersistencePolicy::ProjectPersistent );
  const AssetId session =
    registerRaster( dataManager, stagedPath( QStringLiteral( "b.tif" ) ),
                    PersistencePolicy::SessionTemporary );
  const AssetId task =
    registerRaster( dataManager, stagedPath( QStringLiteral( "c.tif" ) ),
                    PersistencePolicy::TaskTemporary );

  sicnu::DataManagerPanel panel( &dataManager );

  CHECK( panel.rowText( persistent, 3 ) == QStringLiteral( "工程持久" ) );
  CHECK( panel.rowText( session, 3 ) == QStringLiteral( "会话临时" ) );
  CHECK( panel.rowText( task, 3 ) == QStringLiteral( "任务临时" ) );
}

TEST_CASE( "A promote request is emitted for a temporary asset's id",
           "[data_manager_panel][promote]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  const AssetId id = registerRaster(
    dataManager, fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ),
    PersistencePolicy::SessionTemporary );

  sicnu::DataManagerPanel panel( &dataManager );
  QSignalSpy promoteSpy( &panel, &sicnu::DataManagerPanel::promoteRequested );

  // The promote intent carries the temporary asset's id to the shell, which
  // calls DataManager::promote. The panel itself holds no promote business logic.
  panel.requestPromote( id );

  REQUIRE( promoteSpy.count() == 1 );
  CHECK( promoteSpy.first().first().value<AssetId>() == id );
}

TEST_CASE( "requestPromote on an unknown asset emits nothing",
           "[data_manager_panel][promote]" )
{
  ensureQgisApplication();
  DataManager dataManager;

  sicnu::DataManagerPanel panel( &dataManager );
  QSignalSpy promoteSpy( &panel, &sicnu::DataManagerPanel::promoteRequested );

  panel.requestPromote( AssetId::generate() );

  CHECK( promoteSpy.count() == 0 );
}

TEST_CASE( "requestPromote on a persistent asset emits nothing",
           "[data_manager_panel][promote]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  const AssetId id = registerRaster(
    dataManager, fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ),
    PersistencePolicy::ProjectPersistent );

  sicnu::DataManagerPanel panel( &dataManager );
  QSignalSpy promoteSpy( &panel, &sicnu::DataManagerPanel::promoteRequested );

  panel.requestPromote( id );

  CHECK( promoteSpy.count() == 0 );
}

TEST_CASE( "A promoted asset is reflected immediately in the panel",
           "[data_manager_panel][promote]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  const AssetId id = registerRaster(
    dataManager, fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ),
    PersistencePolicy::SessionTemporary );

  sicnu::DataManagerPanel panel( &dataManager );
  REQUIRE( panel.rowText( id, 3 ) == QStringLiteral( "会话临时" ) );

  // The shell consumes promoteRequested and calls DataManager::promote. The
  // panel refreshes automatically via the assetChanged -> refresh connection
  // wired in its constructor - no project reload, no manual refresh needed.
  REQUIRE( dataManager.promote( id ) );

  CHECK( panel.rowText( id, 3 ) == QStringLiteral( "工程持久" ) );
}

// --- Collections (#53) ---

TEST_CASE( "The panel shows a collection as a parent row with its children nested",
           "[data_manager_panel][collection]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  QTemporaryDir dir;

  const auto stagedPath = [&dir]( const QString &name ) {
    const QString path = dir.filePath( name );
    REQUIRE( QFile::copy(
      fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path ) );
    return path;
  };

  const AssetId childA = registerRaster( dataManager, stagedPath( QStringLiteral( "a.tif" ) ) );
  const AssetId childB = registerRaster( dataManager, stagedPath( QStringLiteral( "b.tif" ) ) );

  ProductMetadata metadata;
  metadata.platform = QStringLiteral( "Sentinel-2A" );
  CollectionCreateRequest collectionRequest{ QStringLiteral( "S2A scene" ), metadata };
  const CollectionId collectionId =
    dataManager.createCollection( collectionRequest ).collectionId;
  REQUIRE( dataManager.addChildToCollection( collectionId, childA ) );
  REQUIRE( dataManager.addChildToCollection( collectionId, childB ) );

  sicnu::DataManagerPanel panel( &dataManager );

  // The collection is one parent row (the two children are nested under it, so
  // topLevelItemCount is 1 for this collection).
  CHECK( panel.rowCount() == 1 );
  // Both children are present, carrying their own AssetIds.
  CHECK( panel.rowText( childA, 0 ) == QStringLiteral( "a" ) );
  CHECK( panel.rowText( childB, 0 ) == QStringLiteral( "b" ) );
}

TEST_CASE( "Double-clicking a collection's child emits a display request for the child",
           "[data_manager_panel][collection]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  QTemporaryDir dir;

  const QString path = dir.filePath( QStringLiteral( "child.tif" ) );
  REQUIRE( QFile::copy(
    fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path ) );
  const AssetId child = registerRaster( dataManager, path );

  const CollectionId collectionId = dataManager
    .createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( dataManager.addChildToCollection( collectionId, child ) );

  sicnu::DataManagerPanel panel( &dataManager );
  QSignalSpy displaySpy( &panel, &sicnu::DataManagerPanel::displayRequested );

  // A collection's child is a full Data Asset: activating it still emits a
  // display request for the child's AssetId (collection membership does not
  // limit what the user can do with the band).
  panel.activateAsset( child );

  REQUIRE( displaySpy.count() == 1 );
  CHECK( displaySpy.first().first().value<AssetId>() == child );
}

TEST_CASE( "Standalone assets stay top-level alongside collection parent rows",
           "[data_manager_panel][collection]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  QTemporaryDir dir;

  const auto stagedPath = [&dir]( const QString &name ) {
    const QString path = dir.filePath( name );
    REQUIRE( QFile::copy(
      fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path ) );
    return path;
  };

  const AssetId standalone = registerRaster( dataManager, stagedPath( QStringLiteral( "s.tif" ) ) );
  const AssetId child = registerRaster( dataManager, stagedPath( QStringLiteral( "c.tif" ) ) );

  const CollectionId collectionId = dataManager
    .createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( dataManager.addChildToCollection( collectionId, child ) );

  sicnu::DataManagerPanel panel( &dataManager );

  // One collection parent row + one standalone asset row = 2 top-level items.
  CHECK( panel.rowCount() == 2 );
  // Both the standalone asset and the collection's child are findable.
  CHECK( panel.rowText( standalone, 0 ) == QStringLiteral( "s" ) );
  CHECK( panel.rowText( child, 0 ) == QStringLiteral( "c" ) );
}

TEST_CASE( "Selecting an asset fills the metadata detail panel",
           "[data_manager_panel][detail]" )
{
  ensureQgisApplication();
  DataManager dataManager;
  const AssetId id =
    registerRaster( dataManager, fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );

  sicnu::DataManagerPanel panel( &dataManager );
  panel.selectAsset( id );

  const QString html = panel.detailHtml();
  REQUIRE_FALSE( html.isEmpty() );
  CHECK( html.contains( QStringLiteral( "资产 ID" ) ) );
  CHECK( html.contains( id.toString() ) );
  CHECK( html.contains( QStringLiteral( "数据源" ) ) );
  CHECK( ( html.contains( QStringLiteral( "栅格结构" ) )
           || html.contains( QStringLiteral( "结构" ) ) ) );
}

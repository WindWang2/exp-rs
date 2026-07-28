#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFileInfo>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayerstore.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

#include "app/display/qgis_display_manager.h"
#include "app/project_context.h"
#include "app/shell/rs_session_map_workspace.h"
#include "data/data_asset.h"
#include "data/data_manager.h"

using namespace sicnu::data;
namespace app = sicnu::app;

namespace
{

QString fixturePath( const QString &relative )
{
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

std::unique_ptr<app::ProjectContext>
createContext( QgsMapCanvas &canvas, QgsProject &project )
{
  const sicnu::display::DisplayViewSpec viewSpec{
    &canvas, project.layerTreeRoot(), project.layerStore() };
  auto created = app::ProjectContext::create( viewSpec );
  REQUIRE( created );
  return created.take();
}

/// Stages a fixture copy and returns its absolute path.
QString stageFixture( QTemporaryDir &dir, const QString &fixture, const QString &name )
{
  const QString path = dir.filePath( name );
  REQUIRE( QFile::copy( fixturePath( fixture ), path ) );
  return path;
}

/// Registers a raster asset with the given persistence + DeletableSource.
AssetId registerRasterAsset( DataManager &manager,
                             const QString &path,
                             PersistencePolicy policy )
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  RegisterRequest request;
  request.source = source;
  request.persistence = policy;
  request.additionalCapabilities = AssetCapability::DeletableSource;
  const RegisterResult result = manager.registerSource( request );
  REQUIRE( !result.assetId.isNull() );
  return result.assetId;
}

} // namespace

TEST_CASE( "closeSession reaps idle SessionTemporary assets and keeps persistent ones",
           "[project_context][reap]" )
{
  {
    QgsProject *project = QgsProject::instance();
    project->clear();
    QgsMapCanvas canvas;
    QTemporaryDir dir;
    auto context = createContext( canvas, *project );
    DataManager &manager = context->dataManager();

    // A persistent asset - must survive closeSession.
    const QString persistentPath =
      stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                    QStringLiteral( "persistent.tif" ) );
    const AssetId persistentId =
      registerRasterAsset( manager, persistentPath, PersistencePolicy::ProjectPersistent );

    // An idle session-temporary asset - must be reaped (catalog + file).
    const QString sessionPath =
      stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                    QStringLiteral( "session.tif" ) );
    const AssetId sessionId =
      registerRasterAsset( manager, sessionPath, PersistencePolicy::SessionTemporary );

    REQUIRE( QFile::exists( sessionPath ) );

    const auto reapResult = context->closeSession();

    CHECK( reapResult.reapedCount == 1 );

    // The session-temporary is gone from catalog and disk.
    CHECK_FALSE( manager.asset( sessionId ).has_value() );
    CHECK_FALSE( QFile::exists( sessionPath ) );

    // The persistent asset survives (its file is not deleted by the reap).
    CHECK( manager.asset( persistentId ).has_value() );
    CHECK( QFile::exists( persistentPath ) );
  }
}

TEST_CASE( "closeSession reports a leased SessionTemporary it could not reap",
           "[project_context][reap]" )
{
  {
    QgsProject *project = QgsProject::instance();
    project->clear();
    QgsMapCanvas canvas;
    QTemporaryDir dir;
    auto context = createContext( canvas, *project );
    DataManager &manager = context->dataManager();

    const QString leasedPath =
      stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                    QStringLiteral( "leased.tif" ) );
    const AssetId leasedId =
      registerRasterAsset( manager, leasedPath, PersistencePolicy::SessionTemporary );
    auto lease = manager
                   .acquire( AssetRef{ leasedId, AssetRevision::initial() },
                             AssetUse{ LeaseKind::View, QStringLiteral( "viewer" ) } )
                   .take();
    REQUIRE( lease.isValid() );

    const auto reapResult = context->closeSession();

    CHECK( reapResult.reapedCount == 0 );
    REQUIRE( reapResult.skippedLeased.size() == 1 );
    CHECK( reapResult.skippedLeased.first() == leasedId );
    // The leased asset and its file remain (host decides what to do).
    CHECK( manager.asset( leasedId ).has_value() );
    CHECK( QFile::exists( leasedPath ) );
  }
}

TEST_CASE( "Destroying a ProjectContext reaps session temporaries",
           "[project_context][reap]" )
{
  QString sessionPath;
  AssetId sessionId;
  bool sessionFileExistedBeforeDestruction = false;
  {
    QgsProject *project = QgsProject::instance();
    project->clear();
    QgsMapCanvas canvas;
    QTemporaryDir dir;
    auto context = createContext( canvas, *project );

    sessionPath =
      stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                    QStringLiteral( "session.tif" ) );
    // The temp dir owns the file; keep it alive past context destruction by
    // moving it out of the temporary dir's scope.
    const QString keptPath =
      QFileInfo( sessionPath ).dir().filePath( QStringLiteral( "kept_session.tif" ) );
    REQUIRE( QFile::rename( sessionPath, keptPath ) );
    sessionPath = keptPath;

    sessionId =
      registerRasterAsset( context->dataManager(), sessionPath,
                           PersistencePolicy::SessionTemporary );
    sessionFileExistedBeforeDestruction = QFile::exists( sessionPath );
    REQUIRE( sessionFileExistedBeforeDestruction );

    // Destroy the context (simulates app close). The destructor reaps.
    context.reset();
  }
  // After destruction the session-temporary file is gone.
  CHECK( sessionFileExistedBeforeDestruction );
  CHECK_FALSE( QFile::exists( sessionPath ) );
  QFile::remove( sessionPath ); // tidy in case the assertion above is wrong
  QgsProject::instance()->clear();
}

TEST_CASE( "clearProject reaps session temporaries then unloads the rest",
           "[project_context][reap]" )
{
  {
    QgsProject *project = QgsProject::instance();
    project->clear();
    QgsMapCanvas canvas;
    QTemporaryDir dir;
    auto context = createContext( canvas, *project );
    DataManager &manager = context->dataManager();

    // An idle session-temporary - must be reaped (file deleted) by clearProject.
    const QString sessionPath =
      stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                    QStringLiteral( "session.tif" ) );
    const AssetId sessionId =
      registerRasterAsset( manager, sessionPath, PersistencePolicy::SessionTemporary );

    // A persistent asset - clearProject unloads it (catalog cleared), but its
    // file must NOT be deleted (it is not DeletableSource-owned by reap; unload
    // never deletes).
    const QString persistentPath =
      stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                    QStringLiteral( "persistent.tif" ) );
    const AssetId persistentId =
      registerRasterAsset( manager, persistentPath, PersistencePolicy::ProjectPersistent );

    REQUIRE( QFile::exists( sessionPath ) );

    const auto cleared = context->clearProject( *project );
    REQUIRE( cleared );

    // Both assets are gone from the catalog (clearProject unloads all), but
    // only the session-temporary's file was deleted by the reap-first step.
    CHECK_FALSE( manager.asset( sessionId ).has_value() );
    CHECK_FALSE( QFile::exists( sessionPath ) );
    CHECK_FALSE( manager.asset( persistentId ).has_value() );
    CHECK( QFile::exists( persistentPath ) ); // unload never deletes
  }
  QgsProject::instance()->clear();
}

// ---------------------------------------------------------------------------
// #68: ProjectContext multi-view host — createSecondaryView / views() /
// removeView, and the clear-all-views leak fix.
//
// Pre-fix, clearProject only cleared the main view's display layers; a
// secondary view's layers (and the asset leases they held) survived until the
// destructor reaped them — at which point closeSession had already skipped
// them as leased. These tests pin the multi-view host contract.
// ---------------------------------------------------------------------------

/// A secondary view's host-owned {canvas, tree, store} triple. Lives on the
/// stack for the test so the display manager's QPointers stay valid.
struct SecondaryViewHost {
  QgsMapCanvas canvas;
  QgsLayerTree tree;
  QgsMapLayerStore store;

  sicnu::display::DisplayViewSpec spec() {
    return sicnu::display::DisplayViewSpec{ &canvas, &tree, &store };
  }
};

TEST_CASE( "createSecondaryView returns a distinct id tracked in views()",
           "[project_context][multi_view]" )
{
  {
    QgsProject *project = QgsProject::instance();
    project->clear();
    QgsMapCanvas canvas;
    auto context = createContext( canvas, *project );

    SecondaryViewHost secondary;
    const auto created = context->createSecondaryView( secondary.spec() );
    REQUIRE( created );
    const sicnu::display::DisplayViewId secondaryId = created.value();

    // The secondary id is distinct from the main view id.
    CHECK( secondaryId != context->mainViewId() );

    // views() reports both, main first then the secondary in creation order.
    const QVector<sicnu::display::DisplayViewId> live = context->views();
    REQUIRE( live.size() == 2 );
    CHECK( live.at( 0 ) == context->mainViewId() );
    CHECK( live.at( 1 ) == secondaryId );
  }
  QgsProject::instance()->clear();
}

TEST_CASE( "removeView refuses the main view (QGIS-interop view is non-removable)",
           "[project_context][multi_view]" )
{
  {
    QgsProject *project = QgsProject::instance();
    project->clear();
    QgsMapCanvas canvas;
    auto context = createContext( canvas, *project );

    const auto removed = context->removeView( context->mainViewId() );

    // The main view is not removable through the secondary-view path.
    REQUIRE_FALSE( removed );
    REQUIRE_FALSE( removed.diagnostics().isEmpty() );

    // The main view is still live (the refusal changed nothing).
    const QVector<sicnu::display::DisplayViewId> live = context->views();
    REQUIRE( live.size() == 1 );
    CHECK( live.at( 0 ) == context->mainViewId() );
  }
  QgsProject::instance()->clear();
}

TEST_CASE( "removeView drops a secondary view from views()",
           "[project_context][multi_view]" )
{
  {
    QgsProject *project = QgsProject::instance();
    project->clear();
    QgsMapCanvas canvas;
    auto context = createContext( canvas, *project );

    SecondaryViewHost secondary;
    const auto created = context->createSecondaryView( secondary.spec() );
    REQUIRE( created );
    const sicnu::display::DisplayViewId secondaryId = created.value();

    REQUIRE( context->views().size() == 2 );

    const auto removed = context->removeView( secondaryId );
    REQUIRE( removed );

    // Only the main view remains.
    const QVector<sicnu::display::DisplayViewId> live = context->views();
    REQUIRE( live.size() == 1 );
    CHECK( live.at( 0 ) == context->mainViewId() );
  }
  QgsProject::instance()->clear();
}

TEST_CASE( "clearProject removes layers across ALL views (no secondary leak)",
           "[project_context][multi_view]" )
{
  {
    QgsProject *project = QgsProject::instance();
    project->clear();
    QgsMapCanvas canvas;
    QTemporaryDir dir;
    auto context = createContext( canvas, *project );
    DataManager &manager = context->dataManager();
    sicnu::display::QgisDisplayManager &display = context->displayManager();

    SecondaryViewHost secondary;
    const auto created = context->createSecondaryView( secondary.spec() );
    REQUIRE( created );
    const sicnu::display::DisplayViewId secondaryId = created.value();

    // A persistent raster shown ONLY in the secondary view. Its lease is held
    // by the secondary view's display layer.
    const QString rasterPath =
      stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                    QStringLiteral( "secondary_only.tif" ) );
    const AssetId assetId =
      registerRasterAsset( manager, rasterPath, PersistencePolicy::ProjectPersistent );

    const auto added =
      display.addLayer( secondaryId, assetId,
                        sicnu::display::AddLayerOptions{
                          QStringLiteral( "secondary" ), true } );
    REQUIRE( added );
    REQUIRE( manager.leaseCount( assetId ) == 1 );

    // clearProject must tear down the secondary view's layer too, releasing its
    // lease. Pre-fix, the lease survived clearProject (only the main view was
    // cleared) and the asset was NOT unloaded — leaked until the destructor.
    const auto cleared = context->clearProject( *project );
    REQUIRE( cleared );

    // The lease is gone (the secondary's display layer was removed), and the
    // asset was unloaded from the catalog.
    CHECK( manager.leaseCount( assetId ) == 0 );
    CHECK_FALSE( manager.asset( assetId ).has_value() );
    // The secondary view's layer record is gone.
    CHECK_FALSE( display.layer( added.value() ).has_value() );
  }
  QgsProject::instance()->clear();
}

// ---------------------------------------------------------------------------
// Wave E: RsSessionMapWorkspace as secondary Display View (no dual bridges).
// ---------------------------------------------------------------------------

TEST_CASE( "session map workspace binds as secondary view without local bridge",
           "[project_context][multi_view][wave_e]" )
{
  {
    QgsProject *project = QgsProject::instance();
    project->clear();
    QgsMapCanvas mainCanvas;
    auto context = createContext( mainCanvas, *project );

    QgsMapCanvas sessionCanvas;
    RsSessionMapWorkspace session( &sessionCanvas );
    REQUIRE( session.hasLocalBridge() );

    // Host must release the session-local bridge before createSecondaryView
    // (DisplayManager creates the sole bridge for that tree+canvas).
    session.releaseLocalBridge();
    REQUIRE_FALSE( session.hasLocalBridge() );

    const auto created = context->createSecondaryView( session.viewSpec() );
    REQUIRE( created );
    const sicnu::display::DisplayViewId sessionViewId = created.value();

    CHECK( sessionViewId != context->mainViewId() );
    REQUIRE( context->views().size() == 2 );
    CHECK( context->views().at( 1 ) == sessionViewId );

    // Session-private add still works; DM bridge owns canvas membership.
    auto *mem = new QgsVectorLayer(
      QStringLiteral( "Polygon?crs=EPSG:4326" ), QStringLiteral( "samples" ),
      QStringLiteral( "memory" ) );
    REQUIRE( mem->isValid() );
    session.addLayer( mem, true );
    CHECK( session.layerStore()->mapLayer( mem->id() ) == mem );
    CHECK( session.layerTree()->findLayer( mem->id() ) != nullptr );

    REQUIRE( context->removeView( sessionViewId ) );
    REQUIRE( context->views().size() == 1 );

    session.restoreLocalBridge();
    REQUIRE( session.hasLocalBridge() );
  }
  QgsProject::instance()->clear();
}

TEST_CASE( "dual session maps register as two secondary views (georef I2I shape)",
           "[project_context][multi_view][wave_e]" )
{
  {
    QgsProject *project = QgsProject::instance();
    project->clear();
    QgsMapCanvas mainCanvas;
    auto context = createContext( mainCanvas, *project );

    QgsMapCanvas srcCanvas;
    QgsMapCanvas dstCanvas;
    RsSessionMapWorkspace src( &srcCanvas );
    RsSessionMapWorkspace dst( &dstCanvas );

    src.releaseLocalBridge();
    dst.releaseLocalBridge();

    const auto srcId = context->createSecondaryView( src.viewSpec() );
    const auto dstId = context->createSecondaryView( dst.viewSpec() );
    REQUIRE( srcId );
    REQUIRE( dstId );
    CHECK( srcId.value() != dstId.value() );

    const QVector<sicnu::display::DisplayViewId> live = context->views();
    REQUIRE( live.size() == 3 );
    CHECK( live.at( 0 ) == context->mainViewId() );
    CHECK( live.at( 1 ) == srcId.value() );
    CHECK( live.at( 2 ) == dstId.value() );

    REQUIRE( context->removeView( srcId.value() ) );
    REQUIRE( context->removeView( dstId.value() ) );
    REQUIRE( context->views().size() == 1 );
  }
  QgsProject::instance()->clear();
}

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFileInfo>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>

#include "app/project_context.h"
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

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

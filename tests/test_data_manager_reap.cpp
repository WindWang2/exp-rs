#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>

#include <thread>
#include <vector>

#include <gdal.h>
#include <cpl_conv.h>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/source_descriptor.h"

#include "processing/framework/output_committer.h"

using namespace sicnu::data;

namespace
{

// Synthesise a small GeoTIFF (16×16, single Float32 band) into `dir/name` so
// the test does not depend on a committed sample raster under data/samples/.
static QString createTestRaster( const QString &dir, const QString &name )
{
  GDALAllRegister();
  const QString path = dir + QLatin1Char( '/' ) + name;
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );

  constexpr int W = 16, H = 16;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );

  double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection(
    ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );

  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( W, 1.0f );
  for ( int row = 0; row < H; ++row )
    GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );

  GDALClose( ds );
  return path;
}

/// Stages a synthesised GeoTIFF into dir under name, returning the absolute
/// path. (Replaces a former copy of a committed fixture; `fixture` is retained
/// for call-site symmetry but ignored.)
QString stageFixture( QTemporaryDir &dir, const QString & /*fixture*/, const QString &name )
{
  return createTestRaster( dir.path(), name );
}

/// Registers a real (GDAL-resolved) raster as a temporary asset with an
/// additional DeletableSource capability, so it can be reaped.
AssetId registerDeletableTemporaryRaster( DataManager &manager,
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

TEST_CASE( "A temporary asset with DeletableSource is reaped from catalog and disk",
           "[data_manager][reap]" )
{
  QTemporaryDir dir;
  DataManager manager;
  QSignalSpy aboutToUnloadSpy( &manager, &DataManager::assetAboutToUnload );
  QSignalSpy removedSpy( &manager, &DataManager::assetRemoved );

  const QString path =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ), QStringLiteral( "scene.tif" ) );
  const AssetId id =
    registerDeletableTemporaryRaster( manager, path, PersistencePolicy::SessionTemporary );

  // Sanity: the asset is registered and carries DeletableSource.
  const auto snapshot = manager.asset( id );
  REQUIRE( snapshot.has_value() );
  CHECK( snapshot->capabilities().testFlag( AssetCapability::DeletableSource ) );

  const ReapResult result = manager.reap( ReapRequest{ id } );

  CHECK( result.unloaded );
  CHECK( result.sourceDeleted );
  CHECK_FALSE( manager.asset( id ).has_value() );
  CHECK_FALSE( QFile::exists( path ) );
  CHECK( aboutToUnloadSpy.count() == 1 );
  CHECK( removedSpy.count() == 1 );
}

TEST_CASE( "Reaping a ProjectPersistent asset is refused", "[data_manager][reap]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString path =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ), QStringLiteral( "persistent.tif" ) );
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  RegisterRequest request;
  request.source = source;
  request.persistence = PersistencePolicy::ProjectPersistent;
  request.additionalCapabilities = AssetCapability::DeletableSource;
  const AssetId id = manager.registerSource( request ).assetId;
  REQUIRE( !id.isNull() );

  const ReapResult result = manager.reap( ReapRequest{ id } );

  CHECK_FALSE( result.unloaded );
  REQUIRE_FALSE( result.diagnostics.isEmpty() );
  CHECK( manager.asset( id ).has_value() );
  CHECK( QFile::exists( path ) );
}

TEST_CASE( "Reaping an asset with an active lease is refused", "[data_manager][reap]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString path =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ), QStringLiteral( "leased.tif" ) );
  const AssetId id =
    registerDeletableTemporaryRaster( manager, path, PersistencePolicy::SessionTemporary );

  auto lease = manager
                 .acquire( AssetRef{ id, AssetRevision::initial() },
                           AssetUse{ LeaseKind::View, QStringLiteral( "viewer" ) } )
                 .take();
  REQUIRE( lease.isValid() );

  const ReapResult result = manager.reap( ReapRequest{ id } );

  CHECK_FALSE( result.unloaded );
  REQUIRE_FALSE( result.diagnostics.isEmpty() );
  CHECK( manager.asset( id ).has_value() );
  CHECK( QFile::exists( path ) );
}

TEST_CASE( "A temporary asset without DeletableSource is unloaded but its file is kept",
           "[data_manager][reap]" )
{
  QTemporaryDir dir;
  DataManager manager;

  // Register without the DeletableSource override - a temporary asset whose
  // source the Data Manager does not own.
  const QString path =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ), QStringLiteral( "imported.tif" ) );
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  RegisterRequest request;
  request.source = source;
  request.persistence = PersistencePolicy::SessionTemporary;
  const AssetId id = manager.registerSource( request ).assetId;
  REQUIRE( !id.isNull() );
  REQUIRE_FALSE( manager.asset( id )->capabilities().testFlag( AssetCapability::DeletableSource ) );

  const ReapResult result = manager.reap( ReapRequest{ id } );

  CHECK( result.unloaded );
  CHECK_FALSE( result.sourceDeleted );
  CHECK_FALSE( manager.asset( id ).has_value() );
  CHECK( QFile::exists( path ) ); // imported file is untouched
}

TEST_CASE( "A reap of an unknown asset is rejected", "[data_manager][reap]" )
{
  DataManager manager;
  const ReapResult result = manager.reap( ReapRequest{ AssetId::generate() } );

  CHECK_FALSE( result.unloaded );
  REQUIRE_FALSE( result.diagnostics.isEmpty() );
}

TEST_CASE( "Reaping an asset whose file cannot be deleted still unloads and warns",
           "[data_manager][reap]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString path =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ), QStringLiteral( "doomed.tif" ) );
  const AssetId id =
    registerDeletableTemporaryRaster( manager, path, PersistencePolicy::TaskTemporary );

#ifdef _WIN32
  QFile lockFile( path );
  REQUIRE( lockFile.open( QIODevice::ReadWrite ) );
  const ReapResult result = manager.reap( ReapRequest{ id } );
  lockFile.close();
#else
  // Make the file's parent directory non-writable so removal fails. On Unix,
  // removing a file requires write permission on its parent directory.
  const QDir parentDir( QFileInfo( path ).absolutePath() );
  REQUIRE( QFile::setPermissions( parentDir.absolutePath(),
            QFile::ReadOwner | QFile::ExeOwner ) );

  const ReapResult result = manager.reap( ReapRequest{ id } );

  // Restore permissions so QTemporaryDir can clean up.
  QFile::setPermissions( parentDir.absolutePath(),
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner );
#endif

  CHECK( result.unloaded );        // catalog entry removed
  CHECK_FALSE( result.sourceDeleted );
  REQUIRE_FALSE( result.diagnostics.isEmpty() );
  CHECK( result.diagnostics.first().severity == DiagnosticSeverity::Warning );
  CHECK_FALSE( manager.asset( id ).has_value() );
}

TEST_CASE( "An OutputCommitter-published output carries DeletableSource and is reaped",
           "[data_manager][reap]" )
{
  QTemporaryDir dir;
  DataManager manager;
  sicnu::OutputCommitter committer( &manager );

  // Commit a real output through the committer exactly as an algorithm would:
  // the committer stamps DeletableSource on the registered asset itself.
  const QString tempPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ), QStringLiteral( "scratch.tif" ) );
  const QString stablePath = dir.filePath( QStringLiteral( "published.tif" ) );

  sicnu::AlgorithmOutputRequest commitRequest;
  commitRequest.kind = AssetKind::Raster;
  commitRequest.tempPath = tempPath;
  commitRequest.stablePath = stablePath;
  commitRequest.persistence = PersistencePolicy::SessionTemporary;
  commitRequest.autoLoad = false;
  commitRequest.derivation.algorithmId = QStringLiteral( "sicnu:ndvi" );

  const auto commitResult = committer.commit( commitRequest );
  REQUIRE( commitResult );
  const AssetId id = commitResult.value();
  REQUIRE( QFile::exists( stablePath ) );

  // The committed output carries DeletableSource without the caller asking.
  REQUIRE( manager.asset( id )->capabilities().testFlag( AssetCapability::DeletableSource ) );

  // Reaping it removes the catalog entry and the published file.
  const ReapResult reapResult = manager.reap( ReapRequest{ id } );

  CHECK( reapResult.unloaded );
  CHECK( reapResult.sourceDeleted );
  CHECK_FALSE( manager.asset( id ).has_value() );
  CHECK_FALSE( QFile::exists( stablePath ) );
}

TEST_CASE( "reapSessionTemporaries removes only idle SessionTemporary assets",
           "[data_manager][reap][sweep]" )
{
  QTemporaryDir dir;
  DataManager manager;

  // A persistent asset - must survive the sweep untouched.
  const QString persistentPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "persistent.tif" ) );
  SourceDescriptor persistentSource;
  persistentSource.providerKey = QStringLiteral( "gdal" );
  persistentSource.canonicalSource = persistentPath;
  RegisterRequest persistentReq;
  persistentReq.source = persistentSource;
  persistentReq.persistence = PersistencePolicy::ProjectPersistent;
  persistentReq.additionalCapabilities = AssetCapability::DeletableSource;
  const AssetId persistentId = manager.registerSource( persistentReq ).assetId;

  // A task-temporary asset - must survive the sweep untouched.
  const QString taskPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "task.tif" ) );
  const AssetId taskId =
    registerDeletableTemporaryRaster( manager, taskPath, PersistencePolicy::TaskTemporary );

  // An idle session-temporary asset - must be reaped (catalog + file).
  const QString sessionPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "session.tif" ) );
  const AssetId sessionId =
    registerDeletableTemporaryRaster( manager, sessionPath, PersistencePolicy::SessionTemporary );

  // A leased session-temporary asset - must be skipped and reported.
  const QString leasedPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "leased.tif" ) );
  const AssetId leasedId =
    registerDeletableTemporaryRaster( manager, leasedPath, PersistencePolicy::SessionTemporary );
  auto lease = manager
                 .acquire( AssetRef{ leasedId, AssetRevision::initial() },
                           AssetUse{ LeaseKind::View, QStringLiteral( "viewer" ) } )
                 .take();
  REQUIRE( lease.isValid() );

  const TemporaryReapResult result = manager.reapSessionTemporaries();

  CHECK( result.reapedCount == 1 );
  REQUIRE( result.skippedLeased.size() == 1 );
  CHECK( result.skippedLeased.first() == leasedId );

  // The idle session-temporary is gone from catalog and disk.
  CHECK_FALSE( manager.asset( sessionId ).has_value() );
  CHECK_FALSE( QFile::exists( sessionPath ) );

  // The leased session-temporary remains (host decides what to do).
  CHECK( manager.asset( leasedId ).has_value() );
  CHECK( QFile::exists( leasedPath ) );

  // Persistent and task-temporary are untouched.
  CHECK( manager.asset( persistentId ).has_value() );
  CHECK( QFile::exists( persistentPath ) );
  CHECK( manager.asset( taskId ).has_value() );
  CHECK( QFile::exists( taskPath ) );
}

TEST_CASE( "reapSessionTemporaries with no temporaries reaps nothing",
           "[data_manager][reap][sweep]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString persistentPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "persistent.tif" ) );
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = persistentPath;
  RegisterRequest request;
  request.source = source;
  request.persistence = PersistencePolicy::ProjectPersistent;
  const AssetId id = manager.registerSource( request ).assetId;

  const TemporaryReapResult result = manager.reapSessionTemporaries();

  CHECK( result.reapedCount == 0 );
  CHECK( result.skippedLeased.isEmpty() );
  CHECK( manager.asset( id ).has_value() );
}

TEST_CASE( "reapTaskTemporaries removes only idle TaskTemporary assets",
           "[data_manager][reap][task_sweep]" )
{
  QTemporaryDir dir;
  DataManager manager;

  // A persistent asset - must survive the task-scope sweep untouched.
  const QString persistentPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "persistent.tif" ) );
  SourceDescriptor persistentSource;
  persistentSource.providerKey = QStringLiteral( "gdal" );
  persistentSource.canonicalSource = persistentPath;
  RegisterRequest persistentReq;
  persistentReq.source = persistentSource;
  persistentReq.persistence = PersistencePolicy::ProjectPersistent;
  persistentReq.additionalCapabilities = AssetCapability::DeletableSource;
  const AssetId persistentId = manager.registerSource( persistentReq ).assetId;

  // A session-temporary asset - must survive the task-scope sweep untouched.
  const QString sessionPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "session.tif" ) );
  const AssetId sessionId =
    registerDeletableTemporaryRaster( manager, sessionPath, PersistencePolicy::SessionTemporary );

  // An idle task-temporary asset - must be reaped (catalog + file).
  const QString taskPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "task.tif" ) );
  const AssetId taskId =
    registerDeletableTemporaryRaster( manager, taskPath, PersistencePolicy::TaskTemporary );

  // A leased task-temporary asset - must be skipped and reported.
  const QString leasedPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "leased_task.tif" ) );
  const AssetId leasedId =
    registerDeletableTemporaryRaster( manager, leasedPath, PersistencePolicy::TaskTemporary );
  auto lease = manager
                 .acquire( AssetRef{ leasedId, AssetRevision::initial() },
                           AssetUse{ LeaseKind::Task, QStringLiteral( "downstream" ) } )
                 .take();
  REQUIRE( lease.isValid() );

  const TemporaryReapResult result = manager.reapTaskTemporaries();

  CHECK( result.reapedCount == 1 );
  REQUIRE( result.skippedLeased.size() == 1 );
  CHECK( result.skippedLeased.first() == leasedId );

  // The idle task-temporary is gone from catalog and disk.
  CHECK_FALSE( manager.asset( taskId ).has_value() );
  CHECK_FALSE( QFile::exists( taskPath ) );

  // The leased task-temporary remains until its lease releases.
  CHECK( manager.asset( leasedId ).has_value() );
  CHECK( QFile::exists( leasedPath ) );

  // Session-temporary and persistent are untouched by the task-scope sweep.
  CHECK( manager.asset( sessionId ).has_value() );
  CHECK( QFile::exists( sessionPath ) );
  CHECK( manager.asset( persistentId ).has_value() );
  CHECK( QFile::exists( persistentPath ) );
}

TEST_CASE( "reapTaskTemporaries with no task temporaries reaps nothing",
           "[data_manager][reap][task_sweep]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString sessionPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "session.tif" ) );
  const AssetId sessionId =
    registerDeletableTemporaryRaster( manager, sessionPath, PersistencePolicy::SessionTemporary );

  const TemporaryReapResult result = manager.reapTaskTemporaries();

  CHECK( result.reapedCount == 0 );
  CHECK( result.skippedLeased.isEmpty() );
  CHECK( manager.asset( sessionId ).has_value() );
  CHECK( QFile::exists( sessionPath ) );
}

TEST_CASE( "reap survives a re-entrant assetAboutToUnload slot that mutates the catalog (#615)",
           "[data_manager][reap][reentrancy]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString victimPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ), QStringLiteral( "victim.tif" ) );
  const AssetId victimId =
    registerDeletableTemporaryRaster( manager, victimPath, PersistencePolicy::SessionTemporary );

  const QString bystanderPath =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ), QStringLiteral( "bystander.tif" ) );
  const AssetId bystanderId =
    registerDeletableTemporaryRaster( manager, bystanderPath, PersistencePolicy::SessionTemporary );

  // A DirectConnection slot that unloads ANOTHER asset when the victim's
  // unload is announced - this reallocates/mutates m_impl->records between
  // the old code's snapshot capture and its erase (UAF / stale iterator).
  // The guard flag keeps the re-entrant unload from recursing when the
  // bystander's own assetAboutToUnload fires.
  bool bystanderUnloaded = false;
  const QMetaObject::Connection conn = QObject::connect(
    &manager, &DataManager::assetAboutToUnload, &manager,
    [&manager, &bystanderUnloaded, bystanderId]( const AssetId &announced ) {
      if ( bystanderUnloaded || announced == bystanderId )
        return;
      bystanderUnloaded = true;
      const auto plan = manager.planUnload( bystanderId ).confirmedCascade();
      manager.unload( plan );
    },
    Qt::DirectConnection );

  const ReapResult result = manager.reap( ReapRequest{ victimId } );

  CHECK( result.unloaded );
  CHECK_FALSE( manager.asset( victimId ).has_value() );
  CHECK_FALSE( manager.asset( bystanderId ).has_value() );
}

TEST_CASE( "AssetLease released from a worker thread does not strand the lease (#615)",
           "[data_manager][lease][threads]" )
{
  // Queued lease cleanup needs an event dispatcher on the manager's thread
  // (production always has the application event loop).
  static QCoreApplication *app = []() {
    if ( !QCoreApplication::instance() )
    {
      static int argc = 1;
      static char a0[] = "test_data_manager_reap";
      char *argv[] = {a0, nullptr};
      return new QCoreApplication( argc, argv );
    }
    return QCoreApplication::instance();
  }();
  (void)app;

  QTemporaryDir dir;
  DataManager manager;

  const QString path =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ), QStringLiteral( "leased.tif" ) );
  const AssetId id =
    registerDeletableTemporaryRaster( manager, path, PersistencePolicy::SessionTemporary );

  {
    auto leaseResult = manager.acquire( AssetRef{ id, AssetRevision::initial() },
                                        AssetUse{ LeaseKind::Task, QStringLiteral( "worker" ) } );
    REQUIRE( leaseResult );
    AssetLease lease = leaseResult.take();
    REQUIRE( lease.isValid() );

    // Release from a foreign thread (as the destructor would): the holder's
    // single release attempt must consume the lease instead of returning
    // Invalid and leaving it active forever.
    std::thread releaser( [l = std::move( lease )]() mutable { (void)l.release(); } );
    releaser.join();
  }

  // The manager-thread queued cleanup runs through the event loop; drain it.
  for ( int i = 0; i < 10; ++i )
    QCoreApplication::processEvents( QEventLoop::AllEvents, 5 );

  // unload/reap must no longer be blocked by a stranded lease.
  const ReapResult result = manager.reap( ReapRequest{ id } );
  CHECK( result.unloaded );
}

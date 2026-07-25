#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/source_descriptor.h"

#include "processing/framework/output_committer.h"

using namespace sicnu::data;

namespace
{

/// Resolve a fixture path relative to this source file (tests/ -> ../data).
QString fixturePath( const QString &relative )
{
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

/// Stages a copy of a fixture into dir under name, returning the absolute path.
QString stageFixture( QTemporaryDir &dir, const QString &fixture, const QString &name )
{
  const QString path = dir.filePath( name );
  REQUIRE( QFile::copy( fixturePath( fixture ), path ) );
  return path;
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

  // Make the file's parent directory non-writable so removal fails. On Unix,
  // removing a file requires write permission on its parent directory.
  const QDir parentDir( QFileInfo( path ).absolutePath() );
  REQUIRE( QFile::setPermissions( parentDir.absolutePath(),
            QFile::ReadOwner | QFile::ExeOwner ) );

  const ReapResult result = manager.reap( ReapRequest{ id } );

  // Restore permissions so QTemporaryDir can clean up.
  QFile::setPermissions( parentDir.absolutePath(),
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner );

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

  const SessionReapResult result = manager.reapSessionTemporaries();

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

  const SessionReapResult result = manager.reapSessionTemporaries();

  CHECK( result.reapedCount == 0 );
  CHECK( result.skippedLeased.isEmpty() );
  CHECK( manager.asset( id ).has_value() );
}

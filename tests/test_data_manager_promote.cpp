#include <catch2/catch_test_macros.hpp>

#include <QFileInfo>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/source_descriptor.h"

using namespace sicnu::data;

namespace
{

QString fixturePath( const QString &relative )
{
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

QString stageFixture( QTemporaryDir &dir, const QString &fixture, const QString &name )
{
  const QString path = dir.filePath( name );
  REQUIRE( QFile::copy( fixturePath( fixture ), path ) );
  return path;
}

AssetId registerTemporaryRaster( DataManager &manager,
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

TEST_CASE( "Promoting a temporary asset flips it to ProjectPersistent unchanged",
           "[data_manager][promote]" )
{
  QTemporaryDir dir;
  DataManager manager;
  QSignalSpy changedSpy( &manager, &DataManager::assetChanged );

  const QString path =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "scene.tif" ) );
  const AssetId id =
    registerTemporaryRaster( manager, path, PersistencePolicy::SessionTemporary );

  // Attach provenance so we can verify it survives promotion.
  DerivationRecord derivation;
  derivation.algorithmId = QStringLiteral( "sicnu:ndvi" );
  REQUIRE( manager.attachDerivationRecord( id, derivation ) );

  const auto before = manager.asset( id );
  REQUIRE( before.has_value() );
  REQUIRE( before->persistence() == PersistencePolicy::SessionTemporary );

  const Result<void> result = manager.promote( id );

  REQUIRE( result );
  CHECK( changedSpy.count() == 1 );
  CHECK( changedSpy.takeFirst().at( 0 ).value<AssetId>() == id );

  const auto after = manager.asset( id );
  REQUIRE( after.has_value() );
  CHECK( after->persistence() == PersistencePolicy::ProjectPersistent );
  // Identity, revision, source, structure, capabilities are unchanged.
  CHECK( after->id() == before->id() );
  CHECK( after->revision() == before->revision() );
  CHECK( after->source().canonicalSource == before->source().canonicalSource );
  CHECK( after->structure() == before->structure() );
  CHECK( after->capabilities() == before->capabilities() );
  // Provenance survives.
  const auto provenance = manager.provenance( id );
  REQUIRE( provenance.has_value() );
  CHECK( provenance->algorithmId == QStringLiteral( "sicnu:ndvi" ) );
}

TEST_CASE( "Promoting an already-persistent asset is a no-op success",
           "[data_manager][promote]" )
{
  QTemporaryDir dir;
  DataManager manager;
  QSignalSpy changedSpy( &manager, &DataManager::assetChanged );

  const QString path =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "persistent.tif" ) );
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  RegisterRequest request;
  request.source = source;
  request.persistence = PersistencePolicy::ProjectPersistent;
  const AssetId id = manager.registerSource( request ).assetId;
  REQUIRE( !id.isNull() );

  const Result<void> result = manager.promote( id );

  REQUIRE( result );
  CHECK( changedSpy.count() == 0 ); // no signal on no-op
  CHECK( manager.asset( id )->persistence() == PersistencePolicy::ProjectPersistent );
}

TEST_CASE( "Promoting a TaskTemporary asset flips it to ProjectPersistent",
           "[data_manager][promote]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString path =
    stageFixture( dir, QStringLiteral( "samples/dem_sample.tif" ),
                  QStringLiteral( "task.tif" ) );
  const AssetId id =
    registerTemporaryRaster( manager, path, PersistencePolicy::TaskTemporary );

  const Result<void> result = manager.promote( id );

  REQUIRE( result );
  CHECK( manager.asset( id )->persistence() == PersistencePolicy::ProjectPersistent );
}

TEST_CASE( "Promoting an unknown asset id is rejected",
           "[data_manager][promote]" )
{
  DataManager manager;

  const Result<void> result = manager.promote( AssetId::generate() );

  REQUIRE_FALSE( result );
  REQUIRE_FALSE( result.diagnostics().isEmpty() );
}

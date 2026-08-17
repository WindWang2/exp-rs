#include <catch2/catch_test_macros.hpp>

#include <QFileInfo>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>

#include <vector>

#include <gdal.h>
#include <cpl_conv.h>

#include "data/asset_types.h"
#include "data/collection_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/source_descriptor.h"

using namespace sicnu::data;

namespace
{

// Synthesise a small GeoTIFF once and cache it (plus its holding temp dir) for
// the lifetime of the process, so tests do not depend on a committed sample
// raster under data/samples/.
QString syntheticSample()
{
  static QTemporaryDir dir;
  static const QString cached = []() {
    GDALAllRegister();
    const QString path = dir.path() + QLatin1Char( '/' ) + QStringLiteral( "sample.tif" );
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
  }();
  return cached;
}

QString fixturePath( const QString &relative )
{
  // Sample rasters under data/samples/ are no longer committed; redirect those
  // (and the legacy phr_xs.tif) to the synthesised sample. Other paths resolve
  // to the real data tree as before.
  if ( relative.startsWith( QLatin1String( "samples/" ) ) ||
       relative == QLatin1String( "phr_xs.tif" ) )
  {
    return syntheticSample();
  }
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

AssetId registerRasterAsset( DataManager &manager, const QString &path )
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  RegisterRequest request;
  request.source = source;
  const RegisterResult result = manager.registerSource( request );
  REQUIRE( !result.assetId.isNull() );
  return result.assetId;
}

ProductMetadata sampleMetadata()
{
  ProductMetadata metadata;
  metadata.platform = QStringLiteral( "Sentinel-2" );
  metadata.sensor = QStringLiteral( "MSI" );
  metadata.productLevel = QStringLiteral( "L2A" );
  metadata.acquisitionDate = QStringLiteral( "2026-07-25" );
  metadata.processingLevel = QStringLiteral( "BOA" );
  metadata.attributes.insert( QStringLiteral( "tile" ), QStringLiteral( "T48RVT" ) );
  return metadata;
}

} // namespace

TEST_CASE( "A collection can be created and queried back",
           "[data_manager][collection]" )
{
  DataManager manager;
  QSignalSpy addedSpy( &manager, &DataManager::collectionAdded );

  const CollectionCreateRequest request{ QStringLiteral( "Sentinel-2 S2A scene" ),
                                         sampleMetadata() };
  const CollectionCreateResult result = manager.createCollection( request );

  REQUIRE( !result.collectionId.isNull() );
  CHECK( addedSpy.count() == 1 );
  CHECK( addedSpy.first().first().value<CollectionId>() == result.collectionId );

  const auto collection = manager.collection( result.collectionId );
  REQUIRE( collection.has_value() );
  CHECK( collection->displayName == QStringLiteral( "Sentinel-2 S2A scene" ) );
  CHECK( collection->metadata.platform == QStringLiteral( "Sentinel-2" ) );
  CHECK( collection->metadata.attributes.value( QStringLiteral( "tile" ) ) ==
         QStringLiteral( "T48RVT" ) );
  CHECK( collection->childAssetIds.isEmpty() );
}

TEST_CASE( "Child assets can be added to a collection and queried",
           "[data_manager][collection]" )
{
  QTemporaryDir dir;
  DataManager manager;

  // Stage two distinct rasters as child candidates.
  const QString pathA = dir.filePath( QStringLiteral( "a.tif" ) );
  const QString pathB = dir.filePath( QStringLiteral( "b.tif" ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), pathA ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), pathB ) );

  const AssetId childA = registerRasterAsset( manager, pathA );
  const AssetId childB = registerRasterAsset( manager, pathB );

  const CollectionId collectionId =
    manager.createCollection( { QStringLiteral( "scene" ), sampleMetadata() } ).collectionId;

  REQUIRE( manager.addChildToCollection( collectionId, childA ) );
  REQUIRE( manager.addChildToCollection( collectionId, childB ) );

  const auto collection = manager.collection( collectionId );
  REQUIRE( collection.has_value() );
  REQUIRE( collection->childAssetIds.size() == 2 );
  CHECK( collection->childAssetIds.first() == childA );
  CHECK( collection->childAssetIds.last() == childB );

  // Each child's record carries the parent collection id.
  CHECK( manager.asset( childA )->parentCollectionId() == collectionId );
  CHECK( manager.asset( childB )->parentCollectionId() == collectionId );
}

TEST_CASE( "A child asset is still a full Data Asset independent of its collection",
           "[data_manager][collection]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString path = dir.filePath( QStringLiteral( "child.tif" ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path ) );
  // Register the child as SessionTemporary so it is reapable.
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  RegisterRequest regRequest;
  regRequest.source = source;
  regRequest.persistence = PersistencePolicy::SessionTemporary;
  regRequest.additionalCapabilities = AssetCapability::DeletableSource;
  const AssetId child = manager.registerSource( regRequest ).assetId;

  const CollectionId collectionId =
    manager.createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( manager.addChildToCollection( collectionId, child ) );

  // The child can be leased like any asset.
  auto lease = manager
                 .acquire( AssetRef{ child, AssetRevision::initial() },
                           AssetUse{ LeaseKind::View, QStringLiteral( "viewer" ) } )
                 .take();
  REQUIRE( lease.isValid() );

  // The child can be promoted (becomes ProjectPersistent, survives the session).
  REQUIRE( manager.promote( child ) );
  CHECK( manager.asset( child )->persistence() == PersistencePolicy::ProjectPersistent );

  // The promoted child can be unloaded like any standalone persistent asset.
  ( void ) lease.release();
  const UnloadPlan plan = manager.planUnload( child ).confirmedCascade();
  REQUIRE( manager.unload( plan ) );
  CHECK_FALSE( manager.asset( child ).has_value() );

  // The collection still exists but no longer lists the unloaded child.
  const auto collection = manager.collection( collectionId );
  REQUIRE( collection.has_value() );
  CHECK( collection->childAssetIds.isEmpty() );
}

TEST_CASE( "Unloading a collection without cascade leaves children as standalone assets",
           "[data_manager][collection]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString path = dir.filePath( QStringLiteral( "child.tif" ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path ) );
  const AssetId child = registerRasterAsset( manager, path );

  const CollectionId collectionId =
    manager.createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( manager.addChildToCollection( collectionId, child ) );

  REQUIRE( manager.unloadCollection( collectionId, /*cascade=*/false ) );

  CHECK_FALSE( manager.collection( collectionId ).has_value() );
  // The child survives as a standalone asset, no longer bound to a collection.
  const auto childSnapshot = manager.asset( child );
  REQUIRE( childSnapshot.has_value() );
  CHECK( !childSnapshot->parentCollectionId().has_value() );
}

TEST_CASE( "Unloading a collection with cascade removes it and its children",
           "[data_manager][collection]" )
{
  QTemporaryDir dir;
  DataManager manager;
  QSignalSpy removedSpy( &manager, &DataManager::collectionRemoved );

  const QString pathA = dir.filePath( QStringLiteral( "a.tif" ) );
  const QString pathB = dir.filePath( QStringLiteral( "b.tif" ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), pathA ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), pathB ) );
  const AssetId childA = registerRasterAsset( manager, pathA );
  const AssetId childB = registerRasterAsset( manager, pathB );

  const CollectionId collectionId =
    manager.createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( manager.addChildToCollection( collectionId, childA ) );
  REQUIRE( manager.addChildToCollection( collectionId, childB ) );

  REQUIRE( manager.unloadCollection( collectionId, /*cascade=*/true ) );

  CHECK_FALSE( manager.collection( collectionId ).has_value() );
  CHECK_FALSE( manager.asset( childA ).has_value() );
  CHECK_FALSE( manager.asset( childB ).has_value() );
  CHECK( removedSpy.count() == 1 );
}

TEST_CASE( "A collection with a leased child cannot be cascade-unloaded without confirmation",
           "[data_manager][collection]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString path = dir.filePath( QStringLiteral( "child.tif" ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path ) );
  const AssetId child = registerRasterAsset( manager, path );

  const CollectionId collectionId =
    manager.createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( manager.addChildToCollection( collectionId, child ) );

  auto lease = manager
                 .acquire( AssetRef{ child, AssetRevision::initial() },
                           AssetUse{ LeaseKind::View, QStringLiteral( "viewer" ) } )
                 .take();
  REQUIRE( lease.isValid() );

  // Cascade unload is refused while a child holds a lease.
  const Result<void> result = manager.unloadCollection( collectionId, /*cascade=*/true );
  REQUIRE_FALSE( result );
  // Nothing was removed.
  CHECK( manager.collection( collectionId ).has_value() );
  CHECK( manager.asset( child ).has_value() );

  ( void ) lease.release();
  // After release, the cascade unload succeeds.
  REQUIRE( manager.unloadCollection( collectionId, /*cascade=*/true ) );
  CHECK_FALSE( manager.collection( collectionId ).has_value() );
}

TEST_CASE( "Collections can be listed",
           "[data_manager][collection]" )
{
  DataManager manager;

  const CollectionId a =
    manager.createCollection( { QStringLiteral( "a" ), ProductMetadata() } ).collectionId;
  const CollectionId b =
    manager.createCollection( { QStringLiteral( "b" ), ProductMetadata() } ).collectionId;

  const QVector<CollectionId> all = manager.collections();
  REQUIRE( all.size() == 2 );
  CHECK( all.contains( a ) );
  CHECK( all.contains( b ) );
}

TEST_CASE( "A child cannot be added to two collections",
           "[data_manager][collection]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString path = dir.filePath( QStringLiteral( "child.tif" ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path ) );
  const AssetId child = registerRasterAsset( manager, path );

  const CollectionId a =
    manager.createCollection( { QStringLiteral( "a" ), ProductMetadata() } ).collectionId;
  const CollectionId b =
    manager.createCollection( { QStringLiteral( "b" ), ProductMetadata() } ).collectionId;
  REQUIRE( manager.addChildToCollection( a, child ) );

  // Adding the child to a second collection is refused (flat one-level pointer).
  const Result<void> result = manager.addChildToCollection( b, child );
  REQUIRE_FALSE( result );

  // The child stays in the first collection, not cross-listed.
  CHECK( manager.asset( child )->parentCollectionId() == a );
  CHECK( manager.collection( a )->childAssetIds.size() == 1 );
  CHECK( manager.collection( b )->childAssetIds.isEmpty() );
}

TEST_CASE( "An independently unloaded child is pruned from its collection's list",
           "[data_manager][collection]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString path = dir.filePath( QStringLiteral( "child.tif" ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path ) );
  const AssetId child = registerRasterAsset( manager, path );

  const CollectionId collectionId =
    manager.createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( manager.addChildToCollection( collectionId, child ) );
  REQUIRE( manager.collection( collectionId )->childAssetIds.size() == 1 );

  // Unload the child directly (not via collection cascade).
  const UnloadPlan plan = manager.planUnload( child ).confirmedCascade();
  REQUIRE( manager.unload( plan ) );

  // The collection no longer lists the removed child (eager pruning, not just
  // lazy read-filtering - the persisted list is clean for serialization).
  const auto collection = manager.collection( collectionId );
  REQUIRE( collection.has_value() );
  CHECK( collection->childAssetIds.isEmpty() );
}

TEST_CASE( "Asset mutations preserve parentCollectionId (relocate, promote, commitEdit)",
           "[data_manager][collection][mutation]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const QString path1 = dir.filePath( QStringLiteral( "child1.tif" ) );
  const QString path2 = dir.filePath( QStringLiteral( "child2.tif" ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path1 ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path2 ) );

  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path1;
  RegisterRequest regRequest;
  regRequest.source = source;
  regRequest.persistence = PersistencePolicy::SessionTemporary;
  const RegisterResult regResult = manager.registerSource( regRequest );
  REQUIRE( !regResult.assetId.isNull() );
  const AssetId child = regResult.assetId;

  const CollectionId collectionId =
    manager.createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( manager.addChildToCollection( collectionId, child ) );
  REQUIRE( manager.asset( child )->parentCollectionId() == collectionId );

  // 1. promote preserves parentCollectionId
  REQUIRE( manager.promote( child ) );
  CHECK( manager.asset( child )->parentCollectionId() == collectionId );

  // 2. edit lease + commitEdit preserves parentCollectionId
  AssetLease editLease = manager.acquire( AssetRef{ child }, AssetUse{ LeaseKind::Edit } ).take();
  REQUIRE( manager.commitEdit( child ) );
  CHECK( manager.asset( child )->parentCollectionId() == collectionId );

  // 3. relocate preserves parentCollectionId
  SourceDescriptor repSource;
  repSource.providerKey = QStringLiteral( "gdal" );
  repSource.canonicalSource = path2;
  const auto relocateResult = manager.relocate( { child, repSource } );
  REQUIRE( relocateResult );
  CHECK( manager.asset( child )->parentCollectionId() == collectionId );
}


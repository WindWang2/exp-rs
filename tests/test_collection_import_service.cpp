// test_collection_import_service.cpp - read-only collection-import probe
//
// Drives the CollectionImportService probe with a stub ProductDiscoverer so
// the probe->preview shape (grid splitting, purity, no catalog mutation) is
// tested without real satellite data. A final case wires the real
// SatelliteProductsDiscoverer against a synthetic Sentinel-2 SAFE product.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>

#include <array>
#include <vector>

#include <gdal.h>
#include <gdal_priv.h>

#include "data/asset_types.h"
#include "data/collection_types.h"
#include "data/data_manager.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/framework/collection_import_service.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using sicnu::data::AssetCapabilities;
using sicnu::data::AssetCapability;
using sicnu::data::AssetId;
using sicnu::data::AssetKind;
using sicnu::data::AssetRef;
using sicnu::data::AssetRevision;
using sicnu::data::AssetSnapshot;
using sicnu::data::AssetUse;
using sicnu::data::DataManager;
using sicnu::data::LeaseKind;
using sicnu::data::PersistencePolicy;
using sicnu::data::ProductMetadata;
using sicnu::data::RegisterRequest;
using sicnu::data::SourceDescriptor;
using sicnu::data::UnloadPlan;
using sicnu::ChildBandInfo;
using sicnu::ChildCandidate;
using sicnu::CollectionImportService;
using sicnu::CommitImportRequest;
using sicnu::CommitImportResult;
using sicnu::DiscoveredGridGroup;
using sicnu::DiscoveredProduct;
using sicnu::ImportPreview;
using sicnu::ProductDiscoverer;
using sicnu::SatelliteProductsDiscoverer;
using sicnu::data::Result;

namespace
{

/// A canned discoverer whose output is set per-test. Records the source it was
/// called with so the probe forwards the source verbatim.
class StubDiscoverer : public ProductDiscoverer
{
  public:
    /// When non-empty, `discover` returns this failure instead of the product.
    QString failMessage;
    /// The product to return on success (ignored when failMessage is set).
    DiscoveredProduct product;
    /// Sources this discoverer was asked to discover, in call order.
    QStringList probedSources;

    Result<DiscoveredProduct> discover( const QString &source ) override
    {
      probedSources.append( source );
      if ( !failMessage.isEmpty() )
      {
        return Result<DiscoveredProduct>::failure(
          { QStringLiteral( "stub.discover_failed" ), failMessage } );
      }
      return Result<DiscoveredProduct>::success( product );
    }
};

DiscoveredGridGroup gridGroup( const QString &label,
                               const QString &sourcePath,
                               const QStringList &bandNames )
{
  DiscoveredGridGroup group;
  group.gridLabel = label;
  group.displayName = label + QStringLiteral( " group" );
  group.sourcePath = sourcePath;
  for ( const QString &name : bandNames )
  {
    SatelliteProducts::BandFile band;
    band.name = name;
    band.path = sourcePath;
    band.sourceBand = bandNames.indexOf( name ) + 1;
    group.bands.append( band );
  }
  return group;
}

DiscoveredProduct sentinelLikeProduct( const QVector<DiscoveredGridGroup> &groups )
{
  DiscoveredProduct product;
  product.productId = QStringLiteral( "S2A_MSIL2A_TEST" );
  product.spacecraft = QStringLiteral( "Sentinel-2A" );
  product.processingLevel = QStringLiteral( "L2A" );
  product.acquisitionDate = QStringLiteral( "2026-07-25" );
  product.attributes.insert( QStringLiteral( "tile" ), QStringLiteral( "T48RVT" ) );
  product.gridGroups = groups;
  return product;
}

/// Synthesise a small GeoTIFF once and cache it (plus its holding temp dir) so
/// tests do not depend on a committed sample raster under data/samples/.
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
      (void) GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return path;
  }();
  return cached;
}

/// Resolve a fixture path relative to this source file (tests/ -> ../data).
/// Sample rasters under data/samples/ are no longer committed and resolve to a
/// synthesised GeoTIFF instead.
QString fixturePath( const QString &relative )
{
  if ( relative.startsWith( QLatin1String( "samples/" ) ) ||
       relative == QLatin1String( "phr_xs.tif" ) )
  {
    return syntheticSample();
  }
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

/// Stage a distinct copy of the DEM fixture into the temp dir so two children
/// get distinct source paths (the Data Manager dedups by SourceKey).
QString stageRaster( QTemporaryDir &dir, const QString &name )
{
  const QString path = dir.filePath( name );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ), path ) );
  return path;
}

/// Build a ChildCandidate pointing at `sourcePath` with one band. The band's
/// `sourcePath` is what the commit registers; `kind` defaults to Raster.
ChildCandidate rasterChild( const QString &sourcePath, const QString &displayName = {} )
{
  ChildCandidate candidate;
  candidate.kind = AssetKind::Raster;
  candidate.displayName = displayName.isEmpty() ? sourcePath : displayName;
  candidate.gridLabel = QStringLiteral( "default" );
  candidate.sourcePath = sourcePath;
  ChildBandInfo band;
  band.name = QStringLiteral( "B1" );
  band.sourcePath = sourcePath;
  band.sourceBand = 1;
  candidate.bands.append( band );
  return candidate;
}

/// Build an ImportPreview over the given children, with placeholder metadata.
ImportPreview previewOver( const QVector<ChildCandidate> &children )
{
  ImportPreview preview;
  preview.collectionDisplayName = QStringLiteral( "test-product" );
  preview.metadata.platform = QStringLiteral( "Sentinel-2A" );
  preview.metadata.processingLevel = QStringLiteral( "L2A" );
  preview.children = children;
  return preview;
}

} // namespace

TEST_CASE( "Probe returns a preview and registers nothing in the catalog",
           "[collection_import][probe]" )
{
  DataManager manager;
  StubDiscoverer discoverer;
  discoverer.product =
    sentinelLikeProduct( { gridGroup( QStringLiteral( "10m" ),
                                      QStringLiteral( "/fake/safe/R10m" ),
                                      { QStringLiteral( "B2" ), QStringLiteral( "B3" ),
                                        QStringLiteral( "B4" ), QStringLiteral( "B8" ) } ) } );
  CollectionImportService service( &manager, &discoverer );

  REQUIRE( manager.assets().isEmpty() );
  REQUIRE( manager.collections().isEmpty() );

  const Result<ImportPreview> result = service.probe( QStringLiteral( "/fake/safe" ) );

  REQUIRE( result );
  REQUIRE( result.value().children.size() == 1 );
  // No catalog mutation: the probe is read-only by construction.
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
  // The discoverer was forwarded the source verbatim.
  REQUIRE( discoverer.probedSources.size() == 1 );
  CHECK( discoverer.probedSources.first() == QStringLiteral( "/fake/safe" ) );
}

TEST_CASE( "Probing the same source twice yields equal previews and changes nothing",
           "[collection_import][probe]" )
{
  DataManager manager;
  StubDiscoverer discoverer;
  discoverer.product =
    sentinelLikeProduct( { gridGroup( QStringLiteral( "10m" ),
                                      QStringLiteral( "/fake/R10m" ),
                                      { QStringLiteral( "B2" ), QStringLiteral( "B4" ) } ) } );
  CollectionImportService service( &manager, &discoverer );

  const Result<ImportPreview> first = service.probe( QStringLiteral( "/fake/safe" ) );
  const Result<ImportPreview> second = service.probe( QStringLiteral( "/fake/safe" ) );

  REQUIRE( first );
  REQUIRE( second );
  // Equal value shapes - purity: the probe is a pure function of the discoverer.
  CHECK( first.value() == second.value() );
  // The discoverer was called twice (the probe does not cache), and both calls
  // were forwarded the same source.
  CHECK( discoverer.probedSources.size() == 2 );
  // A probe followed by no commit leaves the catalog untouched.
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
}

TEST_CASE( "Distinct grids produce distinct child candidates, never merged",
           "[collection_import][probe]" )
{
  DataManager manager;
  StubDiscoverer discoverer;
  discoverer.product = sentinelLikeProduct(
    { gridGroup( QStringLiteral( "10m" ), QStringLiteral( "/fake/R10m" ),
                 { QStringLiteral( "B2" ), QStringLiteral( "B4" ) } ),
      gridGroup( QStringLiteral( "20m" ), QStringLiteral( "/fake/R20m" ),
                 { QStringLiteral( "B5" ), QStringLiteral( "B6" ) } ) } );
  CollectionImportService service( &manager, &discoverer );

  const Result<ImportPreview> result = service.probe( QStringLiteral( "/fake/safe" ) );

  REQUIRE( result );
  const ImportPreview &preview = result.value();
  REQUIRE( preview.children.size() == 2 );
  CHECK( preview.children[0].gridLabel == QStringLiteral( "10m" ) );
  CHECK( preview.children[1].gridLabel == QStringLiteral( "20m" ) );
  CHECK( preview.children[0].sourcePath == QStringLiteral( "/fake/R10m" ) );
  CHECK( preview.children[1].sourcePath == QStringLiteral( "/fake/R20m" ) );
  // Each candidate carries its own bands, not a concatenation.
  REQUIRE( preview.children[0].bands.size() == 2 );
  CHECK( preview.children[0].bands[0].name == QStringLiteral( "B2" ) );
  CHECK( preview.children[1].bands[1].name == QStringLiteral( "B6" ) );
}

TEST_CASE( "A failed discoverer propagates failure diagnostics and registers nothing",
           "[collection_import][probe]" )
{
  DataManager manager;
  StubDiscoverer discoverer;
  discoverer.failMessage = QStringLiteral( "unrecognized product" );
  CollectionImportService service( &manager, &discoverer );

  const Result<ImportPreview> result = service.probe( QStringLiteral( "/bogus" ) );

  REQUIRE_FALSE( result );
  REQUIRE_FALSE( result.diagnostics().isEmpty() );
  // The discoverer's diagnostics are forwarded verbatim (not overwritten),
  // so the stub's own code surfaces - the probe adds no synthesized diagnostic
  // when the discoverer already provided one.
  CHECK( result.diagnostics().first().code == QStringLiteral( "stub.discover_failed" ) );
  CHECK( result.diagnostics().first().message == QStringLiteral( "unrecognized product" ) );
  // The probe never touched the catalog.
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
}

TEST_CASE( "Provider metadata is normalized into ProductMetadata, not exposed raw",
           "[collection_import][probe]" )
{
  DataManager manager;
  StubDiscoverer discoverer;
  discoverer.product =
    sentinelLikeProduct( { gridGroup( QStringLiteral( "10m" ),
                                      QStringLiteral( "/fake/R10m" ),
                                      { QStringLiteral( "B4" ) } ) } );
  CollectionImportService service( &manager, &discoverer );

  const Result<ImportPreview> result = service.probe( QStringLiteral( "/fake/safe" ) );
  REQUIRE( result );
  const ImportPreview &preview = result.value();

  // The DiscoveredProduct fields were mapped into normalized ProductMetadata.
  CHECK( preview.metadata.platform == QStringLiteral( "Sentinel-2A" ) );
  CHECK( preview.metadata.sensor == QStringLiteral( "Sentinel-2A" ) );
  CHECK( preview.metadata.processingLevel == QStringLiteral( "L2A" ) );
  CHECK( preview.metadata.acquisitionDate == QStringLiteral( "2026-07-25" ) );
  CHECK( preview.metadata.attributes.value( QStringLiteral( "tile" ) ) ==
         QStringLiteral( "T48RVT" ) );
  // The collection display name falls back to the product id.
  CHECK( preview.collectionDisplayName == QStringLiteral( "S2A_MSIL2A_TEST" ) );

  // The band files were normalized into ChildBandInfo (not raw BandFile).
  REQUIRE( preview.children.size() == 1 );
  REQUIRE( preview.children[0].bands.size() == 1 );
  const ChildBandInfo &band = preview.children[0].bands[0];
  CHECK( band.name == QStringLiteral( "B4" ) );
  CHECK( band.sourcePath == QStringLiteral( "/fake/R10m" ) );
  CHECK( band.sourceBand == 1 );
}

// --- Integration: the real SatelliteProducts discoverer adapter ---

namespace
{

int &appArgc()
{
  static int argc = 1;
  return argc;
}
char appArgv0[] = "test_collection_import_service";
char *appArgv[] = { appArgv0, nullptr };

void ensureApp()
{
  if ( !QCoreApplication::instance() )
    new QCoreApplication( appArgc(), appArgv );
}

void writeTinyBand( const QString &path, float fill )
{
  ensureGdalInit();
  std::array<double, 6> gt = { 500000, 10, 0, 4500000, 0, -10 };
  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4 * 4, fill ) );
  QString err;
  REQUIRE( writeGdalOutput( path, 4, 4, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );
}

/// Minimal synthetic Sentinel-2 L2A SAFE product, mirroring the pattern in
/// test_satellite_products.cpp. Produces a 10 m band group discoverable by
/// SatelliteProducts::discoverSentinel2.
QString writeFakeSentinel2Safe( const QDir &root )
{
  const QString safe = root.filePath( QStringLiteral(
    "S2A_MSIL2A_20200615T000000_N9999_R000_T32TQQ_20200615T000000.SAFE" ) );
  const QString img = safe + QStringLiteral( "/GRANULE/L2A_T32TQQ/IMG_DATA/R10m" );
  QDir().mkpath( img );

  QFile mtd( QDir( safe ).filePath( QStringLiteral( "MTD_MSIL2A.xml" ) ) );
  REQUIRE( mtd.open( QIODevice::WriteOnly | QIODevice::Text ) );
  mtd.write( "<n1:Level-2A_User_Product></n1:Level-2A_User_Product>\n" );
  mtd.close();

  writeTinyBand( img + QStringLiteral( "/T32TQQ_20200615T000000_B02_10m.tif" ), 50.f );
  writeTinyBand( img + QStringLiteral( "/T32TQQ_20200615T000000_B03_10m.tif" ), 60.f );
  writeTinyBand( img + QStringLiteral( "/T32TQQ_20200615T000000_B04_10m.tif" ), 40.f );
  writeTinyBand( img + QStringLiteral( "/T32TQQ_20200615T000000_B08_10m.tif" ), 180.f );
  return safe;
}

} // namespace

TEST_CASE( "Real SatelliteProducts discoverer is wired and maps a SAFE product to a preview",
           "[collection_import][probe][integration]" )
{
  ensureApp();
  QTemporaryDir dir;
  const QString safe = writeFakeSentinel2Safe( QDir( dir.path() ) );

  DataManager manager;
  SatelliteProductsDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  const Result<ImportPreview> result = service.probe( safe );

  REQUIRE( result );
  const ImportPreview &preview = result.value();
  // discoverProduct prefers 10m, so the four R10m band files are discovered.
  // Each band lives in its own file, so each becomes a DISTINCT child candidate
  // (band-by-band selection; bands in one file would share a candidate). All
  // four children carry the 10m grid label.
  REQUIRE( preview.children.size() == 4 );
  for ( const ChildCandidate &child : preview.children )
  {
    CHECK( child.gridLabel == QStringLiteral( "10m" ) );
    CHECK_FALSE( child.bands.isEmpty() );
  }
  // Each 10 m child is a single band; the semantic role discovered for the band
  // is carried into the preview.
  for ( const ChildCandidate &child : preview.children )
  {
    REQUIRE( child.bands.size() == 1 );
    const ChildBandInfo &band = child.bands.first();
    if ( band.name == QStringLiteral( "B2" ) )
      CHECK( band.role == sicnu::data::BandRole::Blue );
    if ( band.name == QStringLiteral( "B4" ) )
      CHECK( band.role == sicnu::data::BandRole::Red );
    if ( band.name == QStringLiteral( "B8" ) )
      CHECK( band.role == sicnu::data::BandRole::NIR );
  }
  CHECK( preview.metadata.platform.contains( QStringLiteral( "Sentinel-2" ) ) );
  CHECK( preview.metadata.processingLevel == QStringLiteral( "L2A" ) );
  // The integration probe is still read-only.
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
}

// --- Atomic commit transaction (#52) ---
//
// The commit cases use the default DataManager (real GDAL/OGR providers) so
// registerSource resolves real staged rasters. A mid-commit failure is forced
// by pointing a child at an unresolvable source path.

TEST_CASE( "Commit registers one collection and one child asset per selection",
           "[collection_import][commit]" )
{
  QTemporaryDir dir;
  DataManager manager;
  QSignalSpy collectionSpy( &manager, &DataManager::collectionAdded );
  QSignalSpy assetSpy( &manager, &DataManager::assetAdded );
  StubDiscoverer discoverer; // unused by commit; required by the constructor
  CollectionImportService service( &manager, &discoverer );

  const QString pathA = stageRaster( dir, QStringLiteral( "a.tif" ) );
  const QString pathB = stageRaster( dir, QStringLiteral( "b.tif" ) );
  const ImportPreview preview =
    previewOver( { rasterChild( pathA, QStringLiteral( "10m" ) ),
                   rasterChild( pathB, QStringLiteral( "20m" ) ) } );

  const CommitImportResult result =
    service.commit( { preview, { 0, 1 }, PersistencePolicy::ProjectPersistent } );

  REQUIRE( !result.collectionId.isNull() );
  REQUIRE( result.childAssetIds.size() == 2 );
  CHECK( collectionSpy.count() == 1 );
  CHECK( assetSpy.count() == 2 );
  // Each child carries the collection as its parent.
  for ( const AssetId &childId : result.childAssetIds )
  {
    const auto snapshot = manager.asset( childId );
    REQUIRE( snapshot.has_value() );
    CHECK( snapshot->parentCollectionId() == result.collectionId );
  }
  // The collection lists both children in selection order.
  const auto collection = manager.collection( result.collectionId );
  REQUIRE( collection.has_value() );
  REQUIRE( collection->childAssetIds.size() == 2 );
  CHECK( collection->childAssetIds == result.childAssetIds );
}

// The product's acquisition date (e.g. a STAC item datetime carried through
// the preview's metadata) lands on each committed child asset's acquisition
// time. A preview without one leaves the children empty. Covers #138.
TEST_CASE( "Commit lands the preview acquisition date on each child asset",
           "[collection_import][commit]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer; // unused by commit
  CollectionImportService service( &manager, &discoverer );

  const QString pathA = stageRaster( dir, QStringLiteral( "a.tif" ) );
  const QString pathB = stageRaster( dir, QStringLiteral( "b.tif" ) );
  ImportPreview preview =
    previewOver( { rasterChild( pathA, QStringLiteral( "10m" ) ),
                   rasterChild( pathB, QStringLiteral( "20m" ) ) } );
  preview.metadata.acquisitionDate = QStringLiteral( "2026-07-25" );

  const CommitImportResult result =
    service.commit( { preview, { 0, 1 }, PersistencePolicy::ProjectPersistent } );
  REQUIRE( result.childAssetIds.size() == 2 );

  // Each child inherits the parsed date; the date-only ISO string parses to
  // midnight on that day.
  const QDateTime expected( QDate( 2026, 7, 25 ), QTime( 0, 0, 0 ) );
  for ( const AssetId &childId : result.childAssetIds )
  {
    const auto snapshot = manager.asset( childId );
    REQUIRE( snapshot.has_value() );
    REQUIRE( snapshot->acquisitionTime().has_value() );
    CHECK( snapshot->acquisitionTime()->date() == expected.date() );
  }

  // A preview without an acquisition date leaves the children empty (the plain,
  // non-STAC import shape): no default-epoch poisoning.
  QTemporaryDir dir2;
  DataManager manager2;
  CollectionImportService service2( &manager2, &discoverer );
  const QString pathC = stageRaster( dir2, QStringLiteral( "c.tif" ) );
  const ImportPreview barePreview =
    previewOver( { rasterChild( pathC, QStringLiteral( "10m" ) ) } );
  const CommitImportResult bareResult =
    service2.commit( { barePreview, { 0 }, PersistencePolicy::ProjectPersistent } );
  REQUIRE( bareResult.childAssetIds.size() == 1 );
  const auto bareSnapshot = manager2.asset( bareResult.childAssetIds.first() );
  REQUIRE( bareSnapshot.has_value() );
  CHECK_FALSE( bareSnapshot->acquisitionTime().has_value() );

  // A present-but-non-ISO date (e.g. a MODIS DOY date like "2020-DOY314") does
  // not parse: the children import with an empty acquisition time (no epoch
  // poisoning) and the commit surfaces a Warning diagnostic.
  QTemporaryDir dir3;
  DataManager manager3;
  CollectionImportService service3( &manager3, &discoverer );
  const QString pathD = stageRaster( dir3, QStringLiteral( "d.tif" ) );
  ImportPreview modisPreview =
    previewOver( { rasterChild( pathD, QStringLiteral( "250m" ) ) } );
  modisPreview.metadata.acquisitionDate = QStringLiteral( "2020-DOY314" );
  const CommitImportResult modisResult =
    service3.commit( { modisPreview, { 0 }, PersistencePolicy::ProjectPersistent } );
  REQUIRE( modisResult.childAssetIds.size() == 1 );
  const auto modisSnapshot = manager3.asset( modisResult.childAssetIds.first() );
  REQUIRE( modisSnapshot.has_value() );
  CHECK_FALSE( modisSnapshot->acquisitionTime().has_value() );
  REQUIRE_FALSE( modisResult.diagnostics.isEmpty() );
  CHECK( modisResult.diagnostics.last().code ==
         QStringLiteral( "import.acquisition_date_unparseable" ) );
}

TEST_CASE( "A mid-commit child registration failure rolls back the whole import",
           "[collection_import][commit][rollback]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  const QString validPath = stageRaster( dir, QStringLiteral( "valid.tif" ) );
  // Child 1 points at a source no registered provider supports, so
  // registerSource genuinely fails (source.unsupported), triggering rollback.
  // (A .tif path that merely doesn't resolve would register as a Missing
  // asset, not fail - the provider is intentionally lenient about missing
  // sources. An unsupported extension is the true registerSource failure.)
  const ImportPreview preview =
    previewOver( { rasterChild( validPath, QStringLiteral( "10m" ) ),
                   rasterChild( QStringLiteral( "/nonexistent/bogus.xyz123" ),
                                QStringLiteral( "20m" ) ) } );

  const CommitImportResult result =
    service.commit( { preview, { 0, 1 }, PersistencePolicy::ProjectPersistent } );

  // All-or-nothing: the failed second child rolls back the collection and the
  // first child. The catalog is fully clean - no half-imported product.
  CHECK( result.collectionId.isNull() );
  CHECK( result.childAssetIds.isEmpty() );
  REQUIRE_FALSE( result.diagnostics.isEmpty() );
  CHECK( result.diagnostics.first().code ==
         QStringLiteral( "source.unsupported" ) );
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
}

TEST_CASE( "Selecting a subset of children registers only those children",
           "[collection_import][commit][subset]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  const QString pathA = stageRaster( dir, QStringLiteral( "a.tif" ) );
  const QString pathB = stageRaster( dir, QStringLiteral( "b.tif" ) );
  const QString pathC = stageRaster( dir, QStringLiteral( "c.tif" ) );
  const ImportPreview preview =
    previewOver( { rasterChild( pathA, QStringLiteral( "10m" ) ),
                   rasterChild( pathB, QStringLiteral( "20m" ) ),
                   rasterChild( pathC, QStringLiteral( "60m" ) ) } );

  // Select only the 10m and 60m groups (indices 0 and 2); skip the 20m group.
  const CommitImportResult result = service.commit( { preview, { 0, 2 } } );

  REQUIRE( !result.collectionId.isNull() );
  REQUIRE( result.childAssetIds.size() == 2 );
  // The unselected middle child is absent from the catalog.
  const QVector<AssetSnapshot> assets = manager.assets();
  REQUIRE( assets.size() == 2 );
  for ( const AssetSnapshot &asset : assets )
  {
    CHECK( asset.source().canonicalSource != pathB );
    CHECK( asset.parentCollectionId() == result.collectionId );
  }
}

TEST_CASE( "Committed children are full Data Assets (lease, promote, unload)",
           "[collection_import][commit][full_asset]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  const QString pathA = stageRaster( dir, QStringLiteral( "a.tif" ) );
  const QString pathB = stageRaster( dir, QStringLiteral( "b.tif" ) );
  const ImportPreview preview =
    previewOver( { rasterChild( pathA, QStringLiteral( "10m" ) ),
                   rasterChild( pathB, QStringLiteral( "20m" ) ) } );

  // Register as SessionTemporary so the promote assertion below is meaningful
  // (promoting an already-persistent asset is a no-op).
  const CommitImportResult result =
    service.commit( { preview, { 0, 1 }, PersistencePolicy::SessionTemporary } );
  REQUIRE( !result.collectionId.isNull() );
  REQUIRE( result.childAssetIds.size() == 2 );
  const AssetId childA = result.childAssetIds[0];
  const AssetId childB = result.childAssetIds[1];

  // The child can be leased like any asset.
  auto lease = manager
                 .acquire( AssetRef{ childA, AssetRevision::initial() },
                           AssetUse{ LeaseKind::View, QStringLiteral( "viewer" ) } )
                 .take();
  REQUIRE( lease.isValid() );

  // The child can be promoted (becomes ProjectPersistent, survives the session).
  REQUIRE( manager.promote( childB ) );
  CHECK( manager.asset( childB )->persistence() == PersistencePolicy::ProjectPersistent );

  // The leased child can be unloaded once the lease is released, like any
  // standalone asset. Unloading it prunes it from the collection's list.
  ( void ) lease.release();
  const UnloadPlan plan = manager.planUnload( childA ).confirmedCascade();
  REQUIRE( manager.unload( plan ) );
  CHECK_FALSE( manager.asset( childA ).has_value() );

  const auto collection = manager.collection( result.collectionId );
  REQUIRE( collection.has_value() );
  REQUIRE( collection->childAssetIds.size() == 1 );
  CHECK( collection->childAssetIds.first() == childB );
}

TEST_CASE( "Committing the same product twice creates two distinct collections",
           "[collection_import][commit][dedup]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  // Two independent imports of the same logical product - the product arrives
  // in two directories (e.g. re-downloaded), so each commit registers its own
  // child asset. The transaction does NOT dedup collections across commits:
  // each commit creates its own collection with its own children. (A child can
  // belong to only one collection, so two collections of the same product must
  // hold distinct child assets - which they do here because the sources differ.)
  const QString path1 = stageRaster( dir, QStringLiteral( "import1.tif" ) );
  const QString path2 = stageRaster( dir, QStringLiteral( "import2.tif" ) );
  const ImportPreview preview1 = previewOver( { rasterChild( path1, QStringLiteral( "10m" ) ) } );
  const ImportPreview preview2 = previewOver( { rasterChild( path2, QStringLiteral( "10m" ) ) } );

  const CommitImportResult first = service.commit( { preview1, { 0 } } );
  const CommitImportResult second = service.commit( { preview2, { 0 } } );

  REQUIRE( !first.collectionId.isNull() );
  REQUIRE( !second.collectionId.isNull() );
  CHECK( first.collectionId != second.collectionId );
  CHECK( manager.collections().size() == 2 );
  CHECK( manager.assets().size() == 2 );
  // Each collection owns its own child.
  CHECK( manager.collection( first.collectionId )->childAssetIds.first() ==
         first.childAssetIds.first() );
  CHECK( manager.collection( second.collectionId )->childAssetIds.first() ==
         second.childAssetIds.first() );
}

TEST_CASE( "Two selections of the same source within one commit register one asset",
           "[collection_import][commit][dedup]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  // Per-child-source dedup WITHIN a commit: two children that resolve to the
  // same source register a single underlying asset, listed under the
  // collection once per selection.
  const QString path = stageRaster( dir, QStringLiteral( "shared.tif" ) );
  const ImportPreview preview =
    previewOver( { rasterChild( path, QStringLiteral( "10m" ) ),
                   rasterChild( path, QStringLiteral( "10m-copy" ) ) } );

  const CommitImportResult result = service.commit( { preview, { 0, 1 } } );

  REQUIRE( !result.collectionId.isNull() );
  // Both selections record the (single) deduped asset id, once each.
  REQUIRE( result.childAssetIds.size() == 2 );
  CHECK( result.childAssetIds[0] == result.childAssetIds[1] );
  // The catalog holds exactly one asset for the shared source.
  CHECK( manager.assets().size() == 1 );
  CHECK( manager.collection( result.collectionId )->childAssetIds.size() == 1 );
}

TEST_CASE( "An invalid selection registers nothing",
           "[collection_import][commit][validation]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  const QString path = stageRaster( dir, QStringLiteral( "a.tif" ) );
  const ImportPreview preview = previewOver( { rasterChild( path, QStringLiteral( "10m" ) ) } );

  // Out-of-range index.
  CommitImportResult result = service.commit( { preview, { 99 } } );
  CHECK( result.collectionId.isNull() );
  CHECK( result.childAssetIds.isEmpty() );
  REQUIRE_FALSE( result.diagnostics.isEmpty() );
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );

  // Duplicate index.
  result = service.commit( { preview, { 0, 0 } } );
  CHECK( result.collectionId.isNull() );
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
}

TEST_CASE( "A child already imported into another collection fails fast without touching it",
           "[collection_import][commit][ownership]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  // First commit imports the source into collection 1.
  const QString path = stageRaster( dir, QStringLiteral( "shared.tif" ) );
  const ImportPreview preview = previewOver( { rasterChild( path, QStringLiteral( "10m" ) ) } );
  const CommitImportResult first = service.commit( { preview, { 0 } } );
  REQUIRE( !first.collectionId.isNull() );
  REQUIRE( first.childAssetIds.size() == 1 );
  const AssetId firstChild = first.childAssetIds.first();

  // Second commit selects the SAME source (registerSource dedups to the same
  // asset, which already belongs to collection 1). A child can belong to only
  // one collection, so the second commit fails fast - and must NOT destroy the
  // first collection's child.
  const CommitImportResult second = service.commit( { preview, { 0 } } );
  CHECK( second.collectionId.isNull() );
  CHECK( second.childAssetIds.isEmpty() );
  REQUIRE_FALSE( second.diagnostics.isEmpty() );
  CHECK( second.diagnostics.first().code ==
         QStringLiteral( "import.child_in_other_collection" ) );

  // The first collection and its child are untouched by the failed second
  // commit (the ownership fix: rollback never unloads a reused asset).
  REQUIRE( manager.collection( first.collectionId ).has_value() );
  REQUIRE( manager.collection( first.collectionId )->childAssetIds.size() == 1 );
  const auto firstChildSnapshot = manager.asset( firstChild );
  REQUIRE( firstChildSnapshot.has_value() );
  CHECK( firstChildSnapshot->parentCollectionId() == first.collectionId );
  // No second collection was created.
  CHECK( manager.collections().size() == 1 );
  CHECK( manager.assets().size() == 1 );
}

TEST_CASE( "An adopted standalone asset survives a rollback as a standalone asset",
           "[collection_import][commit][ownership]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  // Pre-register a standalone asset (no collection) - simulating an asset
  // another component registered earlier.
  const QString path = stageRaster( dir, QStringLiteral( "standalone.tif" ) );
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  RegisterRequest regRequest;
  regRequest.source = source;
  const AssetId standalone = manager.registerSource( regRequest ).assetId;
  REQUIRE( !standalone.isNull() );
  REQUIRE( manager.assets().size() == 1 );

  // Commit a preview whose child 0 adopts the standalone asset (dedup hits it),
  // and whose child 1 is unresolvable, forcing a rollback.
  const ImportPreview preview =
    previewOver( { rasterChild( path, QStringLiteral( "10m" ) ),
                   rasterChild( QStringLiteral( "/nonexistent/bogus.xyz123" ),
                                QStringLiteral( "20m" ) ) } );

  const CommitImportResult result = service.commit( { preview, { 0, 1 } } );

  // The commit rolled back (child 1 failed).
  CHECK( result.collectionId.isNull() );
  // The standalone asset was merely unparented, NOT unloaded - it survives as a
  // standalone asset (owned by whichever component pre-registered it).
  const auto snapshot = manager.asset( standalone );
  REQUIRE( snapshot.has_value() );
  CHECK( !snapshot->parentCollectionId().has_value() );
  // The collection is gone; only the pre-existing standalone asset remains.
  CHECK( manager.collections().isEmpty() );
  CHECK( manager.assets().size() == 1 );
}

TEST_CASE( "CollectionImportService importCollection provides a one-step probe and commit seam",
           "[collection_import][import_collection]" )
{
  QTemporaryDir dir;
  DataManager manager;
  const QString path1 = stageRaster( dir, QStringLiteral( "band1.tif" ) );
  const QString path2 = stageRaster( dir, QStringLiteral( "band2.tif" ) );

  DiscoveredProduct product;
  product.spacecraft = QStringLiteral( "Sentinel-2" );
  product.processingLevel = QStringLiteral( "L2A" );

  DiscoveredGridGroup group;
  group.gridLabel = QStringLiteral( "10m" );
  group.displayName = QStringLiteral( "10m group" );
  group.sourcePath = path1;

  SatelliteProducts::BandFile band1;
  band1.name = QStringLiteral( "B04" );
  band1.path = path1;
  group.bands.append( band1 );

  SatelliteProducts::BandFile band2;
  band2.name = QStringLiteral( "B08" );
  band2.path = path2;
  group.bands.append( band2 );

  product.gridGroups.append( group );

  StubDiscoverer discoverer;
  discoverer.product = product;

  CollectionImportService service( &manager, &discoverer );

  SECTION( "One-step import automatically probes and commits all discovered children" )
  {
    auto res = service.importCollection( path1 );
    REQUIRE( res );
    const CommitImportResult commitRes = res.value();
    CHECK( !commitRes.collectionId.isNull() );
    CHECK( commitRes.childAssetIds.size() == 1 );

    CHECK( manager.collections().size() == 1 );
    CHECK( manager.assets().size() == 1 );
  }

  SECTION( "Failed discoverer propagates failure diagnostics cleanly" )
  {
    StubDiscoverer failDiscoverer;
    failDiscoverer.failMessage = QStringLiteral( "File not found" );

    CollectionImportService failService( &manager, &failDiscoverer );
    auto res = failService.importCollection( QStringLiteral( "/bogus/path.tif" ) );
    CHECK_FALSE( res );
    CHECK( manager.collections().isEmpty() );
    CHECK( manager.assets().isEmpty() );
  }

  SECTION( "autoLoad invokes pathOpener with the primary committed child path" )
  {
    QString openedPath;
    int openCount = 0;
    auto res = service.importCollection(
      path1,
      sicnu::data::PersistencePolicy::ProjectPersistent,
      /*autoLoad=*/true,
      [&]( const QString &path ) {
        openedPath = path;
        ++openCount;
      } );
    REQUIRE( res );
    REQUIRE( openCount == 1 );
    // Primary path is the registered asset's canonical source (Data/Display seam input).
    const auto &childIds = res.value().childAssetIds;
    REQUIRE( !childIds.isEmpty() );
    const auto snap = manager.asset( childIds.first() );
    REQUIRE( snap.has_value() );
    CHECK( openedPath == snap->source().canonicalSource );
    CHECK( openedPath == path1 );
  }

  SECTION( "autoLoad without pathOpener still commits and never opens" )
  {
    auto res = service.importCollection(
      path1,
      sicnu::data::PersistencePolicy::ProjectPersistent,
      /*autoLoad=*/true,
      nullptr );
    REQUIRE( res );
    CHECK( manager.collections().size() == 1 );
    CHECK( manager.assets().size() == 1 );
  }

  SECTION( "autoLoad false does not invoke pathOpener" )
  {
    int openCount = 0;
    auto res = service.importCollection(
      path1,
      sicnu::data::PersistencePolicy::ProjectPersistent,
      /*autoLoad=*/false,
      [&]( const QString & ) { ++openCount; } );
    REQUIRE( res );
    CHECK( openCount == 0 );
  }
}

TEST_CASE( "CollectionImportService zero-arg discoverer constructor uses default discoverer",
           "[collection_import][default_discoverer]" )
{
  ensureApp();
  QTemporaryDir dir;
  const QString safe = writeFakeSentinel2Safe( QDir( dir.path() ) );

  DataManager manager;
  CollectionImportService service( &manager ); // default discoverer

  const Result<ImportPreview> result = service.probe( safe );
  REQUIRE( result );
  const ImportPreview &preview = result.value();
  REQUIRE( preview.children.size() == 4 );
}


TEST_CASE( "Commit records the probed band roles and wavelengths on the child asset",
           "[collection_import][commit][band_metadata]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  const QString path = stageRaster( dir, QStringLiteral( "bands.tif" ) );
  ChildCandidate candidate;
  candidate.kind = AssetKind::Raster;
  candidate.displayName = QStringLiteral( "10m" );
  candidate.gridLabel = QStringLiteral( "10m" );
  candidate.sourcePath = path;
  ChildBandInfo red;
  red.name = QStringLiteral( "B4" );
  red.sourcePath = path;
  red.sourceBand = 1;
  red.wavelengthNm = 665;
  red.role = sicnu::data::BandRole::Red;
  ChildBandInfo nir;
  nir.name = QStringLiteral( "B8" );
  nir.sourcePath = path;
  nir.sourceBand = 2;
  nir.wavelengthNm = 833;
  nir.role = sicnu::data::BandRole::NIR;
  candidate.bands.append( red );
  candidate.bands.append( nir );

  const CommitImportResult result =
    service.commit( { previewOver( { candidate } ), { 0 }, PersistencePolicy::ProjectPersistent } );
  REQUIRE( result.childAssetIds.size() == 1 );

  const auto snapshot = manager.asset( result.childAssetIds.first() );
  REQUIRE( snapshot.has_value() );
  REQUIRE( snapshot->source().dataOptions.contains( QStringLiteral( "bandMetadata" ) ) );
  const QJsonArray bands = QJsonDocument::fromJson(
    snapshot->source().dataOptions.value( QStringLiteral( "bandMetadata" ) ).toUtf8() ).array();
  REQUIRE( bands.size() == 2 );
  CHECK( bands.at( 0 ).toObject().value( QStringLiteral( "name" ) ).toString() ==
         QStringLiteral( "B4" ) );
  CHECK( bands.at( 0 ).toObject().value( QStringLiteral( "role" ) ).toString() ==
         QStringLiteral( "red" ) );
  CHECK( bands.at( 0 ).toObject().value( QStringLiteral( "wavelengthNm" ) ).toInt() == 665 );
  CHECK( bands.at( 1 ).toObject().value( QStringLiteral( "role" ) ).toString() ==
         QStringLiteral( "nir" ) );
  CHECK( bands.at( 1 ).toObject().value( QStringLiteral( "wavelengthNm" ) ).toInt() == 833 );

  // A child whose discovery captured no role and no wavelength must NOT gain
  // the option: dataOptions participate in the SourceKey identity, and plain
  // registrations of the same file must keep deduplicating unchanged.
  const QString plainPath = stageRaster( dir, QStringLiteral( "plain.tif" ) );
  const CommitImportResult plainResult = service.commit(
    { previewOver( { rasterChild( plainPath, QStringLiteral( "20m" ) ) } ),
      { 0 },
      PersistencePolicy::ProjectPersistent } );
  REQUIRE( plainResult.childAssetIds.size() == 1 );
  const auto plainSnapshot = manager.asset( plainResult.childAssetIds.first() );
  REQUIRE( plainSnapshot.has_value() );
  CHECK_FALSE( plainSnapshot->source().dataOptions.contains( QStringLiteral( "bandMetadata" ) ) );
}

TEST_CASE( "Rollback failures are appended to the commit diagnostics, not discarded",
           "[collection_import][commit][rollback]" )
{
  QTemporaryDir dir;
  DataManager manager;
  StubDiscoverer discoverer;
  CollectionImportService service( &manager, &discoverer );

  const QString pathA = stageRaster( dir, QStringLiteral( "a.tif" ) );
  const QString pathB = stageRaster( dir, QStringLiteral( "b.tif" ) );
  const ImportPreview preview =
    previewOver( { rasterChild( pathA, QStringLiteral( "10m" ) ),
                   rasterChild( pathB, QStringLiteral( "20m" ) ),
                   rasterChild( QStringLiteral( "/nonexistent/bogus.xyz123" ),
                                QStringLiteral( "60m" ) ) } );

  CommitImportResult result;
  bool reapedDuringRollback = false;
  // Track what the commit registers; rollback unloads exactly those children
  // in order. When the FIRST created child is unloaded, reap the SECOND one
  // re-entrantly: the rollback's next unload then fails ("unload.unknown_asset").
  // Before the fix that failure was (void)-discarded; now it must ride in the
  // commit's diagnostics.
  QVector<AssetId> addedDuringCommit;
  QObject::connect( &manager, &DataManager::assetAdded,
                    [&]( AssetId id ) { addedDuringCommit.append( id ); } );
  QObject::connect( &manager, &DataManager::assetAboutToUnload, [&]( AssetId id ) {
    if ( reapedDuringRollback || addedDuringCommit.size() < 2 )
      return;
    if ( id == addedDuringCommit.at( 0 ) )
    {
      reapedDuringRollback = true;
      ( void ) manager.reap( { addedDuringCommit.at( 1 ) } );
    }
  } );

  result = service.commit( { preview, { 0, 1, 2 }, PersistencePolicy::ProjectPersistent } );

  CHECK( reapedDuringRollback );
  CHECK( result.collectionId.isNull() );
  // The provider's registration-failure diagnostics come first; the surfaced
  // rollback failure (previously discarded) rides last.
  REQUIRE_FALSE( result.diagnostics.isEmpty() );
  CHECK( result.diagnostics.last().code == QStringLiteral( "unload.unknown_asset" ) );
}

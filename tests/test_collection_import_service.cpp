// test_collection_import_service.cpp - read-only collection-import probe
//
// Drives the CollectionImportService probe with a stub ProductDiscoverer so
// the probe->preview shape (grid splitting, purity, no catalog mutation) is
// tested without real satellite data. A final case wires the real
// SatelliteProductsDiscoverer against a synthetic Sentinel-2 SAFE product.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

using sicnu::data::AssetKind;
using sicnu::data::DataManager;
using sicnu::data::ProductMetadata;
using sicnu::ChildBandInfo;
using sicnu::ChildCandidate;
using sicnu::CollectionImportService;
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
  // discoverProduct prefers 10m, so one grid group is produced. (Real multi-grid
  // extraction for L2A 20m/60m is a documented follow-up; this proves the wiring.)
  REQUIRE( preview.children.size() == 1 );
  CHECK( preview.children[0].gridLabel == QStringLiteral( "10m" ) );
  CHECK_FALSE( preview.children[0].bands.isEmpty() );
  CHECK( preview.metadata.platform.contains( QStringLiteral( "Sentinel-2" ) ) );
  CHECK( preview.metadata.processingLevel == QStringLiteral( "L2A" ) );
  // The integration probe is still read-only.
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
}

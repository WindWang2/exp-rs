// test_product_import_dialog.cpp - product probe-preview-commit dialog logic
//
// Drives the dialog's public probe/commitSelection logic headlessly (no
// exec()), staging synthetic Landsat and Sentinel-2 products and asserting the
// transaction: probe is read-only, commit registers the collection with
// selected children, and cancel registers nothing. The dialog is
// sensor-agnostic; the product family only shapes labels.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <array>
#include <vector>

#include <qgsapplication.h>

#include "app/dialogs/product_import_dialog.h"
#include "data/collection_types.h"
#include "data/data_manager.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using sicnu::data::AssetSnapshot;
using sicnu::data::CollectionId;
using sicnu::data::DataManager;

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_product_import_dialog";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

void writeTinyBand( const QString &path, float fill )
{
  ensureGdalInit();
  std::array<double, 6> gt = { 500000, 30, 0, 4500000, 0, -30 };
  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4 * 4, fill ) );
  QString err;
  REQUIRE( writeGdalOutput( path, 4, 4, bands, gt,
                            QStringLiteral( "EPSG:32648" ), &err ) );
}

/// Minimal synthetic Landsat Collection 2 scene, mirroring the pattern in
/// test_satellite_products.cpp. Returns the scene directory path.
QString writeFakeLandsatScene( const QDir &root )
{
  const QString scene = root.filePath( QStringLiteral( "LC08_L1TP_TEST" ) );
  QDir().mkpath( scene );
  QDir sdir( scene );

  QFile mtl( sdir.filePath( QStringLiteral( "LC08_L1TP_TEST_MTL.txt" ) ) );
  REQUIRE( mtl.open( QIODevice::WriteOnly | QIODevice::Text ) );
  QTextStream out( &mtl );
  out << "GROUP = LANDSAT_METADATA_FILE\n";
  out << "  SPACECRAFT_ID = \"LANDSAT_8\"\n";
  out << "  PROCESSING_LEVEL = \"L1TP\"\n";
  out << "  DATE_ACQUIRED = \"2020-06-15\"\n";
  out << "  LANDSAT_PRODUCT_ID = \"LC08_L1TP_TEST\"\n";
  out << "  FILE_NAME_BAND_2 = \"LC08_L1TP_TEST_B2.TIF\"\n";
  out << "  FILE_NAME_BAND_3 = \"LC08_L1TP_TEST_B3.TIF\"\n";
  out << "  FILE_NAME_BAND_4 = \"LC08_L1TP_TEST_B4.TIF\"\n";
  out << "  FILE_NAME_BAND_5 = \"LC08_L1TP_TEST_B5.TIF\"\n";
  out << "END_GROUP = LANDSAT_METADATA_FILE\n";
  out << "END\n";
  mtl.close();

  writeTinyBand( sdir.filePath( QStringLiteral( "LC08_L1TP_TEST_B2.TIF" ) ), 100.f );
  writeTinyBand( sdir.filePath( QStringLiteral( "LC08_L1TP_TEST_B3.TIF" ) ), 120.f );
  writeTinyBand( sdir.filePath( QStringLiteral( "LC08_L1TP_TEST_B4.TIF" ) ), 80.f );
  writeTinyBand( sdir.filePath( QStringLiteral( "LC08_L1TP_TEST_B5.TIF" ) ), 200.f );
  return scene;
}

/// Minimal synthetic Sentinel-2 L2A SAFE product. Returns the .SAFE path.
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

TEST_CASE( "Probing a Landsat directory fills the preview and registers nothing",
           "[product_import][probe]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString scene = writeFakeLandsatScene( QDir( dir.path() ) );

  DataManager manager;
  ProductImportDialog dialog;
  dialog.setDataManager( &manager );
  dialog.setSourcePath( scene );

  REQUIRE( dialog.probe() );
  // The probe is read-only: the preview shows the discovered child candidates
  // and the catalog is untouched.
  CHECK( dialog.previewCount() >= 1 );
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
}

TEST_CASE( "Importing selected bands registers a collection with the band children",
           "[product_import][commit]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString scene = writeFakeLandsatScene( QDir( dir.path() ) );

  DataManager manager;
  ProductImportDialog dialog;
  dialog.setDataManager( &manager );
  dialog.setSourcePath( scene );
  REQUIRE( dialog.probe() );
  // Each Landsat band lives in its own file, so each is a distinct selectable
  // child candidate (4 bands: B2, B3, B4, B5).
  REQUIRE( dialog.previewCount() == 4 );

  // All children checked (default): commit the whole preview.
  const CollectionId collectionId = dialog.commitSelection();

  REQUIRE( !collectionId.isNull() );
  // One collection; its children are ALL the selected band rasters (no
  // flattening, and no bands silently dropped - every scene band is imported).
  CHECK( manager.collections().size() == 1 );
  const auto collection = manager.collection( collectionId );
  REQUIRE( collection.has_value() );
  CHECK( collection->childAssetIds.size() == dialog.previewCount() );
  // Each child is a full Data Asset parented to the collection.
  for ( const auto &childId : collection->childAssetIds )
  {
    const std::optional<AssetSnapshot> snapshot = manager.asset( childId );
    REQUIRE( snapshot.has_value() );
    CHECK( snapshot->parentCollectionId() == collectionId );
  }
}

TEST_CASE( "A Sentinel-2 SAFE product probes and commits through the same dialog",
           "[product_import][probe][commit][sentinel2]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString safe = writeFakeSentinel2Safe( QDir( dir.path() ) );

  DataManager manager;
  ProductImportDialog dialog;
  dialog.setDataManager( &manager );
  dialog.setProductFamily( QStringLiteral( "sentinel2" ) );
  dialog.setSourcePath( safe );

  REQUIRE( dialog.probe() );
  // The 10 m band files are distinct children (band-by-band selection);
  // the product auto-detected regardless of the family label.
  REQUIRE( dialog.previewCount() == 4 );

  const CollectionId collectionId = dialog.commitSelection();
  REQUIRE( !collectionId.isNull() );
  CHECK( manager.collections().size() == 1 );
  const auto collection = manager.collection( collectionId );
  REQUIRE( collection.has_value() );
  CHECK( collection->childAssetIds.size() == 4 );
}

TEST_CASE( "A subset selection imports only the checked band groups",
           "[product_import][commit]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString scene = writeFakeLandsatScene( QDir( dir.path() ) );

  DataManager manager;
  ProductImportDialog dialog;
  dialog.setDataManager( &manager );
  dialog.setSourcePath( scene );
  REQUIRE( dialog.probe() );
  REQUIRE( dialog.previewCount() == 4 );

  // Select only the first two bands (B2, B3); uncheck the rest (B4, B5).
  dialog.setChildChecked( 2, false );
  dialog.setChildChecked( 3, false );

  const CollectionId collectionId = dialog.commitSelection();

  REQUIRE( !collectionId.isNull() );
  const auto collection = manager.collection( collectionId );
  REQUIRE( collection.has_value() );
  // Only the two checked bands were imported; the unchecked two are absent.
  CHECK( collection->childAssetIds.size() == 2 );
  CHECK( manager.assets().size() == 2 );
}

TEST_CASE( "Unchecking every band refuses the commit and registers nothing",
           "[product_import][commit]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString scene = writeFakeLandsatScene( QDir( dir.path() ) );

  DataManager manager;
  ProductImportDialog dialog;
  dialog.setDataManager( &manager );
  dialog.setSourcePath( scene );
  REQUIRE( dialog.probe() );
  REQUIRE( dialog.previewCount() == 4 );
  for ( int i = 0; i < dialog.previewCount(); ++i )
    dialog.setChildChecked( i, false );

  const CollectionId collectionId = dialog.commitSelection();

  CHECK( collectionId.isNull() );
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
}

TEST_CASE( "Cancelling after a probe registers nothing",
           "[product_import][cancel]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString scene = writeFakeLandsatScene( QDir( dir.path() ) );

  DataManager manager;
  {
    ProductImportDialog dialog;
    dialog.setDataManager( &manager );
    dialog.setSourcePath( scene );
    REQUIRE( dialog.probe() );
    REQUIRE( dialog.previewCount() >= 1 );
    // No commitSelection() call: the dialog is destroyed without Import, which
    // is the cancel path. The probe was read-only, so nothing was registered.
  }

  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
}

TEST_CASE( "Probing an invalid directory surfaces a diagnostic and registers nothing",
           "[product_import][probe]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;

  DataManager manager;
  ProductImportDialog dialog;
  dialog.setDataManager( &manager );
  dialog.setSourcePath( dir.filePath( QStringLiteral( "not-a-scene" ) ) );

  CHECK_FALSE( dialog.probe() );
  CHECK_FALSE( dialog.lastError().isEmpty() );
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.collections().isEmpty() );
}

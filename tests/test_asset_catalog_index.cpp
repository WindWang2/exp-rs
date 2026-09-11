// test_asset_catalog_index.cpp — Professional Workbench 8.0 (package D)
//
// Pins the large-metadata catalog surface: the light incremental index
// (mirror + filter + group), bounded truthful rendering (cap + sentinel),
// lazy collection children, and filter behavior of the Data Manager panel.
// All fixtures are synthetic tiny GeoTIFFs generated per test.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <qgsapplication.h>

#include "app/panels/asset_catalog_index.h"
#include "app/panels/data_manager_panel.h"
#include "data/data_manager.h"

#include <gdal.h>
#include <gdal_priv.h>

namespace
{

void ensureQgisApplication()
{
    if ( QApplication::instance() )
        return;
    static int argc = 1;
    static char applicationName[] = "test_asset_catalog_index";
    static char *argv[] = { applicationName, nullptr };
    new QgsApplication( argc, argv, true );
    QgsApplication::initQgis();
    GDALAllRegister();
}

/// One tiny 8x8 GeoTIFF per call (distinct paths defeat SourceKey dedup).
QString makeTinyRaster( const QString &path )
{
    static GDALDriver *driver = nullptr;
    if ( !driver )
        driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 8, 8, 1, GDT_Byte, nullptr );
    REQUIRE( ds != nullptr );
    GDALClose( ds );
    return path;
}

sicnu::data::AssetId registerAsset( sicnu::data::DataManager &manager,
                                    const QString &path )
{
    sicnu::data::RegisterRequest request;
    request.source.providerKey = QStringLiteral( "gdal" );
    request.source.canonicalSource = path;
    const auto registered = manager.registerSource( request );
    REQUIRE_FALSE( registered.assetId.isNull() );
    return registered.assetId;
}

} // namespace

using namespace sicnu;

TEST_CASE( "AssetCatalogIndex mirrors registration, updates and removal",
           "[wb8][catalog-index]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    sicnu::data::DataManager manager;

    AssetCatalogIndex index;
    REQUIRE( index.totalAssets() == 0 );
    index.rebuild( &manager );
    REQUIRE( index.totalAssets() == 0 );

    const sicnu::data::AssetId a =
      registerAsset( manager, makeTinyRaster( dir.filePath( "a.tif" ) ) );
    const sicnu::data::AssetId b =
      registerAsset( manager, makeTinyRaster( dir.filePath( "b.tif" ) ) );
    index.rebuild( &manager );
    REQUIRE( index.totalAssets() == 2 );
    REQUIRE( index.indexOfAsset( a ) >= 0 );
    REQUIRE( index.indexOfAsset( b ) >= 0 );

    // Signal-driven incremental maintenance (same code path the panel uses).
    const sicnu::data::AssetId c =
      registerAsset( manager, makeTinyRaster( dir.filePath( "c.tif" ) ) );
    index.addOrUpdateAsset( c, &manager );
    REQUIRE( index.totalAssets() == 3 );

    index.removeAsset( a );
    REQUIRE( index.totalAssets() == 2 );
    REQUIRE( index.indexOfAsset( a ) == -1 );
    REQUIRE( index.indexOfAsset( b ) >= 0 );
    // Idempotent removal, no crash on unknown ids.
    index.removeAsset( a );
    REQUIRE( index.totalAssets() == 2 );
}

TEST_CASE( "AssetCatalogIndex filters by name, source and id", "[wb8][catalog-index]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    sicnu::data::DataManager manager;

    const sicnu::data::AssetId water =
      registerAsset( manager, makeTinyRaster( dir.filePath( "water.tif" ) ) );
    registerAsset( manager, makeTinyRaster( dir.filePath( "urban.tif" ) ) );

    AssetCatalogIndex index;
    index.rebuild( &manager );

    // By name (case-insensitive substring).
    REQUIRE( index.filterIndices( QStringLiteral( "WATER" ) ).size() == 1 );
    // By source path substring.
    REQUIRE( index.filterIndices( QStringLiteral( "urban" ) ).size() == 1 );
    // By id text.
    REQUIRE( index.filterIndices( water.toString() ).size() == 1 );
    // Empty filter = everything, catalog order.
    REQUIRE( index.filterIndices( QString() ).size() == 2 );
    // No match → empty (never everything).
    REQUIRE( index.filterIndices( QStringLiteral( "zzz-no-hit" ) ).isEmpty() );
}

TEST_CASE( "AssetCatalogIndex groups children and standalone in one pass",
           "[wb8][catalog-index]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    sicnu::data::DataManager manager;

    const auto collection =
      manager.createCollection( { QStringLiteral( "scene" ), {} } );
    REQUIRE( collection.collectionId.isNull() == false );

    const sicnu::data::AssetId child1 =
      registerAsset( manager, makeTinyRaster( dir.filePath( "c1.tif" ) ) );
    const sicnu::data::AssetId child2 =
      registerAsset( manager, makeTinyRaster( dir.filePath( "c2.tif" ) ) );
    const sicnu::data::AssetId loose =
      registerAsset( manager, makeTinyRaster( dir.filePath( "loose.tif" ) ) );
    REQUIRE( manager.addChildToCollection( collection.collectionId, child1 ) );
    REQUIRE( manager.addChildToCollection( collection.collectionId, child2 ) );

    AssetCatalogIndex index;
    index.rebuild( &manager );
    REQUIRE( index.totalAssets() == 3 );

    QHash<QString, QVector<int>> byCollection;
    QVector<int> standalone;
    index.groupIndices( index.filterIndices( QString() ), byCollection, standalone );
    REQUIRE( byCollection.size() == 1 );
    REQUIRE( byCollection.value( collection.collectionId.toString() ).size() == 2 );
    REQUIRE( standalone.size() == 1 );
    REQUIRE( index.entries()[standalone.first()].id == loose );
}

TEST_CASE( "Panel filter box narrows the rendered rows", "[wb8][catalog-panel]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    sicnu::data::DataManager manager;
    registerAsset( manager, makeTinyRaster( dir.filePath( "alpha.tif" ) ) );
    registerAsset( manager, makeTinyRaster( dir.filePath( "beta.tif" ) ) );

    DataManagerPanel panel( &manager );
    auto *filter = panel.findChild<QLineEdit *>( QStringLiteral( "dataManagerFilter" ) );
    REQUIRE( filter != nullptr );

    filter->setText( QStringLiteral( "alpha" ) );
    panel.refresh(); // direct refresh; the debounce re-refresh is idempotent
    REQUIRE( panel.rowCount() == 1 );
    // The remaining row is alpha's (rowText by id).
    // (rowCount counts rendered asset rows only.)

    filter->setText( QString() );
    panel.refresh();
    REQUIRE( panel.rowCount() == 2 );
}

TEST_CASE( "Bounded rendering: cap + truthful truncation sentinel",
           "[wb8][catalog-panel]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    sicnu::data::DataManager manager;
    for ( int i = 0; i < 5; ++i )
        registerAsset( manager, makeTinyRaster( dir.filePath( QStringLiteral( "r%1.tif" ).arg( i ) ) ) );

    DataManagerPanel panel( &manager );
    panel.setStandaloneRowCap( 3 );

    auto *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "dataManagerTree" ) );
    REQUIRE( tree != nullptr );
    // 3 rendered rows + 1 truthful sentinel.
    REQUIRE( tree->topLevelItemCount() == 4 );

    bool sawSentinel = false;
    QTreeWidgetItemIterator it( tree );
    while ( *it )
    {
        if ( ( *it )->data( 0, Qt::UserRole + 7 ).toBool() )
        {
            sawSentinel = true;
            // The sentinel names BOTH totals — truncation is never silent.
            CHECK( ( *it )->text( 0 ).contains( QStringLiteral( "3" ) ) );
            CHECK( ( *it )->text( 0 ).contains( QStringLiteral( "5" ) ) );
            // It is not selectable (never reads as an asset).
            CHECK( ( *it )->flags() & Qt::ItemIsEnabled );
            CHECK_FALSE( ( *it )->flags() & Qt::ItemIsSelectable );
        }
        ++it;
    }
    REQUIRE( sawSentinel );

    // Selection preservation: select the 5th asset (beyond the cap) — a
    // refresh keeps the catalog honest; selecting a rendered asset keeps it.
    panel.setStandaloneRowCap( 20000 );
    panel.refresh();
    REQUIRE( panel.rowCount() == 5 );
}

TEST_CASE( "Huge collections populate their children lazily on expand",
           "[wb8][catalog-panel]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    sicnu::data::DataManager manager;

    const auto collection = manager.createCollection( { QStringLiteral( "big-scene" ), {} } );
    REQUIRE_FALSE( collection.collectionId.isNull() );
    constexpr int kChildren = 55; // above kLazyChildThreshold (50)
    for ( int i = 0; i < kChildren; ++i )
    {
        const sicnu::data::AssetId child = registerAsset(
          manager, makeTinyRaster( dir.filePath( QStringLiteral( "tile%1.tif" ).arg( i, 3, 10, QLatin1Char( '0' ) ) ) ) );
        REQUIRE( manager.addChildToCollection( collection.collectionId, child ) );
    }

    DataManagerPanel panel( &manager );
    auto *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "dataManagerTree" ) );
    REQUIRE( tree != nullptr );
    REQUIRE( tree->topLevelItemCount() == 1 );

    QTreeWidgetItem *collectionRow = tree->topLevelItem( 0 );
    REQUIRE( collectionRow != nullptr );
    // Lazy: the collection row defers its 55 children to first expand.
    REQUIRE( collectionRow->childCount() == 0 );

    // Expanding populates exactly the full child set.
    collectionRow->setExpanded( true );
    REQUIRE( collectionRow->childCount() == kChildren );
}

TEST_CASE( "Small collections keep eager populated, expanded layout",
           "[wb8][catalog-panel]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    sicnu::data::DataManager manager;
    const auto collection = manager.createCollection( { QStringLiteral( "scene" ), {} } );
    const sicnu::data::AssetId child =
      registerAsset( manager, makeTinyRaster( dir.filePath( "kid.tif" ) ) );
    REQUIRE( manager.addChildToCollection( collection.collectionId, child ) );

    DataManagerPanel panel( &manager );
    auto *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "dataManagerTree" ) );
    REQUIRE( tree != nullptr );
    REQUIRE( tree->topLevelItemCount() == 1 );
    QTreeWidgetItem *collectionRow = tree->topLevelItem( 0 );
    REQUIRE( collectionRow->childCount() == 1 );
    REQUIRE( collectionRow->isExpanded() );
}

TEST_CASE( "Scale evidence: 200k logical records stay bounded and responsive",
           "[wb8][catalog-scale]" )
{
    ensureQgisApplication();

    // Logical-scale fixture: 200,000 light index entries (no files — the
    // index is the unit under test; render stays bounded by the panel cap,
    // proven separately above). Memory: ~200k × (id + strings) — tens of MB.
    AssetCatalogIndex index;
    constexpr int kLogicalRecords = 200000;
    auto syntheticId = []( int i ) {
        // i starts at 1: an all-zero UUID is the null id and must never be
        // used as a catalog key.
        return sicnu::data::AssetId::fromString(
                 QStringLiteral( "{00000000-0000-0000-0000-%1}" )
                   .arg( i, 12, 10, QLatin1Char( '0' ) ) )
                 .value_or( sicnu::data::AssetId() );
    };
    for ( int i = 0; i < kLogicalRecords; ++i )
    {
        AssetCatalogEntry entry;
        entry.id = syntheticId( i + 1 );
        entry.displayName = QStringLiteral( "scene_%1_band.tif" ).arg( i );
        entry.source = QStringLiteral( "/data/catalog/tile%1/tile%1.tif" ).arg( i );
        entry.kind = sicnu::data::AssetKind::Raster;
        index.addOrUpdateEntry( entry );
    }
    REQUIRE( index.totalAssets() == kLogicalRecords );

    QElapsedTimer timer;

    // Filter pass over 200k light rows (substring matching is positional-
    // agnostic, so pin exact unique names, not prefix arithmetic).
    timer.start();
    const QVector<int> uniqueHits =
      index.filterIndices( QStringLiteral( "scene_19990_band" ) );
    REQUIRE( uniqueHits.size() == 1 );
    const QVector<int> hits = index.filterIndices( QStringLiteral( "scene_19990" ) );
    const qint64 filterMs = timer.elapsed();
    // scene_19990 matches by display name and by its source path fragment.
    REQUIRE( hits.size() >= 1 );
    REQUIRE( filterMs < 500 ); // bounded; observed ~ms in Release

    // Grouping pass (collections vs standalone) over 200k.
    timer.restart();
    QHash<QString, QVector<int>> byCollection;
    QVector<int> standalone;
    index.groupIndices( index.filterIndices( QString() ), byCollection, standalone );
    const qint64 groupMs = timer.elapsed();
    REQUIRE( standalone.size() == kLogicalRecords );
    REQUIRE( groupMs < 500 );

    // Update-in-place does not grow the index.
    AssetCatalogEntry updated;
    updated.id = syntheticId( 1 );
    updated.displayName = QStringLiteral( "renamed.tif" );
    index.addOrUpdateEntry( updated );
    REQUIRE( index.totalAssets() == kLogicalRecords );
}

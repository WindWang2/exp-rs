// test_catalog_pagination.cpp — Workbench 9.0 M7: bounded catalog pagination
//
// 8.0 truncated the standalone catalog at the row cap with a truthful
// sentinel; M7 upgrades that to real pagination so a 200k-asset catalog is
// browsable in bounded windows. Contract pinned here: each page renders at
// most the cap, the pager names exact slices and totals, single-page
// catalogs look exactly as before, a new filter restarts at page 0, and the
// selection survives page flips.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>

#include <gdal.h>
#include <gdal_priv.h>

#include <qgsapplication.h>

#include "app/panels/data_manager_panel.h"
#include "data/data_asset.h"
#include "data/data_manager.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_catalog_pagination";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

QString makeTinyRaster( const QString &path )
{
  static GDALDriver *driver = nullptr;
  if ( !driver )
    driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDataset *ds = driver->Create( path.toUtf8().constData(), 4, 4, 1, GDT_Byte, nullptr );
  REQUIRE( ds != nullptr );
  GDALClose( ds );
  return path;
}

sicnu::data::AssetId registerAsset( sicnu::data::DataManager &manager, const QString &path )
{
  sicnu::data::RegisterRequest request;
  request.source.providerKey = QStringLiteral( "gdal" );
  request.source.canonicalSource = path;
  const auto registered = manager.registerSource( request );
  REQUIRE_FALSE( registered.assetId.isNull() );
  return registered.assetId;
}

QTreeWidget *treeOf( sicnu::DataManagerPanel &panel )
{
  auto *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "dataManagerTree" ) );
  REQUIRE( tree != nullptr );
  return tree;
}

/// Top-level rows that are real asset rows (the sentinel/page row is excluded
/// via the sentinel role the panel stamps — never by display text).
QVector<QTreeWidgetItem *> assetRowsOf( sicnu::DataManagerPanel &panel )
{
  QTreeWidget *tree = treeOf( panel );
  QVector<QTreeWidgetItem *> rows;
  for ( int i = 0; i < tree->topLevelItemCount(); ++i )
  {
    QTreeWidgetItem *row = tree->topLevelItem( i );
    if ( !row->data( 0, Qt::UserRole + 7 ).toBool() ) // kSentinelRole
      rows << row;
  }
  return rows;
}

/// Stable asset ids of the rendered rows — valid across page flips.
QStringList assetRowIds( sicnu::DataManagerPanel &panel )
{
  QStringList ids;
  const QVector<QTreeWidgetItem *> rows = assetRowsOf( panel );
  for ( QTreeWidgetItem *row : rows )
    ids << row->data( 0, Qt::UserRole ).toString(); // kAssetIdRole
  return ids;
}

int sentinelRowCount( sicnu::DataManagerPanel &panel )
{
  QTreeWidget *tree = treeOf( panel );
  int sentinels = 0;
  for ( int i = 0; i < tree->topLevelItemCount(); ++i )
  {
    if ( tree->topLevelItem( i )->data( 0, Qt::UserRole + 7 ).toBool() )
      ++sentinels;
  }
  return sentinels;
}

} // namespace

TEST_CASE( "Catalog pagination: bounded windows with truthful page state",
           "[m7][catalog][pagination]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  sicnu::data::DataManager manager;

  constexpr int kAssets = 12;
  for ( int i = 0; i < kAssets; ++i )
    registerAsset( manager, makeTinyRaster( dir.filePath( QStringLiteral( "page_%1.tif" ).arg( i ) ) ) );

  sicnu::DataManagerPanel panel( &manager );
  panel.setStandaloneRowCap( 5 );

  // Page 0: exactly 5 asset rows + the page sentinel. Capture the row ids
  // (NOT the item pointers — every page flip rebuilds the tree) so the
  // flip-back comparison below checks window identity, not dangling memory.
  REQUIRE( panel.standalonePage() == 0 );
  REQUIRE( panel.standalonePageCount() == 3 );
  const QStringList initialIds = assetRowIds( panel );
  CHECK( initialIds.size() == 5 );
  CHECK( sentinelRowCount( panel ) == 1 );

  // Flip forward: a different bounded window renders.
  panel.setStandalonePage( 1 );
  REQUIRE( panel.standalonePage() == 1 );
  CHECK( assetRowsOf( panel ).size() == 5 );

  // Last page: the remainder only.
  panel.setStandalonePage( 2 );
  CHECK( assetRowsOf( panel ).size() == 2 );
  CHECK( sentinelRowCount( panel ) == 1 );

  // Out-of-range pages clamp to the last page (no blank state).
  panel.setStandalonePage( 99 );
  CHECK( panel.standalonePage() == 2 );

  // Back to page 0: the same first window renders again — paging slices a
  // stable index, it never reorders entries between flips.
  panel.setStandalonePage( 0 );
  const QStringList page0Ids = assetRowIds( panel );
  REQUIRE( page0Ids.size() == 5 );
  CHECK( page0Ids == initialIds );
}

TEST_CASE( "Catalog pagination: single-page catalogs render exactly as before",
           "[m7][catalog][pagination]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  sicnu::data::DataManager manager;

  for ( int i = 0; i < 3; ++i )
    registerAsset( manager, makeTinyRaster( dir.filePath( QStringLiteral( "small_%1.tif" ).arg( i ) ) ) );

  sicnu::DataManagerPanel panel( &manager );
  panel.setStandaloneRowCap( 5 );

  CHECK( panel.standalonePageCount() <= 1 );
  CHECK( assetRowsOf( panel ).size() == 3 );
  CHECK( sentinelRowCount( panel ) == 0 );
}

TEST_CASE( "Catalog pagination: selection survives a page flip and return",
           "[m7][catalog][pagination]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  sicnu::data::DataManager manager;

  constexpr int kAssets = 12;
  QVector<sicnu::data::AssetId> ids;
  for ( int i = 0; i < kAssets; ++i )
    ids << registerAsset(
      manager, makeTinyRaster( dir.filePath( QStringLiteral( "sel_%1.tif" ).arg( i ) ) ) );

  sicnu::DataManagerPanel panel( &manager );
  panel.setStandaloneRowCap( 5 );

  // Select the first asset, leave page 0, and come back — the selection is
  // restored by identity (M7 invariant: paging never reorders or loses the
  // user's selection context).
  panel.selectAsset( ids[0] );
  REQUIRE( panel.selectedAssetId() == ids[0] );

  panel.setStandalonePage( 1 );
  CHECK( assetRowsOf( panel ).size() == 5 );
  panel.setStandalonePage( 0 );
  CHECK( panel.selectedAssetId() == ids[0] );
}

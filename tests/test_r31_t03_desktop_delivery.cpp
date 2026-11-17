// test_r31_t03_desktop_delivery.cpp — R31-T03 WP02 / WP18 behaviour tests.
//
// Real widget / real-file tests, not helper-only page-number tests:
//   * WP02: DataManagerPanel over a real DataManager + real small GeoTIFFs,
//     per-collection paging, keyboard-activatable pager rows, tail AssetId
//     reveal, filter parity, deletion fallback, bounded rendering, and a
//     100k logical-record index seam (no 100k files are generated).
//   * WP18: real HelpViewerDialog over a real temporary install tree driven
//     by the saved UI-language preference, including restart, missing locale
//     manual, missing catalog and relocatable-install negatives.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPair>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QTranslator>

#include <gdal.h>
#include <qgsapplication.h>

#include "app/application_language.h"
#include "app/help/help_guide_locale.h"
#include "app/panels/asset_catalog_index.h"
#include "app/panels/data_manager_panel.h"
#include "dialogs/help_viewer_dialog.h"
#include "support/application_translations.h"
#include "support/qsettings_redirect.h"

#include "data/collection_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"

#include <vector>

using sicnu::data::AssetId;
Q_DECLARE_METATYPE( sicnu::data::AssetId )
using sicnu::data::CollectionCreateRequest;
using sicnu::data::CollectionId;
using sicnu::data::DataManager;
using sicnu::data::PersistencePolicy;
using sicnu::data::ProductMetadata;
using sicnu::data::RegisterRequest;
using sicnu::data::SourceDescriptor;

namespace
{

exprs_test::QSettingsUserRootRedirect settingsRedirect;

void ensureApp()
{
  QSettings::setDefaultFormat( QSettings::IniFormat );
  // A QCoreApplication-only bootstrap (Windows unified main) is NOT enough for
  // QWidget-based panels; require a real QApplication (R1 P2-16).
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char appName[] = "test_r31_t03_desktop_delivery";
  static char *argv[] = { appName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
  GDALAllRegister();
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "r31-t03-desktop" ) );
}

/// RAII environment override so each negative case is deterministic and cannot
/// leak into another case or into the real desktop process.
struct EnvScope
{
  explicit EnvScope( QByteArray key ) : m_key( std::move( key ) )
  {
    m_previous = qgetenv( m_key.constData() );
    m_had = qEnvironmentVariableIsSet( m_key.constData() );
  }
  ~EnvScope()
  {
    if ( m_had )
      qputenv( m_key.constData(), m_previous );
    else
      qunsetenv( m_key.constData() );
  }
  void set( const QString &value ) { qputenv( m_key.constData(), value.toUtf8() ); }
  void unset() { qunsetenv( m_key.constData() ); }
  QByteArray m_key;
  QByteArray m_previous;
  bool m_had = false;
};

bool writeText( const QString &path, const QString &content )
{
  QFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    return false;
  const QByteArray bytes = content.toUtf8();
  return file.write( bytes ) == bytes.size();
}

/// One real 16x16 single-band GeoTIFF per distinct path (DataManager dedups on
/// SourceKey, so every collection member needs its own source file).
QString makeRaster( const QString &path, float value )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 16, 16, 1,
                                GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 100.0, 2.0, 0.0, 500.0, 0.0, -2.0 };
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection( ds,
                     "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                     "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
  std::vector<float> line( 16, value );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  for ( int row = 0; row < 16; ++row )
    GDALRasterIO( band, GF_Write, 0, row, 16, 1, line.data(), 16, 1,
                  GDT_Float32, 0, 0 );
  GDALClose( ds );
  return path;
}

SourceDescriptor gdalSource( const QString &path )
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  return source;
}

AssetId registerRaster( DataManager &manager, const QString &path,
                        PersistencePolicy policy = PersistencePolicy::ProjectPersistent )
{
  RegisterRequest request;
  request.source = gdalSource( path );
  request.persistence = policy;
  const auto result = manager.registerSource( request );
  REQUIRE_FALSE( result.assetId.isNull() );
  return result.assetId;
}

QList<QString> visibleAssetIds( QTreeWidget *tree )
{
  QList<QString> ids;
  QTreeWidgetItemIterator it( tree );
  while ( *it )
  {
    const QString id = ( *it )->data( 0, Qt::UserRole ).toString();
    if ( !id.isEmpty() )
      ids.append( id );
    ++it;
  }
  return ids;
}

QTreeWidgetItem *collectionRow( QTreeWidget *tree, const CollectionId &id )
{
  QTreeWidgetItemIterator it( tree );
  while ( *it )
  {
    if ( ( *it )->data( 0, Qt::UserRole + 1 ).toString() == id.toString() )
      return *it;
    ++it;
  }
  return nullptr;
}

QList<QTreeWidgetItem *> pagerRows( QTreeWidgetItem *collection )
{
  QList<QTreeWidgetItem *> rows;
  for ( int i = 0; i < collection->childCount(); ++i )
    if ( !collection->child( i )->data( 0, Qt::UserRole + 9 ).toString().isEmpty() )
      rows.append( collection->child( i ) );
  return rows;
}

/// Real install-like tree: both manuals live under docs/ of a relocated root.
struct GuideTree
{
  QTemporaryDir dir;
  QString docsPath;
  QString enPath;
  QString zhPath;

  GuideTree( bool withEnglish, bool withChinese )
  {
    REQUIRE( dir.isValid() );
    docsPath = QDir( dir.path() ).filePath( QStringLiteral( "share/sicnu_geo_rs/docs" ) );
    REQUIRE( QDir().mkpath( docsPath ) );
    enPath = QDir( docsPath ).filePath( QStringLiteral( "USER_GUIDE.en.md" ) );
    zhPath = QDir( docsPath ).filePath( QStringLiteral( "USER_GUIDE.md" ) );
    if ( withEnglish )
      REQUIRE( writeText( enPath,
        QStringLiteral( "# RS Studio User Guide (English)\n\n"
                        "## command.project.new — New project\n\n"
                        "ENGLISH_GUIDE_MARKER_R31T03\n\n"
                        "Machine IDs such as DATASET_NOT_FOUND and command.layer.addRaster "
                        "are never translated.\n" ) ) );
    if ( withChinese )
      REQUIRE( writeText( zhPath,
        QStringLiteral( "# RS Studio 用户手册（中文）\n\n"
                        "## command.project.new — 新建工程\n\n"
                        "CHINESE_GUIDE_MARKER_R31T03\n\n"
                        "机器 ID 如 DATASET_NOT_FOUND 与 command.layer.addRaster 保持不变。\n" ) ) );
  }
};

struct LanguageScope
{
  LanguageScope()
  {
    ensureApp();
    m_lang.unset();
    // No bootstrap ran for these cases: keep the installed-language slot empty
    // so manual resolution falls back to the saved preference.
    SicnuUi::setInstalledUiLanguage( QString() );
    QSettings().setValue( QStringLiteral( "preferences/uiLanguage" ), QStringLiteral( "en" ) );
  }
  ~LanguageScope()
  {
    exprs_test::removeApplicationTranslators();
    SicnuUi::setInstalledUiLanguage( QString() );
    QSettings().clear();
  }
  void setPreference( const QString &language )
  {
    QSettings().setValue( QStringLiteral( "preferences/uiLanguage" ), language );
  }
  EnvScope m_lang{ "SICNU_LANG" };
};

} // namespace

// ---------------------------------------------------------------------------
// WP02: per-collection paging over real assets.
// ---------------------------------------------------------------------------

TEST_CASE( "R31-T03 WP02: zero/one/cap/cap+1/multi-page stay bounded and truthful",
           "[r31t03][wp02][paging]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  DataManager manager;
  sicnu::DataManagerPanel panel( &manager );
  panel.setCollectionPageSize( 3 );

  const int sizes[] = { 0, 1, 3, 4, 8 };
  const int expectedPages[] = { 0, 1, 1, 2, 3 };
  QVector<QPair<CollectionId, QVector<AssetId>>> collections;
  for ( int s = 0; s < 5; ++s )
  {
    CollectionCreateRequest request{ QStringLiteral( "scene-%1" ).arg( s ),
                                     ProductMetadata() };
    const CollectionId id = manager.createCollection( request ).collectionId;
    QVector<AssetId> members;
    for ( int i = 0; i < sizes[s]; ++i )
    {
      const QString path = dir.filePath(
        QStringLiteral( "s%1-%2.tif" ).arg( s ).arg( i ) );
      const AssetId asset = registerRaster( manager, makeRaster( path, 1.0f + i ) );
      REQUIRE( manager.addChildToCollection( id, asset ) );
      members.append( asset );
    }
    collections.append( { id, members } );
  }
  panel.refresh();

  QTreeWidget *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "dataManagerTree" ) );
  REQUIRE( tree != nullptr );

  for ( int s = 0; s < 5; ++s )
  {
    const CollectionId id = collections[s].first;
    INFO( "collection size " << sizes[s] );
    REQUIRE( panel.collectionPageCount( id ) == expectedPages[s] );
    REQUIRE( panel.collectionPage( id ) == 0 );
    QTreeWidgetItem *row = collectionRow( tree, id );
    if ( sizes[s] == 0 )
    {
      // Empty collections still own a truthful page label (0 pages).
      CHECK( panel.collectionPageLabel( id ).isEmpty() );
      continue;
    }
    REQUIRE( row != nullptr );
    const int assetRows = row->childCount() - pagerRows( row ).size();
    CHECK( assetRows == qMin( 3, sizes[s] ) );
    CHECK( assetRows <= panel.collectionPageSize() );
    if ( expectedPages[s] > 1 )
    {
      REQUIRE( pagerRows( row ).size() == 2 );
      for ( QTreeWidgetItem *pager : pagerRows( row ) )
      {
        // Screen readers must be able to distinguish an ACTION from data.
        CHECK( pager->data( 0, Qt::UserRole ).toString().isEmpty() );
        CHECK_FALSE( pager->data( 0, Qt::AccessibleTextRole ).toString().isEmpty() );
        CHECK_FALSE( pager->data( 0, Qt::AccessibleDescriptionRole ).toString().isEmpty() );
      }
      const QString label = panel.collectionPageLabel( id );
      CHECK_FALSE( label.isEmpty() );
      CHECK( label.contains( QStringLiteral( "1" ) ) );
      CHECK( label.contains( QString::number( sizes[s] ) ) );
    }
    else
    {
      CHECK( pagerRows( row ).isEmpty() );
    }
  }
}

TEST_CASE( "R31-T03 WP02: every page is reachable, has no duplicate or skipped AssetIds",
           "[r31t03][wp02][paging]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  DataManager manager;
  sicnu::DataManagerPanel panel( &manager );
  panel.setCollectionPageSize( 3 );

  CollectionCreateRequest request{ QStringLiteral( "big-scene" ), ProductMetadata() };
  const CollectionId id = manager.createCollection( request ).collectionId;
  QVector<AssetId> members;
  for ( int i = 0; i < 8; ++i )
  {
    const QString path = dir.filePath( QStringLiteral( "m-%1.tif" ).arg( i ) );
    const AssetId asset = registerRaster( manager, makeRaster( path, float( i ) ) );
    REQUIRE( manager.addChildToCollection( id, asset ) );
    members.append( asset );
  }
  panel.refresh();
  REQUIRE( panel.collectionPageCount( id ) == 3 );

  QTreeWidget *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "dataManagerTree" ) );
  REQUIRE( tree != nullptr );

  QList<QString> seen;
  for ( int page = 0; page < 3; ++page )
  {
    panel.setCollectionPage( id, page );
    REQUIRE( panel.collectionPage( id ) == page );
    QTreeWidgetItem *row = collectionRow( tree, id );
    REQUIRE( row != nullptr );
    const QList<QTreeWidgetItem *> pagers = pagerRows( row );
    for ( int c = 0; c < row->childCount(); ++c )
    {
      QTreeWidgetItem *child = row->child( c );
      if ( pagers.contains( child ) )
        continue;
      seen.append( child->data( 0, Qt::UserRole ).toString() );
    }
  }
  REQUIRE( seen.size() == members.size() );
  for ( int i = 0; i < members.size(); ++i )
  {
    INFO( "member " << i );
    CHECK( seen.at( i ) == members.at( i ).toString() );
  }
  // No duplicate / no skip: the multiset of rendered ids equals membership.
  QSet<QString> unique;
  for ( const QString &id : seen )
    unique.insert( id );
  CHECK( unique.size() == members.size() );

  // Middle and tail assets are reachable without rendering the whole catalog.
  REQUIRE( panel.revealCollectionAsset( members.at( 4 ) ) );
  CHECK( panel.selectedAssetId().toString() == members.at( 4 ).toString() );
  CHECK( panel.collectionPage( id ) == 1 );
  REQUIRE( panel.revealCollectionAsset( members.last() ) );
  CHECK( panel.selectedAssetId().toString() == members.last().toString() );
  CHECK( panel.collectionPage( id ) == 2 );
  CHECK( panel.rowText( members.last(), 0 ).contains( QStringLiteral( "m-7" ) ) );
  const AssetId unknown = AssetId::generate();
  CHECK_FALSE( panel.revealCollectionAsset( unknown ) );
}

TEST_CASE( "R31-T03 WP02: activating a pager action changes the visible page",
           "[r31t03][wp02][paging][keyboard]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  DataManager manager;
  sicnu::DataManagerPanel panel( &manager );
  panel.setCollectionPageSize( 2 );
  CollectionCreateRequest request{ QStringLiteral( "paged-scene" ), ProductMetadata() };
  const CollectionId id = manager.createCollection( request ).collectionId;
  QVector<AssetId> members;
  for ( int i = 0; i < 5; ++i )
  {
    const QString path = dir.filePath( QStringLiteral( "k-%1.tif" ).arg( i ) );
    const AssetId asset = registerRaster( manager, makeRaster( path, float( i ) ) );
    REQUIRE( manager.addChildToCollection( id, asset ) );
    members.append( asset );
  }
  panel.refresh();
  REQUIRE( panel.collectionPageCount( id ) == 3 );

  QTreeWidget *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "dataManagerTree" ) );
  REQUIRE( tree != nullptr );
  QTreeWidgetItem *row = collectionRow( tree, id );
  REQUIRE( row != nullptr );

  // "Next page" is the enabled pager action with delta +1. Emitting the real
  // QTreeWidget::itemActivated signal exercises the production keyboard/double
  // click connection instead of calling the private slot directly.
  QTreeWidgetItem *next = nullptr;
  for ( QTreeWidgetItem *pager : pagerRows( row ) )
    if ( pager->data( 0, Qt::UserRole + 10 ).toInt() == 1 &&
         ( pager->flags() & Qt::ItemIsSelectable ) )
      next = pager;
  REQUIRE( next != nullptr );
  // QTreeWidget::itemActivated is public; emitting it drives the very same
  // connection as Enter / double click without needing a metatype registration.
  emit tree->itemActivated( next, 0 );
  CHECK( panel.collectionPage( id ) == 1 );
  const QList<QString> ids = visibleAssetIds( tree );
  CHECK( ids.contains( members.at( 2 ).toString() ) );
  CHECK_FALSE( ids.contains( members.at( 0 ).toString() ) );

  // R2 P1: setCollectionPage() -> refresh() -> m_tree->clear() DELETES every
  // QTreeWidgetItem, so `row`/`next` are dangling after the emit. Re-resolve
  // the collection row from the fresh tree before touching any child pointer.
  QTreeWidgetItem *freshRow = collectionRow( tree, id );
  REQUIRE( freshRow != nullptr );

  // R1 P2-8: the pager-first branch must NOT swallow activation of a normal
  // asset row. Emit itemActivated on a real asset row and observe the actual
  // displayRequested signal rather than just checking a lambda was wired.
  QTreeWidgetItem *assetRow = nullptr;
  for ( int c = 0; c < freshRow->childCount(); ++c )
    if ( freshRow->child( c )->data( 0, Qt::UserRole ).toString() ==
         members.at( 2 ).toString() )
      assetRow = freshRow->child( c );
  REQUIRE( assetRow != nullptr );
  QSignalSpy displaySpy( &panel, &sicnu::DataManagerPanel::displayRequested );
  emit tree->itemActivated( assetRow, 0 );
  REQUIRE( displaySpy.size() == 1 );
  CHECK( displaySpy.at( 0 ).at( 0 ).value<sicnu::data::AssetId>().toString() ==
         members.at( 2 ).toString() );
}

TEST_CASE( "R31-T03 WP02: collection-name hit and child-name hit use one filter rule",
           "[r31t03][wp02][filter]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  DataManager manager;
  sicnu::DataManagerPanel panel( &manager );
  panel.setCollectionPageSize( 2 );

  CollectionCreateRequest request{ QStringLiteral( "OrbitalSet" ), ProductMetadata() };
  const CollectionId id = manager.createCollection( request ).collectionId;
  QVector<AssetId> members;
  for ( int i = 0; i < 3; ++i )
  {
    const QString path = dir.filePath( QStringLiteral( "band-%1.tif" ).arg( i ) );
    const AssetId asset = registerRaster( manager, makeRaster( path, float( i ) ) );
    REQUIRE( manager.addChildToCollection( id, asset ) );
    members.append( asset );
  }
  QLineEdit *filter = panel.findChild<QLineEdit *>( QStringLiteral( "dataManagerFilter" ) );
  REQUIRE( filter != nullptr );

  // Start from a NON-ZERO collection page so a page-reset assertion is
  // meaningful (R1 P1-3: the old test blocked textChanged and started at 0).
  panel.refresh();
  REQUIRE( panel.collectionPageCount( id ) == 2 );
  panel.setCollectionPage( id, 1 );
  REQUIRE( panel.collectionPage( id ) == 1 );

  // A collection-name hit keeps ALL children in the filtered membership (the
  // parent matched), while rendering stays bounded by the page budget of 2.
  // Drive the REAL QLineEdit::textChanged path (no QSignalBlocker), then let
  // refresh() perform its idempotent filter-fingerprint reset.
  QTreeWidget *tree = panel.findChild<QTreeWidget *>(
    QStringLiteral( "dataManagerTree" ) );
  REQUIRE( tree != nullptr );
  filter->setText( QStringLiteral( "OrbitalSet" ) );
  panel.refresh();
  CHECK( panel.collectionPage( id ) == 0 );
  REQUIRE( panel.collectionPageCount( id ) == 2 );
  QList<QString> byName = visibleAssetIds( tree );
  REQUIRE( byName.size() == 2 ); // page budget, not total membership
  CHECK( byName.contains( members.at( 0 ).toString() ) );
  panel.setCollectionPage( id, 1 );
  const QList<QString> nameTail = visibleAssetIds( tree );
  REQUIRE( nameTail.size() == 1 );
  CHECK( nameTail.first() == members.at( 2 ).toString() );
  CHECK( byName.size() + nameTail.size() == 3 ); // whole collection reachable

  // Programmatic setText + direct refresh() must also reset (no external
  // signal dependency): put the page back to 1 first, then change the filter
  // with signals blocked to simulate project restore / batch fixture setup.
  panel.setCollectionPage( id, 1 );
  {
    QSignalBlocker blocker( filter );
    filter->setText( QStringLiteral( "band-1" ) );
  }
  panel.refresh();
  CHECK( panel.collectionPage( id ) == 0 );
  QList<QString> byChild = visibleAssetIds( tree );
  REQUIRE( byChild.size() == 1 );
  CHECK( byChild.first() == members.at( 1 ).toString() );
  CHECK( panel.collectionPageCount( id ) == 1 );

  // Clearing the filter restores every member (again across two bounded pages).
  filter->clear();
  panel.refresh();
  CHECK( panel.collectionPage( id ) == 0 );
  REQUIRE( panel.collectionPageCount( id ) == 2 );
  REQUIRE( visibleAssetIds( tree ).size() == 2 );
  panel.setCollectionPage( id, 1 );
  CHECK( visibleAssetIds( tree ).size() == 1 );
}

TEST_CASE( "R31-T03 WP02: deleting the last member of the final page falls back",
           "[r31t03][wp02][paging]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  DataManager manager;
  sicnu::DataManagerPanel panel( &manager );
  panel.setCollectionPageSize( 2 );

  CollectionCreateRequest request{ QStringLiteral( "shrinking" ), ProductMetadata() };
  const CollectionId id = manager.createCollection( request ).collectionId;
  QVector<AssetId> members;
  for ( int i = 0; i < 5; ++i )
  {
    const QString path = dir.filePath( QStringLiteral( "d-%1.tif" ).arg( i ) );
    const AssetId asset = registerRaster(
      manager, makeRaster( path, float( i ) ), PersistencePolicy::SessionTemporary );
    REQUIRE( manager.addChildToCollection( id, asset ) );
    members.append( asset );
  }
  panel.refresh();
  REQUIRE( panel.collectionPageCount( id ) == 3 );
  panel.setCollectionPage( id, 2 );
  REQUIRE( panel.collectionPage( id ) == 2 );

  const sicnu::data::ReapResult reap = manager.reap( { members.last() } );
  REQUIRE( reap.unloaded );
  panel.refresh();

  CHECK( panel.collectionPageCount( id ) == 2 );
  CHECK( panel.collectionPage( id ) == 1 );
  QTreeWidget *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "dataManagerTree" ) );
  REQUIRE( tree != nullptr );
  QTreeWidgetItem *row = collectionRow( tree, id );
  REQUIRE( row != nullptr );
  CHECK( row->childCount() - pagerRows( row ).size() == 2 );
  CHECK_FALSE( visibleAssetIds( tree ).contains( members.last().toString() ) );
  CHECK_FALSE( panel.revealCollectionAsset( members.last() ) );
}

TEST_CASE( "R31-T03 WP02: 100k logical records stay bounded in the light index",
           "[r31t03][wp02][scale]" )
{
  ensureApp();
  constexpr int kLogicalRecords = 100000;
  sicnu::AssetCatalogIndex index;
  QElapsedTimer timer;
  timer.start();
  for ( int i = 0; i < kLogicalRecords; ++i )
  {
    sicnu::AssetCatalogEntry entry;
    entry.id = AssetId::generate();
    entry.displayName = QStringLiteral( "logical-band-%1" ).arg( i );
    entry.source = QStringLiteral( "memory://logical/%1" ).arg( i );
    index.addOrUpdateEntry( entry );
  }
  const qint64 buildMs = timer.elapsed();
  REQUIRE( index.totalAssets() == kLogicalRecords );

  timer.restart();
  const QVector<int> hit = index.filterIndices( QStringLiteral( "logical-band-99999" ) );
  const qint64 filterMs = timer.elapsed();
  REQUIRE( hit.size() == 1 );
  CHECK( index.entries().at( hit.first() ).displayName ==
         QStringLiteral( "logical-band-99999" ) );
  // Only bounded UI work is claimed here: the backend still returns a full
  // collection snapshot, so we report measured numbers instead of promising
  // an O(page) backend. Assertions must be falsifiable (R1 P2-14).
  INFO( "100k logical records: build=" << buildMs << "ms filter=" << filterMs
        << "ms; rendered rows are capped by collectionPageSize()" );
  CHECK( index.indexOfAsset( index.entries().at( 99999 ).id ) == 99999 );
  const QVector<int> all = index.filterIndices( QStringLiteral( "logical-band-" ) );
  CHECK( all.size() == kLogicalRecords );
  CHECK( index.filterIndices( QStringLiteral( "no-such-record" ) ).isEmpty() );
  // The light index itself is bounded: one entry per logical record and no
  // per-row snapshot copies. Rendering is bounded separately by page size.
  CHECK( index.totalAssets() == kLogicalRecords );
}

// ---------------------------------------------------------------------------
// WP18: locale -> document -> honest fallback.
// ---------------------------------------------------------------------------

TEST_CASE( "R31-T03 WP18: manual locale resolution is exact, honest and relocatable",
           "[r31t03][wp18][guide]" )
{
  // resolveGuideDocumentForUi() reads the process language authority; pin it
  // to English (and clear any installed-language slot) so this case does not
  // depend on the host's system locale (R2 second-round red test #1199).
  LanguageScope language;
  CHECK( sicnu::help::guideDocumentName( QStringLiteral( "en" ) ) ==
         QStringLiteral( "USER_GUIDE.en.md" ) );
  CHECK( sicnu::help::guideDocumentName( QStringLiteral( "zh_CN" ) ) ==
         QStringLiteral( "USER_GUIDE.md" ) );

  GuideTree both( true, true );
  const QStringList roots{ both.docsPath };
  const auto enHit = sicnu::help::resolveGuideDocument( QStringLiteral( "en" ), roots );
  REQUIRE_FALSE( enHit.fallbackUsed );
  CHECK( enHit.servedLanguage == QStringLiteral( "en" ) );
  CHECK( enHit.filePath == both.enPath );
  CHECK( enHit.notice.isEmpty() );
  const auto zhHit = sicnu::help::resolveGuideDocument( QStringLiteral( "zh_CN" ), roots );
  REQUIRE_FALSE( zhHit.fallbackUsed );
  CHECK( zhHit.servedLanguage == QStringLiteral( "zh_CN" ) );
  CHECK( zhHit.filePath == both.zhPath );

  // Missing English manual: serve Chinese WITH a visible English notice.
  GuideTree onlyChinese( false, true );
  const auto fallback = sicnu::help::resolveGuideDocument(
    QStringLiteral( "en" ), { onlyChinese.docsPath } );
  REQUIRE( fallback.fallbackUsed );
  REQUIRE_FALSE( fallback.embeddedFallback );
  CHECK( fallback.servedLanguage == QStringLiteral( "zh_CN" ) );
  CHECK( fallback.filePath == onlyChinese.zhPath );
  CHECK( fallback.notice.contains( QStringLiteral( "Note — manual language" ) ) );
  CHECK( fallback.notice.contains( QStringLiteral( "Machine IDs" ) ) );

  // Missing both: built-in summary, still with a visible notice.
  GuideTree none( false, false );
  const auto embedded = sicnu::help::resolveGuideDocument(
    QStringLiteral( "en" ), { none.docsPath } );
  REQUIRE( embedded.fallbackUsed );
  REQUIRE( embedded.embeddedFallback );
  CHECK( embedded.filePath.isEmpty() );
  CHECK( embedded.notice.contains( QStringLiteral( "manual unavailable" ) ) );

  // Relocated install: an explicit SICNU_GUIDE_DIR is AUTHORITATIVE (R2:
  // letting the source-tree docs root also participate silently serves the
  // developer's in-tree manual when the package is missing one).
  EnvScope pin( "SICNU_GUIDE_DIR" );
  pin.set( both.docsPath );
  EnvScope dataDir( "SICNU_DATA_DIR" );
  dataDir.set( both.dir.path() );
  CHECK( sicnu::help::guideSearchRoots( QStringLiteral( "en" ) ) ==
         QStringList{ both.docsPath } );
  CHECK( sicnu::help::resolveGuideDocumentForUi().filePath == both.enPath );
  // An incomplete pinned tree yields an HONEST embedded fallback, never a
  // silent hit on an unrelated manual from another root.
  pin.set( QDir( both.dir.path() ).filePath( QStringLiteral( "empty-docs" ) ) );
  CHECK( sicnu::help::resolveGuideDocumentForUi().embeddedFallback );
  // With no pin, SICNU_DATA_DIR is honoured as the first candidate.
  pin.unset();
  CHECK( sicnu::help::guideSearchRoots( QStringLiteral( "en" ) ).first() ==
         QDir( both.dir.path() ).filePath( QStringLiteral( "docs" ) ) );
}

TEST_CASE( "R31-T03 WP18: saved English survives restart and hits the real English manual",
           "[r31t03][wp18][guide][restart]" )
{
  LanguageScope language;
  GuideTree both( true, true );
  EnvScope pin( "SICNU_GUIDE_DIR" );
  pin.set( both.docsPath );

  // "Restart" is simulated by destroying the dialog and constructing a fresh
  // one: loadGuide() runs in the constructor from the saved preference only.
  {
    HelpViewerDialog viewer;
    REQUIRE( viewer.guideRequestedLanguage() == QStringLiteral( "en" ) );
    CHECK( viewer.guideServedLanguage() == QStringLiteral( "en" ) );
    CHECK_FALSE( viewer.guideFallbackUsed() );
    CHECK_FALSE( viewer.guideEmbeddedFallback() );
    REQUIRE( viewer.currentFilePath() == both.enPath );
    CHECK( viewer.currentMarkdown().contains( QStringLiteral( "ENGLISH_GUIDE_MARKER_R31T03" ) ) );
    CHECK( viewer.textBrowser()->toPlainText().contains(
      QStringLiteral( "ENGLISH_GUIDE_MARKER_R31T03" ) ) );
    CHECK( viewer.tocTree()->topLevelItemCount() > 0 );
  }
  {
    HelpViewerDialog viewer;
    REQUIRE( viewer.currentFilePath() == both.enPath );
  }

  language.setPreference( QStringLiteral( "zh_CN" ) );
  {
    HelpViewerDialog viewer;
    REQUIRE( viewer.guideRequestedLanguage() == QStringLiteral( "zh_CN" ) );
    CHECK( viewer.guideServedLanguage() == QStringLiteral( "zh_CN" ) );
    REQUIRE( viewer.currentFilePath() == both.zhPath );
    CHECK( viewer.currentMarkdown().contains( QStringLiteral( "CHINESE_GUIDE_MARKER_R31T03" ) ) );
    CHECK( viewer.textBrowser()->toPlainText().contains(
      QStringLiteral( "CHINESE_GUIDE_MARKER_R31T03" ) ) );
  }
}

TEST_CASE( "R31-T03 WP18: missing locale manual / missing catalog are visible negatives",
           "[r31t03][wp18][guide][negative]" )
{
  LanguageScope language;
  GuideTree onlyChinese( false, true );
  EnvScope pin( "SICNU_GUIDE_DIR" );
  pin.set( onlyChinese.docsPath );

  {
    HelpViewerDialog viewer;
    REQUIRE( viewer.guideFallbackUsed() );
    CHECK( viewer.guideServedLanguage() == QStringLiteral( "zh_CN" ) );
    REQUIRE( viewer.currentFilePath() == onlyChinese.zhPath );
    // The notice is actually rendered, not merely stored in a field.
    CHECK( viewer.textBrowser()->toPlainText().contains(
      QStringLiteral( "Note — manual language" ) ) );
    CHECK( viewer.currentMarkdown().contains( QStringLiteral( "Machine IDs" ) ) );
    CHECK( viewer.currentMarkdown().contains( QStringLiteral( "CHINESE_GUIDE_MARKER_R31T03" ) ) );
  }

  GuideTree none( false, false );
  pin.set( none.docsPath );
  {
    HelpViewerDialog viewer;
    REQUIRE( viewer.guideEmbeddedFallback() );
    CHECK( viewer.textBrowser()->toPlainText().contains(
      QStringLiteral( "manual unavailable" ) ) );
    CHECK_FALSE( viewer.textBrowser()->toPlainText().isEmpty() );
  }

  // Missing ONE catalog must fall back to a fully English UI (#2064/#2065),
  // never a half-translated window. Copy only the app catalog into an isolated
  // directory so the Qt catalog is genuinely absent. R1 P1-1: with a zh_CN
  // preference, the MANUAL must follow the actually-installed English UI, not
  // silently resolve the Chinese manual.
  GuideTree installedCatalogs( true, true );
  pin.set( installedCatalogs.docsPath );
  language.setPreference( QStringLiteral( "zh_CN" ) );
  QTemporaryDir catalogDir;
  REQUIRE( catalogDir.isValid() );
  const QString appQmSource = QDir( QStringLiteral( SICNU_TEST_TRANSLATIONS_DIR ) )
                                .filePath( QStringLiteral( "sicnu_zh_CN.qm" ) );
  REQUIRE( QFileInfo( appQmSource ).isFile() );
  REQUIRE( QFile::copy( appQmSource,
                        catalogDir.filePath( QStringLiteral( "sicnu_zh_CN.qm" ) ) ) );
  exprs_test::removeApplicationTranslators();
  auto *app = QCoreApplication::instance();
  REQUIRE( app != nullptr );
  REQUIRE( SicnuUi::installUiTranslations( *app, { catalogDir.path() } ) ==
           QStringLiteral( "en" ) );
  CHECK( QCoreApplication::translate( "QPlatformTheme", "Cancel" ) ==
         QStringLiteral( "Cancel" ) );
  CHECK( SicnuUi::installedUiLanguage() == QStringLiteral( "en" ) );
  // R3 P2-3: all localized surfaces, including the Data Manager pager labels,
  // must follow the SAME actually-installed language (not the zh_CN preference).
  CHECK( SicnuUi::effectiveUiLanguage() == QStringLiteral( "en" ) );
  {
    QTemporaryDir dataDir;
    REQUIRE( dataDir.isValid() );
    DataManager manager;
    sicnu::DataManagerPanel panel( &manager );
    panel.setCollectionPageSize( 1 );
    CollectionCreateRequest request{ QStringLiteral( "effective-lang-set" ),
                                     ProductMetadata() };
    const CollectionId cid = manager.createCollection( request ).collectionId;
    for ( int i = 0; i < 2; ++i )
    {
      const QString path = dataDir.filePath( QStringLiteral( "e-%1.tif" ).arg( i ) );
      const AssetId asset = registerRaster( manager, makeRaster( path, float( i ) ) );
      REQUIRE( manager.addChildToCollection( cid, asset ) );
    }
    panel.refresh();
    QTreeWidget *tree = panel.findChild<QTreeWidget *>(
      QStringLiteral( "dataManagerTree" ) );
    REQUIRE( tree != nullptr );
    QTreeWidgetItem *row = collectionRow( tree, cid );
    REQUIRE( row != nullptr );
    const QList<QTreeWidgetItem *> pagers = pagerRows( row );
    REQUIRE( pagers.size() == 2 );
    QStringList pagerTexts;
    for ( QTreeWidgetItem *pager : pagers )
    {
      pagerTexts.append( pager->text( 0 ) );
      CHECK( pager->data( 0, Qt::AccessibleTextRole ).toString().contains(
        QStringLiteral( "action" ) ) );
    }
    const QString joined = pagerTexts.join( QLatin1Char( ' ' ) );
    CHECK( joined.contains( QStringLiteral( "Previous page" ) ) );
    CHECK( joined.contains( QStringLiteral( "Next page" ) ) );
    CHECK_FALSE( joined.contains( QStringLiteral( "上一页" ) ) );
    CHECK_FALSE( joined.contains( QStringLiteral( "下一页" ) ) );
  }
  {
    HelpViewerDialog viewer;
    REQUIRE( viewer.guideRequestedLanguage() == QStringLiteral( "en" ) );
    CHECK( viewer.guideServedLanguage() == QStringLiteral( "en" ) );
    REQUIRE( viewer.currentFilePath() == installedCatalogs.enPath );
    CHECK( viewer.textBrowser()->toPlainText().contains(
      QStringLiteral( "ENGLISH_GUIDE_MARKER_R31T03" ) ) );
    CHECK_FALSE( viewer.textBrowser()->toPlainText().contains(
      QStringLiteral( "CHINESE_GUIDE_MARKER_R31T03" ) ) );
  }
}

TEST_CASE( "R31-T03 WP18: exact machine IDs stay searchable and links stay inert",
           "[r31t03][wp18][guide][search]" )
{
  ensureApp();
  HelpViewerDialog viewer;
  const QString markdown =
    QStringLiteral( "# Guide\n\n"
                    "## command.project.new — machine ID\n\n"
                    "DATASET_NOT_FOUND stays verbatim.\n\n"
                    "A literal <script> tag and an untrusted link are inert.\n" );
  viewer.loadMarkdownContent( markdown, QStringLiteral( "docs/USER_GUIDE.en.md" ) );
  CHECK( viewer.currentMarkdown() == markdown );
  CHECK( viewer.textBrowser()->toPlainText().contains(
    QStringLiteral( "command.project.new" ) ) );
  CHECK( viewer.textBrowser()->toPlainText().contains(
    QStringLiteral( "DATASET_NOT_FOUND" ) ) );
  // QTextBrowser must not auto-open external links from manual content.
  CHECK_FALSE( viewer.textBrowser()->openExternalLinks() );

  viewer.filterToc( QStringLiteral( "command.project.new" ) );
  REQUIRE( viewer.tocTree()->topLevelItemCount() > 0 );
  CHECK_FALSE( viewer.tocTree()->topLevelItem( 0 )->isHidden() );
  viewer.filterToc( QString() );
  viewer.searchContent( QStringLiteral( "DATASET_NOT_FOUND" ) );
  CHECK( viewer.textBrowser()->textCursor().selectedText().contains(
    QStringLiteral( "DATASET_NOT_FOUND" ) ) );
  viewer.searchContent( QStringLiteral( "unknown-help-id-r31t03" ) );
  CHECK( viewer.textBrowser()->toPlainText().contains(
    QStringLiteral( "command.project.new" ) ) );
}

TEST_CASE( "R31-T03 WP18: both shipped manuals are non-empty, distinct and hashed",
           "[r31t03][wp18][guide][resources]" )
{
  ensureApp();
  const QDir root( QStringLiteral( CMAKE_SOURCE_DIR ) );
  const QStringList names{ QStringLiteral( "docs/USER_GUIDE.md" ),
                           QStringLiteral( "docs/USER_GUIDE.en.md" ) };
  QSet<QByteArray> digests;
  for ( const QString &name : names )
  {
    QFile file( root.filePath( name ) );
    INFO( name.toStdString() );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const QByteArray bytes = file.readAll();
    CHECK( bytes.size() > 1000 );
    const bool hasStableMachineId =
      bytes.contains( "command.project.new" ) ||
      bytes.contains( "DATASET_NOT_FOUND" ) ||
      bytes.contains( "机器 ID" );
    CHECK( hasStableMachineId );
    digests.insert( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ) );
  }
  CHECK( digests.size() == 2 );
  // The install rule and the Windows staging required-list both name BOTH
  // manuals; otherwise an English UI silently receives the Chinese manual.
  QFile cmakeFile( root.filePath( QStringLiteral( "src/app/CMakeLists.txt" ) ) );
  REQUIRE( cmakeFile.open( QIODevice::ReadOnly ) );
  const QString cmakeText = QString::fromUtf8( cmakeFile.readAll() );
  CHECK( cmakeText.contains( QStringLiteral( "docs/USER_GUIDE.md" ) ) );
  CHECK( cmakeText.contains( QStringLiteral( "docs/USER_GUIDE.en.md" ) ) );
  QFile stageFile( root.filePath(
    QStringLiteral( "scripts/stage_windows_application.ps1" ) ) );
  REQUIRE( stageFile.open( QIODevice::ReadOnly ) );
  const QString stageText = QString::fromUtf8( stageFile.readAll() );
  CHECK( stageText.contains( QStringLiteral( "share/sicnu_geo_rs/docs/USER_GUIDE.md" ) ) );
  CHECK( stageText.contains( QStringLiteral( "share/sicnu_geo_rs/docs/USER_GUIDE.en.md" ) ) );
}

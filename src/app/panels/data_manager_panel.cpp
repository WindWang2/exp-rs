#include "data_manager_panel.h"
#include "widgets/rs_empty_state_widget.h"
#include "preview/asset_preview_service.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QJsonDocument>
#include <QLineEdit>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>

#include <QBrush>
#include <QSet>
#include <QVBoxLayout>

#include <variant>

#include "data/collection_types.h"
#include "data/data_asset.h"
#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/algorithms/temporal/temporal_workspace.h"
#include "data/data_manager.h"
#include "dialogs/dialog_help_catalog.h"
#include "dialogs/temporal_analysis_dialog.h"
#include "processing/algorithms/temporal/temporal_preflight.h"

namespace sicnu
{

namespace
{

constexpr int kAssetIdRole = Qt::UserRole;
constexpr int kCollectionIdRole = Qt::UserRole + 1;
constexpr int kTemporalCollectionIdRole = Qt::UserRole + 6;
constexpr int kDisplayNameRole = Qt::UserRole + 2;
constexpr int kKindLabelRole = Qt::UserRole + 3;
constexpr int kStatusLabelRole = Qt::UserRole + 4;
constexpr int kStatusColorRole = Qt::UserRole + 5;

// Workbench 8.0 (package D) scale bounds:
// - Standalone asset rows rendered per refresh when no filter narrows them;
//   past the cap a truthful truncation sentinel names the exact totals.
constexpr int kMaxStandaloneRows = 20000;
// - Collections with more children than this populate on first expand
//   (lazy detail loading) instead of eagerly per refresh.
constexpr int kLazyChildThreshold = 50;
// - Marks the truncation sentinel row (tests rely on the role, never on
//   display text).
constexpr int kSentinelRole = Qt::UserRole + 7;
// - Marks a collection row whose children deferred to first expand.
constexpr int kLazyPopulateRole = Qt::UserRole + 8;
// kTemporalCollectionIdRole used to collide with kDisplayNameRole (both
// UserRole+2, ported from #722): configureNameCell stamps the display name
// on EVERY row, so every asset row read as a "temporal id" whose UUID parse
// failed and the context menu bailed out before showing anything (review P1).

constexpr int kStatusBarWidth = 4;
constexpr int kStatusBarGap = 6;

QIcon appIcon( const char *alias )
{
  return QIcon( QStringLiteral( ":/icons/" ) + QLatin1String( alias ) );
}

QIcon styleIcon( QStyle::StandardPixmap sp )
{
  if ( qApp && qApp->style() )
    return qApp->style()->standardIcon( sp );
  return {};
}

QIcon kindIcon( sicnu::data::AssetKind kind )
{
  switch ( kind )
  {
    case sicnu::data::AssetKind::Raster:
      return appIcon( "r_ster" );
    case sicnu::data::AssetKind::Vector:
      return appIcon( "vector" );
    case sicnu::data::AssetKind::RemoteMap:
      return appIcon( "s_tellite" );
    case sicnu::data::AssetKind::VirtualRaster:
      return appIcon( "l_yer_st_ck" );
  }
  return styleIcon( QStyle::SP_FileIcon );
}

QColor statusColor( sicnu::data::AssetState state )
{
  switch ( state )
  {
    case sicnu::data::AssetState::Ready:
      return QColor( 0x1a, 0x7f, 0x37 ); // green — available
    case sicnu::data::AssetState::Registered:
    case sicnu::data::AssetState::Resolving:
      return QColor( 0x9a, 0x67, 0x00 ); // amber — in progress
    case sicnu::data::AssetState::Missing:
    case sicnu::data::AssetState::UnavailableSource:
    case sicnu::data::AssetState::Error:
      return QColor( 0xcf, 0x22, 0x2e ); // red — unavailable
    case sicnu::data::AssetState::Offline:
    case sicnu::data::AssetState::AuthenticationRequired:
      return QColor( 0x65, 0x6d, 0x76 ); // gray
    case sicnu::data::AssetState::Stale:
      return QColor( 0xbf, 0x87, 0x00 ); // orange
  }
  return QColor( 0x8c, 0x95, 0x9f );
}

/// Paints a tall status color bar on the left of the name cell, then icon + text.
class NameWithStatusBarDelegate : public QStyledItemDelegate
{
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint( QPainter *painter, const QStyleOptionViewItem &option,
                const QModelIndex &index ) const override
    {
      QStyleOptionViewItem opt = option;
      initStyleOption( &opt, index );

      const QColor barColor = index.data( kStatusColorRole ).value<QColor>();
      QRect barRect = opt.rect;
      barRect.setWidth( kStatusBarWidth );
      // Slight vertical inset so the bar reads as a stripe, not a full cell fill.
      barRect.adjust( 0, 2, 0, -2 );

      painter->save();
      if ( barColor.isValid() )
      {
        painter->fillRect( barRect, barColor );
        opt.rect.adjust( kStatusBarWidth + kStatusBarGap, 0, 0, 0 );
      }
      QStyledItemDelegate::paint( painter, opt, index );
      painter->restore();
    }

    QSize sizeHint( const QStyleOptionViewItem &option,
                    const QModelIndex &index ) const override
    {
      QSize s = QStyledItemDelegate::sizeHint( option, index );
      s.setWidth( s.width() + kStatusBarWidth + kStatusBarGap );
      return s;
    }
};

QString escapeHtml( const QString &text )
{
  QString out = text;
  out.replace( QLatin1Char( '&' ), QLatin1String( "&amp;" ) );
  out.replace( QLatin1Char( '<' ), QLatin1String( "&lt;" ) );
  out.replace( QLatin1Char( '>' ), QLatin1String( "&gt;" ) );
  out.replace( QLatin1Char( '"' ), QLatin1String( "&quot;" ) );
  return out;
}

QString kindText( sicnu::data::AssetKind kind )
{
  switch ( kind )
  {
    case sicnu::data::AssetKind::Raster:
      return QObject::tr( "Raster" );
    case sicnu::data::AssetKind::Vector:
      return QObject::tr( "Vector" );
    case sicnu::data::AssetKind::RemoteMap:
      return QObject::tr( "Remote Map" );
    case sicnu::data::AssetKind::VirtualRaster:
      return QObject::tr( "Virtual Raster" );
  }
  return QObject::tr( "Unknown" );
}

/// Detailed type label used as a name prefix (e.g. 多波段栅格 / 单波段栅格).
/// Falls back to the shared plain kind word (kindText) when the snapshot
/// carries no raster structure to refine the label with.
QString kindPrefix( const sicnu::data::AssetSnapshot &snapshot )
{
  if ( snapshot.kind() == sicnu::data::AssetKind::Raster )
  {
    if ( const auto *raster =
           std::get_if<sicnu::data::RasterStructure>( &snapshot.structure() ) )
    {
      if ( raster->bandCount <= 1 )
        return QObject::tr( "Single-band raster" );
      return QObject::tr( "Multiband Raster" );
    }
  }
  return kindText( snapshot.kind() );
}

void configureNameCell( QTreeWidgetItem *item,
                        const QString &displayName,
                        const QString &kindLabel,
                        const QIcon &icon,
                        const QString &statusLabel,
                        const QColor &barColor,
                        const QString &sourcePath = {} )
{
  if ( !item )
    return;
  item->setIcon( 0, icon );
  item->setText( 0, QStringLiteral( "%1 · %2" ).arg( kindLabel, displayName ) );
  item->setData( 0, kDisplayNameRole, displayName );
  item->setData( 0, kKindLabelRole, kindLabel );
  item->setData( 0, kStatusLabelRole, statusLabel );
  item->setData( 0, kStatusColorRole, barColor );
  QString tip = QStringLiteral( "%1\n%2: %3" )
                  .arg( displayName, QObject::tr( "Status" ), statusLabel );
  if ( !sourcePath.isEmpty() )
    tip += QStringLiteral( "\n%1" ).arg( sourcePath );
  item->setToolTip( 0, tip );
}

QString statusText( sicnu::data::AssetState state )
{
  switch ( state )
  {
    case sicnu::data::AssetState::Registered:
      return QObject::tr( "Registered" );
    case sicnu::data::AssetState::Resolving:
      return QObject::tr( "Parsing" );
    case sicnu::data::AssetState::Ready:
      return QObject::tr( "Ready" );
    case sicnu::data::AssetState::Missing:
      return QObject::tr( "Source Missing" );
    case sicnu::data::AssetState::UnavailableSource:
      return QObject::tr( "Source Unavailable" );
    case sicnu::data::AssetState::Offline:
      return QObject::tr( "Offline" );
    case sicnu::data::AssetState::AuthenticationRequired:
      return QObject::tr( "Authentication Required" );
    case sicnu::data::AssetState::Error:
      return QObject::tr( "Error" );
    case sicnu::data::AssetState::Stale:
      return QObject::tr( "Stale" );
  }
  return QObject::tr( "Unknown" );
}

QString persistenceText( sicnu::data::PersistencePolicy persistence )
{
  switch ( persistence )
  {
    case sicnu::data::PersistencePolicy::ProjectPersistent:
      return QObject::tr( "Project Persistent" );
    case sicnu::data::PersistencePolicy::SessionTemporary:
      return QObject::tr( "Session Temporary" );
    case sicnu::data::PersistencePolicy::TaskTemporary:
      return QObject::tr( "Task Temporary" );
  }
  return QObject::tr( "Unknown" );
}

QString storageText( sicnu::data::StorageKind storage )
{
  switch ( storage )
  {
    case sicnu::data::StorageKind::File:
      return QObject::tr( "Files" );
    case sicnu::data::StorageKind::TemporaryFile:
      return QObject::tr( "Temporary Files" );
    case sicnu::data::StorageKind::Memory:
      return QObject::tr( "Memory" );
    case sicnu::data::StorageKind::Remote:
      return QObject::tr( "Remote" );
  }
  return QObject::tr( "Unknown" );
}

QString capabilityBits( sicnu::data::AssetCapabilities caps )
{
  QStringList parts;
  using C = sicnu::data::AssetCapability;
  if ( caps.testFlag( C::Renderable ) )
    parts << QObject::tr( "Renderable" );
  if ( caps.testFlag( C::ReadablePixels ) )
    parts << QObject::tr( "Readable Pixels" );
  if ( caps.testFlag( C::BandMetadata ) )
    parts << QObject::tr( "Band Metadata" );
  if ( caps.testFlag( C::BandStatistics ) )
    parts << QObject::tr( "Band Statistics" );
  if ( caps.testFlag( C::QueryableFeatures ) )
    parts << QObject::tr( "Identifiable" );
  if ( caps.testFlag( C::EditableFeatures ) )
    parts << QObject::tr( "Editable" );
  if ( caps.testFlag( C::Temporal ) )
    parts << QObject::tr( "Time Series" );
  if ( caps.testFlag( C::OfflineCacheable ) )
    parts << QObject::tr( "Offline-cacheable" );
  if ( caps.testFlag( C::Exportable ) )
    parts << QObject::tr( "Exportable" );
  if ( caps.testFlag( C::Relocatable ) )
    parts << QObject::tr( "Re-linkable" );
  if ( caps.testFlag( C::DeletableSource ) )
    parts << QObject::tr( "Source Removable" );
  return parts.isEmpty() ? QObject::tr( "(none)" ) : parts.join( QStringLiteral( " · " ) );
}

QString formatExtent( const sicnu::data::SpatialExtent &extent )
{
  if ( !extent.valid )
    return QObject::tr( "(none)" );
  return QStringLiteral( "X[%1, %2] Y[%3, %4]" )
    .arg( extent.minimumX, 0, 'f', 6 )
    .arg( extent.maximumX, 0, 'f', 6 )
    .arg( extent.minimumY, 0, 'f', 6 )
    .arg( extent.maximumY, 0, 'f', 6 );
}

QString row( const QString &label, const QString &value )
{
  return QStringLiteral( "<tr><td class='k'>%1</td><td class='v'>%2</td></tr>" )
    .arg( escapeHtml( label ), escapeHtml( value ) );
}

QString section( const QString &title, const QString &bodyRows )
{
  return QStringLiteral(
           "<h3>%1</h3><table class='meta'>%2</table>" )
    .arg( escapeHtml( title ), bodyRows );
}

QString formatStructure( const sicnu::data::AssetStructure &structure )
{
  if ( const auto *raster = std::get_if<sicnu::data::RasterStructure>( &structure ) )
  {
    QString rows;
    rows += row( QObject::tr( "Drivers" ), raster->driverName.isEmpty()
                                          ? QObject::tr( "(unknown)" )
                                          : raster->driverName );
    rows += row( QObject::tr( "Size" ),
                 QStringLiteral( "%1 × %2 px" )
                   .arg( raster->width )
                   .arg( raster->height ) );
    rows += row( QObject::tr( "Band Count" ), QString::number( raster->bandCount ) );
    rows += row( QObject::tr( "CRS" ),
                 raster->crsWkt.isEmpty() ? QObject::tr( "(none)" ) : raster->crsWkt );
    rows += row( QObject::tr( "Extent" ), formatExtent( raster->extent ) );
    if ( raster->hasGeoTransform )
    {
      const auto &gt = raster->geoTransform;
      rows += row( QObject::tr( "Affine Transform" ),
                   QStringLiteral( "[%1, %2, %3, %4, %5, %6]" )
                     .arg( gt[0], 0, 'g', 12 )
                     .arg( gt[1], 0, 'g', 12 )
                     .arg( gt[2], 0, 'g', 12 )
                     .arg( gt[3], 0, 'g', 12 )
                     .arg( gt[4], 0, 'g', 12 )
                     .arg( gt[5], 0, 'g', 12 ) );
    }
    QString bandLines;
    for ( const sicnu::data::RasterBandStructure &band : raster->bands )
    {
      QString nodata = band.noDataValue
                         ? QString::number( *band.noDataValue )
                         : QObject::tr( "None" );
      bandLines += QStringLiteral( "B%1 · %2 · NoData=%3 · %4<br/>" )
                     .arg( band.number )
                     .arg( escapeHtml( band.dataType.isEmpty()
                                         ? QObject::tr( "Type unknown" )
                                         : band.dataType ) )
                     .arg( escapeHtml( nodata ) )
                     .arg( escapeHtml( band.colorInterpretation.isEmpty()
                                         ? QStringLiteral( "—" )
                                         : band.colorInterpretation ) );
    }
    if ( bandLines.isEmpty() )
      bandLines = escapeHtml( QObject::tr( "(band details not listed)" ) );
    return section( QObject::tr( "Raster Structure" ), rows )
           + QStringLiteral( "<h3>%1</h3><div class='block'>%2</div>" )
               .arg( escapeHtml( QObject::tr( "Band" ) ), bandLines );
  }

  if ( const auto *vector = std::get_if<sicnu::data::VectorStructure>( &structure ) )
  {
    QString rows;
    rows += row( QObject::tr( "Drivers" ), vector->driverName.isEmpty()
                                          ? QObject::tr( "(unknown)" )
                                          : vector->driverName );
    rows += row( QObject::tr( "Layer Count" ), QString::number( vector->layerCount ) );
    QString layerLines;
    for ( const sicnu::data::VectorLayerStructure &layer : vector->layers )
    {
      layerLines += QStringLiteral( tr("• %1 · %2 · features %3 · %4<br/>%5<br/>") )
                      .arg( escapeHtml( layer.name ) )
                      .arg( escapeHtml( layer.geometryType.isEmpty()
                                          ? QObject::tr( "Geometry Unknown" )
                                          : layer.geometryType ) )
                      .arg( layer.featureCount < 0
                              ? QObject::tr( "Unknown" )
                              : QString::number( layer.featureCount ) )
                      .arg( escapeHtml( layer.crsWkt.isEmpty()
                                          ? QObject::tr( "No CRS" )
                                          : layer.crsWkt ) )
                      .arg( escapeHtml( formatExtent( layer.extent ) ) );
    }
    if ( layerLines.isEmpty() )
      layerLines = escapeHtml( QObject::tr( "(sub-layers not listed)" ) );
    return section( QObject::tr( "Vector Structure" ), rows )
           + QStringLiteral( "<h3>%1</h3><div class='block'>%2</div>" )
               .arg( escapeHtml( QObject::tr( "Sub-layers" ) ), layerLines );
  }

  if ( const auto *remote = std::get_if<sicnu::data::RemoteMapStructure>( &structure ) )
  {
    QString rows;
    rows += row( QObject::tr( "Service Type" ),
                 sicnu::data::RemoteMapStructure::serviceToString( remote->service ) );
    rows += row( QObject::tr( "Layer" ),
                 remote->layerNames.isEmpty()
                   ? QObject::tr( "(none)" )
                   : remote->layerNames.join( QStringLiteral( ", " ) ) );
    rows += row( QObject::tr( "CRS list" ),
                 remote->crsList.isEmpty()
                   ? QObject::tr( "(none)" )
                   : remote->crsList.join( QStringLiteral( ", " ) ) );
    rows += row( QObject::tr( "Extent" ), formatExtent( remote->extent ) );
    rows += row( QObject::tr( "Format" ),
                 remote->imageFormat.isEmpty() ? QObject::tr( "(none)" )
                                               : remote->imageFormat );
    if ( remote->pixelSizeX || remote->pixelSizeY )
    {
      rows += row( QObject::tr( "Pixel Size" ),
                   QStringLiteral( "%1 × %2" )
                     .arg( remote->pixelSizeX ? QString::number( *remote->pixelSizeX )
                                              : QStringLiteral( "?" ) )
                     .arg( remote->pixelSizeY ? QString::number( *remote->pixelSizeY )
                                              : QStringLiteral( "?" ) ) );
    }
    if ( remote->zMax > 0 || remote->zMin > 0 )
      rows += row( QObject::tr( "Zoom Level" ),
                   QStringLiteral( "%1 – %2" ).arg( remote->zMin ).arg( remote->zMax ) );
    rows += row( QObject::tr( "Valid" ),
                 remote->valid ? QObject::tr( "Yes" ) : QObject::tr( "No" ) );
    return section( QObject::tr( "Remote Map Structure" ), rows );
  }

  return section( QObject::tr( "Structure" ),
                  row( QObject::tr( "Structure" ), QObject::tr( "Not parsed yet / no structural information" ) ) );
}

QString wrapHtml( const QString &body )
{
  return QStringLiteral(
           "<html><head><style>"
           "body{font-family:sans-serif;font-size:12px;margin:8px;}"
           "h2{font-size:14px;margin:0 0 8px 0;}"
           "h3{font-size:12px;margin:12px 0 4px 0;color:#0B6E4F;border-bottom:1px solid #c5cdd6;padding-bottom:2px;}"
           "table.meta{width:100%;border-collapse:collapse;}"
           "td.k{width:28%;opacity:0.75;padding:2px 6px 2px 0;vertical-align:top;}"
           "td.v{padding:2px 0;word-break:break-all;}"
           "div.block{line-height:1.45;}"
           "</style></head><body>%1</body></html>" )
    .arg( body );
}

} // namespace

DataManagerPanel::DataManagerPanel( sicnu::data::DataManager *dataManager,
                                    QWidget *parent )
  : QDockWidget( tr( "Data Management" ), parent )
  , m_dataManager( dataManager )
{
  setObjectName( QStringLiteral( "DataManagerPanel" ) );

  m_tree = new QTreeWidget( this );
  m_tree->setObjectName( QStringLiteral( "dataManagerTree" ) );
  // Name embeds status bar + kind prefix/icon; no separate kind/status columns.
  m_tree->setColumnCount( 3 );
  m_tree->setHeaderLabels( { tr( "Name" ), tr( "Persistence" ), tr( "References" ) } );
  m_tree->setRootIsDecorated( true );
  m_tree->setSelectionMode( QAbstractItemView::ExtendedSelection );
  m_tree->setContextMenuPolicy( Qt::CustomContextMenu );
  m_tree->setIconSize( QSize( 18, 18 ) );
  m_tree->setUniformRowHeights( true );
  m_tree->setItemDelegateForColumn( 0, new NameWithStatusBarDelegate( m_tree ) );
  m_tree->header()->setStretchLastSection( false );
  m_tree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
  m_tree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
  m_tree->header()->setSectionResizeMode( 2, QHeaderView::ResizeToContents );
  m_tree->headerItem()->setToolTip(
    0, tr( "The color bar on the left shows status (green = available, red = unavailable); the type prefixes the name" ) );
  m_tree->setMinimumWidth( 220 );

  m_treeStack = new QStackedWidget( this );
  m_treeStack->setObjectName( QStringLiteral( "dataManagerTreeStack" ) );

  // Workbench 8.0: filter box above the tree (incremental, coalesced).
  // The container keeps treeStack index 0 = "catalog present" semantics.
  auto *treePane = new QWidget( this );
  auto *treePaneLay = new QVBoxLayout( treePane );
  treePaneLay->setContentsMargins( 0, 0, 0, 0 );
  treePaneLay->setSpacing( 2 );
  m_filterEdit = new QLineEdit( treePane );
  m_filterEdit->setObjectName( QStringLiteral( "dataManagerFilter" ) );
  m_filterEdit->setPlaceholderText( tr( "Filter by name / path / ID..." ) );
  m_filterEdit->setClearButtonEnabled( true );
  m_filterEdit->setAccessibleName( tr( "Filter Data Assets" ) );
  treePaneLay->addWidget( m_filterEdit );

  // Workbench 9.0 M7: bounded pagination over the filtered catalog — the UI
  // browses any catalog size in windows of standaloneRowCap rows instead of
  // truncating past the first window. Hidden while everything fits in one
  // page (the common case), so small catalogs render exactly as before.
  auto *pagerRow = new QWidget( treePane );
  pagerRow->setObjectName( QStringLiteral( "dataManagerPager" ) );
  auto *pagerLay = new QHBoxLayout( pagerRow );
  pagerLay->setContentsMargins( 0, 0, 0, 0 );
  m_prevPageBtn = new QToolButton( pagerRow );
  m_prevPageBtn->setObjectName( QStringLiteral( "dataManagerPagerPrev" ) );
  m_prevPageBtn->setText( tr( "Previous Page" ) );
  m_prevPageBtn->setAutoRepeat( false );
  m_pageLabel = new QLabel( pagerRow );
  m_pageLabel->setObjectName( QStringLiteral( "dataManagerPagerLabel" ) );
  m_pageLabel->setAccessibleName( tr( "Asset Pagination Status" ) );
  m_nextPageBtn = new QToolButton( pagerRow );
  m_nextPageBtn->setObjectName( QStringLiteral( "dataManagerPagerNext" ) );
  m_nextPageBtn->setText( tr( "Next Page" ) );
  m_nextPageBtn->setAutoRepeat( false );
  pagerLay->addWidget( m_prevPageBtn );
  pagerLay->addWidget( m_pageLabel, 1 );
  pagerLay->addWidget( m_nextPageBtn );
  pagerRow->setVisible( false );
  treePaneLay->addWidget( pagerRow );
  m_pagerRow = pagerRow;

  treePaneLay->addWidget( m_tree, 1 );
  m_treeStack->addWidget( treePane ); // Index 0: Tree (+ filter + pager)

  m_emptyState = new RsEmptyStateWidget(
      QStringLiteral( "d_t_b_se" ),
      tr( "No data assets yet" ),
      tr( "No data assets or collections registered yet. Import remote-sensing imagery, vector files or hyperspectral data to start." ),
      tr( "Import Data Assets..." ),
      m_treeStack );
  connect( m_emptyState, &RsEmptyStateWidget::actionClicked,
           this, &DataManagerPanel::importRequested );
  m_treeStack->addWidget( m_emptyState ); // Index 1: Empty State
  m_treeStack->setCurrentIndex( 1 ); // Initially empty

  auto *detailHost = new QWidget( this );
  detailHost->setObjectName( QStringLiteral( "dataManagerDetailHost" ) );
  auto *detailLay = new QVBoxLayout( detailHost );
  detailLay->setContentsMargins( 4, 4, 4, 4 );
  detailLay->setSpacing( 4 );

  m_detailTitle = new QLabel( tr( "Meta Information" ), detailHost );
  m_detailTitle->setObjectName( QStringLiteral( "dataManagerDetailTitle" ) );
  m_detailTitle->setStyleSheet( QStringLiteral( "font-weight:600;" ) );
  detailLay->addWidget( m_detailTitle );

  // Workbench 8.0: lazy bounded preview above the metadata text. Hidden
  // until a raster/vector asset with a readable local source is selected;
  // renders through AssetPreviewService on the bounded scan pool.
  m_previewLabel = new QLabel( detailHost );
  m_previewLabel->setObjectName( QStringLiteral( "dataManagerPreview" ) );
  m_previewLabel->setAlignment( Qt::AlignCenter );
  m_previewLabel->setMinimumHeight( 120 );
  m_previewLabel->setFrameStyle( QFrame::StyledPanel | QFrame::Sunken );
  m_previewLabel->hide();
  detailLay->addWidget( m_previewLabel );

  m_previewService = new sicnu::app::AssetPreviewService( this );

  m_detailView = new QTextBrowser( detailHost );
  m_detailView->setObjectName( QStringLiteral( "dataManagerDetailView" ) );
  m_detailView->setOpenExternalLinks( false );
  m_detailView->setMinimumHeight( 120 );
  detailLay->addWidget( m_detailView, 1 );

  // Catalog on top, metadata inspector below (vertical split).
  m_splitter = new QSplitter( Qt::Vertical, this );
  m_splitter->setObjectName( QStringLiteral( "dataManagerSplitter" ) );
  m_splitter->addWidget( m_treeStack );
  m_splitter->addWidget( detailHost );
  m_splitter->setStretchFactor( 0, 3 );
  m_splitter->setStretchFactor( 1, 2 );
  m_splitter->setChildrenCollapsible( false );
  setWidget( m_splitter );

  connect( m_tree, &QTreeWidget::itemActivated, this,
           &DataManagerPanel::onItemActivated );
  connect( m_tree, &QTreeWidget::customContextMenuRequested, this,
           &DataManagerPanel::onContextMenu );
  connect( m_tree, &QTreeWidget::itemSelectionChanged, this,
           &DataManagerPanel::onSelectionChanged );
  connect( m_tree, &QTreeWidget::itemExpanded, this,
           &DataManagerPanel::onItemExpanded );

  // Workbench 8.0: filter box — coalesced trailing refresh like #704.
  m_filterDebounce = new QTimer( this );
  m_filterDebounce->setSingleShot( true );
  m_filterDebounce->setInterval( 250 );
  connect( m_filterDebounce, &QTimer::timeout, this, &DataManagerPanel::refresh );
  connect( m_prevPageBtn, &QToolButton::clicked, this, [this] {
    if ( m_standalonePage > 0 )
    {
      --m_standalonePage;
      refresh();
    }
  } );
  connect( m_nextPageBtn, &QToolButton::clicked, this, [this] {
    if ( m_standalonePage + 1 < m_standalonePageCount )
    {
      ++m_standalonePage;
      refresh();
    }
  } );

  connect( m_filterEdit, &QLineEdit::textChanged, this,
           [this] {
               m_standalonePage = 0; // M7: a new filter starts at page 0
               m_filterDebounce->start();
           } );

  if ( m_dataManager )
  {
    // Workbench 8.0: the light catalog index mirrors the manager one signal
    // at a time (cheap incremental maintenance); the coalesced rebuild then
    // renders from the index instead of re-fetching every full snapshot.
    connect( m_dataManager, &sicnu::data::DataManager::assetAdded, this,
             [this]( sicnu::data::AssetId id )
             {
               m_catalogIndex.addOrUpdateAsset( id, m_dataManager );
               scheduleCoalescedRefresh();
             } );
    connect( m_dataManager, &sicnu::data::DataManager::assetChanged, this,
             [this]( sicnu::data::AssetId id )
             {
               m_catalogIndex.addOrUpdateAsset( id, m_dataManager );
               scheduleCoalescedRefresh();
             } );
    connect( m_dataManager, &sicnu::data::DataManager::assetRemoved, this,
             [this]( sicnu::data::AssetId id )
             {
               m_catalogIndex.removeAsset( id );
               scheduleCoalescedRefresh();
             } );
    // #704: batch imports emit one signal per asset; coalesce to a single
    // trailing rebuild instead of N full tree rebuilds on the GUI thread.
    connect( m_dataManager, &sicnu::data::DataManager::assetAboutToUnload, this,
             &DataManagerPanel::scheduleCoalescedRefresh );
    connect( m_dataManager, &sicnu::data::DataManager::collectionAdded, this,
             [this]( sicnu::data::CollectionId )
             {
               // Collection lifecycle events are rare: a full light rebuild
               // heals any index drift (membership is read from the
               // authoritative snapshots at render time regardless).
               m_catalogIndex.rebuild( m_dataManager );
               scheduleCoalescedRefresh();
             } );
    connect( m_dataManager, &sicnu::data::DataManager::collectionRemoved, this,
             [this]( sicnu::data::CollectionId )
             {
               m_catalogIndex.rebuild( m_dataManager );
               scheduleCoalescedRefresh();
             } );
    connect( m_dataManager, &sicnu::data::DataManager::temporalCollectionAdded, this,
             &DataManagerPanel::scheduleCoalescedRefresh );
    connect( m_dataManager, &sicnu::data::DataManager::temporalCollectionChanged, this,
             &DataManagerPanel::scheduleCoalescedRefresh );
    connect( m_dataManager, &sicnu::data::DataManager::temporalCollectionRemoved, this,
             &DataManagerPanel::scheduleCoalescedRefresh );
    m_refreshCoalesceTimer = new QTimer( this );
    m_refreshCoalesceTimer->setSingleShot( true );
    m_refreshCoalesceTimer->setInterval( 250 );
    connect( m_refreshCoalesceTimer, &QTimer::timeout, this, &DataManagerPanel::refresh );
  }

  clearDetails( tr( "Select a data asset or collection to view its meta information." ) );
  refresh();
  applyHelpTips();
}

void DataManagerPanel::applyHelpTips()
{
  setWhatsThis(
    SicnuDialogHelp::htmlForTool( QStringLiteral( "obia_data_manager" ), windowTitle() ) );
  SicnuDialogHelp::tip( this, tr( "Data management: catalog of project data assets and collections; right-click to add to display, promote, unload or view properties." ) );
  SicnuDialogHelp::tip( m_tree, tr( "Asset / collection tree. The color bar on the left shows status (green = available, red = unavailable). Double-click = add to display; right-click for more actions." ) );
  SicnuDialogHelp::tip( m_detailView, tr( "Meta information inspector for the selected assets (path, CRS, band / layer structure, etc.)." ) );
  SicnuDialogHelp::tip( m_detailTitle, tr( "Title of the current inspector item." ) );
  SicnuDialogHelp::tip( m_splitter, tr( "Drag the splitter to adjust the heights of the catalog tree and the inspector." ) );
}

int DataManagerPanel::rowCount() const
{
  // Top-level rows INCLUDING the truncation sentinel (when rendered) —
  // Workbench 8.0 caps standalone rows, so this count is a rendered-row
  // count, never a catalog total (use the index / sentinel text for those).
  return m_tree->topLevelItemCount();
}

QString DataManagerPanel::rowText( sicnu::data::AssetId id, int column ) const
{
  // Logical columns (stable for tests/API, independent of visible tree columns):
  // 0 display name, 1 kind label, 2 status label, 3 persistence, 4 refs
  QTreeWidgetItemIterator it( m_tree );
  while ( *it )
  {
    if ( ( *it )->data( 0, kAssetIdRole ).toString() == id.toString() )
    {
      switch ( column )
      {
        case 0:
          return ( *it )->data( 0, kDisplayNameRole ).toString();
        case 1:
          return ( *it )->data( 0, kKindLabelRole ).toString();
        case 2:
          return ( *it )->data( 0, kStatusLabelRole ).toString();
        case 3:
          return ( *it )->text( 1 );
        case 4:
          return ( *it )->text( 2 );
        default:
          return QString();
      }
    }
    ++it;
  }
  return QString();
}

QString DataManagerPanel::detailHtml() const
{
  return m_detailView ? m_detailView->toHtml() : QString();
}

sicnu::data::AssetId DataManagerPanel::selectedAssetId() const
{
  const QList<sicnu::data::AssetId> ids = selectedAssetIds();
  return ids.isEmpty() ? sicnu::data::AssetId() : ids.first();
}

QList<sicnu::data::AssetId> DataManagerPanel::selectedAssetIds() const
{
  QList<sicnu::data::AssetId> ids;
  if ( !m_tree )
    return ids;
  const QList<QTreeWidgetItem *> items = m_tree->selectedItems();
  for ( QTreeWidgetItem *item : items )
  {
    const sicnu::data::AssetId id = assetForItem( item );
    if ( !id.isNull() && !ids.contains( id ) )
      ids.append( id );
  }
  return ids;
}

void DataManagerPanel::selectAsset( sicnu::data::AssetId id )
{
  if ( !m_tree )
    return;
  m_tree->clearSelection();
  QTreeWidgetItemIterator it( m_tree );
  while ( *it )
  {
    if ( ( *it )->data( 0, kAssetIdRole ).toString() == id.toString() )
    {
      m_tree->setCurrentItem( *it );
      ( *it )->setSelected( true );
      return;
    }
    ++it;
  }
}

void DataManagerPanel::activateAsset( sicnu::data::AssetId id )
{
  if ( !id.isNull() )
    emit displayRequested( id );
}

void DataManagerPanel::requestRemove( sicnu::data::AssetId id )
{
  if ( !id.isNull() )
    emit unloadRequested( id );
}

void DataManagerPanel::requestPromote( sicnu::data::AssetId id )
{
  if ( isPromotable( id ) )
    emit promoteRequested( id );
}

void DataManagerPanel::createRow( QTreeWidgetItem *parent,
                                  const QString &displayName,
                                  const QString &kindLabel,
                                  sicnu::data::AssetKind kind,
                                  sicnu::data::AssetState state,
                                  const QString &source,
                                  sicnu::data::PersistencePolicy persistence,
                                  const sicnu::data::AssetId &id )
{
  auto *item = parent ? new QTreeWidgetItem( parent )
                      : new QTreeWidgetItem( m_tree );
  const QString statusLabel = statusText( state );
  configureNameCell( item, displayName, kindLabel, kindIcon( kind ),
                     statusLabel, statusColor( state ), source );
  item->setText( 1, persistenceText( persistence ) );
  item->setText( 2, QString::number( referenceCount( id ) ) );
  item->setData( 0, kAssetIdRole, id.toString() );
  if ( state == sicnu::data::AssetState::Missing )
  {
    item->setToolTip(
      0, tr( "%1\nStatus: source missing — recoverable by re-linking\n%2" )
           .arg( displayName, source ) );
  }
}

void DataManagerPanel::addAssetRow( QTreeWidgetItem *parent,
                                    const sicnu::data::AssetSnapshot &snapshot )
{
  createRow( parent,
             snapshot.displayName(),
             kindPrefix( snapshot ),
             snapshot.kind(),
             snapshot.state(),
             snapshot.source().canonicalSource,
             snapshot.persistence(),
             snapshot.id() );
}

void DataManagerPanel::addIndexRow( QTreeWidgetItem *parent,
                                    const sicnu::AssetCatalogEntry &entry )
{
  // Light-row variant of addAssetRow: same cell layout from the catalog
  // index (no per-row snapshot copy). The band-count nuance of the kind
  // label degrades to the plain kind word — tests pin status/persistence
  // labels, not band counts.
  const QString kindLabel = [&]
  {
    switch ( entry.kind )
    {
      case sicnu::data::AssetKind::Raster:
      case sicnu::data::AssetKind::Vector:
      case sicnu::data::AssetKind::RemoteMap:
      case sicnu::data::AssetKind::VirtualRaster:
        return kindText( entry.kind );
    }
    return tr( "Assets" );
  }();
  createRow( parent, entry.displayName, kindLabel, entry.kind, entry.state,
             entry.source, entry.persistence, entry.id );
}

void DataManagerPanel::addSentinelRow( QTreeWidgetItem *parent, const QString &text )
{
  // Truthful truncation: names the exact totals, never selectable, never
  // reads as an asset (no asset-id role).
  auto *item = parent ? new QTreeWidgetItem( parent )
                      : new QTreeWidgetItem( m_tree );
  item->setText( 0, text );
  item->setFlags( Qt::ItemIsEnabled );
  item->setData( 0, kSentinelRole, true );
  item->setForeground( 0, QBrush( QColor( 0x8a, 0x8f, 0x98 ) ) );
}

void DataManagerPanel::populateCollectionChildren(
  QTreeWidgetItem *collectionItem, const sicnu::data::CollectionSnapshot &collection )
{
  if ( !collectionItem )
    return;
  if ( collectionItem->childCount() > 0 )
    return; // already populated
  const QString filter = m_filterEdit ? m_filterEdit->text() : QString();
  // Membership from the authoritative collection snapshot (review A2);
  // per-child text filtering via the shared light-entry rule.
  int matchedInCollection = 0;
  int rendered = 0;
  for ( const sicnu::data::AssetId &childId : collection.childAssetIds )
  {
    const int idx = m_catalogIndex.indexOfAsset( childId );
    const sicnu::AssetCatalogEntry *entry =
      idx >= 0 ? &m_catalogIndex.entries()[idx] : nullptr;
    if ( entry && !sicnu::AssetCatalogIndex::matchesFilter( *entry, filter ) )
      continue;
    ++matchedInCollection;
    if ( rendered >= m_standaloneRowCap )
      continue; // keep counting so the sentinel totals stay truthful
    if ( entry )
    {
      addIndexRow( collectionItem, *entry );
      ++rendered;
    }
    else
    {
      // Index lag (e.g. membership changed in the same burst) — fall back
      // to the full snapshot path so the child never silently vanishes.
      const std::optional<sicnu::data::AssetSnapshot> snapshot =
        m_dataManager ? m_dataManager->asset( childId ) : std::nullopt;
      if ( snapshot.has_value() )
        addAssetRow( collectionItem, *snapshot );
      ++rendered;
    }
  }
  if ( matchedInCollection > rendered )
    addSentinelRow( collectionItem,
                    tr( "Showing first %1 of %2 items — use the filter to narrow down" )
                      .arg( rendered )
                      .arg( matchedInCollection ) );
  collectionItem->setData( 0, kLazyPopulateRole, false );
}

void DataManagerPanel::scheduleCoalescedRefresh()
{
  if ( m_refreshCoalesceTimer )
    m_refreshCoalesceTimer->start();
  else
    refresh();
}

void DataManagerPanel::onItemExpanded( QTreeWidgetItem *item )
{
  // Workbench 8.0: deferred collection children populate on first expand.
  if ( !item || !item->data( 0, kLazyPopulateRole ).toBool() )
    return;
  if ( item->childCount() > 0 )
    return; // populated by the expansion-restore path before the signal
  const auto collectionId =
    sicnu::data::CollectionId::fromString( item->data( 0, kCollectionIdRole ).toString() );
  if ( !collectionId || !m_dataManager )
    return;
  const std::optional<sicnu::data::CollectionSnapshot> collection =
    m_dataManager->collection( *collectionId );
  if ( collection.has_value() )
    populateCollectionChildren( item, *collection );
}

void DataManagerPanel::refresh()
{
  // Workbench 9.0 M7: remember the selection ACROSS rebuilds even when the
  // selected asset's row falls outside the rendered page — otherwise paging
  // away would silently drop the user's selection context and paging back
  // would not restore it. The m_inRefresh guard (review round 2) keeps the
  // rebuild's own transient selection-clear from wiping the memory.
  if ( !m_inRefresh && !selectedAssetId().isNull() )
    m_lastSelectedAssetId = selectedAssetId().toString();
  const QString previouslySelected = m_lastSelectedAssetId;
  m_inRefresh = true;

  // Workbench 8.0: preserve collection expansion across rebuilds.
  QSet<QString> expandedCollections;
  {
    QTreeWidgetItemIterator it( m_tree );
    while ( *it )
    {
      const QString cid = ( *it )->data( 0, kCollectionIdRole ).toString();
      if ( !cid.isEmpty() && ( *it )->isExpanded() )
        expandedCollections.insert( cid );
      ++it;
    }
  }

  m_tree->clear();
  if ( !m_dataManager )
  {
    clearDetails( tr( "The data manager is unavailable." ) );
    return;
  }

  // Workbench 8.0: first refresh builds the light catalog index; afterwards
  // the per-asset signals maintain it incrementally, so rebuilds below
  // render from the index instead of re-fetching every full snapshot.
  if ( !m_indexBuilt )
  {
    m_catalogIndex.rebuild( m_dataManager );
    m_indexBuilt = true;
  }

  // One filter pass over light entries: O(assets) comparisons, no snapshot
  // copies. Heavy per-asset data stays a lazy DataManager query.
  const QString filter = m_filterEdit ? m_filterEdit->text() : QString();
  const QVector<int> filtered = m_catalogIndex.filterIndices( filter );
  const bool filtering = !filter.trimmed().isEmpty();

  // Collection membership comes from the AUTHORITATIVE collection snapshot
  // (childAssetIds): DataManager::addChildToCollection emits no per-asset
  // signal, so the index cannot mirror membership without drifting (A2).
  // The index still carries membership-agnostic entry data (name/source/
  // state) for filtering and standalone rendering.
  QVector<sicnu::data::CollectionSnapshot> collectionSnapshots;
  QSet<QString> collectionChildIds;
  for ( const sicnu::data::CollectionId &collectionId : m_dataManager->collections() )
  {
    const std::optional<sicnu::data::CollectionSnapshot> collection =
      m_dataManager->collection( collectionId );
    if ( !collection.has_value() )
      continue;
    collectionSnapshots.append( *collection );
    for ( const sicnu::data::AssetId &childId : collection->childAssetIds )
      collectionChildIds.insert( childId.toString() );
  }

  // Standalone = filtered entries the collections do NOT claim.
  QVector<int> standalone;
  for ( const int idx : filtered )
  {
    if ( !collectionChildIds.contains( m_catalogIndex.entries()[idx].id.toString() ) )
      standalone.append( idx );
  }

  for ( const sicnu::data::CollectionSnapshot &collection : collectionSnapshots )
  {
    // Matching children by the SHARED filter rule over light entries.
    QVector<int> bucket;
    for ( const sicnu::data::AssetId &childId : collection.childAssetIds )
    {
      const int idx = m_catalogIndex.indexOfAsset( childId );
      if ( idx >= 0
           && sicnu::AssetCatalogIndex::matchesFilter(
             m_catalogIndex.entries()[idx], filter ) )
        bucket.append( idx );
    }
    const bool nameMatches =
      !filtering || collection.displayName.contains( filter.trimmed(), Qt::CaseInsensitive );
    // With an active filter a collection renders only when it (or a child)
    // matches — unmatched collections disappear instead of hiding matches.
    if ( filtering && !nameMatches && bucket.isEmpty() )
      continue;

    auto *collectionItem = new QTreeWidgetItem( m_tree );
    configureNameCell( collectionItem,
                       collection.displayName,
                       tr( "Collections" ),
                       appIcon( "d_t_b_se" ),
                       tr( "Collections" ),
                       QColor( 0x09, 0x69, 0xda ) ); // blue stripe for collections
    collectionItem->setText( 2, QString::number( collection.childAssetIds.size() ) );
    collectionItem->setData( 0, kCollectionIdRole, collection.id.toString() );

    // Lazy detail loading: huge collections (temporal scene catalogs with
    // 100k+ children) populate on first expand; small ones populate now so
    // the default-expanded layout does not change for normal catalogs.
    const bool lazy = bucket.size() > kLazyChildThreshold;
    if ( lazy )
    {
      // Deferred: the expander shows without children; expanding (by the
      // user or the expansion restore below) triggers the populate.
      collectionItem->setChildIndicatorPolicy( QTreeWidgetItem::ShowIndicator );
      collectionItem->setData( 0, kLazyPopulateRole, true );
      if ( expandedCollections.contains( collection.id.toString() ) )
      {
        populateCollectionChildren( collectionItem, collection );
        collectionItem->setExpanded( true );
      }
    }
    else
    {
      int rendered = 0;
      for ( const int idx : bucket )
      {
        if ( rendered >= m_standaloneRowCap )
          break;
        addIndexRow( collectionItem, m_catalogIndex.entries()[idx] );
        ++rendered;
      }
      if ( bucket.size() > rendered )
        addSentinelRow( collectionItem,
                        tr( "Showing first %1 of %2 items — use the filter to narrow down" )
                          .arg( rendered )
                          .arg( bucket.size() ) );
      collectionItem->setExpanded( true );
    }
  }

  // Temporal Collections: workspace records (first-class catalog entities).
  // Light descriptor summaries only — never raster I/O on the GUI thread.
  if ( m_dataManager->temporalCollections().size() > 0 )
  {
    auto *temporalGroup = new QTreeWidgetItem( m_tree );
    temporalGroup->setText( 0, tr( "Epoch Collection" ) );
    temporalGroup->setText( 1, tr( "Workspace Records" ) );
    temporalGroup->setText( 2, QString::number( m_dataManager->temporalCollections().size() ) );
    for ( const auto &record : m_dataManager->temporalCollections() )
    {
      auto *temporalItem = new QTreeWidgetItem( temporalGroup );
      configureNameCell( temporalItem,
                         record.displayName,
                         tr( "Epoch Collection" ),
                         appIcon( "d_t_b_se" ),
                         tr( "Epoch collection (multitemporal scene collection)" ),
                         QColor( 0x7c, 0x3a, 0xed ) ); // violet stripe for temporal records
      temporalItem->setData( 0, kTemporalCollectionIdRole, record.id.toString() );
      temporalItem->setText( 2, QString::number( record.revision ) );
      temporalItem->setText( 1, tr( "Revision %1" ).arg( record.revision ) );
    }
    temporalGroup->setExpanded( true );
  }

  // Standalone assets: bounded pagination (Workbench 9.0 M7). The window is
  // standaloneRowCap rows; beyond it the pager names the exact slice and
  // totals instead of a one-way truncation sentinel. Single-page catalogs
  // render exactly as before (pager hidden, no extra rows).
  {
    m_standalonePageCount =
      standalone.size() > 0 ? ( standalone.size() + m_standaloneRowCap - 1 ) / m_standaloneRowCap : 0;
    if ( m_standalonePage >= m_standalonePageCount )
      m_standalonePage = qMax( 0, m_standalonePageCount - 1 );

    const int begin = m_standalonePage * m_standaloneRowCap;
    const int end = qMin( standalone.size(), begin + m_standaloneRowCap );
    for ( int i = begin; i < end; ++i )
      addIndexRow( nullptr, m_catalogIndex.entries()[standalone[i]] );

    const bool paginated = m_standalonePageCount > 1;
    if ( paginated )
    {
      addSentinelRow( nullptr,
                      tr( "Items %1–%2 of %3 assets (page %4/%5)" )
                        .arg( begin + 1 )
                        .arg( end )
                        .arg( standalone.size() )
                        .arg( m_standalonePage + 1 )
                        .arg( m_standalonePageCount ) );
    }
    if ( m_pagerRow )
    {
      m_pagerRow->setVisible( paginated );
      if ( m_pageLabel )
        m_pageLabel->setText( tr( "Page %1/%2 · %3 items in total" )
                                .arg( m_standalonePage + 1 )
                                .arg( qMax( 1, m_standalonePageCount ) )
                                .arg( standalone.size() ) );
      if ( m_prevPageBtn )
        m_prevPageBtn->setEnabled( m_standalonePage > 0 );
      if ( m_nextPageBtn )
        m_nextPageBtn->setEnabled( m_standalonePage + 1 < m_standalonePageCount );
    }
  }

  if ( !previouslySelected.isEmpty() )
  {
    const auto restored = sicnu::data::AssetId::fromString( previouslySelected );
    if ( restored )
      selectAsset( *restored );
  }
  const bool hasData =
    m_catalogIndex.totalAssets() > 0 || !m_dataManager->collections().isEmpty()
    || !m_dataManager->temporalCollections().isEmpty();
  if ( m_treeStack )
    m_treeStack->setCurrentIndex( hasData ? 0 : 1 );

  onSelectionChanged();
  // Review round 2: the guard covers the trailing onSelectionChanged too —
  // only USER-driven selection changes (after refresh returned) may update
  // the remembered identity.
  m_inRefresh = false;
}

void DataManagerPanel::setStandaloneRowCap( int maxRows )
{
  m_standaloneRowCap = qBound( 1, maxRows, kMaxStandaloneRows );
  refresh();
}

void DataManagerPanel::setStandalonePage( int page )
{
  m_standalonePage = qMax( 0, page );
  refresh();
}

void DataManagerPanel::onItemActivated( QTreeWidgetItem *item, int column )
{
  Q_UNUSED( column );
  // Activate applies to all currently selected assets (or the double-clicked row).
  QList<sicnu::data::AssetId> ids = selectedAssetIds();
  if ( ids.isEmpty() )
  {
    const sicnu::data::AssetId id = assetForItem( item );
    if ( !id.isNull() )
      ids.append( id );
  }
  for ( const sicnu::data::AssetId &id : ids )
    emit displayRequested( id );
}

void DataManagerPanel::onContextMenu( const QPoint &pos )
{
  QTreeWidgetItem *item = m_tree->itemAt( pos );
  if ( item && !item->isSelected() )
  {
    m_tree->clearSelection();
    item->setSelected( true );
    m_tree->setCurrentItem( item );
  }

  // Temporal collection rows: workspace-record actions (no asset ids).
  if ( item && m_dataManager )
  {
    const QString temporalId = item->data( 0, kTemporalCollectionIdRole ).toString();
    if ( !temporalId.isEmpty() )
    {
      const auto recordId = sicnu::data::CollectionId::fromString( temporalId );
      const auto record = recordId ? m_dataManager->temporalCollection( *recordId ) : std::nullopt;
      if ( !record )
        return;

      QMenu menu( this );
      QAction *analyzeAction = menu.addAction( tr( "Time Series Analysis..." ) );
      analyzeAction->setToolTip( tr( "Opens and processes this collection in the time series analysis dialog." ) );
      QAction *preflightAction = menu.addAction( tr( "Precheck Collection" ) );
      preflightAction->setToolTip( tr( "Checks raster alignment, time and platform consistency of the collection's scenes." ) );
      menu.addSeparator();
      QAction *describeAction = menu.addAction( tr( "View Collection Info" ) );
      describeAction->setToolTip( tr( "Shows scene count, time range and platform of this epoch collection." ) );
      QAction *removeAction = menu.addAction( tr( "Remove Collection Record" ) );
      removeAction->setToolTip( tr( "Removes the record from the workspace (no scene data is deleted)." ) );
      QAction *chosen = menu.exec( m_tree->viewport()->mapToGlobal( pos ) );
      if ( chosen == analyzeAction )
      {
        TemporalAnalysisDialog dialog( this );
        dialog.setDataManager( m_dataManager );
        dialog.loadCollection( *recordId );
        dialog.exec();
      }
      else if ( chosen == preflightAction )
      {
        sicnu::temporal::TemporalCollection parsed;
        QString parseError;
        if ( sicnu::temporal::collectionFromDescriptorText( record->descriptor, &parsed, &parseError ) )
        {
          sicnu::temporal::PreflightOptions opts;
          const auto report = sicnu::temporal::runPreflight( parsed, opts );
          QStringList errors;
          QStringList warnings;
          for ( const auto &issue : report.issues )
          {
            if ( issue.blocking )
              errors.append( issue.message );
            else
              warnings.append( issue.message );
          }
          QString repText = tr( "Precheck result: %1\nTotal scenes: %2\nValid times: %3\n" )
                              .arg( report.ok() ? tr( "Passed" ) : tr( "Failed" ) )
                              .arg( report.sceneCount )
                              .arg( report.scenesWithTime );
          if ( !errors.isEmpty() )
            repText += tr( "\nErrors:\n- " ) + errors.join( QStringLiteral( "\n- " ) );
          if ( !warnings.isEmpty() )
            repText += tr( "\nWarnings:\n- " ) + warnings.join( QStringLiteral( "\n- " ) );
          QMessageBox::information( this, tr( "Collection Precheck Report" ), repText );
        }
        else
        {
          QMessageBox::warning( this, tr( "Precheck Failed" ), tr( "Cannot parse the collection descriptor: %1" ).arg( parseError ) );
        }
      }
      else if ( chosen == describeAction )
      {
        QString summary = tr( "Name: %1\nRevision: %2" ).arg( record->displayName )
                            .arg( record->revision );
        sicnu::temporal::TemporalCollection parsed;
        QString parseError;
        if ( sicnu::temporal::collectionFromDescriptorText( record->descriptor, &parsed, &parseError ) )
        {
          int bound = 0;
          QStringList platforms;
          for ( const auto &scene : parsed.scenes() )
          {
            if ( !scene.assetId.isEmpty() )
              ++bound;
            if ( !scene.platform.isEmpty() && !platforms.contains( scene.platform ) )
              platforms.append( scene.platform );
          }
          summary += QLatin1Char( '\n' ) + tr( "Scenes: %1 (%2 assets bound)" )
                       .arg( parsed.sceneCount() ).arg( bound );
          if ( !parsed.timeRangeStartIso().isEmpty() )
            summary += QLatin1Char( '\n' ) + tr( "Time range: %1 … %2" )
                         .arg( parsed.timeRangeStartIso(), parsed.timeRangeEndIso() );
          if ( !platforms.isEmpty() )
            summary += QLatin1Char( '\n' ) + tr( "Platform: %1" ).arg( platforms.join( ", " ) );
        }
        else
        {
          summary += QLatin1Char( '\n' ) + tr( "Invalid descriptor: %1" ).arg( parseError );
        }
        QMessageBox::information( this, tr( "Epoch Collection" ), summary );
      }
      else if ( chosen == removeAction )
      {
        const auto answer = QMessageBox::question(
          this, tr( "Remove Epoch Collection" ),
          tr( "Remove collection %1? Scene data will not be deleted." ).arg( record->displayName ) );
        if ( answer == QMessageBox::Yes )
          m_dataManager->removeTemporalCollection( *recordId ); // signals → coalesced refresh
      }
      return;
    }
  }

  const QList<sicnu::data::AssetId> ids = selectedAssetIds();
  if ( ids.isEmpty() )
    return;

  QMenu menu( this );
  const int n = ids.size();

  QAction *displayAction = menu.addAction(
    n == 1 ? tr( "Add to Display" ) : tr( "Add to Display (%1 items)" ).arg( n ) );
  displayAction->setToolTip( tr( "Loads the selected assets as layers into the current view." ) );

  // 查看属性：仅单选时提供（多选时下方检视器已汇总）。
  QAction *inspectAction = nullptr;
  if ( n == 1 && m_dataManager )
  {
    inspectAction = menu.addAction( tr( "View Properties" ) );
    inspectAction->setToolTip( tr( "Refreshes this asset's meta information in the inspector below." ) );
  }

  // 复制源路径：单选/多选均可用。
  QAction *copyPathAction = menu.addAction(
    n == 1 ? tr( "Copy Source Path" ) : tr( "Copy Source Paths (%1 items)" ).arg( n ) );
  copyPathAction->setToolTip( tr( "Copies the asset source path (canonicalSource) to the clipboard." ) );

  int promotable = 0;
  for ( const sicnu::data::AssetId &id : ids )
  {
    if ( isPromotable( id ) )
      ++promotable;
  }
  QAction *promoteAction = menu.addAction(
    promotable <= 1 ? tr( "Promote to Project Persistent..." )
                    : tr( "Promote to Project Persistent (%1 items)..." ).arg( promotable ) );
  promoteAction->setEnabled( promotable > 0 );
  promoteAction->setToolTip( tr( "Promotes temporary assets to project-persistent (saved with the project)." ) );

  // 重定位缺失源：仅当单选且该资产 Missing/Unavailable。
  QAction *relocateAction = nullptr;
  if ( n == 1 && isRelocatable( ids.first() ) )
  {
    relocateAction = menu.addAction( tr( "Re-link Missing Source..." ) );
    relocateAction->setToolTip( tr( "Assign a new source location to missing/unavailable assets so they can be resolved again." ) );
  }

  menu.addSeparator();

  QAction *unloadAction = menu.addAction(
    n == 1 ? tr( "Unload..." ) : tr( "Unload (%1 items)..." ).arg( n ) );
  unloadAction->setToolTip( tr( "Unload the selected assets from the project (a confirmation pops up; dependents are removed cascadingly)." ) );

  QAction *chosen = menu.exec( m_tree->viewport()->mapToGlobal( pos ) );
  if ( chosen == displayAction )
  {
    for ( const sicnu::data::AssetId &id : ids )
      emit displayRequested( id );
  }
  else if ( inspectAction && chosen == inspectAction )
  {
    const auto snapshot = m_dataManager ? m_dataManager->asset( ids.first() ) : std::nullopt;
    if ( snapshot )
      showAssetDetails( *snapshot );
  }
  else if ( chosen == copyPathAction )
  {
    QStringList paths;
    if ( m_dataManager )
    {
      for ( const sicnu::data::AssetId &id : ids )
      {
        const auto snapshot = m_dataManager->asset( id );
        if ( snapshot )
          paths.append( snapshot->source().canonicalSource );
      }
    }
    if ( QClipboard *cb = QGuiApplication::clipboard() )
      cb->setText( paths.join( QLatin1Char( '\n' ) ) );
  }
  else if ( chosen == promoteAction )
  {
    for ( const sicnu::data::AssetId &id : ids )
    {
      if ( isPromotable( id ) )
        emit promoteRequested( id );
    }
  }
  else if ( relocateAction && chosen == relocateAction )
  {
    emit relocateRequested( ids.first() );
  }
  else if ( chosen == unloadAction )
  {
    if ( n == 1 )
      emit unloadRequested( ids.first() );
    else
      emit unloadRequestedMany( ids );
  }
}

void DataManagerPanel::onSelectionChanged()
{
  const QList<sicnu::data::AssetId> selection = selectedAssetIds();
  emit assetSelectionChanged( selection );

  // Review A5: track the user's intent, including CLEARING the selection —
  // otherwise a refresh resurrects a deselected asset from the remembered
  // id. During refresh() the rebuild's own transient empty selection must
  // NOT wipe the memory (that's what makes flip-back restore work).
  if ( !m_inRefresh )
    m_lastSelectedAssetId = selection.size() == 1 ? selection.first().toString() : QString();

  if ( !m_dataManager )
  {
    clearDetails( tr( "The data manager is unavailable." ) );
    return;
  }

  const QList<sicnu::data::AssetId> ids = selection;
  if ( ids.size() > 1 )
  {
    showMultiSelectionDetails( ids );
    return;
  }
  if ( ids.size() == 1 )
  {
    const auto snapshot = m_dataManager->asset( ids.first() );
    if ( snapshot )
    {
      showAssetDetails( *snapshot );
      return;
    }
  }

  // No asset selected: maybe a collection parent row.
  QTreeWidgetItem *item = m_tree->currentItem();
  if ( item )
  {
    const auto collectionId = collectionForItem( item );
    if ( collectionId )
    {
      const auto collection = m_dataManager->collection( *collectionId );
      if ( collection )
      {
        showCollectionDetails( *collection );
        return;
      }
    }
  }

  clearDetails( tr( "Select a data asset or collection to view its meta information. Ctrl / Shift multi-select." ) );
}

void DataManagerPanel::showAssetDetails( const sicnu::data::AssetSnapshot &snapshot )
{
  if ( m_detailTitle )
    m_detailTitle->setText( tr( "Asset Meta Information — %1" ).arg( snapshot.displayName() ) );

  QString identity;
  identity += row( tr( "Display Name" ), snapshot.displayName() );
  identity += row( tr( "Asset ID" ), snapshot.id().toString() );
  identity += row( tr( "Revision" ), QString::number( snapshot.revision().value() ) );
  identity += row( tr( "Type" ), kindText( snapshot.kind() ) );
  identity += row( tr( "Status" ), statusText( snapshot.state() ) );
  identity += row( tr( "Persistence" ), persistenceText( snapshot.persistence() ) );
  identity += row( tr( "Storage" ), storageText( snapshot.storageKind() ) );
  identity += row( tr( "Capabilities" ), capabilityBits( snapshot.capabilities() ) );
  identity += row( tr( "Show References" ), QString::number( referenceCount( snapshot.id() ) ) );
  if ( snapshot.parentCollectionId() )
    identity += row( tr( "Collection" ), snapshot.parentCollectionId()->toString() );

  QString source;
  source += row( tr( "Provider" ),
                 snapshot.source().providerKey.isEmpty()
                   ? tr( "(automatic)" )
                   : snapshot.source().providerKey );
  source += row( tr( "Path / URI" ),
                 snapshot.source().canonicalSource.isEmpty()
                   ? tr( "(none)" )
                   : snapshot.source().canonicalSource );
  if ( !snapshot.source().subdataset.isEmpty() )
    source += row( tr( "Sub-datasets" ), snapshot.source().subdataset );
  if ( !snapshot.source().authConfigId.isEmpty() )
    source += row( tr( "Authentication Settings" ), snapshot.source().authConfigId );
  if ( !snapshot.source().dataOptions.isEmpty() )
  {
    QStringList opts;
    for ( auto it = snapshot.source().dataOptions.constBegin();
          it != snapshot.source().dataOptions.constEnd(); ++it )
      opts << QStringLiteral( "%1=%2" ).arg( it.key(), it.value() );
    source += row( tr( "Data Options" ), opts.join( QStringLiteral( "; " ) ) );
  }

  // Provenance + lineage: what produced this asset (derivation record) and
  // what it was derived from / what was derived from it (ADR 0065 lineage).
  QString provenanceRows;
  if ( m_dataManager )
  {
    const std::optional<sicnu::data::DerivationRecord> record =
      m_dataManager->provenance( snapshot.id() );
    if ( record )
    {
      provenanceRows += row( tr( "Algorithm" ), record->algorithmId );
      if ( !record->algorithmVersion.isEmpty() )
        provenanceRows += row( tr( "Algorithm Version" ), record->algorithmVersion );
      if ( !record->parameters.isEmpty() )
        provenanceRows += row( tr( "Parameters" ),
                               QString::fromUtf8(
                                 QJsonDocument( record->parameters ).toJson( QJsonDocument::Compact ) ) );
      if ( !record->taskReference.isEmpty() )
        provenanceRows += row( tr( "Task References" ), record->taskReference );
      if ( record->completedAtUtc.isValid() )
        provenanceRows += row( tr( "Finish Time" ), record->completedAtUtc.toString( Qt::ISODate ) );

      const QVector<sicnu::data::AssetId> inputs = m_dataManager->derivedFrom( snapshot.id() );
      if ( !inputs.isEmpty() )
      {
        QStringList names;
        for ( const sicnu::data::AssetId &id : inputs )
        {
          const auto input = m_dataManager->asset( id );
          names << ( input ? input->displayName() : id.toString() );
        }
        provenanceRows += row( tr( "Derived from" ), names.join( QStringLiteral( ", " ) ) );
      }
    }
    else
    {
      provenanceRows += row( tr( "Provenance" ), tr( "No derivation record (registered directly)" ) );
    }

    const QVector<sicnu::data::AssetId> outputs = m_dataManager->derivedOutputsOf( snapshot.id() );
    if ( !outputs.isEmpty() )
    {
      QStringList names;
      for ( const sicnu::data::AssetId &id : outputs )
      {
        const auto output = m_dataManager->asset( id );
        names << ( output ? output->displayName() : id.toString() );
      }
      provenanceRows += row( tr( "Derived Artifacts" ), names.join( QStringLiteral( ", " ) ) );
    }
  }

  const QString body =
    QStringLiteral( "<h2>%1</h2>" ).arg( escapeHtml( snapshot.displayName() ) )
    + section( tr( "Identity and Status" ), identity )
    + section( tr( "Data Source" ), source )
    + section( tr( "Provenance and Lineage" ), provenanceRows )
    + formatStructure( snapshot.structure() );

  m_detailView->setHtml( wrapHtml( body ) );
  requestDetailPreview( snapshot );
}

void DataManagerPanel::showMultiSelectionDetails(
  const QList<sicnu::data::AssetId> &ids )
{
  if ( m_previewLabel )
    m_previewLabel->hide(); // previews are per-asset only (review A11)
  if ( m_detailTitle )
    m_detailTitle->setText( tr( "Multiple selection — %1 items" ).arg( ids.size() ) );

  int ready = 0, temporary = 0, raster = 0, vector = 0;
  QString list;
  for ( const sicnu::data::AssetId &id : ids )
  {
    const auto snap = m_dataManager ? m_dataManager->asset( id ) : std::nullopt;
    if ( !snap )
    {
      list += QStringLiteral( "• %1<br/>" ).arg( escapeHtml( id.toString() ) );
      continue;
    }
    if ( snap->state() == sicnu::data::AssetState::Ready )
      ++ready;
    if ( snap->persistence() != sicnu::data::PersistencePolicy::ProjectPersistent )
      ++temporary;
    if ( snap->kind() == sicnu::data::AssetKind::Raster
         || snap->kind() == sicnu::data::AssetKind::VirtualRaster )
      ++raster;
    else if ( snap->kind() == sicnu::data::AssetKind::Vector )
      ++vector;

    list += QStringLiteral( "• %1 <span style='color:#656d76'>(%2 · %3)</span><br/>" )
              .arg( escapeHtml( snap->displayName() ),
                    escapeHtml( kindText( snap->kind() ) ),
                    escapeHtml( statusText( snap->state() ) ) );
  }

  QString summary;
  summary += row( tr( "Selection Count" ), QString::number( ids.size() ) );
  summary += row( tr( "Ready" ), QString::number( ready ) );
  summary += row( tr( "Temporary Assets" ), QString::number( temporary ) );
  summary += row( tr( "Raster Class" ), QString::number( raster ) );
  summary += row( tr( "Vector" ), QString::number( vector ) );

  const QString body =
    QStringLiteral( "<h2>%1</h2>" ).arg( escapeHtml( tr( "%1 assets selected" ).arg( ids.size() ) ) )
    + section( tr( "Summary" ), summary )
    + QStringLiteral( "<h3>%1</h3><div class='block'>%2</div>" )
        .arg( escapeHtml( tr( "List" ) ), list )
    + QStringLiteral( "<p style='color:#656d76'>%1</p>" )
        .arg( escapeHtml( tr( "Right-click for batch actions: add to display / promote / unload." ) ) );

  m_detailView->setHtml( wrapHtml( body ) );
}

void DataManagerPanel::showCollectionDetails(
  const sicnu::data::CollectionSnapshot &collection )
{
  if ( m_previewLabel )
    m_previewLabel->hide(); // previews are per-asset only (review A11)
  if ( m_detailTitle )
    m_detailTitle->setText( tr( "Collection Meta Information — %1" ).arg( collection.displayName ) );

  QString identity;
  identity += row( tr( "Display Name" ), collection.displayName );
  identity += row( tr( "Collection ID" ), collection.id.toString() );
  identity += row( tr( "Sub-asset Count" ),
                   QString::number( collection.childAssetIds.size() ) );

  QString product;
  const auto &md = collection.metadata;
  product += row( tr( "Platform" ),
                  md.platform.isEmpty() ? tr( "(none)" ) : md.platform );
  product += row( tr( "Sensor" ),
                  md.sensor.isEmpty() ? tr( "(none)" ) : md.sensor );
  product += row( tr( "Product Level" ),
                  md.productLevel.isEmpty() ? tr( "(none)" ) : md.productLevel );
  product += row( tr( "Acquisition Date" ),
                  md.acquisitionDate.isEmpty() ? tr( "(none)" ) : md.acquisitionDate );
  product += row( tr( "Processing Level" ),
                  md.processingLevel.isEmpty() ? tr( "(none)" ) : md.processingLevel );
  if ( !md.attributes.isEmpty() )
  {
    QStringList attrs;
    for ( auto it = md.attributes.constBegin(); it != md.attributes.constEnd(); ++it )
      attrs << QStringLiteral( "%1 = %2" ).arg( it.key(), it.value() );
    product += row( tr( "Extended Properties" ), attrs.join( QStringLiteral( "\n" ) ) );
  }

  QString children;
  for ( const sicnu::data::AssetId &id : collection.childAssetIds )
  {
    const auto snap = m_dataManager ? m_dataManager->asset( id ) : std::nullopt;
    if ( snap )
      children += QStringLiteral( "• %1 <span style='color:#656d76'>(%2)</span><br/>" )
                    .arg( escapeHtml( snap->displayName() ),
                          escapeHtml( kindText( snap->kind() ) ) );
    else
      children += QStringLiteral( "• %1<br/>" ).arg( escapeHtml( id.toString() ) );
  }
  if ( children.isEmpty() )
    children = escapeHtml( tr( "(no sub-assets)" ) );

  const QString body =
    QStringLiteral( "<h2>%1</h2>" ).arg( escapeHtml( collection.displayName ) )
    + section( tr( "Collections" ), identity )
    + section( tr( "Product Metadata" ), product )
    + QStringLiteral( "<h3>%1</h3><div class='block'>%2</div>" )
        .arg( escapeHtml( tr( "Sub-assets" ) ), children );

  m_detailView->setHtml( wrapHtml( body ) );
}

void DataManagerPanel::clearDetails( const QString &message )
{
  if ( m_detailTitle )
    m_detailTitle->setText( tr( "Meta Information" ) );
  if ( m_previewLabel )
    m_previewLabel->hide();
  if ( m_detailView )
  {
    m_detailView->setHtml( wrapHtml(
      QStringLiteral( "<p style='color:#656d76'>%1</p>" )
        .arg( escapeHtml( message.isEmpty()
                            ? tr( "Select a data asset or collection to view its meta information." )
                            : message ) ) ) );
  }
}

void DataManagerPanel::requestDetailPreview( const sicnu::data::AssetSnapshot &snapshot )
{
  if ( !m_previewLabel || !m_previewService )
    return;

  const QString source = snapshot.source().canonicalSource;
  const bool localFile = !source.isEmpty() && !source.contains( QStringLiteral( "://" ) )
                         && QFileInfo::exists( source );
  const sicnu::data::AssetKind kind = snapshot.kind();
  if ( !localFile || ( kind != sicnu::data::AssetKind::Raster
                       && kind != sicnu::data::AssetKind::Vector ) )
  {
    // Honest absence: remote maps, virtual rasters and unreadable sources
    // have no cheap preview — the pane simply stays hidden.
    m_previewLabel->hide();
    return;
  }

  m_previewLabel->show();
  m_previewLabel->setText( tr( "Loading preview..." ) );
  m_previewLabel->setPixmap( QPixmap() );
  m_previewSource = source;

  sicnu::app::AssetPreviewService::Request request;
  request.path = source;
  request.kind = kind == sicnu::data::AssetKind::Raster
                   ? sicnu::app::AssetPreviewService::Kind::Raster
                   : sicnu::app::AssetPreviewService::Kind::Vector;
  request.size = QSize( 280, 200 );
  m_previewService->requestPreview(
    request, this, [this]( const sicnu::app::AssetPreviewService::Result &result )
    {
      // The service guarantees "latest request wins" for THIS panel, but a
      // collection/multi-selection detour hides the pane without issuing a
      // superseding request — verify the result still belongs to what the
      // pane currently shows (review A11).
      if ( !m_previewLabel || m_previewLabel->isHidden() )
        return;
      if ( result.path != m_previewSource )
        return;
      if ( result.status == sicnu::app::PreviewRender::Status::Ready
           && !result.image.isNull() )
      {
        m_previewLabel->setText( QString() );
        m_previewLabel->setPixmap( QPixmap::fromImage( result.image ) );
        m_previewLabel->setAccessibleName(
          tr( "Data Asset Preview — %1" ).arg( result.path ) );
      }
      else
      {
        m_previewLabel->setPixmap( QPixmap() );
        m_previewLabel->setText( result.error.isEmpty()
                                   ? tr( "Preview Unavailable" )
                                   : result.error );
      }
    } );
}

sicnu::data::AssetId DataManagerPanel::assetForItem( QTreeWidgetItem *item ) const
{
  if ( !item )
    return sicnu::data::AssetId();
  const auto id = sicnu::data::AssetId::fromString(
    item->data( 0, kAssetIdRole ).toString() );
  return id.value_or( sicnu::data::AssetId() );
}

std::optional<sicnu::data::CollectionId>
DataManagerPanel::collectionForItem( QTreeWidgetItem *item ) const
{
  if ( !item )
    return std::nullopt;
  return sicnu::data::CollectionId::fromString(
    item->data( 0, kCollectionIdRole ).toString() );
}

bool DataManagerPanel::isProjectPersistent( sicnu::data::AssetId id ) const
{
  if ( !m_dataManager || id.isNull() )
    return false;
  const auto snapshot = m_dataManager->asset( id );
  return snapshot &&
         snapshot->persistence() == sicnu::data::PersistencePolicy::ProjectPersistent;
}

bool DataManagerPanel::isPromotable( sicnu::data::AssetId id ) const
{
  if ( !m_dataManager || id.isNull() )
    return false;
  const auto snapshot = m_dataManager->asset( id );
  return snapshot && !isProjectPersistent( id );
}

bool DataManagerPanel::isRelocatable( sicnu::data::AssetId id ) const
{
  if ( !m_dataManager || id.isNull() )
    return false;
  const auto snapshot = m_dataManager->asset( id );
  if ( !snapshot )
    return false;
  const auto state = snapshot->state();
  return state == sicnu::data::AssetState::Missing
         || state == sicnu::data::AssetState::UnavailableSource;
}

int DataManagerPanel::referenceCount( sicnu::data::AssetId id ) const
{
  if ( !m_dataManager )
    return 0;
  int count = 0;
  for ( const sicnu::data::LeaseRef &lease : m_dataManager->leases( id ) )
  {
    if ( lease.kind == sicnu::data::LeaseKind::View )
      ++count;
  }
  return count;
}

} // namespace sicnu

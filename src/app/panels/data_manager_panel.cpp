#include "data_manager_panel.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QGuiApplication>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSize>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QJsonDocument>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <variant>

#include "data/collection_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "dialogs/dialog_help_catalog.h"

namespace sicnu
{

namespace
{

constexpr int kAssetIdRole = Qt::UserRole;
constexpr int kCollectionIdRole = Qt::UserRole + 1;
constexpr int kDisplayNameRole = Qt::UserRole + 2;
constexpr int kKindLabelRole = Qt::UserRole + 3;
constexpr int kStatusLabelRole = Qt::UserRole + 4;
constexpr int kStatusColorRole = Qt::UserRole + 5;

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
      return QObject::tr( "栅格" );
    case sicnu::data::AssetKind::Vector:
      return QObject::tr( "矢量" );
    case sicnu::data::AssetKind::RemoteMap:
      return QObject::tr( "远程地图" );
    case sicnu::data::AssetKind::VirtualRaster:
      return QObject::tr( "虚拟栅格" );
  }
  return QObject::tr( "未知" );
}

/// Detailed type label used as a name prefix (e.g. 多波段栅格 / 单波段栅格).
QString kindPrefix( const sicnu::data::AssetSnapshot &snapshot )
{
  switch ( snapshot.kind() )
  {
    case sicnu::data::AssetKind::Raster:
    {
      if ( const auto *raster =
             std::get_if<sicnu::data::RasterStructure>( &snapshot.structure() ) )
      {
        if ( raster->bandCount <= 1 )
          return QObject::tr( "单波段栅格" );
        return QObject::tr( "多波段栅格" );
      }
      return QObject::tr( "栅格" );
    }
    case sicnu::data::AssetKind::Vector:
      return QObject::tr( "矢量" );
    case sicnu::data::AssetKind::RemoteMap:
      return QObject::tr( "远程地图" );
    case sicnu::data::AssetKind::VirtualRaster:
      return QObject::tr( "虚拟栅格" );
  }
  return QObject::tr( "未知" );
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
                  .arg( displayName, QObject::tr( "状态" ), statusLabel );
  if ( !sourcePath.isEmpty() )
    tip += QStringLiteral( "\n%1" ).arg( sourcePath );
  item->setToolTip( 0, tip );
}

QString statusText( sicnu::data::AssetState state )
{
  switch ( state )
  {
    case sicnu::data::AssetState::Registered:
      return QObject::tr( "已登记" );
    case sicnu::data::AssetState::Resolving:
      return QObject::tr( "解析中" );
    case sicnu::data::AssetState::Ready:
      return QObject::tr( "就绪" );
    case sicnu::data::AssetState::Missing:
      return QObject::tr( "源缺失" );
    case sicnu::data::AssetState::UnavailableSource:
      return QObject::tr( "源不可用" );
    case sicnu::data::AssetState::Offline:
      return QObject::tr( "离线" );
    case sicnu::data::AssetState::AuthenticationRequired:
      return QObject::tr( "需要认证" );
    case sicnu::data::AssetState::Error:
      return QObject::tr( "错误" );
    case sicnu::data::AssetState::Stale:
      return QObject::tr( "过期" );
  }
  return QObject::tr( "未知" );
}

QString persistenceText( sicnu::data::PersistencePolicy persistence )
{
  switch ( persistence )
  {
    case sicnu::data::PersistencePolicy::ProjectPersistent:
      return QObject::tr( "工程持久" );
    case sicnu::data::PersistencePolicy::SessionTemporary:
      return QObject::tr( "会话临时" );
    case sicnu::data::PersistencePolicy::TaskTemporary:
      return QObject::tr( "任务临时" );
  }
  return QObject::tr( "未知" );
}

QString storageText( sicnu::data::StorageKind storage )
{
  switch ( storage )
  {
    case sicnu::data::StorageKind::File:
      return QObject::tr( "文件" );
    case sicnu::data::StorageKind::TemporaryFile:
      return QObject::tr( "临时文件" );
    case sicnu::data::StorageKind::Memory:
      return QObject::tr( "内存" );
    case sicnu::data::StorageKind::Remote:
      return QObject::tr( "远程" );
  }
  return QObject::tr( "未知" );
}

QString capabilityBits( sicnu::data::AssetCapabilities caps )
{
  QStringList parts;
  using C = sicnu::data::AssetCapability;
  if ( caps.testFlag( C::Renderable ) )
    parts << QObject::tr( "可渲染" );
  if ( caps.testFlag( C::ReadablePixels ) )
    parts << QObject::tr( "可读像素" );
  if ( caps.testFlag( C::BandMetadata ) )
    parts << QObject::tr( "波段元数据" );
  if ( caps.testFlag( C::BandStatistics ) )
    parts << QObject::tr( "波段统计" );
  if ( caps.testFlag( C::QueryableFeatures ) )
    parts << QObject::tr( "可查询要素" );
  if ( caps.testFlag( C::EditableFeatures ) )
    parts << QObject::tr( "可编辑要素" );
  if ( caps.testFlag( C::Temporal ) )
    parts << QObject::tr( "时序" );
  if ( caps.testFlag( C::OfflineCacheable ) )
    parts << QObject::tr( "可离线缓存" );
  if ( caps.testFlag( C::Exportable ) )
    parts << QObject::tr( "可导出" );
  if ( caps.testFlag( C::Relocatable ) )
    parts << QObject::tr( "可重定位" );
  if ( caps.testFlag( C::DeletableSource ) )
    parts << QObject::tr( "可删除源" );
  return parts.isEmpty() ? QObject::tr( "（无）" ) : parts.join( QStringLiteral( " · " ) );
}

QString formatExtent( const sicnu::data::SpatialExtent &extent )
{
  if ( !extent.valid )
    return QObject::tr( "（无）" );
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
    rows += row( QObject::tr( "驱动" ), raster->driverName.isEmpty()
                                          ? QObject::tr( "（未知）" )
                                          : raster->driverName );
    rows += row( QObject::tr( "尺寸" ),
                 QStringLiteral( "%1 × %2 px" )
                   .arg( raster->width )
                   .arg( raster->height ) );
    rows += row( QObject::tr( "波段数" ), QString::number( raster->bandCount ) );
    rows += row( QObject::tr( "CRS" ),
                 raster->crsWkt.isEmpty() ? QObject::tr( "（无）" ) : raster->crsWkt );
    rows += row( QObject::tr( "范围" ), formatExtent( raster->extent ) );
    if ( raster->hasGeoTransform )
    {
      const auto &gt = raster->geoTransform;
      rows += row( QObject::tr( "仿射变换" ),
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
                         : QObject::tr( "无" );
      bandLines += QStringLiteral( "B%1 · %2 · NoData=%3 · %4<br/>" )
                     .arg( band.number )
                     .arg( escapeHtml( band.dataType.isEmpty()
                                         ? QObject::tr( "类型未知" )
                                         : band.dataType ) )
                     .arg( escapeHtml( nodata ) )
                     .arg( escapeHtml( band.colorInterpretation.isEmpty()
                                         ? QStringLiteral( "—" )
                                         : band.colorInterpretation ) );
    }
    if ( bandLines.isEmpty() )
      bandLines = escapeHtml( QObject::tr( "（未列出波段明细）" ) );
    return section( QObject::tr( "栅格结构" ), rows )
           + QStringLiteral( "<h3>%1</h3><div class='block'>%2</div>" )
               .arg( escapeHtml( QObject::tr( "波段" ) ), bandLines );
  }

  if ( const auto *vector = std::get_if<sicnu::data::VectorStructure>( &structure ) )
  {
    QString rows;
    rows += row( QObject::tr( "驱动" ), vector->driverName.isEmpty()
                                          ? QObject::tr( "（未知）" )
                                          : vector->driverName );
    rows += row( QObject::tr( "图层数" ), QString::number( vector->layerCount ) );
    QString layerLines;
    for ( const sicnu::data::VectorLayerStructure &layer : vector->layers )
    {
      layerLines += QStringLiteral( "• %1 · %2 · 要素 %3 · %4<br/>%5<br/>" )
                      .arg( escapeHtml( layer.name ) )
                      .arg( escapeHtml( layer.geometryType.isEmpty()
                                          ? QObject::tr( "几何未知" )
                                          : layer.geometryType ) )
                      .arg( layer.featureCount < 0
                              ? QObject::tr( "未知" )
                              : QString::number( layer.featureCount ) )
                      .arg( escapeHtml( layer.crsWkt.isEmpty()
                                          ? QObject::tr( "CRS 无" )
                                          : layer.crsWkt ) )
                      .arg( escapeHtml( formatExtent( layer.extent ) ) );
    }
    if ( layerLines.isEmpty() )
      layerLines = escapeHtml( QObject::tr( "（未列出子图层）" ) );
    return section( QObject::tr( "矢量结构" ), rows )
           + QStringLiteral( "<h3>%1</h3><div class='block'>%2</div>" )
               .arg( escapeHtml( QObject::tr( "子图层" ) ), layerLines );
  }

  if ( const auto *remote = std::get_if<sicnu::data::RemoteMapStructure>( &structure ) )
  {
    QString rows;
    rows += row( QObject::tr( "服务类型" ),
                 sicnu::data::RemoteMapStructure::serviceToString( remote->service ) );
    rows += row( QObject::tr( "图层" ),
                 remote->layerNames.isEmpty()
                   ? QObject::tr( "（无）" )
                   : remote->layerNames.join( QStringLiteral( ", " ) ) );
    rows += row( QObject::tr( "CRS 列表" ),
                 remote->crsList.isEmpty()
                   ? QObject::tr( "（无）" )
                   : remote->crsList.join( QStringLiteral( ", " ) ) );
    rows += row( QObject::tr( "范围" ), formatExtent( remote->extent ) );
    rows += row( QObject::tr( "格式" ),
                 remote->imageFormat.isEmpty() ? QObject::tr( "（无）" )
                                               : remote->imageFormat );
    if ( remote->pixelSizeX || remote->pixelSizeY )
    {
      rows += row( QObject::tr( "像元大小" ),
                   QStringLiteral( "%1 × %2" )
                     .arg( remote->pixelSizeX ? QString::number( *remote->pixelSizeX )
                                              : QStringLiteral( "?" ) )
                     .arg( remote->pixelSizeY ? QString::number( *remote->pixelSizeY )
                                              : QStringLiteral( "?" ) ) );
    }
    if ( remote->zMax > 0 || remote->zMin > 0 )
      rows += row( QObject::tr( "缩放级别" ),
                   QStringLiteral( "%1 – %2" ).arg( remote->zMin ).arg( remote->zMax ) );
    rows += row( QObject::tr( "有效" ),
                 remote->valid ? QObject::tr( "是" ) : QObject::tr( "否" ) );
    return section( QObject::tr( "远程地图结构" ), rows );
  }

  return section( QObject::tr( "结构" ),
                  row( QObject::tr( "结构" ), QObject::tr( "尚未解析 / 无结构信息" ) ) );
}

QString wrapHtml( const QString &body )
{
  return QStringLiteral(
           "<html><head><style>"
           "body{font-family:sans-serif;font-size:12px;color:#1f2328;margin:8px;}"
           "h2{font-size:14px;margin:0 0 8px 0;}"
           "h3{font-size:12px;margin:12px 0 4px 0;color:#0969da;border-bottom:1px solid #d0d7de;padding-bottom:2px;}"
           "table.meta{width:100%;border-collapse:collapse;}"
           "td.k{width:28%;color:#656d76;padding:2px 6px 2px 0;vertical-align:top;}"
           "td.v{padding:2px 0;word-break:break-all;}"
           "div.block{line-height:1.45;}"
           "</style></head><body>%1</body></html>" )
    .arg( body );
}

} // namespace

DataManagerPanel::DataManagerPanel( sicnu::data::DataManager *dataManager,
                                    QWidget *parent )
  : QDockWidget( tr( "数据管理" ), parent )
  , m_dataManager( dataManager )
{
  setObjectName( QStringLiteral( "DataManagerPanel" ) );

  m_tree = new QTreeWidget( this );
  m_tree->setObjectName( QStringLiteral( "dataManagerTree" ) );
  // Name embeds status bar + kind prefix/icon; no separate kind/status columns.
  m_tree->setColumnCount( 3 );
  m_tree->setHeaderLabels( { tr( "名称" ), tr( "持久性" ), tr( "引用" ) } );
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
    0, tr( "左侧色条表示状态（绿=可用，红=不可用）；类型作为名称前缀" ) );
  m_tree->setMinimumWidth( 220 );

  auto *detailHost = new QWidget( this );
  detailHost->setObjectName( QStringLiteral( "dataManagerDetailHost" ) );
  auto *detailLay = new QVBoxLayout( detailHost );
  detailLay->setContentsMargins( 4, 4, 4, 4 );
  detailLay->setSpacing( 4 );

  m_detailTitle = new QLabel( tr( "元信息" ), detailHost );
  m_detailTitle->setObjectName( QStringLiteral( "dataManagerDetailTitle" ) );
  m_detailTitle->setStyleSheet( QStringLiteral( "font-weight:600;" ) );
  detailLay->addWidget( m_detailTitle );

  m_detailView = new QTextBrowser( detailHost );
  m_detailView->setObjectName( QStringLiteral( "dataManagerDetailView" ) );
  m_detailView->setOpenExternalLinks( false );
  m_detailView->setMinimumHeight( 120 );
  detailLay->addWidget( m_detailView, 1 );

  // Catalog on top, metadata inspector below (vertical split).
  m_splitter = new QSplitter( Qt::Vertical, this );
  m_splitter->setObjectName( QStringLiteral( "dataManagerSplitter" ) );
  m_splitter->addWidget( m_tree );
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

  if ( m_dataManager )
  {
    connect( m_dataManager, &sicnu::data::DataManager::assetAdded, this,
             &DataManagerPanel::refresh );
    connect( m_dataManager, &sicnu::data::DataManager::assetChanged, this,
             &DataManagerPanel::refresh );
    connect( m_dataManager, &sicnu::data::DataManager::assetRemoved, this,
             &DataManagerPanel::refresh );
    connect( m_dataManager, &sicnu::data::DataManager::collectionAdded, this,
             &DataManagerPanel::refresh );
    connect( m_dataManager, &sicnu::data::DataManager::collectionRemoved, this,
             &DataManagerPanel::refresh );
  }

  clearDetails( tr( "选择数据资产或集合以查看元信息。" ) );
  refresh();
  applyHelpTips();
}

void DataManagerPanel::applyHelpTips()
{
  setWhatsThis(
    SicnuDialogHelp::htmlForTool( QStringLiteral( "obia_data_manager" ), windowTitle() ) );
  SicnuDialogHelp::tip( this, tr( "数据管理：工程数据资产与集合目录；右键可添加到显示、提升、卸载、查看属性。" ) );
  SicnuDialogHelp::tip( m_tree, tr( "资产/集合树。左侧色条表示状态（绿=可用，红=不可用）。双击=添加到显示；右键更多操作。" ) );
  SicnuDialogHelp::tip( m_detailView, tr( "选中资产的元信息检视器（路径、CRS、波段/图层结构等）。" ) );
  SicnuDialogHelp::tip( m_detailTitle, tr( "当前检视项标题。" ) );
  SicnuDialogHelp::tip( m_splitter, tr( "拖动分隔条调整目录树与检视器的高度。" ) );
}

int DataManagerPanel::rowCount() const
{
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

void DataManagerPanel::addAssetRow( QTreeWidgetItem *parent,
                                    const sicnu::data::AssetSnapshot &snapshot )
{
  auto *item = parent ? new QTreeWidgetItem( parent )
                      : new QTreeWidgetItem( m_tree );
  const QString kindLabel = kindPrefix( snapshot );
  const QString statusLabel = statusText( snapshot.state() );
  configureNameCell( item,
                     snapshot.displayName(),
                     kindLabel,
                     kindIcon( snapshot.kind() ),
                     statusLabel,
                     statusColor( snapshot.state() ),
                     snapshot.source().canonicalSource );
  item->setText( 1, persistenceText( snapshot.persistence() ) );
  item->setText( 2, QString::number( referenceCount( snapshot.id() ) ) );
  item->setData( 0, kAssetIdRole, snapshot.id().toString() );
  if ( snapshot.state() == sicnu::data::AssetState::Missing )
  {
    item->setToolTip( 0,
                      tr( "%1\n状态: 源缺失 — 可通过重定位恢复\n%2" )
                        .arg( snapshot.displayName(),
                              snapshot.source().canonicalSource ) );
  }
}

void DataManagerPanel::refresh()
{
  const QString previouslySelected = selectedAssetId().toString();

  m_tree->clear();
  if ( !m_dataManager )
  {
    clearDetails( tr( "数据管理器不可用。" ) );
    return;
  }

  for ( const sicnu::data::CollectionId &collectionId : m_dataManager->collections() )
  {
    const std::optional<sicnu::data::CollectionSnapshot> collection =
      m_dataManager->collection( collectionId );
    if ( !collection.has_value() )
      continue;

    auto *collectionItem = new QTreeWidgetItem( m_tree );
    configureNameCell( collectionItem,
                       collection->displayName,
                       tr( "集合" ),
                       appIcon( "d_t_b_se" ),
                       tr( "集合" ),
                       QColor( 0x09, 0x69, 0xda ) ); // blue stripe for collections
    collectionItem->setText( 2, QString::number( collection->childAssetIds.size() ) );
    collectionItem->setData( 0, kCollectionIdRole, collection->id.toString() );

    for ( const sicnu::data::AssetId &childId : collection->childAssetIds )
    {
      const std::optional<sicnu::data::AssetSnapshot> snapshot =
        m_dataManager->asset( childId );
      if ( snapshot.has_value() )
        addAssetRow( collectionItem, *snapshot );
    }
    collectionItem->setExpanded( true );
  }

  for ( const sicnu::data::AssetSnapshot &snapshot : m_dataManager->assets() )
  {
    if ( snapshot.parentCollectionId().has_value() )
      continue;
    addAssetRow( nullptr, snapshot );
  }

  if ( !previouslySelected.isEmpty() )
  {
    const auto restored = sicnu::data::AssetId::fromString( previouslySelected );
    if ( restored )
      selectAsset( *restored );
  }
  onSelectionChanged();
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

  const QList<sicnu::data::AssetId> ids = selectedAssetIds();
  if ( ids.isEmpty() )
    return;

  QMenu menu( this );
  const int n = ids.size();

  QAction *displayAction = menu.addAction(
    n == 1 ? tr( "添加到显示" ) : tr( "添加到显示（%1 项）" ).arg( n ) );
  displayAction->setToolTip( tr( "把选中资产作为图层加载到当前视图。" ) );

  // 查看属性：仅单选时提供（多选时下方检视器已汇总）。
  QAction *inspectAction = nullptr;
  if ( n == 1 && m_dataManager )
  {
    inspectAction = menu.addAction( tr( "查看属性" ) );
    inspectAction->setToolTip( tr( "在下方检视器中刷新该资产的元信息。" ) );
  }

  // 复制源路径：单选/多选均可用。
  QAction *copyPathAction = menu.addAction(
    n == 1 ? tr( "复制源路径" ) : tr( "复制源路径（%1 项）" ).arg( n ) );
  copyPathAction->setToolTip( tr( "把资产源路径（canonicalSource）复制到剪贴板。" ) );

  int promotable = 0;
  for ( const sicnu::data::AssetId &id : ids )
  {
    if ( isPromotable( id ) )
      ++promotable;
  }
  QAction *promoteAction = menu.addAction(
    promotable <= 1 ? tr( "提升为工程持久…" )
                    : tr( "提升为工程持久（%1 项）…" ).arg( promotable ) );
  promoteAction->setEnabled( promotable > 0 );
  promoteAction->setToolTip( tr( "把临时资产提升为工程持久（随工程保存）。" ) );

  // 重定位缺失源：仅当单选且该资产 Missing/Unavailable。
  QAction *relocateAction = nullptr;
  if ( n == 1 && isRelocatable( ids.first() ) )
  {
    relocateAction = menu.addAction( tr( "重定位缺失源…" ) );
    relocateAction->setToolTip( tr( "为缺失/不可用的资产指定新的源位置以重新解析。" ) );
  }

  menu.addSeparator();

  QAction *unloadAction = menu.addAction(
    n == 1 ? tr( "卸载…" ) : tr( "卸载（%1 项）…" ).arg( n ) );
  unloadAction->setToolTip( tr( "从工程卸载选中资产（会弹出确认；若有引用将级联移除）。" ) );

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
  if ( !m_dataManager )
  {
    clearDetails( tr( "数据管理器不可用。" ) );
    return;
  }

  const QList<sicnu::data::AssetId> ids = selectedAssetIds();
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

  clearDetails( tr( "选择数据资产或集合以查看元信息。Ctrl/Shift 可多选。" ) );
}

void DataManagerPanel::showAssetDetails( const sicnu::data::AssetSnapshot &snapshot )
{
  if ( m_detailTitle )
    m_detailTitle->setText( tr( "资产元信息 — %1" ).arg( snapshot.displayName() ) );

  QString identity;
  identity += row( tr( "显示名" ), snapshot.displayName() );
  identity += row( tr( "资产 ID" ), snapshot.id().toString() );
  identity += row( tr( "修订" ), QString::number( snapshot.revision().value() ) );
  identity += row( tr( "类型" ), kindText( snapshot.kind() ) );
  identity += row( tr( "状态" ), statusText( snapshot.state() ) );
  identity += row( tr( "持久性" ), persistenceText( snapshot.persistence() ) );
  identity += row( tr( "存储" ), storageText( snapshot.storageKind() ) );
  identity += row( tr( "能力" ), capabilityBits( snapshot.capabilities() ) );
  identity += row( tr( "显示引用" ), QString::number( referenceCount( snapshot.id() ) ) );
  if ( snapshot.parentCollectionId() )
    identity += row( tr( "所属集合" ), snapshot.parentCollectionId()->toString() );

  QString source;
  source += row( tr( "提供者" ),
                 snapshot.source().providerKey.isEmpty()
                   ? tr( "（自动）" )
                   : snapshot.source().providerKey );
  source += row( tr( "路径 / URI" ),
                 snapshot.source().canonicalSource.isEmpty()
                   ? tr( "（无）" )
                   : snapshot.source().canonicalSource );
  if ( !snapshot.source().subdataset.isEmpty() )
    source += row( tr( "子数据集" ), snapshot.source().subdataset );
  if ( !snapshot.source().authConfigId.isEmpty() )
    source += row( tr( "认证配置" ), snapshot.source().authConfigId );
  if ( !snapshot.source().dataOptions.isEmpty() )
  {
    QStringList opts;
    for ( auto it = snapshot.source().dataOptions.constBegin();
          it != snapshot.source().dataOptions.constEnd(); ++it )
      opts << QStringLiteral( "%1=%2" ).arg( it.key(), it.value() );
    source += row( tr( "数据选项" ), opts.join( QStringLiteral( "; " ) ) );
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
      provenanceRows += row( tr( "算法" ), record->algorithmId );
      if ( !record->algorithmVersion.isEmpty() )
        provenanceRows += row( tr( "算法版本" ), record->algorithmVersion );
      if ( !record->parameters.isEmpty() )
        provenanceRows += row( tr( "参数" ),
                               QString::fromUtf8(
                                 QJsonDocument( record->parameters ).toJson( QJsonDocument::Compact ) ) );
      if ( !record->taskReference.isEmpty() )
        provenanceRows += row( tr( "任务引用" ), record->taskReference );
      if ( record->completedAtUtc.isValid() )
        provenanceRows += row( tr( "完成时间" ), record->completedAtUtc.toString( Qt::ISODate ) );

      const QVector<sicnu::data::AssetId> inputs = m_dataManager->derivedFrom( snapshot.id() );
      if ( !inputs.isEmpty() )
      {
        QStringList names;
        for ( const sicnu::data::AssetId &id : inputs )
        {
          const auto input = m_dataManager->asset( id );
          names << ( input ? input->displayName() : id.toString() );
        }
        provenanceRows += row( tr( "源自" ), names.join( QStringLiteral( ", " ) ) );
      }
    }
    else
    {
      provenanceRows += row( tr( "溯源" ), tr( "无派生记录（直接注册）" ) );
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
      provenanceRows += row( tr( "派生产物" ), names.join( QStringLiteral( ", " ) ) );
    }
  }

  const QString body =
    QStringLiteral( "<h2>%1</h2>" ).arg( escapeHtml( snapshot.displayName() ) )
    + section( tr( "标识与状态" ), identity )
    + section( tr( "数据源" ), source )
    + section( tr( "溯源与谱系" ), provenanceRows )
    + formatStructure( snapshot.structure() );

  m_detailView->setHtml( wrapHtml( body ) );
}

void DataManagerPanel::showMultiSelectionDetails(
  const QList<sicnu::data::AssetId> &ids )
{
  if ( m_detailTitle )
    m_detailTitle->setText( tr( "多选 — %1 项" ).arg( ids.size() ) );

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
  summary += row( tr( "选中数量" ), QString::number( ids.size() ) );
  summary += row( tr( "就绪" ), QString::number( ready ) );
  summary += row( tr( "临时资产" ), QString::number( temporary ) );
  summary += row( tr( "栅格类" ), QString::number( raster ) );
  summary += row( tr( "矢量" ), QString::number( vector ) );

  const QString body =
    QStringLiteral( "<h2>%1</h2>" ).arg( escapeHtml( tr( "已选择 %1 个资产" ).arg( ids.size() ) ) )
    + section( tr( "汇总" ), summary )
    + QStringLiteral( "<h3>%1</h3><div class='block'>%2</div>" )
        .arg( escapeHtml( tr( "列表" ) ), list )
    + QStringLiteral( "<p style='color:#656d76'>%1</p>" )
        .arg( escapeHtml( tr( "右键可批量：添加到显示 / 提升 / 卸载。" ) ) );

  m_detailView->setHtml( wrapHtml( body ) );
}

void DataManagerPanel::showCollectionDetails(
  const sicnu::data::CollectionSnapshot &collection )
{
  if ( m_detailTitle )
    m_detailTitle->setText( tr( "集合元信息 — %1" ).arg( collection.displayName ) );

  QString identity;
  identity += row( tr( "显示名" ), collection.displayName );
  identity += row( tr( "集合 ID" ), collection.id.toString() );
  identity += row( tr( "子资产数" ),
                   QString::number( collection.childAssetIds.size() ) );

  QString product;
  const auto &md = collection.metadata;
  product += row( tr( "平台" ),
                  md.platform.isEmpty() ? tr( "（无）" ) : md.platform );
  product += row( tr( "传感器" ),
                  md.sensor.isEmpty() ? tr( "（无）" ) : md.sensor );
  product += row( tr( "产品级别" ),
                  md.productLevel.isEmpty() ? tr( "（无）" ) : md.productLevel );
  product += row( tr( "获取日期" ),
                  md.acquisitionDate.isEmpty() ? tr( "（无）" ) : md.acquisitionDate );
  product += row( tr( "处理级别" ),
                  md.processingLevel.isEmpty() ? tr( "（无）" ) : md.processingLevel );
  if ( !md.attributes.isEmpty() )
  {
    QStringList attrs;
    for ( auto it = md.attributes.constBegin(); it != md.attributes.constEnd(); ++it )
      attrs << QStringLiteral( "%1 = %2" ).arg( it.key(), it.value() );
    product += row( tr( "扩展属性" ), attrs.join( QStringLiteral( "\n" ) ) );
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
    children = escapeHtml( tr( "（无子资产）" ) );

  const QString body =
    QStringLiteral( "<h2>%1</h2>" ).arg( escapeHtml( collection.displayName ) )
    + section( tr( "集合" ), identity )
    + section( tr( "产品元数据" ), product )
    + QStringLiteral( "<h3>%1</h3><div class='block'>%2</div>" )
        .arg( escapeHtml( tr( "子资产" ) ), children );

  m_detailView->setHtml( wrapHtml( body ) );
}

void DataManagerPanel::clearDetails( const QString &message )
{
  if ( m_detailTitle )
    m_detailTitle->setText( tr( "元信息" ) );
  if ( m_detailView )
  {
    m_detailView->setHtml( wrapHtml(
      QStringLiteral( "<p style='color:#656d76'>%1</p>" )
        .arg( escapeHtml( message.isEmpty()
                            ? tr( "选择数据资产或集合以查看元信息。" )
                            : message ) ) ) );
  }
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

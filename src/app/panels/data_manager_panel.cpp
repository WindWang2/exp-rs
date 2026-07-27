#include "data_manager_panel.h"

#include <QApplication>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QSize>
#include <QSplitter>
#include <QStyle>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <variant>

#include "data/collection_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"

namespace sicnu
{

namespace
{

constexpr int kAssetIdRole = Qt::UserRole;
constexpr int kCollectionIdRole = Qt::UserRole + 1;
/// Human-readable label for icon-only columns (kind / status) — used by rowText().
constexpr int kColumnLabelRole = Qt::UserRole + 2;

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

QIcon statusIcon( sicnu::data::AssetState state )
{
  switch ( state )
  {
    case sicnu::data::AssetState::Ready:
      return appIcon( "v_lid_tion_ok" );
    case sicnu::data::AssetState::Registered:
      return styleIcon( QStyle::SP_FileDialogInfoView );
    case sicnu::data::AssetState::Resolving:
      return styleIcon( QStyle::SP_BrowserReload );
    case sicnu::data::AssetState::Missing:
    case sicnu::data::AssetState::UnavailableSource:
      return appIcon( "w_rning_l_bel" );
    case sicnu::data::AssetState::Offline:
      return appIcon( "cloud_sync" );
    case sicnu::data::AssetState::AuthenticationRequired:
      return styleIcon( QStyle::SP_MessageBoxQuestion );
    case sicnu::data::AssetState::Error:
      return styleIcon( QStyle::SP_MessageBoxCritical );
    case sicnu::data::AssetState::Stale:
      return appIcon( "w_rning_l_bel" );
  }
  return styleIcon( QStyle::SP_MessageBoxInformation );
}

void setIconColumn( QTreeWidgetItem *item, int column, const QIcon &icon, const QString &label )
{
  if ( !item )
    return;
  item->setText( column, QString() );
  item->setIcon( column, icon );
  item->setToolTip( column, label );
  item->setData( column, kColumnLabelRole, label );
  item->setTextAlignment( column, Qt::AlignCenter );
}

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
  m_tree->setColumnCount( 5 );
  m_tree->setHeaderLabels(
    { tr( "名称" ), tr( "类型" ), tr( "状态" ), tr( "持久性" ), tr( "引用" ) } );
  m_tree->setRootIsDecorated( true );
  m_tree->setSelectionMode( QAbstractItemView::SingleSelection );
  m_tree->setContextMenuPolicy( Qt::CustomContextMenu );
  m_tree->setIconSize( QSize( 18, 18 ) );
  m_tree->setUniformRowHeights( true );
  m_tree->header()->setStretchLastSection( false );
  m_tree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
  m_tree->header()->setSectionResizeMode( 1, QHeaderView::Fixed );
  m_tree->header()->setSectionResizeMode( 2, QHeaderView::Fixed );
  m_tree->header()->setSectionResizeMode( 3, QHeaderView::ResizeToContents );
  m_tree->header()->setSectionResizeMode( 4, QHeaderView::ResizeToContents );
  m_tree->setColumnWidth( 1, 44 );
  m_tree->setColumnWidth( 2, 44 );
  m_tree->headerItem()->setToolTip( 1, tr( "类型（悬停查看文字）" ) );
  m_tree->headerItem()->setToolTip( 2, tr( "状态（悬停查看文字）" ) );
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
  m_detailView->setMinimumWidth( 240 );
  detailLay->addWidget( m_detailView, 1 );

  m_splitter = new QSplitter( Qt::Horizontal, this );
  m_splitter->setObjectName( QStringLiteral( "dataManagerSplitter" ) );
  m_splitter->addWidget( m_tree );
  m_splitter->addWidget( detailHost );
  m_splitter->setStretchFactor( 0, 2 );
  m_splitter->setStretchFactor( 1, 3 );
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
}

int DataManagerPanel::rowCount() const
{
  return m_tree->topLevelItemCount();
}

QString DataManagerPanel::rowText( sicnu::data::AssetId id, int column ) const
{
  QTreeWidgetItemIterator it( m_tree );
  while ( *it )
  {
    if ( ( *it )->data( 0, kAssetIdRole ).toString() == id.toString() )
    {
      // Kind / status are icon-only; expose the label via tool-role for tests/API.
      if ( column == 1 || column == 2 )
      {
        const QString label = ( *it )->data( column, kColumnLabelRole ).toString();
        if ( !label.isEmpty() )
          return label;
      }
      return ( *it )->text( column );
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
  return assetForItem( m_tree->currentItem() );
}

void DataManagerPanel::selectAsset( sicnu::data::AssetId id )
{
  QTreeWidgetItemIterator it( m_tree );
  while ( *it )
  {
    if ( ( *it )->data( 0, kAssetIdRole ).toString() == id.toString() )
    {
      m_tree->setCurrentItem( *it );
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
  item->setText( 0, snapshot.displayName() );
  setIconColumn( item, 1, kindIcon( snapshot.kind() ), kindText( snapshot.kind() ) );
  const QString statusLabel = statusText( snapshot.state() );
  setIconColumn( item, 2, statusIcon( snapshot.state() ), statusLabel );
  item->setText( 3, persistenceText( snapshot.persistence() ) );
  item->setText( 4, QString::number( referenceCount( snapshot.id() ) ) );
  item->setData( 0, kAssetIdRole, snapshot.id().toString() );
  item->setToolTip( 0, snapshot.source().canonicalSource );
  if ( snapshot.state() == sicnu::data::AssetState::Missing )
  {
    item->setToolTip( 2, tr( "源缺失 — 可通过重定位恢复" ) );
    item->setData( 2, kColumnLabelRole, statusLabel );
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
    collectionItem->setText( 0, collection->displayName );
    setIconColumn( collectionItem, 1, appIcon( "d_t_b_se" ), tr( "集合" ) );
    setIconColumn( collectionItem, 2, styleIcon( QStyle::SP_DirIcon ), tr( "集合" ) );
    collectionItem->setText( 4, QString::number( collection->childAssetIds.size() ) );
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
  const sicnu::data::AssetId id = assetForItem( item );
  if ( !id.isNull() )
    emit displayRequested( id );
}

void DataManagerPanel::onContextMenu( const QPoint &pos )
{
  QTreeWidgetItem *item = m_tree->itemAt( pos );
  const sicnu::data::AssetId id = assetForItem( item );
  if ( id.isNull() )
    return;

  QMenu menu( this );
  QAction *displayAction = menu.addAction( tr( "添加到显示" ) );
  QAction *promoteAction = menu.addAction( tr( "提升为工程持久…" ) );
  promoteAction->setEnabled( isPromotable( id ) );
  QAction *unloadAction = menu.addAction( tr( "卸载…" ) );

  QAction *chosen = menu.exec( m_tree->viewport()->mapToGlobal( pos ) );
  if ( chosen == displayAction )
    emit displayRequested( id );
  else if ( chosen == promoteAction )
    emit promoteRequested( id );
  else if ( chosen == unloadAction )
    emit unloadRequested( id );
}

void DataManagerPanel::onSelectionChanged()
{
  QTreeWidgetItem *item = m_tree->currentItem();
  if ( !item || !m_dataManager )
  {
    clearDetails( tr( "选择数据资产或集合以查看元信息。" ) );
    return;
  }

  const sicnu::data::AssetId assetId = assetForItem( item );
  if ( !assetId.isNull() )
  {
    const auto snapshot = m_dataManager->asset( assetId );
    if ( snapshot )
    {
      showAssetDetails( *snapshot );
      return;
    }
  }

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

  clearDetails( tr( "选择数据资产或集合以查看元信息。" ) );
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

  const QString body =
    QStringLiteral( "<h2>%1</h2>" ).arg( escapeHtml( snapshot.displayName() ) )
    + section( tr( "标识与状态" ), identity )
    + section( tr( "数据源" ), source )
    + formatStructure( snapshot.structure() );

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

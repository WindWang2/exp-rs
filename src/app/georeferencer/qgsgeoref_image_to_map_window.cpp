#include "qgsgeoref_image_to_map_window.h"

#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QStringList>
#include <QToolBar>
#include <QWidget>

#include "core/sicnu_logging.h"
#include "qgsgcptransformer.h"
#include "qgslayertree.h"
#include "qgsmapcanvas.h"
#include "qgsmaplayer.h"
#include "qgsproject.h"

QgsGeorefImageToMapWindow::QgsGeorefImageToMapWindow( QgisInterface *iface, QWidget *parent )
  : QgsGeorefShellWindow( iface, parent )
{
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QStringLiteral( "Image 2 Map georef window opened" ) );
  setWindowTitle( tr( "Image Registration · Image 2 Map" ) );
  resize( 1100, 900 );

  setupMenus();
  setupToolbars();
  setupStatusBar( QStringLiteral( "rsGeorefI2MCoordLabel" ),
                  QStringLiteral( "rsGeorefI2MCrsLabel" ),
                  QStringLiteral( "rsGeorefI2MRmsLabel" ) );
  setupCentralWidget();
  finishCommonSetup( RsGeorefParamsPanel::Profile::ImageToMap,
                     QStringLiteral( "rsGcpDockI2M" ),
                     QStringLiteral( "rsParamDockI2M" ) );

  connect( QgsProject::instance(), &QgsProject::layersAdded, this,
           [this]( const QList<QgsMapLayer *> & ) { refreshMapLayersFromProject(); } );
  connect( QgsProject::instance(),
           qOverload<const QStringList &>( &QgsProject::layersWillBeRemoved ), this,
           [this]( const QStringList & ) { refreshMapLayersFromProject(); } );
  if ( QgsLayerTree *root = QgsProject::instance()->layerTreeRoot() )
  {
    connect( root, &QgsLayerTreeNode::visibilityChanged, this,
             [this]( QgsLayerTreeNode * ) { refreshMapLayersFromProject(); } );
  }
  refreshMapLayersFromProject();
}

void QgsGeorefImageToMapWindow::setupCentralWidget()
{
  auto *splitter = new QSplitter( Qt::Vertical, this );
  splitter->setObjectName( QStringLiteral( "rsGeorefI2MSplitter" ) );

  mSrcCanvas = new QgsMapCanvas( this );
  mSrcCanvas->setObjectName( QStringLiteral( "rsGeorefI2MSrcCanvas" ) );
  mSrcCanvas->setCanvasColor( Qt::white );
  mSrcCanvas->setToolTip( tr(
    "源影像画布 (SRC / Warp)：加载待校正影像。\n"
    "Add GCP 时先在此点击源点，再在下方 Map 点击同名位置。" ) );

  mDstCanvas = new QgsMapCanvas( this );
  mDstCanvas->setObjectName( QStringLiteral( "rsGeorefI2MMapCanvas" ) );
  mDstCanvas->setCanvasColor( QColor( 245, 245, 245 ) );
  mDstCanvas->setToolTip( tr(
    "地图预览 (Map / Base)：镜像主工程中可见图层。\n"
    "Add GCP 时在此点击与源点对应的地图位置（地理坐标），完成一对控制点。" ) );

  QWidget *srcPanel = makeCanvasPanel(
    mSrcCanvas, &mSrcLayerLabel,
    tr( "源 (Warp)" ),
    QStringLiteral( "rsGeorefI2MSrcPanel" ),
    QStringLiteral( "rsGeorefI2MSrcLayerLabel" ) );
  QWidget *mapPanel = makeCanvasPanel(
    mDstCanvas, &mDstLayerLabel,
    tr( "基准 (Base)" ),
    QStringLiteral( "rsGeorefI2MMapPanel" ),
    QStringLiteral( "rsGeorefI2MMapLayerLabel" ) );
  updateSourceLayerCaption();
  updateDestLayerCaption( QString() );

  splitter->addWidget( srcPanel );
  splitter->addWidget( mapPanel );
  splitter->setStretchFactor( 0, 1 );
  splitter->setStretchFactor( 1, 1 );
  setCentralWidget( splitter );
}

void QgsGeorefImageToMapWindow::setupMenus()
{
  QMenu *fileMenu = createFileMenu();
  auto *refresh = fileMenu->addAction( tr( "Refresh map layers" ),
                       this, &QgsGeorefImageToMapWindow::refreshMapLayersFromProject );
  refresh->setToolTip( tr( "重新同步主工程可见图层到下方 Map 画布。" ) );
  refresh->setStatusTip( refresh->toolTip() );
  fileMenu->addSeparator();
  auto *loadPts = fileMenu->addAction( tr( "Load .points..." ), this, &QgsGeorefShellWindow::loadPoints );
  loadPts->setToolTip( tr( "导入已保存的控制点文件。" ) );
  auto *savePts = fileMenu->addAction( tr( "Save .points..." ), this, &QgsGeorefShellWindow::savePoints );
  savePts->setToolTip( tr( "导出当前控制点。" ) );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close )->setToolTip( tr( "关闭本窗口（不影响 Image 2 Image）。" ) );
  addStandardMenuBar();
}

void QgsGeorefImageToMapWindow::setupToolbars()
{
  mToolBar = addToolBar( tr( "Tools" ) );
  mToolBar->setObjectName( QStringLiteral( "rsGeorefI2MToolBar" ) );
  mToolBar->setMovable( false );
  mToolBar->setToolTip( tr( "Image 2 Map 工具：对主地图取点、RPC 方法与运行。" ) );

  addGcpEditActions( mToolBar, QStringLiteral( "rsGeorefI2M" ) );
  mToolBar->addSeparator();
  auto *refresh = mToolBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Refresh map" ),
                       this, &QgsGeorefImageToMapWindow::refreshMapLayersFromProject );
  refresh->setToolTip( tr(
    "刷新地图：将主窗口工程中当前可见图层同步到 Map 预览画布。"
    "主图增删图层或改可见性后点此更新。" ) );
  refresh->setStatusTip( refresh->toolTip() );
  refresh->setWhatsThis( refresh->toolTip() );

  addApplyAction( mToolBar, QStringLiteral( "rsGeorefI2MApplyAction" ) );
}

QString QgsGeorefImageToMapWindow::windowHelpText() const
{
  return tr(
    "<b>Image Registration · Image 2 Map</b><br>"
    "源影像对主工程地图：上方 SRC，下方 Map（主图可见图层镜像）。<br><br>"
    "<b>典型流程</b><br>"
    "1. 主窗口加载已有地理参考底图/矢量<br>"
    "2. 本窗口 File → Open source raster<br>"
    "3. Add GCP：先在 SRC 点源点，再在 Map 点目标位置（双画布点选，不弹坐标表单）<br>"
    "4. 变换方法可选 <b>RPC Physical</b>（需源含 RPC；可配 DEM）<br>"
    "5. 设置输出路径 → 运行 → 任务列表查看/加载结果<br><br>"
    "无 SIFT、无「打开参考影像」。悬停工具与参数可看说明。" );
}

void QgsGeorefImageToMapWindow::refreshMapLayersFromProject()
{
  if ( !mDstCanvas )
    return;

  QList<QgsMapLayer *> layers;
  if ( QgsLayerTree *root = QgsProject::instance()->layerTreeRoot() )
  {
    for ( QgsMapLayer *l : root->checkedLayers() )
    {
      if ( l && l->isValid() )
        layers << l;
    }
  }
  else
  {
    for ( QgsMapLayer *l : QgsProject::instance()->mapLayers().values() )
    {
      if ( l && l->isValid() )
        layers << l;
    }
  }

  mDstCanvas->setLayers( layers );
  if ( mParamsPanel && mParamsPanel->destCrs().isValid() )
    mDstCanvas->setDestinationCrs( mParamsPanel->destCrs() );
  else if ( QgsProject::instance()->crs().isValid() )
    mDstCanvas->setDestinationCrs( QgsProject::instance()->crs() );
  mDstCanvas->refresh();

  // Caption: list visible project layer names as the Map / Base stack.
  if ( layers.isEmpty() )
  {
    updateDestLayerCaption( QString() );
  }
  else
  {
    QStringList names;
    QStringList tipLines;
    names.reserve( layers.size() );
    for ( QgsMapLayer *l : layers )
    {
      if ( !l )
        continue;
      names << l->name();
      tipLines << QStringLiteral( "%1\n  %2" ).arg( l->name(), l->source() );
    }
    QString display = names.join( QStringLiteral( ", " ) );
    constexpr int kMaxCaption = 80;
    if ( display.size() > kMaxCaption )
      display = display.left( kMaxCaption - 1 ) + QChar( 0x2026 ); // …
    updateDestLayerCaption(
      display,
      tr( "地图基准 (Base) — 主工程可见图层 (%1):\n%2" )
        .arg( names.size() )
        .arg( tipLines.join( QLatin1Char( '\n' ) ) ) );
  }
  updateSourceLayerCaption();
}

void QgsGeorefImageToMapWindow::onTransformMethodChangedExtra()
{
  if ( !mParamsPanel )
    return;
  mParamsPanel->setRpcMode(
    mParamsPanel->transformMethod() ==
    QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
}

void QgsGeorefImageToMapWindow::captureShellSpecific( RsGeorefSessionState::WorkflowSnapshot &s ) const
{
  s.mode = 0;
  s.syncZoom = false;
}

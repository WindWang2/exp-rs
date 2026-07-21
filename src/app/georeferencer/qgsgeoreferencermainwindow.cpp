#include "qgsgeoreferencermainwindow.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

#include "core/sicnu_logging.h"
#include "qgis.h"
#include "qgsapplication.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsgcplist.h"
#include "qgsgcppoint.h"
#include "qgsmapcanvas.h"
#include "qgsmessagelog.h"
#include "qgsrasterlayer.h"
#include "qgstaskmanager.h"
#include "rs_sift_dialog.h"
#include "rs_sift_task.h"
#include "rs_twincanvas_sync_controller.h"

QgsGeoreferencerMainWindow::QgsGeoreferencerMainWindow( QgisInterface *iface, QWidget *parent )
  : QgsGeorefShellWindow( iface, parent )
{
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QStringLiteral( "Image 2 Image georef window opened" ) );
  setWindowTitle( tr( "Image Registration · Image 2 Image" ) );
  resize( 1200, 800 );

  setupMenus();
  setupToolbars();
  setupStatusBar( QStringLiteral( "rsGeorefCoordLabel" ),
                  QStringLiteral( "rsGeorefCrsLabel" ),
                  QStringLiteral( "rsGeorefRmsLabel" ) );
  setupCentralWidget();
  finishCommonSetup( RsGeorefParamsPanel::Profile::ImageToImage,
                     QStringLiteral( "rsGcpDock" ),
                     QStringLiteral( "rsParamDock" ) );
}

void QgsGeoreferencerMainWindow::setupCentralWidget()
{
  auto *split = new QSplitter( Qt::Horizontal, this );
  split->setObjectName( QStringLiteral( "rsGeorefSplitter" ) );

  mSrcCanvas = new QgsMapCanvas( this );
  mSrcCanvas->setObjectName( QStringLiteral( "rsSrcCanvas" ) );
  mSrcCanvas->setCanvasColor( Qt::white );
  mSrcCanvas->setToolTip( tr(
    "源影像画布 (SRC / Warp)：加载待校正影像。\n"
    "Add GCP 时先在此点击源点，再在右侧 REF 点击同名点（不弹坐标表单）。" ) );

  mDstCanvas = new QgsMapCanvas( this );
  mDstCanvas->setObjectName( QStringLiteral( "rsRefCanvas" ) );
  mDstCanvas->setCanvasColor( Qt::white );
  mDstCanvas->setToolTip( tr(
    "参考影像画布 (REF / Base)：加载已配准参考影像。\n"
    "Add GCP 时在此点击与源点对应的同名位置，完成一对控制点。" ) );

  QWidget *srcPanel = makeCanvasPanel(
    mSrcCanvas, &mSrcLayerLabel,
    tr( "源 (Warp)" ),
    QStringLiteral( "rsSrcCanvasPanel" ),
    QStringLiteral( "rsSrcLayerLabel" ) );
  QWidget *refPanel = makeCanvasPanel(
    mDstCanvas, &mDstLayerLabel,
    tr( "基准 (Base)" ),
    QStringLiteral( "rsRefCanvasPanel" ),
    QStringLiteral( "rsRefLayerLabel" ) );
  // Role-specific empty captions (makeCanvasPanel used role as prefix once).
  updateSourceLayerCaption();
  updateDestLayerCaption( QString() );

  split->addWidget( srcPanel );
  split->addWidget( refPanel );
  split->setStretchFactor( 0, 1 );
  split->setStretchFactor( 1, 1 );
  setCentralWidget( split );

  mSyncCtl = new RsTwinCanvasSyncController( mSrcCanvas, mDstCanvas, this );
  // Default OFF: SRC is often pixel/local while REF is map CRS — linking extents
  // makes source picks land at wrong coords (e.g. all zeros / off-image).
  if ( mSyncCtl )
    mSyncCtl->setEnabled( false );
  if ( mSyncZoomAction )
  {
    mSyncZoomAction->setCheckable( true );
    mSyncZoomAction->setChecked( false );
    mSyncZoomAction->setToolTip( tr(
      "同步缩放：仅在 SRC 与 REF 坐标单位/范围相近时建议开启。\n"
      "源为像素、参考为地图坐标时请保持关闭，否则取点会错位。" ) );
    connect( mSyncZoomAction, &QAction::toggled, this, [this]( bool on ) {
      if ( mSyncCtl )
        mSyncCtl->setEnabled( on );
    } );
  }
}

void QgsGeoreferencerMainWindow::setupMenus()
{
  QMenu *fileMenu = createFileMenu();
  fileMenu->addSeparator();
  mOpenRefFileAction = fileMenu->addAction(
    tr( "Load reference raster from file..." ),
    this, QOverload<>::of( &QgsGeoreferencerMainWindow::loadReferenceRaster ) );
  mOpenRefFileAction->setToolTip( tr(
    "从文件打开参考影像到右侧 REF（Base），作为 GCP 目标与对齐基准。" ) );
  mOpenRefFileAction->setStatusTip( mOpenRefFileAction->toolTip() );
  mOpenRefLayerAction = fileMenu->addAction(
    tr( "Load reference from project layer..." ),
    this, &QgsGeoreferencerMainWindow::loadReferenceFromProjectLayer );
  mOpenRefLayerAction->setToolTip( tr(
    "从主工程图层列表选择栅格作为参考影像（Base）。" ) );
  mOpenRefLayerAction->setStatusTip( mOpenRefLayerAction->toolTip() );
  fileMenu->addSeparator();
  auto *loadPts = fileMenu->addAction( tr( "Load .points..." ), this, &QgsGeorefShellWindow::loadPoints );
  loadPts->setToolTip( tr( "导入已保存的控制点文件。" ) );
  auto *savePts = fileMenu->addAction( tr( "Save .points..." ), this, &QgsGeorefShellWindow::savePoints );
  savePts->setToolTip( tr( "导出当前控制点，关闭窗口前若有未保存更改也会提示。" ) );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close )->setToolTip( tr( "关闭本窗口（不影响 Image 2 Map）。" ) );
  addStandardMenuBar();
}

void QgsGeoreferencerMainWindow::setupToolbars()
{
  mToolBar = addToolBar( tr( "Tools" ) );
  mToolBar->setObjectName( QStringLiteral( "rsGeorefToolBar" ) );
  mToolBar->setMovable( false );
  mToolBar->setToolTip( tr( "Image 2 Image 工具：双影像配准、SIFT 自动匹配与运行。" ) );

  addGcpEditActions( mToolBar, QStringLiteral( "rsGeoref" ) );
  mToolBar->addSeparator();

  mSyncZoomAction = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Sync zoom" ) );
  mSyncZoomAction->setObjectName( QStringLiteral( "rsGeorefSyncZoomAction" ) );
  mSyncZoomAction->setToolTip( tr(
    "同步缩放：开启后 SRC 与 REF 画布联动缩放/平移，便于对照同名地物取点。" ) );
  mSyncZoomAction->setStatusTip( mSyncZoomAction->toolTip() );
  mSyncZoomAction->setWhatsThis( mSyncZoomAction->toolTip() );

  mSiftAction = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "Auto match (SIFT)" ),
    this, &QgsGeoreferencerMainWindow::runSiftMatch );
  mSiftAction->setObjectName( QStringLiteral( "rsGeorefSiftAction" ) );
  mSiftAction->setToolTip( tr(
    "SIFT 自动匹配：需已打开 SRC 与参考影像。提取特征并筛选内点后，可批量添加 GCP。\n"
    "需要 OpenCV；仅 Image 2 Image 提供。" ) );
  mSiftAction->setStatusTip( mSiftAction->toolTip() );
  mSiftAction->setWhatsThis( mSiftAction->toolTip() );

  addApplyAction( mToolBar, QStringLiteral( "rsGeorefApplyAction" ) );
}

QString QgsGeoreferencerMainWindow::windowHelpText() const
{
  return tr(
    "<b>Image Registration · Image 2 Image</b><br>"
    "双影像配准：左侧源影像 (Warp)，右侧参考影像 (Base)。<br><br>"
    "<b>典型流程</b><br>"
    "1. 打开源影像：从文件 或 从主工程图层<br>"
    "2. 打开参考影像：从文件 或 从主工程图层<br>"
    "3. 两侧都打开后，Add / Move / Delete GCP 才可用<br>"
    "4. 点选 Add GCP：先 SRC 再 REF（右键取消未完成源点）<br>"
    "5. 可选：SIFT、Sync zoom → 设置输出 → 运行<br><br>"
    "不含 RPC（RPC 请用 Image 2 Map）。" );
}

void QgsGeoreferencerMainWindow::runSiftMatch()
{
#ifndef SICNU_HAS_OPENCV
  statusBar()->showMessage( tr( "OpenCV 不可用 — SIFT 已禁用" ), 5000 );
  return;
#else
  if ( !mRefRaster )
  {
    statusBar()->showMessage( tr( "请先 File → Load reference raster…" ), 5000 );
    return;
  }
  if ( mSourceRasterPath.isEmpty() )
  {
    statusBar()->showMessage( tr( "请先打开 SRC 影像" ), 5000 );
    return;
  }
  RsSiftDialog dlg( this );
  if ( dlg.exec() != QDialog::Accepted )
    return;

  auto *task = new RsSiftTask( mSourceRasterPath,
                               mRefRaster->source(),
                               mParamsPanel->destCrs(),
                               dlg.params() );
  connect( task, &QgsTask::taskCompleted, this, [this, task]() {
    const auto r = task->result();
    if ( !r.ok() )
    {
      statusBar()->showMessage( tr( "SIFT 失败：%1" ).arg( r.errorMessage ), 5000 );
      return;
    }
    const QString msg = tr( "找到 %1 对匹配，内点 %2 个 (%3%)，是否全部采用？" )
                          .arg( r.totalMatches )
                          .arg( r.inliers.size() )
                          .arg( int( r.inlierRatio * 100 ) );
    if ( QMessageBox::question( this, tr( "SIFT 匹配结果" ), msg ) != QMessageBox::Yes )
      return;
    const QgsCoordinateReferenceSystem destCrs = mParamsPanel->destCrs();
    for ( const auto &m : r.inliers )
      mGcps->appendPoint( QgsGcpPoint( m.srcPx, m.dstWorld, destCrs, true ) );
    QJsonObject o {
      { QStringLiteral( "event" ),        QStringLiteral( "sift_match" ) },
      { QStringLiteral( "matches" ),      r.totalMatches },
      { QStringLiteral( "inliers" ),      int( r.inliers.size() ) },
      { QStringLiteral( "inlier_ratio" ), r.inlierRatio },
    };
    QgsMessageLog::logMessage(
      QString::fromUtf8( QJsonDocument( o ).toJson( QJsonDocument::Compact ) ),
      QStringLiteral( "Georeferencer" ),
      Qgis::MessageLevel::Info );
  } );
  QgsApplication::taskManager()->addTask( task );
  statusBar()->showMessage( tr( "SIFT 匹配中…" ), 3000 );
#endif
}

void QgsGeoreferencerMainWindow::loadReferenceRaster()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Load reference raster" ), QString(),
    tr( "Raster (*.tif *.tiff *.img *.jp2);;All files (*)" ) );
  if ( path.isEmpty() )
    return;
  loadReferenceRaster( path );
}

void QgsGeoreferencerMainWindow::loadReferenceFromProjectLayer()
{
  QgsRasterLayer *picked = pickProjectRasterLayer(
    tr( "从主工程选择参考影像 (Base)" ) );
  if ( !picked )
    return;
  loadReferenceRaster( picked->source() );
  // Prefer project layer display name on caption if path load used basename.
  if ( mRefRaster && mRefRaster->isValid() && !picked->name().isEmpty() )
  {
    // Layer was created with file basename; caption already set — refresh name tip.
    updateDestLayerCaption(
      picked->name(),
      tr( "参考影像（基准 / Base）— 来自主工程图层\n图层: %1\n路径: %2" )
        .arg( picked->name(), picked->source() ) );
  }
}

bool QgsGeoreferencerMainWindow::loadReferenceRaster( const QString &path )
{
  auto *layer = new QgsRasterLayer( path, QFileInfo( path ).completeBaseName(), QStringLiteral( "gdal" ) );
  if ( !layer->isValid() )
  {
    delete layer;
    layer = new QgsRasterLayer( path, QFileInfo( path ).completeBaseName() );
  }
  if ( !layer->isValid() )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, QString( "Failed to open reference raster: %1" ).arg( path ) );
    delete layer;
    if ( statusBar() )
      statusBar()->showMessage( tr( "无法打开参考影像: %1" ).arg( path ), 5000 );
    return false;
  }
  if ( mLayerStore )
    mLayerStore->addMapLayer( layer );
  mRefRaster = layer;
  mRefRasterPath = path;

  if ( mDstCanvas )
  {
    if ( layer->crs().isValid() )
      mDstCanvas->setDestinationCrs( layer->crs() );
    mDstCanvas->setLayers( { layer } );
    mDstCanvas->setExtent( layer->extent() );
    mDstCanvas->refresh();
  }
  // Align target CRS with reference image when panel CRS is still unset.
  if ( mParamsPanel && layer->crs().isValid() && !mParamsPanel->destCrs().isValid() )
    mParamsPanel->setDestCrs( layer->crs() );

  updateDestLayerCaption(
    layer->name(),
    tr( "参考影像（基准 / Base）\n图层: %1\n路径: %2" )
      .arg( layer->name(), path ) );
  updateToolAvailability();
  recomputeFit();
  mSession.saveWorkflow( captureWorkflowSnapshot() );
  if ( statusBar() )
    statusBar()->showMessage( tr( "已加载参考影像 (Base): %1" ).arg( layer->name() ), 4000 );
  return true;
}

bool QgsGeoreferencerMainWindow::hasDestReady() const
{
  return mRefRaster && mRefRaster->isValid();
}

void QgsGeoreferencerMainWindow::updateToolAvailability()
{
  QgsGeorefShellWindow::updateToolAvailability();
  if ( mSiftAction )
    mSiftAction->setEnabled( hasSourceReady() && hasDestReady() );
  if ( mSyncZoomAction )
    mSyncZoomAction->setEnabled( hasSourceReady() && hasDestReady() );
}

void QgsGeoreferencerMainWindow::captureShellSpecific( RsGeorefSessionState::WorkflowSnapshot &s ) const
{
  s.mode = static_cast<int>( RsGeorefModeToggle::ImageToImage );
  s.lastRefPath = mRefRasterPath;
  s.syncZoom = mSyncZoomAction ? mSyncZoomAction->isChecked() : true;
}

void QgsGeoreferencerMainWindow::applyShellSpecific( const RsGeorefSessionState::WorkflowSnapshot &s )
{
  if ( !s.lastRefPath.isEmpty() )
  {
    mRefRasterPath = s.lastRefPath;
    // Caption only — full layer reload is user-driven if store was empty.
    updateDestLayerCaption(
      QFileInfo( s.lastRefPath ).fileName(),
      tr( "参考影像（基准 / Base）\n路径: %1" ).arg( s.lastRefPath ) );
  }
  if ( mSyncZoomAction )
    mSyncZoomAction->setChecked( s.syncZoom );
  updateSourceLayerCaption();
}

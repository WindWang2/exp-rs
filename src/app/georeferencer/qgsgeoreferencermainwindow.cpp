#include "qgsgeoreferencermainwindow.h"
#include "shell/rs_session_map_workspace.h"

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
#include "dialogs/dialog_help_catalog.h"
#include "qgis.h"
#include "qgsapplication.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsmapcanvas.h"
#include "qgsmessagelog.h"
#include "qgsrasterlayer.h"
#include "rs_sift_dialog.h"
#include "rs_sift_task.h"
#include "rs_template_match_dialog.h"
#include "rs_template_matcher.h"
#include "rs_twincanvas_sync_controller.h"

#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/task_center.h"

#include "qgsfeedback.h"

QgsGeoreferencerMainWindow::QgsGeoreferencerMainWindow( QgisInterface *iface, QWidget *parent )
  : QgsGeorefShellWindow( iface, parent )
{
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QStringLiteral( "Image 2 Image georef window opened" ) );
  setWindowTitle( tr( "Image Registration · Image 2 Image" ) );
  resize( 1200, 800 );
  setWhatsThis( SicnuDialogHelp::htmlForTool( QStringLiteral( "georef_i2i" ), windowTitle() ) );
  setToolTip( SicnuDialogHelp::shortForTool( QStringLiteral( "georef_i2i" ), windowTitle() ) );

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
  // Default OFF: linking extents across different CRS/geotransforms corrupts
  // dual-canvas map picks (e.g. REF X becoming large negative).
  if ( mSyncCtl )
    mSyncCtl->setEnabled( false );
  if ( mSyncZoomAction )
  {
    mSyncZoomAction->setCheckable( true );
    mSyncZoomAction->setChecked( false );
    mSyncZoomAction->setToolTip( tr(
      "同步缩放（默认关闭）：仅当 SRC 与 REF 为同一 CRS 且范围相近时使用。\n"
      "已配准影像对请保持关闭，否则取点坐标会错乱、残差异常。" ) );
    connect( mSyncZoomAction, &QAction::toggled, this, [this]( bool on ) {
      if ( mSyncCtl )
        mSyncCtl->setEnabled( on );
      if ( on && statusBar() )
        statusBar()->showMessage(
          tr( "已开启 Sync zoom — 请确认两侧 CRS 一致，否则 GCP 坐标可能错误" ), 6000 );
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
  mToolBar->setToolTip( tr( "Image 2 Image 工具：导航、双影像配准、SIFT 与运行。" ) );

  addCanvasNavigationActions( mToolBar, QStringLiteral( "rsGeoref" ) );
  mToolBar->addSeparator();
  addGcpEditActions( mToolBar, QStringLiteral( "rsGeoref" ) );
  mToolBar->addSeparator();

  mSyncZoomAction = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "同步缩放" ) );
  mSyncZoomAction->setObjectName( QStringLiteral( "rsGeorefSyncZoomAction" ) );
  mSyncZoomAction->setToolTip( tr(
    "同步缩放（默认关）：两侧 CRS 一致且范围相近时才建议开启。\n"
    "不同 CRS 时联动会弄乱取点坐标。" ) );
  mSyncZoomAction->setStatusTip( mSyncZoomAction->toolTip() );
  mSyncZoomAction->setWhatsThis( mSyncZoomAction->toolTip() );

  mSiftAction = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "SIFT 自动匹配" ),
    this, &QgsGeoreferencerMainWindow::runSiftMatch );
  mSiftAction->setObjectName( QStringLiteral( "rsGeorefSiftAction" ) );
  mSiftAction->setToolTip( tr(
    "SIFT 自动匹配：需已打开 SRC 与参考影像。提取特征并筛选内点后，可批量添加 GCP。\n"
    "需要 OpenCV；仅 Image 2 Image 提供。" ) );
  mSiftAction->setStatusTip( mSiftAction->toolTip() );
  mSiftAction->setWhatsThis( mSiftAction->toolTip() );

  mTemplateMatchAction = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/select" ) ),
    tr( "模板匹配" ),
    this, &QgsGeoreferencerMainWindow::runTemplateMatch );
  mTemplateMatchAction->setObjectName( QStringLiteral( "rsGeorefTemplateMatchAction" ) );
  mTemplateMatchAction->setToolTip( tr(
    "模板匹配（NCC）：利用源影像初始地理坐标预测参考影像搜索区，再做相关匹配。\n"
    "适合已有近似坐标的遥感影像；可网格采样或用现有粗 GCP 作种子。需要 OpenCV。" ) );
  mTemplateMatchAction->setStatusTip( mTemplateMatchAction->toolTip() );
  mTemplateMatchAction->setWhatsThis( mTemplateMatchAction->toolTip() );

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
    "4. 导航：平移 / 放大 / 缩小；适合源 / 适合参考 / 适合两侧<br>"
    "5. 点选 Add GCP：先 SRC 再 REF（右键取消未完成源点）<br>"
    "6. 可选：模板匹配（需 SRC 初始坐标）/ SIFT、Sync zoom → 设置输出 → 运行<br><br>"
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

  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:georef:sift";
  req.title = tr( "SIFT 匹配" ).toStdString();
  req.source = "module";
  req.exclusive = true;

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    [task]( const sicnu::jobs::JobRequest &,
            sicnu::operators::RSOperatorContext &ctx ) {
      ctx.logInfo( "Running SIFT matching" );
      ctx.reportProgress( 0.0, "SIFT" );
      const bool ok = task->run();
      if ( ctx.isCancelled() || !ok )
      {
        if ( ctx.isCancelled() || task->result().errorMessage.contains( QStringLiteral( "cancel" ), Qt::CaseInsensitive ) )
        {
          throw sicnu::operators::RSOperatorError(
            sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
        }
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::ComputationError,
          task->result().errorMessage.isEmpty()
            ? "SIFT failed"
            : task->result().errorMessage.toStdString() );
      }
      Json::Value result( Json::objectValue );
      result["totalMatches"] = task->result().totalMatches;
      result["inliers"] = static_cast<int>( task->result().inliers.size() );
      result["inlierRatio"] = task->result().inlierRatio;
      return result;
    },
    [task]() { task->cancel(); },
    /*autoLoad=*/false );

  auto *conn = new QMetaObject::Connection;
  *conn = connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated, this,
                   [this, task, taskId, conn]( const sicnu::AlgorithmTaskInfo &info ) {
                     if ( info.taskId != taskId )
                       return;
                     if ( info.status != sicnu::TaskStatus::Completed
                          && info.status != sicnu::TaskStatus::Failed
                          && info.status != sicnu::TaskStatus::Canceled )
                       return;
                     disconnect( *conn );
                     delete conn;

                     const auto r = task->result();
                     task->deleteLater();

                     if ( info.status == sicnu::TaskStatus::Canceled )
                     {
                       statusBar()->showMessage( tr( "SIFT 已取消" ), 3000 );
                       return;
                     }
                     if ( info.status != sicnu::TaskStatus::Completed || !r.ok() )
                     {
                       statusBar()->showMessage(
                         tr( "SIFT 失败：%1" )
                           .arg( r.errorMessage.isEmpty()
                                   ? ( !info.errorMessage.isEmpty() ? info.errorMessage
                                                                   : tr( "未知错误" ) )
                                   : r.errorMessage ),
                         5000 );
                       return;
                     }

                     const QString msg = tr( "找到 %1 对匹配，内点 %2 个 (%3%)，是否全部采用？" )
                                           .arg( r.totalMatches )
                                           .arg( r.inliers.size() )
                                           .arg( int( r.inlierRatio * 100 ) );
                     if ( QMessageBox::question( this, tr( "SIFT 匹配结果" ), msg ) != QMessageBox::Yes )
                       return;
                      QVector<QgsGcpPoint> pairs;
                      pairs.reserve( r.inliers.size() );
                      const QgsCoordinateReferenceSystem destCrs = mParamsPanel->destCrs();
                      auto srcPxToMap = [this]( const QgsPointXY &px ) -> QgsPointXY {
                        if ( mSrcRaster && mSrcRaster->isValid() )
                        {
                          const auto extent = mSrcRaster->extent();
                          const int w = mSrcRaster->width();
                          const int h = mSrcRaster->height();
                          if ( w > 0 && h > 0 && extent.width() > 0 && extent.height() > 0 )
                          {
                            const double resX = extent.width() / w;
                            const double resY = extent.height() / h;
                            return QgsPointXY( extent.xMinimum() + px.x() * resX,
                                               extent.yMaximum() - px.y() * resY );
                          }
                        }
                        return QgsPointXY( px.x(), -px.y() );
                      };
                      for ( const auto &m : r.inliers )
                      {
                        pairs.append( QgsGcpPoint( srcPxToMap( m.srcPx ), m.dstWorld, destCrs, true ) );
                      }
                      // Accepted matches go straight into the session (sole GCP owner).
                      georefSession().appendGcps( pairs );
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

  statusBar()->showMessage( tr( "SIFT 匹配中…" ), 3000 );
#endif
}

void QgsGeoreferencerMainWindow::runTemplateMatch()
{
#ifndef SICNU_HAS_OPENCV
  statusBar()->showMessage( tr( "OpenCV 不可用 — 模板匹配已禁用" ), 5000 );
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

  RsTemplateMatchDialog dlg( this );
  if ( dlg.exec() != QDialog::Accepted )
    return;

  RsTemplateMatcher::Params params = dlg.params();
  QVector<QgsPointXY> seeds;
  if ( params.seedMode == RsTemplateMatcher::SeedMode::ExistingSeeds )
  {
    if ( georefSession().gcps().isEmpty() )
    {
      statusBar()->showMessage( tr( "种子模式需要至少一个已有 GCP" ), 5000 );
      return;
    }
    // Enabled source points of the session's GCP list are the match seeds.
    for ( const QgsGcpPoint &g : georefSession().gcps() )
    {
      if ( g.isEnabled() )
      {
        const QgsPointXY srcPt = g.sourcePoint();
        QgsPointXY srcPx = srcPt;
        if ( mSrcRaster && mSrcRaster->isValid() )
        {
          const auto extent = mSrcRaster->extent();
          const int w = mSrcRaster->width();
          const int h = mSrcRaster->height();
          if ( w > 0 && h > 0 && extent.width() > 0 && extent.height() > 0 )
          {
            const double resX = extent.width() / w;
            const double resY = extent.height() / h;
            const double col = ( srcPt.x() - extent.xMinimum() ) / resX;
            const double row = ( extent.yMaximum() - srcPt.y() ) / resY;
            srcPx = QgsPointXY( col, row );
          }
          else
          {
            srcPx = QgsPointXY( srcPt.x(), -srcPt.y() );
          }
        }
        else
        {
          srcPx = QgsPointXY( srcPt.x(), -srcPt.y() );
        }
        seeds.append( srcPx );
      }
    }
    if ( seeds.isEmpty() )
    {
      statusBar()->showMessage( tr( "没有可用的种子点" ), 5000 );
      return;
    }
  }

  // Heap-allocate so the Task Center worker can fill results until UI terminal.
  auto *resultHolder = new RsTemplateMatcher::Result;
  auto *fb = new QgsFeedback;
  auto paramsCopy = params;
  auto seedsCopy = seeds;
  const QString srcPath = mSourceRasterPath;
  const QString refPath = mRefRaster->source();
  const QgsCoordinateReferenceSystem destCrs = mParamsPanel->destCrs();

  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:georef:template_match";
  req.title = tr( "模板匹配" ).toStdString();
  req.source = "module";
  req.exclusive = true;

  const long taskId = sicnu::TaskCenter::instance().submitJob(
    req,
    [resultHolder, fb, paramsCopy, seedsCopy, srcPath, refPath, destCrs](
      const sicnu::jobs::JobRequest &,
      sicnu::operators::RSOperatorContext &ctx ) {
      Q_UNUSED( destCrs );
      ctx.logInfo( "Running geo-initialized template matching (NCC)" );
      ctx.reportProgress( 0.0, "Template match" );
      RsTemplateMatcher matcher( fb );
      *resultHolder = matcher.run( srcPath, refPath, destCrs, paramsCopy, seedsCopy );
      if ( ctx.isCancelled() || resultHolder->errorMessage == QStringLiteral( "cancelled" ) )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
      }
      if ( !resultHolder->ok() )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::ComputationError,
          resultHolder->errorMessage.toStdString() );
      }
      Json::Value result( Json::objectValue );
      result["accepted"] = resultHolder->accepted;
      result["attempted"] = resultHolder->attempted;
      return result;
    },
    [fb]() { fb->cancel(); },
    /*autoLoad=*/false );

  auto *conn = new QMetaObject::Connection;
  *conn = connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated, this,
                   [this, resultHolder, fb, taskId, conn]( const sicnu::AlgorithmTaskInfo &info ) {
                     if ( info.taskId != taskId )
                       return;
                     if ( info.status != sicnu::TaskStatus::Completed
                          && info.status != sicnu::TaskStatus::Failed
                          && info.status != sicnu::TaskStatus::Canceled )
                       return;
                     disconnect( *conn );
                     delete conn;

                     const auto r = *resultHolder;
                     delete resultHolder;
                     delete fb;

                     if ( info.status == sicnu::TaskStatus::Canceled )
                     {
                       statusBar()->showMessage( tr( "模板匹配已取消" ), 3000 );
                       return;
                     }
                     if ( info.status != sicnu::TaskStatus::Completed || !r.ok() )
                     {
                       statusBar()->showMessage(
                         tr( "模板匹配失败：%1" )
                           .arg( r.errorMessage.isEmpty()
                                   ? ( !info.errorMessage.isEmpty() ? info.errorMessage
                                                                   : tr( "未知错误" ) )
                                   : r.errorMessage ),
                         6000 );
                       return;
                     }

                     const QString msg = tr( "尝试 %1 点，接受 %2 对匹配，是否写入 GCP 列表？" )
                                           .arg( r.attempted )
                                           .arg( r.accepted );
                     if ( QMessageBox::question( this, tr( "模板匹配结果" ), msg ) != QMessageBox::Yes )
                       return;

                     QVector<QgsGcpPoint> pairs;
                     pairs.reserve( r.matches.size() );
                     const QgsCoordinateReferenceSystem destCrs = mParamsPanel->destCrs();
                     auto srcPxToMap = [this]( const QgsPointXY &px ) -> QgsPointXY {
                       if ( mSrcRaster && mSrcRaster->isValid() )
                       {
                         const auto extent = mSrcRaster->extent();
                         const int w = mSrcRaster->width();
                         const int h = mSrcRaster->height();
                         if ( w > 0 && h > 0 && extent.width() > 0 && extent.height() > 0 )
                         {
                           const double resX = extent.width() / w;
                           const double resY = extent.height() / h;
                           return QgsPointXY( extent.xMinimum() + px.x() * resX,
                                              extent.yMaximum() - px.y() * resY );
                         }
                       }
                       return QgsPointXY( px.x(), -px.y() );
                     };
                     for ( const auto &m : r.matches )
                     {
                       pairs.append( QgsGcpPoint( srcPxToMap( m.srcPx ), m.dstWorld, destCrs, true ) );
                     }
                     // Accepted matches go straight into the session (sole GCP owner).
                     georefSession().appendGcps( pairs );

                     QJsonObject o {
                       { QStringLiteral( "event" ),     QStringLiteral( "template_match" ) },
                       { QStringLiteral( "attempted" ), r.attempted },
                       { QStringLiteral( "accepted" ),  r.accepted },
                     };
                     QgsMessageLog::logMessage(
                       QString::fromUtf8( QJsonDocument( o ).toJson( QJsonDocument::Compact ) ),
                       QStringLiteral( "Georeferencer" ),
                       Qgis::MessageLevel::Info );
                     statusBar()->showMessage(
                       tr( "已添加 %1 个模板匹配 GCP" ).arg( r.accepted ), 5000 );
                   } );

  statusBar()->showMessage( tr( "模板匹配中…" ), 3000 );
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
  if ( mDstSession )
  {
    if ( mRefRaster )
    {
      mDstSession->removeLayer( mRefRaster );
      delete mRefRaster;
      mRefRaster = nullptr;
      mDstRaster = nullptr;
    }
    mDstSession->addLayer( layer, true );
  }
  else if ( mLayerStore )
    mLayerStore->addMapLayer( layer );
  mRefRaster = layer;
  mRefRasterPath = path;
  mDstRaster = layer;
  mDestRasterPath = path;

  if ( mDstCanvas )
  {
    // Always use the REF layer CRS as the canvas CRS so map picks are in
    // the image's native coordinates (not project CRS).
    if ( layer->crs().isValid() )
      mDstCanvas->setDestinationCrs( layer->crs() );
    if ( mDstSession )
      mDstSession->zoomToLayer( layer );
    else
    {
      mDstCanvas->setLayers( { layer } );
      mDstCanvas->setExtent( layer->extent() );
      mDstCanvas->refresh();
    }
  }
  // Align target CRS with reference image.
  if ( mParamsPanel && layer->crs().isValid() )
    mParamsPanel->setDestCrs( layer->crs() );

  updateDestLayerCaption(
    layer->name(),
    tr( "参考影像（基准 / Base）\n图层: %1\n路径: %2\nCRS: %3" )
      .arg( layer->name(), path,
            layer->crs().isValid() ? layer->crs().authid() : tr( "—" ) ) );
  updateGcpTableRasterPaths();
  updateToolAvailability();
  refreshFit();
  mGeorefSession.saveWorkflow( captureWorkflowSnapshot() );
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

void QgsGeoreferencerMainWindow::captureShellSpecific( RsGeoreferencingSession::WorkflowSnapshot &s ) const
{
  s.mode = static_cast<int>( RsGeorefModeToggle::ImageToImage );
  s.lastRefPath = mRefRasterPath;
  s.syncZoom = mSyncZoomAction ? mSyncZoomAction->isChecked() : true;
}

void QgsGeoreferencerMainWindow::applyShellSpecific( const RsGeoreferencingSession::WorkflowSnapshot &s )
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

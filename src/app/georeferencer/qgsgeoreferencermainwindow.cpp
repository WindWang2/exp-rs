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
#include "qgsrasterchangecoords.h"

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
    tr("Source image canvas (SRC / Warp): loads the image to correct.\n")
    tr("When adding a GCP, click the source point here first, then the conjugate point on the REF side (no coordinate form pops up).") ) );

  mDstCanvas = new QgsMapCanvas( this );
  mDstCanvas->setObjectName( QStringLiteral( "rsRefCanvas" ) );
  mDstCanvas->setCanvasColor( Qt::white );
  mDstCanvas->setToolTip( tr(
    tr("Reference image canvas (REF / Base): loads the registered reference image.\n")
    tr("When adding a GCP, click the conjugate position corresponding to the source point here to complete the control point pair.") ) );

  QWidget *srcPanel = makeCanvasPanel(
    mSrcCanvas, &mSrcLayerLabel,
    tr( "Source (Warp)" ),
    QStringLiteral( "rsSrcCanvasPanel" ),
    QStringLiteral( "rsSrcLayerLabel" ) );
  QWidget *refPanel = makeCanvasPanel(
    mDstCanvas, &mDstLayerLabel,
    tr( "Base" ),
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
      tr("Sync zoom (off by default): use only when SRC and REF share a CRS and similar extents.\n")
      tr("Keep Sync zoom off for already-registered image pairs, otherwise picked coordinates scramble and residuals go wild.") ) );
    connect( mSyncZoomAction, &QAction::toggled, this, [this]( bool on ) {
      if ( mSyncCtl )
        mSyncCtl->setEnabled( on );
      if ( on && statusBar() )
        statusBar()->showMessage(
          tr( "Sync zoom enabled — confirm both sides share the same CRS, otherwise GCP coordinates may be wrong" ), 6000 );
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
    tr("Opens the reference image from a file into the right REF (Base) side, as the GCP target and alignment base.") ) );
  mOpenRefFileAction->setStatusTip( mOpenRefFileAction->toolTip() );
  mOpenRefLayerAction = fileMenu->addAction(
    tr( "Load reference from project layer..." ),
    this, &QgsGeoreferencerMainWindow::loadReferenceFromProjectLayer );
  mOpenRefLayerAction->setToolTip( tr(
    tr("Chooses a raster from the main project layer list as the reference image (Base).") ) );
  mOpenRefLayerAction->setStatusTip( mOpenRefLayerAction->toolTip() );
  fileMenu->addSeparator();
  auto *loadPts = fileMenu->addAction( tr( "Load .points..." ), this, &QgsGeorefShellWindow::loadPoints );
  loadPts->setToolTip( tr( "Import a saved control point file." ) );
  auto *savePts = fileMenu->addAction( tr( "Save .points..." ), this, &QgsGeorefShellWindow::savePoints );
  savePts->setToolTip( tr( "Exports the current control points; unsaved changes are flagged before the window closes." ) );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close )->setToolTip( tr( "Closes this window (does not affect Image to Map)." ) );
  addStandardMenuBar();
}

void QgsGeoreferencerMainWindow::setupToolbars()
{
  mToolBar = addToolBar( tr( "Tools" ) );
  mToolBar->setObjectName( QStringLiteral( "rsGeorefToolBar" ) );
  mToolBar->setMovable( false );
  mToolBar->setToolTip( tr( "Image-to-Image tool: navigation, two-image registration, SIFT and run." ) );

  addCanvasNavigationActions( mToolBar, QStringLiteral( "rsGeoref" ) );
  mToolBar->addSeparator();
  addGcpEditActions( mToolBar, QStringLiteral( "rsGeoref" ) );
  mToolBar->addSeparator();

  mSyncZoomAction = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Sync Zoom" ) );
  mSyncZoomAction->setObjectName( QStringLiteral( "rsGeorefSyncZoomAction" ) );
  mSyncZoomAction->setToolTip( tr(
    tr("Sync zoom (off by default): enable only when both sides share a CRS and similar extents.\n")
    tr("With different CRSs, linking scrambles picked coordinates.") ) );
  mSyncZoomAction->setStatusTip( mSyncZoomAction->toolTip() );
  mSyncZoomAction->setWhatsThis( mSyncZoomAction->toolTip() );

  mSiftAction = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "SIFT Auto Matching" ),
    this, &QgsGeoreferencerMainWindow::runSiftMatch );
  mSiftAction->setObjectName( QStringLiteral( "rsGeorefSiftAction" ) );
  mSiftAction->setToolTip( tr(
    tr("SIFT auto-matching: SRC and the reference image must be open. After feature extraction and inlier filtering, GCPs can be added in batch.\n")
    tr("Needs OpenCV; provided by Image to Image only.") ) );
  mSiftAction->setStatusTip( mSiftAction->toolTip() );
  mSiftAction->setWhatsThis( mSiftAction->toolTip() );

  mTemplateMatchAction = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/select" ) ),
    tr( "Template Matching" ),
    this, &QgsGeoreferencerMainWindow::runTemplateMatch );
  mTemplateMatchAction->setObjectName( QStringLiteral( "rsGeorefTemplateMatchAction" ) );
  mTemplateMatchAction->setToolTip( tr(
    tr("Template matching (NCC): predicts the reference search area from the source image's initial geocoordinates, then runs correlation matching.\n")
    tr("Suits remote-sensing imagery with approximate coordinates; grid sampling or existing rough GCPs serve as seeds. Needs OpenCV.") ) );
  mTemplateMatchAction->setStatusTip( mTemplateMatchAction->toolTip() );
  mTemplateMatchAction->setWhatsThis( mTemplateMatchAction->toolTip() );

  addApplyAction( mToolBar, QStringLiteral( "rsGeorefApplyAction" ) );
}

QString QgsGeoreferencerMainWindow::windowHelpText() const
{
  return tr(
    "<b>Image Registration · Image 2 Image</b><br>"
    tr("Two-image registration: source image (Warp) on the left, reference image (Base) on the right.<br><br>")
    tr("<b>Typical Workflow</b><br>")
    tr("1. Open the source image: from a file or a main project layer<br>")
    tr("2. Open the reference image: from a file or a main project layer<br>")
    tr("3. Add / Move / Delete GCP become available once both sides are open<br>")
    tr("4. Navigation: pan / zoom in / zoom out; fit source / fit reference / fit both<br>")
    tr("5. Press Add GCP: SRC first, then REF (right-click to cancel an unfinished source point)<br>")
    tr("6. Optionally: template matching (needs SRC initial coordinates) / SIFT, Sync zoom → set output → run<br><br>")
    tr("No RPC (use Image to Map for RPC).") );
}

void QgsGeoreferencerMainWindow::runSiftMatch()
{
#ifndef SICNU_HAS_OPENCV
  statusBar()->showMessage( tr( "OpenCV unavailable — SIFT disabled" ), 5000 );
  return;
#else
  if ( !mRefRaster )
  {
    statusBar()->showMessage( tr( "First use File → Load reference raster..." ), 5000 );
    return;
  }
  if ( mSourceRasterPath.isEmpty() )
  {
    statusBar()->showMessage( tr( "Open the SRC image first" ), 5000 );
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
  req.title = tr( "SIFT Matching" ).toStdString();
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
                       statusBar()->showMessage( tr( "SIFT cancelled" ), 3000 );
                       return;
                     }
                     if ( info.status != sicnu::TaskStatus::Completed || !r.ok() )
                     {
                       statusBar()->showMessage(
                         tr( "SIFT failed: %1" )
                           .arg( r.errorMessage.isEmpty()
                                   ? ( !info.errorMessage.isEmpty() ? info.errorMessage
                                                                   : tr( "Unknown error" ) )
                                   : r.errorMessage ),
                         5000 );
                       return;
                     }

                     const QString msg = tr( "Found %1 matches, %2 inliers (%3%). Accept all of them?" )
                                           .arg( r.totalMatches )
                                           .arg( r.inliers.size() )
                                           .arg( int( r.inlierRatio * 100 ) );
                     if ( QMessageBox::question( this, tr( "SIFT Matching Results" ), msg ) != QMessageBox::Yes )
                       return;
                      QVector<QgsGcpPoint> pairs;
                      pairs.reserve( r.inliers.size() );
                      const QgsCoordinateReferenceSystem pointCrs = ( mRefRaster && mRefRaster->isValid() && mRefRaster->crs().isValid() )
                                                                      ? mRefRaster->crs()
                                                                      : mParamsPanel->destCrs();
                      auto srcPxToMap = [this]( const QgsPointXY &px ) -> QgsPointXY {
                        if ( !mSourceRasterPath.isEmpty() )
                        {
                          QgsRasterChangeCoords coords;
                          coords.loadRaster( mSourceRasterPath );
                          if ( coords.hasExistingGeoreference() )
                            return coords.toXY( px );
                        }
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
                        pairs.append( QgsGcpPoint( srcPxToMap( m.srcPx ), m.dstWorld, pointCrs, true ) );
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

  statusBar()->showMessage( tr( "SIFT matching..." ), 3000 );
#endif
}

void QgsGeoreferencerMainWindow::runTemplateMatch()
{
#ifndef SICNU_HAS_OPENCV
  statusBar()->showMessage( tr( "OpenCV unavailable — template matching disabled" ), 5000 );
  return;
#else
  if ( !mRefRaster )
  {
    statusBar()->showMessage( tr( "First use File → Load reference raster..." ), 5000 );
    return;
  }
  if ( mSourceRasterPath.isEmpty() )
  {
    statusBar()->showMessage( tr( "Open the SRC image first" ), 5000 );
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
      statusBar()->showMessage( tr( "Seed mode needs at least one existing GCP" ), 5000 );
      return;
    }
    // Enabled source points of the session's GCP list are the match seeds.
    // Convert session map coords -> pixel col/row via full geotransform (GEOREF-3).
    QgsRasterChangeCoords seedCoords;
    bool seedHasGeo = false;
    if ( !mSourceRasterPath.isEmpty() )
    {
      seedCoords.loadRaster( mSourceRasterPath );
      seedHasGeo = seedCoords.hasExistingGeoreference();
    }
    for ( const QgsGcpPoint &g : georefSession().gcps() )
    {
      if ( g.isEnabled() )
      {
        const QgsPointXY srcPt = g.sourcePoint();
        QgsPointXY srcPx;
        if ( seedHasGeo )
          srcPx = seedCoords.toColumnLine( srcPt );
        else if ( mSrcRaster && mSrcRaster->isValid() )
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
      statusBar()->showMessage( tr( "No seed points available" ), 5000 );
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
  req.title = tr( "Template Matching" ).toStdString();
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
                       statusBar()->showMessage( tr( "Template matching cancelled" ), 3000 );
                       return;
                     }
                     if ( info.status != sicnu::TaskStatus::Completed || !r.ok() )
                     {
                       statusBar()->showMessage(
                         tr( "Template matching failed: %1" )
                           .arg( r.errorMessage.isEmpty()
                                   ? ( !info.errorMessage.isEmpty() ? info.errorMessage
                                                                   : tr( "Unknown error" ) )
                                   : r.errorMessage ),
                         6000 );
                       return;
                     }

                     const QString msg = tr( "Tried %1 points, accepted %2 match pairs. Write them into the GCP list?" )
                                           .arg( r.attempted )
                                           .arg( r.accepted );
                     if ( QMessageBox::question( this, tr( "Template Matching Results" ), msg ) != QMessageBox::Yes )
                       return;

                     QVector<QgsGcpPoint> pairs;
                     pairs.reserve( r.matches.size() );
                     const QgsCoordinateReferenceSystem pointCrs = ( mRefRaster && mRefRaster->isValid() && mRefRaster->crs().isValid() )
                                                                      ? mRefRaster->crs()
                                                                      : mParamsPanel->destCrs();
                     auto srcPxToMap = [this]( const QgsPointXY &px ) -> QgsPointXY {
                       if ( !mSourceRasterPath.isEmpty() )
                       {
                         QgsRasterChangeCoords coords;
                         coords.loadRaster( mSourceRasterPath );
                         if ( coords.hasExistingGeoreference() )
                           return coords.toXY( px );
                       }
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
                       pairs.append( QgsGcpPoint( srcPxToMap( m.srcPx ), m.dstWorld, pointCrs, true ) );
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
                       tr( "Added %1 template-matching GCPs" ).arg( r.accepted ), 5000 );
                   } );

  statusBar()->showMessage( tr( "Template matching..." ), 3000 );
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
    tr( "Select Reference Image from Main Project (Base)" ) );
  if ( !picked )
    return;
  loadReferenceRaster( picked->source() );
  // Prefer project layer display name on caption if path load used basename.
  if ( mRefRaster && mRefRaster->isValid() && !picked->name().isEmpty() )
  {
    // Layer was created with file basename; caption already set — refresh name tip.
    updateDestLayerCaption(
      picked->name(),
      tr( "Reference image (Base) — from a main project layer\nLayer: %1\nPath: %2" )
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
      statusBar()->showMessage( tr( "Cannot open the reference image: %1" ).arg( path ), 5000 );
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
    tr( "Reference image (Base)\nLayer: %1\nPath: %2\nCRS: %3" )
      .arg( layer->name(), path,
            layer->crs().isValid() ? layer->crs().authid() : tr( "—" ) ) );
  updateGcpTableRasterPaths();
  updateToolAvailability();
  refreshFit();
  mGeorefSession.saveWorkflow( captureWorkflowSnapshot() );
  if ( statusBar() )
    statusBar()->showMessage( tr( "Loaded reference image (Base): %1" ).arg( layer->name() ), 4000 );
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
      tr( "Reference image (Base)\nPath: %1" ).arg( s.lastRefPath ) );
  }
  if ( mSyncZoomAction )
    mSyncZoomAction->setChecked( s.syncZoom );
  updateSourceLayerCaption();
}

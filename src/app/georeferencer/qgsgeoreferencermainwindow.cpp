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

  mDstCanvas = new QgsMapCanvas( this );
  mDstCanvas->setObjectName( QStringLiteral( "rsRefCanvas" ) );
  mDstCanvas->setCanvasColor( Qt::white );

  split->addWidget( mSrcCanvas );
  split->addWidget( mDstCanvas );
  split->setStretchFactor( 0, 1 );
  split->setStretchFactor( 1, 1 );
  setCentralWidget( split );

  mSyncCtl = new RsTwinCanvasSyncController( mSrcCanvas, mDstCanvas, this );
  if ( mSyncZoomAction )
  {
    mSyncZoomAction->setCheckable( true );
    mSyncZoomAction->setChecked( true );
    connect( mSyncZoomAction, &QAction::toggled, this, [this]( bool on ) {
      if ( mSyncCtl )
        mSyncCtl->setEnabled( on );
    } );
  }
}

void QgsGeoreferencerMainWindow::setupMenus()
{
  QMenu *fileMenu = createFileMenu();
  fileMenu->addAction( tr( "Load reference raster..." ),
                       this, QOverload<>::of( &QgsGeoreferencerMainWindow::loadReferenceRaster ) );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Load .points..." ), this, &QgsGeorefShellWindow::loadPoints );
  fileMenu->addAction( tr( "Save .points..." ), this, &QgsGeorefShellWindow::savePoints );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close );
  addStandardMenuBar();
}

void QgsGeoreferencerMainWindow::setupToolbars()
{
  mToolBar = addToolBar( tr( "Tools" ) );
  mToolBar->setObjectName( QStringLiteral( "rsGeorefToolBar" ) );
  mToolBar->setMovable( false );

  addGcpEditActions( mToolBar, QStringLiteral( "rsGeoref" ) );
  mToolBar->addSeparator();

  mSyncZoomAction = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Sync zoom" ) );
  mSyncZoomAction->setObjectName( QStringLiteral( "rsGeorefSyncZoomAction" ) );

  auto *sift = mToolBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "Auto match (SIFT)" ),
    this, &QgsGeoreferencerMainWindow::runSiftMatch );
  sift->setObjectName( QStringLiteral( "rsGeorefSiftAction" ) );

  addApplyAction( mToolBar, QStringLiteral( "rsGeorefApplyAction" ) );
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

bool QgsGeoreferencerMainWindow::loadReferenceRaster( const QString &path )
{
  auto *layer = new QgsRasterLayer( path, QFileInfo( path ).baseName(), QStringLiteral( "gdal" ) );
  if ( !layer->isValid() )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, QString( "Failed to open reference raster: %1" ).arg( path ) );
    delete layer;
    return false;
  }
  if ( mLayerStore )
    mLayerStore->addMapLayer( layer );
  mRefRaster = layer;
  mRefRasterPath = path;

  if ( mDstCanvas )
  {
    mDstCanvas->setLayers( { layer } );
    mDstCanvas->setExtent( layer->extent() );
    mDstCanvas->refresh();
  }

  mSession.saveWorkflow( captureWorkflowSnapshot() );
  return true;
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
    mRefRasterPath = s.lastRefPath;
  if ( mSyncZoomAction )
    mSyncZoomAction->setChecked( s.syncZoom );
}

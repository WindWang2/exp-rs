#include "qgsgeoreferencermainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

#include "core/sicnu_logging.h"
#include "qgis.h"
#include "qgisinterface.h"
#include "qgsapplication.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsgcplist.h"
#include "qgsgcppoint.h"
#include "qgsgeorefdatapoint.h"
#include "qgsmapcanvas.h"
#include "qgsmaplayerstore.h"
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

  if ( mModeToggle )
  {
    mModeToggle->setObjectName( QStringLiteral( "rsGeorefModeToggle" ) );
    mModeToggle->setMode( RsGeorefModeToggle::ImageToImage );
    mModeToggle->hide();
    connect( mModeToggle, &RsGeorefModeToggle::modeChanged,
             this, &QgsGeoreferencerMainWindow::onModeChanged );
  }
  onModeChanged( RsGeorefModeToggle::ImageToImage );
  if ( mModeToggle )
  {
    mModeToggle->setMode( RsGeorefModeToggle::ImageToImage );
    mModeToggle->hide();
  }
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
  auto *fileMenu = menuBar()->addMenu( tr( "&File" ) );
  fileMenu->addAction( tr( "Open source raster..." ),
                       this, &QgsGeorefShellWindow::openSourceRaster );
  fileMenu->addAction( tr( "Load reference raster..." ),
                       this, QOverload<>::of( &QgsGeoreferencerMainWindow::loadReferenceRaster ) );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Load .points..." ), this, &QgsGeorefShellWindow::loadPoints );
  fileMenu->addAction( tr( "Save .points..." ), this, &QgsGeorefShellWindow::savePoints );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Close" ), this, &QWidget::close );

  menuBar()->addMenu( tr( "&Edit" ) );
  menuBar()->addMenu( tr( "&View" ) );
  menuBar()->addMenu( tr( "&Settings" ) );
  menuBar()->addMenu( tr( "&Help" ) );
}

void QgsGeoreferencerMainWindow::setupToolbars()
{
  mModeBar = addToolBar( tr( "Mode" ) );
  mModeBar->setObjectName( QStringLiteral( "rsGeorefToolBar" ) );
  mModeBar->setMovable( false );

  mModeToggle = new RsGeorefModeToggle( this );
  mModeToggle->setObjectName( QStringLiteral( "rsGeorefModeToggle" ) );
  mModeBar->addWidget( mModeToggle );
  mModeToggle->hide();
  mModeBar->addSeparator();

  mAddPointAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Add GCP" ) );
  mAddPointAction->setObjectName( QStringLiteral( "rsGeorefAddPointAction" ) );
  mAddPointAction->setCheckable( true );

  mMovePointAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Move GCP" ) );
  mMovePointAction->setObjectName( QStringLiteral( "rsGeorefMovePointAction" ) );
  mMovePointAction->setCheckable( true );

  mDeletePointAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Delete GCP" ) );
  mDeletePointAction->setObjectName( QStringLiteral( "rsGeorefDeletePointAction" ) );
  mDeletePointAction->setCheckable( true );

  auto *mapToolGroup = new QActionGroup( this );
  mapToolGroup->setExclusive( true );
  mapToolGroup->addAction( mAddPointAction );
  mapToolGroup->addAction( mMovePointAction );
  mapToolGroup->addAction( mDeletePointAction );

  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Load .gcp" ),
                       this, &QgsGeorefShellWindow::loadPoints );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Export .gcp" ),
                       this, &QgsGeorefShellWindow::savePoints );

  mModeBar->addSeparator();
  mSyncZoomAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Sync zoom" ) );
  mSyncZoomAction->setObjectName( QStringLiteral( "rsGeorefSyncZoomAction" ) );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Zoom to all" ), this, []() {} );

  auto *sift = mModeBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "Auto match (SIFT)" ),
    this,
    [this]() {
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
      const auto params = dlg.params();
      auto *task = new RsSiftTask( mSourceRasterPath,
                                   mRefRaster->source(),
                                   mParamsPanel->destCrs(),
                                   params );
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
    } );
  sift->setObjectName( QStringLiteral( "rsGeorefSiftAction" ) );

  auto *spacer = new QWidget( this );
  spacer->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  mModeBar->addWidget( spacer );

  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Preview" ), this, []() {} );

  mApplyAction = mModeBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "Apply" ),
    this,
    &QgsGeorefShellWindow::applyTransform );
  mApplyAction->setObjectName( QStringLiteral( "rsGeorefApplyAction" ) );
  mApplyAction->setEnabled( false );
}

QgsMapCanvas *QgsGeoreferencerMainWindow::pickCanvasForMode( RsGeorefModeToggle::Mode m ) const
{
  if ( m == RsGeorefModeToggle::ImageToImage )
    return mDstCanvas;
  if ( mIface && mIface->mapCanvas() )
    return mIface->mapCanvas();
  return mDstCanvas;
}

QgsMapCanvas *QgsGeoreferencerMainWindow::pickCanvas() const
{
  return mDstCanvas; // I2I always picks on REF
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

void QgsGeoreferencerMainWindow::onModeChanged( RsGeorefModeToggle::Mode m )
{
  if ( !mDstCanvas )
    return;

  // I2I shell is pinned to twin-raster layout; still honor REF raster paint.
  Q_UNUSED( m )
  if ( mRefRaster )
  {
    mDstCanvas->setLayers( { mRefRaster } );
    mDstCanvas->setExtent( mRefRaster->extent() );
  }
  mDstCanvas->show();
  if ( mParamsPanel )
    mParamsPanel->setRpcMode( false );
  mDstCanvas->refresh();

  for ( auto it = mDataPoints.begin(); it != mDataPoints.end(); ++it )
  {
    if ( it.value() )
      it.value()->updateMarkers();
  }
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
  if ( mModeToggle )
  {
    mModeToggle->setMode( RsGeorefModeToggle::ImageToImage );
    mModeToggle->hide();
  }
}

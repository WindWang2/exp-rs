#include "qgsgeoreferencermainwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMetaEnum>
#include <QMenu>
#include <QMenuBar>
#include <QPointF>
#include <QSet>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVector>
#include <QWidget>

#include <cmath>

#include "qgis.h"
#include "qgisinterface.h"
#include "qgsapplication.h"
#include "qgscoordinatereferencesystem.h"
#include "qgscoordinatetransformcontext.h"
#include "qgsgcplist.h"
#include "qgsgcplistwidget.h"
#include "qgsgcppoint.h"
#include "qgsgeorefdatapoint.h"
#include "qgsgcppoint.h"
#include "qgsgeorefdatapoint.h"
#include "qgsgeoreftooladdpoint.h"
#include "qgsgeoreftransform.h"
#include "qgsrpcgcptransformer.h"
#include "qgsmapcanvas.h"
#include "qgsmapcoordsdialog.h"
#include "qgsmaplayerstore.h"
#include "qgsmessagelog.h"
#include "qgsrasterlayer.h"
#include "qgstaskmanager.h"
#include "rs_georef_params_panel.h"
#include "rs_twincanvas_sync_controller.h"
#include "rs_warp_task.h"

QgsGeoreferencerMainWindow::QgsGeoreferencerMainWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , mIface( iface )
{
  setWindowTitle( tr( "Georeferencer · 几何校正" ) );
  resize( 1200, 800 );

  // GCP list lives at the main window level — owns QgsGcpPoint instances.
  // QgsGCPList currently has a parameterless ctor (Task 3 design); parent it
  // explicitly so Qt cleans it up with the window.
  mGcps = new QgsGCPList();
  mGcps->setParent( this );

  setupMenus();
  setupToolbars();
  setupStatusBar();
  setupCentralWidget();

  // Bottom dock: 10-column GCP table per design.html ArtboardGeoref.
  mGcpDock = new QDockWidget( tr( "GCP 表" ), this );
  mGcpDock->setObjectName( QStringLiteral( "rsGcpDock" ) );
  mGcpDock->setAllowedAreas( Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea );

  mGcpTable = new QgsGCPListWidget( mGcpDock );
  mGcpTable->setGCPList( mGcps );
  mGcpDock->setWidget( mGcpTable );

  addDockWidget( Qt::BottomDockWidgetArea, mGcpDock );
  resizeDocks( { mGcpDock }, { 280 }, Qt::Vertical );

  // Task 11.4.7 — right param dock
  mParamDock = new QDockWidget( tr( "校正参数" ), this );
  mParamDock->setObjectName( QStringLiteral( "rsParamDock" ) );
  mParamDock->setAllowedAreas( Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea );
  mParamsPanel = new RsGeorefParamsPanel( mParamDock );
  mParamDock->setWidget( mParamsPanel );
  addDockWidget( Qt::RightDockWidgetArea, mParamDock );
  resizeDocks( { mParamDock }, { 340 }, Qt::Horizontal );

  // Wire the params panel to the recompute pipeline.
  connect( mGcps, &QgsGCPList::changed, this, &QgsGeoreferencerMainWindow::recomputeFit );
  // Task 11.5.2 — reconcile QgsGeorefDataPoint adapters (canvas markers) with
  // the live QgsGCPList on every mutation.
  connect( mGcps, &QgsGCPList::changed, this, &QgsGeoreferencerMainWindow::onPointsChanged );
  connect( mParamsPanel, &RsGeorefParamsPanel::transformMethodChanged,
           this, &QgsGeoreferencerMainWindow::recomputeFit );
  connect( mParamsPanel, &RsGeorefParamsPanel::outputPathChanged, this,
           [this]( const QString & ) { recomputeFit(); } );
  // Task 11.5.1 — user-selected destination CRS should re-run the fit.
  connect( mParamsPanel, &RsGeorefParamsPanel::destCrsChanged,
           this, &QgsGeoreferencerMainWindow::recomputeFit );
  // Task 11.5.4 — DEM Z-offset changes feed the RPC transformer's RPC_HEIGHT
  // option; recomputeFit() picks the new value up from the panel.
  connect( mParamsPanel, &RsGeorefParamsPanel::demZOffsetChanged,
           this, &QgsGeoreferencerMainWindow::recomputeFit );

  // Task 11.5.3 — REF canvas content is private to the Georeferencer when
  // Image-to-Image mode is active; the store owns the loaded reference and
  // source rasters and cleans them up with the window.
  mRefStore = new QgsMapLayerStore( this );

  // Task 11.4.8 + 11.5.3 — mode toggle drives RPC visibility on the params
  // panel AND swaps REF canvas content between the main-app layers, the
  // private reference raster, or hidden (RPC mode).
  if ( mModeToggle )
  {
    connect( mModeToggle, &RsGeorefModeToggle::modeChanged,
             this, &QgsGeoreferencerMainWindow::onModeChanged );
  }

  mParamsPanel->setActualGcpCount( 0 );

  recomputeFit();
}

QgsGeoreferencerMainWindow::~QgsGeoreferencerMainWindow()
{
  qDeleteAll( mDataPoints );
  mDataPoints.clear();
}

void QgsGeoreferencerMainWindow::onPointsChanged()
{
  if ( !mGcps )
    return;

  // Reconcile mDataPoints with the live mGcps list. We assign ids by current
  // list position so the on-canvas numeric label tracks the table row.
  QSet<QgsGcpPoint *> live;
  int idCounter = 0;
  for ( QgsGcpPoint *p : *mGcps )
  {
    if ( !p )
    {
      ++idCounter;
      continue;
    }
    live.insert( p );
    QgsGeorefDataPoint *dp = mDataPoints.value( p, nullptr );
    if ( !dp )
    {
      dp = new QgsGeorefDataPoint( mSrcCanvas, mRefCanvas, p );
      mDataPoints.insert( p, dp );
    }
    dp->setId( idCounter );
    dp->updateMarkers();
    ++idCounter;
  }

  // Drop any data points whose backing QgsGcpPoint has been removed.
  QList<QgsGcpPoint *> dead;
  for ( auto it = mDataPoints.cbegin(); it != mDataPoints.cend(); ++it )
  {
    if ( !live.contains( it.key() ) )
      dead.append( it.key() );
  }
  for ( QgsGcpPoint *p : dead )
  {
    delete mDataPoints.take( p );
  }
}

namespace
{
  // Lookup of the per-method minimumGcpCount without needing a fitted transformer.
  // The instance method exists on the concrete subclasses; we construct a bare
  // transformer just to call it.
  int minimumGcpCountFor( QgsGcpTransformerInterface::TransformMethod m )
  {
    std::unique_ptr<QgsGcpTransformerInterface> t(
      QgsGcpTransformerInterface::create( m ) );
    return t ? t->minimumGcpCount() : 0;
  }
}

void QgsGeoreferencerMainWindow::recomputeFit()
{
  if ( !mParamsPanel || !mGcps )
    return;

  const QgsGcpTransformerInterface::TransformMethod method = mParamsPanel->transformMethod();
  const int minN = minimumGcpCountFor( method );
  mParamsPanel->setMinimumGcpCount( minN );

  // Build a fresh transform of the chosen method.
  mTransform.reset( new QgsGeorefTransform( method ) );

  // Collect enabled GCPs (source + destination) in parallel vectors.
  QVector<QgsPointXY> src;
  QVector<QgsPointXY> dst;
  src.reserve( mGcps->size() );
  dst.reserve( mGcps->size() );
  for ( const QgsGcpPoint *p : std::as_const( *mGcps ) )
  {
    if ( !p || !p->isEnabled() )
      continue;
    src.push_back( p->sourcePoint() );
    dst.push_back( p->destinationPoint() );
  }
  const int enabledCount = src.size();
  mParamsPanel->setActualGcpCount( enabledCount );

  // Task 11.5.4/11.5.5 — push DEM/Z-offset settings down into the RPC
  // transformer before the fit so RPC_HEIGHT and (if a DEM is set) RPC_DEM
  // are part of GDALCreateRPCTransformerV2's papszOptions.  Linear-bias
  // refinement is requested only when ≥ 3 enabled GCPs are available
  // (matches QgsRpcGcpTransformer's internal guard).
  if ( method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical )
  {
    if ( auto *rpc = dynamic_cast<QgsRpcGcpTransformer *>(
           mTransform ? mTransform->gcpTransformer() : nullptr ) )
    {
      rpc->setSourceRasterPath( mSourceRasterPath );
      const bool useRefine = ( enabledCount >= 3 );
      rpc->setRpcOptions( mParamsPanel->demPath(),
                          mParamsPanel->demZOffset(),
                          useRefine );
    }
  }

  bool fitOk = false;
  if ( enabledCount >= minN && minN > 0 )
  {
    try
    {
      fitOk = mTransform->updateParametersFromGcps( src, dst, false );
    }
    catch ( ... )
    {
      fitOk = false;
    }
  }

  // Residuals + scatter
  double totalSq = 0.0, xSq = 0.0, ySq = 0.0;
  double maxMag = 0.0;
  int maxRow = -1;
  QVector<QPointF> scatter;
  scatter.reserve( enabledCount );

  if ( fitOk )
  {
    const QgsCoordinateReferenceSystem dstCrs = mParamsPanel->destCrs();
    mGcps->updateResiduals( mTransform.get(), dstCrs, dstCrs );

    int rowId = 0;
    int included = 0;
    for ( const QgsGcpPoint *p : std::as_const( *mGcps ) )
    {
      if ( !p )
      {
        ++rowId;
        continue;
      }
      if ( !p->isEnabled() )
      {
        ++rowId;
        continue;
      }
      const QPointF r = p->residual();
      scatter.push_back( r );
      const double mag = std::hypot( r.x(), r.y() );
      totalSq += mag * mag;
      xSq += r.x() * r.x();
      ySq += r.y() * r.y();
      if ( mag > maxMag )
      {
        maxMag = mag;
        maxRow = rowId;
      }
      ++included;
      ++rowId;
    }

    if ( included > 0 )
    {
      mLastRms = std::sqrt( totalSq / included );
      const double xRms = std::sqrt( xSq / included );
      const double yRms = std::sqrt( ySq / included );
      mParamsPanel->setRmsValues( mGcps->size(), enabledCount,
                                  mLastRms, xRms, yRms, maxMag, maxRow );
    }
    else
    {
      mLastRms = 0.0;
      mParamsPanel->setRmsValues( mGcps->size(), enabledCount, 0, 0, 0, 0, -1 );
    }
  }
  else
  {
    mLastRms = 0.0;
    mParamsPanel->setRmsValues( mGcps->size(), enabledCount, 0, 0, 0, 0, -1 );
  }

  mParamsPanel->setResidualScatter( scatter );

  // Update status-bar RMS label.
  if ( mRmsLabel )
  {
    if ( fitOk && mLastRms > 0.0 )
      mRmsLabel->setText( tr( "RMS: %1 px" ).arg( QString::number( mLastRms, 'f', 3 ) ) );
    else
      mRmsLabel->setText( tr( "RMS: —" ) );
  }

  // Apply enables only when fit is valid AND output path is non-empty.
  if ( mApplyAction )
  {
    const bool canApply = fitOk
                          && enabledCount >= minN
                          && !mParamsPanel->outputPath().isEmpty();
    mApplyAction->setEnabled( canApply );
  }
}

void QgsGeoreferencerMainWindow::applyTransform()
{
  if ( !mParamsPanel || !mGcps )
    return;

  const QgsGcpTransformerInterface::TransformMethod method = mParamsPanel->transformMethod();
  const int minN = minimumGcpCountFor( method );

  int enabled = 0;
  for ( const QgsGcpPoint *p : std::as_const( *mGcps ) )
    if ( p && p->isEnabled() )
      ++enabled;

  if ( enabled < minN )
  {
    statusBar()->showMessage( tr( "GCP 数量不足（需要 %1，实际 %2）" ).arg( minN ).arg( enabled ), 3000 );
    return;
  }
  if ( mParamsPanel->outputPath().isEmpty() )
  {
    statusBar()->showMessage( tr( "请填写输出路径" ), 3000 );
    return;
  }
  if ( mSourceRasterPath.isEmpty() )
  {
    statusBar()->showMessage( tr( "未指定源栅格路径" ), 3000 );
    return;
  }
  if ( !mTransform )
  {
    statusBar()->showMessage( tr( "变换尚未完成拟合" ), 3000 );
    return;
  }

  // Lock UI for the duration of the warp.
  setWarpInProgressForTest( true );

  auto *task = new RsWarpTask( mSourceRasterPath, mParamsPanel->outputPath(),
                               mTransform.get(),
                               mParamsPanel->resamplingMethod(),
                               mParamsPanel->destCrs(),
                               mParamsPanel->outputPixelSize() );

  auto finalize = [this, task]() {
    emitStructuredLog( task->result() );
    setWarpInProgressForTest( false );
    if ( task->result().status == QgsImageWarper::WarpStatus::Ok )
    {
      statusBar()->showMessage(
        tr( "已输出: %1 (%2 字节, %3 ms)" )
          .arg( mParamsPanel->outputPath() )
          .arg( task->result().outputBytes )
          .arg( task->result().durationMs ),
        6000 );
    }
    else
    {
      statusBar()->showMessage(
        tr( "校正失败: %1" ).arg( task->result().errorMessage ),
        6000 );
    }
  };

  connect( task, &QgsTask::taskCompleted, this, finalize );
  connect( task, &QgsTask::taskTerminated, this, finalize );

  QgsApplication::taskManager()->addTask( task );
}

void QgsGeoreferencerMainWindow::emitStructuredLog( const QgsImageWarper::WarpResult &r )
{
  QJsonObject o;
  o.insert( QStringLiteral( "event" ), QStringLiteral( "warp_finished" ) );

  // Stringify the enums via Q_ENUM metatype for stable log keys.
  {
    const QMetaEnum me = QMetaEnum::fromType<QgsGcpTransformerInterface::TransformMethod>();
    const char *key = me.valueToKey( static_cast<int>( mParamsPanel->transformMethod() ) );
    o.insert( QStringLiteral( "method" ), QString::fromUtf8( key ? key : "" ) );
  }
  {
    const QMetaEnum me = QMetaEnum::fromType<QgsImageWarper::ResamplingMethod>();
    const char *key = me.valueToKey( static_cast<int>( mParamsPanel->resamplingMethod() ) );
    o.insert( QStringLiteral( "resampling" ), QString::fromUtf8( key ? key : "" ) );
  }

  o.insert( QStringLiteral( "gcp_total" ), static_cast<int>( mGcps ? mGcps->size() : 0 ) );

  int enabled = 0;
  if ( mGcps )
  {
    for ( const QgsGcpPoint *p : std::as_const( *mGcps ) )
      if ( p && p->isEnabled() )
        ++enabled;
  }
  o.insert( QStringLiteral( "gcp_enabled" ), enabled );
  o.insert( QStringLiteral( "rms_px" ), mLastRms );
  o.insert( QStringLiteral( "output" ), mParamsPanel ? mParamsPanel->outputPath() : QString() );
  o.insert( QStringLiteral( "output_bytes" ), static_cast<double>( r.outputBytes ) );
  o.insert( QStringLiteral( "duration_ms" ), r.durationMs );

  QString status;
  switch ( r.status )
  {
    case QgsImageWarper::WarpStatus::Ok:
      status = QStringLiteral( "ok" );
      break;
    case QgsImageWarper::WarpStatus::Cancelled:
      status = QStringLiteral( "cancelled" );
      break;
    default:
      status = QStringLiteral( "failed" );
      break;
  }
  o.insert( QStringLiteral( "status" ), status );

  if ( r.status != QgsImageWarper::WarpStatus::Ok )
  {
    o.insert( QStringLiteral( "error_code" ), static_cast<int>( r.status ) );
    o.insert( QStringLiteral( "error_msg" ), r.errorMessage );
  }

  QgsMessageLog::logMessage(
    QString::fromUtf8( QJsonDocument( o ).toJson( QJsonDocument::Compact ) ),
    QStringLiteral( "Georeferencer" ),
    Qgis::Info );
}

void QgsGeoreferencerMainWindow::setWarpInProgressForTest( bool on )
{
  if ( mGcpTable )
    mGcpTable->setEnabled( !on );
  if ( mApplyAction )
    mApplyAction->setEnabled( !on );
}

void QgsGeoreferencerMainWindow::setupCentralWidget()
{
  auto *split = new QSplitter( Qt::Horizontal, this );
  split->setObjectName( QStringLiteral( "rsGeorefSplitter" ) );

  mSrcCanvas = new QgsMapCanvas( this );
  mSrcCanvas->setObjectName( QStringLiteral( "rsSrcCanvas" ) );
  mSrcCanvas->setCanvasColor( Qt::white );

  mRefCanvas = new QgsMapCanvas( this );
  mRefCanvas->setObjectName( QStringLiteral( "rsRefCanvas" ) );
  mRefCanvas->setCanvasColor( Qt::white );

  split->addWidget( mSrcCanvas );
  split->addWidget( mRefCanvas );
  split->setStretchFactor( 0, 1 );
  split->setStretchFactor( 1, 1 );
  setCentralWidget( split );

  mSyncCtl = new RsTwinCanvasSyncController( mSrcCanvas, mRefCanvas, this );

  // Add-point map tool — clicking the SRC canvas pops the MapCoords dialog.
  mAddPointTool = new QgsGeorefToolAddPoint( mSrcCanvas );
  mAddPointTool->setParent( this );
  connect( mAddPointTool, &QgsGeorefToolAddPoint::showCoordDialog,
           this, &QgsGeoreferencerMainWindow::showCoordDialog );

  // Wire the toolbar's Add GCP action — toggle installs/uninstalls the tool.
  if ( mAddPointAction )
  {
    mAddPointAction->setCheckable( true );
    connect( mAddPointAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !mSrcCanvas )
        return;
      if ( on )
        mSrcCanvas->setMapTool( mAddPointTool );
      else if ( mSrcCanvas->mapTool() == mAddPointTool )
        mSrcCanvas->unsetMapTool( mAddPointTool );
    } );
  }

  // Wire the Sync zoom action — toggle enables/disables the controller.
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
  // Task 11.5.3 — explicit source / reference raster entries.
  fileMenu->addAction( tr( "Open source raster..." ),
                       this, &QgsGeoreferencerMainWindow::openSourceRaster );
  fileMenu->addAction( tr( "Load reference raster..." ),
                       this, QOverload<>::of( &QgsGeoreferencerMainWindow::loadReferenceRaster ) );
  fileMenu->addSeparator();
  fileMenu->addAction( tr( "Load .points..." ), this, []() {} );
  fileMenu->addAction( tr( "Save .points..." ), this, []() {} );
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
  mModeBar->addWidget( mModeToggle );
  mModeBar->addSeparator();

  // GCP ops — Add GCP gets wired to the map tool in setupCentralWidget().
  mAddPointAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Add GCP" ) );
  mAddPointAction->setObjectName( QStringLiteral( "rsGeorefAddPointAction" ) );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Delete GCP" ), this, []() {} );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Load .gcp" ), this, []() {} );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Export .gcp" ), this, []() {} );

  mModeBar->addSeparator();
  mSyncZoomAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Sync zoom" ) );
  mSyncZoomAction->setObjectName( QStringLiteral( "rsGeorefSyncZoomAction" ) );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Zoom to all" ), this, []() {} );

  auto *sift = mModeBar->addAction(
    QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ),
    tr( "Auto match (SIFT)" ),
    this,
    [this]() {
      statusBar()->showMessage( tr( "SIFT auto-match coming in Phase 11.5" ), 3000 );
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
    &QgsGeoreferencerMainWindow::applyTransform );
  mApplyAction->setObjectName( QStringLiteral( "rsGeorefApplyAction" ) );
  mApplyAction->setEnabled( false ); // becomes enabled once a valid fit + output is set
}

void QgsGeoreferencerMainWindow::setupStatusBar()
{
  mCoordLabel = new QLabel( tr( "—" ), this );
  mCoordLabel->setObjectName( QStringLiteral( "rsGeorefCoordLabel" ) );

  mCrsLabel = new QLabel( tr( "CRS: —" ), this );
  mCrsLabel->setObjectName( QStringLiteral( "rsGeorefCrsLabel" ) );

  mRmsLabel = new QLabel( tr( "RMS: —" ), this );
  mRmsLabel->setObjectName( QStringLiteral( "rsGeorefRmsLabel" ) );

  statusBar()->addWidget( mCoordLabel, 1 );
  statusBar()->addPermanentWidget( mCrsLabel );
  statusBar()->addPermanentWidget( mRmsLabel );
}

void QgsGeoreferencerMainWindow::showCoordDialog( const QgsPointXY &sourcePixel )
{
  // The dialog holds a *preview* QgsGcpPoint while open. We keep one on the
  // heap so it outlives the lambda capture below; the dialog (WA_DeleteOnClose)
  // owns the lifecycle by parenting both.
  auto *tempGcp = new QgsGcpPoint( sourcePixel, QgsPointXY(), QgsCoordinateReferenceSystem(), true );
  auto *tempDataPoint = new QgsGeorefDataPoint( mSrcCanvas, mRefCanvas, tempGcp );

  QgsCoordinateReferenceSystem rasterCrs = mSrcCanvas
                                             ? mSrcCanvas->mapSettings().destinationCrs()
                                             : QgsCoordinateReferenceSystem();

  auto *dlg = new QgsMapCoordsDialog( mRefCanvas, tempDataPoint, rasterCrs, this );
  dlg->setAttribute( Qt::WA_DeleteOnClose );

  // The dialog emits pointAdded(src, dst, destCrs) on OK. Persist into the
  // shared QgsGCPList — its changed() signal cascades to the model and the
  // table refreshes automatically.
  connect( dlg, &QgsMapCoordsDialog::pointAdded, this,
           [this]( const QgsPointXY &srcCoord, const QgsPointXY &dstCoord, const QgsCoordinateReferenceSystem &destCrs ) {
             if ( mGcps )
               mGcps->appendPoint( QgsGcpPoint( srcCoord, dstCoord, destCrs, true ) );
           } );

  // Clean up the heap-allocated preview helpers when the dialog closes.
  connect( dlg, &QDialog::finished, this, [tempGcp, tempDataPoint]( int ) {
    delete tempDataPoint;
    delete tempGcp;
  } );

  dlg->show();
}

void QgsGeoreferencerMainWindow::openSourceRaster()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Open source raster" ), QString(),
    tr( "Raster (*.tif *.tiff *.img *.jp2);;All files (*)" ) );
  if ( path.isEmpty() )
    return;

  setSourceRasterPath( path );

  auto *layer = new QgsRasterLayer( path, QFileInfo( path ).baseName(),
                                    QStringLiteral( "gdal" ) );
  if ( !layer->isValid() )
  {
    delete layer;
    if ( statusBar() )
      statusBar()->showMessage( tr( "Failed to open raster: %1" ).arg( path ), 5000 );
    return;
  }
  if ( mRefStore )
    mRefStore->addMapLayer( layer );
  mSrcRaster = layer;

  if ( mSrcCanvas )
  {
    mSrcCanvas->setLayers( { layer } );
    mSrcCanvas->setExtent( layer->extent() );
    mSrcCanvas->refresh();
  }
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
  auto *layer = new QgsRasterLayer( path, QFileInfo( path ).baseName(),
                                    QStringLiteral( "gdal" ) );
  if ( !layer->isValid() )
  {
    delete layer;
    return false;
  }
  if ( mRefStore )
    mRefStore->addMapLayer( layer );
  mRefRaster = layer;

  // If already in Image-to-Image mode, swap in immediately. Otherwise
  // onModeChanged() will pick it up when the user switches modes.
  if ( mRefCanvas && mModeToggle
       && mModeToggle->currentMode() == RsGeorefModeToggle::ImageToImage )
  {
    mRefCanvas->setLayers( { layer } );
    mRefCanvas->setExtent( layer->extent() );
    mRefCanvas->refresh();
  }
  return true;
}

void QgsGeoreferencerMainWindow::onModeChanged( RsGeorefModeToggle::Mode m )
{
  if ( !mRefCanvas )
    return;

  switch ( m )
  {
    case RsGeorefModeToggle::ImageToMap:
    {
      // Restore main-app layers when the host application provides a canvas;
      // otherwise just clear the REF canvas. The test does not exercise this
      // branch with a non-null iface, but the behavior is harmless.
      QList<QgsMapLayer *> mainLayers;
      if ( mIface && mIface->mapCanvas() )
        mainLayers = mIface->mapCanvas()->layers();
      mRefCanvas->setLayers( mainLayers );
      mRefCanvas->show();
      if ( mParamsPanel )
        mParamsPanel->setRpcMode( false );
      break;
    }
    case RsGeorefModeToggle::ImageToImage:
      if ( mRefRaster )
      {
        mRefCanvas->setLayers( { mRefRaster } );
        mRefCanvas->setExtent( mRefRaster->extent() );
      }
      else
      {
        mRefCanvas->setLayers( {} );
        if ( statusBar() )
          statusBar()->showMessage(
            tr( "请先 File → Load reference raster…" ), 5000 );
      }
      mRefCanvas->show();
      if ( mParamsPanel )
        mParamsPanel->setRpcMode( false );
      break;
    case RsGeorefModeToggle::RpcPhysical:
      mRefCanvas->hide();
      if ( mParamsPanel )
        mParamsPanel->setRpcMode( true );
      break;
  }

  mRefCanvas->refresh();

  // Task 11.5.2 carryover — re-sync canvas markers after mode change so the
  // GCP markers re-read their (possibly newly-reprojected) positions.
  for ( auto it = mDataPoints.begin(); it != mDataPoints.end(); ++it )
  {
    if ( it.value() )
      it.value()->updateMarkers();
  }
}

void QgsGeoreferencerMainWindow::closeEvent( QCloseEvent *e )
{
  // TODO Task 11.4.7: persist QgsSettings, ask about unsaved GCPs.
  e->accept();
}

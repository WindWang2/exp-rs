#include "qgsgeoreferencermainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMetaEnum>
#include <QMenu>
#include <QMenuBar>
#include <QPointF>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVector>
#include <QWidget>

#include <cmath>

#include "core/sicnu_logging.h"
#include "qgis.h"
#include "qgisinterface.h"
#include "qgsapplication.h"
#include "qgscoordinatereferencesystem.h"
#include "qgscoordinatetransformcontext.h"
#include "qgsgcplist.h"
#include "qgsgcplistwidget.h"
#include "qgsgcppoint.h"
#include "qgsgeorefdatapoint.h"
#include "qgsgeoreftooladdpoint.h"
#include "qgsgeoreftooldeletepoint.h"
#include "qgsgeoreftoolmovepoint.h"
#include "qgsgeoreftransform.h"
#include "qgsrpcgcptransformer.h"
#include "qgsmapcanvas.h"
#include "qgsmapcoordsdialog.h"
#include "qgsmaplayerstore.h"
#include "qgsmaptool.h"
#include "qgsmessagelog.h"
#include "qgsrasterlayer.h"
#include "qgstaskmanager.h"
#include "rs_georef_params_panel.h"
#include "rs_sift_dialog.h"
#include "rs_sift_task.h"
#include "rs_twincanvas_sync_controller.h"
#include "rs_warp_task.h"

QgsGeoreferencerMainWindow::QgsGeoreferencerMainWindow( QgisInterface *iface, QWidget *parent )
  : QMainWindow( parent )
  , mIface( iface )
{
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QStringLiteral( "Image 2 Image georef window opened" ) );
  setWindowTitle( tr( "Image Registration · Image 2 Image" ) );
  resize( 1200, 800 );

  // GCP list lives at the main window level — owns QgsGcpPoint instances.
  // QgsGCPList currently has a parameterless ctor (Task 3 design); parent it
  // explicitly so Qt cleans it up with the window.
  mGcps = new QgsGCPList();
  mGcps->setParent( this );

  // Task 11.6.2 — GCP list mutations mark the session dirty (except during
  // load/save which suppress via mSuppressDirtyFromList).
  connect( mGcps, &QgsGCPList::changed, this, [this]() {
    if ( !mSuppressDirtyFromList )
      mSession.markDirty();
  } );

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
  connect( mGcpTable, &QgsGCPListWidget::deleteRowsRequested,
           this, &QgsGeoreferencerMainWindow::deleteGcpRows );

  addDockWidget( Qt::BottomDockWidgetArea, mGcpDock );
  resizeDocks( { mGcpDock }, { 280 }, Qt::Vertical );

  // Task 11.4.7 — right param dock
  mParamDock = new QDockWidget( tr( "校正参数" ), this );
  mParamDock->setObjectName( QStringLiteral( "rsParamDock" ) );
  mParamDock->setAllowedAreas( Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea );
  mParamsPanel = new RsGeorefParamsPanel( mParamDock );
  mParamsPanel->setProfile( RsGeorefParamsPanel::Profile::ImageToImage );
  mParamsPanel->setRpcMode( false );
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
           [this]( const QString & ) {
             // Enable/disable Apply only — workflow paths persist on close / open.
             recomputeFit();
           } );
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

  // Dual-window redesign: this shell is fixed Image-to-Image. Mode toggle is
  // hidden (kept for session restore / tests that find the objectName).
  if ( mModeToggle )
  {
    mModeToggle->setObjectName( QStringLiteral( "rsGeorefModeToggle" ) );
    mModeToggle->setMode( RsGeorefModeToggle::ImageToImage );
    mModeToggle->hide();
    connect( mModeToggle, &RsGeorefModeToggle::modeChanged,
             this, &QgsGeoreferencerMainWindow::onModeChanged );
  }

  mParamsPanel->setActualGcpCount( 0 );

  // Force REF store path (Image-to-Image) once at end of setup.
  onModeChanged( RsGeorefModeToggle::ImageToImage );

  recomputeFit();

  // Task 11.6.2 — restore window geometry/docks + workflow panel settings.
  // Paths only (no auto-load of .points or rasters).
  mSession.restoreWindow( this );
  applyWorkflowSnapshot( mSession.restoreWorkflow() );
  // Re-pin I2I after snapshot (snapshot may restore a legacy mode index).
  if ( mModeToggle )
  {
    mModeToggle->setMode( RsGeorefModeToggle::ImageToImage );
    mModeToggle->hide();
  }
  mParamsPanel->setProfile( RsGeorefParamsPanel::Profile::ImageToImage );
  mParamsPanel->setRpcMode( false );
  onModeChanged( RsGeorefModeToggle::ImageToImage );
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
    QgsGeorefDataPoint *dp = mDataPoints.take( p );
    if ( mMovingPoint == dp )
      mMovingPoint = nullptr;
    if ( mHoveredPoint == dp )
      mHoveredPoint = nullptr;
    delete dp;
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

  // RMS over enabled GCP residual magnitudes (same units as the table / setRmsValues).
  double computeEnabledRms( QgsGCPList *gcps )
  {
    if ( !gcps )
      return 0.0;
    double totalSq = 0.0;
    int n = 0;
    for ( const QgsGcpPoint *p : std::as_const( *gcps ) )
    {
      if ( !p || !p->isEnabled() )
        continue;
      const QPointF r = p->residual();
      totalSq += r.x() * r.x() + r.y() * r.y();
      ++n;
    }
    return n > 0 ? std::sqrt( totalSq / n ) : 0.0;
  }
}

void QgsGeoreferencerMainWindow::recomputeFit()
{
  if ( !mParamsPanel || !mGcps )
    return;

  const QgsGcpTransformerInterface::TransformMethod method = mParamsPanel->transformMethod();
  const int minN = minimumGcpCountFor( method );
  mParamsPanel->setMinimumGcpCount( minN );

  // Collect enabled GCPs with destinations transformed into the target CRS —
  // same vectors residuals use (createGCPVectors). Fitting on raw
  // destinationPoint() while residuals re-projected destinations caused a
  // CRS mismatch. invertYAxis=true matches residual / QGIS raster CS policy.
  const QgsCoordinateReferenceSystem dstCrs = mParamsPanel->destCrs();
  const QgsCoordinateTransformContext transformContext;
  QVector<QgsPointXY> src;
  QVector<QgsPointXY> dst;
  mGcps->createGCPVectors( src, dst, dstCrs, transformContext );
  const int enabledCount = src.size();
  mParamsPanel->setActualGcpCount( enabledCount );

  constexpr bool kInvertYAxis = true;

  bool fitOk = false;

  // Task 6 / §5 — RPC with ≥ 3 enabled GCPs: dual-run unrefined then refined
  // so the params panel can show before/after RMS. Working mTransform keeps
  // the refined model for Apply and residual display.
  if ( method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical
       && enabledCount >= 3 )
  {
    double rmsBefore = -1.0;
    double rmsAfter = -1.0;

    // BEFORE — unrefined RPC
    {
      auto beforeXf = std::make_unique<QgsGeorefTransform>( method );
      if ( auto *rpc = dynamic_cast<QgsRpcGcpTransformer *>(
             beforeXf ? beforeXf->gcpTransformer() : nullptr ) )
      {
        rpc->setSourceRasterPath( mSourceRasterPath );
        rpc->setRpcOptions( mParamsPanel->demPath(),
                            mParamsPanel->demZOffset(),
                            /*useRefine=*/false );
      }
      try
      {
        if ( beforeXf && beforeXf->updateParametersFromGcps( src, dst, kInvertYAxis ) )
        {
          mGcps->updateResiduals( beforeXf.get(), dstCrs, transformContext );
          rmsBefore = computeEnabledRms( mGcps );
        }
      }
      catch ( ... )
      {
        // leave rmsBefore < 0
      }
    }

    // AFTER — refined working transform (Apply uses this)
    mTransform.reset( new QgsGeorefTransform( method ) );
    if ( auto *rpc = dynamic_cast<QgsRpcGcpTransformer *>(
           mTransform ? mTransform->gcpTransformer() : nullptr ) )
    {
      rpc->setSourceRasterPath( mSourceRasterPath );
      rpc->setRpcOptions( mParamsPanel->demPath(),
                          mParamsPanel->demZOffset(),
                          /*useRefine=*/true );
    }
    try
    {
      fitOk = mTransform->updateParametersFromGcps( src, dst, kInvertYAxis );
    }
    catch ( ... )
    {
      fitOk = false;
    }
    if ( fitOk )
    {
      mGcps->updateResiduals( mTransform.get(), dstCrs, transformContext );
      rmsAfter = computeEnabledRms( mGcps );
    }

    if ( rmsBefore >= 0.0 && rmsAfter >= 0.0 )
      mParamsPanel->setRefinementRms( rmsBefore, rmsAfter );
    else
      mParamsPanel->clearRefinementRms();
  }
  else
  {
    // Non-RPC, or RPC with fewer than 3 enabled GCPs: single fit path.
    mParamsPanel->clearRefinementRms();
    mTransform.reset( new QgsGeorefTransform( method ) );

    // Task 11.5.4/11.5.5 — push DEM/Z-offset into the RPC transformer.
    // Linear-bias refinement is off here (enabledCount < 3 for RPC).
    if ( method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical )
    {
      if ( auto *rpc = dynamic_cast<QgsRpcGcpTransformer *>(
             mTransform ? mTransform->gcpTransformer() : nullptr ) )
      {
        rpc->setSourceRasterPath( mSourceRasterPath );
        rpc->setRpcOptions( mParamsPanel->demPath(),
                            mParamsPanel->demZOffset(),
                            /*useRefine=*/false );
      }
    }

    if ( enabledCount >= minN && minN > 0 )
    {
      try
      {
        fitOk = mTransform->updateParametersFromGcps( src, dst, kInvertYAxis );
      }
      catch ( ... )
      {
        fitOk = false;
      }
    }
    // RPC minN is 0 (coefficients from metadata); still allow a fit with 0–2 GCPs.
    else if ( method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical
              && mTransform )
    {
      try
      {
        fitOk = mTransform->updateParametersFromGcps( src, dst, kInvertYAxis );
      }
      catch ( ... )
      {
        fitOk = false;
      }
    }
  }

  // Residuals + scatter (from final mTransform; dual path already wrote refined residuals)
  double totalSq = 0.0, xSq = 0.0, ySq = 0.0;
  double maxMag = 0.0;
  int maxRow = -1;
  QVector<QPointF> scatter;
  scatter.reserve( enabledCount );

  if ( fitOk )
  {
    // Residuals only compute distances; do not re-fit/mutate mTransform.
    mGcps->updateResiduals( mTransform.get(), dstCrs, transformContext );

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
    // Drop stale residuals (e.g. dual-RPC before succeeded, after failed).
    mGcps->clearResiduals();
    for ( auto it = mDataPoints.begin(); it != mDataPoints.end(); ++it )
    {
      if ( it.value() )
        it.value()->updateMarkers();
    }
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

  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QString( "Apply warp: %1 -> %2, %3 GCPs, method %4" )
    .arg( QFileInfo( mSourceRasterPath ).fileName() )
    .arg( QFileInfo( mParamsPanel->outputPath() ).fileName() )
    .arg( enabled )
    .arg( static_cast<int>( method ) ) );

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
      SICNU_LOG_SUCCESS( SicnuLogTags::Georeferencing, QString( "Warp completed: %1 (%2 bytes, %3 ms)" )
        .arg( mParamsPanel->outputPath() )
        .arg( task->result().outputBytes )
        .arg( task->result().durationMs ) );
      statusBar()->showMessage(
        tr( "已输出: %1 (%2 字节, %3 ms)" )
          .arg( mParamsPanel->outputPath() )
          .arg( task->result().outputBytes )
          .arg( task->result().durationMs ),
        6000 );
    }
    else
    {
      SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, QString( "Warp failed: %1" ).arg( task->result().errorMessage ) );
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
  mWarpInProgress = on;
  if ( mGcpTable )
    mGcpTable->setEnabled( !on );
  if ( mApplyAction )
    mApplyAction->setEnabled( !on );
}

bool QgsGeoreferencerMainWindow::isDirtyForTest() const
{
  return mSession.isDirty();
}

void QgsGeoreferencerMainWindow::markDirtyForTest()
{
  mSession.markDirty();
}

RsGeorefSessionState::WorkflowSnapshot QgsGeoreferencerMainWindow::captureWorkflowSnapshot() const
{
  RsGeorefSessionState::WorkflowSnapshot s;
  if ( mModeToggle )
    s.mode = static_cast<int>( mModeToggle->currentMode() );
  if ( mParamsPanel )
  {
    s.transformMethod = static_cast<int>( mParamsPanel->transformMethod() );
    s.resamplingMethod = static_cast<int>( mParamsPanel->resamplingMethod() );
    s.lastOutputPath = mParamsPanel->outputPath();
    s.lastDemPath = mParamsPanel->demPath();
    s.lastDestCrsAuthId = mParamsPanel->destCrs().authid();
    s.demZOffset = mParamsPanel->demZOffset();
  }
  s.lastSourcePath = mSourceRasterPath;
  s.lastRefPath = mRefRasterPath;
  s.lastPointsPath = mSession.lastPointsPath();
  s.syncZoom = mSyncZoomAction ? mSyncZoomAction->isChecked() : true;
  return s;
}

void QgsGeoreferencerMainWindow::applyWorkflowSnapshot( const RsGeorefSessionState::WorkflowSnapshot &s )
{
  // Mode first so setRpcMode() configures the transform combo visibility before
  // we restore the stored method index.
  if ( mModeToggle )
  {
    const auto mode = static_cast<RsGeorefModeToggle::Mode>( s.mode );
    if ( mode >= RsGeorefModeToggle::ImageToMap && mode <= RsGeorefModeToggle::RpcPhysical )
      mModeToggle->setMode( mode );
  }
  if ( mParamsPanel )
  {
    // Block panel signals so setOutputPath does not emit outputPathChanged and
    // overwrite QSettings with a half-applied snapshot mid-restore.
    const QSignalBlocker panelBlocker( mParamsPanel );
    mParamsPanel->setTransformMethod(
      static_cast<QgsGcpTransformerInterface::TransformMethod>( s.transformMethod ) );
    mParamsPanel->setResamplingMethod(
      static_cast<QgsImageWarper::ResamplingMethod>( s.resamplingMethod ) );
    if ( !s.lastOutputPath.isEmpty() )
      mParamsPanel->setOutputPath( s.lastOutputPath );
    if ( !s.lastDemPath.isEmpty() )
      mParamsPanel->setDemPath( s.lastDemPath );
    if ( !s.lastDestCrsAuthId.isEmpty() )
    {
      const QgsCoordinateReferenceSystem crs( s.lastDestCrsAuthId );
      if ( crs.isValid() )
        mParamsPanel->setDestCrs( crs );
    }
    mParamsPanel->setDemZOffset( s.demZOffset );
  }
  if ( !s.lastSourcePath.isEmpty() )
    mSourceRasterPath = s.lastSourcePath;
  if ( !s.lastRefPath.isEmpty() )
    mRefRasterPath = s.lastRefPath;
  if ( mSyncZoomAction )
    mSyncZoomAction->setChecked( s.syncZoom );
  // lastPointsPath is hydrated by restoreWorkflow() into mSession; do not
  // auto-load the .points file (spec: paths only).
  recomputeFit();
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
  mAddPointTool->setAction( mAddPointAction );
  connect( mAddPointTool, &QgsGeorefToolAddPoint::showCoordDialog,
           this, &QgsGeoreferencerMainWindow::showCoordDialog );

  // Task 11.6.3 — Move tools (SRC pixel + REF destination). Two-click begin/end.
  mToolMoveSrc = new QgsGeorefToolMovePoint( mSrcCanvas );
  mToolMoveSrc->setParent( this );
  mToolMoveSrc->setAction( mMovePointAction );
  mToolMoveDst = new QgsGeorefToolMovePoint( mRefCanvas );
  mToolMoveDst->setParent( this );
  mToolMoveDst->setAction( mMovePointAction );

  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointBeginMove, this, &QgsGeoreferencerMainWindow::selectPoint );
  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointMoving, this, &QgsGeoreferencerMainWindow::movePoint );
  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointEndMove, this, &QgsGeoreferencerMainWindow::releasePoint );
  connect( mToolMoveSrc, &QgsGeorefToolMovePoint::pointCancelMove, this, &QgsGeoreferencerMainWindow::cancelPoint );
  connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointBeginMove, this, &QgsGeoreferencerMainWindow::selectPoint );
  connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointMoving, this, &QgsGeoreferencerMainWindow::movePoint );
  connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointEndMove, this, &QgsGeoreferencerMainWindow::releasePoint );
  connect( mToolMoveDst, &QgsGeorefToolMovePoint::pointCancelMove, this, &QgsGeoreferencerMainWindow::cancelPoint );

  const auto clearMoveHover = [this]() {
    mMovingPoint = nullptr;
    if ( mHoveredPoint )
    {
      mHoveredPoint->setHovered( false );
      mHoveredPoint = nullptr;
    }
  };
  connect( mToolMoveSrc, &QgsMapTool::deactivated, this, clearMoveHover );
  connect( mToolMoveDst, &QgsMapTool::deactivated, this, clearMoveHover );

  // Task 11.6.3 — Delete tools on both canvases (shared hit-test slots).
  mToolDeleteSrc = new QgsGeorefToolDeletePoint( mSrcCanvas );
  mToolDeleteSrc->setParent( this );
  mToolDeleteSrc->setAction( mDeletePointAction );
  mToolDeleteDst = new QgsGeorefToolDeletePoint( mRefCanvas );
  mToolDeleteDst->setParent( this );
  mToolDeleteDst->setAction( mDeletePointAction );

  connect( mToolDeleteSrc, &QgsGeorefToolDeletePoint::deletePoint, this, &QgsGeoreferencerMainWindow::deletePointAt );
  connect( mToolDeleteSrc, &QgsGeorefToolDeletePoint::hoverPoint, this, &QgsGeoreferencerMainWindow::hoverPoint );
  connect( mToolDeleteDst, &QgsGeorefToolDeletePoint::deletePoint, this, &QgsGeoreferencerMainWindow::deletePointAt );
  connect( mToolDeleteDst, &QgsGeorefToolDeletePoint::hoverPoint, this, &QgsGeoreferencerMainWindow::hoverPoint );

  const auto clearDeleteHover = [this]() {
    if ( mHoveredPoint )
    {
      mHoveredPoint->setHovered( false );
      mHoveredPoint = nullptr;
    }
  };
  connect( mToolDeleteSrc, &QgsMapTool::deactivated, this, clearDeleteHover );
  connect( mToolDeleteDst, &QgsMapTool::deactivated, this, clearDeleteHover );

  // Wire exclusive toolbar actions to install map tools on the twin canvases.
  if ( mAddPointAction )
  {
    connect( mAddPointAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !on || !mSrcCanvas )
        return;
      mSrcCanvas->setMapTool( mAddPointTool );
      if ( mRefCanvas && mRefCanvas->mapTool()
           && ( mRefCanvas->mapTool() == mToolMoveDst || mRefCanvas->mapTool() == mToolDeleteDst ) )
        mRefCanvas->unsetMapTool( mRefCanvas->mapTool() );
    } );
  }
  if ( mMovePointAction )
  {
    connect( mMovePointAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !on )
        return;
      if ( mSrcCanvas )
        mSrcCanvas->setMapTool( mToolMoveSrc );
      if ( mRefCanvas )
        mRefCanvas->setMapTool( mToolMoveDst );
    } );
  }
  if ( mDeletePointAction )
  {
    connect( mDeletePointAction, &QAction::toggled, this, [this]( bool on ) {
      if ( !on )
        return;
      if ( mSrcCanvas )
        mSrcCanvas->setMapTool( mToolDeleteSrc );
      if ( mRefCanvas )
        mRefCanvas->setMapTool( mToolDeleteDst );
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
  fileMenu->addAction( tr( "Load .points..." ), this, &QgsGeoreferencerMainWindow::loadPoints );
  fileMenu->addAction( tr( "Save .points..." ), this, &QgsGeoreferencerMainWindow::savePoints );
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
  mModeToggle->hide(); // I2I shell: no entry mode toggle (dual-window redesign)
  mModeBar->addSeparator();

  // GCP ops — Add / Move / Delete are mutually exclusive map tools (wired in setupCentralWidget).
  mAddPointAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Add GCP" ) );
  mAddPointAction->setObjectName( QStringLiteral( "rsGeorefAddPointAction" ) );
  mAddPointAction->setCheckable( true );

  mMovePointAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Move GCP" ) );
  mMovePointAction->setObjectName( QStringLiteral( "rsGeorefMovePointAction" ) );
  mMovePointAction->setCheckable( true );

  mDeletePointAction = mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Delete GCP" ) );
  mDeletePointAction->setObjectName( QStringLiteral( "rsGeorefDeletePointAction" ) );
  mDeletePointAction->setCheckable( true );
  // Table-row delete remains available via Del key on the GCP table.

  auto *mapToolGroup = new QActionGroup( this );
  mapToolGroup->setExclusive( true );
  mapToolGroup->addAction( mAddPointAction );
  mapToolGroup->addAction( mMovePointAction );
  mapToolGroup->addAction( mDeletePointAction );

  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Load .gcp" ), this, &QgsGeoreferencerMainWindow::loadPoints );
  mModeBar->addAction( QIcon( QStringLiteral( ":/icons/r_ster_calc" ) ), tr( "Export .gcp" ), this, &QgsGeoreferencerMainWindow::savePoints );

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
        {
          QgsGcpPoint p( m.srcPx, m.dstWorld, destCrs, true );
          mGcps->appendPoint( p );
        }
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

QgsMapCanvas *QgsGeoreferencerMainWindow::pickCanvasForMode( RsGeorefModeToggle::Mode m ) const
{
  if ( m == RsGeorefModeToggle::ImageToImage )
    return mRefCanvas;
  if ( mIface && mIface->mapCanvas() )
    return mIface->mapCanvas();
  return mRefCanvas; // fallback when main app canvas is unavailable
}

QgsMapCanvas *QgsGeoreferencerMainWindow::pickCanvas() const
{
  const auto mode = mModeToggle ? mModeToggle->currentMode()
                                : RsGeorefModeToggle::ImageToMap;
  return pickCanvasForMode( mode );
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

  // Image→Map / RPC pick on the main app canvas; Image→Image on REF.
  // Fall back to REF when no main canvas is available.
  QgsMapCanvas *canvas = pickCanvas();
  const auto mode = mModeToggle ? mModeToggle->currentMode()
                                : RsGeorefModeToggle::ImageToMap;
  if ( statusBar()
       && mode != RsGeorefModeToggle::ImageToImage
       && !( mIface && mIface->mapCanvas() ) )
  {
    statusBar()->showMessage( tr( "主地图不可用，改用参考画布取点" ), 4000 );
  }

  auto *dlg = new QgsMapCoordsDialog( canvas, tempDataPoint, rasterCrs, this );
  dlg->setAttribute( Qt::WA_DeleteOnClose );
  tempDataPoint->setParent( dlg );

  // The dialog emits pointAdded(src, dst, destCrs) on OK. Persist into the
  // shared QgsGCPList — its changed() signal cascades to the model and the
  // table refreshes automatically.
  connect( dlg, &QgsMapCoordsDialog::pointAdded, this,
           [this]( const QgsPointXY &srcCoord, const QgsPointXY &dstCoord, const QgsCoordinateReferenceSystem &destCrs ) {
             if ( mGcps )
             {
               mGcps->appendPoint( QgsGcpPoint( srcCoord, dstCoord, destCrs, true ) );
               SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QString( "GCP added at src(%1, %2) -> dst(%3, %4)" )
                 .arg( srcCoord.x(), 0, 'f', 1 ).arg( srcCoord.y(), 0, 'f', 1 )
                 .arg( dstCoord.x(), 0, 'f', 1 ).arg( dstCoord.y(), 0, 'f', 1 ) );
             }
           } );

  // Clean up the heap-allocated preview helpers when the dialog is destroyed.
  connect( dlg, &QObject::destroyed, this, [tempGcp]() {
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
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, QString( "Failed to open source raster: %1" ).arg( path ) );
    delete layer;
    if ( statusBar() )
      statusBar()->showMessage( tr( "Failed to open raster: %1" ).arg( path ), 5000 );
    return;
  }
  if ( mRefStore )
    mRefStore->addMapLayer( layer );
  mSrcRaster = layer;
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QString( "Source raster loaded: %1" ).arg( QFileInfo( path ).fileName() ) );

  if ( mSrcCanvas )
  {
    mSrcCanvas->setLayers( { layer } );
    mSrcCanvas->setExtent( layer->extent() );
    mSrcCanvas->refresh();
  }

  // Task 11.6.2 — persist last source path immediately on success.
  mSession.saveWorkflow( captureWorkflowSnapshot() );
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
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, QString( "Failed to open reference raster: %1" ).arg( path ) );
    delete layer;
    return false;
  }
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QString( "Reference raster loaded: %1" ).arg( QFileInfo( path ).fileName() ) );
  if ( mRefStore )
    mRefStore->addMapLayer( layer );
  mRefRaster = layer;
  mRefRasterPath = path;

  // If already in Image-to-Image mode, swap in immediately. Otherwise
  // onModeChanged() will pick it up when the user switches modes.
  if ( mRefCanvas && mModeToggle
       && mModeToggle->currentMode() == RsGeorefModeToggle::ImageToImage )
  {
    mRefCanvas->setLayers( { layer } );
    mRefCanvas->setExtent( layer->extent() );
    mRefCanvas->refresh();
  }

  // Task 11.6.2 — persist last reference path immediately on success.
  mSession.saveWorkflow( captureWorkflowSnapshot() );
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
  if ( mWarpInProgress )
  {
    const auto ans = QMessageBox::question(
      this, tr( "几何校正" ),
      tr( "校正任务仍在运行，仍要关闭？" ),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No );
    if ( ans != QMessageBox::Yes )
    {
      e->ignore();
      return;
    }
  }

  if ( mSession.isDirty() )
  {
    const auto ans = QMessageBox::question(
      this, tr( "未保存的控制点" ),
      tr( "GCP 列表有未保存的更改。是否保存？" ),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save );
    if ( ans == QMessageBox::Cancel )
    {
      e->ignore();
      return;
    }
    if ( ans == QMessageBox::Save )
    {
      // Prefer last path; else file dialog via existing savePoints() pattern.
      QString path = mSession.lastPointsPath();
      if ( path.isEmpty() )
      {
        path = QFileDialog::getSaveFileName(
          this, tr( "Save GCP points" ), QString(),
          tr( "GCP Points (*.points *.gcp);;All files (*)" ) );
        if ( path.isEmpty() )
        {
          e->ignore();
          return;
        }
        if ( QFileInfo( path ).suffix().isEmpty() )
          path += QStringLiteral( ".points" );
      }
      if ( !mGcps || !mGcps->saveGcps( path ) )
      {
        QMessageBox::warning( this, tr( "Save GCPs" ),
                              tr( "保存失败，窗口未关闭。" ) );
        e->ignore();
        return;
      }
      SICNU_LOG_INFO( SicnuLogTags::Georeferencing,
                      QString( "Saved GCP points on close to %1" ).arg( path ) );
      if ( statusBar() )
        statusBar()->showMessage( tr( "已保存控制点: %1" ).arg( path ), 4000 );
      mSession.setLastPointsPath( path );
      mSession.clearDirty();
    }
  }

  mSession.saveWorkflow( captureWorkflowSnapshot() );
  mSession.saveWindow( this );
  e->accept();
}

void QgsGeoreferencerMainWindow::loadPoints()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Load GCP points" ),
    mSession.lastPointsPath(),
    tr( "GCP Points (*.points *.gcp);;All files (*)" ) );
  if ( path.isEmpty() )
    return;

  const QgsCoordinateReferenceSystem destCrs = mParamsPanel ? mParamsPanel->destCrs() : QgsCoordinateReferenceSystem();
  mSuppressDirtyFromList = true;
  const bool ok = mGcps->loadGcps( path, destCrs );
  mSuppressDirtyFromList = false;
  if ( !ok )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, QString( "Failed to load GCP points: %1" ).arg( path ) );
    QMessageBox::warning( this, tr( "Load GCPs" ), tr( "Failed to load GCP points from %1" ).arg( path ) );
  }
  else
  {
    mSession.setLastPointsPath( path );
    mSession.clearDirty();
    SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QString( "Loaded GCP points from %1" ).arg( path ) );
  }
}

void QgsGeoreferencerMainWindow::savePoints()
{
  const QString path = QFileDialog::getSaveFileName(
    this, tr( "Save GCP points" ),
    mSession.lastPointsPath(),
    tr( "GCP Points (*.points *.gcp);;All files (*)" ) );
  if ( path.isEmpty() )
    return;

  QString finalPath = path;
  if ( QFileInfo( finalPath ).suffix().isEmpty() )
  {
    finalPath += QStringLiteral( ".points" );
  }

  if ( !mGcps->saveGcps( finalPath ) )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, QString( "Failed to save GCP points: %1" ).arg( finalPath ) );
    QMessageBox::warning( this, tr( "Save GCPs" ), tr( "Failed to save GCP points to %1" ).arg( finalPath ) );
  }
  else
  {
    mSession.setLastPointsPath( finalPath );
    mSession.clearDirty();
    SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QString( "Saved GCP points to %1" ).arg( finalPath ) );
  }
}

void QgsGeoreferencerMainWindow::deleteSelectedGcp()
{
  if ( !mGcpTable )
    return;
  QModelIndexList selected = mGcpTable->selectionModel()->selectedRows();
  if ( selected.isEmpty() )
  {
    statusBar()->showMessage( tr( "请先在表格中选择要删除的 GCP" ), 3000 );
    return;
  }
  QList<int> rows;
  rows.reserve( selected.size() );
  for ( const QModelIndex &idx : selected )
  {
    rows.append( idx.row() );
  }
  deleteGcpRows( rows );
}

void QgsGeoreferencerMainWindow::deleteGcpRows( const QList<int> &rows )
{
  if ( !mGcps || rows.isEmpty() )
    return;
  QList<int> sortedRows = rows;
  std::sort( sortedRows.begin(), sortedRows.end(), std::greater<int>() );
  for ( int row : sortedRows )
  {
    if ( row >= 0 && row < mGcps->size() )
      mGcps->removePointAt( row );
  }
}

QgsGeorefDataPoint *QgsGeoreferencerMainWindow::findDataPoint( const QgsPointXY &p, QgsGcpPoint::PointType type )
{
  QgsGeorefDataPoint *nearest = nullptr;
  double bestDistance = -1.0;
  for ( auto it = mDataPoints.cbegin(); it != mDataPoints.cend(); ++it )
  {
    QgsGeorefDataPoint *dp = it.value();
    if ( !dp )
      continue;
    double distance = 0.0;
    if ( dp->contains( p, type, distance ) )
    {
      if ( bestDistance < 0.0 || distance < bestDistance )
      {
        bestDistance = distance;
        nearest = dp;
      }
    }
  }
  return nearest;
}

void QgsGeoreferencerMainWindow::selectPoint( const QgsPointXY &p )
{
  const bool isSrc = ( sender() == mToolMoveSrc );
  const QgsGcpPoint::PointType type = isSrc
                                        ? QgsGcpPoint::PointType::Source
                                        : QgsGcpPoint::PointType::Destination;
  QgsGeorefToolMovePoint *tool = isSrc ? mToolMoveSrc : mToolMoveDst;

  mMovingPoint = findDataPoint( p, type );
  if ( !mMovingPoint || !tool )
    return;

  if ( mHoveredPoint )
  {
    mHoveredPoint->setHovered( false );
    mHoveredPoint = nullptr;
  }

  mMoveOrigin = ( type == QgsGcpPoint::PointType::Source )
                  ? mMovingPoint->sourcePoint()
                  : mMovingPoint->destinationPoint();
  // Two-click move tool requires a non-empty start so subsequent clicks end the move.
  tool->setStartPoint( mMoveOrigin );
}

void QgsGeoreferencerMainWindow::movePoint( const QgsPointXY &p )
{
  const bool isSrc = ( sender() == mToolMoveSrc );
  const QgsGcpPoint::PointType type = isSrc
                                        ? QgsGcpPoint::PointType::Source
                                        : QgsGcpPoint::PointType::Destination;

  if ( mMovingPoint )
  {
    // Temporary coordinate update only — residual recompute waits for releasePoint.
    mMovingPoint->moveTo( p, type );
    return;
  }

  // Not yet dragging: hover highlight under cursor.
  QgsGeorefDataPoint *point = findDataPoint( p, type );
  if ( point )
    point->setHovered( true );
  if ( mHoveredPoint && point != mHoveredPoint )
    mHoveredPoint->setHovered( false );
  mHoveredPoint = point;
}

void QgsGeoreferencerMainWindow::releasePoint( const QgsPointXY &p )
{
  const bool isSrc = ( sender() == mToolMoveSrc );
  const QgsGcpPoint::PointType type = isSrc
                                        ? QgsGcpPoint::PointType::Source
                                        : QgsGcpPoint::PointType::Destination;
  QgsGeorefToolMovePoint *tool = isSrc ? mToolMoveSrc : mToolMoveDst;

  if ( tool )
    tool->setStartPoint( QgsPointXY() );

  if ( mMovingPoint )
  {
    // In-place QgsGcpPoint mutation — notify list so dirty/fit/table refresh.
    mMovingPoint = nullptr;
    if ( mGcps )
      mGcps->notifyPointsMutated();
  }
  else
  {
    mMovingPoint = nullptr;
  }

  QgsGeorefDataPoint *point = findDataPoint( p, type );
  if ( point )
    point->setHovered( true );
  if ( mHoveredPoint && point != mHoveredPoint )
    mHoveredPoint->setHovered( false );
  mHoveredPoint = point;
}

void QgsGeoreferencerMainWindow::cancelPoint( const QgsPointXY &p )
{
  const bool isSrc = ( sender() == mToolMoveSrc );
  const QgsGcpPoint::PointType type = isSrc
                                        ? QgsGcpPoint::PointType::Source
                                        : QgsGcpPoint::PointType::Destination;
  QgsGeorefToolMovePoint *tool = isSrc ? mToolMoveSrc : mToolMoveDst;

  if ( mMovingPoint )
  {
    // Restore pre-move origin (prefer tool startPoint; fall back to mMoveOrigin).
    const QgsPointXY origin = ( tool && !tool->startPoint().isEmpty() )
                                ? tool->startPoint()
                                : mMoveOrigin;
    if ( type == QgsGcpPoint::PointType::Source )
      mMovingPoint->setSourcePoint( origin );
    else
      mMovingPoint->setDestinationPoint( origin );
  }

  if ( tool )
    tool->setStartPoint( QgsPointXY() );
  mMovingPoint = nullptr;

  QgsGeorefDataPoint *point = findDataPoint( p, type );
  if ( point )
    point->setHovered( true );
  if ( mHoveredPoint && point != mHoveredPoint )
    mHoveredPoint->setHovered( false );
  mHoveredPoint = point;
}

void QgsGeoreferencerMainWindow::hoverPoint( const QgsPointXY &p )
{
  const QgsGcpPoint::PointType type = ( sender() == mToolDeleteSrc )
                                        ? QgsGcpPoint::PointType::Source
                                        : QgsGcpPoint::PointType::Destination;
  QgsGeorefDataPoint *point = findDataPoint( p, type );
  if ( point )
    point->setHovered( true );
  if ( mHoveredPoint && point != mHoveredPoint )
    mHoveredPoint->setHovered( false );
  mHoveredPoint = point;
}

void QgsGeoreferencerMainWindow::deletePointAt( const QgsPointXY &p )
{
  const QgsGcpPoint::PointType type = ( sender() == mToolDeleteSrc )
                                        ? QgsGcpPoint::PointType::Source
                                        : QgsGcpPoint::PointType::Destination;
  QgsGeorefDataPoint *dp = findDataPoint( p, type );
  if ( !dp || !mGcps )
    return;

  if ( mHoveredPoint == dp )
  {
    mHoveredPoint->setHovered( false );
    mHoveredPoint = nullptr;
  }
  if ( mMovingPoint == dp )
    mMovingPoint = nullptr;

  QgsGcpPoint *gcp = dp->gcpPoint();
  for ( int i = 0; i < mGcps->size(); ++i )
  {
    if ( mGcps->at( i ) == gcp )
    {
      mGcps->removePointAt( i );
      break;
    }
  }
}


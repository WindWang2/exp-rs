// rs_georeferencing_session.cpp — Shared Georeferencing Session (#32)
#include "rs_georeferencing_session.h"

#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "qgsgeoreftransform.h"
#include "qgslogger.h"
#include "rs_warp_task.h"

#include <QByteArray>
#include <QMainWindow>
#include <QSettings>
#include <QWidget>
#include <cmath>

namespace
{

constexpr auto kPrefix = "Georeferencer/";

} // namespace

RsGeoreferencingSession::RsGeoreferencingSession( QObject *parent )
  : QObject( parent )
  , mWorkflowRuntime()
{
  connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated,
           this, &RsGeoreferencingSession::onTaskUpdated );
}

RsGeoreferencingSession::RsGeoreferencingSession(
  CustomWarpExecutor customExecutor, QObject *parent )
  : QObject( parent )
  , mCustomExecutor( std::move( customExecutor ) )
  , mWorkflowRuntime()
{
  connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated,
           this, &RsGeoreferencingSession::onTaskUpdated );
}

RsGeoreferencingSession::~RsGeoreferencingSession() = default;

void RsGeoreferencingSession::setLastPointsPath( const QString &path )
{
  mLastPointsPath = path;
  QSettings().setValue( QStringLiteral( "%1lastPointsPath" ).arg( QLatin1String( kPrefix ) ), path );
}

void RsGeoreferencingSession::saveWindow( QWidget *w )
{
  if ( !w )
    return;
  QSettings().setValue( QStringLiteral( "%1geometry" ).arg( QLatin1String( kPrefix ) ), w->saveGeometry() );
}

void RsGeoreferencingSession::restoreWindow( QWidget *w )
{
  if ( !w )
    return;
  const auto g = QSettings().value( QStringLiteral( "%1geometry" ).arg( QLatin1String( kPrefix ) ) ).toByteArray();
  if ( !g.isEmpty() )
    w->restoreGeometry( g );
}

void RsGeoreferencingSession::saveWorkflow( const WorkflowSnapshot &snap )
{
  QSettings s;
  s.setValue( QStringLiteral( "%1mode" ).arg( QLatin1String( kPrefix ) ), snap.mode );
  s.setValue( QStringLiteral( "%1transformMethod" ).arg( QLatin1String( kPrefix ) ), snap.transformMethod );
  s.setValue( QStringLiteral( "%1resamplingMethod" ).arg( QLatin1String( kPrefix ) ), snap.resamplingMethod );
  s.setValue( QStringLiteral( "%1lastSourcePath" ).arg( QLatin1String( kPrefix ) ), snap.lastSourcePath );
  s.setValue( QStringLiteral( "%1lastRefPath" ).arg( QLatin1String( kPrefix ) ), snap.lastRefPath );
  s.setValue( QStringLiteral( "%1lastOutputPath" ).arg( QLatin1String( kPrefix ) ), snap.lastOutputPath );
  s.setValue( QStringLiteral( "%1lastDemPath" ).arg( QLatin1String( kPrefix ) ), snap.lastDemPath );
  s.setValue( QStringLiteral( "%1lastPointsPath" ).arg( QLatin1String( kPrefix ) ), snap.lastPointsPath );
  s.setValue( QStringLiteral( "%1lastDestCrs" ).arg( QLatin1String( kPrefix ) ), snap.lastDestCrsAuthId );
  s.setValue( QStringLiteral( "%1demZOffset" ).arg( QLatin1String( kPrefix ) ), snap.demZOffset );
  s.setValue( QStringLiteral( "%1syncZoom" ).arg( QLatin1String( kPrefix ) ), snap.syncZoom );
  mLastPointsPath = snap.lastPointsPath;
}

RsGeoreferencingSession::WorkflowSnapshot RsGeoreferencingSession::restoreWorkflow()
{
  QSettings s;
  WorkflowSnapshot o;
  o.mode = s.value( QStringLiteral( "%1mode" ).arg( QLatin1String( kPrefix ) ), 0 ).toInt();
  o.transformMethod = s.value( QStringLiteral( "%1transformMethod" ).arg( QLatin1String( kPrefix ) ), 0 ).toInt();
  o.resamplingMethod = s.value( QStringLiteral( "%1resamplingMethod" ).arg( QLatin1String( kPrefix ) ), 0 ).toInt();
  o.lastSourcePath = s.value( QStringLiteral( "%1lastSourcePath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastRefPath = s.value( QStringLiteral( "%1lastRefPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastOutputPath = s.value( QStringLiteral( "%1lastOutputPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastDemPath = s.value( QStringLiteral( "%1lastDemPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastPointsPath = s.value( QStringLiteral( "%1lastPointsPath" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.lastDestCrsAuthId = s.value( QStringLiteral( "%1lastDestCrs" ).arg( QLatin1String( kPrefix ) ) ).toString();
  o.demZOffset = s.value( QStringLiteral( "%1demZOffset" ).arg( QLatin1String( kPrefix ) ), 0.0 ).toDouble();
  o.syncZoom = s.value( QStringLiteral( "%1syncZoom" ).arg( QLatin1String( kPrefix ) ), true ).toBool();
  mLastPointsPath = o.lastPointsPath;
  return o;
}

bool RsGeoreferencingSession::enableWorkflowMirror( const std::string &definitionId )
{
  mWorkflowSessionId = mWorkflowRuntime.open( definitionId );
  if ( mWorkflowSessionId.empty() )
    return false;

  if ( !mSourcePath.isEmpty() )
  {
    mWorkflowRuntime.setArtifact( mWorkflowSessionId, "source_raster", mSourcePath.toStdString() );
    mWorkflowRuntime.markStepComplete( mWorkflowSessionId, "open_image" );
    mWorkflowRuntime.gotoStep( mWorkflowSessionId, "gcp" );
  }
  syncWorkflowGcps();
  return true;
}

void RsGeoreferencingSession::setWorkflowStep( const std::string &stepId )
{
  if ( isWorkflowMirrorActive() )
    mWorkflowRuntime.gotoStep( mWorkflowSessionId, stepId );
}

void RsGeoreferencingSession::markWorkflowStepComplete( const std::string &stepId )
{
  if ( isWorkflowMirrorActive() )
    mWorkflowRuntime.markStepComplete( mWorkflowSessionId, stepId );
}

void RsGeoreferencingSession::syncWorkflowGcps()
{
  if ( !isWorkflowMirrorActive() )
    return;
  const int n = QgsGeorefTransform::enabledGcpCount( mGcps );
  mWorkflowRuntime.setArtifact(
    mWorkflowSessionId, "gcp_count", n > 0 ? std::to_string( n ) : std::string() );
}

void RsGeoreferencingSession::setSourceRasterPath( const QString &path )
{
  mSourcePath = path;
  markDirty();
  if ( isWorkflowMirrorActive() && !path.isEmpty() )
  {
    mWorkflowRuntime.setArtifact( mWorkflowSessionId, "source_raster", path.toStdString() );
    mWorkflowRuntime.markStepComplete( mWorkflowSessionId, "open_image" );
    mWorkflowRuntime.gotoStep( mWorkflowSessionId, "gcp" );
  }
}

void RsGeoreferencingSession::setTransformMethod(
  QgsGcpTransformerInterface::TransformMethod method )
{
  mMethod = method;
  markDirty();
}

void RsGeoreferencingSession::setDemPath( const QString &path )
{
  if ( mDemPath == path )
    return;
  mDemPath = path;
  markDirty();
}

void RsGeoreferencingSession::setDemZOffset( double z )
{
  if ( mDemZOffset == z )
    return;
  mDemZOffset = z;
  markDirty();
}

void RsGeoreferencingSession::setDestinationCrs( const QgsCoordinateReferenceSystem &crs )
{
  if ( mDestCrs == crs )
    return;
  mDestCrs = crs;
  markDirty();
  refit();
}

void RsGeoreferencingSession::setGcps( const QVector<QgsGcpPoint> &gcps )
{
  mGcps = gcps;
  markDirty();
  syncWorkflowGcps();
  emit gcpsChanged();
}

void RsGeoreferencingSession::clearGcps()
{
  if ( mGcps.isEmpty() )
    return;
  mGcps.clear();
  mLastFit = RsGeorefFitResult{};
  markDirty();
  syncWorkflowGcps();
  emit gcpsChanged();
  emit fitChanged( mLastFit );
}

void RsGeoreferencingSession::addGcp( const QgsGcpPoint &gcp )
{
  mGcps.append( gcp );
  markDirty();
  syncWorkflowGcps();
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::appendGcps( const QVector<QgsGcpPoint> &gcps )
{
  if ( gcps.isEmpty() )
    return;
  mGcps.reserve( mGcps.size() + gcps.size() );
  for ( const auto &p : gcps )
    mGcps.append( p );
  markDirty();
  syncWorkflowGcps();
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::removeGcpAt( int row )
{
  if ( row < 0 || row >= mGcps.size() )
    return;
  mGcps.removeAt( row );
  markDirty();
  syncWorkflowGcps();
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::setGcpEnabled( int row, bool enabled )
{
  if ( row < 0 || row >= mGcps.size() )
    return;
  if ( mGcps[row].isEnabled() == enabled )
    return;
  mGcps[row].setEnabled( enabled );
  markDirty();
  syncWorkflowGcps();
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::setGcpSource( int row, const QgsPointXY &source )
{
  if ( row < 0 || row >= mGcps.size() )
    return;
  mGcps[row].setSourcePoint( source );
  markDirty();
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::setGcpDestination( int row, const QgsPointXY &destination )
{
  if ( row < 0 || row >= mGcps.size() )
    return;
  mGcps[row].setDestinationPoint( destination );
  markDirty();
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::setGcpPointType( int row, const QString &pointType )
{
  if ( row < 0 || row >= mGcps.size() )
    return;
  if ( mGcps[row].pointType() == pointType )
    return;
  mGcps[row].setPointType( pointType );
  markDirty();
  emit gcpsChanged();
  refit();
}

RsGeorefFitResult RsGeoreferencingSession::refit()
{
  // All fit orchestration lives in QgsGeorefTransform::fit (ADR 0057):
  // enabled-GCP collection, min-count gating, the RPC before/after
  // double-fit, per-point source-pixel residuals, and RMS.
  // Pass the session's destination CRS so residuals are computed in the
  // same coordinate space as the actual warp transformation (#408).
  mLastFit = QgsGeorefTransform::fit( mGcps, mMethod, mSourcePath, mDemPath, mDemZOffset, mDestCrs );
  emit fitChanged( mLastFit );
  return mLastFit;
}

std::optional<RsGeorefWarpSnapshot> RsGeoreferencingSession::createWarpSnapshot(
  const QString &outputPath,
  QgsImageWarper::ResamplingMethod resampling,
  const QgsCoordinateReferenceSystem &destCrs,
  double pixelSize ) const
{
  if ( mSourcePath.isEmpty() || outputPath.isEmpty() )
    return std::nullopt;
  if ( !mLastFit.ready )
    return std::nullopt;
  // Mirror QgsGeorefShellWindow::applyTransform: refuse when the live GCP list
  // no longer meets the method minimum (e.g. GCPs disabled after the fit).
  if ( QgsGeorefTransform::enabledGcpCount( mGcps ) < QgsGeorefTransform::minimumGcpCountFor( mMethod ) )
    return std::nullopt;

  RsGeorefWarpSnapshot snap;
  snap.sourcePath = mSourcePath;
  snap.outputPath = outputPath;
  snap.gcps = mGcps; // deep copy of QVector / QgsPointXY
  snap.method = mMethod;
  snap.resampling = resampling;
  snap.destCrs = destCrs;
  snap.demPath = mDemPath;
  snap.demZOffset = mDemZOffset;
  snap.pixelSize = pixelSize;
  snap.rmsAtCapture = mLastFit.rms;
  return snap;
}

std::unique_ptr<QgsGeorefTransform> RsGeoreferencingSession::transformFromSnapshot(
  const RsGeorefWarpSnapshot &snap )
{
  try
  {
    auto transform = std::make_unique<QgsGeorefTransform>( snap.method );
    if ( !snap.sourcePath.isEmpty() )
      transform->loadRaster( snap.sourcePath );

    if ( QgsGcpTransformerInterface *impl = transform->gcpTransformer() )
    {
      const bool rpcRefinement = ( snap.method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
      impl->setRpcOptions( snap.sourcePath, snap.demPath, snap.demZOffset, rpcRefinement );
      impl->setDestinationCrs( snap.destCrs );
    }

    QVector<QgsPointXY> src;
    QVector<QgsPointXY> dst;
    bool collectOk = true;
    QgsGeorefTransform::collectEnabledGcps( snap.gcps, src, dst, snap.destCrs, QgsCoordinateTransformContext(), &collectOk );
    if ( !collectOk )
      return nullptr;

    constexpr bool kInvertYAxis = true;
    if ( !transform->updateParametersFromGcps( src, dst, kInvertYAxis ) )
      return nullptr;
    return transform;
  }
  catch ( const std::exception &e )
  {
    QgsDebugError( QStringLiteral( "transformFromSnapshot failed with exception: %1" ).arg( QString::fromUtf8( e.what() ) ) );
    return nullptr;
  }
  catch ( ... )
  {
    QgsDebugError( QStringLiteral( "transformFromSnapshot failed with unknown exception" ) );
    return nullptr;
  }
}

long RsGeoreferencingSession::startWarpTask( const RsGeorefWarpSnapshot &snap )
{
  if ( mPendingWarpTaskId >= 0 )
    return -1;

  std::unique_ptr<QgsGeorefTransform> transform;
  try
  {
    transform = transformFromSnapshot( snap );
  }
  catch ( const std::exception &e )
  {
    QgsDebugError( QStringLiteral( "startWarpTask failed: %1" ).arg( QString::fromUtf8( e.what() ) ) );
    return -1;
  }
  if ( !transform )
    return -1;

  // RsWarpTask copies the transform.
  auto *task = new RsWarpTask( snap.sourcePath, snap.outputPath, transform.get(),
                               snap.resampling, snap.destCrs, snap.pixelSize, snap.backgroundValue );

  sicnu::jobs::JobRequest req;
  req.algorithmId = "module:georef:warp";
  req.title = QStringLiteral( "Georef warp: %1" )
                .arg( snap.outputPath )
                .toStdString();
  req.source = "module";
  req.exclusive = true;
  req.params["input"] = snap.sourcePath.toStdString();
  req.params["output"] = snap.outputPath.toStdString();

  mPendingSnap = snap;
  mPendingWarpTask = task;

  auto jobExec = [task]( const sicnu::jobs::JobRequest &request,
                        sicnu::operators::RSOperatorContext &ctx ) {
    ctx.logInfo( "Georef warp" );
    ctx.reportProgress( 0.0, "Warping" );
    const bool ok = task->run();
    if ( ctx.isCancelled()
         || task->result().status == QgsImageWarper::WarpStatus::Cancelled )
    {
      throw sicnu::operators::RSOperatorError(
        sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
    }
    if ( !ok || task->result().status != QgsImageWarper::WarpStatus::Ok )
    {
      throw sicnu::operators::RSOperatorError(
        sicnu::operators::ErrorCode::ComputationError,
        task->result().errorMessage.toStdString() );
    }
    Json::Value result( Json::objectValue );
    result["output"] = request.params.get( "output", "" ).asString();
    result["durationMs"] = static_cast<Json::Int64>( task->result().durationMs );
    return result;
  };

  auto cancelHook = [task]() { task->cancel(); };

  const long taskId = mCustomExecutor.submit
    ? mCustomExecutor.submit( req, jobExec, cancelHook )
    : sicnu::TaskCenter::instance().submitJob( req, jobExec, cancelHook );

  if ( taskId < 0 )
  {
    delete task;
    mPendingWarpTask = nullptr;
    mPendingSnap = RsGeorefWarpSnapshot();
    mPendingWarpTaskId = -1;
    return -1;
  }

  mPendingWarpTaskId = taskId;
  return taskId;
}

bool RsGeoreferencingSession::cancelWarpTask( long taskCenterId )
{
  if ( taskCenterId < 0 || taskCenterId != mPendingWarpTaskId )
    return false;
  if ( mCustomExecutor.cancel )
    return mCustomExecutor.cancel( taskCenterId );
  return sicnu::TaskCenter::instance().cancelTask( taskCenterId );
}

void RsGeoreferencingSession::onTaskUpdated( const sicnu::AlgorithmTaskInfo &info )
{
  if ( info.taskId != mPendingWarpTaskId || mPendingWarpTaskId < 0 )
    return;
  if ( info.status != sicnu::TaskStatus::Completed
       && info.status != sicnu::TaskStatus::Failed
       && info.status != sicnu::TaskStatus::Canceled )
    return;

  const long id = mPendingWarpTaskId;
  const QString out = mPendingSnap.outputPath;
  RsWarpTask *task = mPendingWarpTask;
  mPendingWarpTaskId = -1;
  mPendingWarpTask = nullptr;

  bool ok = false;
  QString err;
  if ( info.status == sicnu::TaskStatus::Completed
       && task && task->result().status == QgsImageWarper::WarpStatus::Ok )
  {
    ok = true;
  }
  else if ( info.status == sicnu::TaskStatus::Canceled )
  {
    err = QStringLiteral( "Cancelled" );
  }
  else
  {
    err = ( task && !task->result().errorMessage.isEmpty() )
            ? task->result().errorMessage
            : info.errorMessage;
    if ( err.isEmpty() )
      err = QStringLiteral( "Warp failed" );
  }

  if ( ok && isWorkflowMirrorActive() )
  {
    mWorkflowRuntime.setArtifact( mWorkflowSessionId, "output", out.toStdString() );
    mWorkflowRuntime.markStepComplete( mWorkflowSessionId, "warp" );
    mWorkflowRuntime.gotoStep( mWorkflowSessionId, "warp" );
  }

  if ( task )
    task->deleteLater();

  emit warpFinished( id, ok, err, out );
}

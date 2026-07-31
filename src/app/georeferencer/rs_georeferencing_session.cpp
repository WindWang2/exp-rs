// rs_georeferencing_session.cpp — Shared Georeferencing Session (#32)
#include "rs_georeferencing_session.h"

#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "qgsgeoreftransform.h"
#include "qgsrpcgcptransformer.h"
#include "rs_georef_task_center_executor.h"
#include "rs_warp_task.h"

#include <cmath>

namespace
{

int enabledCount( const QVector<RsGeorefGcpPair> &gcps )
{
  int n = 0;
  for ( const auto &g : gcps )
  {
    if ( g.enabled )
      ++n;
  }
  return n;
}

void collectEnabled( const QVector<RsGeorefGcpPair> &gcps,
                     QVector<QgsPointXY> &src, QVector<QgsPointXY> &dst )
{
  src.clear();
  dst.clear();
  for ( const auto &g : gcps )
  {
    if ( !g.enabled )
      continue;
    src.append( g.source );
    dst.append( g.destination );
  }
}

// Source-pixel RMS over (src, dst) pairs: back-transform each destination
// point into source pixel space, residual = predicted - observed pixel,
// RMS = sqrt( mean( dx^2 + dy^2 ) ). Returns -1 when no point transforms.
double pixelRms( QgsGeorefTransform *xf,
                 const QVector<QgsPointXY> &src,
                 const QVector<QgsPointXY> &dst )
{
  if ( !xf )
    return -1.0;
  double sumSq = 0.0;
  int n = 0;
  for ( int i = 0; i < src.size(); ++i )
  {
    QgsPointXY predicted;
    if ( !xf->transformWorldToRaster( dst.at( i ), predicted ) )
      continue;
    const QgsPointXY observed = xf->toSourcePixel( src.at( i ) );
    const double dx = predicted.x() - observed.x();
    const double dy = predicted.y() - observed.y();
    sumSq += dx * dx + dy * dy;
    ++n;
  }
  return ( n > 0 ) ? std::sqrt( sumSq / static_cast<double>( n ) ) : -1.0;
}

} // namespace

RsGeoreferencingSession::RsGeoreferencingSession( QObject *parent )
  : RsGeoreferencingSession( nullptr, parent )
{
}

RsGeoreferencingSession::RsGeoreferencingSession(
  std::shared_ptr<RsGeorefWarpExecutor> executor, QObject *parent )
  : QObject( parent )
  , mExecutor( std::move( executor ) )
{
  if ( !mExecutor )
    mExecutor = std::make_shared<RsGeorefTaskCenterExecutor>();
  connect( mExecutor.get(), &RsGeorefWarpExecutor::taskUpdated,
           this, &RsGeoreferencingSession::onTaskUpdated );
}

RsGeoreferencingSession::~RsGeoreferencingSession() = default;

void RsGeoreferencingSession::setSourceRasterPath( const QString &path )
{
  mSourcePath = path;
}

void RsGeoreferencingSession::setTransformMethod(
  QgsGcpTransformerInterface::TransformMethod method )
{
  mMethod = method;
}

void RsGeoreferencingSession::setGcps( const QVector<RsGeorefGcpPair> &gcps )
{
  mGcps = gcps;
  emit gcpsChanged();
}

void RsGeoreferencingSession::clearGcps()
{
  if ( mGcps.isEmpty() )
    return;
  mGcps.clear();
  mLastFit = RsGeorefFitResult{};
  emit gcpsChanged();
  emit fitChanged( mLastFit );
}

void RsGeoreferencingSession::addGcp( const RsGeorefGcpPair &gcp )
{
  mGcps.append( gcp );
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::appendGcps( const QVector<RsGeorefGcpPair> &gcps )
{
  if ( gcps.isEmpty() )
    return;
  mGcps.reserve( mGcps.size() + gcps.size() );
  for ( const auto &p : gcps )
    mGcps.append( p );
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::removeGcpAt( int row )
{
  if ( row < 0 || row >= mGcps.size() )
    return;
  mGcps.removeAt( row );
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::setGcpEnabled( int row, bool enabled )
{
  if ( row < 0 || row >= mGcps.size() )
    return;
  if ( mGcps[row].enabled == enabled )
    return;
  mGcps[row].enabled = enabled;
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::setGcpSource( int row, const QgsPointXY &source )
{
  if ( row < 0 || row >= mGcps.size() )
    return;
  mGcps[row].source = source;
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::setGcpDestination( int row, const QgsPointXY &destination )
{
  if ( row < 0 || row >= mGcps.size() )
    return;
  mGcps[row].destination = destination;
  emit gcpsChanged();
  refit();
}

void RsGeoreferencingSession::setGcpPointType( int row, const QString &pointType )
{
  if ( row < 0 || row >= mGcps.size() )
    return;
  if ( mGcps[row].pointType == pointType )
    return;
  mGcps[row].pointType = pointType;
  emit gcpsChanged();
  refit();
}

RsGeorefFitResult RsGeoreferencingSession::refit()
{
  RsGeorefFitResult fit;
  fit.enabledGcpCount = enabledCount( mGcps );
  fit.residuals = QVector<QPointF>( mGcps.size(), rsGeorefInvalidResidual() );

  QgsGeorefTransform probe( mMethod );
  fit.minimumGcpCount = probe.minimumGcpCount();

  if ( fit.enabledGcpCount < fit.minimumGcpCount )
  {
    fit.ready = false;
    fit.errorMessage = QStringLiteral( "Need at least %1 GCPs, have %2" )
                         .arg( fit.minimumGcpCount )
                         .arg( fit.enabledGcpCount );
    mLastFit = fit;
    emit fitChanged( mLastFit );
    return mLastFit;
  }

  QVector<QgsPointXY> src;
  QVector<QgsPointXY> dst;
  collectEnabled( mGcps, src, dst );

  // Match shell: always invert Y for GCP parameter estimation.
  constexpr bool kInvertYAxis = true;

  std::unique_ptr<QgsGeorefTransform> transform;
  bool fitOk = false;

  if ( mMethod == QgsGcpTransformerInterface::TransformMethod::RpcPhysical
       && fit.enabledGcpCount >= 3 )
  {
    // RPC refinement (ported from the shell's recomputeFit): fit once without
    // GCP-bias refinement for the before/after diagnostic, then fit the live
    // transform with refinement enabled.
    {
      auto beforeXf = makeConfiguredTransform( /*rpcRefinement=*/false );
      try
      {
        if ( beforeXf && beforeXf->updateParametersFromGcps( src, dst, kInvertYAxis ) )
          fit.refinementRmsBefore = pixelRms( beforeXf.get(), src, dst );
      }
      catch ( ... ) {}
    }
    transform = makeConfiguredTransform( /*rpcRefinement=*/true );
    try
    {
      fitOk = transform && transform->updateParametersFromGcps( src, dst, kInvertYAxis );
    }
    catch ( ... )
    {
      fitOk = false;
    }
  }
  else
  {
    transform = makeConfiguredTransform( /*rpcRefinement=*/false );
    try
    {
      fitOk = transform && transform->updateParametersFromGcps( src, dst, kInvertYAxis );
    }
    catch ( ... )
    {
      fitOk = false;
    }
  }

  if ( !fitOk )
  {
    fit.ready = false;
    fit.errorMessage = QStringLiteral( "Parameter estimation failed" );
    mLastFit = fit;
    emit fitChanged( mLastFit );
    return mLastFit;
  }

  // Per-point residuals in SOURCE PIXELS (ADR 0020 decision 2): back-transform
  // each destination point into source pixel space and take the Euclidean
  // delta against the observed source pixel. Disabled GCPs keep the sentinel.
  double sumSq = 0.0;
  int n = 0;
  for ( int i = 0; i < mGcps.size(); ++i )
  {
    const RsGeorefGcpPair &g = mGcps.at( i );
    if ( !g.enabled )
      continue;
    QgsPointXY predicted;
    if ( !transform->transformWorldToRaster( g.destination, predicted ) )
      continue;
    const QgsPointXY observed = transform->toSourcePixel( g.source );
    const QPointF r( predicted.x() - observed.x(), predicted.y() - observed.y() );
    fit.residuals[i] = r;
    sumSq += r.x() * r.x() + r.y() * r.y();
    ++n;
  }
  fit.ready = true;
  fit.rms = ( n > 0 ) ? std::sqrt( sumSq / static_cast<double>( n ) ) : -1.0;
  fit.errorMessage.clear();
  mLastFit = fit;
  emit fitChanged( mLastFit );
  return mLastFit;
}

std::unique_ptr<QgsGeorefTransform> RsGeoreferencingSession::makeConfiguredTransform(
  bool rpcRefinement ) const
{
  auto transform = std::make_unique<QgsGeorefTransform>( mMethod );
  if ( !mSourcePath.isEmpty() )
    transform->loadRaster( mSourcePath );
  if ( auto *rpc = dynamic_cast<QgsRpcGcpTransformer *>( transform->gcpTransformer() ) )
  {
    rpc->setSourceRasterPath( mSourcePath );
    rpc->setRpcOptions( mDemPath, mDemZOffset, rpcRefinement );
  }
  return transform;
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
  QgsGeorefTransform probe( mMethod );
  if ( enabledCount( mGcps ) < probe.minimumGcpCount() )
    return std::nullopt;

  RsGeorefWarpSnapshot snap;
  snap.sourcePath = mSourcePath;
  snap.outputPath = outputPath;
  snap.gcps = mGcps; // deep copy of QVector / QgsPointXY
  snap.method = mMethod;
  snap.resampling = resampling;
  snap.destCrs = destCrs;
  snap.pixelSize = pixelSize;
  snap.rmsAtCapture = mLastFit.rms;
  return snap;
}

std::unique_ptr<QgsGeorefTransform> RsGeoreferencingSession::transformFromSnapshot(
  const RsGeorefWarpSnapshot &snap )
{
  auto transform = std::make_unique<QgsGeorefTransform>( snap.method );
  if ( !snap.sourcePath.isEmpty() )
    transform->loadRaster( snap.sourcePath );

  QVector<QgsPointXY> src;
  QVector<QgsPointXY> dst;
  collectEnabled( snap.gcps, src, dst );

  constexpr bool kInvertYAxis = true;
  if ( !transform->updateParametersFromGcps( src, dst, kInvertYAxis ) )
    return nullptr;
  return transform;
}

long RsGeoreferencingSession::startWarpTask( const RsGeorefWarpSnapshot &snap )
{
  if ( mPendingWarpTaskId >= 0 )
    return -1;

  auto transform = transformFromSnapshot( snap );
  if ( !transform )
    return -1;

  // RsWarpTask copies the transform.
  auto *task = new RsWarpTask( snap.sourcePath, snap.outputPath, transform.get(),
                               snap.resampling, snap.destCrs, snap.pixelSize );

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

  const long taskId = mExecutor->submitWarp(
    req,
    [task]( const sicnu::jobs::JobRequest &request,
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
      result["outputBytes"] = static_cast<Json::Int64>( task->result().outputBytes );
      return result;
    },
    [task]() { task->cancel(); } );

  mPendingWarpTaskId = taskId;
  return taskId;
}

bool RsGeoreferencingSession::cancelWarpTask( long taskCenterId )
{
  if ( taskCenterId < 0 || taskCenterId != mPendingWarpTaskId )
    return false;
  return mExecutor->cancelWarp( taskCenterId );
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

  if ( task )
    task->deleteLater();

  emit warpFinished( id, ok, err, out );
}

// rs_georeferencing_session.cpp — Shared Georeferencing Session (#32)
#include "rs_georeferencing_session.h"

#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "qgsgeoreftransform.h"
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

} // namespace

RsGeoreferencingSession::RsGeoreferencingSession( QObject *parent )
  : QObject( parent )
{
  connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated,
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
}

void RsGeoreferencingSession::clearGcps()
{
  mGcps.clear();
  mLastFit = RsGeorefFitResult{};
  emit fitChanged( mLastFit );
}

RsGeorefFitResult RsGeoreferencingSession::refit()
{
  RsGeorefFitResult fit;
  fit.enabledGcpCount = enabledCount( mGcps );

  QgsGeorefTransform transform( mMethod );
  if ( !mSourcePath.isEmpty() )
    transform.loadRaster( mSourcePath );

  fit.minimumGcpCount = transform.minimumGcpCount();

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
  const bool ok = transform.updateParametersFromGcps( src, dst, kInvertYAxis );
  if ( !ok )
  {
    fit.ready = false;
    fit.errorMessage = QStringLiteral( "Parameter estimation failed" );
    mLastFit = fit;
    emit fitChanged( mLastFit );
    return mLastFit;
  }

  // Residual RMS in destination units (simple Euclidean mean of residuals).
  double sumSq = 0.0;
  int n = 0;
  for ( int i = 0; i < src.size(); ++i )
  {
    QgsPointXY mapped;
    if ( !transform.transform( src.at( i ), mapped, true ) )
      continue;
    const double dx = mapped.x() - dst.at( i ).x();
    const double dy = mapped.y() - dst.at( i ).y();
    sumSq += dx * dx + dy * dy;
    ++n;
  }
  fit.ready = true;
  fit.rms = ( n > 0 ) ? std::sqrt( sumSq / static_cast<double>( n ) ) : -1.0;
  fit.errorMessage.clear();
  mLastFit = fit;
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

  const long taskId = sicnu::TaskCenter::instance().submitJob(
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
    [task]() { task->cancel(); },
    /*autoLoad=*/false );

  mPendingWarpTaskId = taskId;
  return taskId;
}

bool RsGeoreferencingSession::cancelWarpTask( long taskCenterId )
{
  if ( taskCenterId < 0 || taskCenterId != mPendingWarpTaskId )
    return false;
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

  if ( task )
    task->deleteLater();

  emit warpFinished( id, ok, err, out );
}

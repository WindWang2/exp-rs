#include "rs_warp_task.h"

#include "core/sicnu_logging.h"
#include <QFileInfo>

#include "qgsgeoreftransform.h"

RsWarpTask::RsWarpTask( const QString &in,
                        const QString &out,
                        const QgsGeorefTransform *transform,
                        QgsImageWarper::ResamplingMethod r,
                        const QgsCoordinateReferenceSystem &destCrs,
                        double pixelSize,
                        int backgroundValue )
  : QgsTask( tr( "Warping %1" ).arg( QFileInfo( in ).fileName() ),
             QgsTask::CanCancel )
  , mIn( in )
  , mOut( out )
  , mResamp( r )
  , mDestCrs( destCrs )
  , mPixelSize( pixelSize )
  , mBackground( backgroundValue )
{
  if ( transform )
  {
    // Concrete deep copy (cloneTransform) so the warp runs on a fitted
    // transform with live GDAL/RPC args — no downcast needed (ADR 0057).
    mTransform = transform->cloneTransform();
  }
  connect( &mFb, &QgsFeedback::progressChanged,
           this, [this]( double p ) { setProgress( p ); } );
}

RsWarpTask::~RsWarpTask() = default;

bool RsWarpTask::run()
{
  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QString( "Warp task started: %1" ).arg( mIn ) );
  QgsImageWarper warper( &mFb );
  mResult = warper.warpFile(
    mIn, mOut, mTransform.get(), mResamp,
    /*useZeroAsTrans=*/false,
    /*zeroIsTransparent=*/false,
    mDestCrs,
    QSize(),
    mPixelSize, mPixelSize,
    mBackground );
  const bool ok = mResult.status == QgsImageWarper::WarpStatus::Ok;
  if ( ok )
  {
    SICNU_LOG_SUCCESS( SicnuLogTags::Georeferencing, QString( "Warp task completed: %1 (%2 ms)" )
      .arg( mOut ).arg( mResult.durationMs ) );
  }
  else
  {
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, QString( "Warp task failed: %1" ).arg( mResult.errorMessage ) );
  }
  return ok;
}

void RsWarpTask::cancel()
{
  mFb.cancel();
  QgsTask::cancel();
}

#include "rs_warp_task.h"

#include <QFileInfo>

#include "qgsgeoreftransform.h"

RsWarpTask::RsWarpTask( const QString &in,
                        const QString &out,
                        QgsGeorefTransform *transform,
                        QgsImageWarper::ResamplingMethod r,
                        const QgsCoordinateReferenceSystem &destCrs,
                        double pixelSize )
  : QgsTask( tr( "Warping %1" ).arg( QFileInfo( in ).fileName() ),
             QgsTask::CanCancel )
  , mIn( in )
  , mOut( out )
  , mTransform( transform )
  , mResamp( r )
  , mDestCrs( destCrs )
  , mPixelSize( pixelSize )
{
  connect( &mFb, &QgsFeedback::progressChanged,
           this, [this]( double p ) { setProgress( p ); } );
}

bool RsWarpTask::run()
{
  QgsImageWarper warper( &mFb );
  mResult = warper.warpFile(
    mIn, mOut, mTransform, mResamp,
    /*useZeroAsTrans=*/false,
    /*zeroIsTransparent=*/false,
    mDestCrs,
    QSize(),
    mPixelSize, mPixelSize );
  return mResult.status == QgsImageWarper::WarpStatus::Ok;
}

void RsWarpTask::cancel()
{
  mFb.cancel();
  QgsTask::cancel();
}

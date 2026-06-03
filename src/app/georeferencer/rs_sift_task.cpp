#include "rs_sift_task.h"

#include <QFileInfo>

RsSiftTask::RsSiftTask( QString srcRaster,
                        QString refRaster,
                        QgsCoordinateReferenceSystem refCrs,
                        RsSiftMatcher::Params params )
  : QgsTask( tr( "SIFT matching %1" ).arg( QFileInfo( srcRaster ).fileName() ),
             QgsTask::CanCancel )
  , mSrc( std::move( srcRaster ) )
  , mRef( std::move( refRaster ) )
  , mRefCrs( std::move( refCrs ) )
  , mParams( params )
{
  connect( &mFb, &QgsFeedback::progressChanged,
           this, [this]( double p ) { setProgress( p ); } );
}

bool RsSiftTask::run()
{
  RsSiftMatcher matcher( &mFb );
  mResult = matcher.run( mSrc, mRef, mRefCrs, mParams );
  return mResult.ok();
}

void RsSiftTask::cancel()
{
  mFb.cancel();
  QgsTask::cancel();
}

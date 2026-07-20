// rs_cv_task.cpp — Async cross-validation task implementation
#include "rs_cv_task.h"

RsCvTask::RsCvTask( const cv::Mat &X, const cv::Mat &y,
                    ClassifierFactory factory, int k,
                    const QString &description )
    : QgsTask( description, QgsTask::CanCancel )
    , mX( X.clone() )
    , mY( y.clone() )
    , mFactory( std::move( factory ) )
    , mK( k )
{
}

bool RsCvTask::run()
{
    setProgress( 5 );

    if ( isCanceled() )
        return false;

    // Per-fold feature scaling (default) + cancel checks between folds.
    mResult = RsCrossValidation::kFold(
      mX, mY, mFactory, mK,
      /*scaleFeatures=*/true,
      [this]() { return isCanceled(); } );

    setProgress( 95 );

    if ( isCanceled() || mResult.errorMessage == QStringLiteral( "Cancelled" ) )
        return false;

    setProgress( 100 );

    if ( mResult.ok() )
    {
        if ( mCallback )
            mCallback( mResult );
        return true;
    }

    return false;
}

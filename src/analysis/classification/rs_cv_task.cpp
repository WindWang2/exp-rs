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
    // Run cross-validation with progress reporting
    // RsCrossValidation::kFold doesn't support progress callbacks,
    // so we report progress at key points

    setProgress( 10 );

    if ( isCanceled() )
        return false;

    mResult = RsCrossValidation::kFold( mX, mY, mFactory, mK );

    setProgress( 90 );

    if ( isCanceled() )
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

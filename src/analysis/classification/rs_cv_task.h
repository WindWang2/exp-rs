// rs_cv_task.h — Async cross-validation task for non-blocking UI
#pragma once

#include "rs_cross_validation.h"
#include "rs_classifier_backend.h"

#include <qgstaskmanager.h>

#include <opencv2/core.hpp>

#include <memory>
#include <functional>

/**
 * QgsTask that runs k-fold cross-validation in a background thread.
 * Uses callback pattern instead of signals to avoid MOC issues.
 */
class RsCvTask : public QgsTask
{
public:
    using ClassifierFactory = std::function<std::unique_ptr<RsClassifierBackend>()>;
    using CompletionCallback = std::function<void(const RsCrossValidation::Result &)>;

    RsCvTask( const cv::Mat &X, const cv::Mat &y,
              ClassifierFactory factory, int k = 5,
              const QString &description = QStringLiteral( "Cross Validation" ) );

    void setCompletionCallback( CompletionCallback callback ) { mCallback = std::move( callback ); }

    bool run() override;

    RsCrossValidation::Result result() const { return mResult; }

private:
    cv::Mat mX;
    cv::Mat mY;
    ClassifierFactory mFactory;
    int mK;
    RsCrossValidation::Result mResult;
    CompletionCallback mCallback;
};

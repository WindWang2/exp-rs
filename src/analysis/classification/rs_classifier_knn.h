// rs_classifier_knn.h — k-nearest-neighbour backend (Foundation 5.0, H.1).
//
// Thin wrapper around cv::ml::KNearest through the shared CvBackend
// boilerplate. Brute-force OpenCV KNearest is deterministic for a fixed
// training order; the classification pipeline handles class-id mapping.
#pragma once

#include "rs_classifier_cv_backend.h"

#include <opencv2/ml.hpp>

class QGIS_ANALYSIS_EXPORT RsClassifierKnn : public RsClassifierCvBackend<cv::ml::KNearest>
{
  public:
    explicit RsClassifierKnn( int k = 5 );

    QString name() const override { return QStringLiteral( "kNN" ); }

  private:
    int m_k = 5;
};

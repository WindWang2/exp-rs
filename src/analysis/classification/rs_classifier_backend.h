// rs_classifier_backend.h — Phase 10A Task 10.8: abstract base class for
// pixel-based classifier backends (NormalBayes / SVM / KMeans / future RF /
// Mahalanobis / UNet).
//
// Each backend wraps an OpenCV ML algorithm and exposes a uniform fit/predict
// API so the GUI shell and RsClassificationTask can drive any concrete
// classifier without #ifdefs.
#pragma once

#include "qgis_analysis_export.h"

#include <QString>

#include <opencv2/core.hpp>

class QGIS_ANALYSIS_EXPORT RsClassifierBackend
{
  public:
    virtual ~RsClassifierBackend() = default;

    /// Train on row-major samples.
    /// X: CV_32F, rows = samples, cols = bands.
    /// y: CV_32S, rows = samples, 1 col, class IDs (any positive integer).
    virtual bool fit( const cv::Mat &X, const cv::Mat &y ) = 0;

    /// Predict class IDs for each row of X. Returns CV_32S Nx1.
    virtual cv::Mat predict( const cv::Mat &X ) const = 0;

    /// Human-readable display name (English + parenthesised Chinese OK).
    virtual QString name() const = 0;

    /// Optional persistence (default no-op).
    virtual bool save( const QString & /*path*/ ) const { return false; }
    virtual bool load( const QString & /*path*/ ) { return false; }
};

// rs_classification_split.h — Phase 10A review patch (between 10.8 and 10.9).
//
// Stratified train/test split for the supervised classification pipeline.
// Placed in qgis_analysis (not qgis_app_classify) so unit tests can link
// without pulling in the UI library.
//
// Semantics:
//   * Samples are bucketed by class label.
//   * Each bucket is shuffled with a deterministic seed (std::mt19937, 42).
//   * `ratio` fraction of each bucket goes into the train split; the rest
//     into the test split.
//   * Classes with < 7 samples are placed entirely into the train split
//     (test gets no rows for that class) so tiny classes still influence
//     training instead of being silently dropped.
#pragma once

#include "qgis_analysis_export.h"

#include <opencv2/core.hpp>

struct QGIS_ANALYSIS_EXPORT RsTrainTestSplit
{
    cv::Mat trainX;
    cv::Mat trainY;
    cv::Mat testX;
    cv::Mat testY;
};

class QGIS_ANALYSIS_EXPORT RsClassificationSplit
{
  public:
    /**
     * Stratified split.
     *
     * \param X    CV_32F NxB sample matrix (rows = samples, cols = bands).
     * \param y    CV_32S Nx1 label matrix aligned with X.
     * \param ratio Fraction (0,1) of each class to place in train. 0.7 default.
     * \return Train and test splits. testX/testY are empty (rows=0) if every
     *         class has < 7 samples.
     */
    static RsTrainTestSplit stratifiedSplit( const cv::Mat &X,
                                             const cv::Mat &y,
                                             double ratio = 0.7,
                                             unsigned int seed = 42u );
};

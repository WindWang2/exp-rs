// rs_object_classify.h — K1 supervised object classification on a feature matrix.
//
// Reuses RsClassifierBackend (SVM / NormalBayes / KMeans). No third classifier stack.
// Caller selects classifyLevel when building the matrix; this module only needs X + labels.
#pragma once

#include "qgis_analysis_export.h"

#include <QMap>
#include <QString>
#include <QVector>

#ifdef SICNU_HAS_OPENCV
#include "rs_classifier_backend.h"
#include "rs_feature_scaler.h"
#include <opencv2/core.hpp>
#endif

struct QGIS_ANALYSIS_EXPORT RsObjectClassifyResult
{
  bool ok = false;
  QString errorMessage;
  QMap<quint32, int> segmentClasses;
  QMap<quint32, double> segmentUncertainties;
#ifdef SICNU_HAS_OPENCV
  RsFeatureScaler scaler;
#endif
  int labeledCount = 0;
  int predictedCount = 0;
};

class QGIS_ANALYSIS_EXPORT RsObjectClassify
{
  public:
#ifdef SICNU_HAS_OPENCV
    /// Train on labeled rows of X and predict all rows.
    /// trainingLabels: segmentId → classId (must cover ≥1 samples with matching segmentIds).
    static RsObjectClassifyResult classify(
        const cv::Mat &X,
        const QVector<quint32> &segmentIds,
        const QMap<quint32, int> &trainingLabels,
        RsClassifierBackend &backend,
        bool enableScaling = true,
        RsFeatureScaler::Method scalingMethod = RsFeatureScaler::Method::ZScore );
#endif
};

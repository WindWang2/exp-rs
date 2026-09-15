// rs_classifier_svm.h — Phase 10A Task 10.8.
//
// Wrapper around cv::ml::SVM configured for C_SVC + RBF kernel.
// Defaults: C=10, γ=0.5 (educational defaults; cross-validated grid search
// is left to a v2 task).
//
// Classification & Object Intelligence 11.0: OpenCV exposes no multi-class
// SVM probabilities. This backend can additionally train one-vs-rest (OvR)
// binary SVMs to expose per-class margin decision scores
// (RsClassifierBackend::decisionScores). OvR training is opt-in via the
// constructor flag; the default constructor keeps the historical
// behaviour and cost exactly (DECISIONS D-005).
#pragma once

#include "rs_classifier_cv_backend.h"

#include <QVector>

#include <opencv2/ml.hpp>

class QGIS_ANALYSIS_EXPORT RsClassifierSvm : public RsClassifierCvBackend<cv::ml::SVM>
{
  public:
    RsClassifierSvm();

    /// \param enableOvRDecisionScores when true, fit() additionally trains
    /// one binary SVM per class (one-vs-rest) so decisionScores() is
    /// available. Training cost grows by a factor ≈ classCount.
    explicit RsClassifierSvm( bool enableOvRDecisionScores );

    QString name() const override { return QStringLiteral( "SVM (RBF)" ); }

    bool fit( const cv::Mat &X, const cv::Mat &y ) override;
    QVector<int> classOrder() const override { return mClassLabels; }
    cv::Mat decisionScores( const cv::Mat &X ) const override;
    bool supportsDecisionScores() const override { return mEnableOvR; }
    bool save( const QString &path ) const override;
    bool load( const QString &path ) override;

  private:
    bool mEnableOvR = false;
    /// Ascending training class ids — decisionScores() column order.
    QVector<int> mClassLabels;
    /// One-vs-rest binary models; index i ↦ class mClassLabels[i].
    QVector<cv::Ptr<cv::ml::SVM>> mOvrModels;
    /// Sign normalisation per OvR model: RAW_OUTPUT decision values are
    /// sign-calibrated against training positives at fit time so that
    /// "larger = more likely positive" holds uniformly.
    QVector<int> mOvrSigns;
};

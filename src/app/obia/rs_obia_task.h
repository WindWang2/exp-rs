// rs_obia_task.h — Phase 10B Task 10B.4: OBIA classification pipeline.
//
// QgsTask subclass that runs the full OBIA pipeline on a worker thread:
//   1. Segment image (OTB MeanShift or SimpleSegmenter fallback)
//   2. Extract per-segment features (spectral stats + shape)
//   3. Build training set from user-labeled segments
//   4. Train classifier (RsClassifierBackend)
//   5. Predict all segments
//   6. Write output GeoTIFF with segment-level class IDs
#pragma once

#include "qgstaskmanager.h"

#include "rs_classifier_backend.h"
#include "rs_accuracy_assessment.h"
#include "rs_segment_map.h"
#include "rs_segment_features.h"

#include <QColor>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>

#include <memory>

class RsObiaTask : public QgsTask
{
    Q_OBJECT
  public:
    struct Config
    {
        QString sourceRaster;
        QString outputRaster;
        QVector<int> bandIndices; // 1-based GDAL band numbers

        // Segmentation (used only when existingSegMap is empty)
        bool useOtb = false;      // true = OTB MeanShift, false = SimpleSegmenter
        int spatialRadius = 5;
        int rangeRadius = 15;
        int minRegionSize = 100;
        int maxIteration = 100;
        double threshold = 0.1;

        // SimpleSegmenter fallback params (used only when existingSegMap is empty)
        int smoothKernel = 5;
        int quantizeBins = 32;

        // Pre-computed segment map (skip segmentation if non-empty)
        RsSegmentMap existingSegMap;

        // Pre-computed segment features (skip extraction if non-empty)
        QMap<quint32, RsSegmentFeatures::SegmentStat> existingStats;

        // Classifier
        std::unique_ptr<RsClassifierBackend> backend;

        // Training: segmentId → classId (user-labeled segments)
        QMap<quint32, int> segmentLabels;

        // Feature selection mask
        RsFeatureSelection featureSelection;

        QHash<int, QColor> classColors;
        QString algoName;
    };

    struct Result
    {
        bool ok = false;
        QString errorMessage;
        int totalSegments = 0;
        int labeledSegments = 0;
        int totalPixels = 0;
        int durationMs = 0;
        RsAccuracyAssessment::Result accuracy;
        QMap<quint32, double> segmentUncertainties;
        QMap<quint32, int> segmentClasses;
    };

    explicit RsObiaTask( Config cfg );

    bool run() override;
    void cancel() override;

    const Result &result() const { return mResult; }

    /// Get the segment map (available after run() completes successfully).
    const RsSegmentMap &segmentMap() const { return mSegMap; }

  private:
    Config mCfg;
    Result mResult;
    RsSegmentMap mSegMap;

    bool writeOutput( const QMap<quint32, int> &segmentClasses, QString &errorMsg );
};

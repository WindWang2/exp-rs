// rs_classification_task.h — Phase 10A Task 10.8.
//
// QgsTask subclass that runs the full pixel-based classification pipeline on
// a worker thread:
//
//   1. backend->fit(trainX, trainY) when not already fitted
//   2. optional accuracy assessment on held-out testX/testY (Task 10.9)
//   3. open source raster (GDAL)
//   4. create destination GTiff with same georeferencing + ColorTable (Byte only)
//   5. tile-stream predict (256x256) writing class IDs
//      - GDT_Byte when max class id ≤ 255
//      - GDT_UInt16 / GDT_Int16 when class ids exceed 255 (no silent clamp)
//   6. source-band NoData → output class 0 (unclassified)
//
// Cancellation: a QgsFeedback is connected to setProgress + cancel(). If
// cancelled mid-tile the partially-written output file is removed.
#pragma once

#include "qgstaskmanager.h"
#include "qgsfeedback.h"

#include "rs_classifier_backend.h"
#include "rs_accuracy_assessment.h"
#include "rs_feature_scaler.h"
#include "rs_pixel_window.h"

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

#include <opencv2/core.hpp>

class RsClassificationTask : public QgsTask
{
    Q_OBJECT
  public:
    struct Config
    {
      QString sourceRaster;
      QString outputRaster;
      QVector<int> bandIndices;                       // 1-based GDAL bands
      std::unique_ptr<RsClassifierBackend> backend;   // owned
      cv::Mat trainX;                                 // CV_32F NxB (scaled if scaler fitted)
      cv::Mat trainY;                                 // CV_32S Nx1
      // Held-out split for accuracy assessment (Task 10.9). When non-empty,
      // run() computes confusionMatrix / overallAccuracy / kappa AFTER fit()
      // and BEFORE the tile-streamed predict. KMeans cluster IDs are remapped
      // via Hungarian assignment first.
      cv::Mat testX;                                  // CV_32F MxB (may be empty; scaled)
      cv::Mat testY;                                  // CV_32S Mx1 (may be empty)
      QHash<int, QColor> classColors;                 // classId -> RGB
      QString algoName;                               // for structured log
      // If fitted, Task transforms tile X before predict. Caller scales train/test.
      RsFeatureScaler scaler;
      // Optional: after successful fit, persist model YAML + .scale.json sidecar.
      // Empty = do not save. Non-empty: hard-fails the task if model or
      // sidecar write fails (orphan model file is removed on sidecar failure).
      QString modelSavePath;
      // GDAL GTiff creation options. Defaults favour tiled DEFLATE suitable
      // for large lab scenes. On Create failure with non-empty options the
      // task retries once with no options.
      QStringList creationOptions{
        QStringLiteral( "TILED=YES" ),
        QStringLiteral( "COMPRESS=DEFLATE" ),
        QStringLiteral( "PREDICTOR=2" )
      };
      // When true and window.valid, output size/GT match the pixel window
      // and only that sub-rectangle is classified (preview path). Apply
      // leaves this false for full-raster output.
      bool cropToWindow = false;
      RsPixelWindow window;
    };

    struct Result
    {
      bool ok = false;
      QString errorMessage;
      int totalPixels = 0;
      int durationMs = 0;
      // Phase 10A Task 10.9 — confusion matrix + Kappa + per-class P/R/F1
      // populated when Config.testX / testY are non-empty. KMeans uses
      // Hungarian-remapped cluster IDs so labels align with ROI class IDs.
      RsAccuracyAssessment::Result accuracy;
    };

    explicit RsClassificationTask( Config cfg );

    bool run() override;
    void cancel() override;

    const Result &result() const { return mResult; }

  private:
    Config mCfg;
    QgsFeedback mFb;
    Result mResult;
};

// rs_classification_pipeline.h — ADR 0019 slice S2: deep, GUI-free
// classification pipeline core ("train → persist → tiled predict → class map")
// at the single training/predict seam. The QgsTask adapter that originally
// consumed this seam was deleted in ADR 0053 — GUI code now builds
// RsClassificationPipeline::Config directly.
//
// Synchronous, no QgsTask / QWidget types. Owns the full classify flow:
//   1. backend->fit(trainX, trainY) when not already fitted
//   2. optional accuracy assessment on held-out testX/testY AFTER fit and
//      BEFORE predict (KMeans cluster IDs remapped via Hungarian assignment)
//   3. open source raster (GDAL), create destination GTiff with the same
//      georeferencing + ColorTable (Byte only), retrying once without
//      creation options when Create fails
//   4. tile-streamed predict (256x256) with dtype escalation (Byte →
//      UInt16/Int32 when class ids exceed 255, never a silent clamp)
//   5. NoData / user ignore values → unclassified (RsPixelIgnoreOptions)
//   6. fitted RsFeatureScaler transform of tile X before predict
//   7. optional crop-to-window (RsPixelWindow) preview
//   8. cancel via the Progress sink → partially-written output removed
//   9. optional model persistence: model YAML + one superset JSON sidecar
//      (method + fitted scaler + class metadata + format version) per
//      ADR 0019 decision 3. Replaces the legacy .scale.json sidecar; there
//      is no legacy read path.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_classifier_backend.h"
#include "rs_accuracy_assessment.h"
#include "rs_feature_scaler.h"
#include "rs_pixel_ignore_options.h"
#include "rs_pixel_window.h"

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

#include <opencv2/core.hpp>

struct QGIS_ANALYSIS_EXPORT RsClassificationPipelineResult
{
    /// Machine-readable failure reason, so adapters can keep their own
    /// externally-visible error codes/messages.
    enum class Error
    {
      None = 0,
      NoBackend,
      NotFittedNoTrainingData,
      TrainingFailed,
      ModelSaveFailed,
      SidecarSaveFailed,
      Cancelled,
      RasterOpenFailed,
      EmptyCropWindow,
      InvalidBand,
      OutputDriverUnavailable,
      OutputCreateFailed,
      GdalWriteFailed,
      RasterReadFailed,
      ScalingFailed,
      PredictionFailed,
      PredictionSizeMismatch,
      VectorOpenFailed,
      VectorNoLayers,
      ClassFieldNotFound,
      NoValidPixels,
      InsufficientSamples,
      ModelOpenFailed,
      ModelSidecarMissing,
    };

    bool ok = false;
    Error error = Error::None;
    QString errorMessage;      ///< Human-readable detail, set when !ok.
    int totalPixels = 0;
    int durationMs = 0;
    int trainSamples = 0;
    int classCount = 0;
    int featuresExtracted = 0;
    /// Per-class training sample counts (classId -> samples), from the raw
    /// extraction (before any holdout split). Empty in predict-only mode.
    QHash<int, int> trainSamplesByClass;
    /// Mean best-class probability over valid pixels when a probability
    /// output was requested (0.0 when none written).
    double meanConfidence = 0.0;
    /// Confusion matrix + Kappa + per-class P/R/F1, populated when
    /// Config.testX / testY are non-empty. KMeans uses Hungarian-remapped
    /// cluster IDs so labels align with ROI class IDs.
    RsAccuracyAssessment::Result accuracy;
};

class QGIS_ANALYSIS_EXPORT RsClassificationPipeline
{
  public:
    /**
     * Progress/cancel sink. Called with a fraction in [0,1] and a message;
     * return false to cancel (the run then fails with Error::Cancelled and
     * any partially-written output raster is removed).
     */
    using Progress = std::function<bool( double fraction, const QString &message )>;

    struct Config
    {
        QString sourceRaster;
        QString outputRaster;
        QVector<int> bandIndices;                       // 1-based GDAL bands
        std::unique_ptr<RsClassifierBackend> backend;   // owned

        // Sample extraction & pre-processing parameters
        QString trainingVector;                         // OGR vector shapefile/layer path
        QString classField = QStringLiteral( "class_id" );
        int maxSamplesPerClass = 5000;
        bool fitScaler = false;
        double testSplit = 0.0;                         // 0-0.9 holdout fraction for accuracy
        std::vector<int> groupIds;                      // optional per-sample group IDs for holdout split

        // Optional per-pixel best-class probability raster (Float32, NoData =
        // -1 on ignored pixels). Requires a backend with supportsProbabilities()
        // (NormalBayes / MLP); the mean confidence is reported in the result.
        QString probabilityOutput;

        // Predict-only mode model loading path (model YAML + .meta.json sidecar)
        QString modelLoadPath;

        cv::Mat trainX;                                 // CV_32F NxB (scaled if scaler fitted)
        cv::Mat trainY;                                 // CV_32S Nx1
        // Held-out split for accuracy assessment. When non-empty, run()
        // computes the confusion metrics AFTER fit() and BEFORE the
        // tile-streamed predict.
        cv::Mat testX;                                  // CV_32F MxB (may be empty; scaled)
        cv::Mat testY;                                  // CV_32S Mx1 (may be empty)
        QHash<int, QColor> classColors;                 // classId -> RGB
        // for log + sidecar only — the cluster→class remap is backend-driven
        // (RsClassifierBackend::needsLabelRemap(), ADR 0061).
        QString methodName;
        // If fitted, run() transforms tile X before predict. Caller scales train/test.
        RsFeatureScaler scaler;
        // Optional: after successful fit, persist model YAML + .meta.json
        // superset sidecar. Empty = do not save. Non-empty: hard-fails the
        // run if model or sidecar write fails (orphan model file is removed
        // on sidecar failure).
        QString modelSavePath;
        // GDAL GTiff creation options. On Create failure with non-empty
        // options the run retries once with no options.
        QStringList creationOptions{
          QStringLiteral( "TILED=YES" ),
          QStringLiteral( "COMPRESS=DEFLATE" ),
          QStringLiteral( "PREDICTOR=2" )
        };
        // When true and window.valid, output size/GT match the pixel window
        // and only that sub-rectangle is classified (preview path).
        bool cropToWindow = false;
        RsPixelWindow window;

        /// Edge / background handling: source NoData + optional ignore values.
        RsPixelIgnoreOptions ignoreOptions;

        /// Deterministic pseudo-random seed for data splitting and subsampling.
        unsigned int seed = 42u;
    };

    /// Run the full pipeline synchronously on the caller's thread.
    static RsClassificationPipelineResult run( Config config,
                                               const Progress &progress = Progress() );

    // -- ADR 0019 decision 3: single superset model sidecar -----------------

    /// Sidecar path for a model file: "<dir>/<completeBaseName>.meta.json".
    static QString sidecarPathForModel( const QString &modelPath );

    /// Write the superset sidecar (method + fitted scaler + class metadata +
    /// band feature schema + holdout validation metrics + format version) next
    /// to \a modelPath. The scaler / classes / features / validation sections
    /// are emitted only when fitted / non-empty.
    static bool saveModelSidecar( const QString &modelPath,
                                  const QString &methodName,
                                  const RsFeatureScaler &scaler,
                                  const QHash<int, QColor> &classColors,
                                  const QVector<int> &bandIndices = {},
                                  const RsAccuracyAssessment::Result &accuracy = {},
                                  const QHash<int, int> &kmeansRemap = {} );

    /**
     * Read the superset sidecar for \a modelPath. Returns false when the
     * file is missing, malformed, or an unsupported version. On success
     * \a methodName and \a classColors reflect the stored metadata and
     * \a scaler is fitted only when the sidecar carries a scaler section.
     * \a bandIndices and \a accuracy are populated when the sidecar carries
     * a feature schema / validation section (older sidecars leave them empty).
     */
    static bool loadModelSidecar( const QString &modelPath,
                                  QString &methodName,
                                  RsFeatureScaler &scaler,
                                  QHash<int, QColor> &classColors,
                                  QVector<int> &bandIndices,
                                  RsAccuracyAssessment::Result &accuracy,
                                  QHash<int, int> &kmeansRemap );

    static bool loadModelSidecar( const QString &modelPath,
                                  QString &methodName,
                                  RsFeatureScaler &scaler,
                                  QHash<int, QColor> &classColors,
                                  QVector<int> &bandIndices,
                                  RsAccuracyAssessment::Result &accuracy )
    {
      QHash<int, int> dummy;
      return loadModelSidecar( modelPath, methodName, scaler, classColors, bandIndices, accuracy, dummy );
    }
};

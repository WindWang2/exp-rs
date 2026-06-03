// rs_classification_task.h — Phase 10A Task 10.8.
//
// QgsTask subclass that runs the full pixel-based classification pipeline on
// a worker thread:
//
//   1. backend->fit(trainX, trainY)
//   2. open source raster (GDAL)
//   3. create destination GTiff with same georeferencing + ColorTable
//   4. tile-stream predict (256x256) writing class IDs as uint8 band 1
//
// Cancellation: a QgsFeedback is connected to setProgress + cancel(). If
// cancelled mid-tile the partially-written output file is removed.
#pragma once

#include "qgstaskmanager.h"
#include "qgsfeedback.h"

#include "rs_classifier_backend.h"

#include <QColor>
#include <QHash>
#include <QString>
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
      cv::Mat trainX;                                 // CV_32F NxB
      cv::Mat trainY;                                 // CV_32S Nx1
      QHash<int, QColor> classColors;                 // classId -> RGB
      QString algoName;                               // for structured log
    };

    struct Result
    {
      bool ok = false;
      QString errorMessage;
      int totalPixels = 0;
      int durationMs = 0;
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

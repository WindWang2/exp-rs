// rs_post_process_task.h — QgsTask wrapper for RsPostProcess operator chain.
//
// Pipeline (enabled stages only):
//   load → sieve? → majority? → clump? → recode? → save raster → polygonize?
//
// Cancellation: QgsFeedback between stages; partial output is not guaranteed
// to be cleaned up if save already succeeded.
#pragma once

#include "qgsfeedback.h"
#include "qgstaskmanager.h"

#include <QMap>
#include <QString>
#include <QStringList>

struct RsPostProcessConfig
{
  QString inputPath;
  QString outputRasterPath;
  QString outputVectorPath;
  bool runSieve = true;
  int sieveThreshold = 10;
  int connectedness = 8;
  bool runMajority = true;
  int majorityKernel = 3;
  bool runClump = false;
  bool runRecode = false;
  QMap<int, int> recodeMap;
  bool runPolygonize = false;
  QStringList creationOptions{
    QStringLiteral( "TILED=YES" ),
    QStringLiteral( "COMPRESS=DEFLATE" ),
    QStringLiteral( "PREDICTOR=2" )
  };
};

class RsPostProcessTask : public QgsTask
{
    Q_OBJECT
  public:
    struct Result
    {
      bool ok = false;
      QString errorMessage;
      int durationMs = 0;
    };

    explicit RsPostProcessTask( RsPostProcessConfig cfg );

    bool run() override;
    void cancel() override;

    const Result &result() const { return mResult; }
    const RsPostProcessConfig &config() const { return mCfg; }

  private:
    RsPostProcessConfig mCfg;
    QgsFeedback mFb;
    Result mResult;
};

// rs_post_process_task.h — QgsTask wrapper for RsPostProcess operator chain.
//
// Pipeline (enabled stages only):
//   load → sieve? → majority? → clump? → recode? → save raster → polygonize?
//
// Cancellation: QgsFeedback is checked between stages AND polled INSIDE the
// cancellable backends (sieve/majority/clump). A cancel detected before the
// output write publishes nothing; partial output is not guaranteed to be
// cleaned up if save already succeeded (post-save contract, see HANDOFF H3).
#pragma once

#include "qgsfeedback.h"
#include "qgstaskmanager.h"

#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>

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
  /// R31-T03 WP16 deterministic barrier. Empty in production; a test may
  /// install a hook that requests cancel() exactly while the clump stage is
  /// running, without sleeps or "make the raster big enough" races. It never
  /// changes inputs, outputs, permissions or published results.
  std::function<void()> clumpStageEnteredForTest;
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

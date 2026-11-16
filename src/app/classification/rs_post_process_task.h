// rs_post_process_task.h — QgsTask wrapper for RsPostProcess operator chain.
//
// Pipeline (enabled stages only):
//   load → sieve? → majority? → clump? → recode? → save raster → polygonize?
//
// Cancellation: QgsFeedback is checked between stages AND polled INSIDE the
// cancellable backends (sieve/majority/clump). A cancel detected before the
// output write publishes nothing; partial output is not guaranteed to be
// cleaned up if save already succeeded (post-save contract, see HANDOFF H3).
//
// R32-T06: `QgsFeedback` is only cancelled by cancel()/cancel(), and the job
// bridge runs this task on the stack (it never reaches QgsTaskManager), so
// nothing in the desktop path ever called it — an in-stage poll therefore
// answered "not cancelled" for a whole clump scan. Config::isCanceled lets the
// caller inject the authoritative job predicate; cancellationRequested() ORs it
// with the task-local feedback, so both the bridged and the task-managed cases
// work.
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
  /// R32-T06: authoritative cancellation source for the job bridge. The job
  /// owns `RSOperatorContext` and outlives run(), so the caller forwards its
  /// cancel predicate here; empty means "task-local feedback only", which is
  /// the QgsTaskManager case. Never mutates inputs or outputs.
  std::function<bool()> isCanceled;
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
    /// R32-T06: task-local feedback OR the injected job predicate. Both are
    /// polled by the in-stage callbacks and by the between-stage checks.
    bool cancellationRequested() const;

    RsPostProcessConfig mCfg;
    QgsFeedback mFb;
    Result mResult;
};

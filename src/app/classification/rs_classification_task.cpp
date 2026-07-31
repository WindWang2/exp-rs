// rs_classification_task.cpp — Phase 10A Task 10.8 / ADR 0019 slice S2.
//
// Thin QgsTask adapter: the full classify flow lives in
// RsClassificationPipeline (analysis layer); this file only maps the config,
// bridges QgsFeedback to the pipeline's progress/cancel sink, and maps the
// typed pipeline result back.

#include "rs_classification_task.h"

#include "rs_classification_pipeline.h"

#include <QFileInfo>

RsClassificationTask::RsClassificationTask( Config cfg )
  : QgsTask( tr( "Classifying %1" ).arg( QFileInfo( cfg.sourceRaster ).fileName() ),
             QgsTask::CanCancel )
  , mCfg( std::move( cfg ) )
{
  connect( &mFb, &QgsFeedback::progressChanged,
           this, [this]( double p ) { setProgress( p ); } );
}

void RsClassificationTask::cancel()
{
  mFb.cancel();
  QgsTask::cancel();
}

bool RsClassificationTask::run()
{
  RsClassificationPipeline::Config pcfg;
  pcfg.sourceRaster = std::move( mCfg.sourceRaster );
  pcfg.outputRaster = std::move( mCfg.outputRaster );
  pcfg.bandIndices = std::move( mCfg.bandIndices );
  pcfg.backend = std::move( mCfg.backend );
  pcfg.trainX = std::move( mCfg.trainX );
  pcfg.trainY = std::move( mCfg.trainY );
  pcfg.testX = std::move( mCfg.testX );
  pcfg.testY = std::move( mCfg.testY );
  pcfg.classColors = std::move( mCfg.classColors );
  pcfg.methodName = std::move( mCfg.algoName );
  pcfg.scaler = std::move( mCfg.scaler );
  pcfg.modelSavePath = std::move( mCfg.modelSavePath );
  pcfg.creationOptions = std::move( mCfg.creationOptions );
  pcfg.cropToWindow = mCfg.cropToWindow;
  pcfg.window = mCfg.window;
  pcfg.ignoreOptions = std::move( mCfg.ignoreOptions );

  // Bridge: pipeline fraction [0,1] → QgsFeedback percent; cancel via the
  // sink's return value (pipeline then fails with Error::Cancelled and
  // removes the partially-written output).
  const RsClassificationPipelineResult res = RsClassificationPipeline::run(
    std::move( pcfg ),
    [this]( double fraction, const QString & )
    {
      mFb.setProgress( fraction * 100.0 );
      return !mFb.isCanceled();
    } );

  mResult.ok = res.ok;
  mResult.errorMessage = res.errorMessage;
  mResult.totalPixels = res.totalPixels;
  mResult.durationMs = res.durationMs;
  mResult.accuracy = res.accuracy;
  return res.ok;
}

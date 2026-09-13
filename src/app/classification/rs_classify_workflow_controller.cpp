#include "rs_classify_workflow_controller.h"

RsClassifyWorkflowController::RsClassifyWorkflowController( QObject *parent )
  : QObject( parent )
{
}

RsClassifyStep RsClassifyWorkflowController::currentStep() const
{
  return mStep;
}

void RsClassifyWorkflowController::setCurrentStep( RsClassifyStep s )
{
  if ( s < RsClassifyStep::ClassSystem || s >= RsClassifyStep::Count )
    return;
  if ( mStep == s )
    return;
  mStep = s;
  emit currentStepChanged( mStep );
}

RsClassifyUiMode RsClassifyWorkflowController::mode() const
{
  return mMode;
}

void RsClassifyWorkflowController::setMode( RsClassifyUiMode m )
{
  if ( mMode == m )
    return;
  mMode = m;
  emit modeChanged( mMode );
}

void RsClassifyWorkflowController::setHasSourceRaster( bool v )
{
  if ( mHasSource == v )
    return;
  mHasSource = v;
  emit completionChanged();
}

void RsClassifyWorkflowController::setClassCount( int n )
{
  if ( mClassCount == n )
    return;
  mClassCount = n;
  emit completionChanged();
}

void RsClassifyWorkflowController::setTrainingClassCountWithPixels( int n )
{
  if ( mTrainClasses == n )
    return;
  mTrainClasses = n;
  emit completionChanged();
}

void RsClassifyWorkflowController::setTrainingPixelCount( int n )
{
  if ( mTrainPixels == n )
    return;
  mTrainPixels = n;
  emit completionChanged();
}

void RsClassifyWorkflowController::setEvaluateReviewed( bool v )
{
  if ( mEvalReviewed == v )
    return;
  mEvalReviewed = v;
  emit completionChanged();
}

void RsClassifyWorkflowController::setHasFullClassifyResult( bool v )
{
  if ( mHasFullResult == v )
    return;
  mHasFullResult = v;
  emit completionChanged();
}

void RsClassifyWorkflowController::setHasAccuracyMetrics( bool v )
{
  if ( mHasAccuracy == v )
    return;
  mHasAccuracy = v;
  emit completionChanged();
}

void RsClassifyWorkflowController::setPostProcessSkipped( bool v )
{
  if ( mPostSkipped == v )
    return;
  mPostSkipped = v;
  emit completionChanged();
}

void RsClassifyWorkflowController::setHasPostProcessResult( bool v )
{
  if ( mHasPost == v )
    return;
  mHasPost = v;
  emit completionChanged();
}

void RsClassifyWorkflowController::setHasExportedOrLoadedToMain( bool v )
{
  if ( mExported == v )
    return;
  mExported = v;
  emit completionChanged();
}

bool RsClassifyWorkflowController::isStepComplete( RsClassifyStep s ) const
{
  switch ( s )
  {
    case RsClassifyStep::ClassSystem:
      return mClassCount >= 2;
    case RsClassifyStep::Samples:
      return mTrainClasses >= 2;
    case RsClassifyStep::Evaluate:
      return mEvalReviewed;
    case RsClassifyStep::TrainClassify:
      return mHasFullResult;
    case RsClassifyStep::Accuracy:
      return mHasAccuracy;
    case RsClassifyStep::PostProcess:
      return mPostSkipped || mHasPost;
    case RsClassifyStep::Export:
      return mExported;
    case RsClassifyStep::Count:
      break;
  }
  return false;
}

bool RsClassifyWorkflowController::canTrainOrClassify() const
{
  return mHasSource && mTrainPixels >= 10;
}

bool RsClassifyWorkflowController::canRunPostProcess() const
{
  return mHasFullResult;
}

bool RsClassifyWorkflowController::canExport() const
{
  return mHasFullResult || mHasPost;
}

bool RsClassifyWorkflowController::canRunPrimaryAction( RsClassifyStep s ) const
{
  switch ( s )
  {
    case RsClassifyStep::ClassSystem:
      // Primary: define classes — always available
      return true;
    case RsClassifyStep::Samples:
      // Digitizing needs source + at least one class
      return mHasSource && mClassCount >= 1;
    case RsClassifyStep::Evaluate:
      // JM / spectral needs training pixels present
      return mTrainPixels > 0;
    case RsClassifyStep::TrainClassify:
      return canTrainOrClassify();
    case RsClassifyStep::Accuracy:
      // Metrics from full Apply or recompute sources
      return mHasFullResult || mHasAccuracy;
    case RsClassifyStep::PostProcess:
      return canRunPostProcess();
    case RsClassifyStep::Export:
      return canExport();
    case RsClassifyStep::Count:
      break;
  }
  return false;
}

QStringList RsClassifyWorkflowController::missingRequirements( RsClassifyStep s ) const
{
  QStringList miss;
  switch ( s )
  {
    case RsClassifyStep::ClassSystem:
      if ( mClassCount < 2 )
        miss << tr( "at least 2 classes" );
      break;
    case RsClassifyStep::Samples:
      if ( !mHasSource )
        miss << tr( "Open Source Image" );
      if ( mClassCount < 1 )
        miss << tr( "at least 1 class" );
      break;
    case RsClassifyStep::Evaluate:
      if ( mTrainPixels <= 0 )
        miss << tr( "training pixels ≥ 1" );
      break;
    case RsClassifyStep::TrainClassify:
      if ( !mHasSource )
        miss << tr( "Open Source Image" );
      if ( mTrainPixels < 10 )
        miss << tr( "training pixels ≥ 10" );
      break;
    case RsClassifyStep::Accuracy:
      if ( !mHasFullResult && !mHasAccuracy )
        miss << tr( "Finish Full-Image Classification" );
      break;
    case RsClassifyStep::PostProcess:
      if ( !mHasFullResult )
        miss << tr( "Finish Full-Image Classification" );
      break;
    case RsClassifyStep::Export:
      if ( !mHasFullResult && !mHasPost )
        miss << tr( "Finish Full-Image Classification or Post-Processing" );
      break;
    case RsClassifyStep::Count:
      break;
  }
  return miss;
}

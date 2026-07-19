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
        miss << QStringLiteral( "至少 2 个类别" );
      break;
    case RsClassifyStep::Samples:
      if ( !mHasSource )
        miss << QStringLiteral( "打开源影像" );
      if ( mClassCount < 1 )
        miss << QStringLiteral( "至少 1 个类别" );
      break;
    case RsClassifyStep::Evaluate:
      if ( mTrainPixels <= 0 )
        miss << QStringLiteral( "训练像元 ≥ 1" );
      break;
    case RsClassifyStep::TrainClassify:
      if ( !mHasSource )
        miss << QStringLiteral( "打开源影像" );
      if ( mTrainPixels < 10 )
        miss << QStringLiteral( "训练像元 ≥ 10" );
      break;
    case RsClassifyStep::Accuracy:
      if ( !mHasFullResult && !mHasAccuracy )
        miss << QStringLiteral( "完成全图分类" );
      break;
    case RsClassifyStep::PostProcess:
      if ( !mHasFullResult )
        miss << QStringLiteral( "完成全图分类" );
      break;
    case RsClassifyStep::Export:
      if ( !mHasFullResult && !mHasPost )
        miss << QStringLiteral( "完成全图分类或后处理" );
      break;
    case RsClassifyStep::Count:
      break;
  }
  return miss;
}

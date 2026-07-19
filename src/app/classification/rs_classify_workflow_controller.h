#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

enum class RsClassifyStep {
  ClassSystem = 0,
  Samples,
  Evaluate,
  TrainClassify,
  Accuracy,
  PostProcess,
  Export,
  Count
};

enum class RsClassifyUiMode { Wizard, Expert };

class RsClassifyWorkflowController : public QObject
{
  Q_OBJECT
public:
  explicit RsClassifyWorkflowController( QObject *parent = nullptr );

  RsClassifyStep currentStep() const;
  void setCurrentStep( RsClassifyStep s );

  RsClassifyUiMode mode() const;
  void setMode( RsClassifyUiMode m );

  // Inputs updated by main window
  void setHasSourceRaster( bool v );
  void setClassCount( int n );
  void setTrainingClassCountWithPixels( int n );
  void setTrainingPixelCount( int n );
  void setEvaluateReviewed( bool v );
  void setHasFullClassifyResult( bool v ); // Apply path only, not preview
  void setHasAccuracyMetrics( bool v );
  void setPostProcessSkipped( bool v );
  void setHasPostProcessResult( bool v );
  void setHasExportedOrLoadedToMain( bool v );

  bool isStepComplete( RsClassifyStep s ) const;
  bool canRunPrimaryAction( RsClassifyStep s ) const;
  QStringList missingRequirements( RsClassifyStep s ) const;

  // Convenience for step 4 actions
  bool canTrainOrClassify() const; // source + train pixels >= 10
  bool canRunPostProcess() const;  // has classify result path (tracked via hasFullClassifyResult)
  bool canExport() const;

signals:
  void currentStepChanged( RsClassifyStep s );
  void completionChanged();
  void modeChanged( RsClassifyUiMode m );

private:
  RsClassifyStep mStep = RsClassifyStep::ClassSystem;
  RsClassifyUiMode mMode = RsClassifyUiMode::Wizard;
  bool mHasSource = false;
  int mClassCount = 0;
  int mTrainClasses = 0;
  int mTrainPixels = 0;
  bool mEvalReviewed = false;
  bool mHasFullResult = false;
  bool mHasAccuracy = false;
  bool mPostSkipped = false;
  bool mHasPost = false;
  bool mExported = false;
};

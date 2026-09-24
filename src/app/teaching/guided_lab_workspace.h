#pragma once

#include "teaching/autonomy_effective_display.h"
#include "teaching/lab_feedback_projection.h"
#include "teaching/lab_readiness.h"
#include "teaching/lab_step_timeline.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextEdit;
class QPlainTextEdit;

namespace sicnu::app::teaching {

class GuidedLabWorkspace : public QWidget
{
  Q_OBJECT
public:
  explicit GuidedLabWorkspace( QWidget *parent = nullptr );

  void setTimeline( const sicnu::teaching::LabStepTimeline &tl );
  void setReadiness( const sicnu::teaching::LabReadiness &r );
  void setAutonomy( const sicnu::teaching::AutonomyEffectiveDisplay &a );
  void setFeedback( const sicnu::teaching::LabFeedbackProjection &f );
  void clearFeedback();
  void appendFeedbackNote( const QString &noteZh );
  void setWhyMarkdown( const QString &md );

  /// Student-produced artifact to validate / grade (empty = nothing yet).
  QString artifactPath() const;
  void setArtifactPath( const QString &path );

  sicnu::teaching::LabStepTimeline timeline() const { return m_tl; }

signals:
  void runOperatorRequested( const QString &operatorId, const QString &paramsJson );
  void jumpWorkbenchRequested( const QString &operatorId );
  void humanEvidenceSubmitted( const QString &stepId, const QString &text );
  void validateRequested();
  void exportCapsuleRequested();
  void stepIndexChanged( int index );

private:
  void rebuildSteps();
  void showCurrent();

  sicnu::teaching::LabStepTimeline m_tl;
  sicnu::teaching::LabReadiness m_readiness;
  sicnu::teaching::AutonomyEffectiveDisplay m_autonomy;
  sicnu::teaching::LabFeedbackProjection m_feedback;

  QLabel *m_labTitle = nullptr;
  QLabel *m_readinessLabel = nullptr;
  QLabel *m_autonomyLabel = nullptr;
  QListWidget *m_stepList = nullptr;
  QLabel *m_stepTitle = nullptr;
  QLabel *m_kindLabel = nullptr;
  QTextEdit *m_whyView = nullptr;
  QTextEdit *m_paramsView = nullptr;
  QPlainTextEdit *m_humanInput = nullptr;
  QLineEdit *m_artifactEdit = nullptr;
  QTextEdit *m_feedbackView = nullptr;
  QPushButton *m_prevBtn = nullptr;
  QPushButton *m_nextBtn = nullptr;
  QPushButton *m_runBtn = nullptr;
  QPushButton *m_submitHumanBtn = nullptr;
  QPushButton *m_validateBtn = nullptr;
  QPushButton *m_exportBtn = nullptr;
};

} // namespace sicnu::app::teaching

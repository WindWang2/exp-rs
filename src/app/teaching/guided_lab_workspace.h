#pragma once

#include "teaching/autonomy_effective_display.h"
#include "teaching/lab_feedback_projection.h"
#include "teaching/lab_readiness.h"
#include "teaching/lab_step_timeline.h"

#include <QHash>
#include <QWidget>

class QHideEvent;
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
  ~GuidedLabWorkspace() override;

protected:
  /// Closing/hiding the workspace is a durability boundary: a student may
  /// type and quit without ever changing step or pressing Submit.
  void hideEvent( QHideEvent *event ) override;

public:
  void setTimeline( const sicnu::teaching::LabStepTimeline &tl );
  void setReadiness( const sicnu::teaching::LabReadiness &r );
  void setAutonomy( const sicnu::teaching::AutonomyEffectiveDisplay &a );
  void setFeedback( const sicnu::teaching::LabFeedbackProjection &f );
  void clearFeedback();
  void appendFeedbackNote( const QString &noteZh );

  /// Student-produced artifact to validate / grade (empty = nothing yet).
  QString artifactPath() const;
  void setArtifactPath( const QString &path );

  /// R30-T07 WP10: durable local drafts keyed by course/lab/stable step id.
  /// @p courseId scopes the file; a lab switch persists the outgoing drafts
  /// instead of clearing them. An empty path keeps the old in-memory-only
  /// behavior (used by headless tests).
  void setDraftStore( const QString &path, const QString &courseId );
  /// Re-check restored drafts against the CURRENT rules/artifact context.
  /// Returns true when at least one restored draft was marked stale.
  bool refreshDraftValidity( const QString &rulesVersion );
  QString draftStatusText() const { return m_draftStatus; }
  /// Persist the visible draft immediately (also called on every step/lab
  /// switch). A draft is NEVER a completion or a pass.
  void flushDrafts();

  sicnu::teaching::LabStepTimeline timeline() const { return m_tl; }
  sicnu::teaching::AutonomyEffectiveDisplay autonomy() const { return m_autonomy; }
  sicnu::teaching::LabReadiness readiness() const { return m_readiness; }

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
  void restoreDraftsForCurrentLab( bool applyToEditor = true );
  /// Stable draft key for one step: lab + step id, so identical step ids in
  /// different labs never share a manual draft.
  QString stepDraftKey( const std::string &stepId ) const;

  sicnu::teaching::LabStepTimeline m_tl;
  sicnu::teaching::LabReadiness m_readiness;
  sicnu::teaching::AutonomyEffectiveDisplay m_autonomy;
  sicnu::teaching::LabFeedbackProjection m_feedback;
  /// In-memory manual drafts keyed by lab+step id. The durable contract
  /// stays evidence refs (session evidenceRefs); this only keeps the
  /// student's typing from leaking into (or vanishing on) step switches.
  QHash<QString, QString> m_stepDrafts;
  /// R30-T07 WP10: durable draft context.
  QString m_draftStorePath;
  QString m_draftCourseId;
  QString m_rulesVersion;
  QString m_draftStatus;
  /// True while setTimeline rebuilds the step list: the synthetic
  /// currentRowChanged emissions during a rebuild belong to the NEW
  /// timeline, so the switch handler must not park the visible text under
  /// the new lab's step ids (setTimeline parks under the OLD ids instead).
  bool m_timelineRebuilding = false;

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

#include "guided_lab_workspace.h"
#include "teaching/lab_draft_store.h"
#include <algorithm>
#include <cstdlib>
#include "status_badge.h"
#include <QDateTime>
#include <QHideEvent>

#include <QJsonDocument>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <json/json.h>

namespace sicnu::app::teaching {

GuidedLabWorkspace::~GuidedLabWorkspace()
{
  // R30-T07 R1 (P1-4): last durability boundary. flushDrafts() is a no-op
  // when no store was configured, so the headless in-memory behavior stays.
  flushDrafts();
}

void GuidedLabWorkspace::hideEvent( QHideEvent *event )
{
  flushDrafts();
  QWidget::hideEvent( event );
}

GuidedLabWorkspace::GuidedLabWorkspace( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "undergradGuidedLabWorkspace" ) );
  auto *root = new QVBoxLayout( this );
  m_labTitle = new QLabel( this );
  m_readinessLabel = new QLabel( this );
  m_autonomyLabel = new QLabel( this );
  m_stepList = new QListWidget( this );
  m_stepList->setMaximumHeight( 140 );
  connect( m_stepList, &QListWidget::currentRowChanged, this, [this]( int row ) {
    if ( row < 0 ) return;
    // Step-identity switch (R29-T04): park the OUTGOING step's manual draft
    // under its stable id before the timeline moves, then restore the new
    // step's own draft. Before this, one shared input box kept step 1's
    // answer sitting in step 2's form. A timeline REBUILD is not a user
    // step switch — setTimeline parks under the old ids and restores the
    // new step's own draft itself.
    if ( !m_timelineRebuilding ) {
      if ( const auto *outgoing = m_tl.current() )
        m_stepDrafts.insert( stepDraftKey( outgoing->stepId ),
                             m_humanInput->toPlainText() );
      m_tl.currentIndex = row;
      showCurrent();
      if ( const auto *incoming = m_tl.current() )
        m_humanInput->setPlainText(
          m_stepDrafts.value( stepDraftKey( incoming->stepId ) ) );
      flushDrafts(); // R30-T07: a step switch is a durability boundary
      emit stepIndexChanged( row );
    }
  } );
  m_stepTitle = new QLabel( this );
  m_kindLabel = new QLabel( this );
  m_whyView = new QTextEdit( this );
  m_whyView->setReadOnly( true );
  m_whyView->setPlaceholderText( tr( "Why-this-step / explainable workflow notes" ) );
  m_paramsView = new QTextEdit( this );
  m_paramsView->setReadOnly( true );
  m_humanInput = new QPlainTextEdit( this );
  m_humanInput->setPlaceholderText( tr( "Manual / reflection step: enter structured evidence here (the answer key is never leaked)" ) );
  m_artifactEdit = new QLineEdit( this );
  m_artifactEdit->setPlaceholderText(
    tr( "Artifact paths (files produced by the Processing Toolbox, used for validation and grading)" ) );
  m_artifactEdit->setClearButtonEnabled( true );
  m_artifactEdit->setObjectName( QStringLiteral( "labWorkspaceArtifactEdit" ) );
  m_feedbackView = new QTextEdit( this );
  m_feedbackView->setReadOnly( true );
  m_feedbackView->setObjectName( QStringLiteral( "labWorkspaceFeedbackView" ) );

  auto *nav = new QHBoxLayout;
  m_prevBtn = new QPushButton( tr( "Previous" ), this );
  m_nextBtn = new QPushButton( tr( "Next" ), this );
  m_runBtn = new QPushButton( tr( "Run / Jump to Operator" ), this );
  nav->addWidget( m_prevBtn );
  nav->addWidget( m_nextBtn );
  nav->addWidget( m_runBtn );

  auto *actions = new QHBoxLayout;
  m_submitHumanBtn = new QPushButton( tr( "Submit Manual Evidence" ), this );
  m_validateBtn = new QPushButton( tr( "Validate & Grade" ), this );
  m_validateBtn->setObjectName( QStringLiteral( "labWorkspaceValidateButton" ) );
  m_validateBtn->setProperty( "primary", true );
  m_exportBtn = new QPushButton( tr( "Export Capsule / Report" ), this );
  m_exportBtn->setObjectName( QStringLiteral( "labWorkspaceExportButton" ) );
  actions->addWidget( m_submitHumanBtn );
  actions->addWidget( m_validateBtn );
  actions->addWidget( m_exportBtn );

  connect( m_prevBtn, &QPushButton::clicked, this, [this]() {
    if ( m_tl.currentIndex > 0 ) {
      m_tl.currentIndex--;
      m_stepList->setCurrentRow( m_tl.currentIndex );
    }
  } );
  connect( m_nextBtn, &QPushButton::clicked, this, [this]() {
    if ( m_tl.currentIndex + 1 < static_cast<int>( m_tl.steps.size() ) ) {
      m_tl.currentIndex++;
      m_stepList->setCurrentRow( m_tl.currentIndex );
    }
  } );
  connect( m_runBtn, &QPushButton::clicked, this, [this]() {
    const auto *cur = m_tl.current();
    if ( !cur ) return;
    if ( cur->humanRequired ) {
      emit jumpWorkbenchRequested( QString::fromStdString( cur->operatorId ) );
      return;
    }
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    const QString params = QString::fromStdString( Json::writeString( b, cur->params ) );
    emit runOperatorRequested( QString::fromStdString( cur->operatorId ), params );
  } );
  connect( m_submitHumanBtn, &QPushButton::clicked, this, [this]() {
    const auto *cur = m_tl.current();
    if ( !cur ) return;
    m_stepDrafts.insert( stepDraftKey( cur->stepId ), m_humanInput->toPlainText() );
    flushDrafts(); // explicit submission is durable evidence (NOT a pass)
    emit humanEvidenceSubmitted( QString::fromStdString( cur->stepId ), m_humanInput->toPlainText() );
  } );
  connect( m_validateBtn, &QPushButton::clicked, this, &GuidedLabWorkspace::validateRequested );
  connect( m_exportBtn, &QPushButton::clicked, this, &GuidedLabWorkspace::exportCapsuleRequested );

  root->addWidget( m_labTitle );
  root->addWidget( m_readinessLabel );
  root->addWidget( m_autonomyLabel );
  root->addWidget( m_stepList );
  root->addWidget( m_stepTitle );
  root->addWidget( m_kindLabel );
  root->addWidget( new QLabel( tr( "Why-this-step" ), this ) );
  root->addWidget( m_whyView, 1 );
  root->addWidget( new QLabel( tr( "Parameter Preview (answer key hidden)" ), this ) );
  root->addWidget( m_paramsView );
  root->addWidget( m_humanInput );
  root->addWidget( new QLabel( tr( "Artifacts to validate" ), this ) );
  root->addWidget( m_artifactEdit );
  root->addLayout( nav );
  root->addLayout( actions );
  root->addWidget( new QLabel( tr( "Validation / grading feedback" ), this ) );
  root->addWidget( m_feedbackView, 1 );
}

void GuidedLabWorkspace::setTimeline( const sicnu::teaching::LabStepTimeline &tl )
{
  // Lab switch: the previous lab's manual drafts belong to its steps; a
  // fresh lab starts from a clean sheet, never resurrecting text from an
  // unrelated timeline (draft keys include the lab id, so this is
  // belt-and-braces against alias ids).
  if ( m_tl.labId != tl.labId )
  {
    // R30-T07 WP10: a lab switch PERSISTS the outgoing lab's drafts instead
    // of clearing them. With no store configured, keep the original
    // in-memory-only behavior so headless regressions stay intact.
    flushDrafts();
    m_stepDrafts.clear();
  }
  else if ( const auto *outgoing = m_tl.current() )
    // Same lab re-projected: park the visible draft under the outgoing
    // step's stable id before the timeline moves.
    m_stepDrafts.insert( stepDraftKey( outgoing->stepId ), m_humanInput->toPlainText() );
  m_tl = tl;
  m_timelineRebuilding = true;
  rebuildSteps();
  // rebuildSteps' setCurrentRow already fired the guarded handler; index
  // and panes follow the timeline's own current step. (A stepless timeline
  // keeps index 0 — clamp over an empty range is UB.)
  if ( !m_tl.steps.empty() )
    m_tl.currentIndex = std::clamp( m_tl.currentIndex, 0,
                                    static_cast<int>( m_tl.steps.size() ) - 1 );
  showCurrent();
  m_timelineRebuilding = false;
  if ( const auto *cur = m_tl.current() )
    m_humanInput->setPlainText( m_stepDrafts.value( stepDraftKey( cur->stepId ) ) );
  if ( !m_draftStorePath.isEmpty() )
    restoreDraftsForCurrentLab();
  flushDrafts();
}

void GuidedLabWorkspace::setDraftStore( const QString &path, const QString &courseId )
{
  const bool alreadyConfigured = !m_draftStorePath.isEmpty();
  m_draftStorePath = path;
  m_draftCourseId = courseId;
  m_draftStatus.clear();
  if ( path.isEmpty() )
    return;
  // R30-T07 R1 (P1-4): the cockpit calls this both at construction and after
  // the manifest loads. A second call must NOT overwrite text the student is
  // already editing; only the first configuration restores from disk.
  restoreDraftsForCurrentLab( !alreadyConfigured );
}

void GuidedLabWorkspace::restoreDraftsForCurrentLab( bool applyToEditor )
{
  const auto state = sicnu::teaching::loadLabDrafts( m_draftStorePath.toStdString() );
  if ( !state.ok )
  {
    m_draftStatus = tr( "Local drafts could not be restored: %1" )
                      .arg( state.issues.empty()
                                ? QStringLiteral( "unknown" )
                                : QString::fromStdString( state.issues.front() ) );
    return;
  }
  int restored = 0;
  for ( const auto &record : state.drafts )
  {
    if ( record.courseId != m_draftCourseId.toStdString()
         || record.labId != m_tl.labId )
      continue;
    const auto validity = sicnu::teaching::checkLabDraftValidity(
      record, m_rulesVersion.toStdString(), artifactPath().toStdString() );
    if ( !validity.valid )
    {
      // Keep the TEXT (never destroy student work), but say why it needs a
      // fresh look instead of silently treating it as current.
      m_draftStatus = tr( "Restored draft for step %1 is stale: %2" )
                        .arg( QString::fromStdString( record.stepId ),
                              QString::fromStdString(
                                validity.reasons.empty() ? std::string( "context changed" )
                                                         : validity.reasons.front() ) );
    }
    ++restored;
    if ( !applyToEditor )
      continue; // validity-only pass: never overwrite live in-memory text
    m_stepDrafts.insert(
      QString::fromStdString( m_tl.labId ) + QLatin1Char( '/' )
        + QString::fromStdString( record.stepId ),
      QString::fromStdString( record.text ) );
  }
  if ( m_draftStatus.isEmpty() && restored > 0 )
    m_draftStatus = tr( "%1 local draft(s) restored for this lab (not a completion)." )
                      .arg( restored );
  if ( applyToEditor )
  {
    if ( const auto *cur = m_tl.current() )
      m_humanInput->setPlainText( m_stepDrafts.value( stepDraftKey( cur->stepId ) ) );
  }
}

bool GuidedLabWorkspace::refreshDraftValidity( const QString &rulesVersion )
{
  m_rulesVersion = rulesVersion;
  m_draftStatus.clear();
  // Re-evaluate the restored drafts under the CURRENT rules/artifact
  // context. setTimeline() restored them before the rules file was resolved,
  // so this second pass is what actually detects a rule/artifact change.
  if ( !m_draftStorePath.isEmpty() )
    restoreDraftsForCurrentLab( false );
  const bool stale = m_draftStatus.contains( QStringLiteral( "stale" ),
                                             Qt::CaseInsensitive );
  if ( stale )
    appendFeedbackNote( m_draftStatus );
  return stale;
}

void GuidedLabWorkspace::flushDrafts()
{
  if ( m_draftStorePath.isEmpty() )
    return;
  // A fail-closed timeline has no lab identity; writing would produce an
  // invalid record and a false "save failed" note.
  if ( m_tl.labId.empty() )
    return;
  // Park the visible editor text under the CURRENT stable step id first.
  if ( const auto *cur = m_tl.current() )
    m_stepDrafts.insert( stepDraftKey( cur->stepId ), m_humanInput->toPlainText() );
  // Load-then-merge: drafts belonging to OTHER labs/courses are preserved.
  const auto state = sicnu::teaching::loadLabDrafts( m_draftStorePath.toStdString() );
  if ( !state.ok )
  {
    // R30-T07 R1 (P1-4): NEVER overwrite an unreadable/oversize draft file.
    // Rewriting from an empty merge would destroy other labs' real work;
    // leave the previous bytes intact and report the typed refusal.
    m_draftStatus = tr( "Local drafts were NOT saved: the existing draft file is unreadable (%1)" )
                      .arg( state.issues.empty()
                                ? QStringLiteral( "unknown" )
                                : QString::fromStdString( state.issues.front() ) );
    appendFeedbackNote( m_draftStatus );
    return;
  }
  std::vector<sicnu::teaching::LabDraftRecord> records;
  for ( const auto &record : state.drafts )
  {
    const bool sameCourse = record.courseId == m_draftCourseId.toStdString();
    const bool sameLab = record.labId == m_tl.labId;
    if ( sameCourse && sameLab )
      continue; // replaced by the live in-memory map below
    records.push_back( record );
  }
  const QString nowIso = QDateTime::currentDateTimeUtc().toString( Qt::ISODate );
  for ( auto it = m_stepDrafts.constBegin(); it != m_stepDrafts.constEnd(); ++it )
  {
    const QString key = it.key();
    // stepDraftKey() is "<labId>/<stepId>"; step ids may themselves contain
    // '/', so strip the known lab prefix instead of splitting on a slash.
    const QString labPrefix = QString::fromStdString( m_tl.labId ) + QLatin1Char( '/' );
    const QString stepId = key.startsWith( labPrefix ) ? key.mid( labPrefix.size() )
                                                       : key;
    if ( stepId.isEmpty() )
      continue;
    sicnu::teaching::LabDraftRecord record;
    record.courseId = m_draftCourseId.toStdString();
    record.labId = m_tl.labId;
    record.stepId = stepId.toStdString();
    record.text = it.value().toStdString();
    record.submitted = false; // a draft is never a completion or a pass
    record.rulesVersion = m_rulesVersion.toStdString();
    record.artifactPath = artifactPath().toStdString();
    record.updatedAtIso = nowIso.toStdString();
    records.push_back( record );
  }
  std::string error;
  if ( !sicnu::teaching::saveLabDrafts( m_draftStorePath.toStdString(), records, &error ) )
  {
    m_draftStatus = tr( "Local draft could not be saved: %1" )
                      .arg( QString::fromStdString( error ) );
    appendFeedbackNote( m_draftStatus );
  }
}

QString GuidedLabWorkspace::stepDraftKey( const std::string &stepId ) const
{
  return QString::fromStdString( m_tl.labId ) + QLatin1Char( '/' )
         + QString::fromStdString( stepId );
}

void GuidedLabWorkspace::setReadiness( const sicnu::teaching::LabReadiness &r )
{
  m_readiness = r;
  QString text = tr( "Readiness: %1" ).arg(
    QString::fromUtf8( sicnu::teaching::readinessLevelLabelZh( r.level ) ) );
  const int visibleReasons = std::min<int>( 2, static_cast<int>( r.items.size() ) );
  for ( int i = 0; i < visibleReasons; ++i )
    text += tr( "\n⚠ %1" ).arg( QString::fromStdString( r.items[i].reasonZh ) );
  if ( static_cast<int>( r.items.size() ) > visibleReasons )
    text += tr( "\n+%1 more (hover for details)" ).arg( static_cast<int>( r.items.size() ) - visibleReasons );
  m_readinessLabel->setWordWrap( true );
  m_readinessLabel->setText( text );
  QString tip;
  for ( const auto &it : r.items ) {
    tip += QStringLiteral( "[%1/%2] %3 (%4)\n" )
             .arg( QString::fromStdString( it.severity ),
                   QString::fromStdString( it.category ),
                   QString::fromStdString( it.reasonZh ),
                   QString::fromStdString( it.evidenceSource ) );
  }
  m_readinessLabel->setToolTip( tip );
}

void GuidedLabWorkspace::setAutonomy( const sicnu::teaching::AutonomyEffectiveDisplay &a )
{
  m_autonomy = a;
  QString text =
    tr( "Autonomy level: %1 (%2)" )
      .arg( QString::fromStdString( a.effectiveLevel ) )
      .arg( a.ladderLabelsZh.empty()
              ? QString()
              : QString::fromStdString(
                  a.ladderLabelsZh[static_cast<size_t>( std::clamp(
                    a.effectiveOrdinal, 0, static_cast<int>( a.ladderLabelsZh.size() ) - 1 ) )] ) );
  // Policy issues (parse failures, undeclared authority) surface on the
  // label itself — a tooltip-only honesty note is too easy to miss.
  if ( !a.issuesZh.empty() )
    text += QStringLiteral( "  ⚠ %1" ).arg( QString::fromStdString( a.issuesZh.front() ) );
  m_autonomyLabel->setText( text );
  QString tip = tr( "Ladder:\n" );
  for ( const auto &l : a.ladderLabelsZh ) tip += QString::fromStdString( l ) + QLatin1Char( '\n' );
  for ( const auto &row : a.rows ) {
    tip += QStringLiteral( "%1 → %2 (%3)\n" )
             .arg( QString::fromStdString( row.capability ),
                   QString::fromStdString( row.decision ),
                   QString::fromStdString( row.reasonZh ) );
  }
  m_autonomyLabel->setToolTip( tip );
}

void GuidedLabWorkspace::setFeedback( const sicnu::teaching::LabFeedbackProjection &f )
{
  m_feedback = f;
  QString text = tr( "Overall: %1 (counted as pass=%2)\n%3\n%4\n" )
                   .arg( QString::fromStdString( f.overallStatusZh ) )
                   .arg( f.overallCountsAsPass ? tr( "Yes" ) : tr( "No" ) )
                   .arg( QString::fromStdString( f.techValidationSummaryZh ) )
                   .arg( QString::fromStdString( f.scienceValidationSummaryZh ) );
  for ( const auto &row : f.rows ) {
    text += QStringLiteral( "- [%1] %2: %3 — %4\n" )
              .arg( QString::fromStdString( row.layer ),
                    QString::fromStdString( row.id ),
                    QString::fromStdString( row.statusZh ),
                    QString::fromStdString( row.reasonZh ) );
  }
  if ( !f.capsuleExportRef.empty() )
    text += tr( "\nCapsule reference: %1" ).arg( QString::fromStdString( f.capsuleExportRef ) );
  // Honest "why": engine refusals, skipped lenses and other issues are part
  // of the student surface — an indeterminate verdict without its reason is
  // not honest feedback.
  for ( const auto &i : f.issuesZh )
    text += QStringLiteral( "\n⚠ %1" ).arg( QString::fromStdString( i ) );
  m_feedbackView->setPlainText( text );
}

QString GuidedLabWorkspace::artifactPath() const
{
  return m_artifactEdit ? m_artifactEdit->text().trimmed() : QString();
}

void GuidedLabWorkspace::setArtifactPath( const QString &path )
{
  if ( m_artifactEdit ) m_artifactEdit->setText( path );
}

void GuidedLabWorkspace::clearFeedback()
{
  m_feedback = sicnu::teaching::LabFeedbackProjection{};
  m_feedbackView->clear();
}

void GuidedLabWorkspace::appendFeedbackNote( const QString &noteZh )
{
  m_feedbackView->append( noteZh );
}

void GuidedLabWorkspace::rebuildSteps()
{
  m_labTitle->setText( QString::fromStdString( m_tl.titleZh.empty() ? m_tl.labId : m_tl.titleZh ) );
  m_stepList->clear();
  for ( const auto &s : m_tl.steps ) {
    m_stepList->addItem(
      QStringLiteral( "%1. %2 [%3]%4" )
        .arg( s.index + 1 )
        .arg( QString::fromStdString( s.titleZh.empty() ? s.title : s.titleZh ) )
        .arg( QString::fromStdString( s.kind ) )
        .arg( s.humanRequired ? tr( " Manual step required" ) : QString() ) );
  }
  if ( !m_tl.steps.empty() )
    m_stepList->setCurrentRow( std::clamp( m_tl.currentIndex, 0,
                                           static_cast<int>( m_tl.steps.size() ) - 1 ) );
}

void GuidedLabWorkspace::showCurrent()
{
  const auto *cur = m_tl.current();
  if ( !cur ) {
    m_stepTitle->setText( tr( "No steps" ) );
    m_kindLabel->clear();
    m_paramsView->clear();
    m_humanInput->setEnabled( false );
    // Fail-closed buttons: a stepless/fail-closed timeline (e.g. a canonical
    // lab whose doc failed to load) must never keep the previous lab's
    // navigation or run affordances alive. The grading affordances die with
    // them (#1453/WP02): "Validate & grade" and "Export capsule" used to
    // stay armed on a lab the registry marks out-of-scope, offering to
    // grade and export against a session that does not exist.
    m_runBtn->setEnabled( false );
    m_submitHumanBtn->setEnabled( false );
    m_prevBtn->setEnabled( false );
    m_nextBtn->setEnabled( false );
    m_validateBtn->setEnabled( false );
    m_exportBtn->setEnabled( false );
    m_artifactEdit->setEnabled( false );
    // The projection's refusal reasons belong on the student surface — a
    // bare "无步骤" without its reason is not honest feedback.
    if ( !m_tl.issuesZh.empty() ) {
      QString reasons;
      for ( const auto &issue : m_tl.issuesZh )
        reasons += QStringLiteral( "⚠ %1\n" ).arg( QString::fromStdString( issue ) );
      m_whyView->setPlainText( reasons.trimmed() );
    }
    return;
  }
  m_stepTitle->setText( QString::fromStdString( cur->titleZh.empty() ? cur->title : cur->titleZh ) );
  m_kindLabel->setText(
    tr( "type=%1  operator=%2  manual=%3  AI allowed=%4" )
      .arg( QString::fromStdString( cur->kind ),
            QString::fromStdString( cur->operatorId ),
            cur->humanRequired ? tr( "Yes" ) : tr( "No" ),
            cur->aiAllowed ? tr( "Yes" ) : tr( "No" ) ) );
  // Step-identity why sync (R29-T04): the why pane is rewritten from the
  // CURRENT step on every navigation. The old "only when empty" guard kept
  // step 1's explanation on screen while the student read step 2 — the pane
  // must never explain a step the student is not on. An author who provided
  // no hint gets an explicit "not authored" note, not the previous step's
  // text; the provenance line keeps the authorship vs evidence distinction
  // visible (authoring guidance, not a runtime-verified fact).
  m_whyView->setPlainText( tr( "(explainable workflow projection)\n" ) );
  if ( !cur->whyHintZh.empty() )
    m_whyView->append( QString::fromStdString( cur->whyHintZh ) );
  else
    m_whyView->append( tr( "(the author provided no explanation for this step)" ) );
  m_whyView->append(
    tr( "\nProvenance badges: system fact | authoring guidance | inferred" ) );
  Json::StreamWriterBuilder b;
  b["indentation"] = "  ";
  m_paramsView->setPlainText( QString::fromStdString( Json::writeString( b, cur->paramsDisplay ) ) );
  m_humanInput->setEnabled( cur->humanRequired || cur->kind == "reflection" );
  m_runBtn->setEnabled( !cur->operatorId.empty() || cur->humanRequired );
  m_prevBtn->setEnabled( m_tl.previous() != nullptr );
  m_nextBtn->setEnabled( m_tl.next() != nullptr );
  // A live step re-arms the grading affordances the fail-closed branch
  // disabled — no dead-end state after a failed lab replaced by a good one.
  m_validateBtn->setEnabled( true );
  m_exportBtn->setEnabled( true );
  m_artifactEdit->setEnabled( true );
}

} // namespace sicnu::app::teaching

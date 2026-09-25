#include "guided_lab_workspace.h"
#include <algorithm>
#include <cstdlib>
#include "status_badge.h"

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
    m_tl.currentIndex = row;
    showCurrent();
    emit stepIndexChanged( row );
  } );
  m_stepTitle = new QLabel( this );
  m_kindLabel = new QLabel( this );
  m_whyView = new QTextEdit( this );
  m_whyView->setReadOnly( true );
  m_whyView->setPlaceholderText( tr( "Why-this-step / 可解释工作流说明" ) );
  m_paramsView = new QTextEdit( this );
  m_paramsView->setReadOnly( true );
  m_humanInput = new QPlainTextEdit( this );
  m_humanInput->setPlaceholderText( tr( "人工/反思步骤：在此填写结构化证据（不会泄露标准答案）" ) );
  m_artifactEdit = new QLineEdit( this );
  m_artifactEdit->setPlaceholderText(
    tr( "产物路径（处理工具箱输出的文件，用于验证与评分）" ) );
  m_artifactEdit->setClearButtonEnabled( true );
  m_artifactEdit->setObjectName( QStringLiteral( "labWorkspaceArtifactEdit" ) );
  m_feedbackView = new QTextEdit( this );
  m_feedbackView->setReadOnly( true );
  m_feedbackView->setObjectName( QStringLiteral( "labWorkspaceFeedbackView" ) );

  auto *nav = new QHBoxLayout;
  m_prevBtn = new QPushButton( tr( "上一步" ), this );
  m_nextBtn = new QPushButton( tr( "下一步" ), this );
  m_runBtn = new QPushButton( tr( "运行/跳转算子" ), this );
  m_submitHumanBtn = new QPushButton( tr( "提交人工证据" ), this );
  m_validateBtn = new QPushButton( tr( "验证与评分" ), this );
  m_exportBtn = new QPushButton( tr( "导出胶囊/报告" ), this );
  nav->addWidget( m_prevBtn );
  nav->addWidget( m_nextBtn );
  nav->addWidget( m_runBtn );
  nav->addWidget( m_submitHumanBtn );
  nav->addWidget( m_validateBtn );
  nav->addWidget( m_exportBtn );

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
  root->addWidget( new QLabel( tr( "参数（教学掩码后）" ), this ) );
  root->addWidget( m_paramsView );
  root->addWidget( m_humanInput );
  root->addWidget( new QLabel( tr( "待验证产物" ), this ) );
  root->addWidget( m_artifactEdit );
  root->addLayout( nav );
  root->addWidget( new QLabel( tr( "验证 / 评分反馈" ), this ) );
  root->addWidget( m_feedbackView, 1 );
}

void GuidedLabWorkspace::setTimeline( const sicnu::teaching::LabStepTimeline &tl )
{
  m_tl = tl;
  rebuildSteps();
  showCurrent();
}

void GuidedLabWorkspace::setReadiness( const sicnu::teaching::LabReadiness &r )
{
  m_readiness = r;
  m_readinessLabel->setText(
    tr( "就绪: %1" ).arg( QString::fromUtf8( sicnu::teaching::readinessLevelLabelZh( r.level ) ) ) );
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
  m_autonomyLabel->setText(
    tr( "自主等级: %1（%2）" )
      .arg( QString::fromStdString( a.effectiveLevel ) )
      .arg( a.ladderLabelsZh.empty()
              ? QString()
              : QString::fromStdString(
                  a.ladderLabelsZh[static_cast<size_t>(
                    std::clamp( a.effectiveOrdinal, 0, 5 ) )] ) ) );
  QString tip = tr( "阶梯:\n" );
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
  QString text = tr( "总评: %1（计为通过=%2）\n%3\n%4\n" )
                   .arg( QString::fromStdString( f.overallStatusZh ) )
                   .arg( f.overallCountsAsPass ? tr( "是" ) : tr( "否" ) )
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
    text += tr( "\n胶囊引用: %1" ).arg( QString::fromStdString( f.capsuleExportRef ) );
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

void GuidedLabWorkspace::setWhyMarkdown( const QString &md )
{
  m_whyView->setMarkdown( md );
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
        .arg( s.humanRequired ? tr( " 需要人工" ) : QString() ) );
  }
  if ( !m_tl.steps.empty() )
    m_stepList->setCurrentRow( std::clamp( m_tl.currentIndex, 0,
                                           static_cast<int>( m_tl.steps.size() ) - 1 ) );
}

void GuidedLabWorkspace::showCurrent()
{
  const auto *cur = m_tl.current();
  if ( !cur ) {
    m_stepTitle->setText( tr( "无步骤" ) );
    m_kindLabel->clear();
    m_paramsView->clear();
    m_humanInput->setEnabled( false );
    // Fail-closed buttons: a stepless/fail-closed timeline (e.g. a canonical
    // lab whose doc failed to load) must never keep the previous lab's
    // navigation or run affordances alive.
    m_runBtn->setEnabled( false );
    m_submitHumanBtn->setEnabled( false );
    m_prevBtn->setEnabled( false );
    m_nextBtn->setEnabled( false );
    return;
  }
  m_stepTitle->setText( QString::fromStdString( cur->titleZh.empty() ? cur->title : cur->titleZh ) );
  m_kindLabel->setText(
    tr( "类型=%1  算子=%2  人工=%3  AI允许=%4" )
      .arg( QString::fromStdString( cur->kind ),
            QString::fromStdString( cur->operatorId ),
            cur->humanRequired ? tr( "是" ) : tr( "否" ),
            cur->aiAllowed ? tr( "是" ) : tr( "否" ) ) );
  if ( !cur->whyHintZh.empty() && m_whyView->toPlainText().isEmpty() )
    m_whyView->setPlainText( QString::fromStdString( cur->whyHintZh ) );
  Json::StreamWriterBuilder b;
  b["indentation"] = "  ";
  m_paramsView->setPlainText( QString::fromStdString( Json::writeString( b, cur->paramsDisplay ) ) );
  m_humanInput->setEnabled( cur->humanRequired || cur->kind == "reflection" );
  m_runBtn->setEnabled( !cur->operatorId.empty() || cur->humanRequired );
  m_prevBtn->setEnabled( m_tl.previous() != nullptr );
  m_nextBtn->setEnabled( m_tl.next() != nullptr );
}

} // namespace sicnu::app::teaching

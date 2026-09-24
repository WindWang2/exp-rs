/***************************************************************************
 * step_explanation_panel.cpp — see step_explanation_panel.h
 ***************************************************************************/
#include "step_explanation_panel.h"

#include "explain/explanation_builder.h"
#include "explain/step_explanation.h"
#include "explain/step_explanation_view_model.h"

#include <QLabel>
#include <QVBoxLayout>

namespace sicnu::app
{
namespace
{
constexpr int kMaxLineChars = 2000; // one evidence line stays readable; longer is elided

QString escape( const QString &text )
{
    return text.toHtmlEscaped();
}

QString badgeColor( const QString &badge )
{
    if ( badge == QLatin1String( "系统事实" ) )
        return QStringLiteral( "#2e7d32" );
    if ( badge == QLatin1String( "编写指引" ) )
        return QStringLiteral( "#1565c0" );
    return QStringLiteral( "#616161" ); // 推断 / structural
}

QString badgeSpan( const QString &badge )
{
    if ( badge.isEmpty() )
        return QString();
    return QStringLiteral( "<span style='color:%1'>[%2]</span> " )
        .arg( badgeColor( badge ), escape( badge ) );
}

QString evidenceSuffix( const QString &note )
{
    if ( note.isEmpty() )
        return QString();
    return QStringLiteral( " <span style='color:#757575'>（证据: %1）</span>" ).arg( escape( note ) );
}

QString elide( const QString &text )
{
    if ( text.size() <= kMaxLineChars )
        return text;
    return text.left( kMaxLineChars ) + QStringLiteral( "…（内容过长，已截断显示）" );
}

} // namespace

StepExplanationPanel::StepExplanationPanel( KnowledgeProvider knowledge, GuidanceProvider guidance,
                                            EvidenceProvider evidence, QWidget *parent )
    : QWidget( parent ), m_knowledge( std::move( knowledge ) ), m_guidance( std::move( guidance ) ),
      m_evidence( std::move( evidence ) )
{
    setObjectName( QStringLiteral( "rsStepExplanationPanel" ) );
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 8, 8, 8, 8 );
    m_body = new QLabel( this );
    m_body->setObjectName( QStringLiteral( "rsStepExplanationBody" ) );
    m_body->setTextFormat( Qt::RichText );
    m_body->setAlignment( Qt::AlignTop | Qt::AlignLeft );
    m_body->setWordWrap( true );
    layout->addWidget( m_body );
    layout->addStretch( 1 );
    m_body->setText( tr( "未选择需要解释的步骤。" ) );
}

void StepExplanationPanel::setRendered( const QString &html, const QString &plainText )
{
    m_body->setText( html );
    m_plainText = plainText;
}

void StepExplanationPanel::showStep( const sicnu::explain::ExplanationRequest &request,
                                     const QStringList &evidenceProblemCodes )
{
    ++m_generation;
    m_failureCode.clear();
    m_hasExplanation = false;
    m_markdown.clear();
    m_lastRequest = request;

    const sicnu::explain::IOperatorKnowledge *knowledge = m_knowledge ? m_knowledge() : nullptr;
    const sicnu::explain::IAuthoredGuidance *guidance = m_guidance ? m_guidance() : nullptr;
    const sicnu::explain::IExecutionEvidence *evidence = m_evidence ? m_evidence() : nullptr;
    if ( !knowledge || !guidance )
    {
        setRendered( tr( "解释知识源不可用（算子注册表或编写指引库未就绪），无法解释此步骤。" ),
                     tr( "解释知识源不可用（算子注册表或编写指引库未就绪），无法解释此步骤。" ) );
        return;
    }

    const sicnu::explain::BuildOutcome outcome = sicnu::explain::StepExplanationBuilder(
                                                     *knowledge, *guidance, evidence )
                                                     .build( request );
    if ( outcome.failed() )
    {
        m_failureCode = QString::fromStdString( outcome.failureCode );
        const QString line = QString::fromStdString( outcome.failureCode ) + QStringLiteral( ": " )
                             + QString::fromStdString( outcome.failureMessage );
        setRendered( QStringLiteral( "<h3>%1</h3><p style='color:#b71c1c'>%2</p>" )
                         .arg( tr( "无法解释此步骤" ), escape( line ) ),
                     tr( "无法解释此步骤" ) + QStringLiteral( "\n" ) + line );
        return;
    }

    renderExplanation( outcome.explanation, request, evidenceProblemCodes );

    // Non-fatal build problems stay visible verbatim (contradictions,
    // unknown parameter references) — the panel never filters them.
    if ( !outcome.problems.empty() )
    {
        QString problemHtml = QStringLiteral( "<h3>%1</h3><ul>" ).arg( tr( "问题（Problems）" ) );
        QString problemText = tr( "问题（Problems）" ) + QStringLiteral( ":\n" );
        for ( const sicnu::explain::BuildProblem &problem : outcome.problems )
        {
            const QString line = QStringLiteral( "%1 [%2] %3" )
                                     .arg( QString::fromStdString( problem.code ),
                                           QString::fromStdString( problem.field ),
                                           QString::fromStdString( problem.message ) );
            problemHtml += QStringLiteral( "<li><span style='color:#b8860b'>%1</span></li>" )
                               .arg( escape( elide( line ) ) );
            problemText += QStringLiteral( "- " ) + line + QStringLiteral( "\n" );
        }
        problemHtml += QLatin1String( "</ul>" );
        QString html = m_body->text();
        html += problemHtml;
        setRendered( html, m_plainText + QStringLiteral( "\n" ) + problemText );
    }

    m_hasExplanation = true;
    emit explanationShown();
}

void StepExplanationPanel::renderExplanation( const sicnu::explain::StepExplanation &explanation,
                                              const sicnu::explain::ExplanationRequest &request,
                                              const QStringList &evidenceProblemCodes )
{
    const sicnu::explain::StepExplanationViewModel model =
        sicnu::explain::StepExplanationViewModel::fromExplanation( explanation );
    m_markdown = QString::fromStdString( model.toMarkdown() );

    QString html = QStringLiteral( "<h2>%1</h2>" ).arg( escape( elide( QString::fromStdString( model.headline ) ) ) );
    QString plain = QString::fromStdString( model.headline );
    if ( !model.operatorLine.empty() )
    {
        html += QStringLiteral( "<p>%1 <code>%2</code></p>" )
                    .arg( tr( "算子:" ), escape( QString::fromStdString( model.operatorLine ) ) );
        plain += QStringLiteral( "\n算子: " ) + QString::fromStdString( model.operatorLine );
    }

    bool executionShown = false;
    for ( const sicnu::explain::ExplanationSection &section : model.sections )
    {
        executionShown = executionShown || section.id == "execution";
        html += QStringLiteral( "<h3>%1</h3><ul>" ).arg( escape( QString::fromStdString( section.titleZh ) ) );
        plain += QStringLiteral( "\n" ) + QString::fromStdString( section.titleZh ) + QStringLiteral( ":\n" );
        for ( const sicnu::explain::ExplanationLine &line : section.lines )
        {
            html += QStringLiteral( "<li>%1%2%3</li>" )
                        .arg( badgeSpan( QString::fromStdString( line.badge ) ),
                              escape( elide( QString::fromStdString( line.text ) ) ),
                              evidenceSuffix( QString::fromStdString( line.evidenceNote ) ) );
            plain += QStringLiteral( "- " );
            if ( !line.badge.empty() )
                plain += QStringLiteral( "[%1] " ).arg( QString::fromStdString( line.badge ) );
            plain += QString::fromStdString( line.text );
            if ( !line.evidenceNote.empty() )
                plain += QStringLiteral( "（证据: %1）" ).arg( QString::fromStdString( line.evidenceNote ) );
            plain += QStringLiteral( "\n" );
        }
        html += QLatin1String( "</ul>" );
    }

    // Honest absence right where the execution section would have been: the
    // record carries no facts for this step, so no status is shown — only
    // the reason nothing is shown.
    if ( !executionShown )
    {
        const QString unknown =
            request.runId.empty()
                ? tr( "执行情况未知（计划模式：尚无此步骤的运行证据）。" )
                : tr( "执行情况未知（运行 %1 中没有此步骤的执行证据）。" )
                      .arg( QString::fromStdString( request.runId ) );
        html += QStringLiteral( "<h3>%1</h3><p style='color:#616161'>%2</p>" )
                    .arg( tr( "执行情况（Execution）" ), escape( unknown ) );
        plain += QStringLiteral( "\n" ) + tr( "执行情况（Execution）" ) + QStringLiteral( ":\n- " ) + unknown
                 + QStringLiteral( "\n" );
    }

    // Refused evidence records surface as such — next to the execution
    // section whether it has facts (other records served) or stayed unknown.
    if ( !evidenceProblemCodes.isEmpty() )
    {
        html += QStringLiteral( "<p style='color:#b8860b'>⚠ %1</p><ul>" )
                    .arg( tr( "证据记录问题（记录被拒绝，以下条目不可用）:" ) );
        plain += tr( "证据记录问题（记录被拒绝，以下条目不可用）:" ) + QStringLiteral( "\n" );
        for ( const QString &code : evidenceProblemCodes )
        {
            html += QStringLiteral( "<li><span style='color:#b8860b'>%1</span></li>" ).arg( escape( code ) );
            plain += QStringLiteral( "- " ) + code + QStringLiteral( "\n" );
        }
        html += QLatin1String( "</ul>" );
    }

    if ( !model.trustNotes.empty() )
    {
        html += QStringLiteral( "<h3>%1</h3><ul>" ).arg( tr( "注意（Trust notes）" ) );
        plain += tr( "注意（Trust notes）" ) + QStringLiteral( ":\n" );
        for ( const std::string &note : model.trustNotes )
        {
            html += QStringLiteral( "<li><span style='color:#b8860b'>⚠ %1</span></li>" )
                        .arg( escape( elide( QString::fromStdString( note ) ) ) );
            plain += QStringLiteral( "- ⚠ " ) + QString::fromStdString( note ) + QStringLiteral( "\n" );
        }
        html += QLatin1String( "</ul>" );
    }

    setRendered( html, plain );
}

void StepExplanationPanel::showNote( const QString &text )
{
    ++m_generation;
    m_hasExplanation = false;
    m_markdown.clear();
    m_failureCode.clear();
    m_lastRequest = sicnu::explain::ExplanationRequest();
    setRendered( escape( text ), text );
}

void StepExplanationPanel::reset()
{
    showNote( tr( "未选择需要解释的步骤。" ) );
}

} // namespace sicnu::app

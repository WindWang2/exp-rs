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
    int cut = kMaxLineChars;
    // Never split a UTF-16 surrogate pair at the cut.
    if ( cut > 0 && QChar::isHighSurrogate( text.at( cut - 1 ).unicode() ) )
        --cut;
    return text.left( cut ) + QStringLiteral( "…（内容过长，已截断显示）" );
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
    m_body->setText( tr( "No step selected to explain." ) );
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
        setRendered( tr( "Explanation knowledge sources unavailable (operator registry or authoring guidance library not ready); cannot explain this step." ),
                     tr( "Explanation knowledge sources unavailable (operator registry or authoring guidance library not ready); cannot explain this step." ) );
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
                         .arg( tr( "Cannot explain this step" ), escape( line ) ),
                     tr( "Cannot explain this step" ) + QStringLiteral( "\n" ) + line );
        return;
    }

    renderExplanation( outcome.explanation, request, evidenceProblemCodes );

    // Non-fatal build problems stay visible verbatim (contradictions,
    // unknown parameter references) — the panel never filters them.
    if ( !outcome.problems.empty() )
    {
        QString problemHtml = QStringLiteral( "<h3>%1</h3><ul>" ).arg( tr( "Problems" ) );
        QString problemText = tr( "Problems" ) + QStringLiteral( ":\n" );
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
                    .arg( tr( "Operator:" ), escape( QString::fromStdString( model.operatorLine ) ) );
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
    // the reason nothing is shown (refused record > plan-only > run without
    // evidence).
    if ( !executionShown )
    {
        QString unknown;
        if ( !evidenceProblemCodes.isEmpty() )
            unknown = tr( "Execution status unknown (evidence record rejected; see evidence record issues below)." );
        else if ( request.runId.empty() )
            unknown = tr( "Execution status unknown (plan mode: no run evidence for this step yet)." );
        else
            unknown = tr( "Execution status unknown (run %1 has no execution evidence for this step)." )
                          .arg( QString::fromStdString( request.runId ) );
        html += QStringLiteral( "<h3>%1</h3><p style='color:#616161'>%2</p>" )
                    .arg( tr( "Execution" ), escape( unknown ) );
        plain += QStringLiteral( "\n" ) + tr( "Execution" ) + QStringLiteral( ":\n- " ) + unknown
                 + QStringLiteral( "\n" );
    }

    // Refused evidence records surface as such — next to the execution
    // section whether it has facts (other records served) or stayed unknown.
    if ( !evidenceProblemCodes.isEmpty() )
    {
        html += QStringLiteral( "<p style='color:#b8860b'>⚠ %1</p><ul>" )
                    .arg( tr( "Evidence record issues (record rejected; entries below unavailable):" ) );
        plain += tr( "Evidence record issues (record rejected; entries below unavailable):" ) + QStringLiteral( "\n" );
        for ( const QString &code : evidenceProblemCodes )
        {
            html += QStringLiteral( "<li><span style='color:#b8860b'>%1</span></li>" ).arg( escape( code ) );
            plain += QStringLiteral( "- " ) + code + QStringLiteral( "\n" );
        }
        html += QLatin1String( "</ul>" );
    }

    if ( !model.trustNotes.empty() )
    {
        html += QStringLiteral( "<h3>%1</h3><ul>" ).arg( tr( "Trust notes" ) );
        plain += tr( "Trust notes" ) + QStringLiteral( ":\n" );
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
    showNote( tr( "No step selected to explain." ) );
}

} // namespace sicnu::app

/***************************************************************************
 * step_explanation_section.cpp — see step_explanation_section.h
 ***************************************************************************/
#include "step_explanation_section.h"

#include "explain/adapters/provenance_file_evidence.h"
#include "explain/adapters/workflow_projection.h"
#include "workflow/workflow_ir_v2.h"

#include <QFileInfo>
#include <QVBoxLayout>

namespace sicnu::app
{
namespace
{
// The pinned record name grammar (same pattern ProvenanceFileEvidence
// loads by): provenance_<runId>.json — the runId is everything between.
bool runIdFromFileName( const QString &fileName, QString *runId )
{
    if ( !fileName.startsWith( QLatin1String( "provenance_" ) )
         || !fileName.endsWith( QLatin1String( ".json" ) ) )
        return false;
    const int begin = int( QString( "provenance_" ).size() );
    const int end = fileName.size() - int( QString( ".json" ).size() );
    if ( end <= begin )
        return false;
    *runId = fileName.mid( begin, end - begin );
    return true;
}
} // namespace

StepExplanationSection::StepExplanationSection( StepExplanationPanel::KnowledgeProvider knowledge,
                                                StepExplanationPanel::GuidanceProvider guidance,
                                                DocumentProvider document, QWidget *parent )
    : InspectorSection( parent ), m_document( std::move( document ) )
{
    // Evidence lives here (one adapter per run); the panel re-resolves it on
    // every render, so evidence swaps are visible on the next populate
    // without the panel holding a copy.
    m_panel = new StepExplanationPanel( std::move( knowledge ), std::move( guidance ),
                                        [this]() -> const sicnu::explain::IExecutionEvidence *
                                        { return m_evidence.get(); },
                                        this );
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->addWidget( m_panel );
}

bool StepExplanationSection::supports( const SelectionContextSnapshot &snapshot ) const
{
    if ( !snapshot.hasPipelineNodeSelection() )
        return false;
    // Same rule as ProvenanceSection: the decision needs the provider
    // RESULT — a null document (dock not created yet) is unsupported, not a
    // note.
    if ( !m_document )
        return false;
    return m_document().has_value();
}

void StepExplanationSection::populate( const SelectionContextSnapshot &snapshot )
{
    std::optional<sicnu::workflow::WorkflowDocument> document =
        m_document ? m_document() : std::nullopt;
    if ( !document.has_value() )
    {
        m_panel->showNote( tr( "管线文档不可用，无法解释此节点。" ) );
        return;
    }

    std::vector<sicnu::explain::adapters::ProjectionProblem> projectionProblems;
    const std::optional<sicnu::explain::ExplanationRequest> request =
        sicnu::explain::adapters::projectWorkflowDocument( *document,
                                                           snapshot.selectedPipelineNodeId.toStdString(),
                                                           projectionProblems );
    if ( !request.has_value() )
    {
        m_panel->showNote( tr( "当前文档中没有节点 %1，无法解释。" )
                               .arg( snapshot.selectedPipelineNodeId ) );
        return;
    }

    // Run scope: the current run ref (empty until a run finalizes — the
    // request stays plan-only-honest until then).
    sicnu::explain::ExplanationRequest scoped = *request;
    scoped.runId = m_runId.toStdString();
    m_panel->showStep( scoped, m_evidenceProblemCodes );
}

void StepExplanationSection::attachRunProvenance( const QString &provenanceFilePath )
{
    m_evidence.reset();
    m_runId.clear();
    m_evidenceProblemCodes.clear();
    if ( provenanceFilePath.isEmpty() )
        return;

    QString runId;
    if ( !runIdFromFileName( QFileInfo( provenanceFilePath ).fileName(), &runId ) )
    {
        // Not the pinned record name grammar: serve no evidence and say so.
        m_evidenceProblemCodes.append( QStringLiteral( "malformed_name: %1" )
                                           .arg( QFileInfo( provenanceFilePath ).fileName() ) );
        return;
    }

    std::vector<sicnu::explain::adapters::EvidenceLoadProblem> problems;
    m_evidence = sicnu::explain::adapters::ProvenanceFileEvidence::loadFromDirectory(
        QFileInfo( provenanceFilePath ).absolutePath().toStdString(), problems );
    for ( const auto &problem : problems )
        m_evidenceProblemCodes.append(
            QStringLiteral( "%1 [%2]: %3" )
                .arg( QString::fromStdString( problem.code ),
                      QString::fromStdString( problem.file ),
                      QString::fromStdString( problem.message ) ) );
    // The adapter itself is always non-null (possibly empty): an empty
    // adapter answers nullopt for every lookup, which the panel renders as
    // honest unknown — no status is ever invented from a broken record.
    m_runId = runId;
}

void StepExplanationSection::clearRunEvidence()
{
    m_evidence.reset();
    m_runId.clear();
    m_evidenceProblemCodes.clear();
}

QStringList StepExplanationSection::evidenceProblemCodes() const
{
    return m_evidenceProblemCodes;
}

} // namespace sicnu::app

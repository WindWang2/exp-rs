/***************************************************************************
 * step_explanation_section.h — why-this-step inspector section (RS14-15)
 *
 * Bridges the unified inspector (ADR 0172) to the explain pipeline: the
 * D17 designer canvas pushes its selected node into the SelectionContext,
 * the shell binds the live document identity, and this section projects
 * node + run onto an ExplanationRequest through the existing adapters.
 *
 * Run-scoped evidence follows the run lifecycle the shell already observes
 * (PipelineRunCoordinator): a finished run's provenance_<runId>.json is
 * loaded ONCE through ProvenanceFileEvidence and served to the panel as a
 * provider; a new run or a session boundary clears it. Loading is
 * fail-closed — a malformed/refused record becomes typed load problems that
 * render as honest unknown, never as a fabricated execution status.
 *
 * The section owns no explanation semantics: everything rendered comes from
 * StepExplanationBuilder + StepExplanationViewModel via the panel.
 ***************************************************************************/
#pragma once

#include "inspector_host.h"
#include "step_explanation_panel.h"

// The unique_ptr member needs the complete adapter type in every TU that
// destroys a section.
#include "explain/adapters/provenance_file_evidence.h"

#include <functional>
#include <memory>
#include <optional>

namespace sicnu::workflow
{
struct WorkflowDocument;
}

namespace sicnu::app
{

class StepExplanationSection : public InspectorSection
{
    Q_OBJECT
  public:
    /// The live IR 2.0 document (shell binds to the designer dock; nullopt
    /// when the dock does not exist yet — it is created on demand).
    using DocumentProvider = std::function<std::optional<sicnu::workflow::WorkflowDocument>()>;

    StepExplanationSection( StepExplanationPanel::KnowledgeProvider knowledge,
                            StepExplanationPanel::GuidanceProvider guidance,
                            DocumentProvider document, QWidget *parent = nullptr );

    QString sectionId() const override { return QStringLiteral( "explain" ); }
    QString title() const override { return tr( "步骤解释" ); }
    int order() const override { return 45; }
    bool supports( const SelectionContextSnapshot &snapshot ) const override;
    void populate( const SelectionContextSnapshot &snapshot ) override;

    /// Run lifecycle (shell binds to the designer dock's run signals): load
    /// the finished run's provenance record (its runId comes from the file
    /// name). An unreadable/unparsable record is recorded as typed problems
    /// and the evidence stays absent — unknown stays unknown. An empty path
    /// clears.
    void attachRunProvenance( const QString &provenanceFilePath );
    /// Story boundary / new run starting: drop the previous run's evidence.
    void clearRunEvidence();

    /// The run id evidence is currently scoped to (empty = none).
    QString currentRunId() const { return m_runId; }
    /// Typed load problems of the current evidence source (surfaced by the
    /// panel next to the execution section).
    QStringList evidenceProblemCodes() const;

    StepExplanationPanel *panel() const { return m_panel; }

  private:
    StepExplanationPanel *m_panel = nullptr;
    DocumentProvider m_document;
    std::unique_ptr<sicnu::explain::adapters::ProvenanceFileEvidence> m_evidence;
    QString m_runId;
    QStringList m_evidenceProblemCodes;
};

} // namespace sicnu::app

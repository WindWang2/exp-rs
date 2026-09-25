/***************************************************************************
 * step_explanation_panel.h — why-this-step rendering surface (RS14-15)
 *
 * A render-only panel over the deterministic explain pipeline: the builder
 * composes the StepExplanation from authoritative sources and the view
 * model projects it — the panel NEVER re-derives explanation semantics,
 * badges or provenance (ADR 0172 house rule: surfaces render, sources own).
 *
 * Sources arrive as providers (ProvenanceSection pattern) and are
 * re-resolved on every showStep(), so a project switch or an evidence swap
 * is reflected on the next render without panel-held copies. reset() is the
 * session/story boundary: it clears content and the stored request.
 *
 * Honesty contract: absent execution evidence renders an explicit
 * 执行情况未知 line (with the reason), never blank-as-zero and never a
 * synthesized status; builder problems and trust notes are rendered
 * verbatim, so a contradiction can never be overwritten by authored text.
 ***************************************************************************/
#pragma once

#include "explain/explanation_request.h"

#include <QWidget>

#include <functional>

class QLabel;

namespace sicnu::explain
{
struct StepExplanation;
class IOperatorKnowledge;
class IAuthoredGuidance;
class IExecutionEvidence;
} // namespace sicnu::explain

namespace sicnu::app
{

class StepExplanationPanel : public QWidget
{
    Q_OBJECT
  public:
    using KnowledgeProvider = std::function<const sicnu::explain::IOperatorKnowledge *()>;
    using GuidanceProvider = std::function<const sicnu::explain::IAuthoredGuidance *()>;
    using EvidenceProvider = std::function<const sicnu::explain::IExecutionEvidence *()>;

    /// Providers are invoked on every showStep() on the GUI thread and may
    /// return null — a null source renders an honest unavailable note, not
    /// an empty explanation.
    StepExplanationPanel( KnowledgeProvider knowledge, GuidanceProvider guidance,
                          EvidenceProvider evidence, QWidget *parent = nullptr );

    /// Build + render the explanation for @p request against the CURRENT
    /// providers. Synchronous: every source is a bounded in-memory lookup
    /// (the expensive provenance-directory load happens once in the owner of
    /// the evidence provider, never per render).
    /// @p evidenceProblemCodes: typed load problems of the evidence source
    /// itself (e.g. a refused provenance record) — rendered under the
    /// execution section so a tampered/unreadable record is visible as such.
    void showStep( const sicnu::explain::ExplanationRequest &request,
                   const QStringList &evidenceProblemCodes = QStringList() );

    /// Presentation-level note (provider unavailable, node not projectable).
    /// Clears any previous explanation — a note is never mixed with stale
    /// content.
    void showNote( const QString &text );

    /// Session/project boundary: clears content and the stored request.
    void reset();

    bool hasExplanation() const { return m_hasExplanation; }
    /// Bumped on every showStep/showNote/reset — surfaces the render count
    /// for lifecycle tests (no duplicate render per call).
    quint64 requestGeneration() const { return m_generation; }
    /// The request that produced the current content (valid iff
    /// hasExplanation()).
    const sicnu::explain::ExplanationRequest &lastRequest() const { return m_lastRequest; }
    /// Deterministic view-model markdown of the current explanation
    /// (empty when no explanation is shown).
    QString markdown() const { return m_markdown; }
    /// Plain-text projection of everything currently rendered (tests /
    /// offscreen assertions).
    QString renderedText() const { return m_plainText; }
    /// Non-empty when the last showStep() hard-failed (typed builder code).
    QString lastFailureCode() const { return m_failureCode; }

  signals:
    /// Emitted once per successful showStep() render.
    void explanationShown();

  private:
    void renderExplanation( const sicnu::explain::StepExplanation &explanation,
                            const sicnu::explain::ExplanationRequest &request,
                            const QStringList &evidenceProblemCodes );
    void setRendered( const QString &html, const QString &plainText );

    QLabel *m_body = nullptr;
    KnowledgeProvider m_knowledge;
    GuidanceProvider m_guidance;
    EvidenceProvider m_evidence;

    quint64 m_generation = 0;
    bool m_hasExplanation = false;
    QString m_markdown;
    QString m_plainText;
    QString m_failureCode;
    sicnu::explain::ExplanationRequest m_lastRequest;
};

} // namespace sicnu::app

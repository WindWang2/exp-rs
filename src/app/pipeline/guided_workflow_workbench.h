// src/app/pipeline/guided_workflow_workbench.h — LabSpec cards ⇄ topology, one source of truth (D17, ADR 0162)
#pragma once

//
// sicnu::app::workbench::GuidedWorkflowWidget (the file name deviates from
// the brief because src/app/widgets/guided_workflow_widget.h is the shipped
// global-namespace LabSpec renderer — DECISIONS D1).
//
// Two views over ONE document (the WorkflowDefinition 2.0 AST):
//   pi_card   : W -> ordered step cards (nodes flagged is_lab_step, topology order)
//   pi_canvas : W -> QGraphicsScene (the Package F canvas embed)
// Parameter edits from either side land in the document and emit
// parameterChanged exactly once per edit (ReentrancyGuard depth == 1).
//
// LabSpec 1.0 documents are loaded through the shipped loader
// (lab::loadLabSpecFile) and lifted: operator-bound steps become lab-step
// nodes; UI-verb/manual steps become guidance-only cards attached to the
// nearest preceding lab-step node.
//

#include <QWidget>
#include <QJsonObject>
#include <QString>

#include "workflow/workflow_ir_v2.h"

namespace sicnu::app::pipeline {
class PipelineCanvasWidget;
}

namespace sicnu::app::workbench {

enum class ViewMode
{
    CardWizard,    // beginner step cards (LabSpec 1.0)
    TopologyCanvas // expert node-graph canvas
};

struct LabStepCard
{
    int stepIndex = 0;
    QString title;
    QString guidanceText;
    QString targetNodeId;
    QJsonObject activeParameters;
    bool isCompleted = false;
};

class GuidedWorkflowWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit GuidedWorkflowWidget( QWidget *parent = nullptr );
    ~GuidedWorkflowWidget() override;

    /// Loads a LabSpec 1.0 file and lifts it into the underlying document.
    /// Fail-closed: false + m_loadError, no built-in fallback (ADR 0146).
    bool loadLabSpec( const QString &labSpecJsonPath );

    /// Alternative entry: adopt an already-parsed WorkflowDefinition whose
    /// nodes carry is_lab_step / lab_step_index metadata.
    bool setUnderlyingWorkflow( const sicnu::workflow::WorkflowDefinition &def );

    void setViewMode( ViewMode mode );
    ViewMode viewMode() const { return m_viewMode; }

    /// Writes one parameter into the document (from the card side or the
    /// canvas property panel) and emits parameterChanged exactly once.
    void syncParameterToTopology( const QString &nodeId, const QString &paramKey, const QJsonValue &value );

    const sicnu::workflow::WorkflowDefinition &underlyingWorkflow() const { return m_workflow; }
    QVector<LabStepCard> cards() const { return m_cards; }
    QString loadError() const { return m_loadError; }

    signals:
    void parameterChanged( const QString &nodeId, const QString &paramKey, const QJsonValue &value );
    void executionTriggered();
    void cardsRebuilt();

  private:
    void rebuildCardsFromWorkflow();
    void showEvent( QShowEvent *event ) override;

    sicnu::workflow::WorkflowDefinition m_workflow;
    QVector<LabStepCard> m_cards;
    ViewMode m_viewMode = ViewMode::CardWizard;
    QString m_loadError;
    int m_reentrancyDepth = 0;
    QWidget *m_cardHost = nullptr;
    pipeline::PipelineCanvasWidget *m_canvas = nullptr;
    bool m_uiBuilt = false;
};

} // namespace sicnu::app::workbench

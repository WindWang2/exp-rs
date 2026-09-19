// src/app/pipeline/guided_workflow_workbench.cpp — dual-view projection (D17)
#include "guided_workflow_workbench.h"

#include "widgets/lab_spec_loader.h"
#include "labspec_workflow_lift.h"
#include "pipeline_canvas_widget.h"
#include "workflow/workflow_dag_analyzer.h"

#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>

namespace sicnu::app::workbench {
namespace {
// Lab projections live in WorkflowDocument.metadata["labSteps"][nodeId].
constexpr const char *kLabStepsKey = "labSteps";
constexpr const char *kStepFlag = "is_lab_step";
constexpr const char *kStepTitle = "title";
constexpr const char *kStepGuidance = "guidance";

/// Depth-1 event guard: parameter mutation must never re-trigger the
/// projection that produced it. Nested (re-entrant) invocations are ignored.
class DepthGuard
{
  public:
    explicit DepthGuard( int &depth )
        : m_depth( depth )
        , m_active( m_depth == 0 )
    {
        if ( m_active )
            ++m_depth;
    }
    ~DepthGuard()
    {
        if ( m_active )
            --m_depth;
    }
    bool active() const { return m_active; }

  private:
    int &m_depth;
    bool m_active;
};

QJsonObject labStepEntry( const sicnu::workflow::WorkflowDocument &def, const QString &nodeId )
{
    return def.metadata.value( QLatin1String( kLabStepsKey ) ).toObject().value( nodeId ).toObject();
}
} // namespace

GuidedWorkflowWidget::GuidedWorkflowWidget( QWidget *parent )
    : QWidget( parent )
{
}

GuidedWorkflowWidget::~GuidedWorkflowWidget() = default;

bool GuidedWorkflowWidget::loadLabSpec( const QString &labSpecJsonPath )
{
    lab::LabSpecError error;
    const lab::LabSpec spec = lab::loadLabSpecFile( labSpecJsonPath, &error );
    if ( !error.reason.isEmpty() )
    {
        m_loadError = error.toString();
        return false;
    }
    // Shared LabSpec -> WorkflowDocument lift (also used by the E2E runner).
    return setUnderlyingWorkflow( sicnu::app::pipeline::liftLabSpecToWorkflow( spec ) );
}

bool GuidedWorkflowWidget::setUnderlyingWorkflow( const sicnu::workflow::WorkflowDocument &def )
{
    if ( !sicnu::workflow::WorkflowIR::validateSemantics( def ) )
    {
        m_loadError = QStringLiteral( "underlying workflow is not semantically valid" );
        return false;
    }
    const auto dag = sicnu::workflow::WorkflowDagAnalyzer::analyzeDag( def );
    if ( !dag.isAcyclic )
    {
        m_loadError = dag.errorMessage;
        return false; // cyclic cards would silently be empty — fail closed
    }
    m_workflow = def;
    rebuildCardsFromWorkflow();
    return true;
}

void GuidedWorkflowWidget::rebuildCardsFromWorkflow()
{
    // pi_card: topology-ordered projection over lab-flagged nodes.
    m_cards.clear();
    const auto tiers = sicnu::workflow::WorkflowDagAnalyzer::computeConcurrencyTiers( m_workflow );
    QVector<QString> ordered;
    for ( const auto &tier : tiers )
        ordered += tier.nodeIds;

    const QJsonObject labSteps = m_workflow.metadata.value( QLatin1String( kLabStepsKey ) ).toObject();
    int cardIndex = 0;
    for ( const QString &nodeId : ordered )
    {
        const QJsonObject entry = labSteps.value( nodeId ).toObject();
        if ( !entry.value( QLatin1String( kStepFlag ) ).toBool() )
            continue;
        const sicnu::workflow::NodeFact *node = m_workflow.findNode( nodeId );
        if ( !node )
            continue;
        LabStepCard card;
        card.stepIndex = cardIndex++;
        card.title = entry.value( QLatin1String( kStepTitle ) ).toString( node->displayName );
        card.guidanceText = entry.value( QLatin1String( kStepGuidance ) ).toString();
        card.targetNodeId = node->nodeId;
        card.activeParameters = node->parameters;
        m_cards.append( card );
    }
    emit cardsRebuilt();
}

void GuidedWorkflowWidget::syncParameterToTopology( const QString &nodeId, const QString &paramKey,
                                                    const QJsonValue &value )
{
    DepthGuard guard( m_reentrancyDepth );
    if ( !guard.active() )
        return; // echo of our own parameterChanged — never re-applied

    sicnu::workflow::NodeFact *node = nullptr;
    for ( sicnu::workflow::NodeFact &candidate : m_workflow.nodes )
        if ( candidate.nodeId == nodeId )
            node = &candidate;
    if ( !node )
        return;

    QJsonObject parameters = node->parameters;
    parameters.insert( paramKey, value );
    node->parameters = parameters;

    for ( LabStepCard &card : m_cards )
        if ( card.targetNodeId == nodeId )
            card.activeParameters = parameters;

    emit parameterChanged( nodeId, paramKey, value );
}

void GuidedWorkflowWidget::setViewMode( ViewMode mode )
{
    // #1097: leaving TopologyCanvas must export canvas edits back into
    // m_workflow before any subsequent loadWorkflow would discard them.
    if ( m_uiBuilt && m_viewMode == ViewMode::TopologyCanvas
         && mode != ViewMode::TopologyCanvas && m_canvas )
        m_workflow = m_canvas->exportWorkflow();
    m_viewMode = mode;
    if ( !m_uiBuilt )
        return; // headless use: state flip only, widgets built lazily
    m_cardHost->setVisible( mode == ViewMode::CardWizard );
    m_canvas->setVisible( mode == ViewMode::TopologyCanvas );
    if ( mode == ViewMode::TopologyCanvas )
        m_canvas->loadWorkflow( m_workflow );
}

void GuidedWorkflowWidget::showEvent( QShowEvent *event )
{
    QWidget::showEvent( event );
    if ( m_uiBuilt )
        return;
    auto *layout = new QVBoxLayout( this );
    m_cardHost = new QWidget( this );
    layout->addWidget( m_cardHost );
    m_canvas = new pipeline::PipelineCanvasWidget( this );
    layout->addWidget( m_canvas );
    m_uiBuilt = true;
    setViewMode( m_viewMode );
}

} // namespace sicnu::app::workbench

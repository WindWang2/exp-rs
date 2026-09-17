// src/workflow/workflow_repair_engine.cpp — closed rule table + adapter synthesis (D17)
#include "workflow/workflow_repair_engine.h"

#include <QHash>
#include <QSet>
#include <QStringList>

namespace sicnu::workflow {

QVector<ContractViolation> WorkflowRepairEngine::inspectContracts( const WorkflowDocument &def )
{
    return sicnu::workflow::inspectContracts( def );
}

namespace {

void pushCrsAction( QVector<RepairAction> &actions, const ContractViolation &violation, const QString &targetCrs )
{
    RepairAction action;
    action.ruleId = QStringLiteral( "rule_crs_auto_reproject" );
    action.insertOperatorId = QStringLiteral( "rs:reproject" );
    action.adapterParameters = QJsonObject{ { "target_crs", targetCrs }, { "resampling", QStringLiteral( "bilinear" ) } };
    action.targetEdgeId = violation.edgeId;
    actions.append( action );
}

void pushRadiometricActions( QVector<RepairAction> &actions, const ContractViolation &violation,
                             const QString &actualState, const QString &targetState )
{
    if ( actualState == QLatin1String( "DN" ) )
    {
        RepairAction calibration;
        calibration.ruleId = QStringLiteral( "rule_radiometric_calibration" );
        calibration.insertOperatorId = QStringLiteral( "rs:radiometric_calibration" );
        calibration.adapterParameters = QJsonObject{ { "method", QStringLiteral( "linear" ) } };
        calibration.targetEdgeId = violation.edgeId;
        actions.append( calibration );
    }
    if ( targetState == QLatin1String( "TOA" ) || targetState == QLatin1String( "BOA" ) )
    {
        RepairAction atmospheric;
        atmospheric.ruleId = QStringLiteral( "rule_atmospheric_correction" );
        atmospheric.insertOperatorId = QStringLiteral( "rs:atmospheric_correction" );
        atmospheric.adapterParameters = QJsonObject{ { "method", QStringLiteral( "dos1" ) } };
        atmospheric.targetEdgeId = violation.edgeId;
        actions.append( atmospheric );
    }
}

void pushResolutionAction( QVector<RepairAction> &actions, const ContractViolation &violation,
                           const QString &expectedSpec )
{
    // expectedSpecification is "XxY" (see contract_checker.cpp).
    const QStringList parts = expectedSpec.split( QLatin1Char( 'x' ) );
    if ( parts.size() != 2 )
        return;
    RepairAction action;
    action.ruleId = QStringLiteral( "rule_resolution_resample" );
    action.insertOperatorId = QStringLiteral( "rs:resample" );
    action.adapterParameters = QJsonObject{ { "target_resolution_x", parts[0].toDouble() },
                                            { "target_resolution_y", parts[1].toDouble() },
                                            { "resampling", QStringLiteral( "bilinear" ) } };
    action.targetEdgeId = violation.edgeId;
    actions.append( action );
}

void pushDataTypeAction( QVector<RepairAction> &actions, const ContractViolation &violation,
                         const QString &targetType )
{
    RepairAction action;
    action.ruleId = QStringLiteral( "rule_data_type_convert" );
    action.insertOperatorId = QStringLiteral( "rs:convert_dtype" );
    action.adapterParameters = QJsonObject{ { "target_data_type", targetType } };
    action.targetEdgeId = violation.edgeId;
    actions.append( action );
}

} // namespace

RepairPlan WorkflowRepairEngine::inferRepairs( const WorkflowDocument &def )
{
    RepairPlan plan;
    plan.violations = inspectContracts( def );
    plan.requiresRepair = !plan.violations.isEmpty();

    for ( const ContractViolation &violation : plan.violations )
    {
        const int actionsBefore = plan.suggestedActions.size();
        switch ( violation.mismatchType )
        {
            case ContractMismatchType::CrsMismatch:
                pushCrsAction( plan.suggestedActions, violation, violation.expectedSpecification );
                break;
            case ContractMismatchType::RadiometricStateMismatch:
                pushRadiometricActions( plan.suggestedActions, violation, violation.actualSpecification,
                                        violation.expectedSpecification );
                break;
            case ContractMismatchType::ResolutionMismatch:
                pushResolutionAction( plan.suggestedActions, violation, violation.expectedSpecification );
                break;
            case ContractMismatchType::DataTypeMismatch:
                pushDataTypeAction( plan.suggestedActions, violation, violation.expectedSpecification );
                break;
            case ContractMismatchType::DimensionMismatch:
                break; // no closed rule (not emitted today)
        }

        // Terminal port wiring of the inserted chain: first adapter's input
        // is named "input"; last adapter's output is named "output".
        for ( int i = actionsBefore; i < plan.suggestedActions.size(); ++i )
        {
            plan.suggestedActions[i].newSourcePort = QStringLiteral( "output" );
            plan.suggestedActions[i].newTargetPort = QStringLiteral( "input" );
        }
    }
    return plan;
}

WorkflowDocument WorkflowRepairEngine::applyRepairPlan( const WorkflowDocument &def, const RepairPlan &plan )
{
    WorkflowDocument repaired = def;

    // Group actions per violated edge, preserving plan order (calibration
    // before atmospheric correction, ...).
    QHash<QString, QVector<const RepairAction *>> byEdge;
    for ( const RepairAction &action : plan.suggestedActions )
        byEdge[action.targetEdgeId].append( &action );

    // Deterministic adapter ids: adapter_<edgeId>_<n>, n = insertion counter
    // per operator (collision suffix), then a global chain position.
    QHash<QPair<QString, QString>, int> idCounters;

    QVector<EdgeFact> newEdges;
    for ( const EdgeFact &edge : def.edges )
    {
        auto it = byEdge.find( edge.edgeId );
        if ( it == byEdge.end() || it->isEmpty() )
        {
            newEdges.append( edge );
            continue;
        }

        const NodeFact *source = def.findNode( edge.sourceNodeId );
        const NodeFact *target = def.findNode( edge.targetNodeId );
        if ( !source || !target )
        {
            newEdges.append( edge );
            continue;
        }

        QString upstreamNode = edge.sourceNodeId;
        QString upstreamPort = edge.sourcePortName;

        for ( int i = 0; i < it->size(); ++i )
        {
            const RepairAction *action = ( *it )[i];
            const auto idKey = qMakePair( edge.edgeId, action->insertOperatorId );
            const int suffix = idCounters.value( idKey, 0 );
            idCounters[idKey] = suffix + 1;
            const QString adapterId = suffix == 0
                ? QStringLiteral( "adapter_%1_%2" ).arg( edge.edgeId, action->insertOperatorId.mid( 3 ) )
                : QStringLiteral( "adapter_%1_%2_%3" ).arg( edge.edgeId, action->insertOperatorId.mid( 3 ) ).arg( suffix );

            NodeFact adapter;
            adapter.nodeId = adapterId;
            adapter.operatorId = action->insertOperatorId;
            adapter.displayName = action->insertOperatorId;
            adapter.parameters = action->adapterParameters;
            // Grid position: between source and target, slightly below.
            adapter.canvasPosition = QPointF( ( source->canvasPosition.x() + target->canvasPosition.x() ) / 2.0 + i * 90.0,
                                              ( source->canvasPosition.y() + target->canvasPosition.y() ) / 2.0 + 140.0 );

            PortFact in;
            in.portName = QStringLiteral( "input" );
            in.isRequired = true;
            PortFact out;
            out.portName = QStringLiteral( "output" );
            out.isRequired = false;
            // Inherit upstream facts on the input; the last adapter in the
            // chain satisfies the target requirement on its output.
            const NodeFact *upstream = repaired.findNode( upstreamNode );
            if ( upstream )
                for ( const PortFact &port : upstream->outputPorts )
                    if ( port.portName == upstreamPort )
                    {
                        in = port;
                        in.portName = QStringLiteral( "input" );
                        in.isRequired = true;
                        break;
                    }
            if ( i == it->size() - 1 && target )
                for ( const PortFact &port : target->inputPorts )
                    if ( port.portName == edge.targetPortName )
                    {
                        out = port;
                        out.portName = QStringLiteral( "output" );
                        out.isRequired = false;
                        break;
                    }

            adapter.inputPorts = { in };
            adapter.outputPorts = { out };
            repaired.nodes.append( adapter );

            newEdges.append( EdgeFact{ QStringLiteral( "re_%1_%2" ).arg( edge.edgeId ).arg( i ),
                                       upstreamNode, upstreamPort, adapterId, QStringLiteral( "input" ) } );
            upstreamNode = adapterId;
            upstreamPort = QStringLiteral( "output" );
        }

        newEdges.append( EdgeFact{ QStringLiteral( "re_%1_final" ).arg( edge.edgeId ),
                                   upstreamNode, upstreamPort, edge.targetNodeId, edge.targetPortName } );
    }
    repaired.edges = newEdges;
    return repaired;
}

} // namespace sicnu::workflow

// src/workflow/ir2_registry_node_executor.h — IR2 NodeExecutor ↔ RSOperatorRegistry (D18)
#pragma once

//
// Production bind for PipelineRunCoordinator (D17 injectable NodeExecutor).
// Prefer the existing RSOperatorRegistry over a parallel execution framework:
//   • Bound: node.operatorId resolves in the registry → execute with params
//     + inbound port artifacts (D-W6 multi-input port→param mapping), write
//     artifact under the run directory.
//   • Unbound: empty or unknown operatorId → typed refusal
//     (error prefix ir2.operator_unbound:…); never silent synthetic success.
//     Refusal is evaluated before port→param mapping.
// Synthetic remains available for hermetic D17 tests via makeSyntheticNodeExecutor.
//

#include "workflow/pipeline_run_coordinator.h"

namespace sicnu::workflow {

/// Stable error prefix for unbound IR2 nodes (tests / UI parse this).
inline constexpr const char *kIr2OperatorUnboundPrefix = "ir2.operator_unbound:";

enum class Ir2OperatorBinding
{
    Bound,           ///< RSOperatorRegistry::hasOperator(operatorId)
    UnboundEmpty,    ///< operatorId empty / whitespace
    UnboundUnknown   ///< non-empty id with no registry factory
};

Ir2OperatorBinding classifyIr2OperatorBinding( const QString &operatorId );

/// Build the typed unbound NodeExecutionResult (success=false, stable prefix).
/// Used by makeRegistryNodeExecutor before any port→param mapping; inline so
/// hermetic unit tests need not link RSOperatorRegistry.
inline NodeExecutionResult makeIr2UnboundRefusal( const NodeFact &node, Ir2OperatorBinding kind )
{
    NodeExecutionResult result;
    if ( kind == Ir2OperatorBinding::UnboundEmpty )
    {
        result.errorMessage =
            QStringLiteral( "%1 empty operatorId on node '%2'" )
                .arg( QLatin1String( kIr2OperatorUnboundPrefix ), node.nodeId );
    }
    else
    {
        result.errorMessage =
            QStringLiteral( "%1 no registry binding for '%2' (node '%3')" )
                .arg( QLatin1String( kIr2OperatorUnboundPrefix ), node.operatorId, node.nodeId );
    }
    return result;
}

/// True when classify returns Bound (registry factory present at call time).
bool isIr2OperatorBound( const QString &operatorId );

/// Registry-backed executor for the IR2 designer dock / production path.
/// Uses applyIr2InputPortMapping (see ir2_port_param_mapping.h) for multi-input.
NodeExecutor makeRegistryNodeExecutor();

/// Deterministic D17 synthetic executor (artifact bytes from node + inputs).
/// Used by PipelineRunCoordinator when no executor is set, and by hermetic tests.
NodeExecutor makeSyntheticNodeExecutor();

} // namespace sicnu::workflow

// src/workflow/ir2_registry_node_executor.h — IR2 NodeExecutor ↔ RSOperatorRegistry (D18)
#pragma once

//
// Production bind for PipelineRunCoordinator (D17 injectable NodeExecutor).
// Prefer the existing RSOperatorRegistry over a parallel execution framework:
//   • Bound: node.operatorId resolves in the registry → execute with params
//     + parent artifacts, write artifact under the run directory.
//   • Unbound: empty or unknown operatorId → typed refusal
//     (error prefix ir2.operator_unbound:…); never silent synthetic success.
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

/// True when classify returns Bound (registry factory present at call time).
bool isIr2OperatorBound( const QString &operatorId );

/// Registry-backed executor for the IR2 designer dock / production path.
NodeExecutor makeRegistryNodeExecutor();

/// Deterministic D17 synthetic executor (artifact bytes from node + inputs).
/// Used by PipelineRunCoordinator when no executor is set, and by hermetic tests.
NodeExecutor makeSyntheticNodeExecutor();

} // namespace sicnu::workflow

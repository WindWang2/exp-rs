// src/workflow/ir2_port_param_mapping.h — IR2 inbound port → RSOperator params (D18)
#pragma once

//
// Pure mapping used by makeRegistryNodeExecutor. Prefer explicit IR2 target
// port names as keys in inputArtifacts (PipelineRunCoordinator D-W6). Does not
// depend on RSOperatorRegistry so hermetic unit tests can exercise it.
//

#include "workflow/workflow_ir_v2.h"

#include <QHash>
#include <QString>

#include <json/json.h>

namespace sicnu::workflow {

/// Merge inbound artifacts into @p params for operator execute.
///
/// Conventions (prefer explicit names over order-only):
/// 1. Keys matching a declared inputPorts[].portName (or any key when the
///    node declares no ports) become params[portName] when that param is
///    unset / empty — never overwrite explicit node.parameters.
/// 2. Legacy keys that do not match declared ports (historically source
///    node ids) are zipped onto declared input ports in declaration order
///    after sorted key order — order-only fallback, documented limitation.
/// 3. params["ir2_input_artifacts"] always receives the resolved port→path map.
/// 4. If params["input"] is still empty: prefer port "input", else the first
///    declared input port with a binding, else the lexicographically first
///    bound port name.
void applyIr2InputPortMapping( const NodeFact &node,
                               const QHash<QString, QString> &inputArtifacts,
                               Json::Value &params );

} // namespace sicnu::workflow

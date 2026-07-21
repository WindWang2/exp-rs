// src/workflow/workflow_gate.h
#pragma once

#include "workflow_session.h"
#include "workflow_types.h"

#include <vector>

namespace sicnu::workflow {

/// Evaluate soft-gate predicates against session state.
/// Pure logic — no Qt, no I/O.
CanRunResult evaluateGates( const WorkflowSession &session, const std::vector<GateDef> &gates );

} // namespace sicnu::workflow

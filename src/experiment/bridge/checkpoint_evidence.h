// checkpoint_evidence.h — the shared stale-execution evidence probe for the
// bridge's reconcileStale consumers (monitor AND lab recorder). Lives here
// so both surfaces close crash-orphaned runs from the SAME checkpoint truth
// instead of one wiring real evidence and the other reporting blind.
#pragma once

#include "experiment/run_bridge.h"

#include <functional>
#include <optional>

namespace sicnu::workflow
{
class WorkflowRunCoordinator;
}

namespace sicnu::experiment
{

/// Builds the ExecutionEvidence lookup the bridge's reconcileStale expects:
/// flock probe for live cross-process owners, then checkpoint state (history
/// archive included). std::nullopt = cannot answer (report, never close).
std::function<std::optional<ExecutionEvidence>( const QString &executionRef )>
checkpointEvidenceLookup( workflow::WorkflowRunCoordinator &coordinator );

} // namespace sicnu::experiment

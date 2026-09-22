/***************************************************************************
 * mission_run_resolver.h — MissionRunRef → live execution state
 *
 * Track: glm53-mission-runtime-13 (Mission Runtime 13.0)
 *
 * Resolves a mission task's run authority against the EXISTING execution
 * authorities — TaskCenter (task_center ids), WorkflowRunCoordinator /
 * PipelineRunCoordinator (workflow_run / pipeline_run ids). No second
 * scheduler, no caching: every call queries the live authority.
 *
 * An id the authorities do not know (crashed process, stale project) is
 * reported as Unknown, which the mission layer treats as "not running" —
 * that is what keeps a reopened project from reporting a fake Running task.
 *
 * Qt Core only; compiled into the desktop shell (not into sicnu_agent).
 ***************************************************************************/
#pragma once

#include "app/workbench/mission_run_authority.h"

#include <QString>

#include "workflow/workflow_run.h"

namespace sicnu::app
{

/// Resolve one run reference against the execution authorities.
MissionRunStatus resolveMissionRunStatus( const MissionRunRef &ref );

/// #1168: the workflow-run-state → mission-liveness mapping, exported so
/// the gate test pins it (Interrupted must NOT read as Alive).
MissionRunStatus fromRunState( sicnu::workflow::WorkflowRunState state, const QString &detail );

} // namespace sicnu::app

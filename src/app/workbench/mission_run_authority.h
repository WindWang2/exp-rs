/***************************************************************************
 * mission_run_authority.h — mission task ↔ execution authority binding
 *
 * Track: glm53-mission-runtime-13 (Mission Runtime 13.0)
 *
 * A mission task never runs anything itself: it records WHO runs it
 * (MissionRunRef {kind,id}) and the state machine refuses to report Running
 * without a bound reference. This module answers the other half — is that
 * reference still a live execution? — through a resolver interface so the
 * mission layer can query the EXISTING authorities (TaskCenter,
 * WorkflowRunCoordinator, PipelineRunCoordinator) without owning a second
 * scheduler.
 *
 * The load-time reconciliation is the crash/reopen guarantee: a task left
 * Running whose execution no longer exists must never be reported as
 * Running again. Terminal executions map to their mission status; anything
 * unresolvable becomes Stale (fail closed, never optimistic).
 *
 * Qt Core only — see mission_stage.h.
 ***************************************************************************/
#pragma once

#include "app/workbench/mission_stage.h"

#include <QString>
#include <QStringList>

#include <functional>

namespace sicnu::app
{

enum class MissionRunLiveness
{
    Unknown,         ///< authority not consulted / cannot tell (fail closed)
    Alive,           ///< the run exists and is in flight
    TerminalSuccess, ///< the run completed successfully
    TerminalFailure, ///< the run finished with an error
    Canceled         ///< the run was canceled
};

struct MissionRunStatus
{
    MissionRunLiveness liveness = MissionRunLiveness::Unknown;
    QString stateKey; ///< the execution authority's own state token (bounded)
    QString detail;   ///< bounded hint for humans; never raw logs
};

/// Resolves one run reference against the execution authorities. Supplied by
/// the host (desktop: TaskCenter / run coordinators; headless default:
/// resolveMissionRunUnresolved).
using MissionRunStatusResolver = std::function<MissionRunStatus( const MissionRunRef & )>;

/// Default resolver: the authority was not consulted. Callers must treat
/// Unknown as "cannot verify", never as "running".
MissionRunStatus resolveMissionRunUnresolved( const MissionRunRef & );

struct MissionRunReconciliation
{
    int leftRunning = 0;      ///< verified in flight — stays Running
    int succeededFromRun = 0; ///< terminal success mapped to Succeeded
    int failedFromRun = 0;    ///< terminal failure mapped to Failed
    int canceledFromRun = 0;  ///< canceled execution mapped to Canceled
    int staleFromRun = 0;     ///< missing/unresolvable run → Stale (no fake Running)
    QStringList details;      ///< "taskId:reason" audit lines
};

/// Reconcile every Running task against its run authority. Mutates only
/// through MissionTimeline::transition (fail-closed, audited). A null run
/// reference is `stale_run_reference`; an unresolvable one is
/// `run_authority_unresolved`; both end Stale.
MissionRunReconciliation reconcileRunAuthority( MissionTimeline &timeline,
                                                const MissionRunStatusResolver &resolver,
                                                const QString &iso );

} // namespace sicnu::app

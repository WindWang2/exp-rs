/***************************************************************************
 * shutdown_policy.h — Professional Workbench 7.0 shutdown/switch policy
 *
 * Pure decision layer for "is it safe to quit or switch projects": it
 * projects every registered bench's lifecycle contract (IWorkbench
 * isDirty / hasInFlightCompute, goal §A) plus the TaskCenter non-terminal
 * task count into a ShutdownPlan the shell confirms with the user.
 *
 * The policy knows nothing about widgets or TaskCenter internals — the
 * shell (QgisDesktopWindow::confirmWorkbenchShutdown) supplies the live
 * values, shows the dialogs and routes cancellation through each bench's
 * own requestCancel() seam. Nothing here waits on task termination: quit
 * stays bounded, and the user explicitly chose to cancel, so nothing is
 * silently dropped.
 ***************************************************************************/
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::app
{

class WorkbenchHost;

/// Lifecycle facts of one registered bench at shutdown/switch time.
struct WorkbenchShutdownFacts
{
    QString benchId;
    QString title;
    bool dirty = false;     ///< unsaved interaction state (samples, GCPs, edits)
    bool inFlight = false;  ///< TaskCenter-tracked compute running in the bench
};

/// What the shell must confirm before quitting / switching projects.
struct ShutdownPlan
{
    QStringList dirtyBenches;    ///< bench titles with unsaved state
    QStringList inFlightBenches; ///< bench titles with running compute
    int runningTaskCount = 0;    ///< TaskCenter non-terminal tasks (any surface)

    bool needsConfirmation() const
    {
        return !dirtyBenches.isEmpty() || !inFlightBenches.isEmpty() || runningTaskCount > 0;
    }
    bool isEmpty() const { return !needsConfirmation(); }
};

/// Pure projection: benches + background task count → plan. Deterministic,
/// ordering follows the input order (registration order at the call sites).
ShutdownPlan planWorkbenchShutdown( const QVector<WorkbenchShutdownFacts> &benchFacts,
                                    int runningTaskCount );

/// Reads every registered bench through the IWorkbench contract. Never
/// mutates a bench.
QVector<WorkbenchShutdownFacts> collectWorkbenchShutdownFacts( const WorkbenchHost *host );

/// Calls requestCancel() on every bench reporting in-flight compute.
/// Returns how many benches accepted the cancellation request (best effort —
/// a bench may legitimately refuse when its work already ended).
int cancelInFlightBenches( WorkbenchHost *host );

/// Asks every dirty bench to go away through its own requestClose() —
/// external session windows run their own save/discard confirmation there.
/// Returns false as soon as one bench refuses; the shell must then abort the
/// quit/switch and leave everything as it was.
bool requestCloseDirtyBenches( WorkbenchHost *host );

} // namespace sicnu::app

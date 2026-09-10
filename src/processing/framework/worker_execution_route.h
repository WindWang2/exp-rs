// worker_execution_route.h — production wiring for isolated worker
// execution (Execution Plane 7.0, package A).
//
// This module is the SELECTION SEAM between TaskCenter's dispatch and the
// executor side: it decides WHETHER a task executes in an isolated
// `sicnu_worker` process (via the shared LocalWorkerPool) and provides the
// per-job executor that runs it there. It is NOT a second scheduler — every
// job still flows through TaskCenter -> JobEngine; the pool only decides
// HOW (in-process thread vs isolated process) an admitted job executes.
//
// Modes (env SICNU_WORKER_EXECUTION, default "off" — master behavior):
//   off     — never route; the pool is never started.
//   auto    — route only operators whose AlgorithmDescriptor declares
//             executionPreference "isolated" (opt-in per operator) AND
//             supportsCancellation (an operator that cannot cooperate must
//             not own an un-cancellable process).
//   require — route everything routable; a pool that cannot start fails the
//             task with a typed error (fail-closed: no silent in-process
//             fallback, the caller must never believe isolation happened
//             when it did not).
//
// Bounded admission: isolated jobs consume a dedicated slot count
// (SICNU_WORKER_MAX_CONCURRENT, default 2) so worker-bound jobs cannot
// starve JobEngine's in-process capacity; the gate only DELAYS (the task
// stays queued, like the RAM/RSS gates).
#pragma once

#include "local_worker_pool.h"

#include <QString>

#include <functional>

namespace sicnu::jobs
{
struct JobRequest;
}
namespace sicnu::operators
{
class RSOperatorContext;
}

namespace sicnu::processing
{

enum class WorkerExecutionMode
{
    Off = 0,
    Auto,
    Require,
};

inline const char *workerExecutionModeName( WorkerExecutionMode mode )
{
    switch ( mode )
    {
        case WorkerExecutionMode::Auto:
            return "auto";
        case WorkerExecutionMode::Require:
            return "require";
        case WorkerExecutionMode::Off:
        default:
            return "off";
    }
}

struct WorkerExecutionConfig
{
    WorkerExecutionMode mode = WorkerExecutionMode::Off;
    /// sicnu_worker binary path; empty = resolve from the app dir / PATH.
    QString workerProgram;
    /// Hard bound of concurrent isolated jobs admitted by TaskCenter.
    int maxConcurrentIsolatedJobs = 2;
    LocalWorkerPoolConfig pool;
};

/// Reads SICNU_WORKER_EXECUTION / SICNU_WORKER_PROGRAM /
/// SICNU_WORKER_MAX_CONCURRENT over the defaults (fail-conservative: an
/// unparsable value falls back to the default, never to a risky mode).
WorkerExecutionConfig workerExecutionConfigFromEnvironment();

/// Lazily starts the shared pool (idempotent; a started pool is kept).
/// Returns false when the worker program cannot be started (@p errorOut).
/// Must be called WITHOUT TaskCenter's mutex held (process spawn + 30s
/// handshake). May be called from any thread; the pool's per-worker thread
/// affinity (7.0) lets concurrent JobEngine threads drive their own workers.
bool ensureSharedWorkerPoolStarted( const WorkerExecutionConfig &config, QString *errorOut = nullptr );
/// The shared pool, or nullptr when not started. Callers must treat nullptr
/// as "isolated execution unavailable" — a routed job fails typed, it never
/// falls back in-process.
LocalWorkerPool *sharedWorkerPool();
/// Quiesces the shared pool (shutdown frames → grace → kill). Idempotent;
/// called from TaskCenter::shutdown AFTER the engine joined.
void shutdownSharedWorkerPool();

WorkerExecutionMode currentWorkerExecutionMode();

/// Overrides the CONFIGURED mode (host/test hook). The mode governs the
/// route decision even before (or without) a started pool: "require" must
/// fail tasks closed when the pool is unavailable, never silently route
/// them in-process.
void setWorkerExecutionMode( WorkerExecutionMode mode );

/// The selection predicate evaluated at TaskCenter staging. "require" routes
/// every operator the worker registry can execute; "auto" consults the
/// AtomicAlgorithmRegistry descriptor (executionPreference == "isolated" &&
/// supportsCancellation); "off" never.
bool shouldRunIsolated( const QString &algorithmId );

/// Executor signature shared with TaskCenter's JobExecutor (identical
/// instantiation; declared here to avoid a task_center.h include cycle).
using WorkerJobExecutor =
    std::function<Json::Value( const sicnu::jobs::JobRequest &, sicnu::operators::RSOperatorContext & )>;

/// Builds the per-job executor that runs @p algorithmId on the shared pool,
/// bridging ctx cancellation (cooperative → cancel frame → escalate) and
/// progress (worker frames → ctx progress → engine record). Failures throw
/// the pool's typed std::runtime_error family; the engine maps them to
/// Failed (or Cancelled when the cancel flag is armed) — never a fallback.
WorkerJobExecutor makeIsolatedWorkerExecutor( const QString &algorithmId );

/// Bounded isolated-slot admission: TaskCenter holds the launch in
/// WaitingResource while the configured number of isolated jobs are active
/// (SICNU_WORKER_MAX_CONCURRENT, default 2). The setter exists for tests
/// and hosts; the limit applies on the next admission pass.
void setIsolatedJobLimit( int maxConcurrent );
int isolatedJobLimit();

} // namespace sicnu::processing

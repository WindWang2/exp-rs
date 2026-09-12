// local_worker_pool.h — bounded warm pool of isolated operator workers
// (Reliability 4.0, Milestone H).
//
// Builds on the existing isolated-worker architecture (worker_protocol v1 +
// sicnu_worker binary + the one-shot runInLocalWorker host): instead of one
// throwaway QProcess per job, a bounded set of warm worker processes is
// reused across jobs. This is an executor-side resource, NOT a second job
// scheduler — callers are still expected to run through TaskCenter/JobEngine;
// this pool only decides HOW an operator executes (in-process thread vs
// isolated process), opt-in via LocalWorkerPoolConfig.
//
// Safety rules (fail-conservative over hit rate):
//   - a worker is used only after a verified ready/v1 handshake (spawn-time
//     health check); a worker that ever fails verification is discarded;
//   - per-job timeout (default 30 min) and cancel escalation (terminate →
//     kill after grace) mirror runInLocalWorker's contract exactly;
//   - a crashed/timed-out/cancelled worker is never returned to the pool;
//     the pool transparently spawns a replacement for FUTURE jobs (the
//     failed job itself reports its typed error — no silent automatic retry,
//     because operators may have produced partial side effects);
//   - lifetime recycling: a worker that served maxJobsPerWorker jobs or sat
//     idle longer than idleRecycleAfter is retired at its next release
//     (memory recycling: the OS reclaims any leaked operator memory);
//   - shutdown() quiesces: every worker receives a shutdown frame, gets a
//     grace window, then a kill fallback; post-shutdown runs refuse.
#pragma once

#include "local_worker_host.h"
#include "runtime/observability/execution_telemetry.h"
#include "worker_process_guard.h"
#include "worker_process_io.h"

#include <QProcess>
#include <QString>
#include <QStringList>
#include <QThread>

#include <json/json.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sicnu::processing
{

struct LocalWorkerPoolConfig
{
    QString workerProgram; ///< sicnu_worker binary path (required)
    int maxWorkers = 2;    ///< hard bound; callers block when all are busy
    int minWarmWorkers = 1;///< pre-spawned at start()
    std::chrono::milliseconds jobTimeout{ 30 * 60 * 1000 };
    std::chrono::milliseconds cancelGraceMs{ 3000 };
    std::chrono::milliseconds idleRecycleAfter{ 10 * 60 * 1000 };
    qint64 maxJobsPerWorker = 64;
};

struct WorkerPoolHealthSnapshot
{
    int configuredMax = 0;
    int aliveWorkers = 0;
    int idleWorkers = 0;
    qint64 totalRuns = 0;
    qint64 totalCrashes = 0;
    qint64 totalTimeouts = 0;
    qint64 totalCancels = 0;
    qint64 totalRecycles = 0;
    bool healthy() const { return configuredMax <= 0 || aliveWorkers <= configuredMax; }
};

/// Runs one operator job on a pooled isolated worker; throws the same typed
/// std::runtime_error family as runInLocalWorker
/// ("worker protocol:" / "worker error:" / "worker crashed:" /
///  "worker timeout:" / "worker cancelled" / "worker pool:").
Json::Value runInPooledWorker( class LocalWorkerPool &pool, const std::string &algorithmId,
                               const Json::Value &params,
                               const std::function<bool()> &isCancelled = {} );

class LocalWorkerPool
{
  public:
    LocalWorkerPool() = default;
    ~LocalWorkerPool();
    LocalWorkerPool( const LocalWorkerPool & ) = delete;
    LocalWorkerPool &operator=( const LocalWorkerPool & ) = delete;

    /// Starts (or restarts with a new config) the pool and pre-warms
    /// minWarmWorkers. Returns false when the worker program cannot be
    /// started (@p errorOut explains).
    bool start( const LocalWorkerPoolConfig &config, QString *errorOut = nullptr );
    /// Quiesces: shutdown frames → grace → kill fallback. Idempotent.
    void shutdown();
    bool isRunning() const;

    /// Runs one job (blocks the caller, like runInLocalWorker). Throws typed
    /// errors; never returns a stale/wrong-result silently. @p onProgress
    /// (7.0) receives the worker's progress frames; @p report (7.0) collects
    /// handshake/capabilities/cancel-ack/diagnostics for the run.
    ///
    /// Thread model (7.0): the pool may be driven concurrently from several
    /// threads (e.g. JobEngine workers). Each Worker is QProcess-affine to
    /// the thread that spawned it and is only ever acquired/released by that
    /// thread; the pool's global cap bounds the total. A warm worker parked
    /// by a thread that never returns is recycled by the idle budget or
    /// force-retired when its slot is needed.
    Json::Value run( const std::string &algorithmId, const Json::Value &params,
                     const std::function<bool()> &isCancelled = {},
                     const std::function<void( double, const std::string & )> &onProgress = {},
                     LocalWorkerRunReport *report = nullptr );

    WorkerPoolHealthSnapshot health() const;

  private:
    struct Worker
    {
        std::unique_ptr<QProcess> process;
        qint64 startedMs = 0;
        qint64 lastUsedMs = 0;
        qint64 jobsDone = 0;
        // QProcess affinity: the thread that spawned the process and the
        // only thread that may drive it (acquire/run/release).
        Qt::HANDLE ownerThread = nullptr;
        // 7.0: handshake timing and advertised capabilities; a per-worker
        // bounded stderr tail feeding crash/timeout error reports.
        qint64 handshakeMs = 0;
        QStringList capabilities;
        WorkerDiagnosticsRing diagnostics;
        // 8.0 WP-C: OS-level tree containment (POSIX process group via
        // setsid / Windows kill-on-close Job Object). Lifetime = the
        // worker's: on Windows the job handle closes with the guard, killing
        // any survivors even if the host dies mid-job.
        WorkerProcessGuard guard;
    };

    enum class Outcome
    {
        Result,
        Crashed,
        TimedOut,
        Cancelled,
        ProtocolError,
    };

    /// Pops one drivable idle worker for @p self (QProcess-affine, lifetime
    /// budget left), or nullptr. m_mutex HELD. Workers to retire (lifetime
    /// exhausted, dead process, or the force-retired oldest idle worker under
    /// slot pressure) are appended to @a teardownOut with their m_alive
    /// accounting already applied; the CALLER tears them ALL down after
    /// releasing the mutex — process waits must never happen under m_mutex
    /// (P1). A vector, not a single unique_ptr: two exhausted idle workers
    /// in one scan must not overwrite each other (#932).
    std::unique_ptr<Worker> takeIdleWorkerLocked( Qt::HANDLE self,
                                                  std::vector<std::unique_ptr<Worker>> &teardownOut );
    /// Spawns + handshakes a worker for the calling thread (worst case ~30s).
    /// NO pool lock held. m_config is read without the mutex: the shared pool
    /// is never restarted while running; direct-use pools must not call
    /// start() concurrently with run().
    std::unique_ptr<Worker> spawnWorker();
    /// Tears down a worker already removed from the pool's accounting. NO
    /// lock held, bounded waits (~2s): idle workers have no in-flight job.
    void teardownWorker( std::unique_ptr<Worker> worker );
    /// Runs one job on @a worker; never returns the worker to the pool on a
    /// non-Result outcome.
    Outcome runOnWorker( Worker &worker, const std::string &jobId,
                         const std::string &algorithmId, const Json::Value &params,
                         const std::function<bool()> &isCancelled,
                         const std::function<void( double, const std::string & )> &onProgress,
                         Json::Value *payload, std::string *errorMessage,
                         bool *cancelAcked );
    /// Retires (shutdown frame → grace → kill) and discards @a worker.
    void retireWorker( std::unique_ptr<Worker> worker );
    /// Returns a healthy worker to the idle pool (or retires it when its
    /// lifetime budget is exhausted).
    void releaseWorker( std::unique_ptr<Worker> worker );

    LocalWorkerPoolConfig m_config;
    bool m_running = false;

    mutable std::mutex m_mutex;
    std::condition_variable m_idleChanged;
    std::deque<std::unique_ptr<Worker>> m_idle;
    int m_alive = 0;

    // Health counters (guarded by m_mutex; surfaced via health()).
    qint64 m_totalRuns = 0;
    qint64 m_totalCrashes = 0;
    qint64 m_totalTimeouts = 0;
    qint64 m_totalCancels = 0;
    qint64 m_totalRecycles = 0;
    /// In-flight run() invocations; destruction waits (bounded by the job
    /// timeout) for this to drain so a concurrent run can never touch
    /// already-destroyed members (review P1).
    int m_activeRuns = 0;
    bool m_destroying = false;
};

} // namespace sicnu::processing

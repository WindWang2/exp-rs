// JobEngine — process-local worker pool for RSOperator / pluggable jobs (Qt-free)
#pragma once

#include "job_types.h"

#include "operators/framework/rs_operator_context.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sicnu::jobs {

/**
 * Process-local job scheduler.
 *
 * Default path: algorithmId → RSOperatorRegistry::create().
 * Pluggable path: registerExecutor(prefix, …) matches algorithmId by prefix
 * (longest-prefix wins when multiple register; first registered with matching
 * starts_with is used — prefer unique prefixes like "module:", "processing:").
 * Per-job path: submit(req, executor) runs a one-shot callable (e.g. GDAL lambda).
 *
 * Resolution order in runOperatorJob (ADR 0062):
 *   1. per-job executor (submit(req, executor, …))
 *   2. longest-matching prefix executor (registerExecutor)
 *   3. RSOperatorRegistry::create(algorithmId)   — native rs:/opencv: operators
 *   4. fallback executor (setFallbackExecutor)   — e.g. AtomicAlgorithmRegistry,
 *      so provider algorithms (gdal:/otb:/native:) become executable from jobs.
 *
 * Exclusive policy ("drain then exclusive"):
 * - When an exclusive job is queued, no new non-exclusive jobs are started
 *   so in-flight work can finish (drain).
 * - When the pool is idle, the exclusive job runs alone (m_exclusiveRunning);
 *   no other job starts until it finishes.
 * - Multiple exclusive jobs in the queue run one after another after drain.
 */
class JobEngine
{
  public:
    using Listener = std::function<void( const JobRecord & )>;

    /** Body for pluggable / one-shot jobs. May throw RSOperatorError or std::exception. */
    using JobExecutor = std::function<Json::Value( const JobRequest &req,
                                                   sicnu::operators::RSOperatorContext &ctx )>;

    /**
     * Optional: invoked when cancel() is requested while the job is Running.
     * Hooks are invoked WITHOUT the engine lock held, so they may re-enter
     * JobEngine (e.g. snapshot()) without deadlocking.
     */
    using CancelHook = std::function<void()>;

    static JobEngine &instance();

    /// Default pool size policy (#661): one worker per hardware core minus
    /// the core reserved for UI (ADR 0002), floored at kMinWorkers.
    /// hardware_concurrency() == 0 (unknown) degrades to kMinWorkers.
    static int defaultWorkerCount();

    /// Injectable core of the same policy, so tests pin the property without
    /// depending on the host's core count.
    static int defaultWorkerCount( unsigned hardwareCores );

    /// Lower bound for pool sizes: keeps a worker from starving the very
    /// pool it runs on and preserves the historical floor.
    static constexpr int kMinWorkers = 2;
    /// Upper bound for explicit overrides, guarding against misconfiguration
    /// while still honoring real workstation sizes.
    static constexpr int kMaxWorkersOverride = 64;

    /// Explicit override of the pool size. Honored as-is within
    /// [kMinWorkers, kMaxWorkersOverride]; values outside are clamped.
    void setMaxWorkers( int n );
    int maxWorkers() const;

    /**
     * Stop and join all worker threads. Safe to call multiple times.
     * STICKY in production: after shutdown() the engine is terminated —
     * submit() returns a Cancelled record and never respawns workers
     * (#684). Only shutdownForTests() resets the terminated state.
     */
    void shutdown();

    /** Submit RSOperator (or prefix-registered executor) job. */
    std::string submit( JobRequest req );

    /**
     * Submit a one-shot callable job (preferred for dialog GDAL lambdas and
     * module QgsTask bodies that cannot be pure RSOperators yet).
     * algorithmId should use a stable id for the job panel (e.g. "callable:gdal",
     * "module:classify:apply").
     */
    std::string submit( JobRequest req, JobExecutor executor, CancelHook onCancel = {} );

    /**
     * Register a prefix executor. algorithmId that starts with \a prefix uses
     * this executor instead of RSOperatorRegistry (unless a per-job executor
     * was supplied). Empty prefix is ignored.
     */
    void registerExecutor( const std::string &prefix, JobExecutor executor );

    /** Remove all prefix executors (tests). */
    void clearExecutors();

    /**
     * Install a catch-all fallback executor, tried after the prefix executor and
     * RSOperatorRegistry both miss (ADR 0062). Production wires this to
     * AtomicAlgorithmRegistry::findAdapter so provider algorithms (gdal:/otb:/
     * native:) that the Agent already sees in the exported tool catalog become
     * executable when submitted as jobs. Pass an empty function to clear.
     */
    void setFallbackExecutor( JobExecutor executor );

    /**
     * Cancel a job. For a Queued job the record is cancelled synchronously.
     * For a Running job this returns immediately after setting the cancel flag
     * and (if any) copying out the per-job CancelHook; the terminal state
     * arrives asynchronously via the listener / snapshot once the operator
     * observes cancellation and exits.
     */
    bool cancel( const std::string &jobId );
    std::optional<JobRecord> snapshot( const std::string &jobId ) const;
    std::vector<JobRecord> list() const;

    /**
     * Record retention (ADR 0052): prune terminal records so m_jobs stays
     * bounded for the process lifetime. Queued/running records are never
     * touched, and a record is only pruned after its terminal state was set
     * under m_mutex (the final listener notification uses a pre-copied
     * record, so an in-flight notify still lands).
     *
     * Remove the oldest terminal records beyond \a maxKeep ("oldest" = the
     * record's own timestamps: finishedAtMs, then createdAtMs, then id) and
     * return the count removed. \a maxKeep 0 removes all terminal records.
     */
    std::size_t pruneCompleted( std::size_t maxKeep );

    /**
     * Remove the terminal records identified by \a jobIds (unknown ids and
     * non-terminal records are ignored). Returns the count removed.
     * Used by TaskCenter::clearCompletedTasks to drop exactly the records of
     * the tasks it cleared without touching untracked engine jobs.
     */
    std::size_t removeCompleted( const std::vector<std::string> &jobIds );

    /** Remove ALL terminal records (equivalent to pruneCompleted(0)). */
    void clearCompleted();

    /**
     * Install the single listener slot (REPLACING any previous listener).
     * TaskCenter owns this slot in production (ADR 0051); tests may install
     * their own listener instead, and must re-install TaskCenter's listener
     * (implicitly done on TaskCenter's next submit) if they need both.
     * The listener may be invoked from worker threads — UI must marshal.
     */
    void setListener( Listener listener );

    // Test helpers
    void waitUntilIdleForTests( int timeoutMs = 10000 );
    void shutdownForTests(); // join workers; engine remains reusable

    /// True once production shutdown() ran (sticky; cleared only by
    /// shutdownForTests). Used by tests to assert no-resurrect semantics.
    bool isTerminated() const;

  private:
    JobEngine();
    ~JobEngine();
    JobEngine( const JobEngine & ) = delete;
    JobEngine &operator=( const JobEngine & ) = delete;

    void workerLoop( uint64_t gen );
    void ensureWorkersLocked(); // requires m_mutex
    std::optional<std::string> tryPickJobLocked(); // requires m_mutex
    void appendLog( JobRecord &rec, JobLogLevel level, const std::string &text );
    void notify( const JobRecord &rec );
    void runOperatorJob( const std::string &jobId );
    void finishJobLocked( JobRecord &rec, bool wasExclusive ); // requires m_mutex
    JobExecutor findPrefixExecutorLocked( const std::string &algorithmId ) const;

    struct JobBody
    {
      JobExecutor executor;
      CancelHook onCancel;
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_queue;
    std::unordered_map<std::string, JobRecord> m_jobs;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> m_cancelFlags;
    /// Per-job index of the first log line not yet shipped in a delta notify
    /// (#638); cleaned up with the job's other transient state on finish.
    std::unordered_map<std::string, std::size_t> m_deltaLogCursor;
    std::unordered_map<std::string, JobBody> m_jobBodies; // per-job one-shot
    std::vector<std::pair<std::string, JobExecutor>> m_prefixExecutors;
    JobExecutor m_fallbackExecutor; // catch-all, tried after RSOperatorRegistry
    std::vector<std::thread> m_workers;
    Listener m_listener;
    int m_maxWorkers = defaultWorkerCount();
    int m_running = 0;
    bool m_exclusiveRunning = false;
    bool m_shuttingDown = false;
    /// Sticky production termination (#684): set by shutdown(), never cleared
    /// except by shutdownForTests(). submit()/ensureWorkersLocked() refuse
    /// while set so a post-shutdown submit cannot resurrect worker threads.
    bool m_terminated = false;
    uint64_t m_generation = 0;
    std::atomic<bool> m_stop{false};
    std::atomic<uint64_t> m_nextId{1};
};

} // namespace sicnu::jobs

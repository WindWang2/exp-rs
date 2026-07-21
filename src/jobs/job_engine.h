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

    /** Optional: invoked when cancel() is requested while the job is Running. */
    using CancelHook = std::function<void()>;

    static JobEngine &instance();

    void setMaxWorkers( int n ); // clamp 2..4
    int maxWorkers() const;

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

    bool cancel( const std::string &jobId );
    std::optional<JobRecord> snapshot( const std::string &jobId ) const;
    std::vector<JobRecord> list() const;

    // Listener may be invoked from worker threads — UI must marshal.
    void setListener( Listener listener );

    // Test helpers
    void waitUntilIdleForTests( int timeoutMs = 10000 );
    void shutdownForTests(); // join workers; engine remains reusable

  private:
    JobEngine();
    ~JobEngine();
    JobEngine( const JobEngine & ) = delete;
    JobEngine &operator=( const JobEngine & ) = delete;

    void workerLoop();
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
    std::unordered_map<std::string, JobBody> m_jobBodies; // per-job one-shot
    std::vector<std::pair<std::string, JobExecutor>> m_prefixExecutors;
    std::vector<std::thread> m_workers;
    Listener m_listener;
    int m_maxWorkers = 3;
    int m_running = 0;
    bool m_exclusiveRunning = false;
    std::atomic<bool> m_stop{false};
    std::atomic<uint64_t> m_nextId{1};
};

} // namespace sicnu::jobs

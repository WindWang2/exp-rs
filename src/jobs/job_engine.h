// JobEngine — process-local worker pool for RSOperator jobs (Qt-free)
#pragma once

#include "job_types.h"

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
 * Process-local job scheduler over RSOperatorRegistry.
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

    static JobEngine &instance();

    void setMaxWorkers( int n ); // clamp 2..4
    int maxWorkers() const;

    std::string submit( JobRequest req );
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

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_queue;
    std::unordered_map<std::string, JobRecord> m_jobs;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> m_cancelFlags;
    std::vector<std::thread> m_workers;
    Listener m_listener;
    int m_maxWorkers = 3;
    int m_running = 0;
    bool m_exclusiveRunning = false;
    std::atomic<bool> m_stop{false};
    std::atomic<uint64_t> m_nextId{1};
};

} // namespace sicnu::jobs

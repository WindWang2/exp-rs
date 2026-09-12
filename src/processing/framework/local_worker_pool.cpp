// local_worker_pool.cpp — see local_worker_pool.h for the contract.
#include "local_worker_pool.h"

#include "runtime/worker/worker_protocol.h"

#include <QDateTime>
#include <QThread>

#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <thread>

namespace sicnu::processing
{
namespace
{
namespace telemetry = sicnu::runtime::observability;

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

// Cancellation verdict for error frames lives in the shared protocol header
// (sicnu::runtime::worker::frameErrorMeansCancelled) so the pool and the
// one-shot host cannot drift; it type-checks the legacy "message" fallback
// there.

/// Bounded single-line worker diagnostics for typed error reports.
std::string diagnosticsSuffix( const WorkerDiagnosticsRing &diagnostics )
{
    const QString tail = diagnostics.tail();
    return tail.isEmpty() ? std::string()
                          : " [worker stderr: " + tail.toStdString() + "]";
}

/// 8.0 WP-C hang window (default 0 = off): when enabled, a worker that
/// produces NO frames (progress, ack, result or heartbeat) for this long
/// while a job runs is treated as hung and escalated. Off by default so a
/// legitimately silent long operator is never killed without opt-in.
std::chrono::milliseconds hangDetectionTimeout()
{
    static const std::chrono::milliseconds timeout = [] {
        const QString raw = qEnvironmentVariable( "SICNU_WORKER_HANG_TIMEOUT_MS" );
        bool ok = false;
        const qint64 ms = raw.toLongLong( &ok );
        return ( ok && ms > 0 ) ? std::chrono::milliseconds( ms )
                                : std::chrono::milliseconds( 0 );
    }();
    return timeout;
}
} // namespace

LocalWorkerPool::~LocalWorkerPool()
{
    // Destroying a pool with in-flight runs would hand them already-destroyed
    // members: mark destroying, then wait (bounded by the job timeout) for
    // the in-flight counter to drain before the final quiesce.
    {
        std::unique_lock<std::mutex> lock( m_mutex );
        m_destroying = true;
        const bool drained = m_idleChanged.wait_for( lock, m_config.jobTimeout,
            [ this ]() { return m_activeRuns == 0; } );
        if ( !drained )
        {
            telemetry::ExecutionTelemetry::instance().recordSimple(
                telemetry::EventKind::WorkerStatus, -1, 0,
                "worker-pool:destroyed-with-inflight-runs" );
        }
    }
    shutdown();
}

bool LocalWorkerPool::start( const LocalWorkerPoolConfig &config, QString *errorOut )
{
    if ( config.workerProgram.isEmpty() )
    {
        if ( errorOut )
            *errorOut = QStringLiteral( "worker pool: no worker program configured" );
        return false;
    }
    shutdown();
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_config = config;
        m_config.maxWorkers = std::clamp( m_config.maxWorkers, 1, 16 );
        m_config.minWarmWorkers = std::clamp( m_config.minWarmWorkers, 0, m_config.maxWorkers );
        m_running = true;
    }
    // Pre-warm: spawn+handshake the minimum now so the first job skips the
    // process start cost. A warm-up failure is not fatal (lazy spawn retries
    // on demand); it only means the pool starts cold. Spawning happens
    // WITHOUT the pool mutex (P1: process waits never under m_mutex).
    for ( int i = 0; i < m_config.minWarmWorkers; ++i )
    {
        auto worker = spawnWorker();
        if ( !worker )
            break;
        std::lock_guard<std::mutex> lock( m_mutex );
        m_idle.push_back( std::move( worker ) );
        m_idleChanged.notify_all();
    }
    return true;
}

void LocalWorkerPool::shutdown()
{
    std::deque<std::unique_ptr<Worker>> retired;
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_running = false;
        retired.swap( m_idle );
        m_idleChanged.notify_all();
    }
    for ( auto &worker : retired )
        retireWorker( std::move( worker ) );
}

bool LocalWorkerPool::isRunning() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_running;
}

// Tears down a worker that is ALREADY removed from the pool's accounting.
// NO pool lock held. Bounded (~2s): idle workers have no in-flight job, so a
// short shutdown window followed by a hard kill loses nothing.
void LocalWorkerPool::teardownWorker( std::unique_ptr<Worker> worker )
{
    if ( !worker )
        return;
    if ( worker->process && worker->process->state() == QProcess::Running )
    {
        workerWriteLine( *worker->process, sicnu::runtime::worker::makeShutdownRequest() );
        worker->process->waitForFinished( 1000 );
        if ( worker->process->state() == QProcess::Running )
        {
            // 8.0 WP-C: group/job-wide kill ladder (covers operator-spawned
            // helper processes a plain QProcess::kill would orphan).
            worker->guard.terminateTree( *worker->process );
        }
    }
    worker->process.reset();
}

// m_mutex HELD. Pops one drivable (QProcess-affine to @p self, lifetime
// budget left) idle worker. Lifetime-exhausted owned workers and, under slot
// pressure, the oldest foreign idle worker are appended to @a teardownOut with
// their m_alive accounting already applied; the CALLER tears them ALL down
// after releasing the mutex (P1: process waits must never happen under m_mutex).
std::unique_ptr<LocalWorkerPool::Worker> LocalWorkerPool::takeIdleWorkerLocked(
    Qt::HANDLE self, std::vector<std::unique_ptr<Worker>> &teardownOut )
{
    std::unique_ptr<Worker> ownedHealthy;
    std::deque<std::unique_ptr<Worker>> keep;
    while ( !m_idle.empty() )
    {
        auto worker = std::move( m_idle.front() );
        m_idle.pop_front();
        const bool owned = !worker->ownerThread || worker->ownerThread == self;
        const qint64 idleMs = nowMs() - worker->lastUsedMs;
        const bool lifetimeExhausted = worker->jobsDone >= m_config.maxJobsPerWorker
                                       || idleMs > m_config.idleRecycleAfter.count();
        const bool processAlive = worker->process->state() == QProcess::Running;
        if ( !ownedHealthy && owned && !lifetimeExhausted && processAlive )
        {
            ownedHealthy = std::move( worker );
            continue;
        }
        if ( owned && ( lifetimeExhausted || !processAlive ) )
        {
            // Recycle: accounting applied now, teardown after unlock.
            ++m_totalRecycles;
            if ( m_alive > 0 )
                --m_alive;
            m_idleChanged.notify_all();
            teardownOut.push_back( std::move( worker ) );
            continue;
        }
        keep.push_back( std::move( worker ) );
    }
    if ( !ownedHealthy && m_alive >= m_config.maxWorkers && !keep.empty() )
    {
        // Slot pressure self-healing: force-retire the OLDEST idle worker
        // (foreign by construction — owned ones were scanned above) so a
        // thread that parked its workers and died cannot starve new callers.
        auto victim = std::move( keep.front() );
        keep.pop_front();
        ++m_totalRecycles;
        if ( m_alive > 0 )
            --m_alive;
        m_idleChanged.notify_all();
        teardownOut.push_back( std::move( victim ) );
    }
    m_idle = std::move( keep );
    return ownedHealthy;
}

// NO pool lock held: spawns the worker process and runs the ready/handshake
// (worst case 30s). m_config is read without the mutex — the shared pool is
// never restarted while running; direct-use pools must not call start()
// concurrently with run() (documented contract).
std::unique_ptr<LocalWorkerPool::Worker> LocalWorkerPool::spawnWorker()
{
    auto worker = std::make_unique<Worker>();
    worker->ownerThread = QThread::currentThreadId();
    worker->process = std::make_unique<QProcess>();
    worker->process->setProgram( m_config.workerProgram );
    worker->process->setArguments(
        { QStringLiteral( "--protocol" ),
          QString::fromLatin1( sicnu::runtime::worker::kWorkerProtocolVersion ) } );
    worker->process->setProcessChannelMode( QProcess::SeparateChannels );
    // 8.0 WP-C: bind the worker to OS-level containment BEFORE spawn (POSIX:
    // new session / process group; Windows arms the Job Object after start).
    worker->guard.attach( *worker->process );
    const auto spawnAt = std::chrono::steady_clock::now();
    worker->process->start( QIODevice::ReadWrite );
    if ( !worker->process->waitForStarted( 5000 ) )
    {
        worker->process.reset();
        return nullptr; // cannot spawn: caller reports / retries lazily
    }
    worker->guard.armAfterStart( *worker->process );
    // Spawn-time health check: the first frame must be a ready/v1 handshake.
    // "caps" (7.0) is recorded for routing/diagnostics; its absence is fine.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );
    Json::Value frame;
    bool crashed = false;
    bool softTimedOut = false;
    std::string badFrame;
    if ( !workerReadFrame( *worker->process, deadline, frame, crashed, deadline, softTimedOut,
                           &worker->diagnostics, &badFrame )
         || frame["op"].asString() != "ready" )
    {
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            ++m_totalCrashes;
        }
        telemetry::ExecutionTelemetry::instance().recordSimple(
            telemetry::EventKind::WorkerStatus, -1, 0, "worker-pool:handshake-failed" );
        if ( worker->process->state() == QProcess::Running )
        {
            worker->process->kill();
            worker->process->waitForFinished( 3000 );
        }
        worker->process.reset();
        return nullptr;
    }
    for ( const auto &cap : { sicnu::runtime::worker::kWorkerCapProgress,
                              sicnu::runtime::worker::kWorkerCapCancelAck,
                              sicnu::runtime::worker::kWorkerCapStructuredErrors,
                              sicnu::runtime::worker::kWorkerCapOutputIdentity,
                              sicnu::runtime::worker::kWorkerCapHeartbeat } )
    {
        if ( sicnu::runtime::worker::frameHasCapability( frame, cap ) )
            worker->capabilities << QString::fromLatin1( cap );
    }
    worker->handshakeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now()
                                                               - spawnAt ).count();
    worker->startedMs = nowMs();
    worker->lastUsedMs = worker->startedMs;
    telemetry::ExecutionTelemetry::instance().increment( telemetry::Counter::WorkersSpawned );
    telemetry::ExecutionTelemetry::instance().recordSimple(
        telemetry::EventKind::WorkerStatus, -1, 0, "worker-pool:spawned" );
    return worker;
}

void LocalWorkerPool::retireWorker( std::unique_ptr<Worker> worker )
{
    if ( !worker )
        return;
    if ( worker->process )
    {
        if ( worker->process->state() == QProcess::Running )
        {
            workerWriteLine( *worker->process, sicnu::runtime::worker::makeShutdownRequest() );
            worker->process->waitForFinished( 3000 );
            if ( worker->process->state() == QProcess::Running )
            {
                worker->guard.terminateTree( *worker->process );
            }
        }
        worker->process.reset();
    }
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( m_alive > 0 )
            --m_alive;
        m_idleChanged.notify_all();
    }
}

void LocalWorkerPool::releaseWorker( std::unique_ptr<Worker> worker )
{
    if ( !worker )
        return;
    worker->lastUsedMs = nowMs();
    if ( !m_running || worker->jobsDone >= m_config.maxJobsPerWorker
         || worker->process->state() != QProcess::Running )
    {
        // Lifetime/memory recycling: a worker past its job budget (or one the
        // OS already reaped) is retired instead of pooled.
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            ++m_totalRecycles;
        }
        retireWorker( std::move( worker ) );
        return;
    }
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_idle.push_back( std::move( worker ) );
        m_idleChanged.notify_all();
    }
}

LocalWorkerPool::Outcome LocalWorkerPool::runOnWorker(
    Worker &worker, const std::string &jobId, const std::string &algorithmId,
    const Json::Value &params, const std::function<bool()> &isCancelled,
    const std::function<void( double, const std::string & )> &onProgress, Json::Value *payload,
    std::string *errorMessage, bool *cancelAcked )
{
    const auto deadline = std::chrono::steady_clock::now() + m_config.jobTimeout;
    if ( !workerWriteLine( *worker.process,
                           sicnu::runtime::worker::makeRunRequest( jobId, algorithmId, params ) ) )
    {
        // The write path is broken: worker state is unknown — never reuse.
        *errorMessage = "worker protocol: cannot send run request";
        return Outcome::Crashed;
    }

    bool cancelRequested = false;
    std::chrono::steady_clock::time_point cancelDeadline{};
    // 8.0 WP-C liveness tracking: any frame (including heartbeats) proves the
    // worker process is alive; silence beyond the hang window (when enabled)
    // escalates the whole tree.
    const auto hangWindow = hangDetectionTimeout();
    auto lastFrameAt = std::chrono::steady_clock::now();
    while ( true )
    {
        if ( isCancelled && isCancelled() && !cancelRequested )
        {
            cancelRequested = true;
            cancelDeadline = std::chrono::steady_clock::now() + m_config.cancelGraceMs;
            workerWriteLine( *worker.process, sicnu::runtime::worker::makeCancelRequest( jobId ) );
        }
        if ( cancelRequested && std::chrono::steady_clock::now() >= cancelDeadline )
        {
            // Escalation ladder (see local_worker_host): full grace first,
            // then the group/job-wide terminate → kill ladder. The ack/
            // diagnostics of a cooperatively exiting worker stay readable
            // this way; operator-spawned helper processes die with the tree
            // (8.0 WP-C containment).
            worker.guard.terminateTree( *worker.process );
            worker.diagnostics.drain( *worker.process );
            *errorMessage = "worker cancelled";
            return Outcome::Cancelled;
        }
        Json::Value frame;
        bool crashed = false;
        bool softTimedOut = false;
        std::string badFrame;
        const auto soft = std::chrono::steady_clock::now() + std::chrono::milliseconds( 250 );
        if ( !workerReadFrame( *worker.process, deadline, frame, crashed, soft, softTimedOut,
                               &worker.diagnostics, &badFrame ) )
        {
            const std::string diagnostics = diagnosticsSuffix( worker.diagnostics );
            if ( softTimedOut )
            {
                if ( hangWindow.count() > 0 && !cancelRequested
                     && std::chrono::steady_clock::now() - lastFrameAt > hangWindow )
                {
                    // Hung: alive-but-silent beyond the configured window.
                    // Tree-wide kill — a hung operator cannot read a cancel
                    // frame, and its helper processes must not survive.
                    worker.guard.terminateTree( *worker.process );
                    worker.diagnostics.drain( *worker.process );
                    *errorMessage = "worker timeout: no frames within the hang window"
                                    + diagnosticsSuffix( worker.diagnostics );
                    return Outcome::TimedOut;
                }
                continue; // re-check cancellation, keep waiting
            }
            if ( !badFrame.empty() )
            {
                // A malformed frame desynchronizes the stream: the worker is
                // NEVER reused (same contract as a crash).
                *errorMessage = "worker protocol: malformed frame: " + badFrame + diagnostics;
                return Outcome::Crashed;
            }
            if ( crashed )
            {
                *errorMessage = "worker crashed: process died without a reply" + diagnostics;
                return Outcome::Crashed;
            }
            if ( cancelRequested )
            {
                *errorMessage = "worker cancelled";
                return Outcome::Cancelled;
            }
            *errorMessage = "worker timeout: no reply within the deadline" + diagnostics;
            return Outcome::TimedOut;
        }
        const std::string op = frame["op"].asString();
        lastFrameAt = std::chrono::steady_clock::now(); // any frame = alive
        if ( op == "progress" )
        {
            if ( onProgress )
                onProgress( frame["value"].asDouble(), frame["message"].asString() );
            continue;
        }
        if ( op == "heartbeat" )
        {
            // 8.0 WP-C liveness frame: resets the hang window above; legacy
            // hosts never see it (unknown-op rule), capable hosts just keep
            // waiting.
            continue;
        }
        if ( op == "ack" )
        {
            // 7.0 cancel receipt: the worker accepted the cancel. The
            // escalation ladder stays the enforcement mechanism; the ack is
            // the evidence that the ladder acted on a live worker.
            if ( cancelRequested && frame["kind"].asString() == "cancel" )
            {
                if ( cancelAcked )
                    *cancelAcked = true;
                telemetry::ExecutionTelemetry::instance().increment(
                    telemetry::Counter::WorkerCancelAcks );
            }
            continue;
        }
        if ( op == "error" && frame["jobId"].asString() == jobId )
        {
            if ( cancelRequested && sicnu::runtime::worker::frameErrorMeansCancelled( frame ) )
            {
                *errorMessage = "worker cancelled";
                return Outcome::Cancelled;
            }
            *errorMessage = "worker error: " + frame["message"].asString()
                            + diagnosticsSuffix( worker.diagnostics );
            // A job-level operator error leaves the worker reusable: the
            // process answered and stays in sync for the next job.
            return Outcome::ProtocolError;
        }
        if ( op == "result" && frame["jobId"].asString() == jobId )
        {
            // The caller asked for cancellation: a result that raced the
            // cancel frame is still a cancelled job — its payload must not
            // surface as a success after the caller stopped wanting it.
            if ( cancelRequested )
            {
                *errorMessage = "worker cancelled";
                return Outcome::Cancelled;
            }
            *payload = frame["payload"];
            return Outcome::Result;
        }
        // Unknown op (7.0 extension rule): keep waiting, bounded by the
        // deadline — never a protocol violation.
    }
}

Json::Value LocalWorkerPool::run( const std::string &algorithmId, const Json::Value &params,
                                  const std::function<bool()> &isCancelled,
                                  const std::function<void( double, const std::string & )> &onProgress,
                                  LocalWorkerRunReport *report )
{
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( !m_running || m_destroying )
            throw std::runtime_error( "worker pool: pool is not running" );
        // QProcess affinity (7.0): per-worker. acquireWorkerLocked only
        // returns workers spawned by THIS thread, so acquire → runOnWorker →
        // release never cross threads and concurrent JobEngine threads can
        // drive their own workers safely. The m_destroying refusal closes
        // the destructor race: once destruction began, a late run() can no
        // longer resurrect m_activeRuns and race member destruction.
        ++m_activeRuns;
    }
    struct ActiveRunGuard
    {
        LocalWorkerPool *pool;
        ~ActiveRunGuard()
        {
            std::lock_guard<std::mutex> lock( pool->m_mutex );
            --pool->m_activeRuns;
            pool->m_idleChanged.notify_all();
        }
    } activeRunGuard{ this };
    static std::atomic<long> poolJobSeq{ 0 };
    const std::string jobId = "wp-" + std::to_string( ++poolJobSeq ) + "-"
                              + std::to_string( nowMs() );

    const auto giveUpBy = std::chrono::steady_clock::now() + m_config.jobTimeout;
    int spawnAttempts = 0;
    const Qt::HANDLE self = QThread::currentThreadId();
    for ( ;; )
    {
        std::unique_ptr<Worker> worker;
        std::vector<std::unique_ptr<Worker>> teardowns;
        bool reserveSpawn = false;
        bool waitRequired = false;
        {
            std::unique_lock<std::mutex> lock( m_mutex );
            if ( !m_running || m_destroying )
                throw std::runtime_error( "worker pool: pool is not running" );
            worker = takeIdleWorkerLocked( self, teardowns );
            if ( !worker )
            {
                if ( m_alive < m_config.maxWorkers )
                {
                    // Reserve the slot NOW (under the lock) so concurrent
                    // acquirers cannot oversubscribe; the spawn itself runs
                    // with the lock RELEASED (P1: process spawn + 30s
                    // handshake must never stall the whole pool).
                    ++m_alive;
                    reserveSpawn = true;
                }
                else if ( !teardowns.empty() )
                {
                    // The force-retired victim freed a slot: use it.
                    --m_alive;
                    reserveSpawn = true;
                }
                else
                {
                    waitRequired = true;
                }
            }
        }
        // Teardown AFTER unlock: bounded waits on an idle worker (no
        // in-flight job → nothing to lose by the short kill ladder).
        // Drain every exhausted victim — a single unique_ptr used to drop
        // the first of two without teardownWorker (#932).
        for ( auto &victim : teardowns )
            teardownWorker( std::move( victim ) );
        if ( waitRequired )
        {
            // All slots busy and nothing reclaimable: wait for a release
            // (bounded by the job timeout so a stuck pool cannot wait
            // forever).
            std::unique_lock<std::mutex> lock( m_mutex );
            if ( !m_idleChanged.wait_for( lock, m_config.jobTimeout, [&] {
                     return !m_idle.empty() || m_alive < m_config.maxWorkers || !m_running;
                 } ) )
            {
                throw std::runtime_error( "worker timeout: no worker became available" );
            }
            if ( !m_running || m_destroying )
                throw std::runtime_error( "worker pool: pool is not running" );
            continue;
        }
        if ( reserveSpawn )
        {
            worker = spawnWorker(); // no lock held here
            if ( !worker )
            {
                {
                    std::lock_guard<std::mutex> lock( m_mutex );
                    if ( m_alive > 0 )
                        --m_alive;
                    m_idleChanged.notify_all();
                }
                // Cannot spawn: bounded fail-fast retry (transient fork
                // pressure self-heals; a genuinely broken program fails
                // within seconds, not after the full job timeout).
                ++spawnAttempts;
                if ( spawnAttempts >= 3
                     || std::chrono::steady_clock::now() >= giveUpBy )
                    throw std::runtime_error( "worker protocol: cannot start "
                                              + m_config.workerProgram.toStdString() );
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
                continue;
            }
            spawnAttempts = 0;
        }
        if ( !worker )
            continue; // defensive: no idle, no reservation, no wait flagged

        Json::Value payload;
        std::string errorMessage;
        bool cancelAcked = false;
        const Outcome outcome = runOnWorker( *worker, jobId, algorithmId, params, isCancelled,
                                             onProgress, &payload, &errorMessage, &cancelAcked );
        if ( report )
        {
            report->capabilities = worker->capabilities;
            report->handshakeMs = worker->handshakeMs;
            report->cancelAcked = cancelAcked;
        }
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            ++m_totalRuns;
        }
        auto &telemetryInstance = telemetry::ExecutionTelemetry::instance();
        switch ( outcome )
        {
            case Outcome::Result:
                worker->jobsDone++;
                if ( report )
                    report->stderrTail = worker->diagnostics.tail();
                worker->diagnostics = WorkerDiagnosticsRing {};
                releaseWorker( std::move( worker ) );
                telemetryInstance.recordSimple( telemetry::EventKind::ExecutionEnd,
                                                -1, 0, "worker-pool:" + algorithmId );
                return payload;
            case Outcome::Cancelled:
            {
                std::lock_guard<std::mutex> lock( m_mutex );
                ++m_totalCancels;
            }
                telemetryInstance.recordSimple( telemetry::EventKind::WorkerStatus, -1, 0,
                                                "worker-pool:cancelled" );
                retireWorker( std::move( worker ) );
                throw std::runtime_error( errorMessage );
            case Outcome::Crashed:
            {
                std::lock_guard<std::mutex> lock( m_mutex );
                ++m_totalCrashes;
            }
                if ( report )
                    report->stderrTail = worker->diagnostics.tail();
                telemetryInstance.increment( telemetry::Counter::WorkersCrashed );
                telemetryInstance.recordSimple(
                    telemetry::EventKind::WorkerStatus, -1, 0,
                    "worker-pool:crashed" + diagnosticsSuffix( worker->diagnostics ).substr( 0, 256 ) );
                retireWorker( std::move( worker ) ); // replacement spawns lazily
                throw std::runtime_error( errorMessage );
            case Outcome::TimedOut:
            {
                std::lock_guard<std::mutex> lock( m_mutex );
                ++m_totalTimeouts;
            }
                if ( report )
                    report->stderrTail = worker->diagnostics.tail();
                telemetryInstance.recordSimple(
                    telemetry::EventKind::WorkerStatus, -1, 0,
                    "worker-pool:timeout" + diagnosticsSuffix( worker->diagnostics ).substr( 0, 256 ) );
                retireWorker( std::move( worker ) );
                throw std::runtime_error( errorMessage );
            case Outcome::ProtocolError:
                // Operator-level error: the worker process answered and stays
                // in sync — keep it warm.
                if ( report )
                    report->stderrTail = worker->diagnostics.tail();
                worker->diagnostics = WorkerDiagnosticsRing {};
                worker->jobsDone++;
                releaseWorker( std::move( worker ) );
                throw std::runtime_error( errorMessage );
        }
        Q_UNREACHABLE();
    }
}

WorkerPoolHealthSnapshot LocalWorkerPool::health() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    WorkerPoolHealthSnapshot snapshot;
    snapshot.configuredMax = m_config.maxWorkers;
    snapshot.aliveWorkers = m_alive;
    snapshot.idleWorkers = static_cast<int>( m_idle.size() );
    snapshot.totalRuns = m_totalRuns;
    snapshot.totalCrashes = m_totalCrashes;
    snapshot.totalTimeouts = m_totalTimeouts;
    snapshot.totalCancels = m_totalCancels;
    snapshot.totalRecycles = m_totalRecycles;
    return snapshot;
}

Json::Value runInPooledWorker( LocalWorkerPool &pool, const std::string &algorithmId,
                               const Json::Value &params,
                               const std::function<bool()> &isCancelled )
{
    return pool.run( algorithmId, params, isCancelled );
}

} // namespace sicnu::processing

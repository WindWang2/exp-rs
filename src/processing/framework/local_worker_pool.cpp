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

bool writeLine( QProcess &process, const std::string &line )
{
    const QByteArray bytes = QByteArray::fromStdString( line + "\n" );
    process.write( bytes );
    return process.waitForBytesWritten( 5000 );
}

/// As local_worker_host's readFrame: one protocol frame with a hard deadline
/// and a soft (cancellation-poll) deadline.
bool readFrame( QProcess &process, std::chrono::steady_clock::time_point deadline,
                Json::Value &frame, bool &workerCrashed,
                std::chrono::steady_clock::time_point softDeadline, bool &softTimedOut )
{
    workerCrashed = false;
    while ( true )
    {
        while ( process.canReadLine() )
        {
            const QByteArray raw = process.readLine();
            const std::string line = QString::fromUtf8( raw ).trimmed().toStdString();
            if ( line.empty() )
                continue;
            if ( !sicnu::runtime::worker::parseFrame( line, frame ) )
                return false; // malformed or version mismatch — hard refusal
            return true;
        }
        if ( process.state() != QProcess::Running && !process.canReadLine() )
        {
            workerCrashed = true;
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        if ( now >= deadline )
            return false;
        if ( now >= softDeadline )
        {
            softTimedOut = true;
            return false;
        }
        if ( !process.waitForReadyRead( 100 ) && process.state() != QProcess::Running )
        {
            workerCrashed = true;
            return false;
        }
    }
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
        m_ownerThread = QThread::currentThreadId();
    }
    // Pre-warm: spawn+handshake the minimum now so the first job skips the
    // process start cost. A warm-up failure is not fatal (lazy spawn retries
    // on demand); it only means the pool starts cold. Worker spawn/retire
    // lock the pool mutex themselves — never call them with it held.
    for ( int i = 0; i < m_config.minWarmWorkers; ++i )
    {
        std::unique_ptr<Worker> worker;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            worker = acquireWorkerLocked();
        }
        if ( !worker )
            break;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_idle.push_back( std::move( worker ) );
            m_idleChanged.notify_all();
        }
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

std::unique_ptr<LocalWorkerPool::Worker> LocalWorkerPool::acquireWorkerLocked()
{
    // Prefer an idle warm worker with lifetime budget left.
    while ( !m_idle.empty() )
    {
        auto worker = std::move( m_idle.front() );
        m_idle.pop_front();
        const qint64 idleMs = nowMs() - worker->lastUsedMs;
        const bool lifetimeExhausted = worker->jobsDone >= m_config.maxJobsPerWorker
                                       || idleMs > m_config.idleRecycleAfter.count();
        if ( lifetimeExhausted || worker->process->state() != QProcess::Running )
        {
            ++m_totalRecycles;
            // m_mutex is held: tear the process down inline (rare path) and
            // adjust the alive count here instead of calling retireWorker,
            // which locks.
            if ( worker->process->state() == QProcess::Running )
            {
                writeLine( *worker->process, sicnu::runtime::worker::makeShutdownRequest() );
                worker->process->waitForFinished( 3000 );
                if ( worker->process->state() == QProcess::Running )
                {
                    worker->process->kill();
                    worker->process->waitForFinished( 3000 );
                }
            }
            worker->process.reset();
            if ( m_alive > 0 )
                --m_alive;
            m_idleChanged.notify_all();
            continue;
        }
        return worker;
    }
    // Pool empty: spawn a fresh worker within the bound (or wait — handled by
    // the caller's loop when m_alive is at the cap).
    if ( m_alive >= m_config.maxWorkers )
        return nullptr;
    auto worker = std::make_unique<Worker>();
    worker->process = std::make_unique<QProcess>();
    worker->process->setProgram( m_config.workerProgram );
    worker->process->setArguments(
        { QStringLiteral( "--protocol" ),
          QString::fromLatin1( sicnu::runtime::worker::kWorkerProtocolVersion ) } );
    worker->process->setProcessChannelMode( QProcess::SeparateChannels );
    worker->process->start( QIODevice::ReadWrite );
    if ( !worker->process->waitForStarted( 5000 ) )
    {
        worker->process.reset();
        return nullptr; // cannot spawn: caller reports / retries lazily
    }
    // Spawn-time health check: the first frame must be a ready/v1 handshake.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );
    Json::Value frame;
    bool crashed = false;
    bool softTimedOut = false;
    if ( !readFrame( *worker->process, deadline, frame, crashed, deadline, softTimedOut )
         || frame["op"].asString() != "ready" )
    {
        ++m_totalCrashes;
        telemetry::ExecutionTelemetry::instance().recordSimple(
            telemetry::EventKind::WorkerStatus, -1, 0, "worker-pool:handshake-failed" );
        // m_mutex is held: inline teardown, not retireWorker (which locks).
        if ( worker->process->state() == QProcess::Running )
        {
            worker->process->kill();
            worker->process->waitForFinished( 3000 );
        }
        worker->process.reset();
        return nullptr;
    }
    worker->startedMs = nowMs();
    worker->lastUsedMs = worker->startedMs;
    ++m_alive;
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
            writeLine( *worker->process, sicnu::runtime::worker::makeShutdownRequest() );
            worker->process->waitForFinished( 3000 );
            if ( worker->process->state() == QProcess::Running )
            {
                worker->process->kill();
                worker->process->waitForFinished( 3000 );
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

LocalWorkerPool::Outcome LocalWorkerPool::runOnWorker( Worker &worker, const std::string &jobId,
                                                       const std::string &algorithmId,
                                                       const Json::Value &params,
                                                       const std::function<bool()> &isCancelled,
                                                       Json::Value *payload,
                                                       std::string *errorMessage )
{
    const auto deadline = std::chrono::steady_clock::now() + m_config.jobTimeout;
    if ( !writeLine( *worker.process,
                     sicnu::runtime::worker::makeRunRequest( jobId, algorithmId, params ) ) )
    {
        // The write path is broken: worker state is unknown — never reuse.
        *errorMessage = "worker protocol: cannot send run request";
        return Outcome::Crashed;
    }

    bool cancelRequested = false;
    std::chrono::steady_clock::time_point cancelDeadline{};
    while ( true )
    {
        if ( isCancelled && isCancelled() && !cancelRequested )
        {
            cancelRequested = true;
            cancelDeadline = std::chrono::steady_clock::now() + m_config.cancelGraceMs;
            writeLine( *worker.process, sicnu::runtime::worker::makeCancelRequest( jobId ) );
            worker.process->terminate();
        }
        if ( cancelRequested && std::chrono::steady_clock::now() >= cancelDeadline )
        {
            worker.process->kill();
            worker.process->waitForFinished( 3000 );
            *errorMessage = "worker cancelled";
            return Outcome::Cancelled;
        }
        Json::Value frame;
        bool crashed = false;
        bool softTimedOut = false;
        const auto soft = std::chrono::steady_clock::now() + std::chrono::milliseconds( 250 );
        if ( !readFrame( *worker.process, deadline, frame, crashed, soft, softTimedOut ) )
        {
            if ( softTimedOut )
                continue; // re-check cancellation, keep waiting
            if ( crashed )
            {
                *errorMessage = "worker crashed: process died without a reply";
                return Outcome::Crashed;
            }
            if ( cancelRequested )
            {
                *errorMessage = "worker cancelled";
                return Outcome::Cancelled;
            }
            *errorMessage = "worker timeout: no reply within the deadline";
            return Outcome::TimedOut;
        }
        const std::string op = frame["op"].asString();
        if ( op == "progress" )
            continue;
        if ( op == "error" && frame["jobId"].asString() == jobId )
        {
            if ( cancelRequested && frame["message"].asString() == "cancelled" )
            {
                *errorMessage = "worker cancelled";
                return Outcome::Cancelled;
            }
            *errorMessage = "worker error: " + frame["message"].asString();
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
    }
}

Json::Value LocalWorkerPool::run( const std::string &algorithmId, const Json::Value &params,
                                  const std::function<bool()> &isCancelled )
{
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( !m_running )
            throw std::runtime_error( "worker pool: pool is not running" );
        // QProcess affinity: pooled workers are driven from the owning thread
        // only (a cross-thread run would violate Qt's QProcess contract).
        if ( m_ownerThread && QThread::currentThreadId() != m_ownerThread )
            throw std::runtime_error( "worker pool: run() called from a foreign thread" );
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
    for ( ;; )
    {
        std::unique_ptr<Worker> worker;
        {
            std::unique_lock<std::mutex> lock( m_mutex );
            if ( !m_running )
                throw std::runtime_error( "worker pool: pool is not running" );
            worker = acquireWorkerLocked();
            if ( !worker && m_alive >= m_config.maxWorkers )
            {
                // All workers busy: wait for one to come back (bounded by the
                // job timeout so a stuck pool cannot wait forever).
                if ( !m_idleChanged.wait_for( lock, m_config.jobTimeout, [&] {
                         return !m_idle.empty() || m_alive < m_config.maxWorkers || !m_running;
                     } ) )
                {
                    throw std::runtime_error( "worker timeout: no worker became available" );
                }
                if ( !m_running )
                    throw std::runtime_error( "worker pool: pool is not running" );
                continue;
            }
        }
        if ( !worker )
        {
            // Cannot spawn: bounded fail-fast retry (transient fork pressure
            // self-heals; a genuinely broken program fails within seconds,
            // not after the full job timeout).
            ++spawnAttempts;
            if ( spawnAttempts >= 3
                 || std::chrono::steady_clock::now() >= giveUpBy )
                throw std::runtime_error( "worker protocol: cannot start "
                                          + m_config.workerProgram.toStdString() );
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            continue;
        }
        spawnAttempts = 0;

        Json::Value payload;
        std::string errorMessage;
        const Outcome outcome =
            runOnWorker( *worker, jobId, algorithmId, params, isCancelled, &payload, &errorMessage );
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            ++m_totalRuns;
        }
        auto &telemetryInstance = telemetry::ExecutionTelemetry::instance();
        switch ( outcome )
        {
            case Outcome::Result:
                worker->jobsDone++;
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
                telemetryInstance.increment( telemetry::Counter::WorkersCrashed );
                retireWorker( std::move( worker ) ); // replacement spawns lazily
                throw std::runtime_error( errorMessage );
            case Outcome::TimedOut:
            {
                std::lock_guard<std::mutex> lock( m_mutex );
                ++m_totalTimeouts;
            }
                telemetryInstance.recordSimple( telemetry::EventKind::WorkerStatus, -1, 0,
                                                "worker-pool:timeout" );
                retireWorker( std::move( worker ) );
                throw std::runtime_error( errorMessage );
            case Outcome::ProtocolError:
                // Operator-level error: the worker process answered and stays
                // in sync — keep it warm.
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

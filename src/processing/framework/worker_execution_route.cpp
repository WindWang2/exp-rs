// worker_execution_route.cpp — see worker_execution_route.h for the contract.
#include "worker_execution_route.h"

#include "atomic_algorithm_registry.h"
#include "algorithm_descriptor.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "runtime/observability/execution_telemetry.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace sicnu::processing
{
namespace
{
namespace telemetry = sicnu::runtime::observability;

std::atomic<int> g_isolatedJobLimit{ 2 };
std::atomic<int> g_isolatedJobsRunning{ 0 };
/// CONFIGURED execution mode: -1 = not yet read (first read consumes the
/// environment), otherwise an explicit configuration (pool start or the
/// host/test hook) wins over the environment.
std::atomic<int> g_configuredMode{ -1 };

std::mutex g_poolMutex;
LocalWorkerPool g_pool;
bool g_poolStarted = false;
WorkerExecutionConfig g_activeConfig;

WorkerExecutionMode modeFromString( const QString &raw )
{
    const QString value = raw.trimmed().toLower();
    if ( value == QLatin1String( "auto" ) )
        return WorkerExecutionMode::Auto;
    if ( value == QLatin1String( "require" ) )
        return WorkerExecutionMode::Require;
    return WorkerExecutionMode::Off; // unparsable/empty: the safe default
}

/// Resolution order: explicit config/env override → next to the running
/// executable (sicnu_worker[.exe]) → bare name (PATH). Empty means "cannot
/// resolve here"; start() reports it.
QString resolveWorkerProgram( const QString &explicitProgram )
{
    if ( !explicitProgram.isEmpty() )
        return explicitProgram;
    if ( QCoreApplication::instance() )
    {
        const QString exe = QCoreApplication::applicationDirPath();
        if ( !exe.isEmpty() )
        {
#ifdef Q_OS_WIN
            const QString candidate = exe + QLatin1String( "/sicnu_worker.exe" );
#else
            const QString candidate = exe + QLatin1String( "/sicnu_worker" );
#endif
            if ( QFileInfo::exists( candidate ) )
                return candidate;
        }
    }
    return QStringLiteral( "sicnu_worker" );
}

/// Fail-closed error for a routed job when isolation is unavailable. The
/// message names the mode so the operator knows isolation did NOT happen.
std::runtime_error isolationUnavailableError( const QString &algorithmId )
{
    return std::runtime_error(
        ( QStringLiteral( "worker pool: isolated execution unavailable for %1 "
                          "(mode=%2, pool not started — refusing in-process fallback)" )
              .arg( algorithmId,
                    QString::fromLatin1( workerExecutionModeName( currentWorkerExecutionMode() ) ) ) )
            .toStdString() );
}

/// Descriptor lookup shared by the selection predicate. Unknown algorithms
/// have no declared preference → not routed (conservative).
bool descriptorPrefersIsolated( const std::string &algorithmId )
{
    try
    {
        auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( algorithmId );
        if ( !adapter )
            return false;
        const auto desc = adapter->descriptor();
        return desc.agentMetadata.executionPreference == "isolated"
               && desc.agentMetadata.supportsCancellation;
    }
    catch ( ... )
    {
        return false; // a descriptor failure never routes to a worker
    }
}

} // namespace

WorkerExecutionConfig workerExecutionConfigFromEnvironment()
{
    WorkerExecutionConfig config;
    config.mode = modeFromString( qEnvironmentVariable( "SICNU_WORKER_EXECUTION" ) );
    config.workerProgram = QString::fromLocal8Bit( qgetenv( "SICNU_WORKER_PROGRAM" ) );
    bool ok = false;
    const int requested = qEnvironmentVariableIntValue( "SICNU_WORKER_MAX_CONCURRENT", &ok );
    if ( ok && requested > 0 )
        config.maxConcurrentIsolatedJobs = std::min( requested, 64 );
    config.pool.workerProgram = config.workerProgram;
    return config;
}

WorkerExecutionMode configuredMode()
{
    int expected = g_configuredMode.load();
    if ( expected >= 0 )
        return static_cast<WorkerExecutionMode>( expected );
    const WorkerExecutionMode fromEnv = modeFromString( qEnvironmentVariable( "SICNU_WORKER_EXECUTION" ) );
    g_configuredMode.compare_exchange_strong( expected, static_cast<int>( fromEnv ) );
    return static_cast<WorkerExecutionMode>( g_configuredMode.load() );
}

WorkerExecutionMode currentWorkerExecutionMode()
{
    return configuredMode();
}

void setWorkerExecutionMode( WorkerExecutionMode mode )
{
    g_configuredMode.store( static_cast<int>( mode ) );
}

bool ensureSharedWorkerPoolStarted( const WorkerExecutionConfig &config, QString *errorOut )
{
    std::lock_guard<std::mutex> lock( g_poolMutex );
    if ( g_poolStarted )
        return true;
    WorkerExecutionConfig effective = config;
    g_configuredMode.store( static_cast<int>( effective.mode ) );
    effective.pool.workerProgram = resolveWorkerProgram( config.workerProgram );
    effective.pool.maxWorkers = std::max( effective.maxConcurrentIsolatedJobs,
                                          effective.pool.maxWorkers );
    // Pre-warm is skipped for the shared pool: warm workers are QProcess-
    // affine to the starting thread (see LocalWorkerPool's thread model) and
    // JobEngine dispatches from different threads — a pre-warmed worker
    // would sit foreign-idle until recycled.
    effective.pool.minWarmWorkers = 0;
    g_isolatedJobLimit.store( std::max( 1, effective.maxConcurrentIsolatedJobs ) );
    QString error;
    if ( !g_pool.start( effective.pool, &error ) )
    {
        if ( errorOut )
            *errorOut = error;
        return false;
    }
    g_activeConfig = effective;
    g_poolStarted = true;
    telemetry::ExecutionTelemetry::instance().recordSimple(
        telemetry::EventKind::WorkerStatus, -1, 0,
        ( std::string( "worker-route:started:mode=" )
          + workerExecutionModeName( effective.mode )
          + ":program=" + effective.pool.workerProgram.toStdString() )
            .substr( 0, 240 ) );
    return true;
}

LocalWorkerPool *sharedWorkerPool()
{
    std::lock_guard<std::mutex> lock( g_poolMutex );
    return g_poolStarted ? &g_pool : nullptr;
}

void shutdownSharedWorkerPool()
{
    std::lock_guard<std::mutex> lock( g_poolMutex );
    if ( !g_poolStarted )
        return;
    g_pool.shutdown();
    g_poolStarted = false;
    g_activeConfig = WorkerExecutionConfig{};
    telemetry::ExecutionTelemetry::instance().recordSimple(
        telemetry::EventKind::WorkerStatus, -1, 0, "worker-route:shutdown" );
}

bool shouldRunIsolated( const QString &algorithmId )
{
    // The CONFIGURED mode decides — not the pool's start state: "require"
    // must route (and then fail closed) even when the pool cannot start.
    const WorkerExecutionMode mode = configuredMode();
    switch ( mode )
    {
        case WorkerExecutionMode::Require:
        case WorkerExecutionMode::Auto:
        {
            // The worker executes only what the RSOperator registry knows;
            // provider/callable algorithms keep their own execution path.
            const std::string id = algorithmId.toStdString();
            if ( !sicnu::operators::RSOperatorRegistry::instance().hasOperator( id ) )
                return false;
            if ( mode == WorkerExecutionMode::Require )
                return true;
            return descriptorPrefersIsolated( id );
        }
        case WorkerExecutionMode::Off:
        default:
            return false;
    }
}

WorkerJobExecutor makeIsolatedWorkerExecutor( const QString &algorithmId )
{
    const std::string algorithm = algorithmId.toStdString();
    return [ algorithm ]( const sicnu::jobs::JobRequest &request,
                          sicnu::operators::RSOperatorContext &ctx ) -> Json::Value {
        LocalWorkerPool *pool = sharedWorkerPool();
        if ( !pool )
            throw isolationUnavailableError( QString::fromStdString( algorithm ) );
        LocalWorkerRunReport report;
        Json::Value payload = pool->run(
            request.algorithmId.empty() ? algorithm : request.algorithmId, request.params,
            [ &ctx ] { return ctx.isCancelled(); },
            [ &ctx ]( double value, const std::string &message ) {
                ctx.reportProgress( value, message );
            },
            &report );
        // Diagnostics ride the LOG channel (ctx → engine record → task log),
        // never the result payload: payloads feed the execution cache and
        // downstream consumers and must stay execution-location-independent.
        if ( !report.stderrTail.isEmpty() )
            ctx.logWarning( "worker stderr: " + report.stderrTail.toStdString() );
        return payload;
    };
}

void setIsolatedJobLimit( int maxConcurrent )
{
    g_isolatedJobLimit.store( std::max( 1, maxConcurrent ) );
}

int isolatedJobLimit()
{
    return g_isolatedJobLimit.load();
}

} // namespace sicnu::processing

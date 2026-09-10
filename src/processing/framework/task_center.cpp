#include "task_center.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QMutexLocker>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <utility>

#include "framework/json_params_converter.h"
#include "atomic_algorithm_registry.h"
#include "jobs/job_engine.h"
#include "workflow/workflow_definition.h"
#include "workflow/placeholder_grammar.h"
#include "workflow/artifact_gc.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/temporal/temporal_workspace.h"
#include "framework/fused_chain.h"
#include "framework/worker_execution_route.h"
#include "runtime/observability/execution_telemetry.h"
#include "runtime/observability/trace.h"
#include "data/data_manager.h"
#include "data/execution_identity_resolver.h"
#include "geospatial/remote/remote_identity_resolver.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

namespace sicnu {

namespace
{
/// Execution Plane 8.0 WP-I: one bounded exp.trace.v1 record per scheduling
/// transition (admitted / held / dispatched / retry / cancel / terminal /
/// cache). Correlation: task id (engine job ids join via the JobEngine's own
/// start/end events). Off by default: one relaxed atomic load when no sink
/// is installed (trace.h hot-path rule).
void traceTaskEvent( const char *event, const char *status, long taskId,
                     const QString &algorithmId, const QString &detail = {} )
{
    namespace trace = sicnu::runtime::observability::trace;
    if ( !trace::Trace::enabled() )
        return;
    trace::TraceEvent record;
    record.task = std::to_string( taskId );
    record.op = algorithmId.toStdString();
    record.event = event;
    record.status = status;
    record.detail = detail.left( 200 ).toStdString();
    trace::Trace::publish( record );
}
} // namespace

static QString findOutputPathInParams( const QVariantMap &params )
{
    // Prefer exact "output"/"OUTPUT" keys before alphabetical scan so modelOut
    // does not shadow the raster output (issue 376).
    auto pathIfValid = []( const QVariant &v ) -> QString {
        const QString p = v.toString();
        if ( !p.isEmpty() && !p.startsWith( QLatin1Char( '$' ) )
             && !p.startsWith( QStringLiteral( "\x01SICNU_ERR\x01" ) ) )
            return p;
        return QString();
    };
    for ( const QString &exact : { QStringLiteral( "output" ), QStringLiteral( "OUTPUT" ) } )
    {
        auto it = params.find( exact );
        if ( it != params.end() )
        {
            const QString p = pathIfValid( it.value() );
            if ( !p.isEmpty() ) return p;
        }
        // case-insensitive fallback for exact name
        for ( auto it2 = params.begin(); it2 != params.end(); ++it2 )
        {
            if ( it2.key().compare( exact, Qt::CaseInsensitive ) == 0 )
            {
                const QString p = pathIfValid( it2.value() );
                if ( !p.isEmpty() ) return p;
            }
        }
    }
    for ( auto it = params.begin(); it != params.end(); ++it )
    {
        // ONE shared vocabulary (sicnu::data::isOutputVocabularyKey, #726) so
        // output detection and the fingerprint's output-key filter cannot
        // drift apart.
        if ( sicnu::data::isOutputVocabularyKey( it.key() ) )
        {
            const QString path = pathIfValid( it.value() );
            if ( !path.isEmpty() )
                return path;
        }
    }
    return QString();
}

/// Shared "extract the output path from a Json result payload" helper. The
/// convention is result["output"] = "<path>" (set by RSOperator::run and the
/// QGIS/processing adapters). Returns empty when absent/non-string.
/// (perf/architecture goal 2026-08-08: de-duplicate the 3 inline copies of this
/// pattern across task_center.cpp.)
static QString outputPathFromResult( const Json::Value &result )
{
    if ( result.isObject() && result.isMember( "output" ) && result["output"].isString() )
        return QString::fromStdString( result["output"].asString() );
    return QString();
}



TaskCenter& TaskCenter::instance()
{
    static TaskCenter s_instance;
    return s_instance;
}

void TaskCenter::shutdown()
{
    m_isShuttingDown.store( true );
    {
        QMutexLocker locker( &m_mutex );
        m_waitCondition.wakeAll();
        // No new staging/flushing once shutdown began (#684): drop anything
        // already staged but not yet submitted to the engine.
        m_pendingLaunches.clear();
    }
    // Cancel every non-terminal task FIRST (engine flags armed while the
    // engine still accepts cancel), then join the engine, then finalize.
    cancelAllForShutdown();
    sicnu::jobs::JobEngine::instance().shutdown();
    // Execution Plane 7.0: quiesce the isolated worker pool AFTER the engine
    // joined (no more executor runs can start) and before finalization so a
    // GUI/app shutdown never leaves worker processes behind.
    processing::shutdownSharedWorkerPool();

    // The engine is terminated: no further job records can arrive. Force any
    // task still mid-flight (Cancelling, or never-cancelled stragglers) to a
    // terminal state so waiters and completion callbacks always resolve.
    QList<long> toFinalize;
    {
        QMutexLocker locker( &m_mutex );
        const QList<long> keys = m_tasks.keys();
        for ( long id : keys )
        {
            if ( isTerminalStatus( m_tasks[id].status ) )
                continue;
            auto &info = m_tasks[id];
            const bool wasCancelling = ( info.status == TaskStatus::Cancelling );
            setTaskStatusLocked( info, TaskStatus::Canceled );
            info.errorMessage = wasCancelling ? QStringLiteral( "Canceled during shutdown" )
                                              : QStringLiteral( "Canceled: application is shutting down" );
            info.endTime = QDateTime::currentDateTimeUtc();
            info.logBuffer.append( info.errorMessage );
            m_taskFingerprints.remove( id );
            m_taskFingerprintParams.remove( id );
            m_taskChainedEdges.remove( id );
            m_taskRegisteredInputStats.remove( id );
            updatePipelineForTaskLocked( id );
            queueTaskUpdatedLocked( id );
            toFinalize.append( id );
        }
        m_waitCondition.wakeAll();
    }
    flushPendingSignals();
    for ( long id : toFinalize )
        fireTaskCompletionCallbacks( id );
    {
        // Everything is terminal now; any registration that survived the
        // finalization pass is stale (defense against the #702 watch-leak
        // family) — nothing can fire it anymore.
        QMutexLocker locker( &m_mutex );
        m_completionCallbacks.clear();
        // Drop the catalog seam: the host may destroy its DataManager before
        // this singleton (tests destroy the fixture right after
        // shutdownForTests; at process exit destruction order is not
        // guaranteed), and a late worker record would dereference the
        // dangling pointer in taskExecutionFingerprintLocked (adversarial
        // review of #724). No execution can follow shutdown().
        m_catalog = nullptr;
    }
}

void TaskCenter::cancelAllForShutdown()
{
    QList<long> nonTerminal;
    {
        QMutexLocker locker( &m_mutex );
        for ( auto it = m_tasks.begin(); it != m_tasks.end(); ++it )
        {
            if ( !isTerminalStatus( it.value().status ) )
                nonTerminal.append( it.key() );
        }
    }
    // cancelTask is self-contained (lock, cascade, flush); call it per task.
    // Queued/WaitingResource/Dispatching tasks finalize synchronously;
    // dispatched ones go Cancelling and resolve via the terminal record
    // (or the finalization pass in shutdown() after the engine joined).
    for ( long id : nonTerminal )
        cancelTask( id );
}

TaskCenter::~TaskCenter()
{
    shutdown();
    // Process teardown: the provider lambda calls into ExecutionResultCache
    // (a separate TU's singleton whose destruction order is unspecified) —
    // stop GC sweeps from reaching it once TaskCenter is gone.
    sicnu::workflow::ArtifactGC::installProtectedArtifactProvider( {} );
}

void TaskCenter::shutdownForTests()
{
    shutdown();
    {
        QMutexLocker locker( &m_mutex );
        m_isShuttingDown.store( false );
        m_tasks.clear();
    m_fusedChains.clear();
        m_pipelines.clear();
        m_pendingLaunches.clear();
        m_pendingTaskAdded.clear();
        m_pendingTaskUpdated.clear();
        m_pendingLogs.clear();
        m_taskByJobId.clear();
        m_forwardedLogCounts.clear();
        m_lastForwardedProgress.clear();
        m_estimateMbCache.clear(); // task ids restart at 1: stale estimates must not leak across tests
        m_admissionDimsCache.clear();
        m_completionCallbacks.clear();
        m_taskFingerprints.clear();
        m_taskFingerprintParams.clear();
        m_taskChainedEdges.clear();
        // 8.0 WP-A: reset the incremental admission state (task ids restart
        // at 1 — stale heap/serial/counter entries must not leak across tests).
        m_active = ActiveCounters{};
        m_children.clear();
        m_incompleteParentCount.clear();
        std::priority_queue<ReadyEntry, std::vector<ReadyEntry>, ReadyEntryGreater> emptyHeap;
        m_readyHeap.swap( emptyHeap );
        m_readySerial.clear();
        m_nextReadySerial = 1;
        m_admissionPass = 0;
        m_manualQueued.clear();
        m_nextTaskId = 1;
        m_nextPipelineId = 1;
        m_waitCondition.wakeAll();
    }
    // Engine reset must happen outside m_mutex: shutdownForTests joins
    // workers and resets the sticky termination flag (#684).
    sicnu::jobs::JobEngine::instance().shutdownForTests();
    flushPendingSignals();
}

TaskCenter::TaskCenter()
{
    qRegisterMetaType<AlgorithmTaskInfo>("sicnu::AlgorithmTaskInfo");
    resetResourceProfileLimits();
    installDefaultEstimateResolver();
    // Default the RAM budget to the RSS watermark so the new resource-aware gate
    // is consistent with (not independent of) the existing memory-pressure gate.
    m_resourceBudget.setBudgetMb( m_resourceMonitor.memoryLimitMb() );
    // Cache↔GC lifecycle contract (#726): artifacts the execution cache still
    // holds are protected from ArtifactGC sweeps. TaskCenter is the process
    // singleton every execution path touches, so installing here (instead of
    // per-host) keeps the seam alive for GUI, MCP, CLI and tests alike.
    sicnu::workflow::ArtifactGC::installProtectedArtifactProvider(
        []() {
            return sicnu::data::ExecutionResultCache::instance().cachedArtifacts();
        } );

    // Execution Plane 8.0 WP-F: activate the remote-identity seam (7.0 left
    // it SEAM ONLY) with the geospatial strong-ETag resolver as the DEFAULT.
    // Only installs when no resolver is set: a host that explicitly wired its
    // own identity source keeps full authority. The resolver never blocks
    // submission (it is consulted per remote input ONLY when the execution
    // cache is enabled — off by default), and its empty verdicts stay
    // fail-closed (input uncacheable).
    if ( sicnu::data::executionIdentityResolver() == nullptr )
    {
        static const auto remoteResolver = [resolver = sicnu::geo::makeRemoteInputIdentityResolver()](
                                               const QString &path ) -> QString {
            return QString::fromStdString( resolver( path.toStdString() ) );
        };
        sicnu::data::setExecutionIdentityResolver( remoteResolver );
    }
}

ProviderResourceProfile TaskCenter::resolveResourceProfile( const QString &algorithmId ) const
{
    const auto providers = AlgorithmEngine::instance().registeredProviders();
    for ( const auto &provider : providers )
    {
        if ( provider && algorithmId.startsWith( provider->providerId() + QStringLiteral( ":" ) ) )
        {
            return provider->resourceProfile();
        }
    }
    return ProviderResourceProfile::InProcessThread;
}

unsigned int TaskCenter::defaultLimitForProfile( ProviderResourceProfile profile ) const
{
    // ONE capacity fact (#686): in-process admission equals the JobEngine
    // worker pool. Previously this was hardware_concurrency()-1 while the
    // engine clamped to 2-4 workers, so TaskCenter staged hw-1 tasks as
    // "Running" that then sat in the engine FIFO behind workers that did
    // not exist — fake Running, FIFO-overridden priority, and a RAM budget
    // charged by tasks holding no memory.
    const unsigned int engineWorkers = static_cast<unsigned int>(
        std::max( 1, sicnu::jobs::JobEngine::instance().maxWorkers() ) );
    switch ( profile )
    {
      case ProviderResourceProfile::ExternalCliSubprocess:
        return std::min( 2u, engineWorkers );
      case ProviderResourceProfile::PythonWorkerProcess:
        return std::min( 2u, engineWorkers );
      case ProviderResourceProfile::QgsTaskThread:
        return std::max( 1u, engineWorkers / 2 );
      case ProviderResourceProfile::InProcessThread:
      default:
        return engineWorkers;
    }
}

unsigned int TaskCenter::limitForProfileLocked( ProviderResourceProfile profile ) const
{
    if ( m_profileLimits.contains( profile ) )
        return std::max( 1u, m_profileLimits.value( profile ) );
    return defaultLimitForProfile( profile );
}

void TaskCenter::setResourceProfileLimit( ProviderResourceProfile profile, unsigned int maxConcurrent )
{
    {
        QMutexLocker locker( &m_mutex );
        m_profileLimits[profile] = std::max( 1u, maxConcurrent );
        // 8.0 WP-B dynamic availability: a limit change re-evaluates held
        // candidates immediately instead of waiting for an unrelated task
        // transition (gates only ever DELAY launches, so a lowered limit
        // never preempts running work).
        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();
}

unsigned int TaskCenter::resourceProfileLimit( ProviderResourceProfile profile ) const
{
    QMutexLocker locker( &m_mutex );
    return limitForProfileLocked( profile );
}

void TaskCenter::resetResourceProfileLimits()
{
    QMutexLocker locker( &m_mutex );
    m_profileLimits.clear();
    m_globalConcurrencyLimit = 0;
    m_resourceMonitor = ResourceMonitor{}; // restore default watermark + sampler (ADR 0063)
    // Execution Plane 7.0: restore the multi-dimension gates to their
    // defaults (0 = off) and drop per-task dims so a test reset returns to
    // the master scheduling behavior. Reviewed P2 fix: the purge keeps
    // ACTIVE tasks' entries (their dims are charged into the active sums).
    m_budget2 = TaskResourceBudget2{};
    m_ioHeavyLimit = 0;
    purgeAdmissionCachesForIdleTasksLocked();
    {
        bool ok = false;
        const int envRetries = qEnvironmentVariableIntValue( "SICNU_TASK_MAX_AUTO_RETRIES", &ok );
        m_maxAutoRetries = std::clamp( ok ? envRetries : 1, 0, 3 );
    }
    // Keep the resource-aware budget consistent with the restored watermark so a
    // test reset returns to the default scheduling behavior (perf/architecture goal).
    m_resourceBudget.setBudgetMb( m_resourceMonitor.memoryLimitMb() );
}

void TaskCenter::setGlobalConcurrencyLimit( unsigned int maxConcurrent )
{
    {
        QMutexLocker locker( &m_mutex );
        m_globalConcurrencyLimit = std::max( 1u, maxConcurrent );
        processNextQueuedTasks(); // 8.0 WP-B dynamic availability (see above)
    }
    flushPendingLaunches();
    flushPendingSignals();
}

unsigned int TaskCenter::globalConcurrencyLimit() const
{
    QMutexLocker locker( &m_mutex );
    if ( m_globalConcurrencyLimit > 0 )
        return m_globalConcurrencyLimit;
    return defaultLimitForProfile( ProviderResourceProfile::InProcessThread );
}

void TaskCenter::setMemoryLimitMb( unsigned int mb )
{
    {
        QMutexLocker locker( &m_mutex );
        m_resourceMonitor.setMemoryLimitMb( mb );
        // Keep the resource-aware budget in sync with the watermark (the documented
        // invariant — both gates must use the same cap, perf/architecture goal §7).
        m_resourceBudget.setBudgetMb( mb );
        processNextQueuedTasks(); // 8.0 WP-B dynamic availability
    }
    flushPendingLaunches();
    flushPendingSignals();
}

unsigned int TaskCenter::memoryLimitMb() const
{
    QMutexLocker locker( &m_mutex );
    return m_resourceMonitor.memoryLimitMb();
}

void TaskCenter::setRssSampler( std::function<unsigned int()> sampler )
{
    QMutexLocker locker( &m_mutex );
    m_resourceMonitor.setRssSampler( std::move( sampler ) );
}

void TaskCenter::installDefaultEstimateResolver()
{
    // Registry-backed resolver: read the operator's declared memoryPolicy +
    // executionEstimate via the AtomicAlgorithmRegistry descriptor. This is the
    // ONLY runtime consumer of those fields besides the agent tool catalog.
    // Called from the constructor and re-installed if a test clears the resolver.
    m_resourceBudget.setEstimateResolver(
        []( const std::string &algorithmId ) -> TaskResourceEstimate {
            TaskResourceEstimate est;
            try
            {
                auto adapter =
                    processing::AtomicAlgorithmRegistry::instance().findAdapter( algorithmId );
                if ( !adapter )
                    return est;
                const auto desc = adapter->descriptor();
                const std::string &policy = desc.agentMetadata.memoryPolicy;
                if ( policy == "streaming" )
                    est.memoryClass = TaskMemoryClass::Streaming;
                else if ( policy == "multipass_streaming" )
                    est.memoryClass = TaskMemoryClass::MultiPassStreaming;
                else if ( policy == "external_process" )
                    est.memoryClass = TaskMemoryClass::ExternalProcess;
                else if ( policy == "unsupported_for_large_raster" )
                    est.memoryClass = TaskMemoryClass::UnsupportedForLargeRaster;
                else if ( policy == "full_raster" )
                    est.memoryClass = TaskMemoryClass::FullRaster;
                // else: leave Unknown → FullRaster fallback in resolve().

                const Json::Value &exec = desc.agentMetadata.execution;
                if ( exec.isObject() && exec.isMember( "estimatedRamBytes" ) )
                {
                    const Json::UInt64 bytes = exec["estimatedRamBytes"].asUInt64();
                    est.ramMb = static_cast<unsigned int>( bytes / ( 1024ull * 1024ull ) );
                }
            }
            catch ( ... )
            {
                // Resolver must never throw into the scheduler; fall back to Unknown.
            }
            return est;
        } );
}

void TaskCenter::setResourceBudgetMb( unsigned int mb )
{
    {
        QMutexLocker locker( &m_mutex );
        m_resourceBudget.setBudgetMb( mb );
        processNextQueuedTasks(); // 8.0 WP-B dynamic availability
    }
    flushPendingLaunches();
    flushPendingSignals();
}

unsigned int TaskCenter::resourceBudgetMb() const
{
    QMutexLocker locker( &m_mutex );
    return m_resourceBudget.budgetMb();
}

// --- Execution Plane 7.0: multi-dimension admission -------------------------

void TaskCenter::setTempDiskBudgetMb( unsigned int mb )
{
    {
        QMutexLocker locker( &m_mutex );
        sicnu::SchedulerLimits limits = m_budget2.limits();
        limits.tempDiskMb = mb;
        m_budget2.setLimits( limits );
        processNextQueuedTasks(); // 8.0 WP-B dynamic availability
    }
    flushPendingLaunches();
    flushPendingSignals();
}

unsigned int TaskCenter::tempDiskBudgetMb() const
{
    QMutexLocker locker( &m_mutex );
    return m_budget2.limits().tempDiskMb;
}

void TaskCenter::setVramBudgetMb( unsigned int mb )
{
    {
        QMutexLocker locker( &m_mutex );
        sicnu::SchedulerLimits limits = m_budget2.limits();
        limits.vramMb = mb;
        m_budget2.setLimits( limits );
        processNextQueuedTasks(); // 8.0 WP-B dynamic availability
    }
    flushPendingLaunches();
    flushPendingSignals();
}

unsigned int TaskCenter::vramBudgetMb() const
{
    QMutexLocker locker( &m_mutex );
    return m_budget2.limits().vramMb;
}

void TaskCenter::setIoHeavyLimit( unsigned int maxConcurrent )
{
    {
        QMutexLocker locker( &m_mutex );
        m_ioHeavyLimit = maxConcurrent;
        processNextQueuedTasks(); // 8.0 WP-B dynamic availability
    }
    flushPendingLaunches();
    flushPendingSignals();
}

unsigned int TaskCenter::ioHeavyLimit() const
{
    QMutexLocker locker( &m_mutex );
    return m_ioHeavyLimit;
}

TaskCenter::AdmissionDims TaskCenter::admissionDimsLocked( const AlgorithmTaskInfo &task ) const
{
    if ( m_admissionDimsCache.contains( task.taskId ) )
        return m_admissionDimsCache[ task.taskId ];
    AdmissionDims dims;
    try
    {
        auto adapter =
            processing::AtomicAlgorithmRegistry::instance().findAdapter( task.algorithmId.toStdString() );
        if ( adapter )
        {
            const auto desc = adapter->descriptor();
            dims.ioHeavy = desc.agentMetadata.ioHeavy;
            const Json::Value &execution = desc.agentMetadata.execution;
            if ( execution.isObject() )
            {
                const Json::Value &tempDisk = execution["temporaryDiskBytes"];
                if ( tempDisk.isNumeric() )
                    dims.tempDiskMb = static_cast<unsigned int>(
                        tempDisk.asUInt64() / ( 1024ull * 1024ull ) );
                const Json::Value &vram = execution["estimatedVramBytes"];
                if ( vram.isNumeric() )
                    dims.vramMb = static_cast<unsigned int>(
                        vram.asUInt64() / ( 1024ull * 1024ull ) );
            }
        }
    }
    catch ( ... )
    {
        dims = AdmissionDims{}; // a descriptor failure never gates
    }
    m_admissionDimsCache[ task.taskId ] = dims;
    return dims;
}

void TaskCenter::setEstimateResolver( TaskEstimateResolver resolver )
{
    QMutexLocker locker( &m_mutex );
    if ( resolver )
        m_resourceBudget.setEstimateResolver( std::move( resolver ) );
    else
        installDefaultEstimateResolver();
    // A different resolver may produce different estimates: drop the per-task
    // cache so subsequent passes re-resolve (#702). Reviewed P2 fix: entries
    // of ACTIVE tasks are KEPT — the active-set sums were charged with the
    // values these caches produce, and re-resolving a leaving task under a
    // new resolver would subtract a different value (unsigned underflow ->
    // admission wedged until restart). Active tasks keep their charged
    // identity; only idle entries re-resolve.
    purgeAdmissionCachesForIdleTasksLocked();
}

unsigned int TaskCenter::resolveEstimateMb( const std::string &algorithmId ) const
{
    QMutexLocker locker( &m_mutex );
    return m_resourceBudget.resolve( algorithmId ).ramMb;
}

Json::Value TaskCenter::variantMapToJsonParams( const QVariantMap &params )
{
    Json::Value root( Json::objectValue );
    for ( auto it = params.constBegin(); it != params.constEnd(); ++it )
        root[it.key().toStdString()] = processing::variantToJsonValue( it.value() );
    return root;
}

long TaskCenter::addTaskCompletionCallback( long taskId, TaskCompletionCallback callback )
{
    if ( !callback )
        return 0;
    QMutexLocker locker( &m_mutex );
    auto it = m_tasks.find( taskId );
    if ( it == m_tasks.end() )
        return 0;
    if ( isTerminalStatus( it->status ) )
    {
        // Already terminal: fire inline on the calling thread. Nothing is
        // registered, so there is nothing to remove later.
        AlgorithmTaskInfo snapshot = *it;
        locker.unlock();
        callback( snapshot );
        return 0;
    }
    const long token = m_nextCompletionToken++;
    m_completionCallbacks[taskId][token] = std::move( callback );
    return token;
}

void TaskCenter::removeTaskCompletionCallback( long taskId, long token )
{
    QMutexLocker locker( &m_mutex );
    auto it = m_completionCallbacks.find( taskId );
    if ( it == m_completionCallbacks.end() )
        return;
    it->remove( token );
    if ( it->isEmpty() )
        m_completionCallbacks.erase( it );
}

void TaskCenter::fireTaskCompletionCallbacks( long taskId )
{
    QMap<long, TaskCompletionCallback> toFire;
    AlgorithmTaskInfo snapshot;
    bool deliver = false;
    {
        QMutexLocker locker( &m_mutex );
        auto it = m_completionCallbacks.find( taskId );
        if ( it == m_completionCallbacks.end() )
            return;
        toFire = it.value(); // copy…
        m_completionCallbacks.erase( it ); // …then erase: exactly-once even if a callback re-registers
        const auto infoIt = m_tasks.find( taskId );
        if ( infoIt != m_tasks.end() )
        {
            snapshot = infoIt.value();
            deliver = true;
        }
    }
    if ( !deliver )
        return; // task pruned between erase and snapshot: nothing meaningful to deliver
    for ( const auto &cb : toFire )
        cb( snapshot );
}

TaskAdmissionSnapshot TaskCenter::admissionSnapshot( const QString &algorithmId,
                                                     unsigned int resourceEstimateOverrideMb ) const
{
    TaskAdmissionSnapshot snap;
    QMutexLocker locker( &m_mutex );

    snap.budgetMb = m_resourceBudget.budgetMb();
    snap.globalLimit = m_globalConcurrencyLimit > 0
                         ? m_globalConcurrencyLimit
                         : defaultLimitForProfile( ProviderResourceProfile::InProcessThread );

    const ProviderResourceProfile profile = resolveResourceProfile( algorithmId );
    unsigned int runningInProfile = 0;
    for ( const auto &t : m_tasks )
    {
        // Mirror the admission pass exactly: Dispatching holds a slot until
        // the worker reports, Paused holds its slot, Cancelling holds the
        // worker until the terminal record.
        if ( t.status != TaskStatus::Running && t.status != TaskStatus::Cancelling
             && t.status != TaskStatus::Dispatching && t.status != TaskStatus::Paused )
            continue;
        snap.runningCount += 1;
        snap.runningMb += taskEstimateMbLocked( t );
        if ( t.resourceProfile == profile )
            ++runningInProfile;
    }

    snap.candidateMb = resourceEstimateOverrideMb > 0
                         ? resourceEstimateOverrideMb
                         : m_resourceBudget.resolve( algorithmId.toStdString() ).ramMb;

    if ( snap.runningCount >= snap.globalLimit )
    {
        snap.reason = QStringLiteral( "Global worker slots exhausted (%1/%2)." )
                          .arg( snap.runningCount )
                          .arg( snap.globalLimit );
        return snap;
    }
    const unsigned int profileMax = limitForProfileLocked( profile );
    if ( runningInProfile >= profileMax )
    {
        snap.reason = QStringLiteral( "Profile worker slots exhausted (%1/%2)." ).arg( runningInProfile ).arg( profileMax );
        return snap;
    }
    if ( m_resourceMonitor.memoryPressureHigh() )
    {
        snap.rssHold = true;
        snap.reason = QStringLiteral( "Process RSS at/above the watermark." );
        return snap;
    }
    if ( !m_resourceBudget.canLaunch( snap.runningMb, snap.candidateMb ) )
    {
        snap.reason = QStringLiteral( "RAM budget: projected %1 MiB > budget %2 MiB." )
                          .arg( snap.runningMb + snap.candidateMb )
                          .arg( snap.budgetMb );
        return snap;
    }
    snap.wouldAdmit = true;
    return snap;
}

void TaskCenter::queueTaskAddedLocked( long taskId )
{
    if ( m_tasks.contains( taskId ) )
    {
        m_pendingTaskAdded.append( m_tasks[taskId] );
        m_waitCondition.wakeAll();
    }
}

void TaskCenter::queueTaskUpdatedLocked( long taskId )
{
    if ( m_tasks.contains( taskId ) )
    {
        m_pendingTaskUpdated.append( m_tasks[taskId] );
        m_waitCondition.wakeAll();
    }
}

void TaskCenter::queueTaskLogLocked( long taskId, const QString &message )
{
    m_pendingLogs.append( PendingLog{ taskId, message } );
}

void TaskCenter::flushPendingSignals()
{
    // Drain until empty so nested mutations from slots still surface.
    for ( ;; )
    {
        QList<AlgorithmTaskInfo> added;
        QList<AlgorithmTaskInfo> updated;
        QList<PendingLog> logs;
        {
            QMutexLocker locker( &m_mutex );
            if ( m_pendingTaskAdded.isEmpty() && m_pendingTaskUpdated.isEmpty() && m_pendingLogs.isEmpty() )
                return;
            added.swap( m_pendingTaskAdded );
            updated.swap( m_pendingTaskUpdated );
            logs.swap( m_pendingLogs );
        }
        for ( const auto &info : added )
            emit taskAdded( info );
        for ( const auto &info : updated )
            emit taskUpdated( info );
        for ( const auto &log : logs )
            emit taskLogAdded( log.taskId, log.message );
    }
}

QList<long> TaskCenter::collectTransitiveDescendantsLocked( long rootTaskId ) const
{
    // Invert the parent links once, then a single BFS from the root: O(V+E)
    // with a small map, instead of the previous iterate-until-no-change scan
    // whose worst case was quadratic in live tasks - all while holding
    // m_mutex on every failure/cancel path.
    QHash<long, QVector<long>> childrenOf;
    for ( auto it = m_tasks.begin(); it != m_tasks.end(); ++it )
    {
        if ( isTerminalStatus( it.value().status ) )
            continue;
        for ( long parentId : it.value().parentTaskIds )
            childrenOf[parentId].append( it.key() );
    }

    QList<long> descendants;
    QSet<long> visited;
    visited.insert( rootTaskId );
    QVector<long> frontier{ rootTaskId };
    while ( !frontier.isEmpty() )
    {
        const long current = frontier.takeLast();
        for ( long childId : childrenOf.value( current ) )
        {
            if ( visited.contains( childId ) )
                continue;
            visited.insert( childId );
            descendants.append( childId );
            frontier.append( childId );
        }
    }
    return descendants;
}

QVariant TaskCenter::substituteVariantRecursive( const QVariant &value,
                                                const std::function<std::string( const sicnu::workflow::PlaceholderRef & )> &resolver,
                                                bool *changed )
{
    if ( value.typeId() == QMetaType::QString )
    {
        const std::string raw = value.toString().toStdString();
        const std::string sub = sicnu::workflow::substitutePlaceholders( raw, resolver );
        if ( sub == raw )
            return value; // no substitution: return the shared original, no detach
        if ( changed )
            *changed = true;
        return QString::fromStdString( sub );
    }
    if ( value.typeId() == QMetaType::QVariantList )
    {
        QVariantList list = value.toList();
        bool mutated = false;
        for ( int i = 0; i < list.size(); ++i )
        {
            const QVariant substituted = substituteVariantRecursive( list[i], resolver, changed );
            if ( substituted != list[i] )
            {
                list[i] = substituted;
                mutated = true;
            }
        }
        return mutated ? list : value; // unchanged tree: keep the COW-shared original
    }
    if ( value.typeId() == QMetaType::QVariantMap )
    {
        QVariantMap map = value.toMap();
        bool mutated = false;
        for ( auto it = map.begin(); it != map.end(); ++it )
        {
            const QVariant substituted = substituteVariantRecursive( it.value(), resolver, changed );
            if ( substituted != it.value() )
            {
                it.value() = substituted;
                mutated = true;
            }
        }
        return mutated ? map : value;
    }
    return value;
}

void TaskCenter::applyPlaceholdersForTask( long taskId )
{
    if ( !m_tasks.contains( taskId ) )
        return;

    auto resolveRef = [&]( const sicnu::workflow::PlaceholderRef &ref ) -> std::string {
        for ( long parentId : m_tasks[taskId].parentTaskIds )
        {
            if ( !m_tasks.contains( parentId ) )
                continue;

            const QString parentStepId = m_tasks[parentId].stepId;
            bool isMatch = false;
            if ( !parentStepId.isEmpty() && ref.stepId == parentStepId.toStdString() )
            {
                isMatch = true;
            }
            else if ( ref.parentTaskId == parentId || ref.isParentKeyword )
            {
                isMatch = true;
            }

            if ( isMatch )
            {
                // Port-aware resolution shared with the resume path (#727):
                // exact resultPayload[portName] -> canonical output ->
                // case-insensitive port scan; empty leaves the placeholder
                // unresolved on both paths.
                const std::string resolved = sicnu::workflow::resolvePlaceholderPort(
                    m_tasks[parentId].resultPayload,
                    m_tasks[parentId].outputLayerPath.toStdString(),
                    ref.portName );
                if ( !resolved.empty() )
                    return resolved;
                return ref.rawRef;
            }
        }
        return ref.rawRef;
    };

    QVariantMap &pMap = m_tasks[taskId].parameterMap;
    bool paramsChanged = false;
    for ( auto pIt = pMap.begin(); pIt != pMap.end(); ++pIt )
    {
        const QVariant substituted = substituteVariantRecursive( pIt.value(), resolveRef, &paramsChanged );
        if ( substituted != pIt.value() )
            pIt.value() = substituted;
    }

    // Only re-serialize the seeded JobRequest when a substitution actually
    // changed the parameters: the launch path re-serializes on its own, so
    // paying the map->JSON conversion on every scheduling pass for unchanged
    // parameters is pure overhead.
    if ( m_tasks[taskId].hasJobRequest && paramsChanged )
    {
        sicnu::jobs::JobRequest &req = m_tasks[taskId].jobRequest;
        req.params = variantMapToJsonParams( pMap );
    }

    // Refresh detected output path after substitution
    const QString detectedPath = findOutputPathInParams( pMap );
    if ( !detectedPath.isEmpty() )
        m_tasks[taskId].outputLayerPath = detectedPath;
}


void TaskCenter::updatePipelineForTaskLocked( long taskId )
{
    if ( !m_tasks.contains( taskId ) )
        return;

    const long pipelineId = m_tasks[taskId].pipelineId;
    if ( pipelineId < 0 || !m_pipelines.contains( pipelineId ) )
        return;

    PipelineExecutionInfo &pipe = m_pipelines[pipelineId];
    const QString stepId = m_tasks[taskId].stepId;
    if ( !stepId.isEmpty() )
        pipe.stepStatuses[stepId.toStdString()] = m_tasks[taskId].status;

    if ( m_tasks[taskId].status == TaskStatus::Failed
         || m_tasks[taskId].status == TaskStatus::Canceled )
    {
        pipe.isFailed = true;
        if ( pipe.errorMessage.isEmpty() )
            pipe.errorMessage = m_tasks[taskId].errorMessage;
    }

    bool allTerminal = !pipe.stepToTaskId.isEmpty();
    bool anyFailed = false;
    for ( auto it = pipe.stepToTaskId.begin(); it != pipe.stepToTaskId.end(); ++it )
    {
        if ( !m_tasks.contains( it.value() ) )
        {
            allTerminal = false;
            break;
        }
        const TaskStatus st = m_tasks[it.value()].status;
        if ( !isTerminalStatus( st ) )
        {
            allTerminal = false;
            break;
        }
        if ( st == TaskStatus::Failed || st == TaskStatus::Canceled )
            anyFailed = true;
    }

    if ( allTerminal )
    {
        pipe.isCompleted = true;
        pipe.isFailed = anyFailed;
    }
}

long TaskCenter::enqueueTask( const QString &algorithmId,
                              const QVariantMap &params,
                              bool autoLoad,
                              TaskPriority priority,
                              const QList<long> &parentTaskIds,
                              bool autoDispatch,
                              unsigned int resourceEstimateOverrideMb,
                              const QString &source )
{
    if ( m_isShuttingDown.load() )
        return -1; // no new work after shutdown (#684)
    sicnu::data::DataManager *catalogForWarm = nullptr;
    // 8.0 WP-F (review P1 fix): the remote-identity probe is network I/O —
    // perform it HERE, lock-free on the submitting thread, so the collector's
    // resolver consult under m_mutex resolves from the warm session cache.
    {
        QMutexLocker catalogLocker( &m_mutex );
        catalogForWarm = m_catalog;
    }
    sicnu::temporal::warmExecutionIdentityCache( catalogForWarm, params );
    long id = -1;
    {
        QMutexLocker locker( &m_mutex );
        // Recheck under the lock: shutdown()'s finalization pass could have
        // completed between the atomic read above and here, and a task
        // inserted afterwards would strand in Queued forever with its
        // completion callbacks never firing (review P1).
        if ( m_isShuttingDown.load() )
            return -1;
        id = m_nextTaskId++;
        AlgorithmTaskInfo info;
        info.taskId = id;
        info.algorithmId = algorithmId;
        info.priority = priority;
        info.parentTaskIds = parentTaskIds;
        info.autoDispatch = autoDispatch;
        info.resourceProfile = resolveResourceProfile( algorithmId );
        info.resourceEstimateOverrideMb = resourceEstimateOverrideMb;
        info.source = source;

        auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( algorithmId.toStdString() );
        if ( adapter )
            info.algorithmName = QString::fromStdString( adapter->descriptor().displayName );
        else
            info.algorithmName = algorithmId;

        info.status = TaskStatus::Queued;
        info.progressPercentage = 0.0;
        info.startTime = QDateTime::currentDateTimeUtc();
        info.parameterMap = params;
        info.autoLoadLayer = autoLoad;

        info.outputLayerPath = findOutputPathInParams( params );

        info.logBuffer.append( QString( QStringLiteral( "[%1] Task queued with priority %2." ) )
                                 .arg( info.startTime.toString( QStringLiteral( "yyyy-MM-dd hh:mm:ss" ) ) )
                                 .arg( static_cast<int>( priority ) ) );

        m_tasks[id] = info;
        // 8.0 WP-A: derived admission state for the new task (children index,
        // incomplete-parent counts, ready-heap / manual-queue registration).
        registerParentLinksLocked( m_tasks[id] );
        if ( m_tasks[id].autoDispatch )
            pushReadyCandidateLocked( id );
        else if ( parentsSatisfiedLocked( m_tasks[id] ) )
            m_manualQueued.append( id );
        queueTaskAddedLocked( id );

        // Submission-time execution fingerprint (#726): computed ONCE here on
        // the submitting thread (catalog affinity holds), so downstream /
        // re-admitted tasks never fingerprint on JobEngine worker threads. A
        // placeholder reference to a parent task chains the parent's
        // fingerprint as the input identity.
        const QList<long> fingerprintParents = info.parentTaskIds;
        UpstreamResolver resolver =
            [this, fingerprintParents]( const sicnu::workflow::PlaceholderRef &ref,
                                        const QString & ) -> UpstreamResolution {
            for ( long parentId : fingerprintParents )
            {
                const auto parentIt = m_tasks.constFind( parentId );
                if ( parentIt == m_tasks.constEnd() )
                    continue;
                const AlgorithmTaskInfo &parent = parentIt.value();
                bool isMatch = false;
                if ( !parent.stepId.isEmpty() && ref.stepId == parent.stepId.toStdString() )
                    isMatch = true;
                else if ( ref.parentTaskId == parentId || ref.isParentKeyword )
                    isMatch = true;
                if ( !isMatch )
                    continue;
                UpstreamResolution upstream;
                upstream.producerTaskId = parentId;
                upstream.declaredOutputPath = findOutputPathInParams( parent.parameterMap );
                if ( upstream.declaredOutputPath.isEmpty() )
                    upstream.declaredOutputPath = parent.outputLayerPath;
                return upstream;
            }
            return {};
        };
        computeAndRecordSubmissionFingerprintLocked( id, resolver );

        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();
    return id;
}

long TaskCenter::submitJob( const sicnu::jobs::JobRequest &request )
{
    return submitJobImpl( request, {}, {}, true, TaskPriority::Normal, {} );
}

long TaskCenter::submitJob( const sicnu::jobs::JobRequest &request,
                            JobExecutor executor,
                            CancelHook onCancel,
                            bool autoLoad,
                            TaskPriority priority,
                            const QList<long> &parentTaskIds )
{
    return submitJobImpl( request, std::move( executor ), std::move( onCancel ), autoLoad, priority,
                          parentTaskIds );
}

long TaskCenter::submitJobImpl( const sicnu::jobs::JobRequest &request,
                                JobExecutor executor,
                                CancelHook onCancel,
                                bool autoLoad,
                                TaskPriority priority,
                                const QList<long> &parentTaskIds )
{
    ensureJobListener();

    if ( m_isShuttingDown.load() )
        return -1; // no new work after shutdown (#684)

    QVariantMap params = sicnu::processing::jsonParamsToVariantMap( request.params );

    // ONE admission path (#683/#686): submitJob used to bypass the gated
    // auto-dispatch pipeline and hand the job straight to JobEngine (marking
    // the task Running before any worker existed, with no cancel-during-submit
    // recheck). Stage through the same queue as everyone else: the task is
    // enqueued tracking-only, then armed with its request/executor under the
    // lock and admitted by processNextQueuedTasks (slots / RSS / RAM budget /
    // priority). Executors never run before admission because autoDispatch is
    // only flipped inside the same critical section.
    const long taskId = enqueueTask( QString::fromStdString( request.algorithmId ), params, autoLoad,
                                     priority, parentTaskIds, false, 0,
                                     QString::fromStdString( request.source ) );
    if ( taskId < 0 )
        return -1;

    {
        QMutexLocker locker( &m_mutex );
        auto it = m_tasks.find( taskId );
        if ( it == m_tasks.end() )
            return -1; // cleared concurrently (shutdown)
        if ( m_isShuttingDown.load() )
        {
            // Shutdown slipped between enqueue and arming: no launch will
            // ever happen. Finalize inline so callers/waiters resolve.
            setTaskStatusLocked( *it, TaskStatus::Canceled );
            it->errorMessage = QStringLiteral( "Canceled: application is shutting down" );
            it->endTime = QDateTime::currentDateTimeUtc();
            queueTaskUpdatedLocked( taskId );
        }
        else
        {
            it->jobRequest = request;
            it->jobRequest.clientTag = "task:" + std::to_string( taskId );
            it->hasJobRequest = true;
            it->jobExecutor = std::move( executor );
            it->jobCancelHook = std::move( onCancel );
            it->autoDispatch = true;
            // Reviewed P1 fix: arming flips the task OUT of the legacy
            // manual-placeholder list (enqueue registered it there because
            // autoDispatch was still false) — a stale entry would be fed to
            // the legacy per-pass placeholder loop forever.
            m_manualQueued.removeOne( taskId );
            // 8.0 WP-A: the task just became a launch candidate.
            pushReadyCandidateLocked( taskId );
            processNextQueuedTasks();
        }
    }
    flushPendingLaunches();
    flushPendingSignals();
    if ( m_isShuttingDown.load() )
        fireTaskCompletionCallbacks( taskId );
    return taskId;
}

void TaskCenter::ensureJobListener()
{
    // Single replacing slot: re-claim it on every submit so a test-side reset
    // (shutdownForTests / EngineGuard) cannot permanently detach bookkeeping.
    sicnu::jobs::JobEngine::instance().setListener(
      [this]( const sicnu::jobs::JobRecord &record ) { onJobRecord( record ); } );
}

void TaskCenter::onJobRecord( const sicnu::jobs::JobRecord &record )
{
    long taskId = -1;
    {
        QMutexLocker locker( &m_mutex );
        auto it = m_taskByJobId.find( record.id );
        if ( it != m_taskByJobId.end() )
        {
            taskId = it.value();
        }
        else if ( !record.request.clientTag.empty() )
        {
            // Rapid completion race guard (#799): recover taskId from clientTag if not yet mapped
            std::string tag = record.request.clientTag;
            if ( tag.rfind( "task:", 0 ) == 0 )
                tag = tag.substr( 5 );
            bool ok = false;
            long parsedId = QString::fromStdString( tag ).toLong( &ok );
            if ( ok && m_tasks.contains( parsedId ) )
            {
                taskId = parsedId;
                m_taskByJobId[record.id] = taskId;
                m_tasks[taskId].jobId = record.id;
            }
        }
        if ( taskId == -1 )
            return; // job not submitted through Task Center (e.g. direct engine use in tests)
    }
    processJobRecord( taskId, record );
}

void TaskCenter::processJobRecord( long taskId, const sicnu::jobs::JobRecord &record )
{
    // Same delta semantics as the retired watcher thread (ADR 0051): forward
    // changed progress, new log lines, and exactly one terminal transition.
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) || isTerminalStatus( m_tasks[taskId].status ) )
            return; // task gone, or stale/duplicate record (catch-up vs listener)
    }

    // The engine's Running record is the ONLY sanctioned source for the
    // caller-facing Running status: a worker actually picked the job
    // (#686). Dispatching → Running here; startTime becomes the real
    // worker start, not the enqueue timestamp.
    if ( record.state == sicnu::jobs::JobState::Running )
    {
        bool flipped = false;
        {
            QMutexLocker locker( &m_mutex );
            auto it = m_tasks.find( taskId );
            if ( it != m_tasks.end()
                 && ( it->status == TaskStatus::Queued || it->status == TaskStatus::WaitingResource
                      || it->status == TaskStatus::Dispatching ) )
            {
                setTaskStatusLocked( *it, TaskStatus::Running );
                if ( record.startedAtMs > 0 )
                    it->startTime = QDateTime::fromMSecsSinceEpoch( record.startedAtMs, Qt::UTC );
                updatePipelineForTaskLocked( taskId );
                queueTaskUpdatedLocked( taskId );
                flipped = true;
            }
        }
        if ( flipped )
            flushPendingSignals(); // deliver the honest Running state promptly
    }

    if ( record.progress >= 0.0 )
    {
        bool changed = false;
        {
            QMutexLocker locker( &m_mutex );
            const double last = m_lastForwardedProgress.value( taskId, -2.0 );
            if ( record.progress != last )
            {
                m_lastForwardedProgress[taskId] = record.progress;
                changed = true;
            }
        }
        if ( changed )
            updateTaskProgress( taskId, record.progress );
    }

    // Copy the not-yet-forwarded slice out under the lock and append it
    // outside: the old check-then-act let two racing updaters (listener
    // record vs. submit-time catch-up record) both read the same `forwarded`
    // count and append the same lines twice (#616).
    std::vector<QString> newLines;
    {
        QMutexLocker locker( &m_mutex );
        const std::size_t forwarded = m_forwardedLogCounts.value( taskId, 0 );
        if ( record.logLinesOffset > 0 )
        {
            // Delta record from the engine (#638): logLines holds only the
            // lines appended since the previous notify, starting at engine
            // index (logLinesOffset - 1); the engine-side cursor guarantees
            // each line is shipped exactly once BY THE ENGINE. The
            // submit-time catch-up snapshot (cumulative) races this consumer
            // from the submitting thread, so skip any delta line whose
            // engine index was already forwarded instead of blindly
            // appending - the two producers must stay idempotent.
            const std::size_t first = record.logLinesOffset - 1;
            std::size_t seen = m_forwardedLogCounts.value( taskId, 0 );
            for ( std::size_t i = 0; i < record.logLines.size(); ++i )
            {
                const std::size_t engineIndex = first + i;
                if ( engineIndex < seen )
                    continue;
                newLines.push_back( QString::fromStdString( record.logLines[i].text ) );
                seen = engineIndex + 1;
            }
            m_forwardedLogCounts[taskId] = seen;
        }
        else if ( record.logLines.size() > forwarded )
        {
            for ( std::size_t i = forwarded; i < record.logLines.size(); ++i )
                newLines.push_back( QString::fromStdString( record.logLines[i].text ) );
            m_forwardedLogCounts[taskId] = record.logLines.size();
        }
    }
    for ( const QString &line : newLines )
        appendTaskLog( taskId, line );

    if ( record.state == sicnu::jobs::JobState::Succeeded )
    {
        QVariantMap results;
        for ( const auto &name : record.result.getMemberNames() )
        {
            if ( record.result[name].isString() )
                results.insert( QString::fromStdString( name ),
                                QString::fromStdString( record.result[name].asString() ) );
        }
        markTaskCompleted( taskId, results, record.result );
    }
    else if ( record.state == sicnu::jobs::JobState::Cancelled )
    {
        markTaskCanceled( taskId );
    }
    else if ( record.state == sicnu::jobs::JobState::Failed )
    {
        markTaskFailed( taskId, QString::fromStdString( record.error ) );
    }
    else
    {
        return; // Queued/Running records keep the per-task dispatch state
    }

    // Terminal job: release per-task dedup/dispatch state.
    QMutexLocker locker( &m_mutex );
    m_forwardedLogCounts.remove( taskId );
    m_lastForwardedProgress.remove( taskId );
    m_taskByJobId.remove( record.id );
}

unsigned int TaskCenter::taskEstimateMbLocked( const AlgorithmTaskInfo &task ) const
{
    if ( task.resourceEstimateOverrideMb > 0 )
        return task.resourceEstimateOverrideMb;
    // #702: the registry-backed resolver takes the registry mutex (and reads
    // descriptor JSON) — re-running it for every active task on every
    // scheduling pass under m_mutex was pure repeat work. An algorithm's
    // estimate never changes for a given task, so cache it per task id.
    const auto cached = m_estimateMbCache.constFind( task.taskId );
    if ( cached != m_estimateMbCache.constEnd() )
        return cached.value();
    const unsigned int mb = m_resourceBudget.resolve( task.algorithmId.toStdString() ).ramMb;
    m_estimateMbCache.insert( task.taskId, mb );
    return mb;
}

// --- 8.0 WP-A: incremental admission bookkeeping ---------------------------

bool TaskCenter::isActiveStatus( TaskStatus status )
{
    return status == TaskStatus::Running || status == TaskStatus::Cancelling
           || status == TaskStatus::Dispatching || status == TaskStatus::Paused;
}

void TaskCenter::setTaskStatusLocked( AlgorithmTaskInfo &task, TaskStatus newStatus )
{
    if ( task.status == newStatus )
    {
        // Nothing to re-account, but keep the manual-queue/heap invariants
        // untouched: same-status writes are true no-ops.
        return;
    }
    const bool wasActive = isActiveStatus( task.status );
    const bool wasReadyLike = task.status == TaskStatus::Queued
                              || task.status == TaskStatus::WaitingResource;
    const bool willBeActive = isActiveStatus( newStatus );
    const bool willBeReadyLike = newStatus == TaskStatus::Queued
                                 || newStatus == TaskStatus::WaitingResource;

    if ( wasActive && !willBeActive )
    {
        // Leave: subtract exactly what entering added (the estimates/dims are
        // immutable per task, so the unsigned sums can never underflow).
        --m_active.total;
        m_active.ramMb -= taskEstimateMbLocked( task );
        if ( task.isolatedRoute )
            --m_active.isolated;
        const AdmissionDims dims = admissionDimsLocked( task );
        m_active.usage2.tempDiskMb -= dims.tempDiskMb;
        m_active.usage2.vramMb -= dims.vramMb;
        if ( dims.ioHeavy )
            --m_active.ioHeavy;
        auto &slot = m_active.byProfile[task.resourceProfile];
        if ( slot > 0 )
            --slot;
    }
    else if ( !wasActive && willBeActive )
    {
        ++m_active.total;
        m_active.ramMb += taskEstimateMbLocked( task );
        if ( task.isolatedRoute )
            ++m_active.isolated;
        const AdmissionDims dims = admissionDimsLocked( task );
        m_active.usage2.tempDiskMb += dims.tempDiskMb;
        m_active.usage2.vramMb += dims.vramMb;
        if ( dims.ioHeavy )
            ++m_active.ioHeavy;
        ++m_active.byProfile[task.resourceProfile];
    }
    task.status = newStatus;

    // Leaving a queued-like status also retires the task's heap entries and
    // its legacy manual-queue registration.
    if ( wasReadyLike && !willBeReadyLike )
        dropReadyCandidateLocked( task.taskId );
    if ( task.autoDispatch )
        return;
    // Manual (non-autoDispatch) task tracking for the legacy per-pass
    // placeholder application.
    if ( willBeReadyLike && !wasReadyLike )
    {
        if ( parentsSatisfiedLocked( task ) && !m_manualQueued.contains( task.taskId ) )
            m_manualQueued.append( task.taskId );
    }
    else if ( !willBeReadyLike && wasReadyLike )
    {
        m_manualQueued.removeAll( task.taskId );
    }
}

void TaskCenter::forgetDerivedTaskStateLocked( long taskId )
{
    dropReadyCandidateLocked( taskId );
    m_incompleteParentCount.remove( taskId );
    m_manualQueued.removeAll( taskId );
}

/// Reviewed P2 fix: drops estimate/dims cache entries ONLY for tasks that
/// are not in the active set. The active-set sums were charged from these
/// caches at enter time; re-resolving an active task's values (leave
/// transition) under a swapped resolver would subtract a different amount
/// and wrap the unsigned admission sums. m_mutex held.
void TaskCenter::purgeAdmissionCachesForIdleTasksLocked()
{
    auto isIdle = [this]( long taskId ) {
        const auto it = m_tasks.constFind( taskId );
        return it == m_tasks.constEnd() || !isActiveStatus( it->status );
    };
    for ( auto it = m_estimateMbCache.begin(); it != m_estimateMbCache.end(); )
    {
        if ( isIdle( it.key() ) )
            it = m_estimateMbCache.erase( it );
        else
            ++it;
    }
    for ( auto it = m_admissionDimsCache.begin(); it != m_admissionDimsCache.end(); )
    {
        if ( isIdle( it.key() ) )
            it = m_admissionDimsCache.erase( it );
        else
            ++it;
    }
}

void TaskCenter::registerParentLinksLocked( const AlgorithmTaskInfo &task )
{
    int incomplete = 0;
    for ( long parentId : task.parentTaskIds )
    {
        const auto it = m_tasks.constFind( parentId );
        if ( it == m_tasks.constEnd() )
            continue; // a missing parent counts as satisfied (eligibility rule)
        m_children.insert( parentId, task.taskId );
        if ( it->status != TaskStatus::Completed )
            ++incomplete;
    }
    if ( incomplete > 0 )
        m_incompleteParentCount[task.taskId] = incomplete;
}

void TaskCenter::promoteChildrenOfLocked( long parentTaskId )
{
    const QList<long> children = m_children.values( parentTaskId );
    for ( long childId : children )
    {
        const auto countIt = m_incompleteParentCount.find( childId );
        if ( countIt == m_incompleteParentCount.end() )
            continue; // already satisfied via another edge/pass
        if ( countIt.value() > 0 )
            --countIt.value();
        if ( countIt.value() > 0 )
            continue; // other parents still outstanding
        m_incompleteParentCount.erase( countIt );
        const auto taskIt = m_tasks.constFind( childId );
        if ( taskIt == m_tasks.constEnd() )
            continue;
        if ( taskIt->autoDispatch )
        {
            pushReadyCandidateLocked( childId );
        }
        else if ( taskIt->status == TaskStatus::Queued
                  && !m_manualQueued.contains( childId ) )
        {
            m_manualQueued.append( childId );
        }
    }
    // The parent's edges are consumed: a terminal parent never transitions
    // again, and prune/clear paths drop their own edges.
    m_children.remove( parentTaskId );
}

bool TaskCenter::parentsSatisfiedLocked( const AlgorithmTaskInfo &task ) const
{
    for ( long parentId : task.parentTaskIds )
    {
        const auto it = m_tasks.constFind( parentId );
        if ( it != m_tasks.constEnd() && it->status != TaskStatus::Completed )
            return false;
    }
    return true;
}

bool TaskCenter::isLaunchCandidateLocked( const AlgorithmTaskInfo &task ) const
{
    return ( task.status == TaskStatus::Queued || task.status == TaskStatus::WaitingResource )
           && task.autoDispatch
           && task.jobId.empty()
           && parentsSatisfiedLocked( task );
}

void TaskCenter::pushReadyCandidateLocked( long taskId )
{
    const auto it = m_tasks.constFind( taskId );
    if ( it == m_tasks.constEnd() || !isLaunchCandidateLocked( *it ) )
        return;
    const unsigned long long serial = ++m_nextReadySerial;
    m_readySerial[taskId] = serial;
    m_readyHeap.push( ReadyEntry{ 0, static_cast<int>( it->priority ), taskId, serial } );
}

void TaskCenter::dropReadyCandidateLocked( long taskId )
{
    m_readySerial.remove( taskId );
}

void TaskCenter::processNextQueuedTasks()
{
    // Called with m_mutex held. Only stages work; callers must flushPendingLaunches() outside the lock.
    if ( m_isShuttingDown.load() )
        return; // shutdown stages nothing new (#684)
    const unsigned int globalMax = m_globalConcurrencyLimit > 0
                                     ? m_globalConcurrencyLimit
                                     : defaultLimitForProfile( ProviderResourceProfile::InProcessThread );

    // 8.0 WP-A: the active-set sums (slots, RAM, isolated, io-heavy, temp
    // disk, VRAM) are maintained incrementally by setTaskStatusLocked and the
    // candidate order lives in m_readyHeap — this pass costs
    // O(examined · log n) instead of a full task-map rescan + re-sort. A pass
    // examines at most the budget below; candidates held by per-candidate
    // gates rotate FIFO across passes, so the cap never strands work.
    const unsigned int scanBudget = std::max( kAdmissionScanFloor, 4u * globalMax );
    const unsigned long long passEpoch = ++m_admissionPass;
    unsigned int scanned = 0;
    unsigned int launchedCount = 0;
    QString globalHoldReason;
    std::vector<ReadyEntry> requeue;
    QList<long> resourceBlockedIds;
    QList<long> dagRegressedIds;

    while ( m_active.total < globalMax && !m_readyHeap.empty() && scanned < scanBudget )
    {
        ReadyEntry entry = m_readyHeap.top();
        m_readyHeap.pop();

        // Lazy invalidation: superseded entries (dispatched, canceled,
        // retried, re-promoted) are dropped on pop, never erased eagerly.
        const auto serialIt = m_readySerial.constFind( entry.taskId );
        if ( serialIt == m_readySerial.constEnd() || *serialIt != entry.serial )
            continue;

        const auto taskIt = m_tasks.find( entry.taskId );
        if ( taskIt == m_tasks.end() )
        {
            m_readySerial.remove( entry.taskId );
            continue;
        }
        AlgorithmTaskInfo &task = taskIt.value();

        if ( !isLaunchCandidateLocked( task ) )
        {
            m_readySerial.remove( entry.taskId );
            // A candidate held back because its parents regressed (e.g. an
            // upstream step was retried) returns to plain Queued so the
            // status stays truthful (legacy flip-back).
            if ( task.status == TaskStatus::WaitingResource )
                dagRegressedIds.append( entry.taskId );
            continue;
        }

        // ADR 0063: hold all launches when the process RSS is at/above the
        // watermark. Memory pressure is global, so stop examining (remaining
        // candidates cannot run either). The popped candidate re-queues
        // unchanged (fresh stays fresh: strict priority order), and when
        // nothing is running a bounded timer re-arms the pass.
        if ( m_resourceMonitor.memoryPressureHigh() )
        {
            globalHoldReason = QStringLiteral( "Waiting for memory: process RSS at/above the watermark." );
            resourceBlockedIds.append( entry.taskId );
            requeue.push_back( entry );
            break;
        }

        const ProviderResourceProfile profile = task.resourceProfile;
        const unsigned int profileMax = limitForProfileLocked( profile );
        if ( m_active.byProfile.value( profile, 0u ) >= profileMax )
        {
            entry.epoch = passEpoch;
            requeue.push_back( entry );
            resourceBlockedIds.append( entry.taskId );
            ++scanned;
            continue; // another profile may still launch
        }

        // Execution Plane 7.0: isolated-worker route selection. Tasks with a
        // caller-supplied executor keep it; everything routable that the
        // current mode selects runs in an isolated sicnu_worker process.
        // NOTE: the predicate deliberately does NOT consult hasJobRequest —
        // after a transient auto-retry the flag is already set, and routing
        // must re-engage (a retry silently falling back in-process would
        // break the fail-closed contract).
        const bool isolateRoute =
            !task.jobExecutor && processing::shouldRunIsolated( task.algorithmId );
        if ( isolateRoute
             && m_active.isolated >= static_cast<unsigned int>( std::max( 1, processing::isolatedJobLimit() ) ) )
        {
            entry.epoch = passEpoch;
            requeue.push_back( entry );
            resourceBlockedIds.append( entry.taskId );
            ++scanned;
            continue;
        }

        // Resource-aware gate (perf/architecture goal 2026-08-08): hold the
        // launch when projected (running + candidate) RAM exceeds the budget,
        // UNLESS nothing at all is running (global never-starve: a wrong or
        // missing estimate must not permanently block all work). A budget of
        // 0 disables this gate (legacy behavior). `continue` (not break): a
        // later, lighter eligible task may still fit within the budget.
        const unsigned int candidateMb = taskEstimateMbLocked( task );
        if ( m_active.total > 0 && !m_resourceBudget.canLaunch( m_active.ramMb, candidateMb ) )
        {
            entry.epoch = passEpoch;
            requeue.push_back( entry );
            resourceBlockedIds.append( entry.taskId );
            ++scanned;
            continue;
        }

        // Execution Plane 7.0: multi-dimension admission (TaskResourceBudget2)
        // over descriptor-declared temporary disk and VRAM. A dimension cap
        // of 0 disables the gate; never-starve mirrors the RAM gate (when
        // nothing is running, a declared estimate never blocks the only
        // candidate).
        const AdmissionDims candidateDims = admissionDimsLocked( task );
        if ( m_active.total > 0
             && ( candidateDims.tempDiskMb > 0 || candidateDims.vramMb > 0 ) )
        {
            sicnu::ResourceRequest candidateRequest;
            candidateRequest.tempDiskMb = candidateDims.tempDiskMb;
            candidateRequest.vramMb = candidateDims.vramMb;
            if ( !m_budget2.canLaunch( m_active.usage2, candidateRequest,
                                       std::chrono::steady_clock::now() ) )
            {
                entry.epoch = passEpoch;
                requeue.push_back( entry );
                resourceBlockedIds.append( entry.taskId );
                ++scanned;
                continue;
            }
        }

        // Execution Plane 7.0: io-heavy concurrency gate (descriptor-declared
        // ioHeavy kernels; 0 = off). Delays only, like the isolated-slot gate.
        if ( candidateDims.ioHeavy && m_ioHeavyLimit > 0 && m_active.ioHeavy >= m_ioHeavyLimit )
        {
            entry.epoch = passEpoch;
            requeue.push_back( entry );
            resourceBlockedIds.append( entry.taskId );
            ++scanned;
            continue;
        }

        // Launch staging. Placeholder substitution + dispatch-fingerprint
        // verification (#726/#766) run HERE, exactly once per task, after
        // every gate passed: the gates read only resource identity (never
        // parameters), so per-candidate per-pass re-substitution — the old
        // pass's dominant per-candidate cost — was pure overhead.
        applyPlaceholdersForTask( entry.taskId );

        // Dispatch-time verification (#726): the submission-time fingerprint
        // was computed over statically-resolved parameters; if the real
        // substitution diverged, the fingerprint no longer describes this
        // execution — drop it (conservative miss). Pure in-memory comparison:
        // safe on worker threads, no catalog access.
        verifyDispatchFingerprintLocked( entry.taskId );

        // 8.0 WP-A (regression fix): the isolated flag MUST be set BEFORE the
        // status transition — the enter-transition charges m_active.isolated
        // from task.isolatedRoute, and charging late would underflow the
        // counter at the leave transition (blocking every later routed task).
        task.isolatedRoute = isolateRoute;
        setTaskStatusLocked( task, TaskStatus::Dispatching );
        task.logBuffer.append(
          QString( QStringLiteral( "[%1] Dispatching to JobEngine (profile=%2)." ) )
            .arg( QDateTime::currentDateTimeUtc().toString( QStringLiteral( "hh:mm:ss" ) ) )
            .arg( static_cast<int>( profile ) ) );
        updatePipelineForTaskLocked( entry.taskId );

        PendingLaunch launch;
        launch.taskId = entry.taskId;
        launch.algorithmId = task.algorithmId;
        launch.request.algorithmId = task.algorithmId.toStdString();
        launch.request.title = task.algorithmName.toStdString();
        launch.request.source = task.source.isEmpty()
                                  ? ( task.pipelineId >= 0 ? "pipeline" : "task_center" )
                                  : task.source.toStdString();
        launch.request.params = variantMapToJsonParams( task.parameterMap );
        launch.request.priority = static_cast<int>( task.priority );
        launch.request.clientTag = "task:" + std::to_string( entry.taskId );
        // COPIED, not moved: a transient auto-retry re-stages this task and
        // must re-install the caller's cancel hook (a moved-out hook would
        // leave the retry without its cancellation mechanism).
        launch.onCancel = task.jobCancelHook;
        if ( task.hasJobRequest && task.jobExecutor )
        {
            launch.executor = task.jobExecutor;
            launch.hasExecutor = true;
            launch.request = task.jobRequest;
            launch.request.clientTag = "task:" + std::to_string( entry.taskId );
            launch.request.params = variantMapToJsonParams( task.parameterMap );
            launch.request.priority = static_cast<int>( task.priority );
        }
        else
        {
            task.jobRequest = launch.request;
            task.hasJobRequest = true;
            if ( isolateRoute )
            {
                // The pool itself is started outside m_mutex at flush time;
                // staging only records the route (already reflected in
                // task.isolatedRoute above for the admission accounting).
                launch.executor = processing::makeIsolatedWorkerExecutor( task.algorithmId );
                launch.hasExecutor = true;
                launch.isolated = true;
                task.logBuffer.append(
                    QStringLiteral( "[%1] Routed to an isolated worker process." )
                        .arg( QDateTime::currentDateTimeUtc().toString( QStringLiteral( "hh:mm:ss" ) ) ) );
            }
        }

        queueTaskUpdatedLocked( entry.taskId );
        m_pendingLaunches.append( std::move( launch ) );
        m_readySerial.remove( entry.taskId ); // consumed: staging owns the task now
        ++scanned;
        ++launchedCount;
    }

    for ( const ReadyEntry &e : requeue )
        m_readyHeap.push( e );

    // Admission outcome bookkeeping: examined candidates that did not launch
    // this pass are waiting on resources (worker slots, RSS watermark, RAM
    // budget or a multi-dimension gate), not on pipeline gating. Surface that
    // as an explicit WaitingResource status so entries, panels and tests can
    // distinguish "queued behind the DAG" from "held for admission". Log only
    // on the Queued → WaitingResource transition to avoid spamming
    // re-evaluations. (Candidates the bounded pass never examined keep Queued
    // until examined — equally truthful.)
    for ( long id : resourceBlockedIds )
    {
        const auto it = m_tasks.find( id );
        if ( it == m_tasks.end() || !it->autoDispatch || !it->jobId.empty() )
            continue;
        if ( it->status == TaskStatus::Queued )
        {
            setTaskStatusLocked( *it, TaskStatus::WaitingResource );
            it->logBuffer.append( QStringLiteral( "Waiting for resources (admission held)." ) );
            updatePipelineForTaskLocked( id );
            queueTaskUpdatedLocked( id );
        }
    }
    for ( long id : dagRegressedIds )
    {
        const auto it = m_tasks.find( id );
        if ( it == m_tasks.end() )
            continue;
        if ( it->status == TaskStatus::WaitingResource )
        {
            setTaskStatusLocked( *it, TaskStatus::Queued );
            updatePipelineForTaskLocked( id );
            queueTaskUpdatedLocked( id );
        }
    }

    // Global-hold parity: when this pass launched nothing because a GLOBAL
    // gate holds (slot saturation or the RSS watermark break above), the old
    // full scan flipped every eligible candidate to WaitingResource. A bounded
    // heap pass vouches for the candidate it can see in O(1): the heap head.
    if ( launchedCount == 0 && resourceBlockedIds.isEmpty() && !m_readyHeap.empty()
         && ( !globalHoldReason.isEmpty() || m_active.total >= globalMax ) )
    {
        const ReadyEntry head = m_readyHeap.top();
        const auto serialIt = m_readySerial.constFind( head.taskId );
        if ( serialIt != m_readySerial.constEnd() && *serialIt == head.serial )
        {
            const auto it = m_tasks.find( head.taskId );
            if ( it != m_tasks.end() && it->status == TaskStatus::Queued
                 && it->autoDispatch && it->jobId.empty() )
            {
                setTaskStatusLocked( *it, TaskStatus::WaitingResource );
                it->logBuffer.append( QStringLiteral( "Waiting for resources (admission held)." ) );
                updatePipelineForTaskLocked( head.taskId );
                queueTaskUpdatedLocked( head.taskId );
            }
        }
    }

    // Legacy manual-task placeholder pass: non-autoDispatch tasks never stage
    // a launch; their placeholder substitution keeps running on every pass as
    // before (the tracked set is user-paced and small).
    const QList<long> manualQueued = m_manualQueued;
    for ( long id : manualQueued )
        applyPlaceholdersForTask( id );

    // RSS watermark re-arm (unchanged semantics): with nothing running and
    // the watermark holding the queue, poll until pressure clears — no task
    // transition will ever fire to re-run admission on its own.
    if ( !globalHoldReason.isEmpty() && m_active.total == 0 && QCoreApplication::instance() )
    {
        QTimer::singleShot( 250, QCoreApplication::instance(), [this]() {
            {
                QMutexLocker locker( &m_mutex );
                processNextQueuedTasks();
            }
            flushPendingLaunches();
            flushPendingSignals();
        } );
    }
}

void TaskCenter::flushPendingLaunches()
{
    QList<PendingLaunch> launches;
    {
        QMutexLocker locker( &m_mutex );
        if ( m_isShuttingDown.load() )
        {
            // Shutdown in progress: never hand new work to the engine (#684).
            // The staged tasks are canceled by the shutdown pass.
            m_pendingLaunches.clear();
            return;
        }
        launches.swap( m_pendingLaunches );
    }

    if ( launches.isEmpty() )
        return;
    ensureJobListener();

    // Execution Plane 7.0: start the shared worker pool before submitting
    // the first isolated launch. Process spawn + handshake must never happen
    // under m_mutex, so this runs in the flush thread. A pool that cannot
    // start fails ISOLATED launches with a typed error (fail-closed: never a
    // silent in-process fallback); non-isolated launches proceed.
    bool workerPoolReady = true;
    QString workerPoolError;
    if ( std::any_of( launches.cbegin(), launches.cend(), []( const PendingLaunch &l ) { return l.isolated; } ) )
    {
        sicnu::processing::WorkerExecutionConfig routeConfig =
            processing::workerExecutionConfigFromEnvironment();
        // The CONFIGURED mode (host/test API or a previous explicit start)
        // wins over a re-read of the environment: a mode set after startup
        // must stay consistent with the pool that gets (re)started here.
        routeConfig.mode = processing::currentWorkerExecutionMode();
        workerPoolReady = processing::ensureSharedWorkerPoolStarted( routeConfig, &workerPoolError );
        if ( !workerPoolReady )
            workerPoolError = QStringLiteral( "isolated worker execution unavailable: %1" )
                                  .arg( workerPoolError );
    }

    for ( auto &launch : launches )
    {
        if ( launch.isolated && !workerPoolReady )
        {
            markTaskFailed( launch.taskId, workerPoolError );
            continue;
        }
        {
            QMutexLocker lock( &m_mutex );
            if ( !m_tasks.contains( launch.taskId ) || isTerminalStatus( m_tasks[launch.taskId].status ) )
            {
                continue; // Task was canceled/terminated between staging and flush!
            }
        }

        // Execution-cache serve (#667): runs outside m_mutex (file copy +
        // terminal transition). On a miss the fingerprint goes back so the
        // completion path can record the freshly produced output.
        sicnu::data::ExecutionFingerprint fp;
        {
            QMutexLocker lock( &m_mutex );
            fp = m_taskFingerprints.take( launch.taskId );
        }
        if ( fp.isValid() )
        {
            using sicnu::runtime::observability::ExecutionTelemetry;
            using sicnu::runtime::observability::Counter;
            if ( serveFromExecutionCache( launch.taskId, fp ) )
            {
                ExecutionTelemetry::instance().increment( Counter::CacheHits );
                traceTaskEvent( "cache", "hit", launch.taskId, launch.algorithmId );
                continue;
            }
            ExecutionTelemetry::instance().increment( Counter::CacheMisses );
            traceTaskEvent( "cache", "miss", launch.taskId, launch.algorithmId );
        }
        if ( fp.isValid() )
        {
            QMutexLocker lock( &m_mutex );
            if ( m_tasks.contains( launch.taskId ) && !isTerminalStatus( m_tasks[launch.taskId].status ) )
                m_taskFingerprints[launch.taskId] = fp;
        }

        // #799: PRE-REGISTER the task↔job mapping under m_mutex BEFORE the
        // job exists, using a caller-chosen engine id. The old order
        // (submit → register) stranded tasks in Dispatching whenever the job
        // reached a terminal state before registration landed: its terminal
        // record arrived with an id that mapped to no task and was dropped.
        const std::string jobId = QStringLiteral( "task-%1-%2" )
                                      .arg( launch.taskId )
                                      .arg( QUuid::createUuid().toString( QUuid::WithoutBraces )
                                                .left( 8 ) )
                                      .toStdString();
        {
            QMutexLocker preLock( &m_mutex );
            if ( !m_tasks.contains( launch.taskId ) || isTerminalStatus( m_tasks[launch.taskId].status ) )
                continue; // canceled between staging and dispatch — never submit
            m_taskByJobId[jobId] = launch.taskId;
        }

        std::string submittedId;
        if ( launch.hasExecutor )
            submittedId = sicnu::jobs::JobEngine::instance().submitWithId(
                launch.request, jobId, std::move( launch.executor ),
                std::move( launch.onCancel ) );
        else
            submittedId = sicnu::jobs::JobEngine::instance().submitWithId( launch.request, jobId );

        if ( submittedId.empty() )
        {
            // Refused id (collision — not reachable with a fresh UUID suffix,
            // but never leave the pre-registration dangling).
            {
                QMutexLocker rollback( &m_mutex );
                m_taskByJobId.remove( jobId );
            }
            markTaskFailed( launch.taskId, QStringLiteral( "Task Center could not submit the job" ) );
            continue;
        }

        bool mapped = false;
        {
            QMutexLocker reLock( &m_mutex );
            if ( m_tasks.contains( launch.taskId ) && !isTerminalStatus( m_tasks[launch.taskId].status ) )
            {
                m_tasks[launch.taskId].jobId = submittedId;
                mapped = true;
            }
            else
            {
                // Canceled while submit was in-flight: cancel the newly
                // submitted job immediately, and drop the pre-registration —
                // the task is terminal, so the job's terminal record would
                // otherwise leave an orphan mapping behind (review L P3).
                sicnu::jobs::JobEngine::instance().cancel( submittedId );
                m_taskByJobId.remove( submittedId );
            }
        }
        if ( mapped )
        {
            // Catch-up snapshot; see submitJobImpl for the rationale.
            if ( const auto record = sicnu::jobs::JobEngine::instance().snapshot( submittedId ) )
                onJobRecord( *record );
        }
    }
}

void TaskCenter::attachQgsTask( long taskId, QgsTask *qgsTask )
{
    QMutexLocker locker( &m_mutex );
    if ( m_tasks.contains( taskId ) && qgsTask )
        m_tasks[taskId].taskHandle = qgsTask;
}

void TaskCenter::updateTaskProgress( long taskId, double progress )
{
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) || isTerminalStatus( m_tasks[taskId].status ) )
            return;
        m_tasks[taskId].progressPercentage = progress;
        // Progress from a job that is winding down after a cancel request must
        // not overwrite the Cancelling state (the UI shows "cancelling" until
        // the worker's terminal record arrives).
        if ( m_tasks[taskId].status == TaskStatus::Queued
             || m_tasks[taskId].status == TaskStatus::WaitingResource
             || m_tasks[taskId].status == TaskStatus::Dispatching
             || m_tasks[taskId].status == TaskStatus::Running )
        {
            // Progress ticks mean the executor is genuinely running — flip
            // pre-start states (staged/queued) to Running.
            setTaskStatusLocked( m_tasks[taskId], TaskStatus::Running );
        }
        updatePipelineForTaskLocked( taskId );
        queueTaskUpdatedLocked( taskId );
    }
    flushPendingSignals();
}

void TaskCenter::appendTaskLog( long taskId, const QString &message )
{
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) )
            return;
        m_tasks[taskId].logBuffer.append( message );
        queueTaskLogLocked( taskId, message );
    }
    flushPendingSignals();
}

void TaskCenter::markTaskRunning( long taskId )
{
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) || isTerminalStatus( m_tasks[taskId].status ) )
            return;
        setTaskStatusLocked( m_tasks[taskId], TaskStatus::Running );
        updatePipelineForTaskLocked( taskId );
        queueTaskUpdatedLocked( taskId );
    }
    flushPendingSignals();
}

void TaskCenter::markTaskCompleted( long taskId,
                                    const QVariantMap &results,
                                    const Json::Value &resultPayload )
{
    QString autoLoadPath;
    bool shouldAutoLoad = false;
    {
        QMutexLocker locker( &m_mutex );
        // Terminal is final: a late duplicate record (listener vs catch-up) is a no-op.
        if ( !m_tasks.contains( taskId ) || isTerminalStatus( m_tasks[taskId].status ) )
            return;
        setTaskStatusLocked( m_tasks[taskId], TaskStatus::Completed );
        traceTaskEvent( "terminal", "ok", taskId, m_tasks[taskId].algorithmId );
        m_tasks[taskId].resultPayload = resultPayload;
        sicnu::runtime::observability::ExecutionTelemetry::instance().increment(
            sicnu::runtime::observability::Counter::TasksCompleted );
        m_tasks[taskId].progressPercentage = 1.0;
        m_tasks[taskId].endTime = QDateTime::currentDateTimeUtc();
        m_tasks[taskId].logBuffer.append( QString( QStringLiteral( "[%1] Task completed successfully." ) )
                                            .arg( m_tasks[taskId].endTime.toString( QStringLiteral( "hh:mm:ss" ) ) ) );

        if ( m_tasks[taskId].outputLayerPath.isEmpty() && !results.isEmpty() )
        {
            // Prefer "output" key to avoid alphabetical bias (e.g. modelOut before output).
            auto outIt = results.find( QStringLiteral( "output" ) );
            if ( outIt != results.end() && outIt.value().canConvert<QString>() )
            {
                const QString p = outIt.value().toString();
                if ( !p.isEmpty() ) m_tasks[taskId].outputLayerPath = p;
            }
            else
            {
                // Fallback: first stringifiable result (preserves legacy single-output behavior).
                for ( auto it = results.begin(); it != results.end(); ++it )
                {
                    if ( it.value().canConvert<QString>() )
                    {
                        const QString p = it.value().toString();
                        if ( !p.isEmpty() ) { m_tasks[taskId].outputLayerPath = p; break; }
                    }
                }
            }
        }
        if ( m_tasks[taskId].outputLayerPath.isEmpty() )
            m_tasks[taskId].outputLayerPath = outputPathFromResult( resultPayload );

        shouldAutoLoad = m_tasks[taskId].autoLoadLayer && !m_tasks[taskId].outputLayerPath.isEmpty();
        autoLoadPath = m_tasks[taskId].outputLayerPath;

        // Terminal transitions own their listener-dispatch mapping: without
        // this, tasks finalized off the listener path (stranded Cancelling,
        // shutdown) leak their jobId→taskId entry forever (review P2).
        if ( !m_tasks[taskId].jobId.empty() )
            m_taskByJobId.remove( m_tasks[taskId].jobId );

        // Execution-cache store (#667/#726): the output of a completed
        // revision-identified task is reusable by a future identical run.
        // The fingerprint hex is stamped into the payload first so consumers
        // (CLI asset registration, provenance) can bind the produced asset to
        // the exact producing execution.
        {
            const auto fpIt = m_taskFingerprints.constFind( taskId );
            if ( fpIt != m_taskFingerprints.constEnd() && fpIt->isValid()
                 && m_tasks[taskId].resultPayload.isObject()
                 && !m_tasks[taskId].resultPayload.isMember( "executionFingerprint" ) )
            {
                m_tasks[taskId].resultPayload["executionFingerprint"] = fpIt->toHex().toStdString();
            }
            storeExecutionResultLocked( taskId );
        }

        updatePipelineForTaskLocked( taskId );
        queueTaskUpdatedLocked( taskId );
        // 8.0 WP-A: O(children) promotion of unblocked children onto the
        // ready heap before the admission pass runs.
        promoteChildrenOfLocked( taskId );
        processNextQueuedTasks();
    }

    // Phase C: a fused-chain head completes its members with the tail payload
    // (members never dispatch; see submitPipeline). The tail member's declared
    // output exists, so its own cache store succeeds; intermediate declared
    // outputs deliberately do not exist and the store refuses them
    // (fail-closed). Runs outside m_mutex via the public re-entry.
    QList<long> fusedMembers;
    {
        QMutexLocker locker( &m_mutex );
        const auto bindingIt = m_fusedChains.constFind( taskId );
        if ( bindingIt != m_fusedChains.constEnd() )
        {
            fusedMembers = bindingIt->memberTaskIds;
            m_fusedChains.erase( bindingIt );
        }
    }
    for ( long memberTaskId : fusedMembers )
        markTaskCompleted( memberTaskId, results, resultPayload );

    flushPendingLaunches();
    flushPendingSignals();
    fireTaskCompletionCallbacks( taskId );

    if ( shouldAutoLoad && !autoLoadPath.isEmpty() )
        emit layerAutoLoadRequested( autoLoadPath );
}

void TaskCenter::cascadeCancelTargetsLocked( const QList<long> &targets, long userRootId,
                                             const QString &upstreamCause, bool cleanupScratchOutputs,
                                             QList<long> &cascadeCanceledIds,
                                             std::vector<std::pair<std::string, long>> &jobCancelTargets,
                                             QList<QPointer<QgsTask>> &handlesToCancel )
{
    for ( long targetId : targets )
    {
        auto &info = m_tasks[targetId];
        if ( isTerminalStatus( info.status ) )
            continue;

        if ( info.taskHandle )
            handlesToCancel.append( info.taskHandle );

        const bool isUserRoot = ( targetId == userRootId );
        if ( !info.jobId.empty() )
        {
            // Dispatched work: the worker observes the cancel flag and the
            // terminal Canceled record arrives via the listener. Track the
            // in-between explicitly so entries/UI can show "cancelling".
            setTaskStatusLocked( info, TaskStatus::Cancelling );
            jobCancelTargets.emplace_back( info.jobId, targetId );
            info.logBuffer.append( isUserRoot
                                     ? QStringLiteral( "Cancellation requested by user." )
                                     : QStringLiteral( "Cancellation requested due to upstream parent task %1." ).arg( upstreamCause ) );
        }
        else
        {
            setTaskStatusLocked( info, TaskStatus::Canceled );
            info.errorMessage = isUserRoot
                                  ? QStringLiteral( "Task canceled" )
                                  : QStringLiteral( "Canceled due to upstream parent task %1." ).arg( upstreamCause );
            info.endTime = QDateTime::currentDateTimeUtc();
            info.logBuffer.append( isUserRoot
                                     ? QStringLiteral( "Task canceled by user." )
                                     : QStringLiteral( "Canceled due to upstream parent task %1." ).arg( upstreamCause ) );
            cascadeCanceledIds.append( targetId );

            if ( cleanupScratchOutputs && !info.outputLayerPath.isEmpty() && QFile::exists( info.outputLayerPath ) )
            {
                // Scratch-only deletion: the system temp root (portable —
                // QDir::tempPath() resolves %TEMP% on Windows, which a plain
                // "/tmp/" prefix check always missed) or a .scratch path.
                // Trailing separator so a sibling like /tmp2/x.tif under a
                // /tmp temp root is never prefix-matched.
                const QString tempRoot = QDir::tempPath();
                const QString tempPrefix = tempRoot.endsWith( QLatin1Char( '/' ) )
                                               ? tempRoot
                                               : tempRoot + QLatin1Char( '/' );
                if ( info.outputLayerPath.startsWith( QStringLiteral( "/tmp/" ) )
                     || info.outputLayerPath.startsWith( tempPrefix )
                     || info.outputLayerPath.contains( QStringLiteral( ".scratch" ) ) )
                {
                    QFile::remove( info.outputLayerPath );
                }
            }
        }

        updatePipelineForTaskLocked( targetId );
        queueTaskUpdatedLocked( targetId );
    }
}

void TaskCenter::dispatchPendingCancels( const QList<QPointer<QgsTask>> &handlesToCancel,
                                         const std::vector<std::pair<std::string, long>> &jobCancelTargets,
                                         const QString &strandedReason )
{
    // Attached QgsTask objects are owned by the main thread and cancel() has
    // no thread-safety guarantee; marshal the call onto the handle's own
    // thread instead of calling it from whatever worker thread got here.
    for ( const QPointer<QgsTask> &handle : handlesToCancel )
    {
        if ( handle )
        {
            QMetaObject::invokeMethod( handle, [handle]() { if ( handle ) handle->cancel(); },
                                       Qt::QueuedConnection );
        }
    }

    for ( const auto &[jobId, targetId] : jobCancelTargets )
    {
        if ( sicnu::jobs::JobEngine::instance().cancel( jobId ) )
            continue;

        // The engine no longer knows this job (stale/expired id): no terminal
        // record will arrive via the listener, so the task would strand in
        // Cancelling forever and starve waiters. Finalize it here.
        bool fireCallbacks = false;
        {
            QMutexLocker locker( &m_mutex );
            if ( m_tasks.contains( targetId ) && m_tasks[targetId].status == TaskStatus::Cancelling )
            {
                auto &info = m_tasks[targetId];
                setTaskStatusLocked( info, TaskStatus::Canceled );
                info.errorMessage = strandedReason;
                info.endTime = QDateTime::currentDateTimeUtc();
                info.logBuffer.append( strandedReason );
                updatePipelineForTaskLocked( targetId );
                queueTaskUpdatedLocked( targetId );
                fireCallbacks = true;
            }
        }
        if ( fireCallbacks )
        {
            flushPendingSignals();
            fireTaskCompletionCallbacks( targetId );
        }
    }
}

bool isTransientExecutionError( const QString &error )
{
    const QString message = error.trimmed();
    // Every prefix below is produced by the worker INFRASTRUCTURE only — in
    // all of them the operator provably never produced a result, so a bounded
    // re-run cannot double-produce. (The prefixes are infrastructure-owned
    // namespace; an operator imitating them only widens its own retry
    // budget, which stays bounded by maxAutoRetries.)
    return message.startsWith( QStringLiteral( "worker crashed:" ) )
           || message.startsWith( QStringLiteral( "worker timeout:" ) )
           || message.startsWith( QStringLiteral( "worker protocol: cannot start" ) )
           || message.startsWith( QStringLiteral( "worker protocol: cannot send" ) )
           || message.startsWith( QStringLiteral( "worker protocol: malformed frame" ) );
}

bool TaskCenter::shouldAutoRetryLocked( const AlgorithmTaskInfo &task, const QString &error ) const
{
    if ( m_maxAutoRetries <= 0 )
        return false;
    if ( task.autoRetryAttempts >= m_maxAutoRetries )
        return false;
    return isTransientExecutionError( error );
}

void TaskCenter::setMaxAutoRetries( int maxRetries )
{
    QMutexLocker locker( &m_mutex );
    m_maxAutoRetries = std::clamp( maxRetries, 0, 3 );
}

int TaskCenter::maxAutoRetries() const
{
    QMutexLocker locker( &m_mutex );
    return m_maxAutoRetries;
}

void TaskCenter::markTaskFailed( long taskId, const QString &error )
{
    QList<long> cascadeCanceledIds;
    std::vector<std::pair<std::string, long>> jobCancelTargets;
    QList<QPointer<QgsTask>> handlesToCancel;
    // Execution Plane 7.0: set when the task was resurrected by the bounded
    // transient auto-retry. The post-lock section then flushes the staged
    // re-dispatch (a staged launch must NEVER wait for an unrelated terminal
    // transition) but skips the failure cascade and the terminal completion
    // callbacks — the task is alive, not terminal.
    bool autoRetried = false;
    {
        QMutexLocker locker( &m_mutex );
        // Terminal is final: a late duplicate record (listener vs catch-up) is a no-op.
        if ( !m_tasks.contains( taskId ) || isTerminalStatus( m_tasks[taskId].status ) )
            return;

        // Execution Plane 7.0: bounded transient auto-retry. The task is
        // resurrected in place — children keep their DAG edges and no
        // terminal transition fires — with the failed attempt's job identity
        // cleared so the next admission pass dispatches a FRESH job (#799
        // pre-registration applies to the new id). The attempt counter and
        // the hard cap keep the loop bounded; a second transient failure
        // with an exhausted budget takes the normal Failed path below.
        if ( shouldAutoRetryLocked( m_tasks[taskId], error ) )
        {
            AlgorithmTaskInfo &info = m_tasks[taskId];
            const std::string deadJobId = info.jobId;
            ++info.autoRetryAttempts;
            setTaskStatusLocked( info, TaskStatus::Queued );
            info.jobId.clear();
            info.errorMessage.clear();
            info.endTime = QDateTime();
            info.progressPercentage = 0.0;
            if ( !deadJobId.empty() )
                m_taskByJobId.remove( deadJobId );
            // The retried run records fresh identity: drop the failed
            // attempt's fingerprint bookkeeping (the legacy removals below
            // do not run on this early-return path).
            m_taskFingerprints.remove( taskId );
            m_taskFingerprintParams.remove( taskId );
            // 8.0 WP-A: resurrect onto the ready heap — its heap entries were
            // dropped when the task was staged/dispatched, so a fresh serial
            // is required or the retry would strand.
            pushReadyCandidateLocked( taskId );
            info.logBuffer.append(
                QString( QStringLiteral( "[%1] Transient failure — auto-retry %2/%3: %4" ) )
                    .arg( QDateTime::currentDateTimeUtc().toString( QStringLiteral( "hh:mm:ss" ) ) )
                    .arg( info.autoRetryAttempts )
                    .arg( m_maxAutoRetries )
                    .arg( error ) );
            // Keep the pipeline step status truthful during the retry window
            // (the legacy path recomputes it below; this early branch does
            // not reach that recompute).
            updatePipelineForTaskLocked( taskId );
            sicnu::runtime::observability::ExecutionTelemetry::instance().increment(
                sicnu::runtime::observability::Counter::TaskAutoRetries );
            traceTaskEvent( "retry", "transient", taskId, info.algorithmId,
                            QStringLiteral( "attempt=%1/%2 class=%3" )
                                .arg( info.autoRetryAttempts )
                                .arg( m_maxAutoRetries )
                                .arg( error.left( 60 ) ) );
            queueTaskUpdatedLocked( taskId );
            processNextQueuedTasks();
            autoRetried = true;
        }
        else
        {
        // The root's own engine job must be cancelled too (#702, symmetric
        // with markTaskCanceled): an externally-driven failure must kill the
        // still-running engine job, or it keeps writing output while the task
        // shows Failed. When the job is already terminal (the listener path),
        // engine cancel is a harmless no-op.
        const std::string rootJobId = m_tasks[taskId].jobId;
        if ( !rootJobId.empty() )
            jobCancelTargets.emplace_back( rootJobId, taskId );
        m_taskFingerprints.remove( taskId );
        m_taskFingerprintParams.remove( taskId );
        m_taskChainedEdges.remove( taskId );
        m_taskRegisteredInputStats.remove( taskId );
        setTaskStatusLocked( m_tasks[taskId], TaskStatus::Failed );
        traceTaskEvent( "terminal", "error", taskId, m_tasks[taskId].algorithmId, error.left( 120 ) );
        m_tasks[taskId].errorMessage = error;
        m_tasks[taskId].endTime = QDateTime::currentDateTimeUtc();
        m_tasks[taskId].logBuffer.append( QString( QStringLiteral( "[%1] Task failed: %2" ) )
                                            .arg( m_tasks[taskId].endTime.toString( QStringLiteral( "hh:mm:ss" ) ), error ) );
        // 8.0 WP-D evidence: when this failure ends a retry sequence, the
        // record must say WHY the task was retried and why it stopped.
        if ( m_tasks[taskId].autoRetryAttempts > 0 )
        {
            m_tasks[taskId].logBuffer.append(
                QString( QStringLiteral( "[%1] Auto-retry budget exhausted after %2 transient failure attempt(s); failing permanently (operator errors and cancellations are never auto-retried)." ) )
                    .arg( m_tasks[taskId].endTime.toString( QStringLiteral( "hh:mm:ss" ) ) )
                    .arg( m_tasks[taskId].autoRetryAttempts ) );
        }
        if ( !rootJobId.empty() )
            m_taskByJobId.remove( rootJobId );
        updatePipelineForTaskLocked( taskId );
        queueTaskUpdatedLocked( taskId );

        const QList<long> descendants = collectTransitiveDescendantsLocked( taskId );
        cascadeCancelTargetsLocked( descendants, -1, QStringLiteral( "failure" ), false,
                                    cascadeCanceledIds, jobCancelTargets, handlesToCancel );

        processNextQueuedTasks();
        }
    }
    flushPendingLaunches();
    flushPendingSignals();
    if ( autoRetried )
        return; // the re-dispatch is staged + flushed; no terminal bookkeeping

    dispatchPendingCancels( handlesToCancel, jobCancelTargets,
                            QStringLiteral( "Job no longer known to the engine; task canceled after upstream failure." ) );

    fireTaskCompletionCallbacks( taskId );
    for ( long id : cascadeCanceledIds )
        fireTaskCompletionCallbacks( id );
}

void TaskCenter::markTaskCanceled( long taskId, const QString &reason )
{
    QList<long> cascadeCanceledIds;
    std::vector<std::pair<std::string, long>> jobCancelTargets;
    QList<QPointer<QgsTask>> handlesToCancel;
    {
        QMutexLocker locker( &m_mutex );
        // Terminal is final: a late duplicate record (listener vs catch-up) is a no-op.
        if ( !m_tasks.contains( taskId ) || isTerminalStatus( m_tasks[taskId].status ) )
            return;
        // The root's own engine job must be cancelled too (#702): callers of
        // markTaskCanceled other than processJobRecord (e.g. future direct
        // paths) would otherwise orphan a running job that keeps writing
        // output while the task shows Canceled. When the job is already
        // terminal (the listener path), engine cancel is a harmless no-op.
        const std::string rootJobId = m_tasks[taskId].jobId;
        if ( !rootJobId.empty() )
            jobCancelTargets.emplace_back( rootJobId, taskId );
        m_taskFingerprints.remove( taskId );
        m_taskFingerprintParams.remove( taskId );
        m_taskChainedEdges.remove( taskId );
        m_taskRegisteredInputStats.remove( taskId );
        setTaskStatusLocked( m_tasks[taskId], TaskStatus::Canceled );
        traceTaskEvent( "cancel", "ok", taskId, m_tasks[taskId].algorithmId, reason.left( 120 ) );
        m_tasks[taskId].errorMessage = reason;
        m_tasks[taskId].endTime = QDateTime::currentDateTimeUtc();
        m_tasks[taskId].logBuffer.append( QString( QStringLiteral( "[%1] %2" ) )
                                            .arg( m_tasks[taskId].endTime.toString( QStringLiteral( "hh:mm:ss" ) ), reason ) );
        if ( !rootJobId.empty() )
            m_taskByJobId.remove( rootJobId );
        updatePipelineForTaskLocked( taskId );
        queueTaskUpdatedLocked( taskId );

        const QList<long> descendants = collectTransitiveDescendantsLocked( taskId );
        cascadeCancelTargetsLocked( descendants, -1, QStringLiteral( "cancellation" ), false,
                                    cascadeCanceledIds, jobCancelTargets, handlesToCancel );

        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();

    dispatchPendingCancels( handlesToCancel, jobCancelTargets,
                            QStringLiteral( "Job no longer known to the engine; task canceled." ) );

    fireTaskCompletionCallbacks( taskId );
    for ( long id : cascadeCanceledIds )
        fireTaskCompletionCallbacks( id );
}

bool TaskCenter::cancelTask( long taskId )
{
    std::vector<std::pair<std::string, long>> jobCancelTargets;
    QList<long> cascadeCanceledIds;
    QList<QPointer<QgsTask>> handlesToCancel;
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) )
            return false;
        if ( isTerminalStatus( m_tasks[taskId].status ) )
            return false;

        QList<long> targets;
        targets.append( taskId );
        targets.append( collectTransitiveDescendantsLocked( taskId ) );

        cascadeCancelTargetsLocked( targets, taskId, QStringLiteral( "cancellation" ), true,
                                    cascadeCanceledIds, jobCancelTargets, handlesToCancel );

        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();

    dispatchPendingCancels( handlesToCancel, jobCancelTargets,
                            QStringLiteral( "Job no longer known to the engine; task canceled." ) );

    for ( long id : cascadeCanceledIds )
        fireTaskCompletionCallbacks( id );

    return true;
}

bool TaskCenter::cancelPipeline( long pipelineId )
{
    if ( pipelineId < 0 )
        return false;

    PipelineExecutionInfo pipeInfo = getPipelineInfo( pipelineId );
    if ( pipeInfo.pipelineId < 0 )
        return false;

    bool canceledAny = false;
    for ( long taskId : pipeInfo.stepToTaskId.values() )
    {
        if ( cancelTask( taskId ) )
            canceledAny = true;
    }
    return canceledAny;
}

bool TaskCenter::pauseTask( long taskId )
{
    bool ok = false;
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) )
            return false;
        if ( m_tasks[taskId].status == TaskStatus::Running
             || m_tasks[taskId].status == TaskStatus::Dispatching )
        {
            // JobEngine has no pause primitive: a worker either runs or is
            // cancelled. Only tasks backed by a QgsTask handle (legacy
            // QgsTask path, where hold() genuinely parks the work) may pause;
            // engine-dispatched tasks must NOT fabricate Paused (#702) — the
            // status would free an admission slot while the worker keeps
            // running at full speed.
            if ( !m_tasks[taskId].taskHandle )
                return false;
            // hold() has the same thread-affinity caveat as cancel()
            // (dispatchPendingCancels marshals that call): invoke it on the
            // handle's own thread instead of the caller's (#616).
            QgsTask *handle = m_tasks[taskId].taskHandle.data();
            QMetaObject::invokeMethod( handle, [handle]() { handle->hold(); },
                                       Qt::QueuedConnection );
            setTaskStatusLocked( m_tasks[taskId], TaskStatus::Paused );
            m_tasks[taskId].logBuffer.append( QStringLiteral( "Task paused." ) );
            updatePipelineForTaskLocked( taskId );
            queueTaskUpdatedLocked( taskId );
            ok = true;
        }
    }
    if ( ok )
        flushPendingSignals();
    return ok;
}

bool TaskCenter::resumeTask( long taskId )
{
    bool ok = false;
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) )
            return false;
        if ( m_tasks[taskId].status == TaskStatus::Paused )
        {
            if ( m_tasks[taskId].taskHandle )
            {
                QgsTask *handle = m_tasks[taskId].taskHandle.data();
                QMetaObject::invokeMethod( handle, [handle]() { handle->unhold(); },
                                           Qt::QueuedConnection );
            }
            setTaskStatusLocked( m_tasks[taskId], TaskStatus::Running );
            m_tasks[taskId].logBuffer.append( QStringLiteral( "Task resumed." ) );
            updatePipelineForTaskLocked( taskId );
            queueTaskUpdatedLocked( taskId );
            ok = true;
        }
    }
    if ( ok )
        flushPendingSignals();
    return ok;
}

long TaskCenter::retryTask( long taskId )
{
    AlgorithmTaskInfo oldInfo;
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) )
            return 0;
        // A non-terminal task is still schedulable/running: enqueueing a
        // retry beside it would execute the same work twice (#616).
        if ( !isTerminalStatus( m_tasks[taskId].status ) )
            return 0;
        oldInfo = m_tasks[taskId];
    }

    // Retry must not strand behind parents that can never satisfy DAG gating
    // (#685): a cascade-canceled/failed parent is terminal-but-not-Completed,
    // so a retry keeping it would sit in Queued forever. Keep parents that
    // completed or are still live; drop the rest (missing parents already
    // count as satisfied).
    QList<long> retryParents;
    {
        QMutexLocker locker( &m_mutex );
        for ( long parentId : oldInfo.parentTaskIds )
        {
            const auto it = m_tasks.find( parentId );
            if ( it == m_tasks.end() )
                continue; // cleared: treated as satisfied, drop from the list
            if ( it->status == TaskStatus::Completed || !isTerminalStatus( it->status ) )
                retryParents.append( parentId );
        }
    }

    long newTaskId = -1;
    if ( oldInfo.hasJobRequest )
    {
        // Dispatched-step retries historically ran parent-free. Keep LIVE
        // parents (still-running upstream steps whose $parent.port
        // placeholders resolve from their payloads) and drop only the
        // unsatisfiable terminal ones — running ahead of a live parent would
        // execute with unresolved placeholder paths (review P1). The staged
        // admission path enforces parent gating at scheduling time.
        newTaskId = submitJob( oldInfo.jobRequest, oldInfo.jobExecutor, {}, oldInfo.autoLoadLayer,
                               oldInfo.priority, retryParents );
    }
    else
    {
        newTaskId = enqueueTask( oldInfo.algorithmId, oldInfo.parameterMap, oldInfo.autoLoadLayer,
                                 oldInfo.priority, retryParents, true, oldInfo.resourceEstimateOverrideMb,
                                 oldInfo.source );
    }
    if ( newTaskId <= 0 )
        return 0;

    // Keep the retry attached to its pipeline (#702): without the remap the
    // pipeline kept pointing at the OLD canceled task, so a retried step could
    // never un-fail the pipeline and MCP workflow status stayed wrong.
    if ( oldInfo.pipelineId >= 0 && !oldInfo.stepId.isEmpty() )
    {
        QMutexLocker locker( &m_mutex );
        auto pipeIt = m_pipelines.find( oldInfo.pipelineId );
        auto newIt = m_tasks.find( newTaskId );
        if ( pipeIt != m_pipelines.end() && newIt != m_tasks.end() )
        {
            const std::string stepKey = oldInfo.stepId.toStdString();
            PipelineExecutionInfo &pipe = pipeIt.value();
            pipe.taskToStepId.remove( taskId );
            pipe.taskToStepId[newTaskId] = stepKey;
            pipe.stepToTaskId[stepKey] = newTaskId;
            pipe.stepStatuses[stepKey] = TaskStatus::Queued;
            newIt->pipelineId = oldInfo.pipelineId;
            newIt->stepId = oldInfo.stepId;

            // Recompute the roll-up from step statuses: the retried step is
            // queued again, so an all-terminal pipeline reopens; a failure
            // latch from a DIFFERENT step must survive.
            bool allTerminal = !pipe.stepToTaskId.isEmpty();
            bool anyFailed = false;
            for ( auto it = pipe.stepToTaskId.begin(); it != pipe.stepToTaskId.end(); ++it )
            {
                const auto taskIt = m_tasks.find( it.value() );
                if ( taskIt == m_tasks.end() )
                {
                    allTerminal = false;
                    break;
                }
                if ( !isTerminalStatus( taskIt->status ) )
                {
                    allTerminal = false;
                    break;
                }
                if ( taskIt->status == TaskStatus::Failed || taskIt->status == TaskStatus::Canceled )
                    anyFailed = true;
            }
            pipe.isCompleted = allTerminal;
            pipe.isFailed = allTerminal && anyFailed;
            if ( !allTerminal && pipe.errorMessage == oldInfo.errorMessage )
                pipe.errorMessage.clear(); // the only failed step is being retried
        }
    }
    return newTaskId;
}

QList<AlgorithmTaskInfo> TaskCenter::allTasks() const
{
    QMutexLocker locker( &m_mutex );
    return m_tasks.values();
}

AlgorithmTaskInfo TaskCenter::getTaskInfo( long taskId ) const
{
    QMutexLocker locker( &m_mutex );
    return m_tasks.value( taskId );
}

void TaskCenter::clearCompletedTasks()
{
    QList<long> clearedTaskIds;
    std::vector<std::string> clearedJobIds;
    {
        QMutexLocker locker( &m_mutex );
        const QList<long> keys = m_tasks.keys();
        for ( long id : keys )
        {
            if ( !isTerminalStatus( m_tasks[id].status ) )
                continue;
            if ( !m_tasks[id].jobId.empty() )
                clearedJobIds.push_back( m_tasks[id].jobId );
            clearedTaskIds.append( id );
            // 8.0 WP-A: drop the task's derived admission state and its
            // incoming children-index edges BEFORE the map entry is gone.
            for ( long parentId : m_tasks[id].parentTaskIds )
                m_children.remove( parentId, id );
            forgetDerivedTaskStateLocked( id );
            m_tasks.remove( id );
        }
        // Drop listener-dispatch / delta-dedup state for the cleared tasks
        // (ADR 0052): a straggler job record for a pruned job must no-op as
        // an unknown jobId instead of re-materializing bookkeeping.
        for ( long id : clearedTaskIds )
        {
            m_forwardedLogCounts.remove( id );
            m_lastForwardedProgress.remove( id );
            m_estimateMbCache.remove( id ); // keep the per-task estimate cache bounded
            m_admissionDimsCache.remove( id );
            m_completionCallbacks.remove( id ); // defense (#702): stale registrations
            m_taskFingerprints.remove( id );
            m_taskFingerprintParams.remove( id );
            m_taskChainedEdges.remove( id );
            m_taskRegisteredInputStats.remove( id );
        }
        for ( auto it = m_taskByJobId.begin(); it != m_taskByJobId.end(); )
        {
            if ( !m_tasks.contains( it.value() ) )
                it = m_taskByJobId.erase( it );
            else
                ++it;
        }
    }
    // Prune exactly the engine records of the cleared tasks (ADR 0052);
    // untracked engine jobs (direct submissions) are left untouched.
    if ( !clearedJobIds.empty() )
        sicnu::jobs::JobEngine::instance().removeCompleted( clearedJobIds );
}

long TaskCenter::submitPipeline( const sicnu::workflow::WorkflowDefinition &def, bool autoLoad )
{
    if ( m_isShuttingDown.load() )
        return -1; // no new work after shutdown (#684)

    std::vector<std::string> ordered;
    std::string sortError;
    if ( !sicnu::workflow::topologicalSortSteps( def, ordered, sortError ) )
        return -1;

    // Phase C (intermediate materialization elimination): when enabled, plan
    // a fused elementwise chain so its member steps never dispatch to the
    // engine individually — the head executes them as one streaming tile
    // pipeline and members complete with the tail payload. Identity is
    // preserved (per-step tasks + fingerprints; see fused_chain.h).
    const bool fusedChainsEnabled = [] {
        const char *env = std::getenv( "SICNU_FUSED_CHAIN" );
        return env && ( env[0] == '1' || env[0] == 't' || env[0] == 'T' );
    }();
    sicnu::processing::FusedChainPlan fusedPlan;
    if ( fusedChainsEnabled )
        fusedPlan = sicnu::processing::planFusedChain( def );

    sicnu::data::DataManager *catalogForWarm = nullptr;
    // 8.0 WP-F (review P1 fix): warm the remote-identity session cache for
    // every step's params BEFORE the mutex section (network I/O must never
    // run under the scheduler lock).
    {
        QMutexLocker catalogLocker( &m_mutex );
        catalogForWarm = m_catalog;
    }
    if ( catalogForWarm )
    {
        for ( const auto &step : def.steps )
            sicnu::temporal::warmExecutionIdentityCache(
                catalogForWarm, sicnu::processing::jsonParamsToVariantMap( step.params ) );
    }

    long pipelineId = -1;
    {
        QMutexLocker locker( &m_mutex );
        pipelineId = m_nextPipelineId++;
        sicnu::runtime::observability::ExecutionTelemetry::instance().increment(
            sicnu::runtime::observability::Counter::TasksSubmitted );
        PipelineExecutionInfo pipeInfo;
        pipeInfo.pipelineId = pipelineId;
        pipeInfo.definitionId = QString::fromStdString( def.id );
        pipeInfo.orderedStepIds = ordered;

        QMap<std::string, long> stepToTaskId;
        QMap<QString, QString> declaredOutputByStepId; // step id → declared output path (#726 chained identity)

        for ( const auto &stepId : ordered )
        {
            const sicnu::workflow::StepDef *step = nullptr;
            for ( const auto &s : def.steps )
            {
                if ( s.id == stepId )
                {
                    step = &s;
                    break;
                }
            }
            if ( !step || step->kind != sicnu::workflow::StepKind::Operator || step->operatorId.empty() )
                continue;

            QList<long> parentTaskIds;
            for ( const auto &conn : step->inputs )
            {
                if ( stepToTaskId.contains( conn.fromStepId ) )
                {
                    long pTaskId = stepToTaskId[conn.fromStepId];
                    if ( !parentTaskIds.contains( pTaskId ) )
                        parentTaskIds.append( pTaskId );
                }
            }

            QVariantMap params = sicnu::processing::jsonParamsToVariantMap( step->params );

            long taskId = m_nextTaskId++;
            AlgorithmTaskInfo info;
            info.taskId = taskId;
            info.algorithmId = QString::fromStdString( step->operatorId );
            info.algorithmName = step->title.empty() ? QString::fromStdString( step->operatorId )
                                                     : QString::fromStdString( step->title );
            info.status = TaskStatus::Queued;
            info.priority = TaskPriority::Normal;
            info.parentTaskIds = parentTaskIds;
            info.startTime = QDateTime::currentDateTimeUtc();
            info.parameterMap = params;
            info.autoLoadLayer = autoLoad;
            info.autoDispatch = true;
            info.resourceProfile = resolveResourceProfile( info.algorithmId );
            info.stepId = QString::fromStdString( stepId );
            info.pipelineId = pipelineId;

            // Fused-chain wiring (Phase C): head dispatches the fused executor;
            // members never dispatch and complete with the tail payload.
            if ( fusedPlan.stepIds.size() >= 2
                 && ( stepId == fusedPlan.headStepId || stepId == fusedPlan.tailStepId ) )
            {
                if ( stepId == fusedPlan.headStepId )
                {
                    const sicnu::processing::FusedChainPlan plan = fusedPlan;
                    info.jobExecutor = [plan]( const sicnu::jobs::JobRequest &,
                                               sicnu::operators::RSOperatorContext &ctx ) {
                        return sicnu::processing::executeFusedChain( plan, ctx );
                    };
                    // hasJobRequest flips the staged launch to executor mode;
                    // the staged request re-derives params/priority from the
                    // task, but the identity fields must be present here.
                    info.jobRequest.algorithmId = info.algorithmId.toStdString();
                    info.jobRequest.title = info.algorithmName.toStdString();
                    info.hasJobRequest = true;
                }
                else
                {
                    info.autoDispatch = false;
                    info.fusedMember = true;
                }
            }

            info.outputLayerPath = findOutputPathInParams( params );

            info.logBuffer.append( QString( QStringLiteral( "[%1] Pipeline step %2 queued." ) )
                                     .arg( info.startTime.toString( QStringLiteral( "yyyy-MM-dd hh:mm:ss" ) ),
                                           QString::fromStdString( stepId ) ) );

            m_tasks[taskId] = info;
            // 8.0 WP-A: derived admission state for the pipeline step
            // (children index + ready-heap / manual-queue registration; fused
            // members are autoDispatch=false and land on the manual list,
            // preserving the legacy per-pass placeholder pass for them).
            registerParentLinksLocked( m_tasks[taskId] );
            if ( m_tasks[taskId].autoDispatch )
                pushReadyCandidateLocked( taskId );
            else if ( parentsSatisfiedLocked( m_tasks[taskId] ) )
                m_manualQueued.append( taskId );
            stepToTaskId[stepId] = taskId;
            pipeInfo.stepToTaskId[stepId] = taskId;
            pipeInfo.taskToStepId[taskId] = stepId;
            pipeInfo.stepStatuses[stepId] = TaskStatus::Queued;

            queueTaskAddedLocked( taskId );

            // Submission-time execution fingerprint (#726): computed in
            // topological order on the submitting thread, so each step's
            // chained inputs can key on its producers' already-computed
            // fingerprints — a cold pipeline records a usable cache identity
            // for EVERY deterministic step during its first run, and
            // worker-thread admission never needs the catalog.
            {
                QMap<std::string, long> declaredTaskByStepId;
                for ( auto dIt = declaredOutputByStepId.constBegin();
                      dIt != declaredOutputByStepId.constEnd(); ++dIt )
                    declaredTaskByStepId.insert( dIt.key().toStdString(),
                                                 stepToTaskId.value( dIt.key().toStdString(), -1 ) );
                UpstreamResolver resolver =
                    [this, declaredTaskByStepId, declaredOutputByStepId](
                        const sicnu::workflow::PlaceholderRef &ref,
                        const QString & ) -> UpstreamResolution {
                    if ( ref.stepId.empty() )
                        return {};
                    const QString stepKey = QString::fromStdString( ref.stepId );
                    const auto outIt = declaredOutputByStepId.constFind( stepKey );
                    if ( outIt == declaredOutputByStepId.constEnd() )
                        return {};
                    UpstreamResolution upstream;
                    upstream.producerTaskId = declaredTaskByStepId.value( stepKey.toStdString(), -1 );
                    upstream.declaredOutputPath = outIt.value();
                    return upstream;
                };
                computeAndRecordSubmissionFingerprintLocked( taskId, resolver );
            }

            declaredOutputByStepId.insert( QString::fromStdString( stepId ),
                                           findOutputPathInParams( params ) );
        }

        // Phase C: bind the fused head to its member tasks so completion of
        // the head completes the members with the tail payload.
        if ( fusedPlan.stepIds.size() >= 2 )
        {
            FusedChainBinding binding;
            for ( const auto &memberStepId : fusedPlan.stepIds )
            {
                if ( memberStepId == fusedPlan.headStepId )
                    continue;
                const long memberTaskId = stepToTaskId.value( memberStepId, -1 );
                if ( memberTaskId > 0 )
                    binding.memberTaskIds.append( memberTaskId );
            }
            if ( !binding.memberTaskIds.isEmpty() )
            {
                const long headTaskId = stepToTaskId.value( fusedPlan.headStepId, -1 );
                if ( headTaskId > 0 )
                    m_fusedChains[headTaskId] = binding;
            }
        }

        if ( pipeInfo.stepToTaskId.isEmpty() )
        {
            pipeInfo.isCompleted = true;
            if ( def.steps.empty() )
            {
                pipeInfo.isFailed = false;
            }
            else
            {
                pipeInfo.isFailed = true;
                pipeInfo.errorMessage = QStringLiteral( "Pipeline contains no dispatchable operator steps" );
            }
            m_pipelines[pipelineId] = pipeInfo;
            m_waitCondition.wakeAll();
            return pipelineId;
        }

        m_pipelines[pipelineId] = pipeInfo;
        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();
    return pipelineId;
}

long TaskCenter::submitPipelineJson( const std::string &jsonPipeline, bool autoLoad )
{
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( jsonPipeline.c_str(), jsonPipeline.c_str() + jsonPipeline.length(), &root, &errs ) )
        return -1;

    sicnu::workflow::WorkflowDefinition def;
    std::string parseErr;
    if ( !sicnu::workflow::workflowDefinitionFromJson( root, def, parseErr ) )
        return -1;

    return submitPipeline( def, autoLoad );
}

PipelineExecutionInfo TaskCenter::getPipelineInfo( long pipelineId ) const
{
    QMutexLocker locker( &m_mutex );
    return m_pipelines.value( pipelineId );
}

AlgorithmTaskInfo TaskCenter::waitForTask( long taskId,
                                            std::chrono::milliseconds timeout,
                                            std::chrono::milliseconds pollInterval ) const
{
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + timeout;

    QMutexLocker locker( &m_mutex );
    for ( ;; )
    {
        if ( m_isShuttingDown.load() )
        {
            auto it = m_tasks.find( taskId );
            return it != m_tasks.end() ? *it : AlgorithmTaskInfo{};
        }

        auto it = m_tasks.find( taskId );
        if ( it == m_tasks.end() || isTerminalStatus( it->status ) )
        {
            return it != m_tasks.end() ? *it : AlgorithmTaskInfo{};
        }

        const auto now = clock::now();
        if ( now >= deadline )
        {
            return *it;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>( deadline - now );
        const auto waitTime = std::min( remaining, pollInterval );

        m_waitCondition.wait( &m_mutex, static_cast<unsigned long>( waitTime.count() ) );
    }
}

PipelineExecutionInfo TaskCenter::waitForPipeline( long pipelineId,
                                                    std::chrono::milliseconds timeout,
                                                    std::chrono::milliseconds pollInterval ) const
{
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + timeout;

    QMutexLocker locker( &m_mutex );
    for ( ;; )
    {
        if ( m_isShuttingDown.load() )
        {
            auto it = m_pipelines.find( pipelineId );
            return it != m_pipelines.end() ? *it : PipelineExecutionInfo{};
        }

        auto it = m_pipelines.find( pipelineId );
        if ( it == m_pipelines.end() || it->isCompleted || it->isFailed )
        {
            return it != m_pipelines.end() ? *it : PipelineExecutionInfo{};
        }

        const auto now = clock::now();
        if ( now >= deadline )
        {
            return *it;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>( deadline - now );
        const auto waitTime = std::min( remaining, pollInterval );

        m_waitCondition.wait( &m_mutex, static_cast<unsigned long>( waitTime.count() ) );
    }
}


void TaskCenter::setCatalog( sicnu::data::DataManager *catalog )
{
    QMutexLocker locker( &m_mutex );
    m_catalog = catalog;
}

void TaskCenter::computeAndRecordSubmissionFingerprintLocked( long taskId,
                                                              const UpstreamResolver &resolver )
{
    // Disabled cache ⇒ no fingerprint at all (also skips the operator
    // lookup/hash work for every submission in default configurations).
    if ( !sicnu::data::ExecutionResultCache::instance().isEnabled() )
        return;
    if ( !m_catalog )
        return;
    // The catalog is single-thread-affine by contract (its mutators enforce
    // it); identity resolution from a foreign thread would race concurrent
    // mutations — conservatively refuse to fingerprint there. Computing at
    // SUBMISSION time (this call) keeps the affinity check on the submitting
    // thread, so downstream steps admitted later on JobEngine worker threads
    // reuse the recorded fingerprint without any catalog access (#726).
    if ( QThread::currentThread() != m_catalog->thread() )
        return;

    const auto it = m_tasks.constFind( taskId );
    if ( it == m_tasks.constEnd() )
        return;
    const AlgorithmTaskInfo &info = it.value();

    // Only registered RSOperators carry the schema/metadata contract needed
    // to prove determinism; provider algorithms (gdal:/otb:/qgis:) and
    // one-shot callables stay uncached.
    const auto op = sicnu::operators::RSOperatorRegistry::instance().create(
        info.algorithmId.toStdString() );
    if ( !op )
        return;

    // Determinism gate — two equivalent opt-in surfaces:
    //   1. metadata()["deterministic"] == true (explicit, e.g. temporal ops);
    //   2. determinismGrade() == "bit-exact" (ADR 0124 / #659 — repeated
    //      identical runs produce byte-identical outputs, so a cached
    //      artifact is exactly what a re-run would write).
    // "tolerance"-grade operators stay uncached: their outputs may legitimately
    // vary within tolerance, and a served artifact must be trustworthy.
    const Json::Value meta = op->metadata();
    const bool deterministicOptIn =
        ( meta.isMember( "deterministic" ) && meta["deterministic"].asBool() )
        || op->determinismGrade() == "bit-exact";
    if ( !deterministicOptIn )
        return;

    // Implementation/version identity (#726): the operator's schema document
    // PLUS the explicit execution-cache contract version and the platform
    // version. A schema change (new params, changed defaults) implies a
    // behavior change; the contract version additionally invalidates every
    // entry computed under an older fingerprint SEMANTICS (a behavior fix
    // that leaves the schema untouched must not serve stale artifacts).
    // 8.0: the canonical recipe moved into makeImplementationIdentity so the
    // resume-side operator stamp (WorkflowRunCoordinator) hashes the SAME
    // identity surface — one recipe, no drift.
    Json::StreamWriterBuilder schemaWriter;
    schemaWriter["indentation"] = "";
    const std::string schemaText = Json::writeString( schemaWriter, op->schema() );
    const QString versionHash = sicnu::data::makeImplementationIdentity( schemaText ).toHex();

    // Statically resolve placeholder references against the DECLARED upstream
    // outputs (#726): the resolved map is what the fingerprint hashes, and the
    // dispatch-time verification compares it against the actually-substituted
    // parameters. Output-vocabulary keys are excluded from the hashed params
    // by KEY — never by string-value equality, which collided
    // {input:x,output:x} with {input:y,output:y} and made the fingerprint
    // non-injective. A destination value under any OTHER key stays hashed.
    QVariantMap resolvedAll;   // full statically-resolved map (dispatch verification)
    QJsonObject hashedParams;  // output-vocabulary keys excluded (identity)
    QString currentParamKey;
    QMap<QString, long> chainedProducers; // param key → upstream producer task
    auto resolveRef = [this, taskId, &resolver, &chainedProducers, &currentParamKey](
                          const sicnu::workflow::PlaceholderRef &ref ) -> std::string {
        if ( resolver )
        {
            const UpstreamResolution upstream = resolver( ref, currentParamKey );
            if ( upstream.producerTaskId > 0 && !upstream.declaredOutputPath.isEmpty() )
            {
                if ( !currentParamKey.isEmpty() )
                    chainedProducers.insert( currentParamKey, upstream.producerTaskId );
                return upstream.declaredOutputPath.toStdString();
            }
        }
        return ref.rawRef; // unresolved: dispatch verification fails closed
    };
    for ( auto pIt = info.parameterMap.begin(); pIt != info.parameterMap.end(); ++pIt )
    {
        currentParamKey = pIt.key();
        const QVariant substituted = substituteVariantRecursive( pIt.value(), resolveRef );
        resolvedAll.insert( pIt.key(), substituted );
        if ( !sicnu::data::isOutputVocabularyKey( pIt.key() ) )
            hashedParams.insert( pIt.key(), QJsonValue::fromVariant( substituted ) );
    }

    // Revision-aware input identity (#726): registered local/remote assets +
    // inline scenes + workspace-bound temporal collections. ANY unidentifiable
    // input ⇒ not cacheable — the conservative verdict that keeps hits honest.
    // Chained producer edges are excluded by PARAMETER KEY (their identity is
    // the producer fingerprint added below, not the file's registration
    // revision); a literal key that merely carries the same path stays
    // scanned and revision-stamped.
    QStringList chainedKeys;
    for ( auto cIt = chainedProducers.constBegin(); cIt != chainedProducers.constEnd(); ++cIt )
        chainedKeys.append( cIt.key() );
    QVector<sicnu::data::TaggedDerivationInput> inputs;
    QString reason;
    // Registered-input stat binding (issue #749): captured at submission so
    // the store path can vouch for the exact input bytes this step read.
    QMap<QString, qint64> registeredInputSizes;
    QMap<QString, qint64> registeredInputMsecs;
    if ( !sicnu::temporal::fingerprintInputsForOperatorParams(
             m_catalog, resolvedAll, &inputs, &reason, chainedKeys,
             &registeredInputSizes, &registeredInputMsecs ) )
    {
        return;
    }

    // Chained in-pipeline producer identity: an input produced by an upstream
    // step of the same submission is keyed on the producer's own execution
    // fingerprint, not on the file's catalog revision (the file is registered
    // only after the producing run finishes; keying on it would both miss the
    // first cold run and chase spurious revision bumps). The determinism gate
    // makes "same producer fingerprint" ⇒ "same output bytes", so the chained
    // identity is as strong as a revision stamp. An upstream step without a
    // valid fingerprint fails this step closed.
    for ( auto cIt = chainedProducers.constBegin(); cIt != chainedProducers.constEnd(); ++cIt )
    {
        const auto producerFp = m_taskFingerprints.constFind( cIt.value() );
        if ( producerFp == m_taskFingerprints.constEnd() || !producerFp->isValid() )
            return;
        sicnu::data::TaggedDerivationInput input;
        input.revision = sicnu::data::AssetRevision::initial();
        input.toPort = cIt.key();
        input.valueDomain = QStringLiteral( "pipeline_output" );
        input.producerFingerprint = producerFp->toHex();
        inputs.append( input );
    }

    const sicnu::data::ExecutionFingerprint fp =
        sicnu::data::makeExecutionFingerprintV2( info.algorithmId, versionHash,
                                                 hashedParams, inputs );

    if ( fp.isValid() )
    {
        m_taskFingerprints[taskId] = fp;
        m_taskFingerprintParams[taskId] = resolvedAll;
        m_taskRegisteredInputStats[taskId] =
            qMakePair( registeredInputSizes, registeredInputMsecs );
        QVector<ChainedEdge> edges;
        for ( auto cIt = chainedProducers.constBegin(); cIt != chainedProducers.constEnd(); ++cIt )
        {
            const auto producerFp = m_taskFingerprints.constFind( cIt.value() );
            if ( producerFp == m_taskFingerprints.constEnd() || !producerFp->isValid() )
                continue;
            const QVariant value = resolvedAll.value( cIt.key() );
            if ( value.typeId() != QMetaType::QString )
                continue;
            ChainedEdge edge;
            edge.paramKey = cIt.key();
            edge.producerPath = value.toString();
            edge.producerTaskId = cIt.value();
            edge.producerFingerprintHex = producerFp->toHex();
            edges.append( edge );
        }
        m_taskChainedEdges[taskId] = edges;
    }
    else
    {
        m_taskFingerprints.remove( taskId );
        m_taskFingerprintParams.remove( taskId );
        m_taskChainedEdges.remove( taskId );
        m_taskRegisteredInputStats.remove( taskId );
    }
}

void TaskCenter::verifyDispatchFingerprintLocked( long taskId )
{
    const auto fpIt = m_taskFingerprints.constFind( taskId );
    if ( fpIt == m_taskFingerprints.constEnd() )
    {
        m_taskFingerprintParams.remove( taskId );
        return;
    }
    const auto snapIt = m_taskFingerprintParams.constFind( taskId );
    if ( snapIt == m_taskFingerprintParams.constEnd()
         || snapIt.value() != m_tasks[taskId].parameterMap )
    {
        // The parameters that will actually execute diverge from the snapshot
        // the fingerprint was computed over (placeholder resolved differently,
        // mutated params): the fingerprint no longer describes this execution.
        // Drop it — a real execution is the safe outcome.
        m_taskFingerprints.remove( taskId );
        m_taskFingerprintParams.remove( taskId );
        m_taskChainedEdges.remove( taskId );
        m_taskRegisteredInputStats.remove( taskId );
        return;
    }
    // Re-verify each chained producer edge: the producer's stamped payload
    // fingerprint must equal the hex this consumer's identity was keyed on
    // (#726 review). Absent stamps are tolerated only for producers without
    // an execution identity — but such producers never yield a valid consumer
    // fingerprint at submission time, so a missing stamp here means the edge
    // is unverifiable ⇒ drop.
    const auto edgesIt = m_taskChainedEdges.constFind( taskId );
    if ( edgesIt != m_taskChainedEdges.constEnd() )
    {
        for ( const ChainedEdge &edge : edgesIt.value() )
        {
            const auto producerIt = m_tasks.constFind( edge.producerTaskId );
            if ( producerIt == m_tasks.constEnd()
                 || !producerIt->resultPayload.isObject()
                 || !producerIt->resultPayload.isMember( "executionFingerprint" )
                 || !producerIt->resultPayload["executionFingerprint"].isString()
                 || producerIt->resultPayload["executionFingerprint"].asString()
                        != edge.producerFingerprintHex.toStdString() )
            {
                // The producer's payload no longer vouches for the execution
                // identity this consumer was keyed on.
                m_taskFingerprints.remove( taskId );
                m_taskFingerprintParams.remove( taskId );
                m_taskChainedEdges.remove( taskId );
                m_taskRegisteredInputStats.remove( taskId );
                return;
            }
        }
    }
    // Keep the snapshot and edges until the terminal transition consumes them.
}

namespace
{
QByteArray compactJsonBytes( const Json::Value &value )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return QByteArray::fromStdString( Json::writeString( builder, value ) );
}

Json::Value jsonValueFromQJson( const QJsonValue &value )
{
    switch ( value.type() )
    {
      case QJsonValue::Null:
        return Json::Value();
      case QJsonValue::Bool:
        return Json::Value( value.toBool() );
      case QJsonValue::Double:
      {
        const double d = value.toDouble();
        if ( d == static_cast<qint64>( d ) && std::fabs( d ) < 9.0e18 )
            return Json::Value( static_cast<Json::Int64>( d ) );
        return Json::Value( d );
      }
      case QJsonValue::String:
        return Json::Value( value.toString().toStdString() );
      case QJsonValue::Array:
      {
        Json::Value out( Json::arrayValue );
        const QJsonArray array = value.toArray();
        for ( const QJsonValue &entry : array )
            out.append( jsonValueFromQJson( entry ) );
        return out;
      }
      case QJsonValue::Object:
      {
        Json::Value out( Json::objectValue );
        const QJsonObject object = value.toObject();
        for ( auto it = object.begin(); it != object.end(); ++it )
            out[it.key().toStdString()] = jsonValueFromQJson( it.value() );
        return out;
      }
      case QJsonValue::Undefined:
        break;
    }
    return Json::Value();
}

/// Recursively collects the files an execution PRODUCED, from the result
/// payload (#726). Two guards keep echoed inputs out of the produced set
/// (an input claimed as an artifact would hijack the producing run's cache
/// claim): the string must ride under an output-vocabulary key
/// ("output"/"outputs[*].output"/...), and it must be the declared output or
/// live beside it (the run's output workspace). Single-output steps whose
/// payload carries no such key are still covered — the declared output is
/// added by the caller when it exists.
void collectProducedPayloadPaths( const Json::Value &value, const QString &keyContext,
                                  const QString &declared, QStringList &out )
{
    if ( value.isObject() )
    {
        for ( const auto &name : value.getMemberNames() )
            collectProducedPayloadPaths( value[name], QString::fromStdString( name ),
                                         declared, out );
    }
    else if ( value.isArray() )
    {
        for ( const auto &entry : value )
            collectProducedPayloadPaths( entry, keyContext, declared, out );
    }
    else if ( value.isString() )
    {
        const QString candidate = QString::fromStdString( value.asString() ).trimmed();
        if ( candidate.isEmpty() || !QFile::exists( candidate )
             || out.contains( candidate )
             || !sicnu::data::isOutputVocabularyKey( keyContext ) )
            return;
        if ( candidate == declared
             || QFileInfo( candidate ).absolutePath() == QFileInfo( declared ).absolutePath() )
        {
            out.append( candidate );
        }
    }
}

/// Maps a producing run's artifact path onto the serving run's destination
/// space (#726): the declared output maps onto the new declared output; a
/// sibling artifact (grouped period raster) maps to the new destination's
/// directory with the producer's base name stem substituted — the same
/// convention operators use to derive sibling names (`<base>_<label>.tif`).
/// Unmappable paths are returned unchanged (byte-identical under the
/// determinism gate, so serving the original location stays correct).
QString mapProducedPath( const QString &cachedPath, const QString &cachedDeclared,
                         const QString &servingDeclared )
{
    if ( cachedPath.isEmpty() || cachedDeclared.isEmpty() || cachedPath == cachedDeclared )
    {
        return servingDeclared.isEmpty() ? cachedPath : servingDeclared;
    }
    const QFileInfo declaredFi( cachedDeclared );
    const QFileInfo servingFi( servingDeclared );
    const QString cachedDir = declaredFi.absolutePath();
    if ( !cachedPath.startsWith( cachedDir + QLatin1Char( '/' ) ) )
        return cachedPath;
    const QString relative = cachedPath.mid( cachedDir.size() + 1 );
    const QString cachedStem = declaredFi.completeBaseName();
    const QString servingStem = servingFi.completeBaseName();
    QString mapped = relative;
    if ( !cachedStem.isEmpty() && cachedStem != servingStem
         && relative.startsWith( cachedStem + QLatin1Char( '_' ) ) )
    {
        // Operator sibling convention: <declared-stem>_<label>.<ext>. Prefix
        // (not substring) matching — a file whose name merely CONTAINS the
        // stem is not a sibling artifact, and a sibling can never map onto
        // the declared destination itself (it keeps a suffix after the stem).
        mapped = servingStem + relative.mid( cachedStem.size() );
    }
    return servingFi.absolutePath() + QLatin1Char( '/' ) + mapped;
}

/// Recursively rewrites every payload string that references a producing
/// run's path onto the serving run's destination space, so a served payload
/// is exactly what a real run at this destination would have returned.
void rewriteJsonPaths( QJsonValue &value, const QMap<QString, QString> &pathMap )
{
    if ( value.isObject() )
    {
        QJsonObject object = value.toObject();
        for ( auto it = object.begin(); it != object.end(); ++it )
        {
            QJsonValue rewritten = it.value();
            rewriteJsonPaths( rewritten, pathMap );
            it.value() = rewritten;
        }
        value = object;
    }
    else if ( value.isArray() )
    {
        QJsonArray array = value.toArray();
        for ( int i = 0; i < array.size(); ++i )
        {
            QJsonValue rewritten = array.at( i );
            rewriteJsonPaths( rewritten, pathMap );
            array.replace( i, rewritten );
        }
        value = array;
    }
    else if ( value.isString() )
    {
        const auto mapped = pathMap.constFind( value.toString() );
        if ( mapped != pathMap.constEnd() )
            value = mapped.value();
    }
}

struct ServeTransfer
{
    QString src; // cached artifact
    QString dst; // this run's destination
    QString tmp; // same-directory staging name (atomic rename source)
};

/// Expected identity of a staged source file, so the serve can detect that
/// the cached artifact was rewritten between lookup and materialization
/// (lookup validation alone has a TOCTOU window against a concurrent
/// execution overwriting the same stable path).
struct SourceExpectation
{
    qint64 size = -1;
    qint64 msecs = 0;
};

/// Transactional materialization (cache contract C7): stage EVERY transfer as
/// a same-directory temp copy first (so a partial stage never touches a
/// destination), then atomically rename each into place. Any staging failure
/// aborts with all temps removed and destinations untouched; a rename
/// failure aborts the serve — the subsequent real execution rewrites every
/// destination, so no half-served state survives. Cross-filesystem safe (the
/// temp lives beside its destination).
bool materializeCachedArtifacts( const QList<ServeTransfer> &transfers,
                                 const QStringList &staleSidecars,
                                 const QString &isolationToken,
                                 const QMap<QString, SourceExpectation> &expected )
{
    QList<ServeTransfer> staged;
    auto cleanupStaged = [ &staged ]() {
        for ( const ServeTransfer &transfer : staged )
            QFile::remove( transfer.tmp );
    };
    // The staging name is isolated per serving execution (fingerprint prefix):
    // two serves targeting the same destination must not consume each other's
    // temp file mid-flight. Identical fingerprints stage identical bytes, but
    // a distinct fingerprint serving the same destination is a legitimate
    // (last-writer-wins) user sequence, and its temp must stay its own.
    const QString stagingSuffix = isolationToken.isEmpty()
        ? QStringLiteral( ".cacheserve.tmp" )
        : QStringLiteral( ".%1.cacheserve.tmp" ).arg( isolationToken );
    for ( const ServeTransfer &transfer : transfers )
    {
        ServeTransfer stagedTransfer = transfer;
        stagedTransfer.tmp = transfer.dst + stagingSuffix;
        QFile::remove( stagedTransfer.tmp );
        if ( !QFile::copy( transfer.src, stagedTransfer.tmp ) )
        {
            cleanupStaged();
            return false;
        }
        staged.append( stagedTransfer );
    }
    // Post-stage source revalidation (closes the lookup→copy TOCTOU): if any
    // cached source changed size or mtime while being copied, the staged copy
    // may not be what this fingerprint vouches for — abort the serve and fall
    // through to a real execution. Never rename questionable bytes.
    for ( const ServeTransfer &transfer : staged )
    {
        const auto expectation = expected.constFind( transfer.src );
        if ( expectation == expected.constEnd() )
            continue; // sidecars have no recorded expectation
        const QFileInfo info( transfer.src );
        if ( !info.isFile() || info.size() != expectation->size
             || info.lastModified().toMSecsSinceEpoch() != expectation->msecs )
        {
            cleanupStaged();
            return false;
        }
    }
    // Stale sidecars of the destinations that the cached execution does not
    // carry (e.g. an .aux.xml from an unrelated earlier run) must not survive
    // the replacement.
    for ( const QString &stale : staleSidecars )
        QFile::remove( stale );
    for ( const ServeTransfer &transfer : staged )
    {
        // POSIX rename replaces atomically; on Windows rename fails when the
        // destination exists — fall back to remove-then-rename there (the
        // non-atomic window is a platform limitation, the old naked
        // remove+copy had it on every platform).
        if ( !QFile::rename( transfer.tmp, transfer.dst ) )
        {
            QFile::remove( transfer.dst );
            if ( !QFile::rename( transfer.tmp, transfer.dst ) )
            {
                cleanupStaged();
                return false;
            }
        }
    }
    return true;
}
} // namespace

void TaskCenter::storeExecutionResultLocked( long taskId )
{
    const auto fpIt = m_taskFingerprints.constFind( taskId );
    if ( fpIt == m_taskFingerprints.constEnd() || !fpIt->isValid() )
    {
        m_taskFingerprintParams.remove( taskId );
        return;
    }
    const sicnu::data::ExecutionFingerprint fp = *fpIt;
    m_taskFingerprints.erase( fpIt );
    m_taskFingerprintParams.remove( taskId );
    const QVector<ChainedEdge> edges = m_taskChainedEdges.value( taskId );
    m_taskChainedEdges.remove( taskId );
    // Registered-input stat bindings captured at submission (issue #749) —
    // read BEFORE the cleanup removes them.
    const QPair<QMap<QString, qint64>, QMap<QString, qint64>> registeredStats =
        m_taskRegisteredInputStats.value( taskId );
    m_taskRegisteredInputStats.remove( taskId );

    const auto taskIt = m_tasks.constFind( taskId );
    if ( taskIt == m_tasks.constEnd() )
        return;
    const QString declared = taskIt->outputLayerPath;
    if ( declared.isEmpty() )
        return;

    sicnu::data::ExecutionResultCache::CachedExecution execution;
    execution.declaredOutputPath = declared;

    QStringList produced;
    collectProducedPayloadPaths( taskIt->resultPayload, QString(), declared, produced );
    if ( QFile::exists( declared ) && !produced.contains( declared ) )
        produced.append( declared );
    execution.producedArtifacts = produced;

    // Stat every produced artifact so a lookup can refuse an entry whose
    // files were replaced by a different execution (destination reuse
    // poisoning, #726).
    for ( const QString &artifact : produced )
    {
        const QFileInfo info( artifact );
        if ( !info.isFile() )
            continue;
        execution.artifactSizes.insert( artifact, info.size() );
        execution.artifactMsecs.insert( artifact, info.lastModified().toMSecsSinceEpoch() );
    }
    if ( execution.artifactSizes.isEmpty() )
        return; // nothing materialized to vouch for — never store a claim
    // Bind the consumer's entry to the chained input bytes it actually read:
    // an intermediate rewritten out-of-band must invalidate this entry on the
    // next lookup, or the producer's self-heal would silently mask the
    // poisoning (#726 review).
    for ( const ChainedEdge &edge : edges )
    {
        const QFileInfo info( edge.producerPath );
        if ( !info.isFile() )
            continue;
        execution.inputSizes.insert( edge.producerPath, info.size() );
        execution.inputMsecs.insert( edge.producerPath,
                                     info.lastModified().toMSecsSinceEpoch() );
    }
    // Bind registered (non-chained) inputs to the bytes observed at
    // submission (issue #749) — the same moment the fingerprint identity was
    // computed, so stat + identity describe one provable state. An
    // out-of-band rewrite of a registered input now invalidates this entry
    // at lookup, exactly like a chained intermediate rewrite always did.
    for ( auto it = registeredStats.first.constBegin();
          it != registeredStats.first.constEnd(); ++it )
    {
        execution.inputSizes.insert( it.key(), it.value() );
    }
    for ( auto it = registeredStats.second.constBegin();
          it != registeredStats.second.constEnd(); ++it )
    {
        execution.inputMsecs.insert( it.key(), it.value() );
    }

    execution.resultPayload = QJsonDocument::fromJson( compactJsonBytes( taskIt->resultPayload ) );
    sicnu::data::ExecutionResultCache::instance().storeExecution( fp, execution );
}

bool TaskCenter::serveFromExecutionCache( long taskId, const sicnu::data::ExecutionFingerprint &fp )
{
    const auto cached =
        sicnu::data::ExecutionResultCache::instance().lookupExecution( fp );
    if ( !cached )
        return false;

    QString outputPath;
    {
        QMutexLocker locker( &m_mutex );
        const auto it = m_tasks.constFind( taskId );
        if ( it == m_tasks.constEnd() || isTerminalStatus( it->status ) )
            return false;
        outputPath = it->outputLayerPath;
    }
    if ( outputPath.isEmpty() )
        return false;

    // Map the producing run's artifacts onto this run's destination space
    // (cache contract C6): the declared output onto the declared output, a
    // grouped/period sibling onto its mapped sibling name. Artifacts at the
    // exact producing location (the common identical-resubmission case) need
    // no transfer.
    QMap<QString, QString> pathMap; // producing path → serving path
    QList<ServeTransfer> transfers;
    QStringList staleSidecars;
    // Phase E: when the entry was reconstructed from the persistent
    // content-addressed tier, bytes are copied from the pool object (digest
    // verified at lookup) while payload mapping keeps the original paths.
    const bool fromPool = !cached->sourceOverrides.isEmpty();
    for ( const QString &artifact : cached->producedArtifacts )
    {
        if ( artifact.isEmpty() )
            continue;
        const QString copySource = cached->sourceOverrides.value( artifact, artifact );
        const QString mapped = ( artifact == cached->declaredOutputPath )
                                   ? outputPath
                                   : mapProducedPath( artifact, cached->declaredOutputPath,
                                                      outputPath );
        pathMap.insert( artifact, mapped );
        // In-place serving is only safe when the bytes are the ones the
        // in-memory entry validated. A pool-reconstructed entry vouches for
        // the POOL object, never for whatever currently sits at the original
        // path — always transfer in that case.
        if ( !fromPool
             && QFileInfo( artifact ).absoluteFilePath() == QFileInfo( mapped ).absoluteFilePath()
             && QFile::exists( mapped ) )
            continue; // same file, still in place — nothing to materialize
        // Sidecar contract: sidecars present beside the cached artifact are
        // carried over; destination sidecars the cached execution does not
        // carry are stale (from an unrelated earlier run) and must not
        // survive the replacement. Pool objects carry no sidecars — a served
        // execution therefore clears stale destination sidecars.
        for ( const QString &suffix : sicnu::workflow::ArtifactGC::sidecarSuffixes() )
        {
            if ( !fromPool && QFile::exists( artifact + suffix ) )
            {
                pathMap.insert( artifact + suffix, mapped + suffix );
                transfers.append( { artifact + suffix, mapped + suffix, QString() } );
            }
            else if ( QFile::exists( mapped + suffix ) )
            {
                staleSidecars.append( mapped + suffix );
            }
        }
        transfers.append( { copySource, mapped, QString() } );
    }
    // The declared output itself is never written by grouped executions; only
    // the produced artifact set is materialized and only produced paths land
    // in the payload, so a served run never creates a file the producing run
    // did not declare.
    QMap<QString, SourceExpectation> expected;
    const auto recordExpectation = [ &expected ]( const QString &path,
                                                  const QMap<QString, qint64> &sizes,
                                                  const QMap<QString, qint64> &msecs ) {
        const auto sizeIt = sizes.constFind( path );
        if ( sizeIt == sizes.constEnd() )
            return;
        SourceExpectation e;
        e.size = sizeIt.value();
        const auto msecsIt = msecs.constFind( path );
        e.msecs = msecsIt == msecs.constEnd() ? 0 : msecsIt.value();
        expected.insert( path, e );
    };
    for ( const ServeTransfer &transfer : transfers )
    {
        recordExpectation( transfer.src, cached->artifactSizes, cached->artifactMsecs );
    }
    if ( ( !transfers.isEmpty() || !staleSidecars.isEmpty() )
         && !materializeCachedArtifacts( transfers, staleSidecars, fp.toHex().left( 16 ),
                                         expected ) )
        return false; // fall through to a real execution (cache contract C7)

    // Execution Plane 7.0: post-serve destination verification. The in-memory
    // tier's bytes were stat-bound at store time; a transfer that landed
    // short (disk pressure, concurrent writer at the destination) must not
    // surface as a successful cache hit. Compare every transferred
    // destination against the entry's stat binding for its producing source;
    // any mismatch falls through to a real execution — a cache hit is only
    // served when the output it vouches for is verifiably at the destination.
    for ( auto srcIt = pathMap.constBegin(); srcIt != pathMap.constEnd(); ++srcIt )
    {
        const QString &produced = srcIt.key();
        const QString &served = srcIt.value();
        if ( QFileInfo( produced ).absoluteFilePath() == QFileInfo( served ).absoluteFilePath() )
            continue; // in-place: validated by lookupExecution
        const auto sizeIt = cached->artifactSizes.constFind( produced );
        if ( sizeIt == cached->artifactSizes.constEnd() )
            continue; // sidecar or unmapped sibling: no binding to check
        if ( !QFile::exists( served ) || QFileInfo( served ).size() != sizeIt.value() )
            return false; // fall through to a real execution (cache contract C7)
    }

    // Restore the producing run's full result payload with this run's paths,
    // so GUI auto-load, agents and workflow placeholder resolution see
    // exactly what a real execution at this destination returns.
    QJsonDocument restored = cached->resultPayload;
    if ( restored.isObject() )
    {
        QJsonValue payloadValue( restored.object() );
        rewriteJsonPaths( payloadValue, pathMap );
        QJsonObject payloadObject = payloadValue.toObject();
        payloadObject.insert( QStringLiteral( "cache" ), QStringLiteral( "hit" ) );
        payloadObject.insert( QStringLiteral( "cachedFrom" ),
                              cached->declaredOutputPath );
        payloadObject.insert( QStringLiteral( "executionFingerprint" ), fp.toHex() );
        restored = QJsonDocument( payloadObject );
    }

    const Json::Value payload = jsonValueFromQJson( QJsonValue( restored.object() ) );
    QVariantMap results{ { QStringLiteral( "output" ), outputPath } };
    markTaskCompleted( taskId, results, payload );
    return true;
}

} // namespace sicnu

#include "task_center.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QMutexLocker>
#include <QSet>
#include <QTimer>
#include <utility>

#include "framework/json_params_converter.h"
#include "atomic_algorithm_registry.h"
#include "jobs/job_engine.h"
#include "workflow/workflow_definition.h"
#include "workflow/placeholder_grammar.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/temporal/temporal_workspace.h"
#include "data/data_manager.h"

#include <QCryptographicHash>
#include <QJsonObject>
#include <QThread>

namespace sicnu {

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
        if ( it.key().contains( QStringLiteral( "OUTPUT" ), Qt::CaseInsensitive )
             || it.key().contains( QStringLiteral( "RESULT" ), Qt::CaseInsensitive ) )
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
            info.status = TaskStatus::Canceled;
            info.errorMessage = wasCancelling ? QStringLiteral( "Canceled during shutdown" )
                                              : QStringLiteral( "Canceled: application is shutting down" );
            info.endTime = QDateTime::currentDateTimeUtc();
            info.logBuffer.append( info.errorMessage );
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
}

void TaskCenter::shutdownForTests()
{
    shutdown();
    {
        QMutexLocker locker( &m_mutex );
        m_isShuttingDown.store( false );
        m_tasks.clear();
        m_pipelines.clear();
        m_pendingLaunches.clear();
        m_pendingTaskAdded.clear();
        m_pendingTaskUpdated.clear();
        m_pendingLogs.clear();
        m_taskByJobId.clear();
        m_forwardedLogCounts.clear();
        m_lastForwardedProgress.clear();
        m_estimateMbCache.clear(); // task ids restart at 1: stale estimates must not leak across tests
        m_completionCallbacks.clear();
        m_taskFingerprints.clear();
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
    QMutexLocker locker( &m_mutex );
    m_profileLimits[profile] = std::max( 1u, maxConcurrent );
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
    // Keep the resource-aware budget consistent with the restored watermark so a
    // test reset returns to the default scheduling behavior (perf/architecture goal).
    m_resourceBudget.setBudgetMb( m_resourceMonitor.memoryLimitMb() );
}

void TaskCenter::setGlobalConcurrencyLimit( unsigned int maxConcurrent )
{
    QMutexLocker locker( &m_mutex );
    m_globalConcurrencyLimit = std::max( 1u, maxConcurrent );
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
    QMutexLocker locker( &m_mutex );
    m_resourceMonitor.setMemoryLimitMb( mb );
    // Keep the resource-aware budget in sync with the watermark (the documented
    // invariant — both gates must use the same cap, perf/architecture goal §7).
    m_resourceBudget.setBudgetMb( mb );
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
    QMutexLocker locker( &m_mutex );
    m_resourceBudget.setBudgetMb( mb );
}

unsigned int TaskCenter::resourceBudgetMb() const
{
    QMutexLocker locker( &m_mutex );
    return m_resourceBudget.budgetMb();
}

void TaskCenter::setEstimateResolver( TaskEstimateResolver resolver )
{
    QMutexLocker locker( &m_mutex );
    if ( resolver )
        m_resourceBudget.setEstimateResolver( std::move( resolver ) );
    else
        installDefaultEstimateResolver();
    // A different resolver may produce different estimates: drop the per-task
    // cache so subsequent passes re-resolve (#702).
    m_estimateMbCache.clear();
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
        queueTaskAddedLocked( id );

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
            it->status = TaskStatus::Canceled;
            it->errorMessage = QStringLiteral( "Canceled: application is shutting down" );
            it->endTime = QDateTime::currentDateTimeUtc();
            queueTaskUpdatedLocked( taskId );
        }
        else
        {
            it->jobRequest = request;
            it->hasJobRequest = true;
            it->jobExecutor = std::move( executor );
            it->jobCancelHook = std::move( onCancel );
            it->autoDispatch = true;
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
        if ( it == m_taskByJobId.end() )
            return; // job not submitted through Task Center (e.g. direct engine use in tests)
        taskId = it.value();
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
                it->status = TaskStatus::Running;
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

void TaskCenter::processNextQueuedTasks()
{
    // Called with m_mutex held. Only stages work; callers must flushPendingLaunches() outside the lock.
    if ( m_isShuttingDown.load() )
        return; // shutdown stages nothing new (#684)
    const unsigned int globalMax = m_globalConcurrencyLimit > 0
                                     ? m_globalConcurrencyLimit
                                     : defaultLimitForProfile( ProviderResourceProfile::InProcessThread );

    QMap<ProviderResourceProfile, unsigned int> runningByProfile;
    unsigned int totalRunning = 0;
    unsigned int runningTotalMb = 0; // RAM estimate sum of active tasks (resource-aware gate)

    // Pending launches (Dispatching) already hold their slot; count each
    // active task once. Cancelling tasks still occupy their worker slot until
    // the job reports its terminal record, and Paused tasks hold their
    // (engine or QgsTask) slot too — counting them prevents over-admission
    // while real workers stay busy (#702).
    for ( const auto &t : m_tasks )
    {
        if ( t.status != TaskStatus::Running && t.status != TaskStatus::Cancelling
             && t.status != TaskStatus::Dispatching && t.status != TaskStatus::Paused )
            continue;
        runningByProfile[t.resourceProfile] = runningByProfile.value( t.resourceProfile, 0u ) + 1u;
        ++totalRunning;
        runningTotalMb += taskEstimateMbLocked( t );
    }

    QList<long> eligibleIds;
    for ( auto it = m_tasks.begin(); it != m_tasks.end(); ++it )
    {
        // WaitingResource tasks are launch-eligible candidates held by resource
        // admission on a previous pass; they compete with Queued tasks here.
        if ( it.value().status != TaskStatus::Queued && it.value().status != TaskStatus::WaitingResource )
            continue;

        bool parentsSatisfied = true;
        for ( long parentId : it.value().parentTaskIds )
        {
            if ( m_tasks.contains( parentId ) && m_tasks[parentId].status != TaskStatus::Completed )
            {
                parentsSatisfied = false;
                break;
            }
        }

        if ( parentsSatisfied )
            eligibleIds.append( it.key() );
    }

    std::sort( eligibleIds.begin(), eligibleIds.end(), [this]( long a, long b ) {
        if ( m_tasks[a].priority != m_tasks[b].priority )
            return static_cast<int>( m_tasks[a].priority ) < static_cast<int>( m_tasks[b].priority );
        return m_tasks[a].taskId < m_tasks[b].taskId;
    } );

    QList<long> resourceBlockedIds;
    QString globalHoldReason;

    for ( long id : eligibleIds )
    {
        if ( totalRunning >= globalMax )
        {
            globalHoldReason = QStringLiteral( "Waiting for a worker slot (%1/%2 running)." )
                                   .arg( totalRunning )
                                   .arg( globalMax );
            break;
        }

        applyPlaceholdersForTask( id );

        if ( !m_tasks[id].autoDispatch )
            continue;
        if ( !m_tasks[id].jobId.empty() )
            continue;
        if ( m_tasks[id].status != TaskStatus::Queued && m_tasks[id].status != TaskStatus::WaitingResource )
            continue;

        const ProviderResourceProfile profile = m_tasks[id].resourceProfile;
        const unsigned int profileMax = limitForProfileLocked( profile );
        if ( runningByProfile.value( profile, 0u ) >= profileMax )
        {
            resourceBlockedIds.append( id ); // leave queued; another profile may still launch
            continue;
        }

        // ADR 0063: hold all launches when the process RSS is at/above the
        // watermark. Memory pressure is global, so break rather than continue
        // - remaining eligible tasks cannot run either. Blocked tasks stay
        // Queued/WaitingResource and are re-evaluated when a running task
        // finishes (each terminal transition re-enters processNextQueuedTasks).
        if ( m_resourceMonitor.memoryPressureHigh() )
        {
            globalHoldReason = QStringLiteral( "Waiting for memory: process RSS at/above the watermark." );
            if ( totalRunning == 0 && QCoreApplication::instance() )
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
            break;
        }

        // Resource-aware gate (perf/architecture goal 2026-08-08): hold the
        // launch when projected (running + candidate) RAM exceeds the budget,
        // UNLESS nothing at all is running (global never-starve: a wrong or
        // missing estimate must not permanently block all work). A budget of 0
        // disables this gate (legacy behavior). Like the RSS gate this only
        // DELAYS — the task stays queued and is re-evaluated on the next
        // terminal transition. `continue` (not break): a later, lighter
        // eligible task may still fit within the budget this pass.
        const unsigned int candidateMb = taskEstimateMbLocked( m_tasks[id] );
        if ( !m_resourceBudget.canLaunch( runningTotalMb, candidateMb ) )
        {
            resourceBlockedIds.append( id );
            continue;
        }

        // Revision-aware execution fingerprint (#667): computed while the
        // params are final (post placeholder substitution) and BEFORE
        // admission, so flushPendingLaunches can serve an identical prior
        // execution instead of submitting. Invalid ⇒ not cacheable.
        {
            const sicnu::data::ExecutionFingerprint fp = taskExecutionFingerprintLocked( id );
            if ( fp.isValid() )
                m_taskFingerprints[id] = fp;
            else
                m_taskFingerprints.remove( id );
        }

        m_tasks[id].status = TaskStatus::Dispatching;
        m_tasks[id].logBuffer.append(
          QString( QStringLiteral( "[%1] Dispatching to JobEngine (profile=%2)." ) )
            .arg( QDateTime::currentDateTimeUtc().toString( QStringLiteral( "hh:mm:ss" ) ) )
            .arg( static_cast<int>( profile ) ) );
        updatePipelineForTaskLocked( id );

        PendingLaunch launch;
        launch.taskId = id;
        launch.request.algorithmId = m_tasks[id].algorithmId.toStdString();
        launch.request.title = m_tasks[id].algorithmName.toStdString();
        launch.request.source = m_tasks[id].source.isEmpty()
                                  ? ( m_tasks[id].pipelineId >= 0 ? "pipeline" : "task_center" )
                                  : m_tasks[id].source.toStdString();
        launch.request.params = variantMapToJsonParams( m_tasks[id].parameterMap );
        launch.request.priority = static_cast<int>( m_tasks[id].priority );
        launch.onCancel = std::move( m_tasks[id].jobCancelHook );
        if ( m_tasks[id].hasJobRequest && m_tasks[id].jobExecutor )
        {
            launch.executor = m_tasks[id].jobExecutor;
            launch.hasExecutor = true;
            launch.request = m_tasks[id].jobRequest;
            launch.request.params = variantMapToJsonParams( m_tasks[id].parameterMap );
            launch.request.priority = static_cast<int>( m_tasks[id].priority );
        }
        else
        {
            m_tasks[id].jobRequest = launch.request;
            m_tasks[id].hasJobRequest = true;
        }

        queueTaskUpdatedLocked( id );
        m_pendingLaunches.append( std::move( launch ) );
        runningByProfile[profile] = runningByProfile.value( profile, 0u ) + 1u;
        ++totalRunning;
        runningTotalMb += candidateMb;
    }

    // Admission outcome bookkeeping: launch-eligible candidates that did not
    // launch this pass are waiting on resources (worker slots, RSS watermark
    // or the RAM budget), not on pipeline gating. Surface that as an explicit
    // WaitingResource status so entries, panels and tests can distinguish
    // "queued behind the DAG" from "held for admission". Log only on the
    // Queued → WaitingResource transition to avoid spamming re-evaluations.
    for ( long id : eligibleIds )
    {
        if ( m_tasks[id].status != TaskStatus::Queued && m_tasks[id].status != TaskStatus::WaitingResource )
            continue;
        if ( !m_tasks[id].autoDispatch || !m_tasks[id].jobId.empty() )
            continue;

        const bool blockedByIdentifiedResource = resourceBlockedIds.contains( id ) || !globalHoldReason.isEmpty();
        if ( blockedByIdentifiedResource && m_tasks[id].status == TaskStatus::Queued )
        {
            m_tasks[id].status = TaskStatus::WaitingResource;
            m_tasks[id].logBuffer.append( QStringLiteral( "Waiting for resources (admission held)." ) );
            updatePipelineForTaskLocked( id );
            queueTaskUpdatedLocked( id );
        }
        else if ( !blockedByIdentifiedResource && m_tasks[id].status == TaskStatus::WaitingResource )
        {
            // Held only by DAG gating now (parents not yet Completed on a
            // re-evaluation): fall back to plain Queued so the status stays
            // truthful. This can happen when a parent was re-queued/retried.
            m_tasks[id].status = TaskStatus::Queued;
            updatePipelineForTaskLocked( id );
            queueTaskUpdatedLocked( id );
        }
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

    for ( auto &launch : launches )
    {
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
        if ( fp.isValid() && serveFromExecutionCache( launch.taskId, fp ) )
            continue;
        if ( fp.isValid() )
        {
            QMutexLocker lock( &m_mutex );
            if ( m_tasks.contains( launch.taskId ) && !isTerminalStatus( m_tasks[launch.taskId].status ) )
                m_taskFingerprints[launch.taskId] = fp;
        }

        std::string jobId;
        if ( launch.hasExecutor )
            jobId = sicnu::jobs::JobEngine::instance().submit( launch.request, std::move( launch.executor ),
                                                               std::move( launch.onCancel ) );
        else
            jobId = sicnu::jobs::JobEngine::instance().submit( launch.request );

        if ( jobId.empty() )
        {
            markTaskFailed( launch.taskId, QStringLiteral( "Task Center could not submit the job" ) );
            continue;
        }

        bool mapped = false;
        {
            QMutexLocker reLock( &m_mutex );
            if ( m_tasks.contains( launch.taskId ) && !isTerminalStatus( m_tasks[launch.taskId].status ) )
            {
                m_tasks[launch.taskId].jobId = jobId;
                m_taskByJobId[jobId] = launch.taskId;
                mapped = true;
            }
            else
            {
                // Canceled while submit was in-flight: cancel the newly submitted job immediately
                sicnu::jobs::JobEngine::instance().cancel( jobId );
            }
        }
        if ( mapped )
        {
            // Catch-up snapshot; see submitJobImpl for the rationale.
            if ( const auto record = sicnu::jobs::JobEngine::instance().snapshot( jobId ) )
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
            m_tasks[taskId].status = TaskStatus::Running;
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
        m_tasks[taskId].status = TaskStatus::Running;
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
        m_tasks[taskId].status = TaskStatus::Completed;
        m_tasks[taskId].resultPayload = resultPayload;
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

        // Execution-cache store (#667): the output of a completed
        // revision-identified task is reusable by a future identical run.
        if ( !m_tasks[taskId].outputLayerPath.isEmpty() )
        {
            const auto fpIt = m_taskFingerprints.constFind( taskId );
            if ( fpIt != m_taskFingerprints.constEnd() )
            {
                sicnu::data::ExecutionResultCache::instance().storeOutputPath(
                    *fpIt, m_tasks[taskId].outputLayerPath );
                m_taskFingerprints.erase( fpIt );
            }
        }

        updatePipelineForTaskLocked( taskId );
        queueTaskUpdatedLocked( taskId );
        processNextQueuedTasks();
    }
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
            info.status = TaskStatus::Cancelling;
            jobCancelTargets.emplace_back( info.jobId, targetId );
            info.logBuffer.append( isUserRoot
                                     ? QStringLiteral( "Cancellation requested by user." )
                                     : QStringLiteral( "Cancellation requested due to upstream parent task %1." ).arg( upstreamCause ) );
        }
        else
        {
            info.status = TaskStatus::Canceled;
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
                info.status = TaskStatus::Canceled;
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

void TaskCenter::markTaskFailed( long taskId, const QString &error )
{
    QList<long> cascadeCanceledIds;
    std::vector<std::pair<std::string, long>> jobCancelTargets;
    QList<QPointer<QgsTask>> handlesToCancel;
    {
        QMutexLocker locker( &m_mutex );
        // Terminal is final: a late duplicate record (listener vs catch-up) is a no-op.
        if ( !m_tasks.contains( taskId ) || isTerminalStatus( m_tasks[taskId].status ) )
            return;
        // The root's own engine job must be cancelled too (#702, symmetric
        // with markTaskCanceled): an externally-driven failure must kill the
        // still-running engine job, or it keeps writing output while the task
        // shows Failed. When the job is already terminal (the listener path),
        // engine cancel is a harmless no-op.
        const std::string rootJobId = m_tasks[taskId].jobId;
        if ( !rootJobId.empty() )
            jobCancelTargets.emplace_back( rootJobId, taskId );
        m_taskFingerprints.remove( taskId );
        m_tasks[taskId].status = TaskStatus::Failed;
        m_tasks[taskId].errorMessage = error;
        m_tasks[taskId].endTime = QDateTime::currentDateTimeUtc();
        m_tasks[taskId].logBuffer.append( QString( QStringLiteral( "[%1] Task failed: %2" ) )
                                            .arg( m_tasks[taskId].endTime.toString( QStringLiteral( "hh:mm:ss" ) ), error ) );
        if ( !rootJobId.empty() )
            m_taskByJobId.remove( rootJobId );
        updatePipelineForTaskLocked( taskId );
        queueTaskUpdatedLocked( taskId );

        const QList<long> descendants = collectTransitiveDescendantsLocked( taskId );
        cascadeCancelTargetsLocked( descendants, -1, QStringLiteral( "failure" ), false,
                                    cascadeCanceledIds, jobCancelTargets, handlesToCancel );

        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();

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
        m_tasks[taskId].status = TaskStatus::Canceled;
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
            m_tasks[taskId].status = TaskStatus::Paused;
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
            m_tasks[taskId].status = TaskStatus::Running;
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
            m_completionCallbacks.remove( id ); // defense (#702): stale registrations
            m_taskFingerprints.remove( id );
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

    long pipelineId = -1;
    {
        QMutexLocker locker( &m_mutex );
        pipelineId = m_nextPipelineId++;
        PipelineExecutionInfo pipeInfo;
        pipeInfo.pipelineId = pipelineId;
        pipeInfo.definitionId = QString::fromStdString( def.id );
        pipeInfo.orderedStepIds = ordered;

        QMap<std::string, long> stepToTaskId;

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

            info.outputLayerPath = findOutputPathInParams( params );

            info.logBuffer.append( QString( QStringLiteral( "[%1] Pipeline step %2 queued." ) )
                                     .arg( info.startTime.toString( QStringLiteral( "yyyy-MM-dd hh:mm:ss" ) ),
                                           QString::fromStdString( stepId ) ) );

            m_tasks[taskId] = info;
            stepToTaskId[stepId] = taskId;
            pipeInfo.stepToTaskId[stepId] = taskId;
            pipeInfo.taskToStepId[taskId] = stepId;
            pipeInfo.stepStatuses[stepId] = TaskStatus::Queued;

            queueTaskAddedLocked( taskId );
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

sicnu::data::ExecutionFingerprint
TaskCenter::taskExecutionFingerprintLocked( long taskId ) const
{
    // Disabled cache ⇒ no fingerprint at all (also skips the operator
    // lookup/hash work for every dispatch in default configurations).
    if ( !sicnu::data::ExecutionResultCache::instance().isEnabled() )
        return {};
    if ( !m_catalog )
        return {};
    // The catalog is single-thread-affine by contract (its mutators enforce
    // it); identity resolution from a foreign thread would race concurrent
    // mutations — conservatively refuse to fingerprint there.
    if ( QThread::currentThread() != m_catalog->thread() )
        return {};

    const auto it = m_tasks.constFind( taskId );
    if ( it == m_tasks.constEnd() )
        return {};
    const AlgorithmTaskInfo &info = it.value();

    // Only registered RSOperators carry the schema/metadata contract needed
    // to prove determinism; provider algorithms (gdal:/otb:/qgis:) and
    // one-shot callables stay uncached.
    const auto op = sicnu::operators::RSOperatorRegistry::instance().create(
        info.algorithmId.toStdString() );
    if ( !op )
        return {};

    // Implementation version proxy: the operator's schema document. A schema
    // change (new params, changed defaults) implies a behavior change; the
    // hash keeps the fingerprint honest without a hand-maintained version.
    Json::StreamWriterBuilder schemaWriter;
    schemaWriter["indentation"] = "";
    const std::string schemaText = Json::writeString( schemaWriter, op->schema() );

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
        return {};

    const QString versionHash = QString::fromUtf8(
        QCryptographicHash::hash( QByteArray::fromStdString( schemaText ),
                                  QCryptographicHash::Sha256 ).toHex() );

    // Exclude the destination from identity: two runs differing only in
    // output path produce identical bytes (the served run copies the cached
    // artifact onto its own output path).
    QJsonObject params = QJsonObject::fromVariantMap( info.parameterMap );
    const QString outputPath = info.outputLayerPath;
    if ( !outputPath.isEmpty() )
    {
        for ( auto pit = params.begin(); pit != params.end(); )
        {
            if ( pit.value().isString() && pit.value().toString() == outputPath )
                pit = params.erase( pit );
            else
                ++pit;
        }
    }

    // Revision-aware input identity (registered path inputs + inline scenes
    // + workspace-bound temporal collections). ANY unidentifiable input ⇒
    // not cacheable — the conservative verdict that keeps hits honest.
    QVector<sicnu::data::TaggedDerivationInput> inputs;
    QString reason;
    if ( !sicnu::temporal::fingerprintInputsForOperatorParams(
             m_catalog, info.parameterMap, outputPath, &inputs, &reason ) )
    {
        return {};
    }
    return sicnu::data::makeExecutionFingerprintV2( info.algorithmId, versionHash,
                                                    params, inputs );
}

bool TaskCenter::serveFromExecutionCache( long taskId, const sicnu::data::ExecutionFingerprint &fp )
{
    const auto cached = sicnu::data::ExecutionResultCache::instance().lookupOutputPath( fp );
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

    if ( *cached != outputPath )
    {
        // Materialize the cached artifact on this run's declared output path
        // so downstream consumers and the result payload see the requested
        // file. Failure falls through to a real execution.
        QFile::remove( outputPath );
        if ( !QFile::copy( *cached, outputPath ) )
            return false;
    }

    QVariantMap results{ { QStringLiteral( "output" ), outputPath } };
    Json::Value payload;
    payload["output"] = outputPath.toStdString();
    payload["cache"] = "hit";
    payload["cachedFrom"] = cached->toStdString();
    markTaskCompleted( taskId, results, payload );
    return true;
}

} // namespace sicnu

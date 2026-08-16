#include "task_center.h"

#include <QCoreApplication>
#include <QFile>
#include <QMutexLocker>
#include <QSet>
#include <QTimer>
#include "framework/json_params_converter.h"
#include "atomic_algorithm_registry.h"
#include "jobs/job_engine.h"
#include "workflow/workflow_definition.h"
#include "workflow/placeholder_grammar.h"

namespace sicnu {

static QString findOutputPathInParams( const QVariantMap &params )
{
    for ( auto it = params.begin(); it != params.end(); ++it )
    {
        if ( it.key().contains( QStringLiteral( "OUTPUT" ), Qt::CaseInsensitive )
             || it.key().contains( QStringLiteral( "RESULT" ), Qt::CaseInsensitive ) )
        {
            const QString path = it.value().toString();
            if ( !path.isEmpty() && !path.startsWith( QLatin1Char( '$' ) )
                 && !path.startsWith( QStringLiteral( "\x01SICNU_ERR\x01" ) ) )
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

TaskCenter::~TaskCenter()
{
    // Set shutdown flag and wake any parked watcher threads so they exit
    // before the condition variable and mutex are destroyed.
    m_isShuttingDown.store( true );
    {
        QMutexLocker locker( &m_mutex );
        m_waitCondition.wakeAll();
    }

    // ADR 0051: the JobEngine listener fires on worker threads and a job may
    // still be in flight when the singleton is destroyed at process exit
    // (tests may end without waiting for every submitted job). Join the
    // engine's workers so no notification can touch this instance once its
    // members start being destroyed.
    sicnu::jobs::JobEngine::instance().shutdown();
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
    const unsigned int hw = std::max( 1u, std::thread::hardware_concurrency() );
    const unsigned int inProcessDefault = std::max( 1u, hw > 1 ? hw - 1 : 1u );
    switch ( profile )
    {
      case ProviderResourceProfile::ExternalCliSubprocess:
        return std::min( 2u, inProcessDefault );
      case ProviderResourceProfile::PythonWorkerProcess:
        return std::min( 2u, inProcessDefault );
      case ProviderResourceProfile::QgsTaskThread:
        return std::max( 1u, inProcessDefault / 2 );
      case ProviderResourceProfile::InProcessThread:
      default:
        return inProcessDefault;
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
}

unsigned int TaskCenter::resolveEstimateMb( const std::string &algorithmId ) const
{
    QMutexLocker locker( &m_mutex );
    return m_resourceBudget.resolve( algorithmId ).ramMb;
}

Json::Value TaskCenter::variantMapToJsonParams( const QVariantMap &params )
{
    Json::Value root( Json::objectValue );
    for ( auto it = params.begin(); it != params.end(); ++it )
    {
        const QString key = it.key();
        const QVariant &val = it.value();
        switch ( val.typeId() )
        {
          case QMetaType::Bool:
            root[key.toStdString()] = val.toBool();
            break;
          case QMetaType::Int:
          case QMetaType::LongLong:
          case QMetaType::UInt:
          case QMetaType::ULongLong:
          case QMetaType::Long:
          case QMetaType::Short:
            root[key.toStdString()] = static_cast<Json::Value::Int64>( val.toLongLong() );
            break;
          case QMetaType::Double:
          case QMetaType::Float:
            root[key.toStdString()] = val.toDouble();
            break;
          default:
            root[key.toStdString()] = val.toString().toStdString();
            break;
        }
    }
    return root;
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

void TaskCenter::applyPlaceholdersForTask( long taskId )
{
    if ( !m_tasks.contains( taskId ) )
        return;

    QVariantMap &pMap = m_tasks[taskId].parameterMap;
    for ( auto pIt = pMap.begin(); pIt != pMap.end(); ++pIt )
    {
        // Only string parameters participate in placeholder substitution.
        if ( pIt.value().typeId() != QMetaType::QString )
            continue;

        std::string rawVal = pIt.value().toString().toStdString();
        std::string substituted = sicnu::workflow::substitutePlaceholders( rawVal, [&]( const sicnu::workflow::PlaceholderRef &ref ) -> std::string {
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
                    QString pOut = m_tasks[parentId].outputLayerPath;
                    if ( pOut.isEmpty() )
                        pOut = outputPathFromResult( m_tasks[parentId].resultPayload );
                    return pOut.toStdString();
                }
            }
            return ref.rawRef;
        } );

        *pIt = QString::fromStdString( substituted );
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
                              bool autoDispatch )
{
    long id = -1;
    {
        QMutexLocker locker( &m_mutex );
        id = m_nextTaskId++;
        AlgorithmTaskInfo info;
        info.taskId = id;
        info.algorithmId = algorithmId;
        info.priority = priority;
        info.parentTaskIds = parentTaskIds;
        info.autoDispatch = autoDispatch;
        info.resourceProfile = resolveResourceProfile( algorithmId );

        auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( algorithmId.toStdString() );
        if ( adapter )
            info.algorithmName = QString::fromStdString( adapter->descriptor().displayName );
        else
            info.algorithmName = algorithmId;

        info.status = TaskStatus::Queued;
        info.progressPercentage = 0.0;
        info.startTime = QDateTime::currentDateTime();
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
    return submitJobImpl( request, {}, {}, true );
}

long TaskCenter::submitJob( const sicnu::jobs::JobRequest &request,
                            JobExecutor executor,
                            CancelHook onCancel,
                            bool autoLoad )
{
    return submitJobImpl( request, std::move( executor ), std::move( onCancel ), autoLoad );
}

long TaskCenter::submitJobImpl( const sicnu::jobs::JobRequest &request,
                                JobExecutor executor,
                                CancelHook onCancel,
                                bool autoLoad )
{
    ensureJobListener();

    QVariantMap params = sicnu::processing::jsonParamsToVariantMap( request.params );

    // Tracking-only enqueue; this path owns JobEngine submission itself.
    const long taskId = enqueueTask( QString::fromStdString( request.algorithmId ), params, autoLoad,
                                     TaskPriority::Normal, {}, false );
    const std::string jobId = executor
                                  ? sicnu::jobs::JobEngine::instance().submit( request, executor, std::move( onCancel ) )
                                  : sicnu::jobs::JobEngine::instance().submit( request );
    if ( jobId.empty() )
    {
        markTaskFailed( taskId, QStringLiteral( "Task Center could not submit the job" ) );
        return taskId;
    }

    {
        QMutexLocker locker( &m_mutex );
        if ( m_tasks.contains( taskId ) )
        {
            m_tasks[taskId].jobId = jobId;
            m_tasks[taskId].jobRequest = request;
            m_tasks[taskId].hasJobRequest = true;
            m_tasks[taskId].jobExecutor = std::move( executor );
            m_taskByJobId[jobId] = taskId;
        }
    }

    markTaskRunning( taskId );
    // Catch-up: a job that reached a terminal state before the listener could
    // dispatch it (mapping registered after submit returned) must still land
    // in the bookkeeping. Delta logic makes this idempotent.
    if ( const auto record = sicnu::jobs::JobEngine::instance().snapshot( jobId ) )
        onJobRecord( *record );
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

    std::size_t forwarded = 0;
    {
        QMutexLocker locker( &m_mutex );
        forwarded = m_forwardedLogCounts.value( taskId, 0 );
        if ( record.logLines.size() > forwarded )
            m_forwardedLogCounts[taskId] = record.logLines.size();
    }
    while ( forwarded < record.logLines.size() )
    {
        appendTaskLog( taskId, QString::fromStdString( record.logLines[forwarded].text ) );
        ++forwarded;
    }

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

void TaskCenter::processNextQueuedTasks()
{
    // Called with m_mutex held. Only stages work; callers must flushPendingLaunches() outside the lock.
    const unsigned int globalMax = m_globalConcurrencyLimit > 0
                                     ? m_globalConcurrencyLimit
                                     : defaultLimitForProfile( ProviderResourceProfile::InProcessThread );

    QMap<ProviderResourceProfile, unsigned int> runningByProfile;
    unsigned int totalRunning = 0;
    unsigned int runningTotalMb = 0; // RAM estimate sum of Running tasks (resource-aware gate)

    // Pending launches already have status Running; count only Running tasks once.
    for ( const auto &t : m_tasks )
    {
        if ( t.status != TaskStatus::Running )
            continue;
        runningByProfile[t.resourceProfile] = runningByProfile.value( t.resourceProfile, 0u ) + 1;
        ++totalRunning;
        runningTotalMb += m_resourceBudget.resolve( t.algorithmId.toStdString() ).ramMb;
    }

    if ( totalRunning >= globalMax )
        return;

    QList<long> eligibleIds;
    for ( auto it = m_tasks.begin(); it != m_tasks.end(); ++it )
    {
        if ( it.value().status != TaskStatus::Queued )
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

    for ( long id : eligibleIds )
    {
        if ( totalRunning >= globalMax )
            break;

        applyPlaceholdersForTask( id );

        if ( !m_tasks[id].autoDispatch )
            continue;
        if ( !m_tasks[id].jobId.empty() )
            continue;
        if ( m_tasks[id].status != TaskStatus::Queued )
            continue;

        const ProviderResourceProfile profile = m_tasks[id].resourceProfile;
        const unsigned int profileMax = limitForProfileLocked( profile );
        if ( runningByProfile.value( profile, 0u ) >= profileMax )
            continue; // leave queued; another profile may still launch

        // ADR 0063: hold all launches when the process RSS is at/above the
        // watermark. Memory pressure is global, so break rather than continue
        // - remaining eligible tasks cannot run either. Blocked tasks stay
        // Queued and are re-evaluated when a running task finishes (each
        // terminal transition re-enters processNextQueuedTasks).
        if ( m_resourceMonitor.memoryPressureHigh() )
        {
            if ( totalRunning == 0 && QCoreApplication::instance() )
            {
                QTimer::singleShot( 250, [this]() {
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
        // DELAYS — the task stays Queued and is re-evaluated on the next
        // terminal transition. `continue` (not break): a later, lighter
        // eligible task may still fit within the budget this pass.
        const unsigned int candidateMb =
            m_resourceBudget.resolve( m_tasks[id].algorithmId.toStdString() ).ramMb;
        if ( !m_resourceBudget.canLaunch( runningTotalMb, candidateMb ) )
            continue;

        m_tasks[id].status = TaskStatus::Running;
        m_tasks[id].logBuffer.append(
          QString( QStringLiteral( "[%1] Dispatching to JobEngine (profile=%2)." ) )
            .arg( QDateTime::currentDateTime().toString( QStringLiteral( "hh:mm:ss" ) ) )
            .arg( static_cast<int>( profile ) ) );
        updatePipelineForTaskLocked( id );

        PendingLaunch launch;
        launch.taskId = id;
        launch.request.algorithmId = m_tasks[id].algorithmId.toStdString();
        launch.request.title = m_tasks[id].algorithmName.toStdString();
        launch.request.source = m_tasks[id].pipelineId >= 0 ? "pipeline" : "task_center";
        launch.request.params = variantMapToJsonParams( m_tasks[id].parameterMap );
        if ( m_tasks[id].hasJobRequest && m_tasks[id].jobExecutor )
        {
            launch.executor = m_tasks[id].jobExecutor;
            launch.hasExecutor = true;
            launch.request = m_tasks[id].jobRequest;
            launch.request.params = variantMapToJsonParams( m_tasks[id].parameterMap );
        }
        else
        {
            m_tasks[id].jobRequest = launch.request;
            m_tasks[id].hasJobRequest = true;
        }

        queueTaskUpdatedLocked( id );
        m_pendingLaunches.append( std::move( launch ) );
        runningByProfile[profile] = runningByProfile.value( profile, 0u ) + 1;
        ++totalRunning;
        runningTotalMb += candidateMb;
    }
}

void TaskCenter::flushPendingLaunches()
{
    QList<PendingLaunch> launches;
    {
        QMutexLocker locker( &m_mutex );
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

        std::string jobId;
        if ( launch.hasExecutor )
            jobId = sicnu::jobs::JobEngine::instance().submit( launch.request, std::move( launch.executor ) );
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
        if ( !m_tasks.contains( taskId ) || m_tasks[taskId].status == TaskStatus::Canceled )
            return;
        m_tasks[taskId].progressPercentage = progress;
        m_tasks[taskId].status = TaskStatus::Running;
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
        if ( !m_tasks.contains( taskId ) || m_tasks[taskId].status == TaskStatus::Canceled )
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
        m_tasks[taskId].endTime = QDateTime::currentDateTime();
        m_tasks[taskId].logBuffer.append( QString( QStringLiteral( "[%1] Task completed successfully." ) )
                                            .arg( m_tasks[taskId].endTime.toString( QStringLiteral( "hh:mm:ss" ) ) ) );

        if ( m_tasks[taskId].outputLayerPath.isEmpty() && !results.isEmpty() )
        {
            for ( auto it = results.begin(); it != results.end(); ++it )
            {
                if ( it.value().canConvert<QString>() )
                {
                    m_tasks[taskId].outputLayerPath = it.value().toString();
                    break;
                }
            }
        }
        if ( m_tasks[taskId].outputLayerPath.isEmpty() )
            m_tasks[taskId].outputLayerPath = outputPathFromResult( resultPayload );

        shouldAutoLoad = m_tasks[taskId].autoLoadLayer && !m_tasks[taskId].outputLayerPath.isEmpty();
        autoLoadPath = m_tasks[taskId].outputLayerPath;

        updatePipelineForTaskLocked( taskId );
        queueTaskUpdatedLocked( taskId );
        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();

    if ( shouldAutoLoad && !autoLoadPath.isEmpty() )
        emit layerAutoLoadRequested( autoLoadPath );
}

void TaskCenter::markTaskFailed( long taskId, const QString &error )
{
    {
        QMutexLocker locker( &m_mutex );
        // Terminal is final: a late duplicate record (listener vs catch-up) is a no-op.
        if ( !m_tasks.contains( taskId ) || isTerminalStatus( m_tasks[taskId].status ) )
            return;
        m_tasks[taskId].status = TaskStatus::Failed;
        m_tasks[taskId].errorMessage = error;
        m_tasks[taskId].endTime = QDateTime::currentDateTime();
        m_tasks[taskId].logBuffer.append( QString( QStringLiteral( "[%1] Task failed: %2" ) )
                                            .arg( m_tasks[taskId].endTime.toString( QStringLiteral( "hh:mm:ss" ) ), error ) );
        updatePipelineForTaskLocked( taskId );
        queueTaskUpdatedLocked( taskId );

        QList<long> keys = m_tasks.keys();
        for ( long id : keys )
        {
            if ( m_tasks[id].parentTaskIds.contains( taskId ) && m_tasks[id].status == TaskStatus::Queued )
            {
                m_tasks[id].status = TaskStatus::Canceled;
                m_tasks[id].endTime = QDateTime::currentDateTime();
                m_tasks[id].errorMessage = QStringLiteral( "Canceled due to upstream parent task failure." );
                m_tasks[id].logBuffer.append( QStringLiteral( "Canceled due to upstream parent task failure." ) );
                updatePipelineForTaskLocked( id );
                queueTaskUpdatedLocked( id );
            }
        }

        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();
}

void TaskCenter::markTaskCanceled( long taskId, const QString &reason )
{
    {
        QMutexLocker locker( &m_mutex );
        // Terminal is final: a late duplicate record (listener vs catch-up) is a no-op.
        if ( !m_tasks.contains( taskId ) || isTerminalStatus( m_tasks[taskId].status ) )
            return;
        m_tasks[taskId].status = TaskStatus::Canceled;
        m_tasks[taskId].errorMessage = reason;
        m_tasks[taskId].endTime = QDateTime::currentDateTime();
        m_tasks[taskId].logBuffer.append( QString( QStringLiteral( "[%1] %2" ) )
                                            .arg( m_tasks[taskId].endTime.toString( QStringLiteral( "hh:mm:ss" ) ), reason ) );
        updatePipelineForTaskLocked( taskId );
        queueTaskUpdatedLocked( taskId );
        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();
}

bool TaskCenter::cancelTask( long taskId )
{
    std::vector<std::string> jobIdsToCancel;
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) )
            return false;
        if ( isTerminalStatus( m_tasks[taskId].status ) )
            return false;

        // Recursive search for all descendant task IDs in the DAG
        QSet<long> canceledAncestors;
        canceledAncestors.insert( taskId );
        bool addedNew = true;
        while ( addedNew )
        {
            addedNew = false;
            for ( auto it = m_tasks.begin(); it != m_tasks.end(); ++it )
            {
                if ( isTerminalStatus( it.value().status ) )
                    continue;
                if ( canceledAncestors.contains( it.key() ) )
                    continue;
                for ( long parentId : it.value().parentTaskIds )
                {
                    if ( canceledAncestors.contains( parentId ) )
                    {
                        canceledAncestors.insert( it.key() );
                        addedNew = true;
                        break;
                    }
                }
            }
        }

        for ( long targetId : canceledAncestors )
        {
            auto &info = m_tasks[targetId];
            if ( isTerminalStatus( info.status ) )
                continue;

            if ( info.taskHandle )
                info.taskHandle->cancel();

            if ( !info.jobId.empty() )
            {
                jobIdsToCancel.push_back( info.jobId );
                info.logBuffer.append( ( targetId == taskId )
                                         ? QStringLiteral( "Cancellation requested by user." )
                                         : QStringLiteral( "Cancellation requested due to upstream parent task cancellation." ) );
            }
            else
            {
                info.status = TaskStatus::Canceled;
                info.errorMessage = ( targetId == taskId )
                                      ? QStringLiteral( "Task canceled" )
                                      : QStringLiteral( "Canceled due to upstream parent task cancellation." );
                info.endTime = QDateTime::currentDateTime();
                info.logBuffer.append( ( targetId == taskId )
                                         ? QStringLiteral( "Task canceled by user." )
                                         : QStringLiteral( "Canceled due to upstream parent task cancellation." ) );

                if ( !info.outputLayerPath.isEmpty() && QFile::exists( info.outputLayerPath ) )
                {
                    if ( info.outputLayerPath.startsWith( QStringLiteral( "/tmp/" ) )
                         || info.outputLayerPath.contains( QStringLiteral( ".scratch" ) ) )
                    {
                        QFile::remove( info.outputLayerPath );
                    }
                }
            }

            updatePipelineForTaskLocked( targetId );
            queueTaskUpdatedLocked( targetId );
        }

        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();

    for ( const auto &jId : jobIdsToCancel )
        sicnu::jobs::JobEngine::instance().cancel( jId );

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
        if ( m_tasks[taskId].status == TaskStatus::Running )
        {
            if ( m_tasks[taskId].taskHandle )
                m_tasks[taskId].taskHandle->hold();
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
                m_tasks[taskId].taskHandle->unhold();
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

bool TaskCenter::retryTask( long taskId )
{
    AlgorithmTaskInfo oldInfo;
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) )
            return false;
        oldInfo = m_tasks[taskId];
    }
    if ( oldInfo.hasJobRequest )
        return submitJob( oldInfo.jobRequest, oldInfo.jobExecutor, {}, oldInfo.autoLoadLayer ) > 0;
    return enqueueTask( oldInfo.algorithmId, oldInfo.parameterMap, oldInfo.autoLoadLayer, oldInfo.priority,
                        oldInfo.parentTaskIds, oldInfo.autoDispatch )
           > 0;
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
            info.startTime = QDateTime::currentDateTime();
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
            pipeInfo.isFailed = false;
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

} // namespace sicnu

#include "task_center.h"

#include <QMutexLocker>
#include <qgsapplication.h>
#include <thread>
#include <algorithm>
#include <chrono>
#include "jobs/job_engine.h"
#include "workflow/workflow_definition.h"

namespace sicnu {

static QString findOutputPathInParams( const QVariantMap &params )
{
    for ( auto it = params.begin(); it != params.end(); ++it )
    {
        if ( it.key().contains( QStringLiteral( "OUTPUT" ), Qt::CaseInsensitive )
             || it.key().contains( QStringLiteral( "RESULT" ), Qt::CaseInsensitive ) )
        {
            const QString path = it.value().toString();
            if ( !path.isEmpty() && !path.startsWith( QLatin1Char( '$' ) ) )
                return path;
        }
    }
    return QString();
}



TaskCenter& TaskCenter::instance()
{
    static TaskCenter s_instance;
    return s_instance;
}

TaskCenter::TaskCenter()
{
    qRegisterMetaType<AlgorithmTaskInfo>("sicnu::AlgorithmTaskInfo");
    resetResourceProfileLimits();
}

ProviderResourceProfile TaskCenter::resolveResourceProfile( const QString &algorithmId ) const
{
    auto adapter = AlgorithmEngine::instance().findAlgorithm( algorithmId );
    if ( adapter )
        return adapter->descriptor().resourceProfile;
    // Documented fallback when the algorithm is not registered on AlgorithmEngine
    // (e.g. pure JobEngine / RSOperator ids used only at runtime).
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

QVariantMap TaskCenter::jsonParamsToVariantMap( const Json::Value &params )
{
    QVariantMap variantMap;
    if ( !params.isObject() )
        return variantMap;

    for ( const auto &key : params.getMemberNames() )
    {
        const auto &val = params[key];
        if ( val.isString() )
            variantMap[QString::fromStdString( key )] = QString::fromStdString( val.asString() );
        else if ( val.isBool() )
            variantMap[QString::fromStdString( key )] = val.asBool();
        else if ( val.isInt() || val.isUInt() || val.isInt64() || val.isUInt64() )
            variantMap[QString::fromStdString( key )] = static_cast<qint64>( val.asInt64() );
        else if ( val.isNumeric() )
            variantMap[QString::fromStdString( key )] = val.asDouble();
        else
            variantMap[QString::fromStdString( key )] = QString::fromStdString( val.toStyledString() );
    }
    return variantMap;
}

void TaskCenter::queueTaskAddedLocked( long taskId )
{
    if ( m_tasks.contains( taskId ) )
        m_pendingTaskAdded.append( m_tasks[taskId] );
}

void TaskCenter::queueTaskUpdatedLocked( long taskId )
{
    if ( m_tasks.contains( taskId ) )
        m_pendingTaskUpdated.append( m_tasks[taskId] );
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
        // Only string parameters participate in $step.output substitution.
        // Converting numbers to QString here would break integer JobEngine params.
        if ( pIt.value().typeId() != QMetaType::QString )
            continue;

        QString valStr = pIt.value().toString();
        for ( long parentId : m_tasks[taskId].parentTaskIds )
        {
            if ( !m_tasks.contains( parentId ) )
                continue;

            QString pOut = m_tasks[parentId].outputLayerPath;
            if ( pOut.isEmpty()
                 && m_tasks[parentId].resultPayload.isMember( "output" )
                 && m_tasks[parentId].resultPayload["output"].isString() )
            {
                pOut = QString::fromStdString( m_tasks[parentId].resultPayload["output"].asString() );
            }

            const QString parentStepId = m_tasks[parentId].stepId;
            if ( !parentStepId.isEmpty() )
            {
                valStr.replace( QString( QStringLiteral( "$%1.output" ) ).arg( parentStepId ), pOut );
                valStr.replace( QString( QStringLiteral( "${%1.output}" ) ).arg( parentStepId ), pOut );
            }
            valStr.replace( QString( QStringLiteral( "${task.%1.output}" ) ).arg( parentId ), pOut );
            valStr.replace( QStringLiteral( "${task.parent.output}" ), pOut );
        }
        *pIt = valStr;
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

        auto adapter = AlgorithmEngine::instance().findAlgorithm( algorithmId );
        if ( adapter )
            info.algorithmName = adapter->descriptor().name;
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

long TaskCenter::enqueueToolCall( const std::string &jsonToolCall, bool autoLoad, TaskPriority priority )
{
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );

    std::string toolName = "unknown_tool";
    QVariantMap variantParams;

    if ( reader->parse( jsonToolCall.c_str(), jsonToolCall.c_str() + jsonToolCall.length(), &root, &errs ) )
    {
        if ( root.isMember( "name" ) )
            toolName = root["name"].asString();
        else if ( root.isMember( "function" ) && root["function"].isMember( "name" ) )
            toolName = root["function"]["name"].asString();

        Json::Value paramsNode;
        if ( root.isMember( "parameters" ) )
            paramsNode = root["parameters"];
        else if ( root.isMember( "function" ) && root["function"].isMember( "arguments" ) )
            paramsNode = root["function"]["arguments"];

        if ( paramsNode.isObject() )
        {
            variantParams = jsonParamsToVariantMap( paramsNode );
        }
    }

    return enqueueTask( QString::fromStdString( toolName ), variantParams, autoLoad, priority, {}, true );
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
    QVariantMap params = jsonParamsToVariantMap( request.params );

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
        }
    }

    markTaskRunning( taskId );
    watchSubmittedJob( taskId, jobId );
    return taskId;
}

void TaskCenter::watchSubmittedJob( long taskId, std::string jobId )
{
    std::thread( [taskId, jobId = std::move( jobId )]() {
        auto &engine = sicnu::jobs::JobEngine::instance();
        std::size_t forwardedLogCount = 0;
        double lastProgress = -2.0;
        for ( ;; )
        {
            const auto record = engine.snapshot( jobId );
            if ( !record )
            {
                TaskCenter::instance().markTaskFailed( taskId, QStringLiteral( "Task Center lost the job record" ) );
                return;
            }
            if ( record->progress >= 0.0 && record->progress != lastProgress )
            {
                TaskCenter::instance().updateTaskProgress( taskId, record->progress );
                lastProgress = record->progress;
            }
            while ( forwardedLogCount < record->logLines.size() )
            {
                TaskCenter::instance().appendTaskLog(
                  taskId, QString::fromStdString( record->logLines[forwardedLogCount].text ) );
                ++forwardedLogCount;
            }
            if ( record->state == sicnu::jobs::JobState::Succeeded
                 || record->state == sicnu::jobs::JobState::Failed
                 || record->state == sicnu::jobs::JobState::Cancelled )
            {
                if ( record->state == sicnu::jobs::JobState::Succeeded )
                {
                    QVariantMap results;
                    for ( const auto &name : record->result.getMemberNames() )
                    {
                        if ( record->result[name].isString() )
                            results.insert( QString::fromStdString( name ),
                                            QString::fromStdString( record->result[name].asString() ) );
                    }
                    TaskCenter::instance().markTaskCompleted( taskId, results, record->result );
                }
                else if ( record->state == sicnu::jobs::JobState::Cancelled )
                {
                    TaskCenter::instance().markTaskCanceled( taskId );
                }
                else
                {
                    TaskCenter::instance().markTaskFailed( taskId, QString::fromStdString( record->error ) );
                }
                return;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
    } ).detach();
}

void TaskCenter::processNextQueuedTasks()
{
    // Called with m_mutex held. Only stages work; callers must flushPendingLaunches() outside the lock.
    const unsigned int globalMax = m_globalConcurrencyLimit > 0
                                     ? m_globalConcurrencyLimit
                                     : defaultLimitForProfile( ProviderResourceProfile::InProcessThread );

    QMap<ProviderResourceProfile, unsigned int> runningByProfile;
    unsigned int totalRunning = 0;

    // Pending launches already have status Running; count only Running tasks once.
    for ( const auto &t : m_tasks )
    {
        if ( t.status != TaskStatus::Running )
            continue;
        runningByProfile[t.resourceProfile] = runningByProfile.value( t.resourceProfile, 0u ) + 1;
        ++totalRunning;
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
    }
}

void TaskCenter::flushPendingLaunches()
{
    QList<PendingLaunch> launches;
    {
        QMutexLocker locker( &m_mutex );
        launches.swap( m_pendingLaunches );
    }

    for ( auto &launch : launches )
    {
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

        {
            QMutexLocker reLock( &m_mutex );
            if ( m_tasks.contains( launch.taskId ) )
                m_tasks[launch.taskId].jobId = jobId;
        }
        watchSubmittedJob( launch.taskId, jobId );
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
        if ( !m_tasks.contains( taskId ) || m_tasks[taskId].status == TaskStatus::Canceled )
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
        if ( m_tasks[taskId].outputLayerPath.isEmpty()
             && resultPayload.isMember( "output" )
             && resultPayload["output"].isString() )
        {
            m_tasks[taskId].outputLayerPath = QString::fromStdString( resultPayload["output"].asString() );
        }

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
        if ( !m_tasks.contains( taskId ) || m_tasks[taskId].status == TaskStatus::Canceled )
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
        if ( !m_tasks.contains( taskId ) || m_tasks[taskId].status == TaskStatus::Canceled )
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
    std::string jobId;
    bool cancelImmediately = false;
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) )
            return false;
        if ( isTerminalStatus( m_tasks[taskId].status ) )
            return false;
        if ( m_tasks[taskId].taskHandle )
            m_tasks[taskId].taskHandle->cancel();
        jobId = m_tasks[taskId].jobId;
        cancelImmediately = jobId.empty();
        if ( cancelImmediately )
        {
            m_tasks[taskId].status = TaskStatus::Canceled;
            m_tasks[taskId].errorMessage = QStringLiteral( "Task canceled" );
            m_tasks[taskId].endTime = QDateTime::currentDateTime();
            m_tasks[taskId].logBuffer.append( QStringLiteral( "Task canceled by user." ) );
            updatePipelineForTaskLocked( taskId );
        }
        else
        {
            m_tasks[taskId].logBuffer.append( QStringLiteral( "Cancellation requested by user." ) );
        }
        queueTaskUpdatedLocked( taskId );

        if ( cancelImmediately )
        {
            QList<long> keys = m_tasks.keys();
            for ( long id : keys )
            {
                if ( m_tasks[id].parentTaskIds.contains( taskId ) && m_tasks[id].status == TaskStatus::Queued )
                {
                    m_tasks[id].status = TaskStatus::Canceled;
                    m_tasks[id].endTime = QDateTime::currentDateTime();
                    m_tasks[id].logBuffer.append( QStringLiteral( "Canceled due to upstream parent task failure." ) );
                    updatePipelineForTaskLocked( id );
                    queueTaskUpdatedLocked( id );
                }
            }
        }

        processNextQueuedTasks();
    }
    flushPendingLaunches();
    flushPendingSignals();
    if ( !jobId.empty() )
        sicnu::jobs::JobEngine::instance().cancel( jobId );
    return true;
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
    QMutexLocker locker( &m_mutex );
    QList<long> keys = m_tasks.keys();
    for ( long id : keys )
    {
        if ( isTerminalStatus( m_tasks[id].status ) )
            m_tasks.remove( id );
    }
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

            QVariantMap params = jsonParamsToVariantMap( step->params );

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
    for ( ;; )
    {
        AlgorithmTaskInfo info = getTaskInfo( taskId );
        if ( info.taskId < 0 || isTerminalStatus( info.status ) )
        {
            return info;
        }
        if ( clock::now() >= deadline )
        {
            return info;
        }
        std::this_thread::sleep_for( pollInterval );
    }
}

PipelineExecutionInfo TaskCenter::waitForPipeline( long pipelineId,
                                                    std::chrono::milliseconds timeout,
                                                    std::chrono::milliseconds pollInterval ) const
{
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + timeout;
    for ( ;; )
    {
        PipelineExecutionInfo info = getPipelineInfo( pipelineId );
        if ( info.pipelineId < 0 || info.isCompleted || info.isFailed )
        {
            return info;
        }
        if ( clock::now() >= deadline )
        {
            return info;
        }
        std::this_thread::sleep_for( pollInterval );
    }
}

} // namespace sicnu

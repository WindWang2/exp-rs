#include "task_center.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QSet>
#include <QTimer>
#include <map>
#include <optional>
#include <utility>

#include "framework/json_params_converter.h"
#include "atomic_algorithm_registry.h"
#include "jobs/job_engine.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_checkpoint.h"
#include "workflow/placeholder_grammar.h"
#include "data/execution_fingerprint.h"
#include "data/asset_types.h"

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
    }
    sicnu::jobs::JobEngine::instance().shutdown();
}

TaskCenter::~TaskCenter()
{
    shutdown();
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
        if ( t.status != TaskStatus::Running && t.status != TaskStatus::Cancelling )
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
                // Port-aware: try resultPayload[portName] first (376).
                const Json::Value &payload = m_tasks[parentId].resultPayload;
                if ( payload.isObject() && payload.isMember( ref.portName ) && payload[ref.portName].isString() )
                {
                    const std::string s = payload[ref.portName].asString();
                    if ( !s.empty() ) return s;
                }
                // Also check case where portName is the generic "output" but payload uses another key;
                // fallback to outputLayerPath / outputPathFromResult for single-output steps.
                QString pOut = m_tasks[parentId].outputLayerPath;
                if ( pOut.isEmpty() )
                    pOut = outputPathFromResult( payload );
                // If still empty and portName != "output", try payload[portName] via variant map results stored earlier.
                if ( pOut.isEmpty() && payload.isObject() )
                {
                    for ( const auto &name : payload.getMemberNames() )
                    {
                        if ( QString::fromStdString( name ).compare( QString::fromStdString( ref.portName ), Qt::CaseInsensitive ) == 0
                             && payload[name].isString() )
                        {
                            const std::string s = payload[name].asString();
                            if ( !s.empty() ) return s;
                        }
                    }
                }
                return pOut.toStdString();
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

    mirrorStepToRunLocked( pipelineId, stepId.toStdString(), m_tasks[taskId].status );

    if ( m_tasks[taskId].status == TaskStatus::Completed )
        storePipelineStepOutputLocked( pipelineId, taskId );

    if ( allTerminal )
    {
        pipe.isCompleted = true;
        pipe.isFailed = anyFailed;
        finalizeWorkflowRunLocked( pipelineId, anyFailed, pipe.errorMessage );
    }
}

// --- Workflow Engine 2.0 wiring (ADR 0123, #662) ----------------------------

namespace {

/// Deterministic pseudo-AssetId for an upstream pipeline step, derived from
/// the parent step's fingerprint so any upstream change (params, inputs)
/// yields a different derivation identity downstream. Returns nullopt when
/// the fingerprint hex cannot be shaped into a UUID string.
std::optional<sicnu::data::AssetId> stepAssetIdFromFingerprint( const std::string &fpHex )
{
    if ( fpHex.size() < 32 )
        return std::nullopt;
    const QString hex = QString::fromStdString( fpHex.substr( 0, 32 ) );
    const QString uuid = QStringLiteral( "%1-%2-%3-%4-%5" )
                             .arg( hex.mid( 0, 8 ), hex.mid( 8, 4 ), hex.mid( 12, 4 ),
                                   hex.mid( 16, 4 ), hex.mid( 20, 12 ) );
    return sicnu::data::AssetId::fromString( uuid );
}

/// Numeric revision token for an upstream step: the parent fingerprint's first
/// 8 hex chars, so a changed upstream plan invalidates the child fingerprint.
sicnu::data::AssetRevision stepRevisionFromFingerprint( const std::string &fpHex )
{
    if ( fpHex.size() < 8 )
        return sicnu::data::AssetRevision::initial();
    const quint64 value = std::stoull( fpHex.substr( 0, 8 ), nullptr, 16 );
    return sicnu::data::AssetRevision::fromValue( value ? value : 1 );
}

/// TaskStatus -> StepPlan status vocabulary (workflow_run.h StepPlan).
std::string stepStatusForTaskStatus( TaskStatus status )
{
    switch ( status )
    {
    case TaskStatus::Queued:
    case TaskStatus::WaitingResource:
        return "Ready";
    case TaskStatus::Running:
    case TaskStatus::Paused:
    case TaskStatus::Cancelling:
        return "Running";
    case TaskStatus::Completed:
        return "Completed";
    case TaskStatus::Failed:
        return "Failed";
    case TaskStatus::Canceled:
        return "Canceled";
    }
    return "Running";
}

} // namespace

void TaskCenter::attachWorkflowRunLocked( long pipelineId,
                                          const sicnu::workflow::WorkflowDefinition &def,
                                          const std::vector<std::string> &orderedStepIds )
{
    std::shared_ptr<sicnu::workflow::WorkflowRun> run =
        sicnu::workflow::WorkflowRun::createFromDefinition( def );
    if ( !run )
        return;

    // Submit-time planning is done once the topological order exists; the run
    // enters Running when the pipeline is dispatched below.
    run->transitionTo( sicnu::workflow::WorkflowRunState::Planning );
    run->transitionTo( sicnu::workflow::WorkflowRunState::Ready );

    // Fingerprints are computed in topological order so parent plans exist
    // when children are hashed: a child's derivation revision is its parent's
    // fingerprint, which gives transitive invalidation for param/path changes.
    std::map<std::string, std::string> stepFingerprints;
    std::vector<sicnu::workflow::StepPlan> plans;
    plans.reserve( def.steps.size() );

    for ( const auto &stepId : orderedStepIds )
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
        if ( !step )
            continue;

        sicnu::workflow::StepPlan plan;
        plan.stepId = step->id;
        plan.operatorId = step->operatorId;
        plan.kind = step->kind;
        plan.rawParams = step->params;
        plan.resolvedParams = step->params; // placeholders resolve per-task at dispatch
        for ( const auto &conn : step->inputs )
            plan.dependencies.push_back( conn.fromStepId );

        const QJsonObject paramsJson = QJsonObject::fromVariantMap(
            sicnu::processing::jsonParamsToVariantMap( step->params ) );
        QVector<sicnu::data::TaggedDerivationInput> inputs;
        for ( const auto &conn : step->inputs )
        {
            const auto parentIt = stepFingerprints.find( conn.fromStepId );
            if ( parentIt == stepFingerprints.end() )
                continue;
            const auto assetId = stepAssetIdFromFingerprint( parentIt->second );
            if ( !assetId )
                continue;
            sicnu::data::TaggedDerivationInput in;
            in.assetId = *assetId;
            in.revision = stepRevisionFromFingerprint( parentIt->second );
            in.fromPort = QString::fromStdString( conn.fromPort );
            in.toPort = QString::fromStdString( conn.toPort );
            inputs.append( in );
        }
        // Operator versions are not yet declared by the registry; the constant
        // keeps the fingerprint contract total. When the meta store publishes
        // versions, thread them through here so upgrades invalidate the cache.
        const sicnu::data::ExecutionFingerprint fp = sicnu::data::makeExecutionFingerprintV2(
            QString::fromStdString( step->operatorId ), QStringLiteral( "1" ),
            paramsJson, inputs );
        if ( fp.isValid() )
        {
            plan.fingerprint = fp.toStdString();
            stepFingerprints[step->id] = plan.fingerprint;
        }

        plans.push_back( std::move( plan ) );
    }

    run->setStepPlans( std::move( plans ) );
    run->transitionTo( sicnu::workflow::WorkflowRunState::Running );

    // Crash-safe checkpoint at submit; refreshed on every mirrored transition.
    sicnu::workflow::WorkflowCheckpointManager().saveCheckpoint( *run );

    m_pipelineRuns[pipelineId] = std::move( run );
}

void TaskCenter::mirrorStepToRunLocked( long pipelineId, const std::string &stepId,
                                        TaskStatus status )
{
    const auto runIt = m_pipelineRuns.find( pipelineId );
    if ( runIt == m_pipelineRuns.end() || !runIt.value() )
        return;
    sicnu::workflow::WorkflowRun &run = *runIt.value();

    auto plan = run.stepPlan( stepId );
    if ( !plan )
        return;
    plan->status = stepStatusForTaskStatus( status );
    if ( status == TaskStatus::Completed )
        plan->endTime = QDateTime::currentDateTimeUtc().toString(
            QStringLiteral( "yyyy-MM-dd hh:mm:ss" ) );
    run.updateStepPlan( *plan );

    sicnu::workflow::WorkflowCheckpointManager().saveCheckpoint( run );
}

void TaskCenter::finalizeWorkflowRunLocked( long pipelineId, bool failed,
                                            const QString &errorMessage )
{
    const auto runIt = m_pipelineRuns.find( pipelineId );
    if ( runIt == m_pipelineRuns.end() || !runIt.value() )
        return;
    sicnu::workflow::WorkflowRun &run = *runIt.value();

    if ( sicnu::workflow::isTerminalRunState( run.state() ) )
        return;

    if ( failed )
    {
        if ( !errorMessage.isEmpty() )
            run.setErrorMessage( errorMessage.toStdString() );
        run.transitionTo( sicnu::workflow::WorkflowRunState::Failed );
    }
    else
    {
        run.transitionTo( sicnu::workflow::WorkflowRunState::Completed );
    }
    run.recalculateProgress();

    sicnu::workflow::WorkflowCheckpointManager().saveCheckpoint( run );
}

void TaskCenter::storePipelineStepOutputLocked( long pipelineId, long taskId )
{
    const auto runIt = m_pipelineRuns.find( pipelineId );
    if ( runIt == m_pipelineRuns.end() || !runIt.value() )
        return;
    auto plan = runIt.value()->stepPlan( m_tasks[taskId].stepId.toStdString() );
    if ( !plan || plan->fingerprint.empty() || plan->cacheHit )
        return; // cache-hit steps own nothing: their output belongs to the prior run

    const QString out = findOutputPathInParams( m_tasks[taskId].parameterMap );
    if ( out.isEmpty() )
        return;

    sicnu::data::ExecutionFingerprint fp;
    fp.digest = QByteArray::fromHex( QString::fromStdString( plan->fingerprint ).toUtf8() );
    if ( !fp.isValid() )
        return;
    sicnu::data::ExecutionResultCache::instance().storeOutputPath( fp, out );
}

std::shared_ptr<const sicnu::workflow::WorkflowRun>
TaskCenter::workflowRunForPipeline( long pipelineId ) const
{
    QMutexLocker locker( &m_mutex );
    const auto it = m_pipelineRuns.find( pipelineId );
    return it == m_pipelineRuns.end() ? nullptr : it.value();
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
    return m_resourceBudget.resolve( task.algorithmId.toStdString() ).ramMb;
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
    // Cancelling tasks still occupy their worker slot until the job reports
    // its terminal record, so they count toward every limit.
    for ( const auto &t : m_tasks )
    {
        if ( t.status != TaskStatus::Running && t.status != TaskStatus::Cancelling )
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

        m_tasks[id].status = TaskStatus::Running;
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
        if ( !m_tasks.contains( taskId ) || isTerminalStatus( m_tasks[taskId].status ) )
            return;
        m_tasks[taskId].progressPercentage = progress;
        // Progress from a job that is winding down after a cancel request must
        // not overwrite the Cancelling state (the UI shows "cancelling" until
        // the worker's terminal record arrives).
        if ( m_tasks[taskId].status == TaskStatus::Queued
             || m_tasks[taskId].status == TaskStatus::WaitingResource
             || m_tasks[taskId].status == TaskStatus::Running )
        {
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
        m_tasks[taskId].status = TaskStatus::Failed;
        m_tasks[taskId].errorMessage = error;
        m_tasks[taskId].endTime = QDateTime::currentDateTimeUtc();
        m_tasks[taskId].logBuffer.append( QString( QStringLiteral( "[%1] Task failed: %2" ) )
                                            .arg( m_tasks[taskId].endTime.toString( QStringLiteral( "hh:mm:ss" ) ), error ) );
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
        m_tasks[taskId].status = TaskStatus::Canceled;
        m_tasks[taskId].errorMessage = reason;
        m_tasks[taskId].endTime = QDateTime::currentDateTimeUtc();
        m_tasks[taskId].logBuffer.append( QString( QStringLiteral( "[%1] %2" ) )
                                            .arg( m_tasks[taskId].endTime.toString( QStringLiteral( "hh:mm:ss" ) ), reason ) );
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
        if ( m_tasks[taskId].status == TaskStatus::Running )
        {
            // hold() has the same thread-affinity caveat as cancel()
            // (dispatchPendingCancels marshals that call): invoke it on the
            // handle's own thread instead of the caller's (#616).
            if ( m_tasks[taskId].taskHandle )
            {
                QgsTask *handle = m_tasks[taskId].taskHandle.data();
                QMetaObject::invokeMethod( handle, [handle]() { handle->hold(); },
                                           Qt::QueuedConnection );
            }
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

bool TaskCenter::retryTask( long taskId )
{
    AlgorithmTaskInfo oldInfo;
    {
        QMutexLocker locker( &m_mutex );
        if ( !m_tasks.contains( taskId ) )
            return false;
        // A non-terminal task is still schedulable/running: enqueueing a
        // retry beside it would execute the same work twice (#616).
        if ( !isTerminalStatus( m_tasks[taskId].status ) )
            return false;
        oldInfo = m_tasks[taskId];
    }
    if ( oldInfo.hasJobRequest )
        return submitJob( oldInfo.jobRequest, oldInfo.jobExecutor, {}, oldInfo.autoLoadLayer ) > 0;
    return enqueueTask( oldInfo.algorithmId, oldInfo.parameterMap, oldInfo.autoLoadLayer, oldInfo.priority,
                        oldInfo.parentTaskIds, true )
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

        // Workflow Engine 2.0 run aggregate (ADR 0123, #662): built BEFORE the
        // task loop so each step can consult the execution cache (#667).
        attachWorkflowRunLocked( pipelineId, def, ordered );
        std::vector<long> precompletedTaskIds;

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

            // Execution-cache consultation (#667): an identical prior step
            // (same operator, canonical params, upstream derivation revisions)
            // skips re-execution. The cached output path is injected as the
            // pre-completed task's result payload so downstream placeholder
            // substitution resolves $stepId.output to the cached artifact.
            QString cachedPath;
            const auto runIt = m_pipelineRuns.find( pipelineId );
            if ( runIt != m_pipelineRuns.end() && runIt.value() )
            {
                if ( auto plan = runIt.value()->stepPlan( stepId );
                     plan && plan->cacheHit && !plan->cachedOutputPath.empty() )
                    cachedPath = QString::fromStdString( plan->cachedOutputPath );
            }

            long taskId = m_nextTaskId++;
            AlgorithmTaskInfo info;
            info.taskId = taskId;
            info.algorithmId = QString::fromStdString( step->operatorId );
            info.algorithmName = step->title.empty() ? QString::fromStdString( step->operatorId )
                                                     : QString::fromStdString( step->title );
            info.priority = TaskPriority::Normal;
            info.parentTaskIds = parentTaskIds;
            info.startTime = QDateTime::currentDateTimeUtc();
            info.parameterMap = params;
            info.autoLoadLayer = autoLoad;
            info.resourceProfile = resolveResourceProfile( info.algorithmId );
            info.stepId = QString::fromStdString( stepId );
            info.pipelineId = pipelineId;

            if ( !cachedPath.isEmpty() )
            {
                // Pre-completed task: no JobEngine job, terminal from birth.
                info.status = TaskStatus::Completed;
                info.autoDispatch = false;
                info.endTime = QDateTime::currentDateTimeUtc();
                Json::Value payload( Json::objectValue );
                payload["output"] = cachedPath.toStdString();
                info.resultPayload = payload;
                info.outputLayerPath = cachedPath;
                info.logBuffer.append( QString( QStringLiteral( "[%1] Pipeline step %2 served from execution cache." ) )
                                         .arg( info.startTime.toString( QStringLiteral( "yyyy-MM-dd hh:mm:ss" ) ),
                                               QString::fromStdString( stepId ) ) );
            }
            else
            {
                info.status = TaskStatus::Queued;
                info.autoDispatch = true;
                info.outputLayerPath = findOutputPathInParams( params );
                info.logBuffer.append( QString( QStringLiteral( "[%1] Pipeline step %2 queued." ) )
                                         .arg( info.startTime.toString( QStringLiteral( "yyyy-MM-dd hh:mm:ss" ) ),
                                               QString::fromStdString( stepId ) ) );
            }

            m_tasks[taskId] = info;
            stepToTaskId[stepId] = taskId;
            pipeInfo.stepToTaskId[stepId] = taskId;
            pipeInfo.taskToStepId[taskId] = stepId;
            pipeInfo.stepStatuses[stepId] = info.status;

            queueTaskAddedLocked( taskId );

            if ( !cachedPath.isEmpty() )
                precompletedTaskIds.push_back( taskId );
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
            finalizeWorkflowRunLocked( pipelineId, pipeInfo.isFailed, pipeInfo.errorMessage );
            m_waitCondition.wakeAll();
            return pipelineId;
        }

        m_pipelines[pipelineId] = pipeInfo;
        // A pre-completed (cache-hit) step has no job-listener transition that
        // would ever mark the pipeline terminal, so drive the aggregate here:
        // a fully cached pipeline completes on this pass.
        for ( long preId : precompletedTaskIds )
            updatePipelineForTaskLocked( preId );
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

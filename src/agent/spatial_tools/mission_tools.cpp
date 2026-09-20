/***************************************************************************
 * mission_tools.cpp — the `mission:*` tool objects + their host
 *
 * See mission_tools.h for the contract. Implementation notes:
 *   - QJson (Qt) is the mission domain's serialization; Json::Value
 *     (jsoncpp) is the tool surface's. The conversion goes through the
 *     canonical compact JSON so both surfaces see identical bytes.
 *   - Every mutating action runs inside MissionToolHost::mutateRuntime:
 *     one lock, one load, one state-machine pass, one atomic save. A
 *     rejection leaves the authority untouched (no half-published state).
 *   - `start` may not claim Running for a run it cannot verify (see the
 *     resolver policy in the header of the advance tool's action table).
 ***************************************************************************/

#include "agent/spatial_tools/mission_tools.h"

#include "app/workbench/mission_projection.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QStringList>

#include <algorithm>

namespace sicnu::agent::spatial_tools
{

namespace
{

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString( Qt::ISODate );
}

/// QJsonObject → Json::Value through the canonical compact encoding, so the
/// tool payload and the GUI projection are byte-identical by construction.
bool jsonValueFromQJson( const QJsonObject &obj, Json::Value &out, std::string *error )
{
    const QByteArray bytes = QJsonDocument( obj ).toJson( QJsonDocument::Compact );
    Json::CharReaderBuilder builder;
    std::string errs;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    return reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &out, &errs );
}

int optionalInt( const Json::Value &input, const char *key, int fallback, bool *present )
{
    if ( present )
        *present = false;
    if ( !input.isObject() || !input.isMember( key ) )
        return fallback;
    const Json::Value &v = input[ key ];
    if ( v.isIntegral() )
    {
        if ( present )
            *present = true;
        return v.asInt();
    }
    return fallback;
}

QString optionalString( const Json::Value &input, const char *key )
{
    if ( !input.isObject() || !input.isMember( key ) || !input[ key ].isString() )
        return {};
    return QString::fromStdString( input[ key ].asString() );
}

Json::Value stringArray( const std::vector<std::string> &values )
{
    Json::Value arr( Json::arrayValue );
    for ( const std::string &v : values )
        arr.append( v );
    return arr;
}

Json::Value schemaString( const std::string &description )
{
    Json::Value v( Json::objectValue );
    v[ "type" ] = "string";
    v[ "description" ] = description;
    return v;
}

Json::Value schemaInteger( const std::string &description )
{
    Json::Value v( Json::objectValue );
    v[ "type" ] = "integer";
    v[ "description" ] = description;
    return v;
}

Json::Value schemaBoolean( const std::string &description )
{
    Json::Value v( Json::objectValue );
    v[ "type" ] = "boolean";
    v[ "description" ] = description;
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// MissionToolHost
// ---------------------------------------------------------------------------

MissionToolHost &MissionToolHost::instance()
{
    static MissionToolHost host;
    return host;
}

void MissionToolHost::setAuthority( std::shared_ptr<MissionAuthority> authority )
{
    std::lock_guard<std::mutex> lock( mProviderMutex );
    mAuthority = std::move( authority );
}

void MissionToolHost::setProjectPathProvider( ProjectPathProvider provider )
{
    std::lock_guard<std::mutex> lock( mProviderMutex );
    mProjectPath = std::move( provider );
}

void MissionToolHost::setRunStatusResolver( RunStatusResolver resolver )
{
    std::lock_guard<std::mutex> lock( mProviderMutex );
    mRunResolver = std::move( resolver );
}

void MissionToolHost::setRefResolverProvider( RefResolverProvider provider )
{
    std::lock_guard<std::mutex> lock( mProviderMutex );
    mRefResolver = std::move( provider );
}

void MissionToolHost::setProjectDocumentProvider( ProjectDocumentProvider provider )
{
    std::lock_guard<std::mutex> lock( mProviderMutex );
    mDocument = std::move( provider );
}

QString MissionToolHost::currentProjectPath() const
{
    std::lock_guard<std::mutex> lock( mProviderMutex );
    return mProjectPath ? mProjectPath() : QString();
}

bool MissionToolHost::resolveRun( const sicnu::app::MissionRunRef &ref,
                                  sicnu::app::MissionRunStatus &status ) const
{
    std::lock_guard<std::mutex> lock( mProviderMutex );
    if ( !mRunResolver )
        return false;
    status = mRunResolver( ref );
    return true;
}

bool MissionToolHost::refResolver( sicnu::app::MissionRefResolver &resolver ) const
{
    std::lock_guard<std::mutex> lock( mProviderMutex );
    if ( !mRefResolver )
        return false;
    resolver = mRefResolver();
    return true;
}

bool MissionToolHost::readRuntime( sicnu::app::MissionRuntimeState &state, QString &errorCode,
                                   QString &errorMessage ) const
{
    const QString projectPath = currentProjectPath();
    if ( projectPath.isEmpty() )
    {
        errorCode = QStringLiteral( "no_active_project" );
        errorMessage = QStringLiteral( "no mission project is open" );
        return false;
    }
    std::shared_ptr<MissionAuthority> authority;
    QDomDocument document;
    {
        std::lock_guard<std::mutex> lock( mProviderMutex );
        authority = mAuthority;
        if ( mDocument )
            document = mDocument();
    }
    if ( !authority )
    {
        errorCode = QStringLiteral( "mission_authority_unavailable" );
        errorMessage = QStringLiteral( "no mission authority is installed" );
        return false;
    }
    QString err;
    if ( !authority->load( projectPath, document, state, errorCode, errorMessage ) )
        return false;
    Q_UNUSED( err )
    return true;
}

bool MissionToolHost::commitRuntime( sicnu::app::MissionRuntimeState &state, QString &errorCode,
                                     QString &errorMessage ) const
{
    const QString projectPath = currentProjectPath();
    if ( projectPath.isEmpty() )
    {
        errorCode = QStringLiteral( "no_active_project" );
        errorMessage = QStringLiteral( "no mission project is open" );
        return false;
    }
    std::shared_ptr<MissionAuthority> authority;
    QDomDocument document;
    {
        std::lock_guard<std::mutex> lock( mProviderMutex );
        authority = mAuthority;
        if ( mDocument )
            document = mDocument();
    }
    if ( !authority )
    {
        errorCode = QStringLiteral( "mission_authority_unavailable" );
        errorMessage = QStringLiteral( "no mission authority is installed" );
        return false;
    }
    if ( !authority->commit( projectPath, document, state, errorCode, errorMessage ) )
        return false;
    return true;
}

bool MissionToolHost::mutateRuntime( sicnu::app::MissionRuntimeState &state, const Mutation &mutate,
                                     QString &errorCode, QString &errorMessage, QString &reason )
{
    std::lock_guard<std::mutex> lock( mRuntimeMutex );
    if ( !readRuntime( state, errorCode, errorMessage ) )
        return false;
    if ( !mutate( state, reason ) )
    {
        // Domain rejection: nothing was mutated worth persisting. Keep the
        // state-machine reason and do NOT commit.
        errorCode = reason;
        errorMessage = reason;
        return false;
    }
    if ( !commitRuntime( state, errorCode, errorMessage ) )
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// mission:context
// ---------------------------------------------------------------------------

std::string MissionContextTool::description() const
{
    return "Read the current mission snapshot: identity, project reference, stage summary, "
           "task counts and the bounded object lists (layers, results, …).";
}

std::vector<std::string> MissionContextTool::tags() const
{
    return { "mission", "context", "workspace", "read-only" };
}

Json::Value MissionContextTool::inputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema[ "type" ] = "object";
    Json::Value props( Json::objectValue );
    props[ "max_items" ] = schemaInteger( "Cap for bounded list lengths (default 32)." );
    schema[ "properties" ] = props;
    return schema;
}

Json::Value MissionContextTool::outputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema[ "type" ] = "object";
    Json::Value props( Json::objectValue );
    props[ "mission_id" ] = schemaString( "Mission identity." );
    props[ "mission_name" ] = schemaString( "Human mission name." );
    props[ "project_ref" ] = schemaString( "Project file path or stable project id." );
    props[ "current_stage" ] = schemaString( "Earliest stage with unsettled work." );
    props[ "task_count" ] = schemaInteger( "Number of tasks in the timeline." );
    props[ "revision" ] = schemaInteger( "Timeline revision (bumped per applied mutation)." );
    props[ "last_event_seq" ] = schemaInteger( "Event-log cursor for incremental reads." );
    props[ "timeline_present" ] = schemaBoolean( "Whether the authority carries a timeline." );
    props[ "counts" ] = schemaString( "Per-kind object counts (layers, results, …)." );
    props[ "layers" ] = schemaString( "Bounded layer id list." );
    props[ "results" ] = schemaString( "Bounded result id list." );
    schema[ "properties" ] = props;
    return schema;
}

SpatialToolResult MissionContextTool::execute( const Json::Value &input )
{
    int maxItems = 32;
    maxItems = optionalInt( input, "max_items", maxItems, nullptr );
    const int cap = std::max( 1, maxItems );

    sicnu::app::MissionRuntimeState state;
    QString code;
    QString message;
    if ( !MissionToolHost::instance().readRuntime( state, code, message ) )
        return SpatialToolResult::failure( message.toStdString(), code.toStdString(),
                                           code == QStringLiteral( "commit_failed" ) ? "io" : "runtime",
                                           false );

    const sicnu::app::MissionContext &ctx = state.context;

    // Bounded projection of the mission value model (GOAL §9): counts first,
    // then capped id lists — never an unbounded dump.
    QJsonObject counts;
    counts.insert( QStringLiteral( "layers" ), ctx.layers.size() );
    counts.insert( QStringLiteral( "results" ), ctx.results.size() );
    counts.insert( QStringLiteral( "datasets" ), ctx.datasets.size() );
    counts.insert( QStringLiteral( "assets" ), ctx.assets.size() );
    counts.insert( QStringLiteral( "models" ), ctx.models.size() );
    counts.insert( QStringLiteral( "experiments" ), ctx.experiments.size() );
    counts.insert( QStringLiteral( "workflow_runs" ), ctx.workflowRuns.size() );
    counts.insert( QStringLiteral( "artifacts" ), ctx.artifacts.size() );

    const auto boundedIds = [&cap]( const QVector<sicnu::app::WorkbenchObjectRef> &refs ) {
        QStringList ids;
        for ( const sicnu::app::WorkbenchObjectRef &ref : refs )
        {
            if ( ids.size() >= cap )
                break;
            if ( !ref.id.isEmpty() )
                ids.push_back( ref.id );
        }
        return ids;
    };

    QJsonObject out;
    out.insert( QStringLiteral( "mission_id" ), ctx.missionId );
    out.insert( QStringLiteral( "mission_name" ), ctx.missionName );
    out.insert( QStringLiteral( "project_ref" ), ctx.projectRef );
    out.insert( QStringLiteral( "current_stage" ),
                QLatin1String( sicnu::app::missionStageKey( state.timeline.currentStage() ) ) );
    out.insert( QStringLiteral( "task_count" ), state.timeline.tasks().size() );
    out.insert( QStringLiteral( "revision" ),
                static_cast<qint64>( state.timeline.revision() ) );
    out.insert( QStringLiteral( "last_event_seq" ),
                static_cast<qint64>( state.timeline.lastEventSeq() ) );
    out.insert( QStringLiteral( "timeline_present" ),
                ctx.metadata.contains(
            QLatin1String( sicnu::app::kMissionTimelineMetadataKey ) ) );
    out.insert( QStringLiteral( "counts" ), counts );
    out.insert( QStringLiteral( "layers" ), QJsonArray::fromStringList( boundedIds( ctx.layers ) ) );
    out.insert( QStringLiteral( "results" ), QJsonArray::fromStringList( boundedIds( ctx.results ) ) );

    Json::Value payload;
    std::string parseError;
    if ( !jsonValueFromQJson( out, payload, &parseError ) )
        return SpatialToolResult::failure( "mission projection serialization failed: " + parseError,
                                           "projection_failed", "runtime", false );
    return SpatialToolResult::ok( payload );
}

// ---------------------------------------------------------------------------
// mission:timeline
// ---------------------------------------------------------------------------

std::string MissionTimelineTool::description() const
{
    return "Read the mission task timeline: stage summary, tasks and events after an optional "
           "cursor (since_seq).";
}

std::vector<std::string> MissionTimelineTool::tags() const
{
    return { "mission", "timeline", "tasks", "read-only" };
}

Json::Value MissionTimelineTool::inputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema[ "type" ] = "object";
    Json::Value props( Json::objectValue );
    props[ "since_seq" ] =
        schemaInteger( "Only return events after this event-log cursor (0 = all)." );
    props[ "max_items" ] =
        schemaInteger( "Cap for task rows (default 32; 0 = stage summary only)." );
    schema[ "properties" ] = props;
    return schema;
}

Json::Value MissionTimelineTool::outputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema[ "type" ] = "object";
    Json::Value props( Json::objectValue );
    props[ "kind" ] = schemaString( "Artifact kind (mission_timeline)." );
    props[ "schema_version" ] = schemaString( "Timeline schema version." );
    props[ "mission_id" ] = schemaString( "Mission identity." );
    props[ "current_stage" ] = schemaString( "Earliest stage with unsettled work." );
    props[ "revision" ] = schemaInteger( "Timeline revision." );
    props[ "last_event_seq" ] = schemaInteger( "Event-log cursor." );
    props[ "task_count" ] = schemaInteger( "Total tasks (before truncation)." );
    props[ "task_truncated" ] = schemaBoolean( "Whether max_items hid task rows." );
    props[ "stages" ] = schemaString( "Per-stage status counts." );
    props[ "tasks" ] = schemaString( "Task rows in canonical projection order." );
    props[ "events" ] = schemaString( "Events after since_seq." );
    props[ "reconciliation_available" ] =
        schemaBoolean( "Whether a live reference resolver is installed." );
    props[ "reconciliation" ] =
        schemaString( "Reference health scan (only when a resolver is installed)." );
    schema[ "properties" ] = props;
    return schema;
}

SpatialToolResult MissionTimelineTool::execute( const Json::Value &input )
{
    quint64 sinceSeq = 0;
    if ( input.isObject() && input.isMember( "since_seq" ) && input[ "since_seq" ].isIntegral() )
        sinceSeq = static_cast<quint64>( std::max<Json::Int64>( 0, input[ "since_seq" ].asInt64() ) );
    int maxItems = 32;
    maxItems = optionalInt( input, "max_items", maxItems, nullptr );

    sicnu::app::MissionRuntimeState state;
    QString code;
    QString message;
    if ( !MissionToolHost::instance().readRuntime( state, code, message ) )
        return SpatialToolResult::failure( message.toStdString(), code.toStdString(),
                                           code == QStringLiteral( "commit_failed" ) ? "io" : "runtime",
                                           false );

    QJsonObject out = sicnu::app::missionTimelineProjectionJson( state.timeline, maxItems, sinceSeq );

    sicnu::app::MissionRefResolver resolver;
    if ( MissionToolHost::instance().refResolver( resolver ) )
    {
        out.insert( QStringLiteral( "reconciliation_available" ), true );
        out.insert( QStringLiteral( "reconciliation" ),
                    sicnu::app::missionReconciliationProjectionJson(
                        sicnu::app::reconcileMission( state.timeline, resolver ) ) );
    }
    else
    {
        out.insert( QStringLiteral( "reconciliation_available" ), false );
    }

    Json::Value payload;
    std::string parseError;
    if ( !jsonValueFromQJson( out, payload, &parseError ) )
        return SpatialToolResult::failure( "timeline projection serialization failed: " + parseError,
                                           "projection_failed", "runtime", false );
    return SpatialToolResult::ok( payload );
}

// ---------------------------------------------------------------------------
// mission:advance
// ---------------------------------------------------------------------------

std::string MissionAdvanceTool::description() const
{
    return "Request a mission task transition (start/succeed/fail/cancel/retry), bind or unbind "
           "its run authority, or reconcile references. Rejects illegal transitions without "
           "mutating; start requires a verifiable run authority (no fake Running).";
}

std::vector<std::string> MissionAdvanceTool::tags() const
{
    return { "mission", "task", "transition", "retry", "reconcile" };
}

Json::Value MissionAdvanceTool::inputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema[ "type" ] = "object";
    Json::Value props( Json::objectValue );
    props[ "task_id" ] = schemaString( "Task id inside the mission timeline." );
    Json::Value action( Json::objectValue );
    action[ "type" ] = "string";
    action[ "description" ] =
        "start | succeed | fail | cancel | retry | bind_run | unbind_run | reconcile";
    Json::Value allowed( Json::arrayValue );
    for ( const char *a : { "start", "succeed", "fail", "cancel", "retry", "bind_run",
                            "unbind_run", "reconcile" } )
        allowed.append( a );
    action[ "enum" ] = allowed;
    props[ "action" ] = action;
    props[ "run_kind" ] =
        schemaString( "Run authority kind (task_center | workflow_run | pipeline_run)." );
    props[ "run_id" ] = schemaString( "Run authority id in that authority." );
    props[ "error_code" ] = schemaString( "Machine error code recorded with fail." );
    props[ "error_message" ] = schemaString( "Bounded human error detail recorded with fail." );
    props[ "note" ] = schemaString( "Audit note appended to the event log." );
    schema[ "properties" ] = props;
    Json::Value required( Json::arrayValue );
    required.append( "task_id" );
    required.append( "action" );
    schema[ "required" ] = required;
    return schema;
}

Json::Value MissionAdvanceTool::outputSchema() const
{
    Json::Value schema( Json::objectValue );
    schema[ "type" ] = "object";
    Json::Value props( Json::objectValue );
    props[ "applied" ] = schemaBoolean( "Whether the action was applied and persisted." );
    props[ "reason" ] = schemaString( "Machine code (ok, or the rejection code)." );
    props[ "task" ] = schemaString( "Updated task projection (empty when unknown)." );
    props[ "revision" ] = schemaInteger( "Timeline revision after the action." );
    props[ "last_event_seq" ] = schemaInteger( "Event-log cursor after the action." );
    props[ "reconciliation" ] =
        schemaString( "Reconciliation report (only for action=reconcile)." );
    schema[ "properties" ] = props;
    return schema;
}

MissionActionResult applyMissionAction( const QString &taskId, const QString &action,
                                        const QString &runKind, const QString &runId,
                                        const QString &errorCode, const QString &errorMessage,
                                        const QString &note )
{
    MissionActionResult result;
    const QString iso = nowIso();
    MissionToolHost &host = MissionToolHost::instance();
    // Adapter: the host reports "no resolver" as Unknown, which is exactly
    // the fail-closed liveness the reconciler expects.
    const sicnu::app::MissionRunStatusResolver runResolver =
        [&host]( const sicnu::app::MissionRunRef &ref ) -> sicnu::app::MissionRunStatus {
        sicnu::app::MissionRunStatus status;
        host.resolveRun( ref, status );
        return status;
    };
    sicnu::app::MissionRuntimeState state;
    QString code;
    QString message;
    QString reason;
    sicnu::app::MissionReconciliation reconcileReport;
    bool didReconcile = false;
    bool unknownTask = false;

    const bool committed = host.mutateRuntime(
        state,
        [&]( sicnu::app::MissionRuntimeState &runtime, QString &rejectReason ) -> bool {
            sicnu::app::MissionTimeline &timeline = runtime.timeline;
            if ( !timeline.hasTask( taskId ) )
            {
                // An id that does not exist is an input error, not a domain
                // refusal: the caller learns it immediately.
                unknownTask = true;
                rejectReason = QStringLiteral( "unknown_task" );
                return false;
            }

            if ( action == QLatin1String( "reconcile" ) )
            {
                sicnu::app::MissionRefResolver resolver;
                if ( !host.refResolver( resolver ) )
                {
                    // Without a liveness resolver every ref would look dead;
                    // refuse instead of marking a healthy mission stale.
                    rejectReason = QStringLiteral( "ref_resolver_unavailable" );
                    return false;
                }
                reconcileReport = sicnu::app::reconcileMission( timeline, resolver );
                sicnu::app::applyReconciliation( timeline, reconcileReport, iso );
                sicnu::app::reconcileRunAuthority( timeline, runResolver, iso );
                didReconcile = true;
                return true;
            }

            if ( action == QLatin1String( "bind_run" ) || action == QLatin1String( "unbind_run" ) )
            {
                if ( action == QLatin1String( "bind_run" )
                     && ( runKind.trimmed().isEmpty() || runId.trimmed().isEmpty() ) )
                {
                    rejectReason = QStringLiteral( "run_reference_incomplete" );
                    return false;
                }
                const sicnu::app::MissionTask *task = timeline.task( taskId );
                if ( action == QLatin1String( "unbind_run" ) && task
                     && task->status == sicnu::app::MissionTaskStatus::Running )
                {
                    // Unbinding a Running task would resurrect the "Running
                    // without authority" state the machine forbids.
                    rejectReason = QStringLiteral( "unbind_running_rejected" );
                    return false;
                }
                sicnu::app::MissionRunRef run;
                if ( action == QLatin1String( "bind_run" ) )
                {
                    run.kind = runKind;
                    run.id = runId;
                }
                const sicnu::app::MissionOutcome outcome =
                    timeline.bindRunReference( taskId, run, iso, note );
                if ( !outcome.applied )
                {
                    rejectReason = outcome.reason;
                    return false;
                }
                return true;
            }

            if ( action == QLatin1String( "retry" ) )
            {
                sicnu::app::MissionRunRef run;
                if ( !runKind.trimmed().isEmpty() && !runId.trimmed().isEmpty() )
                {
                    run.kind = runKind;
                    run.id = runId;
                }
                const sicnu::app::MissionOutcome outcome =
                    timeline.retry( taskId, iso, run, note );
                if ( !outcome.applied )
                {
                    rejectReason = outcome.reason;
                    return false;
                }
                return true;
            }

            if ( action == QLatin1String( "start" ) )
            {
                const bool explicitBinding =
                    !runKind.trimmed().isEmpty() && !runId.trimmed().isEmpty();
                if ( explicitBinding )
                {
                    sicnu::app::MissionRunRef run;
                    run.kind = runKind;
                    run.id = runId;
                    const sicnu::app::MissionOutcome bound =
                        timeline.bindRunReference( taskId, run, iso, note );
                    if ( !bound.applied )
                    {
                        rejectReason = bound.reason;
                        return false;
                    }
                }

                const sicnu::app::MissionTask *task = timeline.task( taskId );
                if ( !task || task->run.isNull() )
                {
                    rejectReason = QStringLiteral( "stale_run_reference" );
                    return false;
                }

                // No fake Running: a pre-existing binding must be verified
                // against the execution authority. An explicit binding in
                // THIS call is the caller asserting authority; without any
                // resolver installed it is trusted, with a resolver it is
                // verified like any other.
                sicnu::app::MissionRunStatus status;
                const bool resolved = host.resolveRun( task->run, status );
                if ( resolved )
                {
                    if ( status.liveness != sicnu::app::MissionRunLiveness::Alive )
                    {
                        rejectReason =
                            status.liveness == sicnu::app::MissionRunLiveness::Unknown
                                ? QStringLiteral( "run_authority_unresolved" )
                                : QStringLiteral( "stale_run_reference" );
                        return false;
                    }
                }
                else if ( !explicitBinding )
                {
                    rejectReason = QStringLiteral( "run_authority_unresolved" );
                    return false;
                }

                const sicnu::app::MissionOutcome outcome =
                    timeline.transition( taskId, sicnu::app::MissionTaskStatus::Running, iso, note );
                if ( !outcome.applied )
                {
                    rejectReason = outcome.reason;
                    return false;
                }
                return true;
            }

            // succeed / fail / cancel
            sicnu::app::MissionTaskStatus target = sicnu::app::MissionTaskStatus::Succeeded;
            if ( action == QLatin1String( "fail" ) )
                target = sicnu::app::MissionTaskStatus::Failed;
            else if ( action == QLatin1String( "cancel" ) )
                target = sicnu::app::MissionTaskStatus::Canceled;

            const sicnu::app::MissionOutcome outcome =
                timeline.transition( taskId, target, iso, note, errorCode, errorMessage );
            if ( !outcome.applied )
            {
                rejectReason = outcome.reason;
                return false;
            }
            return true;
        },
        code, message, reason );

    result.applied = committed;
    result.reason = committed ? QStringLiteral( "ok" ) : reason;
    result.revision = state.timeline.revision();
    result.lastEventSeq = state.timeline.lastEventSeq();
    const sicnu::app::MissionTask *task = state.timeline.task( taskId );
    if ( task )
        result.task = sicnu::app::missionTaskProjectionJson( *task );
    if ( didReconcile )
        result.reconciliation = sicnu::app::missionReconciliationProjectionJson( reconcileReport );
    // Transport/authority failures and a non-existent task id are real
    // errors; every other rejection is a structured domain answer.
    result.transportFailure =
        !committed
        && ( unknownTask || code == QStringLiteral( "no_active_project" )
             || code == QStringLiteral( "authority_corrupt" )
             || code == QStringLiteral( "authority_unreadable" )
             || code == QStringLiteral( "mission_authority_unavailable" )
             || code == QStringLiteral( "commit_failed" ) );
    result.errorCode = code;
    result.errorMessage = message;
    return result;
}

SpatialToolResult MissionAdvanceTool::execute( const Json::Value &input )
{
    std::string missing;
    const QString taskId = requireStringField( input, "task_id", &missing );
    if ( taskId.trimmed().isEmpty() )
        return SpatialToolResult::failure( missing.empty() ? "missing string parameter 'task_id'"
                                                          : missing,
                                           "INVALID_PARAMETER", "validation", false );
    const QString action = optionalString( input, "action" );
    static const QStringList kActions = { QStringLiteral( "start" ),     QStringLiteral( "succeed" ),
                                          QStringLiteral( "fail" ),      QStringLiteral( "cancel" ),
                                          QStringLiteral( "retry" ),     QStringLiteral( "bind_run" ),
                                          QStringLiteral( "unbind_run" ), QStringLiteral( "reconcile" ) };
    if ( !kActions.contains( action ) )
        return SpatialToolResult::failure( "unknown action '" + action.toStdString() + "'",
                                           "INVALID_PARAMETER", "validation", false );

    const MissionActionResult result =
        applyMissionAction( taskId, action, optionalString( input, "run_kind" ),
                            optionalString( input, "run_id" ), optionalString( input, "error_code" ),
                            optionalString( input, "error_message" ),
                            optionalString( input, "note" ) );

    if ( result.transportFailure )
        return SpatialToolResult::failure(
            result.errorMessage.toStdString(), result.errorCode.toStdString(),
            result.errorCode == QStringLiteral( "commit_failed" ) ? "io" : "validation",
            result.errorCode == QStringLiteral( "commit_failed" ) );

    QJsonObject out;
    out.insert( QStringLiteral( "applied" ), result.applied );
    out.insert( QStringLiteral( "reason" ), result.reason );
    out.insert( QStringLiteral( "revision" ), static_cast<qint64>( result.revision ) );
    out.insert( QStringLiteral( "last_event_seq" ), static_cast<qint64>( result.lastEventSeq ) );
    out.insert( QStringLiteral( "task" ), result.task );
    if ( !result.reconciliation.isEmpty() )
        out.insert( QStringLiteral( "reconciliation" ), result.reconciliation );

    Json::Value payload;
    std::string parseError;
    if ( !jsonValueFromQJson( out, payload, &parseError ) )
        return SpatialToolResult::failure( "advance projection serialization failed: " + parseError,
                                           "projection_failed", "runtime", false );
    return SpatialToolResult::ok( payload );
}

// registration
// ---------------------------------------------------------------------------

void registerMissionTools()
{
    auto &registry = SpatialToolRegistry::instance();
    registry.registerTool( std::make_shared<MissionContextTool>() );
    registry.registerTool( std::make_shared<MissionTimelineTool>() );
    registry.registerTool( std::make_shared<MissionAdvanceTool>() );
}

} // namespace sicnu::agent::spatial_tools

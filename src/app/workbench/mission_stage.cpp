/***************************************************************************
 * mission_stage.cpp — Mission task space implementation (Qt Core only)
 *
 * See mission_stage.h for the contract. Summary of the invariants enforced
 * here (every one of them is covered by tests/test_mission_stage.cpp):
 *
 *   1. Illegal transitions never mutate and never bump the revision.
 *   2. A task may only enter Running with a run authority id recorded, so
 *      GUI / MCP / Pi can never disagree about *which* run is executing.
 *   3. Retry / resume always go through Pending and always bump `attempts`,
 *      keeping the retry lineage (`retryOf`) intact.
 *   4. Reconciliation resolves each referenced id exactly once, so cost is
 *      O(distinct refs) and not O(tasks x refs).
 ***************************************************************************/

#include "app/workbench/mission_stage.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QStringView>

#include <algorithm>
#include <utility>

namespace sicnu::app
{

namespace
{
QJsonArray stringListToJson( const QStringList &values )
{
    QJsonArray arr;
    for ( const QString &v : values )
        arr.push_back( v );
    return arr;
}

QStringList stringListFromJson( const QJsonValue &value )
{
    QStringList out;
    if ( !value.isArray() )
        return out;
    const QJsonArray arr = value.toArray();
    out.reserve( arr.size() );
    for ( const QJsonValue &v : arr )
    {
        if ( v.isString() )
            out.push_back( v.toString() );
    }
    return out;
}

qint64 intFromJson( const QJsonValue &value, qint64 fallback = 0 )
{
    if ( value.isDouble() )
        return static_cast<qint64>( value.toDouble() );
    return fallback;
}
} // namespace

// ---------------------------------------------------------------------------
// Stage axis
// ---------------------------------------------------------------------------

const char *missionStageKey( MissionStage stage )
{
    switch ( stage )
    {
        case MissionStage::Import:
            return "import";
        case MissionStage::Preprocess:
            return "preprocess";
        case MissionStage::Analyze:
            return "analyze";
        case MissionStage::Verify:
            return "verify";
        case MissionStage::Publish:
            return "publish";
    }
    return "import";
}

std::optional<MissionStage> missionStageFromKey( const QString &key )
{
    if ( key == QLatin1String( "import" ) )
        return MissionStage::Import;
    if ( key == QLatin1String( "preprocess" ) )
        return MissionStage::Preprocess;
    if ( key == QLatin1String( "analyze" ) )
        return MissionStage::Analyze;
    if ( key == QLatin1String( "verify" ) )
        return MissionStage::Verify;
    if ( key == QLatin1String( "publish" ) )
        return MissionStage::Publish;
    return std::nullopt;
}

QString missionStageLabel( MissionStage stage )
{
    switch ( stage )
    {
        case MissionStage::Import:
            return QStringLiteral( "Import" );
        case MissionStage::Preprocess:
            return QStringLiteral( "Preprocess" );
        case MissionStage::Analyze:
            return QStringLiteral( "Analyze" );
        case MissionStage::Verify:
            return QStringLiteral( "Verify" );
        case MissionStage::Publish:
            return QStringLiteral( "Publish" );
    }
    return QStringLiteral( "Import" );
}

QVector<MissionStage> missionStages()
{
    return { MissionStage::Import, MissionStage::Preprocess, MissionStage::Analyze,
             MissionStage::Verify, MissionStage::Publish };
}

// ---------------------------------------------------------------------------
// Task status
// ---------------------------------------------------------------------------

const char *missionTaskStatusKey( MissionTaskStatus status )
{
    switch ( status )
    {
        case MissionTaskStatus::Pending:
            return "pending";
        case MissionTaskStatus::Running:
            return "running";
        case MissionTaskStatus::Succeeded:
            return "succeeded";
        case MissionTaskStatus::Failed:
            return "failed";
        case MissionTaskStatus::Canceled:
            return "canceled";
        case MissionTaskStatus::Stale:
            return "stale";
    }
    return "pending";
}

std::optional<MissionTaskStatus> missionTaskStatusFromKey( const QString &key )
{
    if ( key == QLatin1String( "pending" ) )
        return MissionTaskStatus::Pending;
    if ( key == QLatin1String( "running" ) )
        return MissionTaskStatus::Running;
    if ( key == QLatin1String( "succeeded" ) )
        return MissionTaskStatus::Succeeded;
    if ( key == QLatin1String( "failed" ) )
        return MissionTaskStatus::Failed;
    if ( key == QLatin1String( "canceled" ) )
        return MissionTaskStatus::Canceled;
    if ( key == QLatin1String( "stale" ) )
        return MissionTaskStatus::Stale;
    return std::nullopt;
}

QString missionTaskStatusLabel( MissionTaskStatus status )
{
    return QString::fromLatin1( missionTaskStatusKey( status ) );
}

bool missionTaskStatusIsSettled( MissionTaskStatus status )
{
    switch ( status )
    {
        case MissionTaskStatus::Pending:
        case MissionTaskStatus::Running:
            return false;
        case MissionTaskStatus::Succeeded:
        case MissionTaskStatus::Failed:
        case MissionTaskStatus::Canceled:
        case MissionTaskStatus::Stale:
            return true;
    }
    return false;
}


bool missionTransitionAllowed( MissionTaskStatus from, MissionTaskStatus to )
{
    if ( from == to )
        return true; // idempotent replay (duplicate progress / double cancel)

    switch ( from )
    {
        case MissionTaskStatus::Pending:
            return to == MissionTaskStatus::Running || to == MissionTaskStatus::Canceled
                   || to == MissionTaskStatus::Stale;

        case MissionTaskStatus::Running:
            return to == MissionTaskStatus::Succeeded || to == MissionTaskStatus::Failed
                   || to == MissionTaskStatus::Canceled || to == MissionTaskStatus::Stale;

        case MissionTaskStatus::Succeeded:
            // A finished task only degrades when its references disappear.
            return to == MissionTaskStatus::Stale;

        case MissionTaskStatus::Failed:
            return to == MissionTaskStatus::Pending || to == MissionTaskStatus::Stale;

        case MissionTaskStatus::Canceled:
            return to == MissionTaskStatus::Pending || to == MissionTaskStatus::Stale;

        case MissionTaskStatus::Stale:
            // Stale work must be re-bound (Pending) and re-verified; it can
            // never jump straight back into Running.
            return to == MissionTaskStatus::Pending;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

MissionTask *MissionTimeline::mutableTask( const QString &taskId )
{
    for ( MissionTask &task : mTasks )
    {
        if ( task.id == taskId )
            return &task;
    }
    return nullptr;
}

const MissionTask *MissionTimeline::task( const QString &taskId ) const
{
    for ( const MissionTask &t : mTasks )
    {
        if ( t.id == taskId )
            return &t;
    }
    return nullptr;
}

bool MissionTimeline::hasTask( const QString &taskId ) const
{
    return task( taskId ) != nullptr;
}

MissionOutcome MissionTimeline::addTask( const MissionTask &task )
{
    if ( task.id.trimmed().isEmpty() )
        return MissionOutcome::rejected( QLatin1String( kMissionErrEmptyTaskId ) );
    if ( hasTask( task.id ) )
        return MissionOutcome::rejected( QLatin1String( kMissionErrDuplicateTask ) );

    mTasks.push_back( task );
    MissionEvent ev;
    ev.seq = ++mSeq;
    ev.taskId = task.id;
    ev.from = task.status;
    ev.to = task.status;
    ev.note = QStringLiteral( "task_added" );
    appendEventLocked( std::move( ev ) );
    ++mRevision;
    return MissionOutcome::ok( QLatin1String( kMissionOk ) );
}

QVector<MissionTask> MissionTimeline::tasksForStage( MissionStage stage ) const
{
    QVector<MissionTask> out;
    for ( const MissionTask &t : mTasks )
    {
        if ( t.stage == stage )
            out.push_back( t );
    }
    return out;
}

MissionOutcome MissionTimeline::transition( const QString &taskId,
                                            MissionTaskStatus to,
                                            const QString &iso,
                                            const QString &note,
                                            const QString &errorCode,
                                            const QString &errorMessage )
{
    MissionTask *t = mutableTask( taskId );
    if ( t == nullptr )
        return MissionOutcome::rejected( QLatin1String( kMissionErrUnknownTask ) );

    const MissionTaskStatus from = t->status;
    if ( !missionTransitionAllowed( from, to ) )
    {
        return MissionOutcome::rejected( QStringLiteral( "%1:%2->%3" )
                                             .arg( QLatin1String( kMissionErrIllegalTransition ),
                                                   QLatin1String( missionTaskStatusKey( from ) ),
                                                   QLatin1String( missionTaskStatusKey( to ) ) ) );
    }

    // Fail closed: "Running" without a run authority would let the three
    // surfaces disagree about which execution is in flight.
    if ( to == MissionTaskStatus::Running && from != MissionTaskStatus::Running
         && t->run.isNull() )
    {
        return MissionOutcome::rejected( QLatin1String( kMissionErrStaleRun ) );
    }

    const bool noop = ( from == to );

    if ( !noop )
    {
        switch ( to )
        {
            case MissionTaskStatus::Running:
                t->attempts += 1;
                t->startedIso = iso;
                t->endedIso.clear();
                t->errorCode.clear();
                t->errorMessage.clear();
                break;
            case MissionTaskStatus::Pending:
                t->startedIso.clear();
                t->endedIso.clear();
                t->errorCode.clear();
                t->errorMessage.clear();
                break;
            case MissionTaskStatus::Succeeded:
            case MissionTaskStatus::Canceled:
                t->endedIso = iso;
                t->errorCode.clear();
                t->errorMessage.clear();
                break;
            case MissionTaskStatus::Failed:
                t->endedIso = iso;
                // The latest failure wins; an empty pair clears a previous one.
                t->errorCode = errorCode;
                t->errorMessage = errorMessage;
                break;
            case MissionTaskStatus::Stale:
                t->endedIso = iso;
                break;
        }
        t->status = to;
    }

    MissionEvent ev;
    ev.seq = ++mSeq;
    ev.taskId = taskId;
    ev.from = from;
    ev.to = to;
    ev.iso = iso;
    ev.note = noop ? QStringLiteral( "noop" ) : note;
    appendEventLocked( std::move( ev ) );

    if ( !noop )
        ++mRevision;

    return MissionOutcome::ok( noop ? QStringLiteral( "noop" ) : QLatin1String( kMissionOk ) );
}

MissionOutcome MissionTimeline::retry( const QString &taskId,
                                       const QString &iso,
                                       const MissionRunRef &run,
                                       const QString &note )
{
    MissionTask *t = mutableTask( taskId );
    if ( t == nullptr )
        return MissionOutcome::rejected( QLatin1String( kMissionErrUnknownTask ) );
    if ( !missionTaskStatusIsRetryable( t->status ) )
    {
        return MissionOutcome::rejected( QStringLiteral( "%1:%2" )
                                             .arg( QLatin1String( kMissionErrNotRetryable ),
                                                   QLatin1String( missionTaskStatusKey( t->status ) ) ) );
    }

    const MissionTaskStatus from = t->status;
    if ( t->retryOf.isEmpty() )
        t->retryOf = t->id;
    t->status = MissionTaskStatus::Pending;
    // `attempts` counts executions actually started (Pending -> Running), so a
    // requeue alone must not inflate it.
    t->startedIso.clear();
    t->endedIso.clear();
    t->errorCode.clear();
    t->errorMessage.clear();
    if ( !run.isNull() )
        t->run = run;

    MissionEvent ev;
    ev.seq = ++mSeq;
    ev.taskId = taskId;
    ev.from = from;
    ev.to = MissionTaskStatus::Pending;
    ev.iso = iso;
    ev.note = note.isEmpty() ? QStringLiteral( "retry" ) : note;
    appendEventLocked( std::move( ev ) );
    ++mRevision;
    return MissionOutcome::ok( QLatin1String( kMissionOk ) );
}

MissionOutcome MissionTimeline::bindRunReference( const QString &taskId,
                                                  const MissionRunRef &run,
                                                  const QString &iso,
                                                  const QString &note )
{
    MissionTask *t = mutableTask( taskId );
    if ( t == nullptr )
        return MissionOutcome::rejected( QLatin1String( kMissionErrUnknownTask ) );

    t->run = run;
    ++mRevision;
    // No event: the id is part of the task record and the Pending -> Running
    // transition that follows is the auditable step.
    Q_UNUSED( iso )
    Q_UNUSED( note )
    return MissionOutcome::ok( QLatin1String( kMissionOk ) );
}

MissionStage MissionTimeline::currentStage() const
{
    // Earliest stage that still needs attention; otherwise the furthest stage
    // that produced a result.
    MissionStage fallback = MissionStage::Import;
    for ( MissionStage stage : missionStages() )
    {
        for ( const MissionTask &t : mTasks )
        {
            if ( t.stage != stage )
                continue;
            if ( t.status != MissionTaskStatus::Succeeded )
                return stage;
            fallback = stage;
        }
    }
    return fallback;
}

QVector<MissionEvent> MissionTimeline::eventsSince( quint64 seq ) const
{
    // #1170: ascending seq — the tail is one upper_bound away, not a scan.
    const auto first = std::upper_bound( mEvents.cbegin(), mEvents.cend(), seq,
                                         []( quint64 s, const MissionEvent &ev ) {
                                             return s < ev.seq;
                                         } );
    QVector<MissionEvent> out( first, mEvents.cend() );
    return out;
}

int MissionTimeline::rewriteReference( const QString &fromId,
                                       const QString &toId,
                                       const QString &iso,
                                       const QString &note )
{
    if ( fromId.isEmpty() || toId.isEmpty() || fromId == toId )
        return 0;

    int rewritten = 0;
    QVector<QString> touchedTasks;
    for ( MissionTask &t : mTasks )
    {
        bool touched = false;
        for ( int i = 0; i < t.inputRefIds.size(); ++i )
        {
            if ( t.inputRefIds.at( i ) == fromId )
            {
                t.inputRefIds[ i ] = toId;
                touched = true;
                ++rewritten;
            }
        }
        for ( int i = 0; i < t.outputRefIds.size(); ++i )
        {
            if ( t.outputRefIds.at( i ) == fromId )
            {
                t.outputRefIds[ i ] = toId;
                touched = true;
                ++rewritten;
            }
        }
        if ( t.run.id == fromId )
        {
            t.run.id = toId;
            touched = true;
            ++rewritten;
        }
        if ( touched )
            touchedTasks.push_back( t.id );
    }

    if ( rewritten == 0 )
        return 0;

    // A rename keeps status, attempts and lineage: it is an audit-only change.
    const QString eventNote = note.isEmpty()
                                  ? QStringLiteral( "ref_renamed:%1->%2" ).arg( fromId, toId )
                                  : note;
    for ( const QString &taskId : touchedTasks )
    {
        const MissionTask *t = task( taskId );
        if ( t == nullptr )
            continue;
        MissionEvent ev;
        ev.seq = ++mSeq;
        ev.taskId = taskId;
        ev.from = t->status;
        ev.to = t->status;
        ev.iso = iso;
        ev.note = eventNote;
        appendEventLocked( std::move( ev ) );
    }
    ++mRevision;
    return rewritten;
}

QJsonObject MissionTimeline::toJson() const
{
    QJsonArray tasks;
    for ( const MissionTask &t : mTasks )
    {
        QJsonObject run;
        run.insert( QStringLiteral( "kind" ), t.run.kind );
        run.insert( QStringLiteral( "id" ), t.run.id );

        QJsonObject obj;
        obj.insert( QStringLiteral( "id" ), t.id );
        obj.insert( QStringLiteral( "stage" ), QLatin1String( missionStageKey( t.stage ) ) );
        obj.insert( QStringLiteral( "title" ), t.title );
        obj.insert( QStringLiteral( "capability_id" ), t.capabilityId );
        obj.insert( QStringLiteral( "inputs" ), stringListToJson( t.inputRefIds ) );
        obj.insert( QStringLiteral( "outputs" ), stringListToJson( t.outputRefIds ) );
        obj.insert( QStringLiteral( "status" ), QLatin1String( missionTaskStatusKey( t.status ) ) );
        obj.insert( QStringLiteral( "attempts" ), t.attempts );
        obj.insert( QStringLiteral( "started_iso" ), t.startedIso );
        obj.insert( QStringLiteral( "ended_iso" ), t.endedIso );
        obj.insert( QStringLiteral( "error_code" ), t.errorCode );
        obj.insert( QStringLiteral( "error_message" ), t.errorMessage );
        obj.insert( QStringLiteral( "run" ), run );
        obj.insert( QStringLiteral( "retry_of" ), t.retryOf );
        tasks.push_back( obj );
    }

    QJsonArray events;
    for ( const MissionEvent &ev : mEvents )
    {
        QJsonObject obj;
        obj.insert( QStringLiteral( "seq" ), static_cast<qint64>( ev.seq ) );
        obj.insert( QStringLiteral( "task_id" ), ev.taskId );
        obj.insert( QStringLiteral( "from" ), QLatin1String( missionTaskStatusKey( ev.from ) ) );
        obj.insert( QStringLiteral( "to" ), QLatin1String( missionTaskStatusKey( ev.to ) ) );
        obj.insert( QStringLiteral( "iso" ), ev.iso );
        obj.insert( QStringLiteral( "note" ), ev.note );
        events.push_back( obj );
    }

    QJsonObject root;
    root.insert( QStringLiteral( "kind" ), QLatin1String( kMissionTimelineKind ) );
    root.insert( QStringLiteral( "schema_version" ), QLatin1String( kMissionTimelineSchemaVersion ) );
    root.insert( QStringLiteral( "mission_id" ), mMissionId );
    root.insert( QStringLiteral( "project_ref" ), mProjectRef );
    root.insert( QStringLiteral( "revision" ), static_cast<qint64>( mRevision ) );
    root.insert( QStringLiteral( "last_event_seq" ), static_cast<qint64>( mSeq ) );
    root.insert( QStringLiteral( "tasks" ), tasks );
    root.insert( QStringLiteral( "events" ), events );
    // #1170: the log is bounded — persist the truncation marker and the
    // first retained seq so incremental consumers never read a silent gap.
    root.insert( QStringLiteral( "events_truncated" ), mEventsTruncated );
    root.insert( QStringLiteral( "first_event_seq" ),
                 static_cast<qint64>( mFirstRetainedSeq ) );
    return root;
}

bool MissionTimeline::fromJson( const QJsonObject &obj, QString *error )
{
    const auto fail = [&error]( const QString &why ) -> bool {
        if ( error )
            *error = why;
        return false;
    };

    if ( obj.value( QStringLiteral( "kind" ) ).toString() != QLatin1String( kMissionTimelineKind ) )
        return fail( QStringLiteral( "unexpected_kind" ) );
    if ( obj.value( QStringLiteral( "schema_version" ) ).toString()
         != QLatin1String( kMissionTimelineSchemaVersion ) )
        return fail( QStringLiteral( "unsupported_schema_version" ) );

    MissionTimeline decoded;
    decoded.mMissionId = obj.value( QStringLiteral( "mission_id" ) ).toString();
    decoded.mProjectRef = obj.value( QStringLiteral( "project_ref" ) ).toString();

    const QJsonValue tasksValue = obj.value( QStringLiteral( "tasks" ) );
    if ( !tasksValue.isArray() )
        return fail( QStringLiteral( "tasks_not_array" ) );

    QSet<QString> seenIds;
    for ( const QJsonValue &tv : tasksValue.toArray() )
    {
        if ( !tv.isObject() )
            return fail( QStringLiteral( "task_not_object" ) );
        const QJsonObject to = tv.toObject();

        MissionTask t;
        t.id = to.value( QStringLiteral( "id" ) ).toString();
        if ( t.id.trimmed().isEmpty() )
            return fail( QStringLiteral( "task_id_empty" ) );
        if ( seenIds.contains( t.id ) )
            return fail( QStringLiteral( "duplicate_task_id" ) );
        seenIds.insert( t.id );

        const QString stageKey = to.value( QStringLiteral( "stage" ) ).toString();
        auto stage = missionStageFromKey( stageKey );
        if ( !stage.has_value() )
            return fail( QStringLiteral( "unknown_stage" ) );
        t.stage = *stage;

        const QString statusKey = to.value( QStringLiteral( "status" ) ).toString();
        auto status = missionTaskStatusFromKey( statusKey );
        if ( !status.has_value() )
            return fail( QStringLiteral( "unknown_status" ) );
        t.status = *status;

        t.title = to.value( QStringLiteral( "title" ) ).toString();
        t.capabilityId = to.value( QStringLiteral( "capability_id" ) ).toString();
        t.inputRefIds = stringListFromJson( to.value( QStringLiteral( "inputs" ) ) );
        t.outputRefIds = stringListFromJson( to.value( QStringLiteral( "outputs" ) ) );
        t.attempts = static_cast<int>( intFromJson( to.value( QStringLiteral( "attempts" ) ) ) );
        t.startedIso = to.value( QStringLiteral( "started_iso" ) ).toString();
        t.endedIso = to.value( QStringLiteral( "ended_iso" ) ).toString();
        t.errorCode = to.value( QStringLiteral( "error_code" ) ).toString();
        t.errorMessage = to.value( QStringLiteral( "error_message" ) ).toString();
        t.retryOf = to.value( QStringLiteral( "retry_of" ) ).toString();

        const QJsonObject runObj = to.value( QStringLiteral( "run" ) ).toObject();
        t.run.kind = runObj.value( QStringLiteral( "kind" ) ).toString();
        t.run.id = runObj.value( QStringLiteral( "id" ) ).toString();

        decoded.mTasks.push_back( std::move( t ) );
    }

    const QJsonValue eventsValue = obj.value( QStringLiteral( "events" ) );
    if ( !eventsValue.isArray() )
        return fail( QStringLiteral( "events_not_array" ) );

    for ( const QJsonValue &ev : eventsValue.toArray() )
    {
        if ( !ev.isObject() )
            return fail( QStringLiteral( "event_not_object" ) );
        const QJsonObject eo = ev.toObject();

        MissionEvent e;
        e.seq = static_cast<quint64>( intFromJson( eo.value( QStringLiteral( "seq" ) ) ) );
        e.taskId = eo.value( QStringLiteral( "task_id" ) ).toString();
        auto from = missionTaskStatusFromKey( eo.value( QStringLiteral( "from" ) ).toString() );
        auto to = missionTaskStatusFromKey( eo.value( QStringLiteral( "to" ) ).toString() );
        if ( !from.has_value() || !to.has_value() )
            return fail( QStringLiteral( "unknown_event_status" ) );
        e.from = *from;
        e.to = *to;
        e.iso = eo.value( QStringLiteral( "iso" ) ).toString();
        e.note = eo.value( QStringLiteral( "note" ) ).toString();
        decoded.appendEventLocked( std::move( e ) );
    }

    decoded.mEventsTruncated =
        obj.value( QStringLiteral( "events_truncated" ) ).toBool( false );
    decoded.mFirstRetainedSeq = static_cast<quint64>( intFromJson(
        obj.value( QStringLiteral( "first_event_seq" ) ) ) );
    if ( decoded.mFirstRetainedSeq < 1 )
        decoded.mFirstRetainedSeq = 1;
    decoded.mRevision = static_cast<quint64>(
        intFromJson( obj.value( QStringLiteral( "revision" ) ) ) );
    decoded.mSeq = static_cast<quint64>(
        intFromJson( obj.value( QStringLiteral( "last_event_seq" ) ) ) );
    // Backfill from the log itself: a short or tampered last_event_seq must
    // never make the next mutation mint a DUPLICATE seq — that would break
    // eventsSince() for every incremental consumer (GUI model, agent cursor).
    if ( !decoded.mEvents.isEmpty() )
        decoded.mSeq = qMax( decoded.mSeq, decoded.mEvents.last().seq );

    *this = std::move( decoded );
    return true;
}

// ---------------------------------------------------------------------------
// Reconciliation
// ---------------------------------------------------------------------------

MissionReconciliation reconcileMission( const MissionTimeline &timeline,
                                        const MissionRefResolver &resolver )
{
    MissionReconciliation rec;
    if ( !resolver )
        return rec;

    // Distinct refs only: a mission with 300 layers referenced by 2000 tasks
    // still costs 300 resolver calls.
    QSet<QString> distinct;
    for ( const MissionTask &t : timeline.tasks() )
    {
        for ( const QString &id : t.inputRefIds )
            distinct.insert( id );
        for ( const QString &id : t.outputRefIds )
            distinct.insert( id );
    }

    QSet<QString> dead;
    for ( const QString &id : distinct )
    {
        if ( id.trimmed().isEmpty() )
            continue;
        MissionRefStatus status = resolver( id );
        MissionRefHealth health;
        health.refId = id;
        health.alive = status.alive;
        health.reason = status.alive ? QString() : ( status.reason.isEmpty()
                                                         ? QStringLiteral( "missing_reference" )
                                                         : status.reason );
        rec.refs.push_back( health );
        if ( !status.alive )
        {
            dead.insert( id );
            rec.danglingRefIds.push_back( id );
        }
    }

    for ( const MissionTask &t : timeline.tasks() )
    {
        bool affected = false;
        for ( const QString &id : t.inputRefIds )
        {
            if ( dead.contains( id ) )
            {
                affected = true;
                break;
            }
        }
        if ( !affected )
        {
            for ( const QString &id : t.outputRefIds )
            {
                if ( dead.contains( id ) )
                {
                    affected = true;
                    break;
                }
            }
        }
        if ( affected )
            rec.staleTaskIds.push_back( t.id );
    }

    rec.danglingRefIds.sort();
    return rec;
}

int applyReconciliation( MissionTimeline &timeline,
                         const MissionReconciliation &rec,
                         const QString &iso )
{
    int moved = 0;
    for ( const QString &taskId : rec.staleTaskIds )
    {
        const MissionTask *t = timeline.task( taskId );
        if ( t == nullptr || t->status == MissionTaskStatus::Stale )
            continue;
        const MissionOutcome outcome = timeline.transition( taskId, MissionTaskStatus::Stale, iso,
                                                            QStringLiteral( "reference_lost" ) );
        if ( outcome.applied )
            ++moved;
    }
    return moved;
}

int applyRenames( MissionTimeline &timeline,
                  const QVector<MissionRename> &renames,
                  const QString &iso )
{
    int rewritten = 0;
    for ( const MissionRename &rename : renames )
    {
        rewritten += timeline.rewriteReference(
            rename.fromId, rename.toId, iso,
            QStringLiteral( "ref_renamed:%1->%2" ).arg( rename.fromId, rename.toId ) );
    }
    return rewritten;
}

} // namespace sicnu::app

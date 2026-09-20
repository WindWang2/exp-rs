/***************************************************************************
 * mission_projection.cpp — shared mission projections + surface registry
 ***************************************************************************/

#include "app/workbench/mission_projection.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace sicnu::app
{

QByteArray missionCanonicalJson( const QJsonObject &obj )
{
    // QJsonDocument::Compact + fixed insertion order == deterministic bytes.
    return QJsonDocument( obj ).toJson( QJsonDocument::Compact );
}

QJsonObject missionTaskProjectionJson( const MissionTask &task )
{
    QJsonObject obj;
    obj.insert( QStringLiteral( "id" ), task.id );
    obj.insert( QStringLiteral( "stage" ), QLatin1String( missionStageKey( task.stage ) ) );
    obj.insert( QStringLiteral( "title" ), task.title );
    obj.insert( QStringLiteral( "capability_id" ), task.capabilityId );
    obj.insert( QStringLiteral( "status" ), QLatin1String( missionTaskStatusKey( task.status ) ) );
    obj.insert( QStringLiteral( "attempts" ), task.attempts );
    obj.insert( QStringLiteral( "inputs" ), QJsonArray::fromStringList( task.inputRefIds ) );
    obj.insert( QStringLiteral( "outputs" ), QJsonArray::fromStringList( task.outputRefIds ) );
    obj.insert( QStringLiteral( "started_iso" ), task.startedIso );
    obj.insert( QStringLiteral( "ended_iso" ), task.endedIso );
    obj.insert( QStringLiteral( "error_code" ), task.errorCode );
    obj.insert( QStringLiteral( "error_message" ), task.errorMessage );
    obj.insert( QStringLiteral( "run_kind" ), task.run.kind );
    obj.insert( QStringLiteral( "run_id" ), task.run.id );
    obj.insert( QStringLiteral( "retry_of" ), task.retryOf );
    return obj;
}

QJsonObject missionTimelineProjectionJson( const MissionTimeline &timeline,
                                           int maxItems,
                                           quint64 sinceSeq )
{
    QJsonArray stageSummary;
    for ( MissionStage stage : missionStages() )
    {
        const QVector<MissionTask> tasks = timeline.tasksForStage( stage );
        int succeeded = 0;
        int failed = 0;
        int canceled = 0;
        int stale = 0;
        int running = 0;
        int pending = 0;
        for ( const MissionTask &t : tasks )
        {
            switch ( t.status )
            {
                case MissionTaskStatus::Pending:
                    ++pending;
                    break;
                case MissionTaskStatus::Running:
                    ++running;
                    break;
                case MissionTaskStatus::Succeeded:
                    ++succeeded;
                    break;
                case MissionTaskStatus::Failed:
                    ++failed;
                    break;
                case MissionTaskStatus::Canceled:
                    ++canceled;
                    break;
                case MissionTaskStatus::Stale:
                    ++stale;
                    break;
            }
        }
        QJsonObject entry;
        entry.insert( QStringLiteral( "stage" ), QLatin1String( missionStageKey( stage ) ) );
        entry.insert( QStringLiteral( "total" ), tasks.size() );
        entry.insert( QStringLiteral( "pending" ), pending );
        entry.insert( QStringLiteral( "running" ), running );
        entry.insert( QStringLiteral( "succeeded" ), succeeded );
        entry.insert( QStringLiteral( "failed" ), failed );
        entry.insert( QStringLiteral( "canceled" ), canceled );
        entry.insert( QStringLiteral( "stale" ), stale );
        stageSummary.push_back( entry );
    }

    const QVector<MissionTask> all = timeline.tasks();
    // maxItems <= 0 means "no task rows" (metadata only) — used by the agent
    // when it only wants the stage summary and the event cursor.
    const int shown = qMax( 0, qMin( maxItems, all.size() ) );

    QJsonArray tasks;
    for ( int i = 0; i < shown; ++i )
        tasks.push_back( missionTaskProjectionJson( all.at( i ) ) );

    QJsonArray events;
    for ( const MissionEvent &ev : timeline.eventsSince( sinceSeq ) )
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

    QJsonObject obj;
    obj.insert( QStringLiteral( "kind" ), QLatin1String( kMissionTimelineKind ) );
    obj.insert( QStringLiteral( "schema_version" ), QLatin1String( kMissionTimelineSchemaVersion ) );
    obj.insert( QStringLiteral( "mission_id" ), timeline.missionId() );
    obj.insert( QStringLiteral( "project_ref" ), timeline.projectRef() );
    obj.insert( QStringLiteral( "current_stage" ),
                QLatin1String( missionStageKey( timeline.currentStage() ) ) );
    obj.insert( QStringLiteral( "revision" ), static_cast<qint64>( timeline.revision() ) );
    obj.insert( QStringLiteral( "last_event_seq" ), static_cast<qint64>( timeline.lastEventSeq() ) );
    obj.insert( QStringLiteral( "task_count" ), all.size() );
    obj.insert( QStringLiteral( "task_truncated" ), shown < all.size() );
    obj.insert( QStringLiteral( "stages" ), stageSummary );
    obj.insert( QStringLiteral( "tasks" ), tasks );
    obj.insert( QStringLiteral( "events" ), events );
    return obj;
}

QJsonObject missionReconciliationProjectionJson( const MissionReconciliation &rec )
{
    QJsonArray refs;
    for ( const MissionRefHealth &health : rec.refs )
    {
        QJsonObject obj;
        obj.insert( QStringLiteral( "ref_id" ), health.refId );
        obj.insert( QStringLiteral( "alive" ), health.alive );
        obj.insert( QStringLiteral( "reason" ), health.reason );
        refs.push_back( obj );
    }

    QJsonObject obj;
    obj.insert( QStringLiteral( "dangling_refs" ), QJsonArray::fromStringList( rec.danglingRefIds ) );
    obj.insert( QStringLiteral( "stale_tasks" ), QJsonArray::fromStringList( rec.staleTaskIds ) );
    obj.insert( QStringLiteral( "has_issues" ), rec.hasIssues() );
    obj.insert( QStringLiteral( "refs" ), refs );
    return obj;
}

// ---------------------------------------------------------------------------
// Surface registry
// ---------------------------------------------------------------------------

const char *missionSurfaceKey( MissionSurface surface )
{
    switch ( surface )
    {
        case MissionSurface::AgentTool:
            return "agent_tool";
        case MissionSurface::AppCommand:
            return "app_command";
        case MissionSurface::ArtifactKind:
            return "artifact_kind";
    }
    return "agent_tool";
}

const QVector<MissionSurfaceEntry> &missionSurfaceRegistry()
{
    static const QVector<MissionSurfaceEntry> kRegistry = {
        // --- MCP / Agent tool ids (family "mission", allowed in
        //     surface_registry.cpp kAllowed and Pi EXP_RS_TOOL_CATEGORIES) ---
        { QStringLiteral( "mission:context" ), MissionSurface::AgentTool,
          QStringLiteral( "Read the current mission context projection: layers, selection, "
                          "extent, CRS, temporal window, current run and recent artifacts." ) },
        { QStringLiteral( "mission:timeline" ), MissionSurface::AgentTool,
          QStringLiteral( "Read the mission task timeline: stage summary, tasks and events "
                          "after an optional cursor (since_seq)." ) },
        { QStringLiteral( "mission:advance" ), MissionSurface::AgentTool,
          QStringLiteral( "Request a mission task transition (start/cancel/retry). Routes "
                          "through TaskCenter; rejects illegal transitions without mutating." ) },

        // --- CommandRegistry ids (desktop shell) ---
        { QStringLiteral( "mission.timeline.show" ), MissionSurface::AppCommand,
          QStringLiteral( "Show/focus the mission timeline dock." ) },
        { QStringLiteral( "mission.task.retry" ), MissionSurface::AppCommand,
          QStringLiteral( "Retry the selected failed or canceled mission task." ) },
        { QStringLiteral( "mission.task.resume" ), MissionSurface::AppCommand,
          QStringLiteral( "Resume the selected stale or canceled mission task after re-binding "
                          "its references." ) },

        // --- Persisted artifact kinds ---
        { QStringLiteral( "mission.timeline.json" ), MissionSurface::ArtifactKind,
          QStringLiteral( "Mission timeline sidecar artifact (schema_version 1.0)." ) },
        { QStringLiteral( "mission.context.json" ), MissionSurface::ArtifactKind,
          QStringLiteral( "Mission context sidecar artifact produced by D18 (#991)." ) },
    };
    return kRegistry;
}

QStringList missionSurfaceIds( const QVector<MissionSurfaceEntry> &entries )
{
    QStringList out;
    out.reserve( entries.size() );
    for ( const MissionSurfaceEntry &entry : entries )
        out.push_back( entry.id );
    return out;
}

QStringList missionSurfaceIds( MissionSurface surface )
{
    QStringList out;
    for ( const MissionSurfaceEntry &entry : missionSurfaceRegistry() )
    {
        if ( entry.surface == surface )
            out.push_back( entry.id );
    }
    return out;
}

QStringList missionDuplicateSurfaceIds( const QVector<MissionSurfaceEntry> &entries )
{
    QSet<QString> seen;
    QSet<QString> dupes;
    for ( const MissionSurfaceEntry &entry : entries )
    {
        if ( !seen.contains( entry.id ) )
        {
            seen.insert( entry.id );
        }
        else
        {
            dupes.insert( entry.id );
        }
    }
    QStringList out( dupes.begin(), dupes.end() );
    out.sort();
    return out;
}

QStringList missionDuplicateSurfaceIds()
{
    return missionDuplicateSurfaceIds( missionSurfaceRegistry() );
}

QStringList missionPhantomSurfaceIds( const QVector<MissionSurfaceEntry> &entries )
{
    // A phantom is an advertised id the agent cannot explain: empty id or
    // empty description.
    QStringList out;
    for ( const MissionSurfaceEntry &entry : entries )
    {
        if ( entry.id.trimmed().isEmpty() || entry.description.trimmed().isEmpty() )
            out.push_back( entry.id );
    }
    out.sort();
    return out;
}

QStringList missionPhantomSurfaceIds()
{
    return missionPhantomSurfaceIds( missionSurfaceRegistry() );
}

QString missionPiToolName( const QString &mcpToolId )
{
    // Mirrors pi/mcp_bridge.ts: "exprs_" + id with every character outside
    // [A-Za-z0-9_-] replaced by "_".
    QString out = QStringLiteral( "exprs_" );
    for ( QChar ch : mcpToolId )
    {
        const char16_t c = ch.unicode();
        const bool keep = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
                          || ( c >= '0' && c <= '9' ) || c == '_' || c == '-';
        out.append( keep ? ch : QLatin1Char( '_' ) );
    }
    return out;
}

QStringList missionPiToolNames()
{
    QStringList out;
    for ( const MissionSurfaceEntry &entry : missionSurfaceRegistry() )
    {
        if ( entry.surface == MissionSurface::AgentTool )
            out.push_back( missionPiToolName( entry.id ) );
    }
    return out;
}

QStringList missionDuplicatePiToolNames()
{
    const QStringList names = missionPiToolNames();
    QSet<QString> seen;
    QSet<QString> dupes;
    for ( const QString &name : names )
    {
        if ( !seen.contains( name ) )
        {
            seen.insert( name );
        }
        else
        {
            dupes.insert( name );
        }
    }
    QStringList out( dupes.begin(), dupes.end() );
    out.sort();
    return out;
}

} // namespace sicnu::app

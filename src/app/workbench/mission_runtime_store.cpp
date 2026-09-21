/***************************************************************************
 * mission_runtime_store.cpp — single-authority mission runtime persistence
 ***************************************************************************/

#include "app/workbench/mission_runtime_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace sicnu::app
{

namespace
{

constexpr const char *kXmlElementName = "sicnuMissionContext";
constexpr const char *kLegacySidecarMarker = "legacy-sidecar-12.0";

/// Read one specific sidecar file with the same fail-closed decode as
/// mission_context_store's loader (the store derives the path itself, the
/// last-good channel needs an explicit path).
bool readContextSidecarAt( const QString &path, MissionContext &out, QString *error )
{
    QFile file( path );
    if ( !file.exists() )
    {
        if ( error )
            *error = QStringLiteral( "sidecar missing: %1" ).arg( path );
        return false;
    }
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        if ( error )
            *error = QStringLiteral( "cannot open sidecar: %1" ).arg( path );
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &pe );
    if ( pe.error != QJsonParseError::NoError || !doc.isObject() )
    {
        if ( error )
            *error = QStringLiteral( "sidecar JSON parse error: %1" ).arg( pe.errorString() );
        return false;
    }
    return missionContextFromJson( doc.object(), out, error );
}

bool writeBytesAtomically( const QString &path, const QByteArray &bytes, QString *error )
{
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        if ( error )
            *error = QStringLiteral( "cannot open for write: %1" ).arg( path );
        return false;
    }
    if ( file.write( bytes ) != static_cast<qint64>( bytes.size() ) )
    {
        file.cancelWriting();
        if ( error )
            *error = QStringLiteral( "short write: %1" ).arg( path );
        return false;
    }
    if ( !file.commit() )
    {
        if ( error )
            *error = QStringLiteral( "commit failed: %1" ).arg( path );
        return false;
    }
    return true;
}

/// Snapshot the just-committed authority sidecar as the last-known-good
/// copy, so a later corruption costs nothing. A sidecar that no longer
/// decodes is never snapshotted (the older good copy survives). Best-effort:
/// the primary write already committed, so a snapshot failure does not fail
/// the save.
void rotateLastGood( const QString &projectFilePath )
{
    const QString side = missionSidecarPathForProject( projectFilePath );
    if ( side.isEmpty() || !QFileInfo::exists( side ) )
        return;
    MissionContext probe;
    QString probeErr;
    if ( !readContextSidecarAt( side, probe, &probeErr ) )
        return;
    QFile in( side );
    if ( !in.open( QIODevice::ReadOnly ) )
        return;
    const QByteArray bytes = in.readAll();
    in.close();
    if ( bytes.isEmpty() )
        return;
    QString ignored;
    writeBytesAtomically( missionRuntimeLastGoodPathForProject( projectFilePath ), bytes, &ignored );
}

/// Adopt the legacy 12.0 timeline sidecar (import-only channel).
bool adoptLegacyTimeline( const QString &projectFilePath, MissionTimeline &out,
                          QString *problem )
{
    const QString path = missionTimelineLegacySidecarPathForProject( projectFilePath );
    if ( path.isEmpty() || !QFileInfo::exists( path ) )
        return false;

    MissionTimeline legacy;
    bool loaded = false;
    QString err;
    if ( !loadMissionTimelineFromSidecar( projectFilePath, legacy, &loaded, &err ) || !loaded )
    {
        // A corrupt legacy residue is NOT the authority: record it and open
        // with an empty task space rather than refusing the project.
        if ( problem )
            *problem = QStringLiteral( "legacy_timeline_unreadable:%1" )
                           .arg( err.isEmpty() ? QStringLiteral( "unknown" ) : err );
        return false;
    }
    out = std::move( legacy );
    return true;
}

} // namespace

QString missionTimelineLegacySidecarPathForProject( const QString &projectFilePath )
{
    return missionTimelineSidecarPathForProject( projectFilePath );
}

QString missionRuntimeLastGoodPathForProject( const QString &projectFilePath )
{
    const QString side = missionSidecarPathForProject( projectFilePath );
    if ( side.isEmpty() )
        return {};
    return side + QStringLiteral( ".last-good" );
}

bool loadMissionRuntime( const QString &projectFilePath, const QDomDocument &projectDocument,
                         MissionRuntimeState &out, QString *error )
{
    out = MissionRuntimeState{};

    if ( projectFilePath.isEmpty() && projectDocument.isNull() )
        return true; // no project at all: fresh runtime, not an error

    // ── authority channel 1: sidecar (with last-good recovery) ──────────
    bool authorityDecoded = false;
    if ( !projectFilePath.isEmpty() )
    {
        const QString side = missionSidecarPathForProject( projectFilePath );
        if ( QFileInfo::exists( side ) )
        {
            QString sideErr;
            if ( readContextSidecarAt( side, out.context, &sideErr ) )
            {
                authorityDecoded = true;
            }
            else
            {
                const QString lastGood = missionRuntimeLastGoodPathForProject( projectFilePath );
                QString lgErr;
                if ( !lastGood.isEmpty() && QFileInfo::exists( lastGood )
                     && readContextSidecarAt( lastGood, out.context, &lgErr ) )
                {
                    authorityDecoded = true;
                    out.recoveredFromLastGood = true;
                    out.notices.append( QStringLiteral( "authority_recovered_from_last_good" ) );
                }
                else
                {
                    out.problems.append(
                        QStringLiteral( "authority_sidecar_unreadable:%1" ).arg( sideErr ) );
                }
            }
        }
    }

    // ── authority channel 2: project XML (same authority, .qgz channel) ─
    const bool xmlArtifactPresent =
        !projectDocument.isNull()
        && !projectDocument.documentElement()
               .firstChildElement( QLatin1String( kXmlElementName ) )
               .isNull();
    if ( !authorityDecoded && xmlArtifactPresent )
    {
        QString xmlErr;
        if ( readMissionContextFromProjectXml( projectDocument, out.context, &xmlErr ) )
        {
            authorityDecoded = true;
        }
        else
        {
            out.problems.append( QStringLiteral( "authority_xml_unreadable:%1" ).arg( xmlErr ) );
        }
    }

    const bool sidecarArtifactPresent =
        !projectFilePath.isEmpty()
        && QFileInfo::exists( missionSidecarPathForProject( projectFilePath ) );

    if ( !authorityDecoded )
    {
        if ( sidecarArtifactPresent || xmlArtifactPresent )
        {
            // The authority exists but decoded nowhere and last-good did not
            // recover it: refuse. Saving over it would destroy the artifact
            // the user still has.
            out.authorityCorrupt = true;
            if ( error )
                *error = out.problems.isEmpty()
                             ? QStringLiteral( "authority_unreadable" )
                             : out.problems.join( QLatin1Char( ';' ) );
            return false;
        }
        // Fresh project: no authority anywhere.
        out.authorityLoaded = false;
    }
    else
    {
        out.authorityLoaded = true;
    }

    // ── timeline resolution: authority first, legacy import second ──────
    if ( out.authorityLoaded )
    {
        MissionTimeline embedded;
        QString extractErr;
        if ( extractMissionTimeline( out.context, embedded, &extractErr ) )
        {
            out.timeline = std::move( embedded );
        }
        else if ( extractErr == QLatin1String( "missing" ) )
        {
            MissionTimeline legacy;
            QString problem;
            if ( adoptLegacyTimeline( projectFilePath, legacy, &problem ) )
            {
                out.timeline = std::move( legacy );
                out.timelineMigrated = true;
                out.notices.append( QStringLiteral( "timeline_migrated_from_legacy_sidecar" ) );
                // Record the provenance in memory so the next save persists
                // it and a second open reports the same notice.
                setMissionTimelineMigrationSource( out.context,
                                                   QLatin1String( kLegacySidecarMarker ) );
            }
            else if ( !problem.isEmpty() )
            {
                out.problems.append( problem );
            }
        }
        else
        {
            // A present-but-unusable embedded timeline (unknown kind, future
            // schema_version, malformed payload) is a corrupt/future
            // authority: refuse rather than silently dropping the task space
            // or downgrading it on the next save.
            out.authorityCorrupt = true;
            out.problems.append( QStringLiteral( "embedded_timeline:%1" ).arg( extractErr ) );
            if ( error )
                *error = extractErr;
            return false;
        }
    }
    else
    {
        // No authority document at all: a lone 12.0 timeline sidecar is
        // still adopted so the task space survives a lost context sidecar.
        MissionTimeline legacy;
        QString problem;
        if ( adoptLegacyTimeline( projectFilePath, legacy, &problem ) )
        {
            out.timeline = std::move( legacy );
            out.timelineMigrated = true;
            out.notices.append( QStringLiteral( "timeline_migrated_from_legacy_sidecar" ) );
            setMissionTimelineMigrationSource( out.context, QLatin1String( kLegacySidecarMarker ) );
        }
        else if ( !problem.isEmpty() )
        {
            out.problems.append( problem );
        }
    }

    // ── identity sync: the context is the join key ──────────────────────
    if ( !out.context.missionId.isEmpty() )
    {
        out.timeline.setMissionId( out.context.missionId );
    }
    else if ( !out.timeline.missionId().isEmpty() )
    {
        out.context.missionId = out.timeline.missionId();
    }
    if ( out.timeline.projectRef().isEmpty() && !projectFilePath.isEmpty() )
        out.timeline.setProjectRef( projectFilePath );

    // A previously migrated mission reports the notice on every open (the
    // marker lives in the authority); the timeline itself is not re-mutated.
    if ( !out.timelineMigrated )
    {
        const QString source = missionTimelineMigrationSource( out.context );
        if ( !source.isEmpty() )
            out.notices.append( QStringLiteral( "timeline_migrated_from_legacy_sidecar" ) );
    }

    return true;
}

bool saveMissionRuntime( const QString &projectFilePath, QDomDocument &projectDocument,
                         MissionRuntimeState &state, QString *error )
{
    if ( state.authorityCorrupt )
    {
        // Poisoned state (corrupt or future-version authority): publishing it
        // would overwrite the last known usable artifact with a degraded or
        // downgraded document. Refuse; the caller must recover explicitly.
        if ( error )
            *error = QStringLiteral( "poisoned_authority" );
        return false;
    }

    MissionContext ctx = state.context;
    // Join key both ways: an id-less context adopts the timeline's id before
    // ensureMissionId mints a fresh one.
    if ( ctx.missionId.isEmpty() && !state.timeline.missionId().isEmpty() )
        ctx.missionId = state.timeline.missionId();
    ensureMissionId( ctx );
    if ( ctx.projectRef.isEmpty() && !projectFilePath.isEmpty() )
        ctx.projectRef = projectFilePath;

    MissionTimeline timeline = state.timeline;
    timeline.setMissionId( ctx.missionId );
    if ( timeline.projectRef().isEmpty() && !projectFilePath.isEmpty() )
        timeline.setProjectRef( projectFilePath );
    embedMissionTimeline( ctx, timeline );
    if ( state.timelineMigrated )
        setMissionTimelineMigrationSource( ctx, QLatin1String( kLegacySidecarMarker ) );

    QString persistErr;
    if ( !persistMissionContextWithProject( projectFilePath, projectDocument, ctx, &persistErr ) )
    {
        if ( error )
            *error = persistErr;
        return false;
    }

    // Rotate the last-known-good snapshot AFTER a successful commit: the
    // snapshot is exactly the last state the authority durably held. A failed
    // save leaves both the old sidecar and the old snapshot untouched (the
    // write itself is QSaveFile-atomic).
    if ( !projectFilePath.isEmpty() )
        rotateLastGood( projectFilePath );

    // The in-memory state now matches the authority exactly.
    state.context = std::move( ctx );
    state.timeline = std::move( timeline );
    state.authorityLoaded = true;
    state.authorityCorrupt = false;
    state.recoveredFromLastGood = false;
    state.timelineMigrated = false;
    return true;
}

} // namespace sicnu::app

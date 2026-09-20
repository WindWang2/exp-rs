// tests/test_mission_runtime_persistence.cpp
//
// Mission Runtime 13.0 — single-authority persistence gates (WP1/WP4).
// Compiles the REAL repo sources (see mission-runtime-gate/CMakeLists.txt).
//
// Oracles covered (.planning/glm53-mission-runtime-13/ORACLES.md):
//   O1 single authority        — one document, no legacy write
//   O2 tamper precedence       — a forged legacy copy cannot override
//   O3 legacy migration        — 12.0 sidecar adopted, idempotent
//   O4 fail-closed versioning  — future schema refused, poison never saved
//   O5 restart/reopen          — fresh PROCESS reload from disk only
//   O6 layer delete/rename     — refs reconcile, rename preserves status
//   O8 no fake Running         — crash/reopen run-authority reconciliation
//   + last-known-good recovery and the fingerprint stability property

#include "app/workbench/mission_context.h"
#include "app/workbench/mission_context_store.h"
#include "app/workbench/mission_projection.h"
#include "app/workbench/mission_runtime_store.h"
#include "app/workbench/mission_run_authority.h"
#include "app/workbench/mission_stage.h"
#include "app/workbench/mission_timeline_bridge.h"
#include "app/workbench/mission_timeline_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QCryptographicHash>
#include <QCoreApplication>

#include <cstdio>

#include <catch2/catch_test_macros.hpp>

using namespace sicnu::app;

namespace
{

QString uniqueStem()
{
    return QStringLiteral( "mission-%1" )
        .arg( QString::number( QRandomGenerator::global()->generate(), 16 ) );
}

MissionTask makeTask( const QString &id, MissionStage stage, const QString &title,
                      MissionTaskStatus status = MissionTaskStatus::Pending,
                      const QStringList &inputs = {}, const QStringList &outputs = {} )
{
    MissionTask t;
    t.id = id;
    t.stage = stage;
    t.title = title;
    t.status = status;
    t.capabilityId = QStringLiteral( "rs:spectral_index" );
    t.inputRefIds = inputs;
    t.outputRefIds = outputs;
    return t;
}

MissionRuntimeState makeRuntime( const QString &missionId )
{
    MissionRuntimeState state;
    state.context.missionId = missionId;
    state.context.missionName = QStringLiteral( "Gate mission" );
    MissionTimeline &tl = state.timeline;
    tl.setMissionId( missionId );
    tl.addTask( makeTask( QStringLiteral( "import-1" ), MissionStage::Import,
                          QStringLiteral( "Import scene" ), MissionTaskStatus::Succeeded ) );
    tl.addTask( makeTask( QStringLiteral( "pre-1" ), MissionStage::Preprocess,
                          QStringLiteral( "Atmos correction" ), MissionTaskStatus::Failed,
                          { QStringLiteral( "layer-a" ) } ) );
    tl.addTask( makeTask( QStringLiteral( "ana-1" ), MissionStage::Analyze,
                          QStringLiteral( "NDVI" ), MissionTaskStatus::Running,
                          { QStringLiteral( "layer-a" ) }, { QStringLiteral( "artifact-ndvi" ) } ) );
    MissionRunRef run;
    run.kind = QStringLiteral( "task_center" );
    run.id = QStringLiteral( "4242" );
    tl.bindRunReference( QStringLiteral( "ana-1" ), run );
    tl.transition( QStringLiteral( "ana-1" ), MissionTaskStatus::Running,
                   QStringLiteral( "2026-09-21T00:00:00Z" ) );
    return state;
}

QByteArray projectionHash( const MissionTimeline &timeline )
{
    const QByteArray bytes = missionCanonicalJson( missionTimelineProjectionJson( timeline, 64, 0 ) );
    return QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex();
}

QByteArray readAll( const QString &path )
{
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
        return {};
    return f.readAll();
}

void writeAll( const QString &path, const QByteArray &bytes )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( f.write( bytes ) == static_cast<qint64>( bytes.size() ) );
}

QJsonObject sidecarDoc( const QString &projectPath )
{
    const QByteArray bytes = readAll( missionSidecarPathForProject( projectPath ) );
    REQUIRE( !bytes.isEmpty() );
    return QJsonDocument::fromJson( bytes ).object();
}

} // namespace

// ── O1: one authority, one write path ────────────────────────────────────

TEST_CASE( "mission runtime save writes exactly one authority", "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    MissionRuntimeState state = makeRuntime( QStringLiteral( "mission-o1" ) );
    QDomDocument doc;
    QString err;
    REQUIRE( saveMissionRuntime( project, doc, state, &err ) );

    // The authority sidecar exists and carries the embedded timeline.
    const QJsonObject authority = sidecarDoc( project );
    CHECK( authority.value( QStringLiteral( "kind" ) ).toString()
           == QLatin1String( "mission_context" ) );
    const QJsonObject metadata = authority.value( QStringLiteral( "metadata" ) ).toObject();
    const QJsonObject embedded =
        metadata.value( QLatin1String( kMissionTimelineMetadataKey ) ).toObject();
    CHECK( embedded.value( QStringLiteral( "kind" ) ).toString()
           == QLatin1String( "mission_timeline" ) );
    CHECK( embedded.value( QStringLiteral( "schema_version" ) ).toString()
           == QLatin1String( "1.0" ) );
    CHECK( embedded.value( QStringLiteral( "tasks" ) ).toArray().size() == 3 );

    // The legacy 12.0 timeline sidecar is NEVER written by the runtime.
    CHECK_FALSE( QFileInfo::exists( missionTimelineSidecarPathForProject( project ) ) );

    // The embedded document is exactly the in-memory timeline.
    MissionContext reloadedContext;
    QString loadErr;
    REQUIRE( loadMissionContextFromSidecar( project, reloadedContext, &loadErr ) );
    MissionTimeline extracted;
    REQUIRE( extractMissionTimeline( reloadedContext, extracted ) );
    CHECK( extracted == state.timeline );
}

TEST_CASE( "mission runtime reopen reproduces the same projection", "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    MissionRuntimeState state = makeRuntime( QStringLiteral( "mission-o1b" ) );
    QDomDocument doc;
    QString err;
    REQUIRE( saveMissionRuntime( project, doc, state, &err ) );
    const QByteArray before = projectionHash( state.timeline );

    MissionRuntimeState reopened;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), reopened, &err ) );
    CHECK( reopened.authorityLoaded );
    CHECK( reopened.timeline == state.timeline );
    CHECK( projectionHash( reopened.timeline ) == before );
}

TEST_CASE( "mission runtime reopen happens in a fresh process", "[mission][persistence]" )
{
#ifndef Q_OS_LINUX
    SKIP( "fresh-process reopen uses /proc/self/exe (Linux); the in-repo suite covers "
          "reopen through the store round-trip on every platform" );
#endif
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    MissionRuntimeState state = makeRuntime( QStringLiteral( "mission-restart" ) );
    QDomDocument doc;
    QString err;
    REQUIRE( saveMissionRuntime( project, doc, state, &err ) );
    const QByteArray expected = projectionHash( state.timeline );

    int argc = 1;
    static char arg0[] = "mission-runtime-gate";
    static char *argv[] = { arg0, nullptr };
    QCoreApplication app( argc, argv );

    QProcess proc;
    proc.setProgram( QStringLiteral( "/proc/self/exe" ) );
    proc.setArguments( { QStringLiteral( "mission runtime fresh-process load helper" ) } );
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert( QStringLiteral( "MISSION_FRESH_PROJECT" ), project );
    env.insert( QStringLiteral( "MISSION_FRESH_EXPECT" ), QString::fromUtf8( expected ) );
    env.insert( QStringLiteral( "QT_QPA_PLATFORM" ), QStringLiteral( "offscreen" ) );
    proc.setProcessEnvironment( env );
    proc.start();
    REQUIRE( proc.waitForFinished( 60000 ) );
    CHECK( proc.exitStatus() == QProcess::NormalExit );
    CHECK( proc.exitCode() == 0 );
    const QByteArray out = proc.readAllStandardOutput();
    CHECK( out.contains( "MISSION_FRESH_HASH " + expected ) );
}

TEST_CASE( "mission runtime fresh-process load helper", "[mission][persistence][helper]" )
{
    const QByteArray project = qgetenv( "MISSION_FRESH_PROJECT" );
    if ( project.isEmpty() )
        SKIP( "helper case: driven by the parent case through a child process" );

    MissionRuntimeState state;
    QString err;
    REQUIRE( loadMissionRuntime( QString::fromUtf8( project ), QDomDocument(), state, &err ) );
    const QByteArray hash = projectionHash( state.timeline );
    std::printf( "MISSION_FRESH_HASH %s\n", hash.constData() );
    CHECK( hash == qgetenv( "MISSION_FRESH_EXPECT" ) );
}

// ── O2: a tampered legacy copy cannot override the authority ─────────────

TEST_CASE( "a tampered legacy sidecar never overrides the authority", "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    MissionRuntimeState state = makeRuntime( QStringLiteral( "mission-o2" ) );
    QDomDocument doc;
    QString err;
    REQUIRE( saveMissionRuntime( project, doc, state, &err ) );

    // Forge a legacy 12.0 timeline sidecar with different content.
    MissionTimeline tampered;
    tampered.setMissionId( QStringLiteral( "mission-forged" ) );
    tampered.addTask( makeTask( QStringLiteral( "ghost-1" ), MissionStage::Publish,
                                QStringLiteral( "Forged task" ), MissionTaskStatus::Succeeded ) );
    QString legacyErr;
    REQUIRE( saveMissionTimelineToSidecar( project, tampered, &legacyErr ) );

    MissionRuntimeState loaded;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), loaded, &err ) );
    // The authority wins: no migration notice, the embedded timeline is loaded.
    CHECK_FALSE( loaded.timelineMigrated );
    CHECK( loaded.notices.isEmpty() );
    CHECK( loaded.timeline == state.timeline );
    CHECK( loaded.timeline.missionId() == QLatin1String( "mission-o2" ) );
    CHECK_FALSE( loaded.timeline.hasTask( QStringLiteral( "ghost-1" ) ) );

    // A subsequent save does not adopt the tampered content either.
    MissionRuntimeState saved = loaded;
    REQUIRE( saveMissionRuntime( project, doc, saved, &err ) );
    const QJsonObject embedded =
        sidecarDoc( project ).value( QStringLiteral( "metadata" ) ).toObject()
            .value( QLatin1String( kMissionTimelineMetadataKey ) ).toObject();
    CHECK( embedded.value( QStringLiteral( "mission_id" ) ).toString()
           == QLatin1String( "mission-o2" ) );
    CHECK( embedded.value( QStringLiteral( "tasks" ) ).toArray().size() == 3 );
}

// ── O3: legacy 12.0 migration round-trip ─────────────────────────────────

TEST_CASE( "a 12.0 project migrates its timeline exactly once", "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    // A 12.0-era project: mission context sidecar WITHOUT an embedded
    // timeline, plus the legacy timeline sidecar.
    MissionContext legacyContext;
    legacyContext.missionId = QStringLiteral( "mission-12" );
    QString err;
    REQUIRE( saveMissionContextToSidecar( project, legacyContext, &err ) );

    MissionTimeline legacy;
    legacy.setMissionId( QStringLiteral( "mission-12" ) );
    legacy.addTask( makeTask( QStringLiteral( "import-1" ), MissionStage::Import,
                              QStringLiteral( "Import scene" ), MissionTaskStatus::Succeeded ) );
    legacy.addTask( makeTask( QStringLiteral( "ana-1" ), MissionStage::Analyze,
                              QStringLiteral( "NDVI" ), MissionTaskStatus::Failed ) );
    REQUIRE( saveMissionTimelineToSidecar( project, legacy, &err ) );

    MissionRuntimeState first;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), first, &err ) );
    CHECK( first.authorityLoaded );
    CHECK( first.timelineMigrated );
    CHECK( first.notices.contains( QStringLiteral( "timeline_migrated_from_legacy_sidecar" ) ) );
    CHECK( missionTimelineMigrationSource( first.context ) == QLatin1String( "legacy-sidecar-12.0" ) );
    // Same task space (the loader syncs project_ref onto the timeline).
    MissionTimeline expected = legacy;
    expected.setProjectRef( project );
    CHECK( first.timeline == expected );

    // Persisting the migration embeds the timeline in the authority.
    QDomDocument doc;
    MissionRuntimeState persisted = first;
    REQUIRE( saveMissionRuntime( project, doc, persisted, &err ) );

    // A second open is idempotent: no second migration, no event growth.
    MissionRuntimeState second;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), second, &err ) );
    CHECK_FALSE( second.timelineMigrated );
    CHECK( second.notices.contains( QStringLiteral( "timeline_migrated_from_legacy_sidecar" ) ) );
    CHECK( second.timeline == expected );
    CHECK( second.timeline.events().size() == legacy.events().size() );
}

TEST_CASE( "a corrupt legacy sidecar does not block opening the project", "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    MissionContext ctx;
    ctx.missionId = QStringLiteral( "mission-o3b" );
    QString err;
    REQUIRE( saveMissionContextToSidecar( project, ctx, &err ) );
    writeAll( missionTimelineSidecarPathForProject( project ),
              QByteArray( "{ this is not json" ) );

    MissionRuntimeState loaded;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), loaded, &err ) );
    CHECK( loaded.authorityLoaded );
    CHECK_FALSE( loaded.timelineMigrated );
    CHECK( loaded.timeline.tasks().isEmpty() );
    bool sawProblem = false;
    for ( const QString &p : loaded.problems )
        sawProblem = sawProblem || p.startsWith( QStringLiteral( "legacy_timeline_unreadable" ) );
    CHECK( sawProblem );
}

// ── O4: fail-closed versioning + poison guard ────────────────────────────

TEST_CASE( "an unknown future timeline version fails closed", "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    // Hand-craft an authority document whose embedded timeline claims a
    // future schema version.
    MissionContext ctx;
    ctx.missionId = QStringLiteral( "mission-future" );
    QJsonObject embedded;
    embedded.insert( QStringLiteral( "kind" ), QLatin1String( "mission_timeline" ) );
    embedded.insert( QStringLiteral( "schema_version" ), QStringLiteral( "2.0" ) );
    embedded.insert( QStringLiteral( "tasks" ), QJsonArray() );
    embedded.insert( QStringLiteral( "events" ), QJsonArray() );
    ctx.metadata.insert( QLatin1String( kMissionTimelineMetadataKey ), embedded );
    QString err;
    REQUIRE( saveMissionContextToSidecar( project, ctx, &err ) );

    MissionRuntimeState loaded;
    CHECK_FALSE( loadMissionRuntime( project, QDomDocument(), loaded, &err ) );
    CHECK( err == QLatin1String( "unsupported_schema_version" ) );
    CHECK( loaded.authorityCorrupt );

    // The poisoned state can never be published over the artifact.
    QDomDocument doc;
    CHECK_FALSE( saveMissionRuntime( project, doc, loaded, &err ) );
    CHECK( err == QLatin1String( "poisoned_authority" ) );
    // The artifact is untouched.
    const QJsonObject after = sidecarDoc( project );
    CHECK( after.value( QStringLiteral( "metadata" ) ).toObject()
               .value( QLatin1String( kMissionTimelineMetadataKey ) ).toObject()
               .value( QStringLiteral( "schema_version" ) ).toString()
           == QLatin1String( "2.0" ) );
}

TEST_CASE( "a corrupt authority recovers from the last known good state", "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    MissionRuntimeState state = makeRuntime( QStringLiteral( "mission-o4" ) );
    QDomDocument doc;
    QString err;
    REQUIRE( saveMissionRuntime( project, doc, state, &err ) );
    CHECK( QFileInfo::exists( missionRuntimeLastGoodPathForProject( project ) ) );

    // Corrupt the authority sidecar (a torn write).
    const QString side = missionSidecarPathForProject( project );
    writeAll( side, QByteArray( "{\"kind\":\"mission_context\",\"schema_vers" ) );

    MissionRuntimeState recovered;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), recovered, &err ) );
    CHECK( recovered.recoveredFromLastGood );
    CHECK( recovered.notices.contains( QStringLiteral( "authority_recovered_from_last_good" ) ) );
    CHECK( recovered.timeline == state.timeline );

    // Saving repairs the authority; a reload no longer needs the snapshot.
    MissionRuntimeState repaired = recovered;
    REQUIRE( saveMissionRuntime( project, doc, repaired, &err ) );
    MissionRuntimeState after;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), after, &err ) );
    CHECK_FALSE( after.recoveredFromLastGood );
    CHECK( after.timeline == state.timeline );
}

TEST_CASE( "a corrupt authority without recovery refuses instead of overwriting",
           "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    MissionRuntimeState state = makeRuntime( QStringLiteral( "mission-o4b" ) );
    QDomDocument doc;
    QString err;
    REQUIRE( saveMissionRuntime( project, doc, state, &err ) );

    // Remove the last-good snapshot, then corrupt the only artifact.
    REQUIRE( QFile::remove( missionRuntimeLastGoodPathForProject( project ) ) );
    const QByteArray torn( "{ \"kind\": \"mission_context\", \"schema_version\": \"1.0\", " );
    writeAll( missionSidecarPathForProject( project ), torn );

    MissionRuntimeState loaded;
    CHECK_FALSE( loadMissionRuntime( project, QDomDocument(), loaded, &err ) );
    CHECK( loaded.authorityCorrupt );

    // The torn artifact survives: nothing was published over it.
    CHECK( readAll( missionSidecarPathForProject( project ) ) == torn );
    QDomDocument doc2;
    CHECK_FALSE( saveMissionRuntime( project, doc2, loaded, &err ) );
    CHECK( readAll( missionSidecarPathForProject( project ) ) == torn );
}

// ── XML channel (.qgz round-trip) ────────────────────────────────────────

TEST_CASE( "the project XML channel carries the embedded timeline", "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    MissionRuntimeState state = makeRuntime( QStringLiteral( "mission-xml" ) );
    QDomDocument doc;
    doc.appendChild( doc.createElement( QStringLiteral( "qgis" ) ) );
    QString err;
    REQUIRE( saveMissionRuntime( project, doc, state, &err ) );

    // The XML element alone carries the same authority document.
    MissionContext fromXml;
    REQUIRE( readMissionContextFromProjectXml( doc, fromXml, &err ) );
    MissionTimeline extracted;
    REQUIRE( extractMissionTimeline( fromXml, extracted ) );
    CHECK( extracted == state.timeline );

    // Loading with NO sidecar (XML-only channel) yields the same timeline.
    MissionRuntimeState loaded;
    REQUIRE( loadMissionRuntime( QString(), doc, loaded, &err ) );
    CHECK( loaded.authorityLoaded );
    CHECK( loaded.timeline == state.timeline );
}

TEST_CASE( "a fresh project opens with an empty task space", "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    MissionRuntimeState loaded;
    QString err;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), loaded, &err ) );
    CHECK_FALSE( loaded.authorityLoaded );
    CHECK( loaded.timeline.tasks().isEmpty() );
    CHECK( loaded.notices.isEmpty() );

    QDomDocument doc;
    MissionRuntimeState saved = loaded;
    saved.timeline.setMissionId( QStringLiteral( "mission-fresh" ) );
    saved.timeline.addTask( makeTask( QStringLiteral( "import-1" ), MissionStage::Import,
                                      QStringLiteral( "Import" ) ) );
    REQUIRE( saveMissionRuntime( project, doc, saved, &err ) );
    CHECK( saved.context.missionId == QLatin1String( "mission-fresh" ) );

    MissionRuntimeState reopened;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), reopened, &err ) );
    CHECK( reopened.authorityLoaded );
    CHECK( reopened.timeline.tasks().size() == 1 );
}

// ── Property: fingerprint stability ──────────────────────────────────────

TEST_CASE( "embedding the timeline does not change the mission fingerprint",
           "[mission][persistence]" )
{
    MissionRuntimeState state = makeRuntime( QStringLiteral( "mission-fp" ) );
    const QString before = missionContentFingerprint( state.context );
    embedMissionTimeline( state.context, state.timeline );
    CHECK( missionContentFingerprint( state.context ) == before );
}

// ── O8: no fake Running after crash/reopen ───────────────────────────────

TEST_CASE( "run authority reconciliation never reports a fake Running",
           "[mission][persistence]" )
{
    MissionTimeline tl;
    tl.setMissionId( QStringLiteral( "mission-run" ) );
    for ( const QString &id : { QStringLiteral( "alive" ), QStringLiteral( "done" ),
                                QStringLiteral( "failed" ), QStringLiteral( "canceled" ),
                                QStringLiteral( "gone" ), QStringLiteral( "unbound" ) } )
    {
        MissionTask t = makeTask( id, MissionStage::Analyze, id, MissionTaskStatus::Pending );
        tl.addTask( t );
        MissionRunRef run;
        run.kind = QStringLiteral( "task_center" );
        run.id = id;
        tl.bindRunReference( id, run );
        tl.transition( id, MissionTaskStatus::Running, QStringLiteral( "2026-09-21T00:00:00Z" ) );
    }
    // Crash residue: a task that went Running and then lost its binding.
    tl.bindRunReference( QStringLiteral( "unbound" ), MissionRunRef{} );
    CHECK( tl.task( QStringLiteral( "unbound" ) )->status == MissionTaskStatus::Running );
    CHECK( tl.task( QStringLiteral( "unbound" ) )->run.isNull() );

    const auto resolver = []( const MissionRunRef &ref ) -> MissionRunStatus {
        MissionRunStatus s;
        if ( ref.id == QLatin1String( "alive" ) )
            s.liveness = MissionRunLiveness::Alive;
        else if ( ref.id == QLatin1String( "done" ) )
            s.liveness = MissionRunLiveness::TerminalSuccess;
        else if ( ref.id == QLatin1String( "failed" ) )
            s.liveness = MissionRunLiveness::TerminalFailure;
        else if ( ref.id == QLatin1String( "canceled" ) )
            s.liveness = MissionRunLiveness::Canceled;
        else
            s.liveness = MissionRunLiveness::Unknown; // "gone": the run is not there
        return s;
    };

    const MissionRunReconciliation report =
        reconcileRunAuthority( tl, resolver, QStringLiteral( "2026-09-21T01:00:00Z" ) );

    CHECK( report.leftRunning == 1 );
    CHECK( report.succeededFromRun == 1 );
    CHECK( report.failedFromRun == 1 );
    CHECK( report.canceledFromRun == 1 );
    CHECK( report.staleFromRun == 2 ); // "gone" (unresolvable) + "unbound" (no run)
    CHECK( tl.task( QStringLiteral( "alive" ) )->status == MissionTaskStatus::Running );
    CHECK( tl.task( QStringLiteral( "done" ) )->status == MissionTaskStatus::Succeeded );
    CHECK( tl.task( QStringLiteral( "failed" ) )->status == MissionTaskStatus::Failed );
    CHECK( tl.task( QStringLiteral( "canceled" ) )->status == MissionTaskStatus::Canceled );
    CHECK( tl.task( QStringLiteral( "gone" ) )->status == MissionTaskStatus::Stale );
    CHECK( tl.task( QStringLiteral( "unbound" ) )->status == MissionTaskStatus::Stale );

    // The reconciliation is audited in the event log.
    const QVector<MissionEvent> events = tl.events();
    int reconciledNotes = 0;
    for ( const MissionEvent &ev : events )
        reconciledNotes += ev.note.startsWith( QStringLiteral( "run_authority_reconciled:" ) ) ? 1 : 0;
    CHECK( reconciledNotes == 5 );

    // Without any resolver at all, nothing may stay Running.
    MissionTimeline unverified;
    unverified.addTask( makeTask( QStringLiteral( "t1" ), MissionStage::Analyze, QStringLiteral( "t1" ) ) );
    MissionRunRef run;
    run.kind = QStringLiteral( "task_center" );
    run.id = QStringLiteral( "9" );
    unverified.bindRunReference( QStringLiteral( "t1" ), run );
    unverified.transition( QStringLiteral( "t1" ), MissionTaskStatus::Running,
                           QStringLiteral( "2026-09-21T00:00:00Z" ) );
    const MissionRunReconciliation strict =
        reconcileRunAuthority( unverified, nullptr, QStringLiteral( "2026-09-21T02:00:00Z" ) );
    CHECK( strict.staleFromRun == 1 );
    CHECK( strict.leftRunning == 0 );
    CHECK( unverified.task( QStringLiteral( "t1" ) )->status == MissionTaskStatus::Stale );
}

TEST_CASE( "a reopened project reconciles its run authority before any surface reads it",
           "[mission][persistence]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString project = dir.filePath( uniqueStem() + QStringLiteral( ".qgz" ) );

    // A session that crashed with a task still marked Running.
    MissionRuntimeState state = makeRuntime( QStringLiteral( "mission-crash" ) );
    QDomDocument doc;
    QString err;
    REQUIRE( saveMissionRuntime( project, doc, state, &err ) );

    // Reopen: TaskCenter no longer knows run 4242 (the process died).
    MissionRuntimeState reopened;
    REQUIRE( loadMissionRuntime( project, QDomDocument(), reopened, &err ) );
    const MissionRunReconciliation report = reconcileRunAuthority(
        reopened.timeline, nullptr, QStringLiteral( "2026-09-21T03:00:00Z" ) );
    CHECK( report.staleFromRun == 1 );
    CHECK( reopened.timeline.task( QStringLiteral( "ana-1" ) )->status
           == MissionTaskStatus::Stale );

    // No surface may report it as Running afterwards.
    const QJsonObject projection = missionTimelineProjectionJson( reopened.timeline, 32, 0 );
    const QJsonArray tasks = projection.value( QStringLiteral( "tasks" ) ).toArray();
    for ( const QJsonValue &v : tasks )
    {
        if ( v.toObject().value( QStringLiteral( "id" ) ).toString() == QLatin1String( "ana-1" ) )
            CHECK( v.toObject().value( QStringLiteral( "status" ) ).toString()
                   == QLatin1String( "stale" ) );
    }
}

// ── O6: layer delete/rename recovery through the runtime ─────────────────

TEST_CASE( "deleting a referenced layer marks dependent tasks stale", "[mission][persistence]" )
{
    MissionTimeline tl;
    tl.setMissionId( QStringLiteral( "mission-refs" ) );
    tl.addTask( makeTask( QStringLiteral( "t1" ), MissionStage::Analyze, QStringLiteral( "t1" ),
                          MissionTaskStatus::Succeeded, { QStringLiteral( "layer-a" ) },
                          { QStringLiteral( "artifact-1" ) } ) );
    tl.addTask( makeTask( QStringLiteral( "t2" ), MissionStage::Analyze, QStringLiteral( "t2" ),
                          MissionTaskStatus::Succeeded, { QStringLiteral( "layer-b" ) } ) );

    const MissionRefResolver resolver = []( const QString &refId ) -> MissionRefStatus {
        MissionRefStatus s;
        if ( refId == QLatin1String( "layer-a" ) || refId == QLatin1String( "artifact-1" ) )
        {
            s.alive = false;
            s.reason = QStringLiteral( "deleted_layer" );
        }
        return s;
    };

    const MissionReconciliation rec = reconcileMission( tl, resolver );
    CHECK( rec.danglingRefIds.contains( QStringLiteral( "layer-a" ) ) );
    CHECK( rec.staleTaskIds == QStringList { QStringLiteral( "t1" ) } );

    const int moved = applyReconciliation( tl, rec, QStringLiteral( "2026-09-21T04:00:00Z" ) );
    CHECK( moved == 1 );
    CHECK( tl.task( QStringLiteral( "t1" ) )->status == MissionTaskStatus::Stale );
    CHECK( tl.task( QStringLiteral( "t2" ) )->status == MissionTaskStatus::Succeeded );

    // A rename is not a dangling reference: status, attempts and run binding
    // survive (ids are stable across renames).
    const int rewritten = applyRenames(
        tl, { { QStringLiteral( "layer-b" ), QStringLiteral( "layer-b-renamed" ) } },
        QStringLiteral( "2026-09-21T05:00:00Z" ) );
    CHECK( rewritten == 1 );
    const MissionTask *after = tl.task( QStringLiteral( "t2" ) );
    CHECK( after->status == MissionTaskStatus::Succeeded );
    CHECK( after->inputRefIds == QStringList { QStringLiteral( "layer-b-renamed" ) } );
}

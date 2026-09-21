// tests/test_workflow_durability_13.cpp — Engine-2.0 durability lane (Track 13)
//
// Covers the lineage envelope (attempt / resumeOf), the content-aware
// checkpoint election, legacy-checkpoint migration and the runId grammar
// fuzz. Light lane: the checkpoint/run/lock sources are compiled directly
// (no qgis/task_center chain), mirroring the D17 test-target pattern.
#include <catch2/catch_test_macros.hpp>

#include "workflow/workflow_checkpoint.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_lock.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <json/json.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace sicnu::workflow;

namespace {

WorkflowDefinition testDefinition( const std::string &id )
{
    WorkflowDefinition def;
    def.id = id;
    def.title = id;
    StepDef step;
    step.id = "s1";
    step.operatorId = "rs:test";
    def.steps.push_back( step );
    return def;
}

/// A Running run with @p runId, persisted into @p dir.
QString saveRunningRun( const QString &dir, const std::string &runId, int attempt = 1,
                        const std::string &resumeOf = {} )
{
    auto run = WorkflowRun::createFromDefinition( testDefinition( "wf-" + runId ), runId );
    if ( !run )
        return {};
    run->transitionTo( WorkflowRunState::Planning );
    run->transitionTo( WorkflowRunState::Ready );
    run->transitionTo( WorkflowRunState::Running );
    run->setAttempt( attempt );
    if ( !resumeOf.empty() )
        run->setResumeOf( resumeOf );
    return WorkflowCheckpointManager().saveCheckpoint( *run, dir );
}

/// Serialize @p root back to @p path (test-side checkpoint crafting).
void writeJson( const QString &path, const Json::Value &root )
{
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "  ";
    const std::string out = Json::writeString( writer, root );
    QFile outFile( path );
    REQUIRE( outFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) );
    REQUIRE( outFile.write( out.data(), static_cast<qint64>( out.size() ) )
             == static_cast<qint64>( out.size() ) );
    outFile.close();
}

/// Rewrite top-level envelope fields of a saved checkpoint in place (the
/// filename — and therefore the on-disk name the election sees — is kept).
void patchCheckpoint( const QString &path, const std::map<std::string, Json::Value> &fields )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly | QIODevice::Text ) );
    const QByteArray raw = file.readAll();
    file.close();
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    std::istringstream stream( raw.toStdString() );
    REQUIRE( Json::parseFromStream( builder, stream, &root, &errs ) );
    for ( const auto &[key, value] : fields )
        root[key] = value;
    writeJson( path, root );
}

/// Drop top-level members from a saved checkpoint (legacy-payload crafting).
void removeMembers( const QString &path, const std::vector<std::string> &members )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly | QIODevice::Text ) );
    const QByteArray raw = file.readAll();
    file.close();
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    std::istringstream stream( raw.toStdString() );
    REQUIRE( Json::parseFromStream( builder, stream, &root, &errs ) );
    for ( const std::string &member : members )
        root.removeMember( member );
    writeJson( path, root );
}

/// Make @p path provably newer/older than the rest (explicit mtime: write
/// order is not deterministic at filesystem timestamp granularity).
void setMtime( const QString &path, const QDateTime &when )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadWrite ) );
    REQUIRE( file.setFileTime( when, QFileDevice::FileModificationTime ) );
    file.close();
}

QStringList checkpointEntries( const QString &dir )
{
    return QDir( dir ).entryList( QStringList{ QStringLiteral( "checkpoint_*.json" ) }, QDir::Files );
}

int orphanedEntries( const QString &dir )
{
    return QDir( dir ).entryList( QStringList{ QStringLiteral( "*.orphaned" ) }, QDir::Files ).size();
}

} // namespace

TEST_CASE( "Lineage envelope round-trips; legacy version-1 checkpoints still load",
           "[workflow][durability][envelope]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    auto run = WorkflowRun::createFromDefinition( testDefinition( "wf-env" ), "run-env" );
    run->transitionTo( WorkflowRunState::Running );
    const QString path = WorkflowCheckpointManager().saveCheckpoint( *run, dir.path() );
    REQUIRE( !path.isEmpty() );

    QString err;
    auto loaded = WorkflowCheckpointManager().loadCheckpoint( path, &err );
    REQUIRE( loaded != nullptr );
    REQUIRE( loaded->attempt() == 1 );
    REQUIRE( loaded->resumeOf().empty() );

    // Attempt lineage survives the round trip and is visible in the payload.
    auto resumed = WorkflowRun::createFromDefinition( testDefinition( "wf-env" ), "run-env" );
    resumed->setAttempt( 3 );
    resumed->setResumeOf( "run-env" );
    const QString ghostPath = WorkflowCheckpointManager().saveCheckpoint( *resumed, dir.path() );
    REQUIRE( !ghostPath.isEmpty() );
    auto ghostLoaded = WorkflowCheckpointManager().loadCheckpoint( ghostPath, &err );
    REQUIRE( ghostLoaded != nullptr );
    REQUIRE( ghostLoaded->attempt() == 3 );
    REQUIRE( ghostLoaded->resumeOf() == "run-env" );

    // Legacy version-1 payload (no lineage fields) still loads, with the
    // documented migration semantics: attempt=1, no resumeOf. The resumed
    // run shares the runId, so it rewrote the SAME file — demote it to a
    // genuine version-1 document.
    patchCheckpoint( path, { { "version", Json::Value( 1 ) } } );
    removeMembers( path, { "attempt", "resumeOf" } );
    auto legacyLoaded = WorkflowCheckpointManager().loadCheckpoint( path, &err );
    REQUIRE( legacyLoaded != nullptr );
    REQUIRE( legacyLoaded->attempt() == 1 );
    REQUIRE( legacyLoaded->resumeOf().empty() );

    // A future version is refused by name — fail closed, never reinterpreted.
    patchCheckpoint( path, { { "version", Json::Value( kWorkflowRunSerializationVersion + 1 ) } } );
    auto future = WorkflowCheckpointManager().loadCheckpoint( path, &err );
    REQUIRE( future == nullptr );
    REQUIRE( err.contains( QStringLiteral( "version" ) ) );
}

TEST_CASE( "A corrupt lineage envelope is refused, never clamped into service",
           "[workflow][durability][envelope]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString base = saveRunningRun( dir.path(), "run-env-bad" );
    REQUIRE( !base.isEmpty() );

    struct Case
    {
        const char *name;
        std::map<std::string, Json::Value> fields;
    };
    const std::vector<Case> cases = {
        { "zero attempt", { { "attempt", Json::Value( 0 ) } } },
        { "negative attempt", { { "attempt", Json::Value( -3 ) } } },
        { "absurd attempt", { { "attempt", Json::Value( kMaxRunAttempt + 1 ) } } },
        { "string attempt", { { "attempt", Json::Value( "2" ) } } },
        { "fractional attempt", { { "attempt", Json::Value( 2.5 ) } } },
        { "traversal resumeOf", { { "resumeOf", Json::Value( "../evil" ) } } },
        { "unicode resumeOf", { { "resumeOf", Json::Value( "r\u00f6m" ) } } },
        { "empty-string attempt member", { { "attempt", Json::Value( "" ) } } },
    };
    for ( const Case &c : cases )
    {
        patchCheckpoint( base, c.fields );
        QString err;
        auto loaded = WorkflowCheckpointManager().loadCheckpoint( base, &err );
        INFO( c.name );
        REQUIRE( loaded == nullptr );
        REQUIRE( !err.isEmpty() );
    }
}

TEST_CASE( "A user-named *_resume run is not grouped with an unrelated run",
           "[workflow][durability][election]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Two legitimate, independently named runs. The filename-suffix rule of
    // the legacy election grouped "foo_resume" under "foo" and quarantined
    // one of them — a valid run's checkpoint destroyed by another run's
    // election. With the lineage envelope both are standalone.
    REQUIRE( !saveRunningRun( dir.path(), "foo" ).isEmpty() );
    REQUIRE( !saveRunningRun( dir.path(), "foo_resume" ).isEmpty() );
    REQUIRE( checkpointEntries( dir.path() ).size() == 2 );

    const int quarantined = WorkflowCheckpointManager().electCheckpoints( dir.path() );
    REQUIRE( quarantined == 0 );
    REQUIRE( checkpointEntries( dir.path() ).size() == 2 );
    REQUIRE( orphanedEntries( dir.path() ) == 0 );

    // Both remain recoverable — neither run was silently dropped.
    const auto recovered = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    REQUIRE( recovered.size() == 2 );
}

TEST_CASE( "A crash-leftover resume ghost elects against its original",
           "[workflow][durability][election]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Crash window: the original is Running on disk and the temporary resume
    // submission (a GENERATED runId, so no filename relation at all) is newer.
    const QString original = saveRunningRun( dir.path(), "run-base", /*attempt=*/2 );
    const QString ghost = saveRunningRun( dir.path(), "run-1750000000000-1-deadbeef",
                                          /*attempt=*/1, /*resumeOf=*/"run-base" );
    REQUIRE( !original.isEmpty() );
    REQUIRE( !ghost.isEmpty() );
    const QDateTime now = QDateTime::currentDateTimeUtc();
    setMtime( original, now.addSecs( -60 ) ); // original written first
    setMtime( ghost, now );                   // ghost is the NEWER file

    const int quarantined = WorkflowCheckpointManager().electCheckpoints( dir.path() );
    REQUIRE( quarantined == 1 );
    // The COMPLETE lineage survives: the ghost is derivable from the original
    // (which also carries every earlier pass's completed plans), so recency
    // does not get to promote the derivative over it.
    REQUIRE( QFile::exists( original ) );
    REQUIRE_FALSE( QFile::exists( ghost ) );
    REQUIRE( QFile::exists( ghost + QStringLiteral( ".orphaned" ) ) );
    REQUIRE( orphanedEntries( dir.path() ) == 1 );

    // Recovery resumes the original exactly once — no double execution.
    const auto recovered = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    REQUIRE( recovered.size() == 1 );
    REQUIRE( recovered.front()->runId() == "run-base" );
    REQUIRE( recovered.front()->state() == WorkflowRunState::Interrupted );
    REQUIRE( recovered.front()->attempt() == 2 );
}

TEST_CASE( "A finalized resume ghost is inert and never elected against its original",
           "[workflow][durability][election]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // The NORMAL resume swap leaves the ghost's terminal (Canceled)
    // checkpoint behind after deleting the file mid-lock. It cannot be
    // resurrected, so it must not drag the original into an election.
    const QString original = saveRunningRun( dir.path(), "run-live", /*attempt=*/2 );
    REQUIRE( !original.isEmpty() );
    auto ghost = WorkflowRun::createFromDefinition( testDefinition( "wf-live" ),
                                                   "run-1750000000001-2-cafe" );
    ghost->transitionTo( WorkflowRunState::Planning );
    ghost->transitionTo( WorkflowRunState::Ready );
    ghost->transitionTo( WorkflowRunState::Running );
    ghost->setResumeOf( "run-live" );
    const QString ghostPath = WorkflowCheckpointManager().saveCheckpoint( *ghost, dir.path() );
    REQUIRE( !ghostPath.isEmpty() );
    patchCheckpoint( ghostPath, { { "state", Json::Value( "Canceled" ) } } );
    const QDateTime now = QDateTime::currentDateTimeUtc();
    setMtime( original, now.addSecs( -60 ) );
    setMtime( ghostPath, now );

    REQUIRE( WorkflowCheckpointManager().electCheckpoints( dir.path() ) == 0 );
    REQUIRE( QFile::exists( original ) );
    REQUIRE( QFile::exists( ghostPath ) );

    // The original is still the one recovery resumes; the terminal ghost is
    // skipped, not resurrected.
    const auto recovered = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    REQUIRE( recovered.size() == 1 );
    REQUIRE( recovered.front()->runId() == "run-live" );
}

TEST_CASE( "Legacy version-1 _resume ghosts still elect by the filename rule",
           "[workflow][durability][election][legacy]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Migration: a pre-envelope ghost carries no declared lineage, so the
    // historical `<runId>_resume` suffix remains its only evidence.
    const QString original = saveRunningRun( dir.path(), "run-legacy" );
    const QString ghost = saveRunningRun( dir.path(), "run-legacy_resume" );
    REQUIRE( !original.isEmpty() );
    REQUIRE( !ghost.isEmpty() );
    for ( const QString &path : { original, ghost } )
        patchCheckpoint( path, { { "version", Json::Value( 1 ) } } );
    const QDateTime now = QDateTime::currentDateTimeUtc();
    setMtime( original, now.addSecs( -60 ) );
    setMtime( ghost, now );

    REQUIRE( WorkflowCheckpointManager().electCheckpoints( dir.path() ) == 1 );
    REQUIRE( QFile::exists( ghost ) );
    REQUIRE_FALSE( QFile::exists( original ) );
    REQUIRE( QFile::exists( original + QStringLiteral( ".orphaned" ) ) );
}

TEST_CASE( "RunId grammar fuzz: suffixes, unicode, traversal, duplicates",
           "[workflow][durability][fuzz]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Filename-unsafe ids are refused by saveCheckpoint (the id is embedded
    // in the path) — no file is written, nothing is silently renamed.
    const std::vector<std::string> unsafe = {
        "",                                     // empty
        ".",                                    // dot
        "..",                                   // traversal
        ".hidden",                              // leading dot
        "../evil",                              // traversal
        "a/b",                                  // path separator
        "a\\b",                                 // windows separator
        "r\u00f6m",                             // non-ASCII
        "run with space",
        "run\x01ctl",
        std::string( 129, 'a' ),                // over the 128-char bound
        std::string( 200, 'x' ),
    };
    for ( const std::string &id : unsafe )
    {
        auto run = WorkflowRun::createFromDefinition( testDefinition( "wf-fuzz" ), id );
        if ( id.empty() )
        {
            // createFromDefinition treats the empty id as "generate one".
            REQUIRE( run != nullptr );
            continue;
        }
        // An unsafe id is refused at construction: no checkpoint file whose
        // name embeds it is ever written.
        INFO( id );
        REQUIRE( run == nullptr );
    }

    // Weird-but-legal ids, including every suffix shape that collides with
    // the resume grammar, plus a 128-char maximum-length id.
    const std::vector<std::string> legal = {
        "foo",          "foo_resume",     "_resume",          "resume_",
        "foo__resume",  "foo_resume_resume", "foo.resume",    "foo-resume",
        "a.b-c_d",      "RUN-1",          "x",
        std::string( 128, 'z' ),
    };
    for ( const std::string &id : legal )
        REQUIRE( !saveRunningRun( dir.path(), id ).isEmpty() );
    REQUIRE( checkpointEntries( dir.path() ).size() == static_cast<int>( legal.size() ) );

    // None of them is grouped with another: the election is a no-op and
    // every run stays recoverable.
    REQUIRE( WorkflowCheckpointManager().electCheckpoints( dir.path() ) == 0 );
    REQUIRE( checkpointEntries( dir.path() ).size() == static_cast<int>( legal.size() ) );
    const auto recovered = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    REQUIRE( recovered.size() == legal.size() );

    // Duplicate runIds collapse onto one file (the identity IS the filename):
    // a second save overwrites, never forks a second run.
    REQUIRE( !saveRunningRun( dir.path(), "dup" ).isEmpty() );
    REQUIRE( !saveRunningRun( dir.path(), "dup" ).isEmpty() );
    REQUIRE( checkpointEntries( dir.path() ).size() == static_cast<int>( legal.size() ) + 1 );
    REQUIRE( WorkflowCheckpointManager().electCheckpoints( dir.path() ) == 0 );
}

TEST_CASE( "Recovery after a process restart is idempotent and skips corrupt files",
           "[workflow][durability][recovery]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    REQUIRE( !saveRunningRun( dir.path(), "run-restart-a" ).isEmpty() );
    // A completed run is terminal: recovery must not resurrect it.
    {
        auto done = WorkflowRun::createFromDefinition( testDefinition( "wf-done" ), "run-done" );
        done->transitionTo( WorkflowRunState::Planning );
        done->transitionTo( WorkflowRunState::Ready );
        done->transitionTo( WorkflowRunState::Running );
        done->transitionTo( WorkflowRunState::Completed );
        REQUIRE( !WorkflowCheckpointManager().saveCheckpoint( *done, dir.path() ).isEmpty() );
    }
    // A corrupt checkpoint is skipped without blocking the others and is
    // left in place for forensics.
    const QString corrupt = QDir( dir.path() ).filePath( QStringLiteral( "checkpoint_corrupt.json" ) );
    {
        QFile file( corrupt );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text ) );
        REQUIRE( file.write( "{ not json at all" ) > 0 );
    }
    // Crash residue from an interrupted atomic save: swept at recovery start.
    const QString residue =
        QDir( dir.path() ).filePath( QStringLiteral( "checkpoint_run-restart-a.json.tmp.999.1" ) );
    {
        QFile file( residue );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text ) );
        REQUIRE( file.write( "half-written" ) > 0 );
    }

    // First pass ("process 1").
    const auto first = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    REQUIRE( first.size() == 1 );
    REQUIRE( first.front()->runId() == "run-restart-a" );
    REQUIRE( first.front()->state() == WorkflowRunState::Interrupted );
    REQUIRE( QFile::exists( corrupt ) );     // skipped, not destroyed
    REQUIRE_FALSE( QFile::exists( residue ) ); // swept

    // Second pass ("process 2", fresh manager): the reconciled run is now
    // Interrupted (terminal) and must NOT be recovered a second time.
    const auto second = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    REQUIRE( second.empty() );
}

TEST_CASE( "A corrupt provenance record never affects checkpoint recovery",
           "[workflow][durability][recovery]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    REQUIRE( !saveRunningRun( dir.path(), "run-provenance-noise" ).isEmpty() );

    // Provenance is audit output; a planted corrupt record next to the
    // checkpoints must not disturb the recovery path (which never reads it).
    const QString noise = QDir( dir.path() ).filePath( QStringLiteral( "provenance_run-x.json" ) );
    {
        QFile file( noise );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text ) );
        REQUIRE( file.write( "\"kind\": \"d17_provenance\", \"edges\": [ dangling" ) > 0 );
    }
    const auto recovered = WorkflowCheckpointManager().recoverInterruptedRuns( dir.path() );
    REQUIRE( recovered.size() == 1 );
    REQUIRE( recovered.front()->runId() == "run-provenance-noise" );
    REQUIRE( QFile::exists( noise ) );
}

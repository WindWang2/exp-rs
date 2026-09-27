// tests/test_runloop_recovery_r4.cpp — Track 8 R4 WP-E: crash-recovery
// pressure on the harness session checkpoint, in the fault-injection
// tradition of test_io_atomic_failures.cpp.
//
// Premise note (BASELINE.md §8): the measured crash-safe persistence seam
// (#1325 lineage) is HarnessSessionStore (context_checkpoint.{h,cpp}) — the
// planner-pipeline stage cursor, decisions, and fact identities — plus the
// agent_loop SessionJournal (own suite) and the engine checkpoints
// (src/workflow, neighboring track). This suite injects the failures the
// atomic-write contract claims to survive and asserts the recovery contract:
//
//   * a hostile or torn file in the store directory is a typed refusal,
//     never a crash and never a half-restored state;
//   * an unwritable store fails loudly and leaves the previous checkpoint
//     intact (atomic publication);
//   * the store stays bounded (count and document size);
//   * resume rewinds to grounding when the facts moved on, while decisions
//     and attempt history survive — no step replays on stale facts.

#include <catch2/catch_test_macros.hpp>

#include "agent/harness/context_checkpoint.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <json/writer.h>

#include <string>
#include <thread>
#include <vector>

using namespace sicnu::agent::harness;

namespace {

/// A valid state at an arbitrary pipeline stage.
HarnessSessionState stateAt( const std::string &id, const std::string &stage )
{
    HarnessSessionState state;
    state.sessionId = id;
    state.stageCursor = stage;
    state.goal = "flood extent after the storm";
    state.intent = "sar_water";
    return state;
}

/// Plants a raw file inside the store directory under a session's name —
/// simulating a torn write from a hostile/foreign producer.
QString plantRawFile( const QDir &dir, const std::string &id, const QByteArray &bytes )
{
    const QString path = dir.filePath(
        QStringLiteral( "harness_session_%1.json" ).arg( QString::fromStdString( id ) ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( bytes );
    file.close();
    return path;
}

/// RAII store-directory override (tests must never touch the real profile).
struct StoreDir
{
    QTemporaryDir dir;
    StoreDir() { HarnessSessionStore::instance().setDirectory( dir.path() ); }
    ~StoreDir() { HarnessSessionStore::instance().setDirectory( QString() ); }
    QDir qdir() const { return QDir( dir.path() ); }
};

} // namespace

// — injection 1/2: torn and garbage documents load as typed refusals ——————

TEST_CASE( "a torn or garbage session document is refused with a typed error, "
           "never half-restored", "[harness][recovery-r4][corrupt]" )
{
    StoreDir store;
    const std::string id = "torn-doc";

    // Torn write: a valid prefix, then the bytes stop mid-document (the
    // failure mode QSaveFile exists to prevent — planted here to prove the
    // loader still refuses it if it ever escapes).
    plantRawFile( store.qdir(), id,
                  QByteArray( "{\"kind\":\"harness_session\",\"schema_version\":\"1.0"
                              ",\"session_id\":\"torn-doc\"" ) );
    {
        HarnessError error;
        const auto state = HarnessSessionStore::instance().loadSession( id, error );
        CHECK_FALSE( state.has_value() );
        CHECK( error.code == error_codes::kInvalidPlan );
        CHECK( error.summary.find( "corrupt" ) != std::string::npos );
    }

    // Pure garbage.
    plantRawFile( store.qdir(), id, QByteArray( "\x00\x01\xff not json at all" ) );
    {
        HarnessError error;
        const auto state = HarnessSessionStore::instance().loadSession( id, error );
        CHECK_FALSE( state.has_value() );
        CHECK( error.code == error_codes::kInvalidPlan );
    }

    // A refusal leaves the file in place (the operator may inspect it);
    // deletion is the explicit next step and works on hostile files too.
    CHECK( HarnessSessionStore::instance().deleteSession( id ) );
    HarnessError error;
    CHECK_FALSE( HarnessSessionStore::instance().loadSession( id, error ).has_value() );
    CHECK( error.code == error_codes::kWorkflowNotFound );
}

// — injection 3/4/5: structurally lying documents ————————————————————————

TEST_CASE( "foreign-kind, foreign-version and unknown-stage documents fail "
           "closed", "[harness][recovery-r4][foreign]" )
{
    StoreDir store;
    auto &instance = HarnessSessionStore::instance();

    // Valid JSON, wrong kind: some other app's state file in our directory.
    plantRawFile( store.qdir(), "foreign-kind",
                  QByteArray( "{\"kind\":\"vscode_workspace\",\"session_id\":\"foreign-kind\"}" ) );
    HarnessError error;
    CHECK_FALSE( instance.loadSession( "foreign-kind", error ).has_value() );
    CHECK( error.summary.find( "kind" ) != std::string::npos );

    // Right kind, unsupported schema version: refuse, never best-effort.
    plantRawFile(
        store.qdir(), "foreign-version",
        QByteArray( "{\"kind\":\"harness_session\",\"schema_version\":\"9.9\","
                    "\"session_id\":\"foreign-version\",\"stage_cursor\":\"intent\"}" ) );
    CHECK_FALSE( instance.loadSession( "foreign-version", error ).has_value() );
    CHECK( error.summary.find( "schema_version" ) != std::string::npos );

    // Unknown stage cursor: the closed stage vocabulary is load-bearing for
    // resume — an unknown cursor must never come back as a valid state.
    plantRawFile(
        store.qdir(), "bad-stage",
        QByteArray( "{\"kind\":\"harness_session\",\"schema_version\":\"1.0\","
                    "\"session_id\":\"bad-stage\",\"stage_cursor\":\"phase eleven\"}" ) );
    CHECK_FALSE( instance.loadSession( "bad-stage", error ).has_value() );
    CHECK( error.summary.find( "stage_cursor" ) != std::string::npos );

    // The recovery contract holds on the resume path too.
    CHECK_FALSE( instance.resumeSession( "foreign-version", error ).has_value() );
}

// — injection 6/7: oversized documents are bounded, not buffered ——————————

TEST_CASE( "a foreign oversized document is refused without buffering and a "
           "save that cannot shrink fails typed without writing",
           "[harness][recovery-r4][bounds]" )
{
    StoreDir store;
    auto &instance = HarnessSessionStore::instance();

    // 100 KiB of valid JSON: past kMaxDocumentBytes. The loader must reject
    // on SIZE before parsing (B-17) — a hostile file cannot force a buffer.
    const std::string huge =
        "{\"kind\":\"harness_session\",\"pad\":\"" + std::string( 100 * 1024, 'x' ) + "\"}";
    plantRawFile( store.qdir(), "oversize-foreign", QByteArray( huge.c_str(), int( huge.size() ) ) );
    HarnessError error;
    CHECK_FALSE( instance.loadSession( "oversize-foreign", error ).has_value() );
    CHECK( error.summary.find( "64 KiB" ) != std::string::npos );
    // listSessions skips it instead of choking.
    bool skipped = true;
    for ( const Json::Value &entry : instance.listSessions() )
        if ( entry["session_id"].asString() == "oversize-foreign" )
            skipped = false;
    CHECK( skipped );

    // A save whose payload cannot compact under the bound (the IR is kept
    // whole by the documented compaction projection) fails typed and writes
    // NOTHING — no partial checkpoint at the caller's path.
    HarnessSessionState state = stateAt( "oversize-save", "ir" );
    state.ir = Json::Value( Json::objectValue );
    state.ir["matrix"] = std::string( 100 * 1024, 'y' ); // incompressible bulk
    const QString path = instance.saveSession( state, error );
    CHECK( path.isEmpty() );
    CHECK( error.code == error_codes::kInvalidParameter );
    CHECK_FALSE( QFile::exists( store.qdir().filePath(
        QStringLiteral( "harness_session_oversize-save.json" ) ) ) );
}

// — injection 8: unwritable store fails loudly, previous checkpoint intact —

TEST_CASE( "an unwritable store fails typed and the previous checkpoint "
           "survives byte-identical", "[harness][recovery-r4][io]" )
{
    StoreDir store;
    auto &instance = HarnessSessionStore::instance();

    // First checkpoint publishes fine.
    HarnessError error;
    const HarnessSessionState original = stateAt( "durable", "analysis" );
    REQUIRE_FALSE( instance.saveSession( original, error ).isEmpty() );
    const QString path = store.qdir().filePath(
        QStringLiteral( "harness_session_durable.json" ) );
    REQUIRE( QFile::exists( path ) );
    QFile before( path );
    REQUIRE( before.open( QIODevice::ReadOnly ) );
    const QByteArray bytesBefore = before.readAll();
    before.close();

    // Make the store read-only: the NEXT save must fail typed and must not
    // touch the published checkpoint (no torn overwrite, no deletion).
    QFile::setPermissions( store.dir.path(),
                           QFile::Permissions( QFile::ReadOwner | QFile::ExeOwner ) );
    const HarnessSessionState updated = stateAt( "durable", "verifying" );
    const QString failed = instance.saveSession( updated, error );
    QFile::setPermissions( store.dir.path(),
                           QFile::Permissions( QFile::ReadOwner | QFile::WriteOwner |
                                               QFile::ExeOwner ) );
    CHECK( failed.isEmpty() );
    CHECK( error.code == error_codes::kExecutionFailed );

    QFile after( path );
    REQUIRE( after.open( QIODevice::ReadOnly ) );
    CHECK( after.readAll() == bytesBefore );
    // And the OLD checkpoint still loads — recovery never regresses.
    const auto restored = instance.loadSession( "durable", error );
    REQUIRE( restored.has_value() );
    CHECK( restored->stageCursor == "analysis" );
}

// — injection 9: hostile session ids cannot escape the store ——————————————

TEST_CASE( "hostile session ids are refused on save/load/delete and no file "
           "escapes the store directory", "[harness][recovery-r4][ids]" )
{
    StoreDir store;
    auto &instance = HarnessSessionStore::instance();

    // ".." is charset-legal but cannot escape: the id is interpolated
    // BETWEEN the prefix and ".json", so it stays a single filename inside
    // the store — pinned here as the documented safe case.
    for ( const char *hostile : { "../../escaped", "a/b", "c:\\windows",
                                  "semi;colon", "space id", "" } )
    {
        INFO( "id: " << hostile );
        HarnessSessionState state = stateAt( hostile, "intent" );
        HarnessError error;
        CHECK( instance.saveSession( state, error ).isEmpty() );
        CHECK( error.code == error_codes::kInvalidParameter );
        CHECK_FALSE( instance.loadSession( hostile, error ).has_value() );
        CHECK_FALSE( instance.deleteSession( hostile ) );
    }

    // A 129-character id is over the bound; 128 is not.
    const std::string longId( 129, 'a' );
    HarnessError error;
    CHECK( instance.saveSession( stateAt( longId, "intent" ), error ).isEmpty() );
    const std::string maxId( 128, 'b' );
    CHECK_FALSE( instance.saveSession( stateAt( maxId, "intent" ), error ).isEmpty() );

    // Nothing landed outside the store directory.
    const QStringList entries = store.qdir().entryList( QDir::Files );
    for ( const QString &entry : entries )
        CHECK( entry.startsWith( "harness_session_" ) );
}

// — injection 10/11: resume rewinds stale facts; decisions survive —————————

TEST_CASE( "resume rewinds to grounding when facts changed and keeps the "
           "decision history", "[harness][recovery-r4][resume]" )
{
    StoreDir store;
    auto &instance = HarnessSessionStore::instance();

    QTemporaryDir facts;
    const QString factFile = facts.filePath( QStringLiteral( "scene.tif" ) );
    {
        QFile f( factFile );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "scene-bytes-v1" ) );
    }
    const QFileInfo info( factFile );

    HarnessSessionState state = stateAt( "resume-edge", "analysis" );
    state.decisions = Json::Value( Json::arrayValue );
    Json::Value decision( Json::objectValue );
    decision["id"] = "decision-1";
    decision["status"] = "resolved";
    decision["chosen"] = "band 8";
    state.decisions.append( decision );
    Json::Value identity( Json::objectValue );
    identity["path"] = factFile.toStdString();
    identity["size"] = static_cast<Json::Int64>( info.size() );
    identity["mtime_ms"] = static_cast<Json::Int64>( info.lastModified().toMSecsSinceEpoch() );
    state.factIdentities["scene"] = identity;
    HarnessError ignoredError;
    REQUIRE_FALSE( instance.saveSession( state, ignoredError ).isEmpty() );

    // Facts still current: resume continues from the saved stage.
    {
        HarnessError error;
        const auto resumed = instance.resumeSession( "resume-edge", error );
        REQUIRE( resumed.has_value() );
        CHECK( resumed->rewindToStage == "analysis" );
        CHECK( resumed->state.decisions.size() == 1 );
    }

    // The file is rewritten (size changes): the facts predate the bytes —
    // resume must rewind to grounding, not replay analysis on stale facts.
    {
        QFile f( factFile );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "scene-bytes-v2-much-longer" ) );
    }
    {
        HarnessError error;
        const auto resumed = instance.resumeSession( "resume-edge", error );
        REQUIRE( resumed.has_value() );
        CHECK( resumed->rewindToStage == "grounding" );
        CHECK( resumed->staleSlots["scene"]["reason"].asString() == "file_changed" );
        // Decisions and attempt history survive the rewind (they are not
        // facts) — the operator does not re-decide what was decided.
        REQUIRE( resumed->state.decisions.size() == 1 );
        CHECK( resumed->state.decisions[0]["chosen"].asString() == "band 8" );
    }

    // The fact file vanishing entirely is the strongest staleness.
    QFile::remove( factFile );
    {
        HarnessError error;
        const auto resumed = instance.resumeSession( "resume-edge", error );
        REQUIRE( resumed.has_value() );
        CHECK( resumed->rewindToStage == "grounding" );
        CHECK( resumed->staleSlots["scene"]["reason"].asString() == "file_missing" );
    }
}

TEST_CASE( "a slot saved without an identity is stale by construction "
           "(no_identity)", "[harness][recovery-r4][resume]" )
{
    StoreDir store;
    auto &instance = HarnessSessionStore::instance();
    HarnessSessionState state = stateAt( "no-identity", "candidates" );
    Json::Value identity( Json::objectValue );
    identity["note"] = "identity never derived";
    state.factIdentities["slot"] = identity;
    HarnessError ignoredError;
    REQUIRE_FALSE( instance.saveSession( state, ignoredError ).isEmpty() );

    HarnessError error;
    const auto resumed = instance.resumeSession( "no-identity", error );
    REQUIRE( resumed.has_value() );
    CHECK( resumed->rewindToStage == "grounding" );
    CHECK( resumed->staleSlots["slot"]["reason"].asString() == "no_identity" );
}

// — injection 12: concurrent savers never publish a torn checkpoint ————————

TEST_CASE( "concurrent savers leave every published checkpoint loadable",
           "[harness][recovery-r4][concurrency]" )
{
    StoreDir store;
    auto &instance = HarnessSessionStore::instance();

    constexpr int kThreads = 4;
    constexpr int kSavesPerThread = 6;
    std::vector<std::thread> threads;
    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back( [ &instance, t ] {
            for ( int i = 0; i < kSavesPerThread; ++i )
            {
                const std::string id = "conc-" + std::to_string( t ) + "-" +
                                       std::to_string( i );
                HarnessSessionState state = stateAt( id, "grounding" );
                state.ir = Json::Value( Json::objectValue );
                state.ir["step"] = i;
                HarnessError error;
                const QString path = instance.saveSession( state, error );
                INFO( "save " << id << ": " << error.summary );
                CHECK_FALSE( path.isEmpty() );
            }
        } );
    }
    for ( auto &thread : threads )
        thread.join();

    // Every published checkpoint loads back exactly (atomic rename: a reader
    // never observes a half-written document).
    for ( int t = 0; t < kThreads; ++t )
    {
        for ( int i = 0; i < kSavesPerThread; ++i )
        {
            const std::string id = "conc-" + std::to_string( t ) + "-" + std::to_string( i );
            HarnessError error;
            const auto state = instance.loadSession( id, error );
            REQUIRE( state.has_value() );
            CHECK( state->ir["step"].asInt() == i );
        }
    }
}

TEST_CASE( "the same session saved concurrently always resolves to one "
           "intact document", "[harness][recovery-r4][concurrency]" )
{
    StoreDir store;
    auto &instance = HarnessSessionStore::instance();

    std::vector<std::thread> threads;
    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back( [ &instance, t ] {
            for ( int i = 0; i < 10; ++i )
            {
                HarnessSessionState state = stateAt( "same-id", "ir" );
                state.ir = Json::Value( Json::objectValue );
                state.ir["writer"] = t;
                HarnessError ignoredError;
                instance.saveSession( state, ignoredError );
                QThread::msleep( 1 );
            }
        } );
    }
    for ( auto &thread : threads )
        thread.join();

    HarnessError error;
    const auto state = instance.loadSession( "same-id", error );
    REQUIRE( state.has_value() );
    CHECK( isKnownSessionStage( state->stageCursor ) );
    const int writer = state->ir["writer"].asInt();
    CHECK( writer >= 0 );
    CHECK( writer < 4 );
}

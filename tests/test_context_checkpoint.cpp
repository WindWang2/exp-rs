// tests/test_context_checkpoint.cpp
//
// Compiler 10.0: harness session checkpoint/resume — persistence bounds,
// fail-closed loading, stale-fact invalidation, deterministic compaction.

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <string>

#include "agent/harness/context_checkpoint.h"

using namespace sicnu::agent::harness;

namespace
{
const char *kStoreDir = "harness_session_test_store";

void cleanStore()
{
  QDir( kStoreDir ).removeRecursively();
}

HarnessSessionState sampleState( const std::string &id )
{
  HarnessSessionState state;
  state.sessionId = id;
  state.stageCursor = session_stages::kRepair;
  state.goal = "NDVI change over the August pair";
  state.intent = "change";
  Json::Reader reader;
  Json::Value ir;
  REQUIRE( reader.parse( R"({
    "kind": "workflow_ir", "schema_version": "1.0",
    "ir_id": "wir-test",
    "nodes": [ { "id": "diff", "operator": "rs:change_difference" } ]
  })", ir ) );
  state.ir = ir;
  Json::Value analysis;
  analysis["verdict"] = "ok";
  state.analysis = analysis;
  Json::Value decision( Json::objectValue );
  decision["kind"] = "alternative";
  decision["subject"] = "ndvi_method";
  decision["status"] = "resolved";
  state.decisions.append( decision );
  Json::Value attempt( Json::objectValue );
  attempt["attempt"] = 1;
  attempt["code"] = "EXECUTION_FAILED";
  state.failedAttempts.append( attempt );
  Json::Value identity( Json::objectValue );
  identity["path"] = "harness_session_fixture.tif";
  identity["size"] = static_cast<Json::Int64>( 12345 );
  identity["mtime_ms"] = static_cast<Json::Int64>( 1700000000000LL );
  state.factIdentities["primary"] = identity;
  return state;
}
} // namespace

TEST_CASE( "Sessions round-trip through save/load with typed failures", "[context_checkpoint]" )
{
  cleanStore();
  HarnessSessionStore &store = HarnessSessionStore::instance();
  store.setDirectory( kStoreDir );

  HarnessError error;
  const QString path = store.saveSession( sampleState( "sess-1" ), error );
  REQUIRE( !path.isEmpty() );
  REQUIRE( error.code.empty() );

  auto loaded = store.loadSession( "sess-1", error );
  REQUIRE( loaded );
  CHECK( loaded->stageCursor == session_stages::kRepair );
  CHECK( loaded->goal == "NDVI change over the August pair" );
  CHECK( loaded->ir["ir_id"].asString() == "wir-test" );
  REQUIRE( loaded->decisions.size() == 1 );
  CHECK( loaded->decisions[0]["subject"].asString() == "ndvi_method" );
  CHECK( loaded->factIdentities["primary"]["path"].asString() == "harness_session_fixture.tif" );

  // Unknown session: typed failure, never a default object.
  error = HarnessError{};
  CHECK( !store.loadSession( "no-such-session", error ).has_value() );
  CHECK( error.code == "WORKFLOW_NOT_FOUND" );

  // Hostile session ids are refused before touching the filesystem.
  HarnessSessionState evil = sampleState( "../escape" );
  error = HarnessError{};
  CHECK( store.saveSession( evil, error ).isEmpty() );
  CHECK( error.code == "INVALID_PARAMETER" );

  // The same filename-safety contract applies to the read/delete/resume
  // paths (#1056). The contract is "the hostile id is refused BEFORE the
  // filesystem is touched", so the pre-arranged escaped file only proves the
  // refusal is not merely "file missing". A trailing-".." directory name is
  // not creatable on Windows (the platform strips it), so the on-disk probe
  // is POSIX-only; the id-refusal assertions below run everywhere.
  const QString escapedPath = kStoreDir + QStringLiteral( "/harness_session_../escape.json" );
#ifndef _WIN32
  REQUIRE( QDir().mkpath( kStoreDir + QStringLiteral( "/harness_session_../" ) ) );
  {
    QFile escaped( escapedPath );
    REQUIRE( escaped.open( QIODevice::WriteOnly ) );
    escaped.write( "{\"kind\":\"harness_session\",\"schema_version\":\"1.0\",\"session_id\":\"escape\"}" );
    escaped.close();
  }
#endif
  error = HarnessError{};
  CHECK( !store.loadSession( "../escape", error ).has_value() );
  CHECK( error.code == "INVALID_PARAMETER" );
  CHECK_FALSE( store.deleteSession( "../escape" ) );
#ifndef _WIN32
  // The pre-arranged file is still there: the delete never happened.
  CHECK( QFile::exists( escapedPath ) );
#endif
  error = HarnessError{};
  CHECK( !store.resumeSession( "../escape", error ).has_value() );
  CHECK( error.code == "INVALID_PARAMETER" );
  for ( const char *hostile : { "/etc/passwd", "..\\escape", "sess\x01ctl" } )
  {
    error = HarnessError{};
    CHECK( !store.loadSession( hostile, error ).has_value() );
    CHECK( error.code == "INVALID_PARAMETER" );
    CHECK_FALSE( store.deleteSession( hostile ) );
  }
  // A valid-but-unknown id keeps the not-found contract.
  error = HarnessError{};
  CHECK( !store.loadSession( "sess-none", error ).has_value() );
  CHECK( error.code == "WORKFLOW_NOT_FOUND" );
  CHECK_FALSE( store.deleteSession( "sess-none" ) );

  // Corrupt documents are rejected fail-closed.
  QFile corrupt( QDir( kStoreDir ).filePath( "harness_session_broken.json" ) );
  REQUIRE( corrupt.open( QIODevice::WriteOnly ) );
  corrupt.write( "{ not json" );
  corrupt.close();
  error = HarnessError{};
  CHECK( !store.loadSession( "broken", error ).has_value() );
  CHECK( error.code == "INVALID_PLAN" );

  cleanStore();
}

TEST_CASE( "Resume marks changed fact files stale and rewinds to grounding",
           "[context_checkpoint]" )
{
  cleanStore();
  HarnessSessionStore &store = HarnessSessionStore::instance();
  store.setDirectory( kStoreDir );

  // A real file whose identity changes between save and resume.
  const QString fixture = QDir( kStoreDir ).filePath( "scene.tif" );
  REQUIRE( QDir().mkpath( kStoreDir ) );
  {
    QFile file( fixture );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "v1" );
  }
  QFileInfo info( fixture );

  HarnessSessionState state = sampleState( "sess-stale" );
  Json::Value identity( Json::objectValue );
  identity["path"] = fixture.toStdString();
  identity["size"] = static_cast<Json::Int64>( info.size() );
  identity["mtime_ms"] = static_cast<Json::Int64>( info.lastModified().toMSecsSinceEpoch() );
  state.factIdentities["primary"] = identity;

  HarnessError error;
  REQUIRE( !store.saveSession( state, error ).isEmpty() );

  // Unchanged file: resume continues from the saved cursor.
  auto fresh = store.resumeSession( "sess-stale", error );
  REQUIRE( fresh );
  CHECK( fresh->rewindToStage == session_stages::kRepair );
  CHECK( !fresh->staleSlots["primary"].get( "stale", true ).asBool() );

  // Rewritten file (new bytes, new mtime): stale -> rewind to grounding,
  // decisions/attempts survive.
  {
    QFile file( fixture );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    file.write( "v2-with-different-bytes" );
    file.flush();
  }
  info.refresh();
  REQUIRE( static_cast<long long>( info.size() ) != 2 ); // self-check guard
  auto stale = store.resumeSession( "sess-stale", error );
  REQUIRE( stale );
  CHECK( stale->rewindToStage == session_stages::kGrounding );
  CHECK( stale->staleSlots["primary"].get( "stale", false ).asBool() );
  CHECK( stale->staleSlots["primary"].get( "reason", "" ).asString() == "file_changed" );
  REQUIRE( stale->state.decisions.size() == 1 );
  REQUIRE( stale->state.failedAttempts.size() == 1 );

  // Missing file: stale with file_missing.
  Json::Value identity2( Json::objectValue );
  identity2["path"] = "/nonexistent/vanished.tif";
  identity2["size"] = static_cast<Json::Int64>( 10 );
  identity2["mtime_ms"] = static_cast<Json::Int64>( 1 );
  state.factIdentities["secondary"] = identity2;
  REQUIRE( !store.saveSession( state, error ).isEmpty() );
  auto vanished = store.resumeSession( "sess-stale", error );
  REQUIRE( vanished );
  CHECK( vanished->staleSlots["secondary"].get( "reason", "" ).asString() == "file_missing" );

  cleanStore();
}

TEST_CASE( "The store is bounded: oldest sessions evict first", "[context_checkpoint]" )
{
  cleanStore();
  HarnessSessionStore &store = HarnessSessionStore::instance();
  store.setDirectory( kStoreDir );
  HarnessError error;
  for ( int i = 0; i < HarnessSessionStore::kMaxSessions + 2; ++i )
  {
    const std::string id = "bulk-" + std::to_string( i );
    REQUIRE( !store.saveSession( sampleState( id ), error ).isEmpty() );
    QFile( QDir( kStoreDir ).filePath( "touch.bin" ) ).open( QIODevice::WriteOnly ); // mtime spacing
  }
  const Json::Value sessions = store.listSessions();
  CHECK( static_cast<int>( sessions.size() ) <= HarnessSessionStore::kMaxSessions );
  // The oldest two were evicted.
  bool sawOldest = false;
  for ( const Json::Value &entry : sessions )
    if ( entry["session_id"].asString() == "bulk-0" )
      sawOldest = true;
  CHECK( !sawOldest );
  cleanStore();
}

TEST_CASE( "Compaction keeps the science and drops the bulk, deterministically",
           "[context_checkpoint]" )
{
  HarnessSessionState state = sampleState( "compact-me" );
  Json::Value analysis;
  analysis["verdict"] = "fixable";
  Json::Value checks( Json::arrayValue );
  for ( int i = 0; i < 64; ++i )
  {
    Json::Value check( Json::objectValue );
    check["check"] = "filler_" + std::to_string( i );
    check["details"] = "long detail blob to give the document real bulk";
    checks.append( check );
  }
  analysis["checks"] = checks;
  state.analysis = analysis;

  const Json::Value first = HarnessSessionStore::compactState( state );
  const Json::Value second = HarnessSessionStore::compactState( state );
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  CHECK( Json::writeString( builder, first ) == Json::writeString( builder, second ) );
  CHECK( static_cast<long long>( first.toStyledString().size() ) <
         static_cast<long long>( state.toJson().toStyledString().size() ) );
  CHECK( first["analysis"]["verdict"].asString() == "fixable" );
  CHECK( first.isMember( "analysis" ) ); // analysis kept (slim)
  CHECK( first["analysis"]["checks"].isNull() );
  CHECK( first["ir"]["ir_id"].asString() == "wir-test" );
  CHECK( first["fact_identities"]["primary"]["path"].asString() ==
         "harness_session_fixture.tif" );
}

TEST_CASE( "An oversized session document compacts under the 64 KiB bound",
           "[context_checkpoint]" )
{
  cleanStore();
  HarnessSessionStore &store = HarnessSessionStore::instance();
  store.setDirectory( kStoreDir );
  HarnessSessionState state = sampleState( "huge" );
  // Bulk: a large checks ledger + a large issues list in the analysis.
  Json::Value analysis;
  analysis["verdict"] = "fixable";
  for ( int i = 0; i < 3000; ++i )
  {
    Json::Value check( Json::objectValue );
    check["check"] = "filler_check_name_" + std::to_string( i );
    check["status"] = "pass";
    check["details"] = "padding blob to give the document real bulk volume";
    analysis["checks"].append( check );
  }
  state.analysis = analysis;
  REQUIRE( state.approxTokens() * 4 > HarnessSessionStore::kMaxDocumentBytes );

  HarnessError error;
  const QString path = store.saveSession( state, error );
  REQUIRE( !path.isEmpty() ); // compaction saved it — no typed failure
  QFileInfo info( path );
  CHECK( static_cast<long long>( info.size() ) <=
         HarnessSessionStore::kMaxDocumentBytes );
  // The compacted document keeps the science verdict.
  auto loaded = store.loadSession( "huge", error );
  REQUIRE( loaded );
  CHECK( loaded->analysis["verdict"].asString() == "fixable" );
  cleanStore();
}

TEST_CASE( "Stage vocabulary is closed", "[context_checkpoint]" )
{
  CHECK( isKnownSessionStage( session_stages::kGrounding ) );
  CHECK( isKnownSessionStage( session_stages::kDone ) );
  CHECK( !isKnownSessionStage( "vibes" ) );
}

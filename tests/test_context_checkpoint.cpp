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
#include <vector>

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

TEST_CASE( "session_id filename safety is enforced on every store operation",
           "[context_checkpoint]" )
{
  // #1056: save validated the id charset, load/delete/resume/staleness did
  // not — a crafted id reached sessionPathLocked() and could read (delete:
  // unlink) outside the store directory. Every path-deriving operation now
  // runs the identical gate, BEFORE any path is constructed.
  cleanStore();
  HarnessSessionStore &store = HarnessSessionStore::instance();
  store.setDirectory( kStoreDir );
  REQUIRE( QDir().mkpath( kStoreDir ) );

  HarnessError error;
  REQUIRE( !store.saveSession( sampleState( "sess-gate" ), error ).isEmpty() );

  // A decoy file OUTSIDE the store directory: a traversal id must not be
  // able to reach (let alone delete) it through the store.
  const QString decoy = QDir( kStoreDir ).filePath( "decoy.json" );
  {
    QFile file( decoy );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "{}" );
  }

  const std::vector<std::string> invalidIds = {
    "",                      // empty
    "..",                    // parent
    "../escape",             // save already rejected this; load/delete did not
    "a/b",                   // separator
    "a\\b",                  // windows separator
    "id with spaces",
    "id;rm -rf",             // shell-shaped
    std::string( 129, 'x' ), // over the 128-char bound
    "\xC3\xA9",              // non-ASCII
  };
  for ( const std::string &id : invalidIds )
  {
    INFO( "invalid id: " << id );
    error = HarnessError{};
    CHECK_FALSE( store.loadSession( id, error ).has_value() );
    CHECK( error.code == "INVALID_PARAMETER" );

    error = HarnessError{};
    CHECK_FALSE( store.resumeSession( id, error ).has_value() );
    CHECK( error.code == "INVALID_PARAMETER" );

    error = HarnessError{};
    CHECK_FALSE( store.deleteSession( id, &error ) );
    CHECK( error.code == "INVALID_PARAMETER" );

    HarnessSessionState state = sampleState( id );
    error = HarnessError{};
    CHECK( store.saveSession( state, error ).isEmpty() );
    CHECK( error.code == "INVALID_PARAMETER" );
  }

  // The decoy survived every invalid-id operation.
  CHECK( QFile::exists( decoy ) );
  // The legitimate session still loads, resumes and deletes.
  CHECK( store.loadSession( "sess-gate", error ).has_value() );
  CHECK( store.resumeSession( "sess-gate", error ).has_value() );
  error = HarnessError{};
  CHECK( store.deleteSession( "sess-gate", &error ) );
  CHECK( error.code.empty() );
  // Deleting again is a plain "no such session" (not a validation failure).
  error = HarnessError{};
  CHECK_FALSE( store.deleteSession( "sess-gate", &error ) );
  CHECK( error.code.empty() );

  cleanStore();
}

TEST_CASE( "A stored document carrying a non-filename-safe session_id is corrupt",
           "[context_checkpoint]" )
{
  // Defense in depth (#1056): a hand-written checkpoint file can never
  // smuggle a path-bearing id through fromJson into the session state.
  Json::Value doc = sampleState( "whatever" ).toJson();
  doc["session_id"] = "../escape";
  std::string error;
  CHECK_FALSE( HarnessSessionState::fromJson( doc, &error ).has_value() );
  CHECK( error == "session_id is not filename-safe" );
}

TEST_CASE( "isFilenameSafeSessionId matches the save-side charset", "[context_checkpoint]" )
{
  CHECK( isFilenameSafeSessionId( "sess-1" ) );
  CHECK( isFilenameSafeSessionId( "A_b.9" ) );
  CHECK_FALSE( isFilenameSafeSessionId( "" ) );
  CHECK_FALSE( isFilenameSafeSessionId( std::string( 129, 'x' ) ) );
  CHECK( isFilenameSafeSessionId( std::string( 128, 'x' ) ) );
  CHECK_FALSE( isFilenameSafeSessionId( "a/b" ) );
  CHECK_FALSE( isFilenameSafeSessionId( ".." ) );
}

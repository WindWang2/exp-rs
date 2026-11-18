/***************************************************************************
 * tests/test_r33_t05_journal_bounds.cpp — R33-T05 WP05/WP06 journal gates.
 *
 * Pins the seq-space edges of the session journal (the append-only audit
 * authority) and the compaction projection's terminal-evidence contract:
 *
 *   - the persisted next_seq is honored (an evicted history must not
 *     regress the counter into seq reuse),
 *   - documents at the int64 ceiling — where no append can safely follow —
 *     are refused at load, and append() refuses before the wraparound,
 *   - compaction keeps the terminal entry's typed stop reason, so a
 *     compacted-then-reloaded cancelled session still says why it stopped.
 *
 * Pure C++20 + jsoncpp — same lane as test_agent_loop_journal_durability.
 ***************************************************************************/
#include "agent_loop/session_journal.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cstdint>
#include <filesystem>
#include <string>

using sicnu::agent_loop::SessionJournal;

namespace
{

Json::Value entryDoc( long long seq, const char *event, const char *stage,
                      Json::Value payload = Json::Value( Json::objectValue ) )
{
    Json::Value doc( Json::objectValue );
    doc[ "seq" ] = Json::Value( static_cast< Json::Int64 >( seq ) );
    doc[ "at" ] = Json::Value( static_cast< Json::Int64 >( 1 ) );
    doc[ "stage" ] = stage;
    doc[ "event" ] = event;
    doc[ "payload" ] = std::move( payload );
    return doc;
}

Json::Value journalDoc( const std::string &sessionId, std::vector<Json::Value> entries,
                        std::optional<long long> nextSeq = std::nullopt )
{
    Json::Value doc( Json::objectValue );
    doc[ "schema_version" ] = "1.0";
    doc[ "session_id" ] = sessionId;
    if ( nextSeq )
        doc[ "next_seq" ] = Json::Value( static_cast< Json::Int64 >( *nextSeq ) );
    Json::Value array( Json::arrayValue );
    for ( auto &entry : entries )
        array.append( std::move( entry ) );
    doc[ "entries" ] = std::move( array );
    return doc;
}

const char *kTerminalStage = "aborted";
const char *kStopReason = "CANCELLED";

} // namespace

TEST_CASE( "journal next_seq is honored instead of regressed by eviction",
           "[agent_loop][r33t05][seq]" )
{
    // A custom maxEntries of 0 evicts every entry after appending: the
    // saved document has an empty entries array whose persisted next_seq
    // is the only witness of how far the journal got.
    SessionJournal journal( "sess-evicted", 0 );
    for ( int i = 0; i < 5; ++i )
        REQUIRE( journal.append( "note", "plan_request", Json::Value( Json::objectValue ), i + 1 ) );
    REQUIRE( journal.entries().empty() );
    REQUIRE( journal.nextSeq() == 6 );

    const Json::Value doc = journal.toJson();
    REQUIRE( doc[ "next_seq" ].asInt64() == 6 );

    std::string error;
    auto reloaded = SessionJournal::fromJson( doc, &error );
    REQUIRE( reloaded );
    REQUIRE( reloaded->nextSeq() == 6 ); // derived last+1 would say 1

    // The next append continues past the evicted history — no seq reuse.
    REQUIRE( reloaded->append( "note", "plan_request", Json::Value( Json::objectValue ), 10 ) );
    REQUIRE( reloaded->entries().back().seq == 6 );
}

TEST_CASE( "journal documents at the int64 seq ceiling are handled fail-closed for writes",
           "[agent_loop][r33t05][seq]" )
{
    std::string error;

    SECTION( "an entry at INT64_MAX cannot be safely followed" )
    {
        const Json::Value doc = journalDoc(
            "sess-max", { entryDoc( INT64_MAX - 1, "note", "plan_request" ),
                          entryDoc( INT64_MAX, "note", "plan_request" ) } );
        REQUIRE_FALSE( SessionJournal::fromJson( doc, &error ).has_value() );
        REQUIRE( error.find( "int64 limit" ) != std::string::npos );
    }

    SECTION( "a persisted next_seq at the ceiling loads read-only; append refuses" )
    {
        // R1 P3-3 convergence: a journal saved at the exhaustion point
        // must still LOAD (replay-only consumers keep the evidence) while
        // append() refuses instead of wrapping.
        const Json::Value doc = journalDoc(
            "sess-max-next", { entryDoc( 7, "note", "plan_request" ) }, INT64_MAX );
        auto journal = SessionJournal::fromJson( doc, &error );
        REQUIRE( journal );
        REQUIRE( journal->nextSeq() == INT64_MAX );
        REQUIRE( journal->replay().entries.size() == 1 );
        REQUIRE_FALSE( journal->append( "note", "plan_request",
                                        Json::Value( Json::objectValue ), 99 ) );
        REQUIRE( journal->entries().size() == 1 ); // nothing was written
    }

    SECTION( "a next_seq not past the last entry is a spliced document" )
    {
        const Json::Value doc = journalDoc(
            "sess-spliced", { entryDoc( 7, "note", "plan_request" ) }, 7 );
        REQUIRE_FALSE( SessionJournal::fromJson( doc, &error ).has_value() );
        REQUIRE( error.find( "not past the last entry" ) != std::string::npos );
    }

    SECTION( "a non-integer next_seq is refused" )
    {
        Json::Value doc = journalDoc( "sess-str", { entryDoc( 7, "note", "plan_request" ) } );
        doc[ "next_seq" ] = "eight";
        REQUIRE_FALSE( SessionJournal::fromJson( doc, &error ).has_value() );
    }
}

TEST_CASE( "journal append refuses when the seq space is exhausted",
           "[agent_loop][r33t05][seq]" )
{
    // One before the ceiling: exactly one append remains, then refusal —
    // never a wrapped (negative) seq. The exhausted journal still saves
    // and reloads (read-only evidence; the follow-up append refuses).
    const Json::Value doc = journalDoc(
        "sess-last", { entryDoc( INT64_MAX - 2, "note", "plan_request" ) }, INT64_MAX - 1 );
    std::string error;
    auto journal = SessionJournal::fromJson( doc, &error );
    REQUIRE( journal );
    REQUIRE( journal->nextSeq() == INT64_MAX - 1 );

    REQUIRE( journal->append( "note", "plan_request", Json::Value( Json::objectValue ), 99 ) );
    REQUIRE( journal->entries().back().seq == INT64_MAX - 1 );
    REQUIRE( journal->nextSeq() == INT64_MAX );

    REQUIRE_FALSE( journal->append( "note", "plan_request", Json::Value( Json::objectValue ), 100 ) );
    REQUIRE( journal->nextSeq() == INT64_MAX ); // no wraparound to a negative seq
    REQUIRE( journal->entries().size() == 2 );

    // Round-trip at the exhaustion point (R1 P3-3): save, reload, replay.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "sicnu_r33_t05_journal" / "exhausted";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir, ec );
    std::string saveError;
    REQUIRE( journal->save( dir.string(), &saveError ) );
    auto reloaded = SessionJournal::load( dir.string(), "sess-last", &error );
    REQUIRE( reloaded );
    REQUIRE( reloaded->nextSeq() == INT64_MAX );
    REQUIRE( reloaded->replay().entries.size() == 2 );
    REQUIRE_FALSE( reloaded->append( "note", "plan_request",
                                     Json::Value( Json::objectValue ), 101 ) );
    fs::remove_all( dir, ec );
}

TEST_CASE( "journal legacy documents without next_seq still load",
           "[agent_loop][r33t05][seq]" )
{
    const Json::Value doc = journalDoc( "sess-legacy", { entryDoc( 3, "note", "plan_request" ),
                                                          entryDoc( 9, "note", "plan_request" ) } );
    std::string error;
    auto journal = SessionJournal::fromJson( doc, &error );
    REQUIRE( journal );
    REQUIRE( journal->nextSeq() == 10 ); // derived: last + 1
}

TEST_CASE( "compaction preserves the terminal stop reason across reload",
           "[agent_loop][r33t05][compaction]" )
{
    SessionJournal journal( "sess-aborted" );
    REQUIRE( journal.append( "stage_enter", "plan_request", Json::Value( Json::objectValue ), 1 ) );
    Json::Value big( Json::objectValue );
    big[ "blob" ] = std::string( 2048, 'x' );
    REQUIRE( journal.append( "note", "plan_request", big, 2 ) );
    REQUIRE( journal.append( "terminal", kTerminalStage,
                             Json::Value( "CANCELLED" ), 3 ) );
    REQUIRE( journal.replay().stopReason == "CANCELLED" );

    const Json::Value compacted = journal.compactProjection();

    // The terminal payload survives as a string; everything else compacts.
    const Json::Value &entries = compacted[ "entries" ];
    REQUIRE( entries.size() == 3 );
    REQUIRE( entries[ 1 ][ "payload" ].isObject() );
    REQUIRE( entries[ 1 ][ "payload" ][ "compacted" ].asBool() );
    REQUIRE( entries[ 2 ][ "event" ].asString() == "terminal" );
    REQUIRE( entries[ 2 ][ "payload" ].isString() );
    REQUIRE( entries[ 2 ][ "payload" ].asString() == "CANCELLED" );

    // Reload the compacted document: replay still reports the typed stop
    // reason — a cancelled session must not degrade to "stopped, unknown
    // why" just because the journal was saved through compaction.
    std::string error;
    const auto reloaded = SessionJournal::fromJson( compacted, &error );
    REQUIRE( reloaded );
    const auto replay = reloaded->replay();
    REQUIRE( replay.terminalState == kTerminalStage );
    REQUIRE( replay.stopReason == "CANCELLED" );
}

TEST_CASE( "a disk round-trip through the compaction projection keeps the stop reason",
           "[agent_loop][r33t05][compaction][disk]" )
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "sicnu_r33_t05_journal" / "compaction";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir, ec );

    SessionJournal journal( "sess-disk" );
    REQUIRE( journal.append( "stage_enter", "plan_request", Json::Value( Json::objectValue ), 1 ) );
    // A payload body past kMaxDocumentBytes forces save() through the
    // compaction projection on a real file.
    Json::Value big( Json::objectValue );
    big[ "blob" ] = std::string( SessionJournal::kMaxDocumentBytes, 'y' );
    REQUIRE( journal.append( "note", "plan_request", big, 2 ) );
    REQUIRE( journal.append( "terminal", kTerminalStage,
                             Json::Value( "CANCELLED" ), 3 ) );

    std::string saveError;
    REQUIRE( journal.save( dir.string(), &saveError ) );
    std::string loadError;
    const auto reloaded = SessionJournal::load( dir.string(), "sess-disk", &loadError );
    REQUIRE( reloaded );
    const auto replay = reloaded->replay();
    REQUIRE( replay.terminalState == kTerminalStage );
    REQUIRE( replay.stopReason == "CANCELLED" );
    // The oversized body did not ride along: the document fits the bound.
    REQUIRE( reloaded->entries().size() == 3 );
    REQUIRE( reloaded->entries()[ 1 ].payload.isObject() );

    fs::remove_all( dir, ec );
}

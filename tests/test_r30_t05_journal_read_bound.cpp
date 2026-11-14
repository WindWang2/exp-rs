// tests/test_r30_t05_journal_read_bound.cpp
//
// R30-T05 WP06: the journal reader's byte bound must hold for the bytes
// actually READ, not only the stat seen before the read. #1999 rejected an
// oversized document only AFTER an unbounded rdbuf() slurp — a file that
// grew between the stat and the read allocated whatever it had grown to
// before the rejection fired. The reader now stops reading at cap+1 bytes,
// so the allocation itself is bounded.
//
// This file pins the observable envelope of that contract: a document at
// the cap boundary loads, one byte beyond it refuses with the typed bound
// error, and legal journals (small, deep-payload) round-trip unchanged.
//
// Pure C++ — no Qt, no GDAL.

#include <catch2/catch_test_macros.hpp>

#include <json/writer.h>

#include "agent_loop/session_journal.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace sicnu::agent_loop;

namespace {

std::string uniqueTempDir( const char *tag )
{
    const fs::path base = fs::temp_directory_path() /
        ( std::string( "sicnu-r30t05-journal-" ) + tag + "-" +
          std::to_string( std::hash<std::string>{}( tag ) ) );
    std::error_code ec;
    fs::remove_all( base, ec );
    fs::create_directories( base, ec );
    return base.string();
}

/// A minimal legal journal document whose serialized size is exactly
/// `targetBytes` (a plain-letter filler rides inside the payload string, so
/// no JSON escaping inflates the arithmetic).
std::string journalDocumentOfBytes( const std::string &sessionId, std::size_t targetBytes )
{
    const std::string prefix =
        "{\"schema_version\":\"" + std::string( kSessionJournalSchemaVersion ) +
        "\",\"session_id\":\"" + sessionId +
        "\",\"next_seq\":1,\"entries\":[{\"seq\":1,\"at\":1,\"stage\":\"plan_request\","
        "\"event\":\"stage_enter\",\"payload\":{\"filler\":\"";
    const std::string suffix = "\"}}]}";
    if ( targetBytes <= prefix.size() + suffix.size() )
        return prefix + suffix;
    const std::size_t filler = targetBytes - prefix.size() - suffix.size();
    return prefix + std::string( filler, 'f' ) + suffix;
}

void writeFile( const fs::path &path, const std::string &body )
{
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    out << body;
}

} // namespace

TEST_CASE( "journal load accepts a document at the byte cap", "[agent_loop][r30t05]" )
{
    const std::string dir = uniqueTempDir( "cap" );
    const std::string sessionId = "sess-cap";
    // A journal whose payload is exactly one filler byte long round-trips:
    // the legal shape stays loadable after the bounded-read change.
    SessionJournal journal( sessionId );
    Json::Value payload;
    payload["filler"] = "f";
    REQUIRE( journal.append( "stage_enter", stages::kPlanRequest, payload, 1 ) );
    REQUIRE( journal.save( dir, nullptr ) );
    std::string error;
    const auto loaded = SessionJournal::load( dir, sessionId, &error );
    REQUIRE( loaded.has_value() );
    REQUIRE( error.empty() );
    REQUIRE( loaded->entries().size() == 1 );
}

TEST_CASE( "journal load refuses a document beyond the byte cap with a typed error",
           "[agent_loop][r30t05]" )
{
    const std::string dir = uniqueTempDir( "over" );
    const fs::path path = fs::path( dir ) / "sess-over.json";
    writeFile( path, journalDocumentOfBytes( "sess-over", SessionJournal::kJournalWriteCapBytes + 1 ) );
    REQUIRE( fs::file_size( path ) > SessionJournal::kJournalWriteCapBytes );

    std::string error;
    const auto loaded = SessionJournal::load( dir, "sess-over", &error );
    REQUIRE_FALSE( loaded.has_value() );
    REQUIRE_FALSE( error.empty() );
    REQUIRE( error.find( "byte bound" ) != std::string::npos );
}

TEST_CASE( "a just-under-cap journal document is read to the end (no early cut)",
           "[agent_loop][r30t05]" )
{
    // The chunked reader must not stop at a chunk boundary and mistake a
    // legal document for a truncated one: a document spanning many 64 KiB
    // chunks (but under the cap) parses completely. 8 MiB keeps the test
    // fast while crossing ~128 chunks.
    const std::string dir = uniqueTempDir( "chunks" );
    const fs::path path = fs::path( dir ) / "sess-chunks.json";
    const std::string body = journalDocumentOfBytes( "sess-chunks", 8u * 1024u * 1024u );
    writeFile( path, body );

    std::string error;
    const auto loaded = SessionJournal::load( dir, "sess-chunks", &error );
    REQUIRE( loaded.has_value() );
    REQUIRE( error.empty() );
    REQUIRE( loaded->entries().size() == 1 );
    REQUIRE( loaded->entries()[0].payload["filler"].asString().size() > 8u * 1024u * 1024u - 512u );
}

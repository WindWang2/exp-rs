/***************************************************************************
  tests/test_agent_loop_journal_durability.cpp — journal publication oracle.

  Pins the SessionJournal::save/load durability contract that the replay
  guarantee rests on ("a replayed session cannot lie"):
    - concurrent publishers of the same journal file never expose a torn
      document (shared-temp truncation would),
    - save→rename→reopen round-trips through Unicode and deep paths,
    - a failed publish leaves the previous journal untouched,
    - unrepresentable (over-long) paths fail gracefully, never by crash.

  Pure C++20 + jsoncpp — same lane as test_agent_loop_core.
 ***************************************************************************/

#include "agent_loop/session_journal.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>

#if !defined( _WIN32 )
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using sicnu::agent_loop::SessionJournal;

namespace
{

std::string scratchDir( const std::string &name )
{
    const fs::path dir = fs::temp_directory_path() / "sicnu_journal_durability" / name;
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir, ec );
    return dir.string();
}

SessionJournal journalWithFiller( const std::string &sessionId, const std::string &filler,
                                  long long at )
{
    SessionJournal journal( sessionId );
    Json::Value payload;
    payload["filler"] = filler;
    payload["writer"] = sessionId;
    REQUIRE( journal.append( "note", "execute", payload, at ) );
    return journal;
}

/// The filler size must exceed the ofstream buffer several times over so a
/// shared temp file actually interleaves writers between their write(2)
/// calls — that interleave is exactly the torn document the fixed staging
/// name makes impossible.
constexpr std::size_t kFillerBytes = 512 * 1024;

std::string fillerOf( char c )
{
    return std::string( kFillerBytes, c );
}

} // namespace

TEST_CASE( "concurrent saves of one journal never publish a torn document",
           "[agent_loop][journal][durability]" )
{
    const std::string dir = scratchDir( "concurrent" );
    const std::string sessionId = "sess-race";

    // Each writer pins its own complete expected document; after the round,
    // whatever is on disk must be exactly one of them.
    const SessionJournal writerA = journalWithFiller( sessionId, fillerOf( 'A' ), 1 );
    const SessionJournal writerB = journalWithFiller( sessionId, fillerOf( 'B' ), 2 );

    constexpr int kRounds = 24;
    for ( int round = 0; round < kRounds; ++round )
    {
        std::atomic<bool> aDone{ false };
        std::atomic<bool> bDone{ false };
        std::string errorA, errorB;
        std::thread a( [&] {
            std::string e;
            writerA.save( dir, &e );
            errorA = e;
            aDone.store( true );
        } );
        std::thread b( [&] {
            std::string e;
            writerB.save( dir, &e );
            errorB = e;
            bDone.store( true );
        } );
        a.join();
        b.join();

        INFO( "round " << round << " errorA: " << errorA << " errorB: " << errorB );
        REQUIRE( errorA.empty() );
        REQUIRE( errorB.empty() );

        std::string loadError;
        const auto loaded = SessionJournal::load( dir, sessionId, &loadError );
        if ( !loaded )
        {
            FAIL( "round " << round << ": published journal does not reopen (torn publish?): "
                           << loadError );
        }
        const bool matchesA = loaded->toJson() == writerA.toJson();
        const bool matchesB = loaded->toJson() == writerB.toJson();
        if ( !matchesA && !matchesB )
        {
            FAIL( "round " << round
                           << ": published journal matches neither publisher (torn content)" );
        }
    }
}

TEST_CASE( "journal save→rename→reopen round-trips Unicode and deep paths",
           "[agent_loop][journal][unicode][paths]" )
{
    // Unicode directory components survive the whole publication chain: the
    // path text is UTF-8 by repo convention and must never be re-encoded
    // through a platform narrow conversion.
    const std::string dir =
      ( fs::temp_directory_path() / "sicnu-journal-实验-🌍" / "café" / "nested" ).string();
    std::error_code ec;
    fs::remove_all( fs::temp_directory_path() / "sicnu-journal-实验-🌍", ec );
    fs::create_directories( dir, ec );
    REQUIRE_FALSE( ec );

    SessionJournal journal( "sess-unicode" );
    REQUIRE( journal.append( "note", "execute", Json::Value( Json::objectValue ), 7 ) );

    std::string saveError;
    REQUIRE( journal.save( dir, &saveError ) );
    INFO( "save error: " << saveError );

    std::string loadError;
    const auto loaded = SessionJournal::load( dir, "sess-unicode", &loadError );
    REQUIRE( loaded.has_value() );
    REQUIRE( loadError.empty() );
    REQUIRE( loaded->toJson() == journal.toJson() );

    fs::remove_all( fs::temp_directory_path() / "sicnu-journal-实验-🌍", ec );
}

TEST_CASE( "a failed publish leaves the previous journal untouched",
           "[agent_loop][journal][durability][recovery]" )
{
    const std::string dir = scratchDir( "locked" );

    SessionJournal journal( "sess-keep" );
    REQUIRE( journal.append( "note", "execute", Json::Value( Json::objectValue ), 1 ) );
    std::string error;
    REQUIRE( journal.save( dir, &error ) );
    REQUIRE( error.empty() );

    // Read back the published bytes: this is the document a failed
    // re-publish must preserve byte-for-byte.
    const fs::path target = fs::path( dir ) / "sess-keep.json";
    std::ifstream in( target, std::ios::binary );
    const std::string published( ( std::istreambuf_iterator<char>( in ) ),
                                 std::istreambuf_iterator<char>() );
    in.close();
    REQUIRE( published.size() > 2 );

#if !defined( _WIN32 )
    if ( ::geteuid() == 0 )
        return; // root ignores mode bits: the injection below would not fire
    // Inject the staging failure at the filesystem: a read-only journal
    // directory cannot take a new temp file, so save must fail loudly and
    // the published document must survive untouched (no half-clobbered
    // target, no missing file).
    REQUIRE( ::chmod( dir.c_str(), 0555 ) == 0 );
    std::string republishError;
    const bool republished = journal.save( dir, &republishError );
    REQUIRE( ::chmod( dir.c_str(), 0755 ) == 0 );
    INFO( "republish error: " << republishError );
    REQUIRE_FALSE( republished );
    REQUIRE_FALSE( republishError.empty() );

    std::string reloadError;
    const auto reloaded = SessionJournal::load( dir, "sess-keep", &reloadError );
    REQUIRE( reloaded.has_value() );
    REQUIRE( reloadError.empty() );
    REQUIRE( reloaded->toJson() == journal.toJson() );

    std::ifstream after( target, std::ios::binary );
    const std::string survived( ( std::istreambuf_iterator<char>( after ) ),
                                std::istreambuf_iterator<char>() );
    REQUIRE( survived == published );
#endif
}

TEST_CASE( "over-long journal paths fail gracefully instead of crashing",
           "[agent_loop][journal][longpath]" )
{
    // Build a directory text beyond PATH_MAX: create_directories cannot
    // realize it, so save must return false with an error — never throw,
    // never truncate the path silently.
    std::string dir = ( fs::temp_directory_path() / "sicnu-journal-long" ).string();
    const std::string step = "/step0123456789012345678901234567890123456789";
    while ( dir.size() < 4200 )
        dir += step;
    REQUIRE( dir.size() > 4096 );

    SessionJournal journal( "sess-long" );
    REQUIRE( journal.append( "note", "execute", Json::Value( Json::objectValue ), 1 ) );

    std::string error;
    const bool saved = journal.save( dir, &error );
    if ( saved )
    {
        // A filesystem that allows arbitrary path lengths (some CI overlays):
        // the document must at least reopen. POSIX PATH_MAX hosts take the
        // failure branch below.
        std::string loadError;
        REQUIRE( SessionJournal::load( dir, "sess-long", &loadError ).has_value() );
    }
    else
    {
        INFO( "save error: " << error );
        REQUIRE_FALSE( error.empty() );
    }
}

// test_chunk_resume_11.cpp — WP-C: crash-safe resume of ResumableTileRun.
//
// Oracle independence:
//  - Tile truth is a closed-form kernel written in THIS file
//    (value = ((index*31 + i) % 977) / 977.0f); the baseline used for
//    byte-equality is produced by the test's own writer, never by the
//    driver's machinery.
//  - Crash simulation is two-layered: (a) fault-registry injections that
//    route down the driver's REAL failure branches (journal append, publish,
//    marker), and (b) a REAL child-process kill: this binary re-executes
//    itself with SICNU_TEST_CRASH_AFTER=<k> and hard-exits (_exit) after k
//    committed tiles — no unwinding, no cleanup, exactly a process death.
//    The parent then proves: committed tiles are never recomputed, the
//    output is byte-equal to the truth file, and publication happens once.
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/resumable_tile_run.h"
#include "runtime/chunk/tile_run_contract.h"
#include "runtime/observability/fault_registry.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#if defined( _WIN32 )
#include <windows.h>
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace sicnu::runtime::chunk;

// ---------------------------------------------------------------------------
// Child-process crash hook (runs before Catch2's main via static init).
// ---------------------------------------------------------------------------
namespace
{

int selfPid()
{
#if defined( _WIN32 )
    return static_cast<int>( _getpid() );
#else
    return static_cast<int>( ::getpid() );
#endif
}

std::filesystem::path makeTempDir( const std::string &tag )
{
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path( ec );
    if ( ec )
        base = std::filesystem::current_path();
    static std::atomic<unsigned> counter{ 0 };
    const auto dir = base / ( "sicnu-exec11-" + tag + "-"
                              + std::to_string( selfPid() ) + "-"
                              + std::to_string( counter.fetch_add( 1 ) ) );
    std::filesystem::create_directories( dir, ec );
    return dir;
}

std::string selfExePath()
{
#if defined( _WIN32 )
    char buf[1024];
    const DWORD n = GetModuleFileNameA( nullptr, buf, sizeof( buf ) );
    return n > 0 && n < sizeof( buf ) ? std::string( buf, n ) : std::string();
#else
    std::error_code ec;
    if ( auto p = std::filesystem::read_symlink( "/proc/self/exe", ec ); !ec )
        return p.generic_string();
    return std::string();
#endif
}

std::filesystem::path g_scratchRoot;
std::filesystem::path g_stateBase;
std::filesystem::path g_outputFile;
std::filesystem::path g_truthFile;

struct CrashChildRunner
{
    CrashChildRunner()
    {
        const char *crashAfter = std::getenv( "SICNU_TEST_CRASH_AFTER" );
        if ( !crashAfter || !*crashAfter )
            return;
        const char *root = std::getenv( "SICNU_TEST_SCRATCH_ROOT" );
        const char *state = std::getenv( "SICNU_TEST_STATE_BASE" );
        const char *out = std::getenv( "SICNU_TEST_OUTPUT" );
        if ( !root || !state || !out )
            std::_Exit( 64 );
        const int k = std::atoi( crashAfter );

        TileRunSpec spec = childSpec();
        ResumableTileRun::Config cfg;
        cfg.scratchRoot = root;
        cfg.statePath = state;
        ResumableTileRun run( spec, cfg );

        ResumableTileRun::Callbacks cb;
        std::atomic<int> computes{ 0 };
        cb.compute = [&computes, k]( const TileSpec &s ) {
            const int n = computes.fetch_add( 1 ) + 1;
            if ( n > k )
                std::_Exit( 70 ); // hard process death AFTER k committed tiles
            return truthTile( s );
        };
        std::ofstream output( out, std::ios::binary | std::ios::trunc );
        cb.consume = [&output]( const TilePayload &p ) {
            if ( p.pixels && !p.pixels->empty() )
                output.write( reinterpret_cast<const char *>( p.pixels->data() ),
                              static_cast<std::streamsize>( p.pixels->size()
                                                            * sizeof( float ) ) );
        };
        cb.publish = [&output] { output.flush(); };
        TileRunCancelSource noCancel;
        run.execute( noCancel, cb );
        std::_Exit( 0 );
    }

    static TileRunSpec childSpec()
    {
        TileRunSpec spec;
        spec.identity.operatorIdentity = 0xABCD1234ABCD1234ull;
        spec.identity.inputIdentity = 0x1111222233334444ull;
        spec.identity.partitionDigest = 0;
        spec.partition.rasterWidth = 12;
        spec.partition.rasterHeight = 8;
        spec.partition.tileWidth = 3;
        spec.partition.tileHeight = 2;
        spec.partition.bands = 1;
        spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );
        spec.output.finalPath = "test://child";
        return spec;
    }

    static TilePayload truthTile( const TileSpec &s )
    {
        auto buf = std::make_shared<std::vector<float>>( s.bufferElementCount() );
        for ( size_t i = 0; i < buf->size(); ++i )
            ( *buf )[i] = static_cast<float>( ( static_cast<long long>( s.index ) * 31
                                                + static_cast<long long>( i ) )
                                              % 977 )
                          / 977.0f;
        return TilePayload{ s, std::move( buf ) };
    }
};

// Static init runs the child protocol BEFORE Catch2's main when armed.
// NOLINTNEXTLINE(cert-err58-cpp): intentional test-side global
const CrashChildRunner g_crashChildRunner;

/// Independent truth file: the exact bytes a correct full run must produce.
void writeTruthFile( const std::filesystem::path &path, const TileRunPartition &part )
{
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    for ( std::uint64_t i = 0; i < part.totalTiles(); ++i )
    {
        const TilePayload p = CrashChildRunner::truthTile( tileSpecAt( part, i ) );
        out.write( reinterpret_cast<const char *>( p.pixels->data() ),
                   static_cast<std::streamsize>( p.pixels->size() * sizeof( float ) ) );
    }
}

bool filesByteEqual( const std::filesystem::path &a, const std::filesystem::path &b )
{
    std::ifstream ia( a, std::ios::binary ), ib( b, std::ios::binary );
    if ( !ia || !ib )
        return false;
    std::vector<char> ba( ( std::istreambuf_iterator<char>( ia ) ),
                          std::istreambuf_iterator<char>() );
    std::vector<char> bb( ( std::istreambuf_iterator<char>( ib ) ),
                          std::istreambuf_iterator<char>() );
    return ba == bb;
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE( "Fresh run computes every tile and re-execution publishes nothing",
           "[chunk][resume]" )
{
    const auto dir = makeTempDir( "fresh" );
    TileRunSpec spec = CrashChildRunner::childSpec();
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();

    ResumableTileRun run( spec, cfg );
    std::uint64_t computes = 0, publishes = 0;
    const auto output = dir / "out.bin";
    {
        std::ofstream out( output, std::ios::binary | std::ios::trunc );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            ++computes;
            return CrashChildRunner::truthTile( s );
        };
        cb.consume = [&out]( const TilePayload &p ) {
            out.write( reinterpret_cast<const char *>( p.pixels->data() ),
                       static_cast<std::streamsize>( p.pixels->size() * sizeof( float ) ) );
        };
        cb.publish = [&] {
            ++publishes;
            out.flush();
        };
        TileRunCancelSource noCancel;
        const auto r = run.execute( noCancel, cb );
        REQUIRE( r.totalTiles == spec.partition.totalTiles() );
        REQUIRE( r.tilesComputed == r.totalTiles );
        REQUIRE( r.tilesReused == 0 );
        REQUIRE_FALSE( r.alreadyPublished );
    }
    writeTruthFile( dir / "truth.bin", spec.partition );
    REQUIRE( filesByteEqual( output, dir / "truth.bin" ) );

    // R5: the PUBLISHED marker is the exactly-once gate — a second execution
    // does ZERO kernel work and does not re-publish.
    {
        std::ofstream out( output, std::ios::binary | std::ios::app );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec & ) {
            ++computes;
            return TilePayload{}; // must never run on the marker path
        };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [&] { ++publishes; };
        TileRunCancelSource noCancel;
        const auto r = run.execute( noCancel, cb );
        REQUIRE( r.alreadyPublished );
        REQUIRE( r.tilesComputed == 0 );
        REQUIRE( r.tilesReused == 0 );
    }
    REQUIRE( computes == spec.partition.totalTiles() );
    REQUIRE( publishes == 1 );

    run.cleanupAfterPublish();
    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Crash before publish resumes with zero recomputed tiles (R2)",
           "[chunk][resume]" )
{
    using namespace sicnu::runtime::observability::fault;
    const auto dir = makeTempDir( "prepublish" );
    TileRunSpec spec = CrashChildRunner::childSpec();
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();

    ResumableTileRun run( spec, cfg );
    {
        ArmedFault crash(
            FaultAction{ "exec11.publish", Mode::NextN, 1, "" } );
        ResumableTileRun::Callbacks cb;
        cb.compute = []( const TileSpec &s ) { return CrashChildRunner::truthTile( s ); };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource noCancel;
        bool threw = false;
        try
        {
            run.execute( noCancel, cb );
        }
        catch ( const std::runtime_error & )
        {
            threw = true; // the injected publish failure, real failure path
        }
        REQUIRE( threw );
    }

    // Resume: every tile is committed in the journal; ALL must be reused
    // from verified disk state — the kernel must not run once.
    std::uint64_t computes = 0;
    const auto output = dir / "out.bin";
    std::ofstream out( output, std::ios::binary | std::ios::trunc );
    ResumableTileRun::Callbacks cb2;
    cb2.compute = [&]( const TileSpec & ) {
        ++computes;
        return TilePayload{};
    };
    cb2.consume = [&out]( const TilePayload &p ) {
        out.write( reinterpret_cast<const char *>( p.pixels->data() ),
                   static_cast<std::streamsize>( p.pixels->size() * sizeof( float ) ) );
    };
    cb2.publish = [&out] { out.flush(); };
    TileRunCancelSource noCancel;
    const auto r = run.execute( noCancel, cb2 );
    REQUIRE( r.tilesReused == r.totalTiles );
    REQUIRE( r.tilesComputed == 0 );
    REQUIRE( computes == 0 );
    writeTruthFile( dir / "truth.bin", spec.partition );
    REQUIRE( filesByteEqual( output, dir / "truth.bin" ) );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Cooperative cancel mid-stream keeps committed tiles resumable",
           "[chunk][resume]" )
{
    const auto dir = makeTempDir( "cancelmid" );
    TileRunSpec spec = CrashChildRunner::childSpec();
    const std::uint64_t total = spec.partition.totalTiles();
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();

    ResumableTileRun run( spec, cfg );
    const std::uint64_t cancelAfter = 6;
    std::uint64_t computes = 0;
    {
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            ++computes;
            return CrashChildRunner::truthTile( s );
        };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource cancelAfter6;
        cancelAfter6.predicate = [&] { return computes >= cancelAfter; };
        bool threw = false;
        try
        {
            run.execute( cancelAfter6, cb );
        }
        catch ( const ChunkCancelled & )
        {
            threw = true;
        }
        REQUIRE( threw );
        REQUIRE( computes == cancelAfter );
    }

    // Resume to completion: exactly the un-cancelled remainder computes.
    const auto output = dir / "out.bin";
    std::ofstream out( output, std::ios::binary | std::ios::trunc );
    ResumableTileRun::Callbacks cb2;
    cb2.compute = [&]( const TileSpec &s ) {
        ++computes;
        return CrashChildRunner::truthTile( s );
    };
    cb2.consume = [&out]( const TilePayload &p ) {
        out.write( reinterpret_cast<const char *>( p.pixels->data() ),
                   static_cast<std::streamsize>( p.pixels->size() * sizeof( float ) ) );
    };
    cb2.publish = [&out] { out.flush(); };
    TileRunCancelSource noCancel;
    const auto r = run.execute( noCancel, cb2 );
    REQUIRE( r.tilesReused == cancelAfter );
    REQUIRE( r.tilesComputed == total - cancelAfter );
    REQUIRE( computes == total ); // exactly one compute per tile overall
    writeTruthFile( dir / "truth.bin", spec.partition );
    REQUIRE( filesByteEqual( output, dir / "truth.bin" ) );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Identity drift refuses reuse and re-executes from scratch (R1)",
           "[chunk][resume]" )
{
    const auto dir = makeTempDir( "drift" );
    TileRunSpec spec = CrashChildRunner::childSpec();
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();

    // Establish committed state (crash before publish leaves journal+tiles).
    {
        using namespace sicnu::runtime::observability::fault;
        ArmedFault crash( FaultAction{ "exec11.publish", Mode::NextN, 1, "" } );
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = []( const TileSpec &s ) { return CrashChildRunner::truthTile( s ); };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource noCancel;
        REQUIRE_THROWS( run.execute( noCancel, cb ) );
    }

    // Drift: params changed ⇒ different inputIdentity ⇒ same state paths now
    // belong to a foreign run. NOTHING may be reused.
    spec.identity.inputIdentity ^= 1;
    spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );
    ResumableTileRun drifted( spec, cfg );
    std::uint64_t computes = 0;
    ResumableTileRun::Callbacks cb2;
    cb2.compute = [&]( const TileSpec &s ) {
        ++computes;
        return CrashChildRunner::truthTile( s );
    };
    cb2.consume = []( const TilePayload & ) {};
    cb2.publish = [] {};
    TileRunCancelSource noCancel;
    const auto r = drifted.execute( noCancel, cb2 );
    REQUIRE( r.tilesReused == 0 );
    REQUIRE( r.tilesComputed == spec.partition.totalTiles() );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Journal corruption fails closed; torn tail truncates (R3)",
           "[chunk][resume]" )
{
    const auto dir = makeTempDir( "journal" );
    TileRunSpec spec = CrashChildRunner::childSpec();
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();
    const std::string journal = cfg.statePath + ".journal";

    // Establish committed state.
    {
        using namespace sicnu::runtime::observability::fault;
        ArmedFault crash( FaultAction{ "exec11.publish", Mode::NextN, 1, "" } );
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = []( const TileSpec &s ) { return CrashChildRunner::truthTile( s ); };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource noCancel;
        REQUIRE_THROWS( run.execute( noCancel, cb ) );
    }

    SECTION( "mid-file garbage is a typed failure, never silent" )
    {
        // Rewrite the journal with junk injected after the header.
        std::ifstream in( journal, std::ios::binary );
        std::string header;
        std::getline( in, header );
        in.close();
        std::ofstream out( journal, std::ios::binary | std::ios::trunc );
        out << header << '\n' << "X totally not a commit line\n"
            << "C 0 tile-0.tl\n";
        out.close();

        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = []( const TileSpec &s ) { return CrashChildRunner::truthTile( s ); };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource noCancel;
        REQUIRE_THROWS_AS( run.execute( noCancel, cb ), ChunkCorruptTile );
    }

    SECTION( "torn tail truncates and the run resumes from the last commit" )
    {
        // Truncate the journal mid-last-line (crash during append).
        std::error_code ec;
        const auto size = std::filesystem::file_size( journal, ec );
        REQUIRE_FALSE( ec );
        std::filesystem::resize_file( journal, size - 6, ec ); // cut into "tl\n"
        REQUIRE_FALSE( ec );

        ResumableTileRun run( spec, cfg );
        std::uint64_t computes = 0;
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            ++computes;
            return CrashChildRunner::truthTile( s );
        };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource noCancel;
        const auto r = run.execute( noCancel, cb );
        // All-but-the-torn tile reuse; exactly one tile recomputes.
        REQUIRE( r.tilesComputed == 1 );
        REQUIRE( r.tilesReused == r.totalTiles - 1 );
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Corrupt committed tile self-heals by recomputation (R4)",
           "[chunk][resume]" )
{
    const auto dir = makeTempDir( "selfheal" );
    TileRunSpec spec = CrashChildRunner::childSpec();
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();

    ResumableTileRun run( spec, cfg );
    {
        using namespace sicnu::runtime::observability::fault;
        ArmedFault crash( FaultAction{ "exec11.publish", Mode::NextN, 1, "" } );
        ResumableTileRun::Callbacks cb;
        cb.compute = []( const TileSpec &s ) { return CrashChildRunner::truthTile( s ); };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource noCancel;
        REQUIRE_THROWS( run.execute( noCancel, cb ) );
    }

    // Flip one payload byte of tile 2 (skip the self-describing header).
    const auto tileFile = std::filesystem::path( cfg.scratchRoot )
                          / run.runKey() / "tile-2.tl";
    REQUIRE( std::filesystem::exists( tileFile ) );
    const auto size = std::filesystem::file_size( tileFile );
    std::fstream f( tileFile, std::ios::in | std::ios::out | std::ios::binary );
    f.seekg( static_cast<std::streamoff>( size - 4 ) );
    char b = 0;
    f.read( &b, 1 );
    f.seekp( static_cast<std::streamoff>( size - 4 ) );
    f.write( "\x7f", 1 );
    f.close();

    const auto output = dir / "out.bin";
    std::ofstream out( output, std::ios::binary | std::ios::trunc );
    ResumableTileRun::Callbacks cb2;
    cb2.compute = []( const TileSpec &s ) { return CrashChildRunner::truthTile( s ); };
    cb2.consume = [&out]( const TilePayload &p ) {
        out.write( reinterpret_cast<const char *>( p.pixels->data() ),
                   static_cast<std::streamsize>( p.pixels->size() * sizeof( float ) ) );
    };
    cb2.publish = [&out] { out.flush(); };
    TileRunCancelSource noCancel;
    const auto r = run.execute( noCancel, cb2 );
    REQUIRE( r.tilesComputed == 1 ); // only the corrupted tile
    REQUIRE( r.tilesReused == r.totalTiles - 1 );
    writeTruthFile( dir / "truth.bin", spec.partition );
    REQUIRE( filesByteEqual( output, dir / "truth.bin" ) ); // output still exact

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "abandon wipes resumable state: the run must never resume",
           "[chunk][resume]" )
{
    const auto dir = makeTempDir( "abandon" );
    TileRunSpec spec = CrashChildRunner::childSpec();
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();

    ResumableTileRun run( spec, cfg );
    {
        std::uint64_t computes = 0;
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            if ( ++computes > 6 )
            {
                // Simulate the caller abandoning mid-run.
                throw ChunkCancelled();
            }
            return CrashChildRunner::truthTile( s );
        };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource noCancel;
        REQUIRE_THROWS_AS( run.execute( noCancel, cb ), ChunkCancelled );
    }
    run.abandon();

    // After abandon: no journal, no checkpoint, no tiles; a new execution
    // starts from zero.
    REQUIRE_FALSE( std::filesystem::exists( cfg.statePath + ".journal" ) );
    REQUIRE_FALSE( std::filesystem::exists( cfg.statePath + ".ckpt" ) );
    REQUIRE_FALSE( std::filesystem::exists(
        std::filesystem::path( cfg.scratchRoot ) / run.runKey() ) );
    std::uint64_t computes = 0;
    ResumableTileRun::Callbacks cb2;
    cb2.compute = [&]( const TileSpec &s ) {
        ++computes;
        return CrashChildRunner::truthTile( s );
    };
    cb2.consume = []( const TilePayload & ) {};
    cb2.publish = [] {};
    TileRunCancelSource noCancel;
    const auto r = run.execute( noCancel, cb2 );
    REQUIRE( r.tilesComputed == spec.partition.totalTiles() );
    REQUIRE( r.tilesReused == 0 );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "REAL process crash: committed tiles survive a hard kill (R2)",
           "[chunk][resume][crash]" )
{
    const std::string exe = selfExePath();
    if ( exe.empty() )
    {
        // Cannot resolve the test binary path on this host — record and skip
        // the child-process layer (fault-injection layers above still cover
        // the same windows).
        SUCCEED( "self exe path unavailable; child-process crash layer skipped" );
        return;
    }

    const auto dir = makeTempDir( "crash" );
    TileRunSpec spec = CrashChildRunner::childSpec();
    const std::uint64_t total = spec.partition.totalTiles();
    const std::string scratchRoot = ( dir / "scratch" ).generic_string();
    const std::string statePath = ( dir / "state" / "run" ).generic_string();
    const std::string childOut = ( dir / "child-out.bin" ).generic_string();

    // Arm the child via inherited environment, run it, and require the hard
    // exit code (70 = killed after k committed tiles).
    const std::string setVars =
#if defined( _WIN32 )
        "set SICNU_TEST_CRASH_AFTER=7&& set SICNU_TEST_SCRATCH_ROOT=" + scratchRoot +
        "&& set SICNU_TEST_STATE_BASE=" + statePath + "&& set SICNU_TEST_OUTPUT=" +
        childOut + "&& \"" + exe + "\"";
#else
        "SICNU_TEST_CRASH_AFTER=7 SICNU_TEST_SCRATCH_ROOT='" + scratchRoot +
        "' SICNU_TEST_STATE_BASE='" + statePath + "' SICNU_TEST_OUTPUT='" + childOut +
        "' '" + exe + "'";
#endif
    const int rc = std::system( setVars.c_str() );
    // The shell propagates the child's exit code: require the EXACT
    // hard-crash code so an early death for the wrong reason (missing env,
    // loader failure) cannot pass as a crash.
#if defined( _WIN32 )
    REQUIRE( rc == 70 );
#else
    REQUIRE( WEXITSTATUS( rc ) == 70 );
#endif

    // Resume in THIS process: the 7 committed tiles must be reused from
    // verified disk state; only the remainder computes; output byte-equal.
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = scratchRoot;
    cfg.statePath = statePath;
    ResumableTileRun run( spec, cfg );
    std::uint64_t computes = 0;
    const auto output = dir / "out.bin";
    std::ofstream out( output, std::ios::binary | std::ios::trunc );
    ResumableTileRun::Callbacks cb;
    cb.compute = [&]( const TileSpec &s ) {
        ++computes;
        return CrashChildRunner::truthTile( s );
    };
    cb.consume = [&out]( const TilePayload &p ) {
        out.write( reinterpret_cast<const char *>( p.pixels->data() ),
                   static_cast<std::streamsize>( p.pixels->size() * sizeof( float ) ) );
    };
    cb.publish = [&out] { out.flush(); };
    TileRunCancelSource noCancel;
    const auto r = run.execute( noCancel, cb );
    REQUIRE( r.tilesReused == 7 );
    REQUIRE( r.tilesComputed == total - 7 );
    REQUIRE( computes == total - 7 );
    writeTruthFile( dir / "truth.bin", spec.partition );
    REQUIRE( filesByteEqual( output, dir / "truth.bin" ) );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

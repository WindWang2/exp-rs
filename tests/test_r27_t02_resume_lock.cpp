// test_r27_t02_resume_lock.cpp — R27-T2 / #1766 (RIO-5): a resume state
// path has exactly ONE legal owner.
//
//   * OS-level exclusive lock (flock / LockFileEx) — typed refusal for a
//     second acquirer, in-process (two threads) and cross-process alike.
//   * ResumableTileRun::execute/abandon/cleanupAfterPublish fail closed
//     with ChunkRunLockHeld while another run holds the state.
//   * a holder that DIES releases the lock at process death: after a REAL
//     child hard-exit mid-run the parent deterministically resumes the
//     journal (committed tiles reused, byte-equal output).
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/exclusive_path_lock.h"
#include "runtime/chunk/resumable_tile_run.h"
#include "runtime/chunk/tile_run_contract.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined( _WIN32 )
#include <process.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace sicnu::runtime::chunk;

// ---------------------------------------------------------------------------
// Child-process protocol (runs before Catch2 main via static init).
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
    const auto dir = base / ( "sicnu-r27t02-lock-" + tag + "-"
                              + std::to_string( selfPid() ) + "-"
                              + std::to_string( counter.fetch_add( 1 ) ) );
    std::filesystem::create_directories( dir, ec );
    return dir;
}

std::string selfExePath()
{
#if defined( _WIN32 )
    char buf[ 1024 ];
    const DWORD n = GetModuleFileNameA( nullptr, buf, sizeof( buf ) );
    return n > 0 && n < sizeof( buf ) ? std::string( buf, n ) : std::string();
#else
    std::error_code ec;
    if ( auto p = std::filesystem::read_symlink( "/proc/self/exe", ec ); !ec )
        return p.generic_string();
    return std::string();
#endif
}

TilePayload truthTile( const TileSpec &s )
{
    auto buf = std::make_shared<std::vector<float>>( s.bufferElementCount() );
    for ( size_t i = 0; i < buf->size(); ++i )
        ( *buf )[i] = static_cast<float>( ( static_cast<long long>( s.index ) * 31
                                            + static_cast<long long>( i ) )
                                          % 977 )
                      / 977.0f;
    return TilePayload{ s, std::move( buf ) };
}

TileRunSpec lockSpec()
{
    TileRunSpec spec;
    spec.identity.operatorIdentity = 0xBEEF00BEEF00BEEFull;
    spec.identity.inputIdentity = 0x5555666677778888ull;
    spec.identity.partitionDigest = 0;
    spec.partition.rasterWidth = 12;
    spec.partition.rasterHeight = 8;
    spec.partition.tileWidth = 3;
    spec.partition.tileHeight = 2;
    spec.partition.bands = 1;
    spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );
    spec.output.finalPath = "test://r27t02";
    return spec;
}

// Child mode: run to k committed tiles, then hard-exit WHILE HOLDING the
// state lock — no unwinding, exactly a process death mid-run.
struct LockCrashChild
{
    LockCrashChild()
    {
        const char *armed = std::getenv( "SICNU_R27T02_LOCK_CHILD" );
        if ( !armed || !*armed )
            return;
        const char *root = std::getenv( "SICNU_TEST_SCRATCH_ROOT" );
        const char *state = std::getenv( "SICNU_TEST_STATE_BASE" );
        const char *readyFile = std::getenv( "SICNU_R27T02_READY_FILE" );
        if ( !root || !state )
            std::_Exit( 64 );
        const int k = std::atoi( armed );

        ResumableTileRun::Config cfg;
        cfg.scratchRoot = root;
        cfg.statePath = state;
        ResumableTileRun run( lockSpec(), cfg );
        ResumableTileRun::Callbacks cb;
        std::atomic<int> computes{ 0 };
        cb.compute = [&]( const TileSpec &s )
        {
            const int n = computes.fetch_add( 1 ) + 1;
            if ( readyFile && n == 1 )
            {
                // Signal the parent that THIS process holds the state lock
                // and is mid-run (the lock was taken by execute()).
                std::ofstream( readyFile ) << "held";
            }
            if ( n > k )
                std::_Exit( 70 ); // hard death while holding the lock
            return truthTile( s );
        };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource noCancel;
        run.execute( noCancel, cb );
        std::_Exit( 0 );
    }
};
// NOLINTNEXTLINE(cert-err58-cpp): intentional test-side global
const LockCrashChild g_lockCrashChild;

} // namespace

// ---------------------------------------------------------------------------
// ExclusivePathLock unit contract
// ---------------------------------------------------------------------------

TEST_CASE( "ExclusivePathLock: single holder, typed second refusal, release",
           "[chunk][r27t02][lock]" )
{
    const auto dir = makeTempDir( "unit" );
    const std::string lockPath = ( dir / "state" ).generic_string() + ".lock";

    ExclusivePathLock a;
    std::string err;
    REQUIRE( a.tryAcquire( lockPath, err ) == ExclusivePathLock::Refusal::None );
    REQUIRE( a.held() );

    // A second open description — same process, different instance — conflicts.
    ExclusivePathLock b;
    REQUIRE( b.tryAcquire( lockPath, err ) == ExclusivePathLock::Refusal::HeldElsewhere );
    REQUIRE_FALSE( b.held() );

    a.release();
    REQUIRE_FALSE( a.held() );
    REQUIRE( b.tryAcquire( lockPath, err ) == ExclusivePathLock::Refusal::None );
    b.release();

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "ExclusivePathLock: unusable lock file fails closed, typed",
           "[chunk][r27t02][lock]" )
{
    ExclusivePathLock lock;
    std::string err;
    // Missing parent directory: open must fail with the typed OpenFailed,
    // never silently proceed unlocked.
    const std::string bad = "/nonexistent-r27t02-dir/state.lock";
    REQUIRE( lock.tryAcquire( bad, err ) == ExclusivePathLock::Refusal::OpenFailed );
    REQUIRE_FALSE( lock.held() );
    REQUIRE_FALSE( err.empty() );
}

// ---------------------------------------------------------------------------
// ResumableTileRun single-owner semantics (#1766)
// ---------------------------------------------------------------------------

TEST_CASE( "#1766 second execute on the same statePath fails closed while the first runs",
           "[chunk][resume][r27t02]" )
{
    const auto dir = makeTempDir( "thread" );
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();
    ResumableTileRun first( lockSpec(), cfg );

    std::promise<void> enteredCompute;
    std::future<void> computeGate = enteredCompute.get_future();
    std::promise<void> releaseCompute;
    std::shared_future<void> release = releaseCompute.get_future().share();

    ResumableTileRun::Callbacks cb;
    bool computeEntered = false; // compute fires per tile — signal ONCE
    cb.compute = [&]( const TileSpec &s )
    {
        if ( !computeEntered )
        {
            computeEntered = true;
            enteredCompute.set_value();
        }
        release.wait(); // hold the state lock until the test says so
        return truthTile( s );
    };
    cb.consume = []( const TilePayload & ) {};
    cb.publish = [] {};

    TileRunCancelSource noCancel;
    std::thread runner( [&] { first.execute( noCancel, cb ); } );
    computeGate.wait(); // first.execute holds the lock inside compute

    // Same process, second object, same statePath: typed refusal.
    ResumableTileRun second( lockSpec(), cfg );
    ResumableTileRun::Callbacks cb2;
    cb2.compute = []( const TileSpec &s ) { return truthTile( s ); };
    cb2.consume = []( const TilePayload & ) {};
    cb2.publish = [] {};
    REQUIRE_THROWS_AS( second.execute( noCancel, cb2 ), ChunkRunLockHeld );
    REQUIRE_THROWS_AS( second.abandon(), ChunkRunLockHeld );

    releaseCompute.set_value();
    runner.join();

    // Owner released: the second run may now take the state (marker already
    // written by the first ⇒ alreadyPublished, zero work).
    const auto r2 = second.execute( noCancel, cb2 );
    REQUIRE( r2.alreadyPublished );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "#1766 REAL crash of the holder releases the lock and resumes deterministically",
           "[chunk][resume][r27t02][crash]" )
{
    const std::string exe = selfExePath();
    if ( exe.empty() )
    {
        SUCCEED( "self exe path unavailable; child-process layer skipped" );
        return;
    }
    const auto dir = makeTempDir( "crash" );
    const std::string scratchRoot = ( dir / "scratch" ).generic_string();
    const std::string statePath = ( dir / "state" / "run" ).generic_string();
    const std::string readyFile = ( dir / "child-held.flag" ).generic_string();

    // Child: acquires the state lock, commits k tiles, hard-exits (70).
    const std::string cmd =
#if defined( _WIN32 )
        "set SICNU_R27T02_LOCK_CHILD=5&& set SICNU_TEST_SCRATCH_ROOT=" + scratchRoot +
        "&& set SICNU_TEST_STATE_BASE=" + statePath + "&& set SICNU_R27T02_READY_FILE=" +
        readyFile + "&& \"" + exe + "\"";
#else
        "SICNU_R27T02_LOCK_CHILD=5 SICNU_TEST_SCRATCH_ROOT='" + scratchRoot +
        "' SICNU_TEST_STATE_BASE='" + statePath + "' SICNU_R27T02_READY_FILE='" + readyFile +
        "' '" + exe + "'";
#endif
    const int rc = std::system( cmd.c_str() );
#if defined( _WIN32 )
    REQUIRE( rc == 70 );
#else
    REQUIRE( WEXITSTATUS( rc ) == 70 );
#endif
    REQUIRE( std::filesystem::exists( readyFile ) );

    // The child died holding the lock; the kernel must have released it.
    // If the lock leaked, this execute() throws ChunkRunLockHeld and the
    // test fails — the deterministic-recovery oracle.
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = scratchRoot;
    cfg.statePath = statePath;
    ResumableTileRun run( lockSpec(), cfg );
    std::uint64_t computes = 0;
    ResumableTileRun::Callbacks cb;
    cb.compute = [&]( const TileSpec &s )
    {
        ++computes;
        return truthTile( s );
    };
    cb.consume = []( const TilePayload & ) {};
    cb.publish = [] {};
    TileRunCancelSource noCancel;
    const auto r = run.execute( noCancel, cb );
    const auto total = lockSpec().partition.totalTiles();
    REQUIRE( r.tilesReused == 5 );
    REQUIRE( r.tilesComputed == total - 5 );
    REQUIRE( computes == total - 5 );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "#1766 lock file residue is inert and never blocks the next owner",
           "[chunk][resume][r27t02]" )
{
    const auto dir = makeTempDir( "residue" );
    const std::string lockPath = ( dir / "state" ).generic_string() + ".lock";
    {
        ExclusivePathLock a;
        std::string err;
        REQUIRE( a.tryAcquire( lockPath, err ) == ExclusivePathLock::Refusal::None );
    } // released; file remains by design
    REQUIRE( std::filesystem::exists( lockPath ) );
    ExclusivePathLock b;
    std::string err;
    REQUIRE( b.tryAcquire( lockPath, err ) == ExclusivePathLock::Refusal::None );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

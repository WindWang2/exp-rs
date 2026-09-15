// test_execution_scale_fault_11.cpp — WP-H: scale and fault hermetic matrix.
//
// Invariants are EXACT (counts, digests, byte-equality) — no wall-clock
// gates. Logical 10^6-tile plans are pure arithmetic (opt-in full replay via
// SICNU_SCALE_11=1); the default suite keeps bounded logical scale with real
// execution. The cancel storm uses SEEDED deterministic cancel points, so a
// failure reproduces bit-for-bit.
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/resumable_tile_run.h"
#include "runtime/chunk/tile_run_contract.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace sicnu::runtime::chunk;

#ifdef _WIN32
#include <process.h>
static int selfPid() { return static_cast<int>( _getpid() ); }
#else
#include <unistd.h>
static int selfPid() { return static_cast<int>( ::getpid() ); }
#endif

namespace
{

std::filesystem::path makeTempDir( const char *tag )
{
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path( ec );
    if ( ec )
        base = std::filesystem::current_path();
    static std::atomic<unsigned> n{ 0 };
    const auto dir = base / ( "sicnu-scale11-" + std::string( tag ) + "-"
                              + std::to_string( selfPid() ) + std::to_string( n++ ) );
    std::filesystem::create_directories( dir, ec );
    return dir;
}

TileRunSpec smallSpec( std::uint64_t tilesAcross, std::uint64_t tilesDown )
{
    TileRunSpec spec;
    spec.identity.operatorIdentity = 0x5CA1E11A11ull;
    spec.identity.inputIdentity = 0x11CA1CADAull;
    spec.partition.rasterWidth = static_cast<int>( tilesAcross * 3 );
    spec.partition.rasterHeight = static_cast<int>( tilesDown * 2 );
    spec.partition.tileWidth = 3;
    spec.partition.tileHeight = 2;
    spec.partition.bands = 1;
    spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );
    return spec;
}

TilePayload scaleKernel( const TileSpec &s )
{
    auto buf = std::make_shared<std::vector<float>>( s.bufferElementCount() );
    for ( size_t i = 0; i < buf->size(); ++i )
        ( *buf )[i] = static_cast<float>( ( static_cast<long long>( s.index ) * 13
                                            + static_cast<long long>( i ) )
                                          % 101 )
                      / 101.0f;
    return TilePayload{ s, std::move( buf ) };
}

} // namespace

TEST_CASE( "Million logical tiles stay arithmetic (no materialization)",
           "[execution][scale]" )
{
    // 32000x32000 raster at 32x32 tiles = 1,000,000 logical tiles.
    TileRunPartition p;
    p.rasterWidth = 32000;
    p.rasterHeight = 32000;
    p.tileWidth = 32;
    p.tileHeight = 32;
    p.bands = 1;

    REQUIRE( p.totalTiles() == 1'000'000ull );
    // O(1) geometry: any tile index resolves without a grid vector.
    const TileSpec corner = tileSpecAt( p, 999'999 );
    REQUIRE( corner.index == 999'999 );
    REQUIRE( corner.totalTiles == 1'000'000 );
    REQUIRE( corner.xOffset == 31968 );
    REQUIRE( corner.yOffset == 31968 );
    REQUIRE( corner.width == 32 );
    REQUIRE( corner.height == 32 );

    // Identity math is stable at scale (same digest twice, cheap).
    const auto d1 = tileRunPartitionDigest( p );
    REQUIRE( d1 == tileRunPartitionDigest( p ) );
    REQUIRE( d1 != 0 );

    // tileSpecAt agrees with the materializing builder on a small grid —
    // the scale path inherits the tested math.
    TileRunPartition small = p;
    small.rasterWidth = 70;
    small.rasterHeight = 50;
    const auto grid = buildTileGrid( 70, 50, 32, 32, 0, 1 );
    REQUIRE( grid.size() == small.totalTiles() );
    for ( std::uint64_t i = 0; i < small.totalTiles(); ++i )
    {
        const TileSpec a = tileSpecAt( small, i );
        const TileSpec &b = grid[static_cast<size_t>( i )];
        REQUIRE( a.index == b.index );
        REQUIRE( a.totalTiles == b.totalTiles );
        REQUIRE( a.xOffset == b.xOffset );
        REQUIRE( a.yOffset == b.yOffset );
        REQUIRE( a.width == b.width );
        REQUIRE( a.height == b.height );
        REQUIRE( a.bufferWidth == b.bufferWidth );
        REQUIRE( a.bufferHeight == b.bufferHeight );
        REQUIRE( a.bands == b.bands );
    }
}

TEST_CASE( "Cancel storm: seeded random cancels, then completion is exact",
           "[execution][fault]" )
{
    const auto dir = makeTempDir( "storm" );
    const std::uint64_t total = 80; // 10x8 grid
    TileRunSpec spec = smallSpec( 10, 8 );
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "storm" ).generic_string();
    cfg.checkpointIntervalTiles = 8;

    std::vector<float> output;
    std::uint64_t kernelCalls = 0;

    auto runOnce = [&]( std::function<bool()> cancelPredicate ) {
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            ++kernelCalls;
            return scaleKernel( s );
        };
        cb.consume = [&]( const TilePayload &p ) {
            output.insert( output.end(), p.pixels->begin(), p.pixels->end() );
        };
        cb.publish = [] {};
        TileRunCancelSource cancel;
        cancel.predicate = cancelPredicate;
        return run.execute( cancel, cb );
    };

    // Counted storm: same seed, real per-round cancel counters.
    std::mt19937 rng2( 20260915 );
    for ( int round = 0; round < 8; ++round )
    {
        const std::uint64_t cancelAt = 1 + rng2() % static_cast<unsigned>( total - 2 );
        // Count CONSUMED tiles (reused or computed): rounds after the first
        // start with a committed prefix and would never match a
        // compute-only predicate.
        std::uint64_t consumed = 0;
        output.clear();
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            ++kernelCalls;
            return scaleKernel( s );
        };
        cb.consume = [&]( const TilePayload &p ) {
            ++consumed;
            output.insert( output.end(), p.pixels->begin(), p.pixels->end() );
        };
        cb.publish = [] {};
        TileRunCancelSource cancel;
        cancel.predicate = [&] { return consumed >= cancelAt; };
        REQUIRE_THROWS_AS( run.execute( cancel, cb ), ChunkCancelled );
    }

    // Completion after the storm: output byte-equal to the full truth and
    // each tile computed exactly once overall (kernelCalls == total + storm
    // recomputes of uncommitted cancels; the EXACT invariant is that the
    // final round computed ZERO tiles that were already committed).
    const std::uint64_t callsBeforeFinal = kernelCalls;
    output.clear();
    const auto r = runOnce( {} );
    REQUIRE( r.tilesComputed + r.tilesReused == total );
    REQUIRE( r.tilesReused > 0 ); // the storm left committed tiles behind
    REQUIRE( kernelCalls - callsBeforeFinal == r.tilesComputed );

    // Truth comparison: independent assembly of the expected bytes.
    std::vector<float> truth;
    for ( std::uint64_t t = 0; t < total; ++t )
    {
        const TileSpec s = tileSpecAt( spec.partition, t );
        const TilePayload p = scaleKernel( s );
        truth.insert( truth.end(), p.pixels->begin(), p.pixels->end() );
    }
    REQUIRE( output == truth );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Intermittent hard-state resets converge to exactly-once publication",
           "[execution][fault]" )
{
    const auto dir = makeTempDir( "resets" );
    const std::uint64_t total = 40; // 8x5
    TileRunSpec spec = smallSpec( 8, 5 );
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "resets" ).generic_string();
    cfg.checkpointIntervalTiles = 5;

    std::uint64_t kernelCalls = 0;
    std::vector<float> output;

    // Three "crash-like" rounds: run until k tiles computed, then abandon
    // the CALLER side only (simulate by cancelling; disk state stays).
    const std::uint64_t stopAt[] = { 7, 19, 31 };
    for ( const std::uint64_t k : stopAt )
    {
        // Consumed-counted stop point (committed prefixes make compute-only
        // predicates unreachable in later rounds).
        std::uint64_t consumed = 0;
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            ++kernelCalls;
            return scaleKernel( s );
        };
        cb.consume = [&]( const TilePayload & ) { ++consumed; };
        cb.publish = [] {};
        TileRunCancelSource cancel;
        cancel.predicate = [&] { return consumed >= k; };
        REQUIRE_THROWS_AS( run.execute( cancel, cb ), ChunkCancelled );
    }

    // Final round to completion + a redundant re-execution (marker).
    std::uint64_t publishes = 0;
    {
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            ++kernelCalls;
            return scaleKernel( s );
        };
        cb.consume = [&]( const TilePayload &p ) {
            output.insert( output.end(), p.pixels->begin(), p.pixels->end() );
        };
        cb.publish = [&] { ++publishes; };
        TileRunCancelSource noCancel;
        const auto r = run.execute( noCancel, cb );
        REQUIRE( r.tilesComputed + r.tilesReused == total );
        REQUIRE( publishes == 1 );
    }
    {
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            ++kernelCalls;
            return scaleKernel( s );
        };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [&] { ++publishes; };
        TileRunCancelSource noCancel;
        const auto r = run.execute( noCancel, cb );
        REQUIRE( r.alreadyPublished );
        REQUIRE( r.tilesComputed == 0 );
        REQUIRE( publishes == 1 ); // exactly-once publication held
    }

    std::vector<float> truth;
    for ( std::uint64_t t = 0; t < total; ++t )
    {
        const TilePayload p = scaleKernel( tileSpecAt( spec.partition, t ) );
        truth.insert( truth.end(), p.pixels->begin(), p.pixels->end() );
    }
    REQUIRE( output == truth );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Opt-in scale replay: large journal round-trips exactly (SICNU_SCALE_11=1)",
           "[execution][scale]" )
{
    if ( !std::getenv( "SICNU_SCALE_11" ) )
    {
        SUCCEED( "opt-in scale gate not set; skipped (daily gate uses bounded scale)" );
        return;
    }
    // 100k real tiles, tiny payloads: journal + checkpoint round-trip with
    // one mid-way hard reset; exact reuse accounting throughout.
    const auto dir = makeTempDir( "scale100k" );
    const std::uint64_t total = 100'000; // 250x400 grid
    TileRunSpec spec = smallSpec( 250, 400 );
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "scale" ).generic_string();
    cfg.checkpointIntervalTiles = 1000;

    std::uint64_t kernelCalls = 0;
    {
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            ++kernelCalls;
            return scaleKernel( s );
        };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        std::uint64_t consumed = 0;
        cb.consume = [&consumed]( const TilePayload & ) { ++consumed; };
        TileRunCancelSource cancel;
        cancel.predicate = [&] { return consumed >= 37'000; };
        REQUIRE_THROWS_AS( run.execute( cancel, cb ), ChunkCancelled );
    }
    {
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            ++kernelCalls;
            return scaleKernel( s );
        };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource noCancel;
        const auto r = run.execute( noCancel, cb );
        REQUIRE( r.totalTiles == total );
        REQUIRE( r.tilesReused + r.tilesComputed == total );
        // The 37k committed tiles were NOT recomputed; total kernel calls
        // stayed under total + the cancel-point slack.
        REQUIRE( r.tilesReused >= 37'000 );
        REQUIRE( kernelCalls < total + 1'000 );
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

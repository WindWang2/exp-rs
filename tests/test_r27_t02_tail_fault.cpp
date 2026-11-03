// test_r27_t02_tail_fault.cpp — R27-T2 milestone E verification: tail-batch
// exactness and injected-failure recovery over the production tile runtime
// (ResumableTileRun + the exec11 fault seams). No new production code —
// these are the acceptance oracles:
//
//   * a NON-multiple tile grid (raster 100×60, tiles 32×32 → tail tiles
//     4×28 and 32×28) loses nothing: the reassembled stream has exactly
//     rasterWidth×rasterHeight pixels and is byte-equal to the truth file.
//   * an injected journal-append failure surfaces typed, leaves resumable
//     state behind, and a retry RESUMES (committed tiles reused, not
//     recomputed).
//   * cooperative cancellation leaves the journal consistent and resumable.
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/resumable_tile_run.h"
#include "runtime/chunk/tile_run_contract.h"
#include "runtime/observability/fault_registry.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace sicnu::runtime::chunk;
using sicnu::runtime::observability::fault::ArmedFault;
using sicnu::runtime::observability::fault::Mode;

namespace
{

std::filesystem::path makeTempDir( const std::string &tag )
{
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path( ec );
    static std::atomic<unsigned> counter{ 0 };
    const auto dir = base / ( "sicnu-r27t02-e2e-" + tag + "-"
                              + std::to_string( counter.fetch_add( 1 ) ) );
    std::filesystem::create_directories( dir, ec );
    return dir;
}

TilePayload truthTile( const TileSpec &s )
{
    auto buf = std::make_shared<std::vector<float>>( s.bufferElementCount() );
    for ( std::size_t i = 0; i < buf->size(); ++i )
        ( *buf )[i] = static_cast<float>( ( static_cast<long long>( s.index ) * 131
                                            + static_cast<long long>( i ) )
                                          % 997 )
                      / 997.0f;
    return TilePayload{ s, std::move( buf ) };
}

/// 100×60 raster, 32×32 tiles: 4×2 grid with tail tiles 4 wide / 28 tall —
/// the non-multiple shape whose tail silently vanished in the #1719-era bug.
TileRunSpec tailSpec()
{
    TileRunSpec spec;
    spec.identity.operatorIdentity = 0xE2E2E2E2E2E2E2E2ull;
    spec.identity.inputIdentity = 0x1717171717171717ull;
    spec.identity.partitionDigest = 0;
    spec.partition.rasterWidth = 100;
    spec.partition.rasterHeight = 60;
    spec.partition.tileWidth = 32;
    spec.partition.tileHeight = 32;
    spec.partition.bands = 1;
    spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );
    spec.output.finalPath = "test://r27t02-tail";
    return spec;
}

std::uint64_t totalPixels( const TileRunPartition &part )
{
    std::uint64_t px = 0;
    for ( std::uint64_t i = 0; i < part.totalTiles(); ++i )
        px += static_cast<std::uint64_t>( tileSpecAt( part, i ).bufferElementCount() );
    return px;
}

} // namespace

TEST_CASE( "E: tail batch (non-multiple grid) loses no pixels and is byte-exact",
           "[chunk][r27t02][tail]" )
{
    const auto dir = makeTempDir( "tail" );
    const TileRunSpec spec = tailSpec();
    REQUIRE( spec.partition.tilesAcross() == 4 );
    REQUIRE( spec.partition.tilesDown() == 2 );
    // The grid really has tails: last column 4 px wide, last row 28 px tall.
    REQUIRE( tileSpecAt( spec.partition, 3 ).width == 4 );
    REQUIRE( tileSpecAt( spec.partition, 4 ).height == 28 );

    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();
    ResumableTileRun run( spec, cfg );

    std::vector<float> reassembled;
    std::uint64_t pixelCount = 0;
    ResumableTileRun::Callbacks cb;
    cb.compute = []( const TileSpec &s ) { return truthTile( s ); };
    cb.consume = [&]( const TilePayload &p )
    {
        pixelCount += p.pixels->size();
        reassembled.insert( reassembled.end(), p.pixels->begin(), p.pixels->end() );
    };
    cb.publish = [] {};
    TileRunCancelSource noCancel;
    const auto r = run.execute( noCancel, cb );

    REQUIRE( r.tilesComputed == spec.partition.totalTiles() );
    // Pixel-count conservation: sum over tiles == raster pixels (the tail
    // acceptance oracle — a dropped tail tile breaks this identity).
    REQUIRE( pixelCount == 100ull * 60ull );
    REQUIRE( pixelCount == totalPixels( spec.partition ) );
    // Byte-exact against the same kernel walked in index order.
    std::vector<float> truth;
    for ( std::uint64_t i = 0; i < spec.partition.totalTiles(); ++i )
    {
        const TilePayload p = truthTile( tileSpecAt( spec.partition, i ) );
        truth.insert( truth.end(), p.pixels->begin(), p.pixels->end() );
    }
    REQUIRE( reassembled == truth );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "E: injected journal failure is typed and resumable without recompute",
           "[chunk][r27t02][fault]" )
{
    const auto dir = makeTempDir( "fault" );
    const TileRunSpec spec = tailSpec();
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();
    ResumableTileRun run( spec, cfg );

    std::uint64_t computes = 0;
    ResumableTileRun::Callbacks cb;
    cb.compute = [&]( const TileSpec &s )
    {
        ++computes;
        return truthTile( s );
    };
    cb.consume = []( const TilePayload & ) {};
    cb.publish = [] {};

    // The journal append fails on its FIRST firing: one tile computes and
    // writes, the commit record fails, execute() throws typed — resumable
    // state stays behind (R6/contract).
    {
        ArmedFault fault( { "exec11.journalAppend", Mode::NextN, 1, {} } );
        TileRunCancelSource noCancel;
        REQUIRE_THROWS_AS( run.execute( noCancel, cb ), std::runtime_error );
    }

    // Retry: the committed prefix is reused from verified disk state; only
    // the remainder computes; the run completes.
    TileRunCancelSource noCancel;
    const auto r = run.execute( noCancel, cb );
    REQUIRE( r.tilesComputed + r.tilesReused == spec.partition.totalTiles() );
    // Exactly one tile was computed twice (the one whose commit failed).
    REQUIRE( computes == spec.partition.totalTiles() + 1 );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "E: cooperative cancellation leaves a consistent, resumable journal",
           "[chunk][r27t02][cancel]" )
{
    const auto dir = makeTempDir( "cancel" );
    const TileRunSpec spec = tailSpec();
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();
    ResumableTileRun run( spec, cfg );

    std::uint64_t computes = 0;
    ResumableTileRun::Callbacks cb;
    cb.compute = [&]( const TileSpec &s )
    {
        ++computes;
        return truthTile( s );
    };
    cb.consume = []( const TilePayload & ) {};
    cb.publish = [] {};

    // Cancel after three committed tiles: ChunkCancelled, state on disk.
    // The driver polls cancelled() between tiles; the predicate flips after
    // the third COMPUTE (the 4th pick aborts — wait, TileRunCancelSource
    // semantics: cancelled() observed between tiles; three tiles commit).
    TileRunCancelSource cancelSrc;
    std::uint64_t computesAtCancel = 0;
    std::atomic<bool> flip{ false };
    cb.compute = [&]( const TileSpec &s )
    {
        const TilePayload p = truthTile( s );
        ++computes;
        if ( computes == 3 )
            flip.store( true ); // cancel once the third tile computed
        return p;
    };
    cancelSrc.flag = &flip;
    REQUIRE_THROWS_AS( run.execute( cancelSrc, cb ), ChunkCancelled );
    REQUIRE( computes == 3 );

    // Resume to completion: the 3 committed tiles are reused, not recomputed.
    TileRunCancelSource noCancel;
    const auto r = run.execute( noCancel, cb );
    REQUIRE( r.tilesReused == 3 );
    REQUIRE( r.tilesComputed == spec.partition.totalTiles() - 3 );
    REQUIRE( computes == spec.partition.totalTiles() );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// test_runtime_chunk_boundaries_r4.cpp — Track 15 WP-B, chunk subdomain.
// Boundary oracles for the chunk execution plane: the partition arithmetic
// (typed at the zero/negative/overflow boundaries, mirroring buildTileGrid's
// #1056 discipline), tileSpecAt range gates, the graph source's null-buffer
// contract breach (typed, never UB), the documented completedTiles delivery
// semantics, the zero-byte journal bootstrap, and the two previously
// un-armed fault points (journalAppend / marker) at their real failure
// branches. Resume expectations come from resumable_tile_run.h's R1-R6 laws,
// never from implementation internals.
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/chunk_graph.h"
#include "runtime/chunk/resumable_tile_run.h"
#include "runtime/chunk/tile_run_contract.h"
#include "runtime/observability/fault_registry.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

using namespace sicnu::runtime::chunk;
namespace fault = sicnu::runtime::observability::fault;

namespace
{
TileRunPartition fourTilePartition()
{
    TileRunPartition partition;
    partition.rasterWidth = 4;
    partition.rasterHeight = 4;
    partition.tileWidth = 2;
    partition.tileHeight = 2;
    return partition; // 2x2 tiles = 4
}

TileRunSpec makeSpec( const TileRunPartition &partition )
{
    TileRunSpec spec;
    spec.partition = partition;
    spec.identity.operatorIdentity = 0xC0FFEE;
    spec.identity.inputIdentity = 0x42;
    spec.identity.partitionDigest = tileRunPartitionDigest( partition );
    spec.output.finalPath = "unused";
    return spec;
}

std::filesystem::path scratchDir( const std::string &tag )
{
    const auto dir = std::filesystem::temp_directory_path()
                     / ( "r4-chunk-" + tag + "-"
                         + std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() ) );
    std::filesystem::create_directories( dir );
    return dir;
}

TilePayload makeTile( const TileSpec &spec )
{
    return TilePayload{ spec,
                        std::make_shared<std::vector<float>>( spec.bufferElementCount(), 1.0f ) };
}

// Drives one full run; counts compute/consume/publish invocations.
struct RunCounters
{
    int computeCalls = 0;
    int consumeCalls = 0;
    int publishCalls = 0;
};

ResumableTileRun::Callbacks countingCallbacks( RunCounters &counters )
{
    ResumableTileRun::Callbacks callbacks;
    callbacks.compute = [ &counters ]( const TileSpec &spec ) {
        ++counters.computeCalls;
        return makeTile( spec );
    };
    callbacks.consume = [ &counters ]( const TilePayload & ) { ++counters.consumeCalls; };
    callbacks.publish = [ &counters ] { ++counters.publishCalls; };
    return callbacks;
}
} // namespace

TEST_CASE( "Partition arithmetic is typed at the zero, negative and overflow boundaries",
           "[runtime][chunk][r4]" )
{
    // Sanity: the ordinary grid arithmetic stays exact.
    TileRunPartition ordinary = fourTilePartition();
    REQUIRE( ordinary.tilesAcross() == 2 );
    REQUIRE( ordinary.tilesDown() == 2 );
    REQUIRE( ordinary.totalTiles() == 4 );

    // Zero tile dimension must be a typed refusal, not a division by zero.
    TileRunPartition zeroTile = fourTilePartition();
    zeroTile.tileWidth = 0;
    REQUIRE_THROWS_AS( zeroTile.tilesAcross(), std::invalid_argument );
    zeroTile.tileWidth = 2;
    zeroTile.tileHeight = 0;
    REQUIRE_THROWS_AS( zeroTile.tilesDown(), std::invalid_argument );

    // Non-positive raster extents are equally invalid.
    TileRunPartition zeroRaster = fourTilePartition();
    zeroRaster.rasterWidth = 0;
    REQUIRE_THROWS_AS( zeroRaster.tilesAcross(), std::invalid_argument );
    zeroRaster.rasterWidth = 4;
    zeroRaster.rasterHeight = -1;
    REQUIRE_THROWS_AS( zeroRaster.tilesDown(), std::invalid_argument );

    // INT_MAX extents with unit tiles are representable per axis...
    TileRunPartition huge = fourTilePartition();
    huge.rasterWidth = std::numeric_limits<int>::max();
    huge.rasterHeight = std::numeric_limits<int>::max();
    huge.tileWidth = 1;
    huge.tileHeight = 1;
    REQUIRE( huge.tilesAcross() == std::numeric_limits<int>::max() );
    REQUIRE( huge.tilesDown() == std::numeric_limits<int>::max() );
    // ...but the product leaves the int tile-index domain: typed overflow
    // (same contract as buildTileGrid, #1056), never a wrapped negative.
    REQUIRE_THROWS_AS( huge.totalTiles(), std::overflow_error );
}

TEST_CASE( "tileSpecAt refuses out-of-range indices and int-domain overflow",
           "[runtime][chunk][r4]" )
{
    const TileRunPartition partition = fourTilePartition();

    // Equivalent to buildTileGrid for every valid index (spot check).
    const TileSpec first = tileSpecAt( partition, 0 );
    REQUIRE( first.index == 0 );
    REQUIRE( first.totalTiles == 4 );
    REQUIRE( first.width == 2 );
    const TileSpec last = tileSpecAt( partition, 3 );
    REQUIRE( last.xOffset == 2 );
    REQUIRE( last.yOffset == 2 );

    // The partition's end is a typed boundary: index == total is out of range
    // (previously it silently computed a negative-width tile).
    REQUIRE_THROWS_AS( tileSpecAt( partition, 4 ), std::invalid_argument );
    REQUIRE_THROWS_AS( tileSpecAt( partition, 1'000'000 ), std::invalid_argument );

    // A grid whose product leaves the int domain is refused, not truncated.
    TileRunPartition huge = fourTilePartition();
    huge.rasterWidth = std::numeric_limits<int>::max();
    huge.rasterHeight = std::numeric_limits<int>::max();
    huge.tileWidth = 1;
    huge.tileHeight = 1;
    REQUIRE_THROWS_AS( tileSpecAt( huge, 0 ), std::overflow_error );
}

TEST_CASE( "A graph source breaching the buffer contract is a typed error, never undefined behavior",
           "[runtime][chunk][r4]" )
{
    ChunkGraph graph;
    graph.addSource( []( TilePayload &payload ) {
        // Contract breach: "true" promises a tile, but no pixel buffer is
        // attached. The gate must throw a typed logic_error (same family as
        // ChunkPipeline::validateBuffer), not dereference null.
        payload.spec.totalTiles = 1;
        payload.spec.bufferWidth = 2;
        payload.spec.bufferHeight = 2;
        payload.pixels = nullptr;
        return true;
    } );
    int consumed = 0;
    graph.addSink( 0, [ &consumed ]( TilePayload && ) {
        ++consumed;
        return true;
    } );
    REQUIRE_THROWS_AS( graph.run(), std::logic_error );
    REQUIRE( consumed == 0 );
}

TEST_CASE( "completedTiles counts payloads delivered to the sink, including an aborting final tile",
           "[runtime][chunk][r4]" )
{
    // Documented semantics (chunk_graph.h): every payload that LEAVES the
    // final queue bumps completedTiles — the counter is a delivery counter,
    // not an acknowledgement counter (mirrors ChunkPipeline).
    ChunkGraph graph;
    std::atomic<int> emitted{ 0 };
    graph.addSource( [ &emitted ]( TilePayload &payload ) {
        if ( emitted.load() >= 3 )
            return false;
        payload = makeTile( TileSpec{} );
        payload.spec.index = emitted.fetch_add( 1 );
        return true;
    } );
    // The sink owns its abort decision: it consumes tiles 1 and 2 and aborts
    // on the third — decoupled from the source's emission counter so the
    // observation is deterministic regardless of queue timing.
    std::atomic<int> consumed{ 0 };
    graph.addSink( 0, [ &consumed ]( TilePayload && ) {
        return consumed.fetch_add( 1 ) + 1 < 3;
    } );

    REQUIRE_THROWS_AS( graph.run(), ChunkGraphCancelled );
    REQUIRE( graph.completedTiles() == 3 );
}

TEST_CASE( "A zero-byte journal is re-headed on resume, never continued headerless",
           "[runtime][chunk][r4]" )
{
    const auto dir = scratchDir( "journal-zero" );
    const std::string statePath = ( dir / "run" ).string();
    const std::string scratchRoot = dir.string();

    TileRunSpec spec = makeSpec( fourTilePartition() );
    // First pass: every tile commits, then publication fails (armed fault at
    // the real publish branch) — so there is NO marker and the journal is
    // the only resume truth.
    {
        const fault::ArmedFault crash(
            fault::FaultAction{ "exec11.publish", fault::Mode::NextN, 1, "" } );
        RunCounters first;
        ResumableTileRun run( spec, { scratchRoot, statePath, 0 } );
        REQUIRE_THROWS_AS( run.execute( {}, countingCallbacks( first ) ), std::runtime_error );
        // The fault point sits BEFORE the publish callback: the injection
        // aborts publication, so zero publishes ran.
        REQUIRE( first.publishCalls == 0 );
    }

    // Crash between journal creation and the first flush: a 0-byte journal.
    {
        std::error_code ec;
        std::filesystem::resize_file( statePath + ".journal", 0, ec );
        REQUIRE_FALSE( ec );
    }

    RunCounters second;
    {
        // The journal is the resume truth: a headerless (empty) journal
        // claims nothing committed, so every tile recomputes — and the next
        // commit must re-write the identity header (review P1 fix), keeping
        // the file a valid journal rather than a headerless one.
        ResumableTileRun run( spec, { scratchRoot, statePath, 0 } );
        const ResumableTileRun::Result result = run.execute( {}, countingCallbacks( second ) );
        REQUIRE( result.tilesComputed == 4 );
        REQUIRE( result.tilesReused == 0 );
    }

    std::ifstream journal( statePath + ".journal", std::ios::binary );
    REQUIRE( journal.is_open() );
    std::string header;
    std::getline( journal, header );
    REQUIRE( header.rfind( "V 1 ", 0 ) == 0 );
    REQUIRE( header.find( tileRunIdentityKey( spec.identity ) ) != std::string::npos );

    // The healed journal resumes cleanly and the marker short-circuits (R5).
    RunCounters third;
    ResumableTileRun run( spec, { scratchRoot, statePath, 0 } );
    const ResumableTileRun::Result done = run.execute( {}, countingCallbacks( third ) );
    REQUIRE( done.alreadyPublished );
    REQUIRE( third.computeCalls == 0 );
    REQUIRE( third.publishCalls == 0 );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "An injected journal-append failure commits nothing for that tile: resume recomputes it",
           "[runtime][chunk][r4]" )
{
    const auto dir = scratchDir( "journal-fault" );
    const std::string statePath = ( dir / "run" ).string();
    const std::string scratchRoot = dir.string();
    const TileRunSpec spec = makeSpec( fourTilePartition() );

    {
        // EveryNth=2: the FIRST journal append succeeds (tile 0 commits),
        // the SECOND throws at the real fault site — the journal then holds
        // exactly one commit line. Resume law R2 says the journal, not the
        // tile files, decides reuse.
        const fault::ArmedFault crash(
            fault::FaultAction{ "exec11.journalAppend", fault::Mode::EveryNth, 2, "" } );
        RunCounters first;
        ResumableTileRun run( spec, { scratchRoot, statePath, 0 } );
        REQUIRE_THROWS_AS( run.execute( {}, countingCallbacks( first ) ), std::runtime_error );
        REQUIRE( first.computeCalls == 2 ); // tile 0 committed, tile 1 threw
    }
    {
        std::ifstream journal( statePath + ".journal", std::ios::binary );
        REQUIRE( journal.is_open() );
        unsigned commitLines = 0;
        std::string line;
        while ( std::getline( journal, line ) )
            commitLines += line.rfind( "C ", 0 ) == 0 ? 1u : 0u;
        REQUIRE( commitLines == 1 );
    }

    RunCounters resume;
    ResumableTileRun run( spec, { scratchRoot, statePath, 0 } );
    const ResumableTileRun::Result result = run.execute( {}, countingCallbacks( resume ) );
    REQUIRE( result.tilesReused == 1 ); // tile 0 was committed before the fault
    REQUIRE( result.tilesComputed == 3 );
    REQUIRE( resume.publishCalls == 1 );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "An injected marker failure keeps the run resumable: resume reuses every tile and republishes",
           "[runtime][chunk][r4]" )
{
    const auto dir = scratchDir( "marker-fault" );
    const std::string statePath = ( dir / "run" ).string();
    const std::string scratchRoot = dir.string();
    const TileRunSpec spec = makeSpec( fourTilePartition() );

    RunCounters first;
    {
        const fault::ArmedFault crash(
            fault::FaultAction{ "exec11.marker", fault::Mode::NextN, 1, "" } );
        ResumableTileRun run( spec, { scratchRoot, statePath, 0 } );
        // Publish ran (it precedes the marker), then the marker write failed:
        // the run is complete but NOT published — exactly-once requires the
        // marker, so a rerun must publish again (R6/R5 boundary).
        REQUIRE_THROWS_AS( run.execute( {}, countingCallbacks( first ) ), std::runtime_error );
    }
    REQUIRE( first.publishCalls == 1 );
    REQUIRE( first.computeCalls == 4 );

    RunCounters second;
    {
        ResumableTileRun run( spec, { scratchRoot, statePath, 0 } );
        const ResumableTileRun::Result result = run.execute( {}, countingCallbacks( second ) );
        // All four tiles were committed before the marker fault: pure reuse.
        REQUIRE( result.tilesReused == 4 );
        REQUIRE( result.tilesComputed == 0 );
        REQUIRE( second.computeCalls == 0 );
        REQUIRE( second.publishCalls == 1 );
    }

    // Now the marker exists: the third pass is a no-op (R5 exactly-once).
    RunCounters third;
    ResumableTileRun run( spec, { scratchRoot, statePath, 0 } );
    const ResumableTileRun::Result done = run.execute( {}, countingCallbacks( third ) );
    REQUIRE( done.alreadyPublished );
    REQUIRE( third.consumeCalls == 0 );
    REQUIRE( third.publishCalls == 0 );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

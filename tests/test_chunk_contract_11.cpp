// test_chunk_contract_11.cpp — WP-B: the unified TileRun contract.
//
// Oracle independence: partition digests are checked against an FNV-1a
// implementation written INSIDE this test (public algorithm, repo-family
// constants) over hand-encoded canonical bytes — the production code's
// digest is never used to generate its own expectation. Error-envelope rows
// are a fixed known-answer table; cancel-bridge behavior is asserted
// through observable throws, not implementation details.
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include "operators/framework/chunk_error_bridge.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "runtime/chunk/chunk_graph.h"
#include "runtime/chunk/chunk_pipeline.h"
#include "runtime/chunk/disk_tile_store.h"
#include "runtime/chunk/scratch_registry.h"
#include "runtime/chunk/tile_run_contract.h"
#include "runtime/chunk/tile_spec.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sicnu::runtime::chunk;
using sicnu::operators::ErrorCode;
using sicnu::operators::RSOperatorError;

namespace
{

/// Independent FNV-1a 64 (repo family constants, re-implemented here so the
/// production digest can't manufacture its own expectation).
struct OracleFnv
{
    std::uint64_t h = 1469598103934665603ull;
    void byte( std::uint8_t b )
    {
        h ^= b;
        h *= 1099511628211ull;
    }
    void u64( std::uint64_t v )
    {
        for ( int i = 0; i < 8; ++i )
            byte( static_cast<std::uint8_t>( ( v >> ( 8 * i ) ) & 0xFF ) );
    }
};

std::uint64_t oraclePartitionDigest( const TileRunPartition &p )
{
    OracleFnv f;
    f.byte( 'W' );
    f.u64( static_cast<std::uint32_t>( p.rasterWidth ) );
    f.byte( 'H' );
    f.u64( static_cast<std::uint32_t>( p.rasterHeight ) );
    f.byte( 'w' );
    f.u64( static_cast<std::uint32_t>( p.tileWidth ) );
    f.byte( 'h' );
    f.u64( static_cast<std::uint32_t>( p.tileHeight ) );
    f.byte( 'k' );
    f.u64( static_cast<std::uint32_t>( p.halo ) );
    f.byte( 'b' );
    f.u64( static_cast<std::uint32_t>( p.bands ) );
    f.byte( 'o' );
    f.u64( static_cast<std::uint32_t>( p.bandOffset ) );
    f.byte( 't' );
    f.u64( static_cast<std::uint32_t>( p.timeIndex ) );
    return f.h;
}

TileRunPartition samplePartition()
{
    TileRunPartition p;
    p.rasterWidth = 1024;
    p.rasterHeight = 768;
    p.tileWidth = 64;
    p.tileHeight = 64;
    p.halo = 1;
    p.bands = 4;
    p.bandOffset = 2;
    p.timeIndex = 3;
    return p;
}

/// Runs @p thrower through the translation and returns the resulting operator
/// error code (Success when nothing threw — assertions catch that lie).
template <typename Thrower>
ErrorCode codeOf( Thrower &&thrower )
{
    try
    {
        sicnu::operators::runWithChunkErrorTranslation( [&] {
            thrower();
            return 0;
        } );
    }
    catch ( const RSOperatorError &e )
    {
        return e.code();
    }
    return ErrorCode::Success;
}

} // namespace

TEST_CASE( "Partition digest matches the independent oracle (known answer)", "[chunk][contract]" )
{
    // Hand-picked values cover every field with distinct magnitudes; the
    // oracle bytes are written out above exactly as the format defines them.
    const TileRunPartition p = samplePartition();
    REQUIRE( tileRunPartitionDigest( p ) == oraclePartitionDigest( p ) );

    TileRunPartition minimal;
    minimal.rasterWidth = 1;
    minimal.rasterHeight = 1;
    minimal.tileWidth = 1;
    minimal.tileHeight = 1;
    REQUIRE( tileRunPartitionDigest( minimal ) == oraclePartitionDigest( minimal ) );
}

TEST_CASE( "Partition digest is stable and drift-sensitive", "[chunk][contract]" )
{
    const TileRunPartition p = samplePartition();

    // L1 stability: repeated computation, copied partition.
    const auto d0 = tileRunPartitionDigest( p );
    REQUIRE( tileRunPartitionDigest( p ) == d0 );
    TileRunPartition copy = p;
    REQUIRE( tileRunPartitionDigest( copy ) == d0 );

    // L2 drift sensitivity: EVERY geometry/provenance field must change the
    // digest (resume must refuse stale tile state by construction).
    const std::pair<const char *, void ( * )( TileRunPartition & )> mutations[] = {
        { "rasterWidth", []( TileRunPartition &q ) { q.rasterWidth += 1; } },
        { "rasterHeight", []( TileRunPartition &q ) { q.rasterHeight += 1; } },
        { "tileWidth", []( TileRunPartition &q ) { q.tileWidth += 1; } },
        { "tileHeight", []( TileRunPartition &q ) { q.tileHeight += 1; } },
        { "halo", []( TileRunPartition &q ) { q.halo += 1; } },
        { "bands", []( TileRunPartition &q ) { q.bands += 1; } },
        { "bandOffset", []( TileRunPartition &q ) { q.bandOffset += 1; } },
        { "timeIndex", []( TileRunPartition &q ) { q.timeIndex += 1; } },
    };
    for ( const auto &[field, mutate] : mutations )
    {
        TileRunPartition drifted = p;
        mutate( drifted );
        INFO( "field: " << field );
        REQUIRE( tileRunPartitionDigest( drifted ) != d0 );
    }
}

TEST_CASE( "Partition tile math is exact and O(1)", "[chunk][contract]" )
{
    TileRunPartition p;
    p.rasterWidth = 1000;
    p.rasterHeight = 1000;
    p.tileWidth = 32;
    p.tileHeight = 32;
    REQUIRE( p.totalTiles() == 32ull * 32ull );

    p.rasterWidth = 1001; // ragged edge: ceil division on BOTH axes
    REQUIRE( p.tilesAcross() == 32 );
    REQUIRE( p.totalTiles() == 32ull * 32ull );

    p.rasterWidth = 1025;
    REQUIRE( p.tilesAcross() == 33 );
    REQUIRE( p.totalTiles() == 33ull * 32ull );

    // A 10^6-logical-tile plan must be pure arithmetic (no materialization).
    p.rasterWidth = 32000;
    p.rasterHeight = 32000;
    REQUIRE( p.totalTiles() == 1000ull * 1000ull );
}

TEST_CASE( "Identity key round-trips and rejects malformed input", "[chunk][contract]" )
{
    TileRunIdentity id;
    id.operatorIdentity = 0x0123456789ABCDEFull;
    id.inputIdentity = 0xFEDCBA9876543210ull;
    id.partitionDigest = 42;

    const std::string key = tileRunIdentityKey( id );
    REQUIRE( key.size() == 3 * 16 + 2 );
    const TileRunIdentity back = tileRunIdentityFromKey( key );
    REQUIRE( back.operatorIdentity == id.operatorIdentity );
    REQUIRE( back.inputIdentity == id.inputIdentity );
    REQUIRE( back.partitionDigest == id.partitionDigest );

    // Malformed keys decode to the all-zero identity (callers gate on that).
    const TileRunIdentity bad = tileRunIdentityFromKey( "not-a-key" );
    REQUIRE( bad.operatorIdentity == 0 );
    REQUIRE( bad.inputIdentity == 0 );
    REQUIRE( bad.partitionDigest == 0 );
}

TEST_CASE( "Error envelope known-answer table", "[chunk][contract]" )
{
    REQUIRE( codeOf( [] { throw ChunkCancelled(); } ) == ErrorCode::Cancelled );
    REQUIRE( codeOf( [] { throw ChunkConsumerAborted(); } ) == ErrorCode::Cancelled );
    REQUIRE( codeOf( [] { throw ChunkGraphCancelled(); } ) == ErrorCode::Cancelled );
    REQUIRE( codeOf( [] { throw ScratchBudgetExceeded( 10, 0, 8 ); } ) ==
             ErrorCode::ResourceBudgetExceeded );
    REQUIRE( codeOf( [] { throw ChunkCorruptTile( "tile.bin" ); } ) ==
             ErrorCode::CorruptArtifactData );
    REQUIRE( codeOf( [] { throw ChunkPartitionMismatch( 2 ); } ) == ErrorCode::ComputationError );

    // Foreign errors pass through untouched (not disguised as chunk errors).
    bool rethrown = false;
    try
    {
        sicnu::operators::runWithChunkErrorTranslation(
            []() -> int { throw std::logic_error( "foreign" ); } );
    }
    catch ( const std::logic_error & )
    {
        rethrown = true;
    }
    catch ( const RSOperatorError & )
    {
        rethrown = false;
    }
    REQUIRE( rethrown );

    // Return values pass through unchanged.
    REQUIRE( sicnu::operators::runWithChunkErrorTranslation( [] { return 41 + 1; } ) == 42 );
}

TEST_CASE( "Cancel bridge wires the context's own flag and polls predicates", "[chunk][contract]" )
{
    using sicnu::operators::ChunkCancelBridge;

    // Flag-based context: the bridge hands the pipeline THE SAME flag (no
    // mirror). Observable: pipeline stops with ChunkCancelled when the flag
    // is raised mid-stream.
    std::atomic<bool> cancel{ false };
    sicnu::operators::RSOperatorContext ctx;
    ctx.setCancelFlag( &cancel );

    ChunkCancelBridge bridge( ctx );
    REQUIRE_FALSE( ctx.isCancelled() );
    bridge.throwIfCancelled(); // no-throw while not cancelled

    cancel = true;
    bool threw = false;
    try
    {
        bridge.throwIfCancelled();
    }
    catch ( const ChunkCancelled & )
    {
        threw = true;
    }
    REQUIRE( threw );

    // Wire() actually reaches the pipeline: producer floods tiles, flag is
    // already set, run() unwinds with ChunkCancelled rather than finishing.
    TileSpec spec;
    spec.totalTiles = 1 << 30;
    spec.width = spec.height = 1;
    spec.bufferWidth = spec.bufferHeight = 1;
    spec.bands = 1;
    ChunkPipeline pipeline(
        [&spec]( TilePayload &out ) {
            out = TilePayload{ spec, std::make_shared<std::vector<float>>( 1, 0.f ) };
            return true;
        },
        {},
        []( TilePayload && ) { return true; } );
    bridge.wire( pipeline );
    REQUIRE_THROWS_AS( pipeline.run(), ChunkCancelled );

    // Callback-based context: no flag to wire (no-op), but throwIfCancelled
    // still honors the predicate between tiles.
    sicnu::operators::RSOperatorContext callbackCtx;
    bool cancelled = false;
    callbackCtx.setCancelCallback( [&cancelled] { return cancelled; } );
    ChunkCancelBridge callbackBridge( callbackCtx );
    callbackBridge.throwIfCancelled(); // predicate false: no-throw
    cancelled = true;
    try
    {
        callbackBridge.throwIfCancelled();
        threw = false;
    }
    catch ( const ChunkCancelled & )
    {
        threw = true;
    }
    REQUIRE( threw );
}

TEST_CASE( "TileRunCancelSource honors flag-then-predicate precedence", "[chunk][contract]" )
{
    std::atomic<bool> flag{ false };
    bool predicateCalled = false;
    TileRunCancelSource source;
    source.flag = &flag;
    source.predicate = [&predicateCalled] {
        predicateCalled = true;
        return true;
    };

    // Flag false -> the predicate is consulted and its true wins.
    REQUIRE( source.cancelled() );
    REQUIRE( predicateCalled );

    // Flag true -> predicate short-circuited (the flag wins without polling).
    predicateCalled = false;
    flag = true;
    REQUIRE( source.cancelled() );
    REQUIRE_FALSE( predicateCalled );

    TileRunCancelSource onlyPredicate;
    onlyPredicate.predicate = [] { return true; };
    REQUIRE( onlyPredicate.cancelled() );
}

TEST_CASE( "buildTileGrid refuses overflow-sized grids instead of wrapping (#1056)",
           "[chunk][contract][tile_grid]" )
{
    using sicnu::runtime::chunk::buildTileGrid;

    // Sanity: a normal grid is unchanged.
    const auto grid = buildTileGrid( 1024, 768, 64, 64, 1, 4 );
    REQUIRE( grid.size() == 16 * 12 );
    REQUIRE( grid.front().totalTiles == 16 * 12 );
    REQUIRE( grid.back().xOffset == 15 * 64 );

    // GDAL-reported dimensions near INT_MAX: `rasterWidth + tileWidth - 1`
    // and `cols * rows` overflow signed int — the grid must refuse loudly
    // rather than wrap into UB or an absurd reserve.
    REQUIRE_THROWS_AS( buildTileGrid( std::numeric_limits<int>::max(),
                                      std::numeric_limits<int>::max(), 64, 64, 0, 1 ),
                       std::length_error );
    REQUIRE_THROWS_AS( buildTileGrid( 1 << 20, 1 << 20, 1, 1, 0, 1 ), std::length_error );
    // `tile + 2*halo` must never overflow the buffer side either.
    REQUIRE_THROWS_AS( buildTileGrid( 8, 8, 4, 4, std::numeric_limits<int>::max(), 1 ),
                       std::length_error );
    REQUIRE_THROWS_AS( buildTileGrid( 8, 8, 4, 4, -1, 1 ), std::invalid_argument );
}

TEST_CASE( "ScratchRegistry::acquire refuses path-injection components (#1056)",
           "[chunk][contract][scratch]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    ScratchRegistry::Config config;
    config.root = dir.path().toStdString();
    ScratchRegistry registry( config );

    const auto lease = registry.acquire( "run-1", "tile", 1024 );
    REQUIRE( lease.path().find( "run-1" ) != std::string::npos );

    // runId / stem are spliced into paths: separators, traversal tokens and
    // colons must never be able to escape the scratch root.
    REQUIRE_THROWS_AS( registry.acquire( "../escape", "tile", 1024 ), std::invalid_argument );
    REQUIRE_THROWS_AS( registry.acquire( "run", "..", 1024 ), std::invalid_argument );
    REQUIRE_THROWS_AS( registry.acquire( "run", "a/b", 1024 ), std::invalid_argument );
    REQUIRE_THROWS_AS( registry.acquire( "run", "C:tile", 1024 ), std::invalid_argument );
    REQUIRE_THROWS_AS( registry.acquire( "", "tile", 1024 ), std::invalid_argument );
}

// test_chunk_adoption_11.cpp — WP-G: the chunked_run adoption kit through a
// synthetic reference adopter (registered HERE, not in the builtin operator
// list — domain operators adopt incrementally per docs/execution/
// ADOPTION_GUIDE.md).
//
// Oracle: the tile truth is a closed form written independently in this file
// (value = ((index*37 + pixel*7 + band) % 251)/251); the assembled output is
// compared against a test-side full raster built with that formula — never
// against the kernel's own output.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/chunk_error_bridge.h"
#include "operators/framework/chunked_run.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "runtime/chunk/tile_run_contract.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "platform/portable.h"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace
{
int selfPid()
{
    return static_cast<int>( sicnu::portable::pid() );
}
} // namespace

using namespace sicnu::operators;
using namespace sicnu::runtime::chunk;

namespace
{

constexpr int kRaster = 24; // 6x4 tiles of 4x2 → 24 tiles
constexpr int kTileW = 4;
constexpr int kTileH = 2;
constexpr int kBands = 2;

float truthValue( std::uint64_t tileIndex, std::uint64_t pixel, int band )
{
    return static_cast<float>( ( tileIndex * 37 + pixel * 7 + static_cast<std::uint64_t>( band ) )
                               % 251 )
           / 251.0f;
}

/// The synthetic adopter's kernel (mirrors truthValue; production adopters
/// bring their real science kernel).
std::vector<float> referenceKernel( const TileSpec &s )
{
    std::vector<float> buffer( s.bufferElementCount() );
    for ( int b = 0; b < s.bands; ++b )
        for ( int y = 0; y < s.bufferHeight; ++y )
            for ( int x = 0; x < s.bufferWidth; ++x )
            {
                const std::uint64_t pixel =
                    static_cast<std::uint64_t>( y ) * s.bufferWidth + x;
                buffer[static_cast<size_t>( b ) * s.bufferWidth * s.bufferHeight + pixel] =
                    truthValue( s.index, pixel, b );
            }
    return buffer;
}

TileRunPartition referencePartition()
{
    TileRunPartition p;
    p.rasterWidth = kRaster;
    p.rasterHeight = 8;
    p.tileWidth = kTileW;
    p.tileHeight = kTileH;
    p.bands = kBands;
    return p;
}

/// Independent truth assembly: the full output the sink must have received.
std::vector<float> truthRaster()
{
    const TileRunPartition p = referencePartition();
    std::vector<float> truth;
    truth.reserve( static_cast<size_t>( p.totalTiles() ) * kTileW * kTileH * kBands );
    for ( std::uint64_t t = 0; t < p.totalTiles(); ++t )
    {
        const TileSpec s = tileSpecAt( p, t );
        for ( int b = 0; b < s.bands; ++b )
            for ( int y = 0; y < s.height; ++y )
                for ( int x = 0; x < s.width; ++x )
                {
                    const std::uint64_t pixel =
                        static_cast<std::uint64_t>( y ) * s.bufferWidth + x;
                    truth.push_back( truthValue( t, pixel, b ) );
                }
    }
    return truth;
}

std::filesystem::path makeTempDir( const char *tag )
{
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path( ec );
    if ( ec )
        base = std::filesystem::current_path();
    static std::atomic<unsigned> n{ 0 };
    const auto dir = base / ( "sicnu-adopt11-" + std::string( tag ) + "-"
                              + std::to_string( selfPid() ) + std::to_string( n++ ) );
    std::filesystem::create_directories( dir, ec );
    return dir;
}

struct AdopterRun
{
    std::vector<float> output;
    std::uint64_t kernelCalls = 0;
    ChunkedRunResult result{};

    /// @p sinkCount (optional) counts sunk tiles for cancel predicates.
    AdopterRun( RSOperatorContext &ctx, const ChunkedRunOptions &options,
                std::uint64_t *sinkCount = nullptr,
                const std::function<bool()> &cancelAfter = {} )
    {
        output.reserve( truthRaster().size() );
        ChunkedRunOptions opts = options;
        auto kernelCounter = std::make_shared<std::uint64_t>( 0 );
        ChunkTileKernel kernel = [kernelCounter]( const TileSpec &s ) {
            ++*kernelCounter;
            return referenceKernel( s );
        };
        std::function<void( const TilePayload & )> sink = [&]( const TilePayload &p ) {
            if ( sinkCount )
                ++*sinkCount;
            // core pixels only (no halo in this adopter), band-major
            for ( int b = 0; b < p.spec.bands; ++b )
                for ( int y = 0; y < p.spec.height; ++y )
                    for ( int x = 0; x < p.spec.width; ++x )
                        output.push_back( ( *p.pixels )[static_cast<size_t>( b )
                                                              * p.spec.bufferWidth
                                                                  * p.spec.bufferHeight
                                                          + static_cast<size_t>( y )
                                                                * p.spec.bufferWidth
                                                          + x] );
        };
        std::function<void()> publish = [this] { published = true; };
        opts.publish = publish;

        // Cancellation via the CONTEXT (callback flavor) — proves the kit
        // bridges both context shapes.
        if ( cancelAfter )
            ctx.setCancelCallback( cancelAfter );
        try
        {
            result = runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                                         ctx, referencePartition(),
                                         TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( ... )
        {
            kernelCalls = *kernelCounter;
            threw = std::current_exception();
            return;
        }
        kernelCalls = *kernelCounter;
    }

    bool threwSomething() const { return threw != nullptr; }
    bool published = false;
    std::exception_ptr threw;
};

} // namespace

TEST_CASE( "Reference adopter: fresh run matches the closed-form truth", "[chunk][adoption]" )
{
    const auto dir = makeTempDir( "fresh" );
    RSOperatorContext ctx( dir.generic_string() );
    ChunkedRunOptions options;
    options.scratchRoot = ( dir / "scratch" ).generic_string();

    AdopterRun run( ctx, options );
    REQUIRE_FALSE( run.threwSomething() );
    REQUIRE( run.result.tilesComputed == referencePartition().totalTiles() );
    REQUIRE( run.result.tilesReused == 0 );
    REQUIRE( run.published );
    REQUIRE( run.output == truthRaster() );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Reference adopter: cancel resumes with zero recomputed tiles",
           "[chunk][adoption]" )
{
    const auto dir = makeTempDir( "resume" );
    const std::uint64_t total = referencePartition().totalTiles();
    RSOperatorContext ctx( dir.generic_string() );
    ChunkedRunOptions options;
    options.scratchRoot = ( dir / "scratch" ).generic_string();
    // Deterministic state base so the second run finds the first's state.
    options.resumeStateBase = ( dir / "state" / "ref" ).generic_string();

    std::uint64_t sunk = 0;
    AdopterRun first( ctx, options, &sunk, [&sunk] { return sunk >= 9; } );
    // Count sunk tiles via output size (each tile = width*height*bands floats).
    // Cancel after ~9 tiles: the run must stop with the typed cancellation.
    REQUIRE( first.threwSomething() );
    bool cancelled = false;
    try
    {
        if ( first.threw )
            std::rethrow_exception( first.threw );
    }
    catch ( const RSOperatorError &e )
    {
        cancelled = e.code() == ErrorCode::Cancelled;
    }
    REQUIRE( cancelled );

    sunk = 0;
    RSOperatorContext ctx2( dir.generic_string() );
    AdopterRun resumed( ctx2, options );
    REQUIRE_FALSE( resumed.threwSomething() );
    REQUIRE( resumed.result.totalTiles == total );
    REQUIRE( resumed.result.tilesReused + resumed.result.tilesComputed == total );
    REQUIRE( resumed.output == truthRaster() );
    // The committed prefix (>= 9 commits: consume runs AFTER the journal
    // append) was reused — a silent state wipe cannot pass this.
    REQUIRE( resumed.result.tilesReused >= 9 );
    REQUIRE( resumed.kernelCalls == total - resumed.result.tilesReused );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Reference adopter: published marker short-circuits re-runs",
           "[chunk][adoption]" )
{
    const auto dir = makeTempDir( "marker" );
    RSOperatorContext ctx( dir.generic_string() );
    ChunkedRunOptions options;
    options.scratchRoot = ( dir / "scratch" ).generic_string();
    options.resumeStateBase = ( dir / "state" / "ref" ).generic_string();

    AdopterRun first( ctx, options );
    REQUIRE( first.published );
    const auto firstKernelCalls = first.kernelCalls;

    AdopterRun second( ctx, options );
    REQUIRE( second.result.alreadyPublished );
    REQUIRE( second.kernelCalls == 0 );
    REQUIRE( firstKernelCalls == referencePartition().totalTiles() );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Reference adopter: pipeline mode is byte-identical to resumable mode",
           "[chunk][adoption]" )
{
    const auto dirA = makeTempDir( "modea" );
    const auto dirB = makeTempDir( "modeb" );

    RSOperatorContext ctxA( dirA.generic_string() );
    ChunkedRunOptions resumable;
    resumable.scratchRoot = ( dirA / "scratch" ).generic_string();
    AdopterRun a( ctxA, resumable );

    RSOperatorContext ctxB( dirB.generic_string() );
    ChunkedRunOptions pipeline;
    pipeline.mode = ChunkedRunOptions::Mode::Pipeline;
    AdopterRun b( ctxB, pipeline );

    REQUIRE_FALSE( a.threwSomething() );
    REQUIRE_FALSE( b.threwSomething() );
    REQUIRE( a.output == b.output );
    REQUIRE( a.output == truthRaster() );

    std::error_code ec;
    std::filesystem::remove_all( dirA, ec );
    std::filesystem::remove_all( dirB, ec );
}

TEST_CASE( "Reference adopter: pipeline mode honors callback-based cancellation",
           "[chunk][adoption]" )
{
    const auto dir = makeTempDir( "pipecancel" );
    RSOperatorContext ctx( dir.generic_string() );
    ChunkedRunOptions options;
    options.mode = ChunkedRunOptions::Mode::Pipeline;
    options.scratchRoot = ( dir / "scratch" ).generic_string();

    std::uint64_t sunk = 0;
    AdopterRun run( ctx, options, &sunk, [&sunk] { return sunk >= 6; } );
    REQUIRE( run.threwSomething() );
    bool cancelled = false;
    try
    {
        if ( run.threw )
            std::rethrow_exception( run.threw );
    }
    catch ( const RSOperatorError &e )
    {
        cancelled = e.code() == ErrorCode::Cancelled;
    }
    // The pipeline must STOP (typed cancellation), not run to completion —
    // bridge.throwIfCancelled covers callback-shaped contexts too.
    REQUIRE( cancelled );
    REQUIRE( run.output.size() < truthRaster().size() );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Reference adopter: hard RAM admission refuses before any kernel work",
           "[chunk][adoption]" )
{
    const auto dir = makeTempDir( "refuse" );
    RSOperatorContext ctx( dir.generic_string() );
    ChunkedRunOptions options;
    options.scratchRoot = ( dir / "scratch" ).generic_string();
    options.ramBudgetBytes = 8; // smaller than a single tile

    AdopterRun refused( ctx, options );
    REQUIRE( refused.threwSomething() );
    bool budgetRefused = false;
    try
    {
        if ( refused.threw )
            std::rethrow_exception( refused.threw );
    }
    catch ( const RSOperatorError &e )
    {
        budgetRefused = e.code() == ErrorCode::ResourceBudgetExceeded;
    }
    REQUIRE( budgetRefused );
    REQUIRE( refused.kernelCalls == 0 ); // nothing ran

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

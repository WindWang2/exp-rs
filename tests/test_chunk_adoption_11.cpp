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
#include "runtime/chunk/disk_tile_store.h"
#include "runtime/chunk/tile_run_contract.h"

#include "data/data_manager.h"
#include "processing/framework/output_committer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <gdal.h>

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

namespace
{
void createSyntheticGeoTiff( const QString &path, int w = 24, int h = 8 )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    QDir().mkpath( QFileInfo( path ).absolutePath() );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( h ), 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    GDALSetProjection(
        ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
            "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( static_cast<size_t>( w ), 0.42f );
    for ( int row = 0; row < h; ++row )
    {
        const CPLErr ioErr = GDALRasterIO( band, GF_Write, 0, row, w, 1, line.data(), w, 1, GDT_Float32, 0, 0 );
        REQUIRE( ioErr == CE_None );
    }
    GDALClose( ds );
}
} // namespace

TEST_CASE( "M2-T1 & M2-T2: OutputCommitter publish-then-swap and exactly-once re-run bypass",
           "[chunk][adoption][output_committer]" )
{
    const auto dir = makeTempDir( "m2_commit" );
    const auto qDir = QString::fromStdString( dir.generic_string() );
    const QString tempOut = qDir + QStringLiteral( "/temp_output.tif" );
    const QString stableOut = qDir + QStringLiteral( "/stable_output.tif" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );
    std::vector<sicnu::data::AssetId> displayedAssets;
    QObject::connect( &committer, &sicnu::OutputCommitter::displayRequested,
                      [&]( sicnu::data::AssetId id ) {
                          displayedAssets.push_back( id );
                      } );

    RSOperatorContext ctx( dir.generic_string() );
    ChunkedRunOptions options;
    options.scratchRoot = ( dir / "scratch" ).generic_string();
    options.resumeStateBase = ( dir / "state" / "ref" ).generic_string();

    ChunkedOutputCommitSpec commitSpec;
    commitSpec.committer = &committer;
    commitSpec.tempPath = tempOut;
    commitSpec.stablePath = stableOut;
    commitSpec.kind = sicnu::data::AssetKind::Raster;
    commitSpec.persistence = sicnu::data::PersistencePolicy::SessionTemporary;
    commitSpec.autoLoad = true;

    sicnu::data::DerivationRecord derivation;
    derivation.algorithmId = QStringLiteral( "test:chunk_reference" );
    derivation.algorithmVersion = QStringLiteral( "1.0.0" );
    derivation.taskReference = QStringLiteral( "task-m2" );
    commitSpec.derivation = derivation;

    options.commitSpec = commitSpec;

    // Simulate sink writing and flushing the output raster before publish
    options.publish = [&]() {
        createSyntheticGeoTiff( tempOut, 24, 8 );
    };

    auto kernelCounter = std::make_shared<std::uint64_t>( 0 );
    ChunkTileKernel kernel = [kernelCounter]( const TileSpec &s ) {
        ++*kernelCounter;
        return referenceKernel( s );
    };
    std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

    // M2-T1: First run - executes kernel, publishes via committer, registers in DataManager
    const auto result1 = runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                                             ctx, referencePartition(),
                                             TileRunDeterminism::BitExact, kernel, sink, options );

    REQUIRE( result1.totalTiles == referencePartition().totalTiles() );
    REQUIRE( result1.tilesComputed == referencePartition().totalTiles() );
    REQUIRE( result1.tilesReused == 0 );
    REQUIRE_FALSE( result1.alreadyPublished );
    REQUIRE_FALSE( result1.publishedAssetId.isEmpty() );

    // 1. Temp file consumed by atomic rename
    CHECK_FALSE( QFile::exists( tempOut ) );
    // 2. Stable output exists on disk
    CHECK( QFile::exists( stableOut ) );

    // 3. Registered in DataManager
    const auto optAssetId = sicnu::data::AssetId::fromString( result1.publishedAssetId );
    REQUIRE( optAssetId.has_value() );
    const auto snapshot = manager.asset( *optAssetId );
    REQUIRE( snapshot.has_value() );
    CHECK( snapshot->kind() == sicnu::data::AssetKind::Raster );
    CHECK( snapshot->persistence() == sicnu::data::PersistencePolicy::SessionTemporary );

    // 4. autoLoad triggered displayRequested exactly once
    CHECK( displayedAssets.size() == 1 );
    if ( !displayedAssets.empty() )
    {
        CHECK( displayedAssets[0] == *optAssetId );
    }

    // 5. Marker exists, scratch directory swept
    const auto markerPath = dir / "state" / "ref.published";
    CHECK( std::filesystem::exists( markerPath ) );
    CHECK_FALSE( std::filesystem::exists( dir / "state" / "ref.journal" ) );

    // M2-T2: Second run - exactly-once invariant skips re-computation & double-publish
    *kernelCounter = 0;
    RSOperatorContext ctx2( dir.generic_string() );
    const auto result2 = runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                                             ctx2, referencePartition(),
                                             TileRunDeterminism::BitExact, kernel, sink, options );

    REQUIRE( result2.alreadyPublished );
    REQUIRE( result2.tilesComputed == 0 );
    REQUIRE( result2.tilesReused == 0 );
    REQUIRE( *kernelCounter == 0 );
    // Committer displayRequested was NOT reinvoked (no double-publish)
    CHECK( displayedAssets.size() == 1 );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "M2-T3: Scratch sweep via cleanupAfterPublish removes tiles while retaining .published",
           "[chunk][adoption][scratch]" )
{
    // Part A: cleanupScratchOnSuccess = true (default)
    {
        const auto dirA = makeTempDir( "m2_sweep_on" );
        RSOperatorContext ctxA( dirA.generic_string() );
        ChunkedRunOptions optsA;
        optsA.scratchRoot = ( dirA / "scratch" ).generic_string();
        optsA.resumeStateBase = ( dirA / "state" / "ref" ).generic_string();
        optsA.cleanupScratchOnSuccess = true;

        AdopterRun runA( ctxA, optsA );
        REQUIRE_FALSE( runA.threwSomething() );
        REQUIRE( runA.result.tilesComputed == referencePartition().totalTiles() );

        // Marker MUST exist
        CHECK( std::filesystem::exists( dirA / "state" / "ref.published" ) );
        // Journal and ckpt MUST be swept
        CHECK_FALSE( std::filesystem::exists( dirA / "state" / "ref.journal" ) );
        CHECK_FALSE( std::filesystem::exists( dirA / "state" / "ref.ckpt" ) );

        // Scratch tile directory (rt-*) must NOT exist or be completely empty of .tl files
        bool foundTileFile = false;
        if ( std::filesystem::exists( dirA / "scratch" ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( dirA / "scratch" ) )
            {
                if ( entry.path().extension() == ".tl" )
                    foundTileFile = true;
            }
        }
        CHECK_FALSE( foundTileFile );

        std::error_code ec;
        std::filesystem::remove_all( dirA, ec );
    }

    // Part B: cleanupScratchOnSuccess = false preserves scratch tiles
    {
        const auto dirB = makeTempDir( "m2_sweep_off" );
        RSOperatorContext ctxB( dirB.generic_string() );
        ChunkedRunOptions optsB;
        optsB.scratchRoot = ( dirB / "scratch" ).generic_string();
        optsB.resumeStateBase = ( dirB / "state" / "ref" ).generic_string();
        optsB.cleanupScratchOnSuccess = false;

        AdopterRun runB( ctxB, optsB );
        REQUIRE_FALSE( runB.threwSomething() );
        REQUIRE( runB.result.tilesComputed == referencePartition().totalTiles() );

        // Marker MUST exist
        CHECK( std::filesystem::exists( dirB / "state" / "ref.published" ) );
        // Journal MUST still exist because cleanup was disabled
        CHECK( std::filesystem::exists( dirB / "state" / "ref.journal" ) );

        // Tile files MUST still exist in scratchRoot
        std::uint64_t tileFileCount = 0;
        if ( std::filesystem::exists( dirB / "scratch" ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( dirB / "scratch" ) )
            {
                if ( entry.path().extension() == ".tl" )
                    ++tileFileCount;
            }
        }
        CHECK( tileFileCount == referencePartition().totalTiles() );

        std::error_code ec;
        std::filesystem::remove_all( dirB, ec );
    }
}

TEST_CASE( "M2-T4: Interrupted runs preserve scratch and resume without recomputing finished tiles",
           "[chunk][adoption][resume]" )
{
    const auto dir = makeTempDir( "m2_interrupt" );
    const auto total = referencePartition().totalTiles();
    const auto qDir = QString::fromStdString( dir.generic_string() );
    const QString tempOut = qDir + QStringLiteral( "/temp_out.tif" );
    const QString stableOut = qDir + QStringLiteral( "/stable_out.tif" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    ChunkedRunOptions options;
    options.scratchRoot = ( dir / "scratch" ).generic_string();
    options.resumeStateBase = ( dir / "state" / "ref" ).generic_string();

    ChunkedOutputCommitSpec commitSpec;
    commitSpec.committer = &committer;
    commitSpec.tempPath = tempOut;
    commitSpec.stablePath = stableOut;
    options.commitSpec = commitSpec;
    options.publish = [&]() {
        createSyntheticGeoTiff( tempOut, 24, 8 );
    };

    // Stage 1: Cancel cooperatively after 9 tiles
    std::uint64_t sunk = 0;
    RSOperatorContext ctx1( dir.generic_string() );
    ctx1.setCancelCallback( [&sunk] { return sunk >= 9; } );

    auto kernelCounter = std::make_shared<std::uint64_t>( 0 );
    ChunkTileKernel kernel = [kernelCounter]( const TileSpec &s ) {
        ++*kernelCounter;
        return referenceKernel( s );
    };
    std::function<void( const TilePayload & )> sink = [&]( const TilePayload & ) {
        ++sunk;
    };

    bool cancelledCaught = false;
    try
    {
        runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                            ctx1, referencePartition(),
                            TileRunDeterminism::BitExact, kernel, sink, options );
    }
    catch ( const RSOperatorError &e )
    {
        if ( e.code() == ErrorCode::Cancelled )
            cancelledCaught = true;
    }
    REQUIRE( cancelledCaught );

    // Scratch journal and tile files MUST be preserved for resume
    CHECK( std::filesystem::exists( dir / "state" / "ref.journal" ) );
    CHECK_FALSE( std::filesystem::exists( dir / "state" / "ref.published" ) );
    CHECK_FALSE( QFile::exists( stableOut ) );

    std::uint64_t preservedTiles = 0;
    if ( std::filesystem::exists( dir / "scratch" ) )
    {
        for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir / "scratch" ) )
        {
            if ( entry.path().extension() == ".tl" )
                ++preservedTiles;
        }
    }
    REQUIRE( preservedTiles >= 9 );

    // Stage 2: Resume with clean context
    *kernelCounter = 0;
    sunk = 0;
    RSOperatorContext ctx2( dir.generic_string() );

    const auto resumed = runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                                             ctx2, referencePartition(),
                                             TileRunDeterminism::BitExact, kernel, sink, options );

    REQUIRE( resumed.totalTiles == total );
    REQUIRE( resumed.tilesReused >= 9 );
    REQUIRE( resumed.tilesComputed == total - resumed.tilesReused );
    REQUIRE( *kernelCounter == total - resumed.tilesReused );
    REQUIRE_FALSE( resumed.publishedAssetId.isEmpty() );

    // After successful publication, scratch was swept and stablePath published
    CHECK( QFile::exists( stableOut ) );
    CHECK_FALSE( QFile::exists( tempOut ) );
    CHECK( std::filesystem::exists( dir / "state" / "ref.published" ) );
    CHECK_FALSE( std::filesystem::exists( dir / "state" / "ref.journal" ) );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "M2-T5: Invalid commit spec parameter validation and error translation",
           "[chunk][adoption][validation]" )
{
    const auto dir = makeTempDir( "m2_validation" );
    const auto qDir = QString::fromStdString( dir.generic_string() );
    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    RSOperatorContext ctx( dir.generic_string() );
    ChunkTileKernel kernel = []( const TileSpec &s ) { return referenceKernel( s ); };
    std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

    SECTION( "Null committer throws InvalidParameter" )
    {
        ChunkedRunOptions opts;
        ChunkedOutputCommitSpec spec;
        spec.committer = nullptr;
        spec.tempPath = qDir + QStringLiteral( "/temp.tif" );
        spec.stablePath = qDir + QStringLiteral( "/stable.tif" );
        opts.commitSpec = spec;

        bool threwInvalidParam = false;
        try
        {
            runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            threwInvalidParam = ( e.code() == ErrorCode::InvalidParameter );
        }
        CHECK( threwInvalidParam );
    }

    SECTION( "Empty tempPath or stablePath throws InvalidParameter" )
    {
        ChunkedRunOptions opts;
        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = QStringLiteral( "" );
        spec.stablePath = qDir + QStringLiteral( "/stable.tif" );
        opts.commitSpec = spec;

        bool threwInvalidParam = false;
        try
        {
            runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            threwInvalidParam = ( e.code() == ErrorCode::InvalidParameter );
        }
        CHECK( threwInvalidParam );

        // whitespace only
        opts.commitSpec->tempPath = QStringLiteral( "   " );
        threwInvalidParam = false;
        try
        {
            runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            threwInvalidParam = ( e.code() == ErrorCode::InvalidParameter );
        }
        CHECK( threwInvalidParam );
    }

    SECTION( "Commit failure translates to FileNotWritable, preserves temp & scratch for diagnosis, prevents marker" )
    {
        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "ref" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        // Non-existent tempPath causes commit to fail validation
        spec.tempPath = qDir + QStringLiteral( "/non_existent.tif" );
        spec.stablePath = qDir + QStringLiteral( "/stable.tif" );
        opts.commitSpec = spec;

        bool threwFileNotWritable = false;
        try
        {
            runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            threwFileNotWritable = ( e.code() == ErrorCode::FileNotWritable );
        }
        CHECK( threwFileNotWritable );
        // .published marker must NOT have been written
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "ref.published" ) );
        // Scratch journal must still exist for diagnosis
        CHECK( std::filesystem::exists( dir / "state" / "ref.journal" ) );
    }

    SECTION( "Abnormal kernel failure calls run.abandon() and discards temporary output" )
    {
        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "ref" ).generic_string();

        const QString tempFile = qDir + QStringLiteral( "/temp_discard.tif" );
        createSyntheticGeoTiff( tempFile, 24, 8 );
        REQUIRE( QFile::exists( tempFile ) );

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempFile;
        spec.stablePath = qDir + QStringLiteral( "/stable_discard.tif" );
        opts.commitSpec = spec;

        ChunkTileKernel badKernel = []( const TileSpec &s ) -> std::vector<float> {
            if ( s.index >= 3 )
                throw std::runtime_error( "unhandled kernel fault" );
            return referenceKernel( s );
        };

        bool threwRuntime = false;
        try
        {
            runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, badKernel, sink, opts );
        }
        catch ( const std::exception & )
        {
            threwRuntime = true;
        }
        CHECK( threwRuntime );

        // Temporary output was swept by discardTemporary
        CHECK_FALSE( QFile::exists( tempFile ) );
        // Resumable state was wiped by run.abandon()
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "ref.journal" ) );
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "ref.published" ) );
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "M2-Pipeline: OutputCommitter integration in pipeline mode",
           "[chunk][adoption][pipeline][output_committer]" )
{
    const auto dir = makeTempDir( "m2_pipe_commit" );
    const auto qDir = QString::fromStdString( dir.generic_string() );
    const QString tempOut = qDir + QStringLiteral( "/temp_pipe.tif" );
    const QString stableOut = qDir + QStringLiteral( "/stable_pipe.tif" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    RSOperatorContext ctx( dir.generic_string() );
    ChunkedRunOptions options;
    options.mode = ChunkedRunOptions::Mode::Pipeline;
    options.scratchRoot = ( dir / "scratch" ).generic_string();

    ChunkedOutputCommitSpec commitSpec;
    commitSpec.committer = &committer;
    commitSpec.tempPath = tempOut;
    commitSpec.stablePath = stableOut;
    options.commitSpec = commitSpec;
    options.publish = [&]() {
        createSyntheticGeoTiff( tempOut, 24, 8 );
    };

    ChunkTileKernel kernel = []( const TileSpec &s ) { return referenceKernel( s ); };
    std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

    const auto result = runChunkedOperator( "test:chunk_reference", Json::Value( Json::objectValue ),
                                            ctx, referencePartition(),
                                            TileRunDeterminism::BitExact, kernel, sink, options );

    REQUIRE( result.tilesComputed == referencePartition().totalTiles() );
    REQUIRE_FALSE( result.publishedAssetId.isEmpty() );
    CHECK( QFile::exists( stableOut ) );
    CHECK_FALSE( QFile::exists( tempOut ) );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// ============================================================================
// Milestone 2 Empirical Adversarial Challenges
// ============================================================================

TEST_CASE( "M2-Challenge-1: Scratch tile sweeping and preservation matrix",
           "[chunk][adoption][scratch][empirical_challenge]" )
{
    // Custom partition: 48 tiles (6x8 grid, 2 bands, halo=0)
    TileRunPartition customPart;
    customPart.rasterWidth = 24;
    customPart.rasterHeight = 16;
    customPart.tileWidth = 4;
    customPart.tileHeight = 2;
    customPart.bands = 2;
    const std::uint64_t total = customPart.totalTiles();
    REQUIRE( total == 48 );

    auto kernelCounter = std::make_shared<std::uint64_t>( 0 );
    ChunkTileKernel challengeKernel = [kernelCounter]( const TileSpec &s ) {
        ++*kernelCounter;
        return referenceKernel( s );
    };
    std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

    SECTION( "1A: cleanupScratchOnSuccess = true with custom scratchRoot and commitSpec sweeps all .tl files" )
    {
        const auto dir = makeTempDir( "chal_sweep_commit" );
        const auto qDir = QString::fromStdString( dir.generic_string() );
        const QString tempOut = qDir + QStringLiteral( "/temp_1a.tif" );
        const QString stableOut = qDir + QStringLiteral( "/stable_1a.tif" );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        RSOperatorContext ctx( dir.generic_string() );
        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "run1a" ).generic_string();
        opts.cleanupScratchOnSuccess = true;

        ChunkedOutputCommitSpec commitSpec;
        commitSpec.committer = &committer;
        commitSpec.tempPath = tempOut;
        commitSpec.stablePath = stableOut;
        opts.commitSpec = commitSpec;
        opts.publish = [&]() {
            createSyntheticGeoTiff( tempOut, 24, 16 );
        };

        *kernelCounter = 0;
        const auto res1 = runChunkedOperator( "test:challenge_sweep", Json::Value( Json::objectValue ),
                                              ctx, customPart,
                                              TileRunDeterminism::BitExact, challengeKernel, sink, opts );

        CHECK( res1.totalTiles == 48 );
        CHECK( res1.tilesComputed == 48 );
        CHECK_FALSE( res1.alreadyPublished );
        CHECK_FALSE( res1.publishedAssetId.isEmpty() );

        // Stable output must exist, temp output must be consumed
        CHECK( QFile::exists( stableOut ) );
        CHECK_FALSE( QFile::exists( tempOut ) );

        // Marker must exist, journal and ckpt must be swept
        CHECK( std::filesystem::exists( dir / "state" / "run1a.published" ) );
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "run1a.journal" ) );
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "run1a.ckpt" ) );

        // Rigorous scratch inspection: no .tl or .part files under scratchRoot
        std::uint64_t orphanedTiles = 0;
        if ( std::filesystem::exists( dir / "scratch" ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir / "scratch" ) )
            {
                if ( entry.is_regular_file() )
                {
                    const auto ext = entry.path().extension();
                    if ( ext == ".tl" || ext == ".part" )
                        ++orphanedTiles;
                }
            }
        }
        CHECK( orphanedTiles == 0 );

        // Second run: bypasses execution via .published marker
        *kernelCounter = 0;
        RSOperatorContext ctx2( dir.generic_string() );
        const auto res2 = runChunkedOperator( "test:challenge_sweep", Json::Value( Json::objectValue ),
                                              ctx2, customPart,
                                              TileRunDeterminism::BitExact, challengeKernel, sink, opts );
        CHECK( res2.alreadyPublished );
        CHECK( res2.tilesComputed == 0 );
        CHECK( *kernelCounter == 0 );

        // Still zero scratch files
        orphanedTiles = 0;
        if ( std::filesystem::exists( dir / "scratch" ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir / "scratch" ) )
            {
                if ( entry.is_regular_file() )
                {
                    const auto ext = entry.path().extension();
                    if ( ext == ".tl" || ext == ".part" )
                        ++orphanedTiles;
                }
            }
        }
        CHECK( orphanedTiles == 0 );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }

    SECTION( "1B: cleanupScratchOnSuccess = true with default scratchRoot sweeps rt-* under workDir" )
    {
        const auto dir = makeTempDir( "chal_sweep_default" );
        RSOperatorContext ctx( dir.generic_string() );
        ChunkedRunOptions opts;
        opts.scratchRoot = ""; // triggers default to workDir
        opts.resumeStateBase = ( dir / "state" / "run1b" ).generic_string();
        opts.cleanupScratchOnSuccess = true;

        *kernelCounter = 0;
        const auto res = runChunkedOperator( "test:challenge_sweep_def", Json::Value( Json::objectValue ),
                                             ctx, customPart,
                                             TileRunDeterminism::BitExact, challengeKernel, sink, opts );

        CHECK( res.tilesComputed == 48 );
        CHECK( std::filesystem::exists( dir / "state" / "run1b.published" ) );

        // Under dir (which is workDir), there must be no orphaned .tl files
        std::uint64_t orphanedTiles = 0;
        for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir ) )
        {
            if ( entry.is_regular_file() )
            {
                const auto ext = entry.path().extension();
                if ( ext == ".tl" || ext == ".part" )
                    ++orphanedTiles;
            }
        }
        CHECK( orphanedTiles == 0 );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }

    SECTION( "1C: cleanupScratchOnSuccess = false preserves all 48 tiles, readable via DiskTileStore" )
    {
        const auto dir = makeTempDir( "chal_preserve" );
        const auto qDir = QString::fromStdString( dir.generic_string() );
        const QString tempOut = qDir + QStringLiteral( "/temp_1c.tif" );
        const QString stableOut = qDir + QStringLiteral( "/stable_1c.tif" );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        RSOperatorContext ctx( dir.generic_string() );
        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "run1c" ).generic_string();
        opts.cleanupScratchOnSuccess = false;

        ChunkedOutputCommitSpec commitSpec;
        commitSpec.committer = &committer;
        commitSpec.tempPath = tempOut;
        commitSpec.stablePath = stableOut;
        opts.commitSpec = commitSpec;
        opts.publish = [&]() {
            createSyntheticGeoTiff( tempOut, 24, 16 );
        };

        *kernelCounter = 0;
        const auto res1 = runChunkedOperator( "test:challenge_preserve", Json::Value( Json::objectValue ),
                                              ctx, customPart,
                                              TileRunDeterminism::BitExact, challengeKernel, sink, opts );

        CHECK( res1.tilesComputed == 48 );
        CHECK( std::filesystem::exists( dir / "state" / "run1c.published" ) );
        CHECK( std::filesystem::exists( dir / "state" / "run1c.journal" ) );

        // Scan scratch tiles and verify readability
        std::vector<std::filesystem::path> tilePaths;
        if ( std::filesystem::exists( dir / "scratch" ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir / "scratch" ) )
            {
                if ( entry.path().extension() == ".tl" )
                    tilePaths.push_back( entry.path() );
            }
        }
        REQUIRE( tilePaths.size() == 48 );

        // Verify each tile is valid and decodeable via DiskTileStore
        for ( const auto &tp : tilePaths )
        {
            const auto payload = DiskTileStore::readFile( tp.generic_string() );
            REQUIRE( payload.pixels != nullptr );
            CHECK( payload.pixels->size() == payload.spec.bufferElementCount() );
            CHECK( payload.spec.bands == 2 );
        }

        // Re-running with cleanupScratchOnSuccess = false still preserves tiles and skips recomputation
        *kernelCounter = 0;
        RSOperatorContext ctx2( dir.generic_string() );
        const auto res2 = runChunkedOperator( "test:challenge_preserve", Json::Value( Json::objectValue ),
                                              ctx2, customPart,
                                              TileRunDeterminism::BitExact, challengeKernel, sink, opts );
        CHECK( res2.alreadyPublished );
        CHECK( res2.tilesComputed == 0 );
        CHECK( *kernelCounter == 0 );

        // Tile files remain intact
        std::uint64_t tileCountAfter = 0;
        for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir / "scratch" ) )
        {
            if ( entry.path().extension() == ".tl" )
                ++tileCountAfter;
        }
        CHECK( tileCountAfter == 48 );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }
}

TEST_CASE( "M2-Challenge-2: Parameter validation rigor and side-effect isolation",
           "[chunk][adoption][validation][empirical_challenge]" )
{
    const auto dir = makeTempDir( "chal_validation" );
    const auto qDir = QString::fromStdString( dir.generic_string() );
    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    RSOperatorContext ctx( dir.generic_string() );
    ChunkTileKernel validKernel = []( const TileSpec &s ) { return referenceKernel( s ); };
    std::function<void( const TilePayload & )> validSink = []( const TilePayload & ) {};

    auto countFilesInDir = []( const std::filesystem::path &p ) -> std::uint64_t {
        std::uint64_t count = 0;
        if ( std::filesystem::exists( p ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( p ) )
            {
                if ( entry.is_regular_file() )
                    ++count;
            }
        }
        return count;
    };

    SECTION( "Empty operatorId throws InvalidParameter with zero disk mutations" )
    {
        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "test" ).generic_string();

        bool threwCorrect = false;
        try
        {
            runChunkedOperator( "", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, validKernel, validSink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            threwCorrect = ( e.code() == ErrorCode::InvalidParameter &&
                             std::string( e.what() ).find( "operatorId is required" ) != std::string::npos );
        }
        CHECK( threwCorrect );
        CHECK( countFilesInDir( dir ) == 0 );
    }

    SECTION( "Null kernel or sink throws InvalidParameter with zero disk mutations" )
    {
        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();

        bool threwKernel = false;
        try
        {
            runChunkedOperator( "test:valid_op", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, nullptr, validSink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            threwKernel = ( e.code() == ErrorCode::InvalidParameter &&
                            std::string( e.what() ).find( "kernel and sink are required" ) != std::string::npos );
        }
        CHECK( threwKernel );

        bool threwSink = false;
        try
        {
            runChunkedOperator( "test:valid_op", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, validKernel, nullptr, opts );
        }
        catch ( const RSOperatorError &e )
        {
            threwSink = ( e.code() == ErrorCode::InvalidParameter &&
                          std::string( e.what() ).find( "kernel and sink are required" ) != std::string::npos );
        }
        CHECK( threwSink );
        CHECK( countFilesInDir( dir ) == 0 );
    }

    SECTION( "CommitSpec invalid parameter matrix in Resumable and Pipeline modes" )
    {
        for ( auto mode : { ChunkedRunOptions::Mode::Resumable, ChunkedRunOptions::Mode::Pipeline } )
        {
            // Case 1: committer == nullptr
            {
                ChunkedRunOptions opts;
                opts.mode = mode;
                opts.scratchRoot = ( dir / "scratch" ).generic_string();
                ChunkedOutputCommitSpec spec;
                spec.committer = nullptr;
                spec.tempPath = qDir + QStringLiteral( "/temp.tif" );
                spec.stablePath = qDir + QStringLiteral( "/stable.tif" );
                opts.commitSpec = spec;

                bool threw = false;
                try
                {
                    runChunkedOperator( "test:valid_op", Json::Value( Json::objectValue ),
                                        ctx, referencePartition(),
                                        TileRunDeterminism::BitExact, validKernel, validSink, opts );
                }
                catch ( const RSOperatorError &e )
                {
                    threw = ( e.code() == ErrorCode::InvalidParameter &&
                              std::string( e.what() ).find( "OutputCommitter is null" ) != std::string::npos );
                }
                CHECK( threw );
                CHECK( countFilesInDir( dir ) == 0 );
            }

            // Case 2: empty tempPath
            {
                ChunkedRunOptions opts;
                opts.mode = mode;
                opts.scratchRoot = ( dir / "scratch" ).generic_string();
                ChunkedOutputCommitSpec spec;
                spec.committer = &committer;
                spec.tempPath = QStringLiteral( "" );
                spec.stablePath = qDir + QStringLiteral( "/stable.tif" );
                opts.commitSpec = spec;

                bool threw = false;
                try
                {
                    runChunkedOperator( "test:valid_op", Json::Value( Json::objectValue ),
                                        ctx, referencePartition(),
                                        TileRunDeterminism::BitExact, validKernel, validSink, opts );
                }
                catch ( const RSOperatorError &e )
                {
                    threw = ( e.code() == ErrorCode::InvalidParameter );
                }
                CHECK( threw );
                CHECK( countFilesInDir( dir ) == 0 );
            }

            // Case 3: whitespace tempPath
            {
                ChunkedRunOptions opts;
                opts.mode = mode;
                opts.scratchRoot = ( dir / "scratch" ).generic_string();
                ChunkedOutputCommitSpec spec;
                spec.committer = &committer;
                spec.tempPath = QStringLiteral( "   \t\n   " );
                spec.stablePath = qDir + QStringLiteral( "/stable.tif" );
                opts.commitSpec = spec;

                bool threw = false;
                try
                {
                    runChunkedOperator( "test:valid_op", Json::Value( Json::objectValue ),
                                        ctx, referencePartition(),
                                        TileRunDeterminism::BitExact, validKernel, validSink, opts );
                }
                catch ( const RSOperatorError &e )
                {
                    threw = ( e.code() == ErrorCode::InvalidParameter );
                }
                CHECK( threw );
                CHECK( countFilesInDir( dir ) == 0 );
            }

            // Case 4: empty stablePath
            {
                ChunkedRunOptions opts;
                opts.mode = mode;
                opts.scratchRoot = ( dir / "scratch" ).generic_string();
                ChunkedOutputCommitSpec spec;
                spec.committer = &committer;
                spec.tempPath = qDir + QStringLiteral( "/temp.tif" );
                spec.stablePath = QStringLiteral( "" );
                opts.commitSpec = spec;

                bool threw = false;
                try
                {
                    runChunkedOperator( "test:valid_op", Json::Value( Json::objectValue ),
                                        ctx, referencePartition(),
                                        TileRunDeterminism::BitExact, validKernel, validSink, opts );
                }
                catch ( const RSOperatorError &e )
                {
                    threw = ( e.code() == ErrorCode::InvalidParameter );
                }
                CHECK( threw );
                CHECK( countFilesInDir( dir ) == 0 );
            }

            // Case 5: whitespace stablePath
            {
                ChunkedRunOptions opts;
                opts.mode = mode;
                opts.scratchRoot = ( dir / "scratch" ).generic_string();
                ChunkedOutputCommitSpec spec;
                spec.committer = &committer;
                spec.tempPath = qDir + QStringLiteral( "/temp.tif" );
                spec.stablePath = QStringLiteral( "  \n\t  " );
                opts.commitSpec = spec;

                bool threw = false;
                try
                {
                    runChunkedOperator( "test:valid_op", Json::Value( Json::objectValue ),
                                        ctx, referencePartition(),
                                        TileRunDeterminism::BitExact, validKernel, validSink, opts );
                }
                catch ( const RSOperatorError &e )
                {
                    threw = ( e.code() == ErrorCode::InvalidParameter );
                }
                CHECK( threw );
                CHECK( countFilesInDir( dir ) == 0 );
            }
        }
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "M2-Challenge-3: Error abandonment and temporary file cleanup on unhandled exceptions",
           "[chunk][adoption][abandon][empirical_challenge]" )
{
    const auto dir = makeTempDir( "chal_abandon" );
    const auto qDir = QString::fromStdString( dir.generic_string() );
    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    RSOperatorContext ctx( dir.generic_string() );

    SECTION( "3A: Kernel exception in Resumable mode wipes scratchRoot/rt-*, wipes journal, and discards tempPath" )
    {
        const QString tempFile = qDir + QStringLiteral( "/temp_3a.tif" );
        createSyntheticGeoTiff( tempFile, 24, 8 );
        REQUIRE( QFile::exists( tempFile ) );

        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "run3a" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempFile;
        spec.stablePath = qDir + QStringLiteral( "/stable_3a.tif" );
        opts.commitSpec = spec;

        // Fails after 4 tiles
        ChunkTileKernel badKernel = []( const TileSpec &s ) -> std::vector<float> {
            if ( s.index >= 4 )
                throw std::runtime_error( "unhandled kernel crash on tile 4" );
            return referenceKernel( s );
        };
        std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

        bool threw = false;
        try
        {
            runChunkedOperator( "test:challenge_bad_kernel", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, badKernel, sink, opts );
        }
        catch ( const std::exception & )
        {
            threw = true;
        }
        CHECK( threw );

        // 1. Temporary output was discarded
        CHECK_FALSE( QFile::exists( tempFile ) );

        // 2. Journal, ckpt, and published markers do NOT exist
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "run3a.journal" ) );
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "run3a.ckpt" ) );
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "run3a.published" ) );

        // 3. Scratch directory rt-* under scratchRoot was completely swept by run.abandon()
        std::uint64_t orphanedTiles = 0;
        if ( std::filesystem::exists( dir / "scratch" ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir / "scratch" ) )
            {
                if ( entry.path().extension() == ".tl" )
                    ++orphanedTiles;
            }
        }
        CHECK( orphanedTiles == 0 );
    }

    SECTION( "3B: Sink exception in Resumable mode wipes scratch, journal, and tempPath" )
    {
        const QString tempFile = qDir + QStringLiteral( "/temp_3b.tif" );
        createSyntheticGeoTiff( tempFile, 24, 8 );
        REQUIRE( QFile::exists( tempFile ) );

        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "run3b" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempFile;
        spec.stablePath = qDir + QStringLiteral( "/stable_3b.tif" );
        opts.commitSpec = spec;

        ChunkTileKernel kernel = []( const TileSpec &s ) { return referenceKernel( s ); };
        std::uint64_t sunk = 0;
        std::function<void( const TilePayload & )> badSink = [&]( const TilePayload & ) {
            if ( ++sunk >= 5 )
                throw std::runtime_error( "unhandled sink crash on tile 5" );
        };

        bool threw = false;
        try
        {
            runChunkedOperator( "test:challenge_bad_sink", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, kernel, badSink, opts );
        }
        catch ( const std::exception & )
        {
            threw = true;
        }
        CHECK( threw );

        // Scratch, journal, published marker, and tempFile must all be gone
        CHECK_FALSE( QFile::exists( tempFile ) );
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "run3b.journal" ) );
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "run3b.published" ) );

        std::uint64_t orphanedTiles = 0;
        if ( std::filesystem::exists( dir / "scratch" ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir / "scratch" ) )
            {
                if ( entry.path().extension() == ".tl" )
                    ++orphanedTiles;
            }
        }
        CHECK( orphanedTiles == 0 );
    }

    SECTION( "3C: options.publish callback exception wipes scratch, journal, and tempPath, prevents marker" )
    {
        const QString tempFile = qDir + QStringLiteral( "/temp_3c.tif" );
        createSyntheticGeoTiff( tempFile, 24, 8 );
        REQUIRE( QFile::exists( tempFile ) );

        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "run3c" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempFile;
        spec.stablePath = qDir + QStringLiteral( "/stable_3c.tif" );
        opts.commitSpec = spec;

        // options.publish throws unhandled exception
        opts.publish = [&]() {
            throw std::runtime_error( "unhandled failure inside publish callback" );
        };

        ChunkTileKernel kernel = []( const TileSpec &s ) { return referenceKernel( s ); };
        std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

        bool threw = false;
        try
        {
            runChunkedOperator( "test:challenge_bad_publish", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( const std::exception & )
        {
            threw = true;
        }
        CHECK( threw );

        // Verify everything swept
        CHECK_FALSE( QFile::exists( tempFile ) );
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "run3c.journal" ) );
        CHECK_FALSE( std::filesystem::exists( dir / "state" / "run3c.published" ) );

        std::uint64_t orphanedTiles = 0;
        if ( std::filesystem::exists( dir / "scratch" ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir / "scratch" ) )
            {
                if ( entry.path().extension() == ".tl" )
                    ++orphanedTiles;
            }
        }
        CHECK( orphanedTiles == 0 );
    }

    SECTION( "3D: Kernel exception in Pipeline mode discards temporary output" )
    {
        const QString tempFile = qDir + QStringLiteral( "/temp_3d.tif" );
        createSyntheticGeoTiff( tempFile, 24, 8 );
        REQUIRE( QFile::exists( tempFile ) );

        ChunkedRunOptions opts;
        opts.mode = ChunkedRunOptions::Mode::Pipeline;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempFile;
        spec.stablePath = qDir + QStringLiteral( "/stable_3d.tif" );
        opts.commitSpec = spec;

        ChunkTileKernel badKernel = []( const TileSpec &s ) -> std::vector<float> {
            if ( s.index >= 3 )
                throw std::runtime_error( "pipeline kernel error" );
            return referenceKernel( s );
        };
        std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

        bool threw = false;
        try
        {
            runChunkedOperator( "test:challenge_pipe_fault", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, badKernel, sink, opts );
        }
        catch ( const std::exception & )
        {
            threw = true;
        }
        CHECK( threw );

        // Temporary output was swept by discardTemporary
        CHECK_FALSE( QFile::exists( tempFile ) );
    }

    SECTION( "3E: Cancellation in Pipeline mode discards temporary output" )
    {
        const QString tempFile = qDir + QStringLiteral( "/temp_3e.tif" );
        createSyntheticGeoTiff( tempFile, 24, 8 );
        REQUIRE( QFile::exists( tempFile ) );

        ChunkedRunOptions opts;
        opts.mode = ChunkedRunOptions::Mode::Pipeline;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempFile;
        spec.stablePath = qDir + QStringLiteral( "/stable_3e.tif" );
        opts.commitSpec = spec;

        std::uint64_t sunk = 0;
        ctx.setCancelCallback( [&sunk] { return sunk >= 3; } );

        ChunkTileKernel kernel = []( const TileSpec &s ) { return referenceKernel( s ); };
        std::function<void( const TilePayload & )> sink = [&]( const TilePayload & ) {
            ++sunk;
        };

        bool threwCancelled = false;
        try
        {
            runChunkedOperator( "test:challenge_pipe_cancel", Json::Value( Json::objectValue ),
                                ctx, referencePartition(),
                                TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            threwCancelled = ( e.code() == ErrorCode::Cancelled );
        }
        CHECK( threwCancelled );

        // Temporary output was swept by discardTemporary
        CHECK_FALSE( QFile::exists( tempFile ) );
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}


// tests/test_challenger_m2_1_harness.cpp
// Empirical Challenger Stress Harness for Milestone 2 Resumability & Exactly-Once Invariants
// Authored by challenger_m2_1

#include "operators/framework/chunk_error_bridge.h"
#include "operators/framework/chunked_run.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "runtime/chunk/disk_tile_store.h"
#include "runtime/chunk/resumable_tile_run.h"
#include "runtime/chunk/tile_run_contract.h"

#include "data/data_manager.h"
#include "processing/framework/output_committer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <gdal.h>

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace sicnu::operators;
using namespace sicnu::runtime::chunk;

#define HARNESS_ASSERT( cond, msg )                                                    \
    do                                                                                 \
    {                                                                                  \
        if ( !( cond ) )                                                               \
        {                                                                              \
            std::cerr << "[-] HARNESS ASSERTION FAILED: " << msg << "\n    at "        \
                      << __FILE__ << ":" << __LINE__ << " (" #cond ")" << std::endl;   \
            std::exit( 1 );                                                            \
        }                                                                              \
    } while ( 0 )

namespace
{

constexpr int kTotalTiles = 32;
constexpr int kTileW = 4;
constexpr int kTileH = 4;
constexpr int kBands = 2;

// Closed-form deterministic truth oracle
float truthValue( std::uint64_t tileIndex, std::uint64_t pixel, int band )
{
    return static_cast<float>( ( tileIndex * 41 + pixel * 13 + static_cast<std::uint64_t>( band ) )
                               % 353 )
           / 353.0f;
}

std::vector<float> generateReferenceKernel( const TileSpec &s )
{
    std::vector<float> buffer( s.bufferElementCount() );
    for ( int b = 0; b < s.bands; ++b )
    {
        for ( int y = 0; y < s.bufferHeight; ++y )
        {
            for ( int x = 0; x < s.bufferWidth; ++x )
            {
                const std::uint64_t pixel =
                    static_cast<std::uint64_t>( y ) * s.bufferWidth + x;
                buffer[static_cast<size_t>( b ) * s.bufferWidth * s.bufferHeight + pixel] =
                    truthValue( s.index, pixel, b );
            }
        }
    }
    return buffer;
}

TileRunPartition makePartition32()
{
    TileRunPartition p;
    p.rasterWidth = 32;
    p.rasterHeight = 16;
    p.tileWidth = kTileW;
    p.tileHeight = kTileH;
    p.bands = kBands;
    return p;
}

std::vector<float> generateTruthRaster()
{
    const TileRunPartition p = makePartition32();
    std::vector<float> truth;
    truth.reserve( static_cast<size_t>( p.totalTiles() ) * kTileW * kTileH * kBands );
    for ( std::uint64_t t = 0; t < p.totalTiles(); ++t )
    {
        const TileSpec s = tileSpecAt( p, t );
        for ( int b = 0; b < s.bands; ++b )
        {
            for ( int y = 0; y < s.height; ++y )
            {
                for ( int x = 0; x < s.width; ++x )
                {
                    const std::uint64_t pixel =
                        static_cast<std::uint64_t>( y ) * s.bufferWidth + x;
                    truth.push_back( truthValue( t, pixel, b ) );
                }
            }
        }
    }
    return truth;
}

std::filesystem::path createTempDir( const char *tag )
{
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path( ec );
    if ( ec )
        base = std::filesystem::current_path();
    static std::atomic<unsigned> counter{ 0 };
    const auto dir = base / ( "challenger-m2-" + std::string( tag ) + "-"
                              + std::to_string( ::getpid() ) + "-"
                              + std::to_string( counter.fetch_add( 1 ) ) );
    std::filesystem::create_directories( dir, ec );
    return dir;
}

void writeSyntheticGeoTiff( const QString &path, int w = 32, int h = 16 )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    HARNESS_ASSERT( driver != nullptr, "GTiff driver must be available" );
    QDir().mkpath( QFileInfo( path ).absolutePath() );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr );
    HARNESS_ASSERT( ds != nullptr, "GDALCreate must succeed" );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( h ), 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    GDALSetProjection(
        ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
            "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( static_cast<size_t>( w ), 1.234f );
    for ( int row = 0; row < h; ++row )
    {
        const CPLErr ioErr = GDALRasterIO( band, GF_Write, 0, row, w, 1, line.data(), w, 1, GDT_Float32, 0, 0 );
        HARNESS_ASSERT( ioErr == CE_None, "GDALRasterIO must succeed" );
    }
    GDALClose( ds );
}

} // namespace

// ============================================================================
// Suite 1: Interrupted Run and Resume Precision Oracle
// ============================================================================
void testInterruptedRunAndResume()
{
    std::cout << "[*] Running Suite 1: Interrupted Run and Resume Precision Oracle..." << std::endl;

    const std::vector<std::uint64_t> interruptPoints = { 0, 1, 8, 17, 31 };
    const auto truth = generateTruthRaster();

    for ( std::uint64_t interruptAt : interruptPoints )
    {
        const auto dir = createTempDir( "interrupted" );
        const auto qDir = QString::fromStdString( dir.generic_string() );
        const QString tempOut = qDir + QStringLiteral( "/temp_output.tif" );
        const QString stableOut = qDir + QStringLiteral( "/stable_output.tif" );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        ChunkedRunOptions options;
        options.scratchRoot = ( dir / "scratch" ).generic_string();
        options.resumeStateBase = ( dir / "state" / "run" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        options.commitSpec = spec;
        options.publish = [&]() {
            writeSyntheticGeoTiff( tempOut, 32, 16 );
        };

        // Phase 1: Run with cooperative cancellation at interruptAt tiles
        std::uint64_t sinkCount1 = 0;
        std::atomic<std::uint64_t> kernelCalls1{ 0 };
        RSOperatorContext ctx1( dir.generic_string() );
        ctx1.setCancelCallback( [&]() { return sinkCount1 >= interruptAt; } );

        ChunkTileKernel kernel1 = [&]( const TileSpec &s ) {
            kernelCalls1.fetch_add( 1 );
            return generateReferenceKernel( s );
        };
        std::vector<float> sinkOutput1;
        std::function<void( const TilePayload & )> sink1 = [&]( const TilePayload &p ) {
            ++sinkCount1;
            for ( int b = 0; b < p.spec.bands; ++b )
                for ( int y = 0; y < p.spec.height; ++y )
                    for ( int x = 0; x < p.spec.width; ++x )
                        sinkOutput1.push_back( ( *p.pixels )[static_cast<size_t>( b )
                                                                  * p.spec.bufferWidth
                                                                      * p.spec.bufferHeight
                                                              + static_cast<size_t>( y )
                                                                    * p.spec.bufferWidth
                                                              + x] );
        };

        bool cancelledCaught = false;
        try
        {
            runChunkedOperator( "test:adopter", Json::Value( Json::objectValue ),
                                ctx1, makePartition32(),
                                TileRunDeterminism::BitExact, kernel1, sink1, options );
        }
        catch ( const RSOperatorError &e )
        {
            if ( e.code() == ErrorCode::Cancelled )
                cancelledCaught = true;
        }

        if ( interruptAt < 32 )
        {
            HARNESS_ASSERT( cancelledCaught, "Must catch Cancelled error when cancelled" );
            HARNESS_ASSERT( !QFile::exists( stableOut ), "Stable output must not exist after cancelled run" );
            HARNESS_ASSERT( !std::filesystem::exists( dir / "state" / "run.published" ), "Published marker must not exist" );
        }

        // Count preserved scratch tiles on disk
        std::uint64_t preservedTiles = 0;
        if ( std::filesystem::exists( dir / "scratch" ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir / "scratch" ) )
            {
                if ( entry.path().extension() == ".tl" )
                    ++preservedTiles;
            }
        }
        HARNESS_ASSERT( preservedTiles >= interruptAt, "Scratch tiles up to interrupt point must be preserved on disk" );

        // Phase 2: Resume run with clean context to completion
        RSOperatorContext ctx2( dir.generic_string() );
        std::uint64_t sinkCount2 = 0;
        std::atomic<std::uint64_t> kernelCalls2{ 0 };
        ChunkTileKernel kernel2 = [&]( const TileSpec &s ) {
            kernelCalls2.fetch_add( 1 );
            return generateReferenceKernel( s );
        };
        std::vector<float> sinkOutput2;
        std::function<void( const TilePayload & )> sink2 = [&]( const TilePayload &p ) {
            ++sinkCount2;
            for ( int b = 0; b < p.spec.bands; ++b )
                for ( int y = 0; y < p.spec.height; ++y )
                    for ( int x = 0; x < p.spec.width; ++x )
                        sinkOutput2.push_back( ( *p.pixels )[static_cast<size_t>( b )
                                                                  * p.spec.bufferWidth
                                                                      * p.spec.bufferHeight
                                                              + static_cast<size_t>( y )
                                                                    * p.spec.bufferWidth
                                                              + x] );
        };

        const auto resumeResult = runChunkedOperator(
            "test:adopter", Json::Value( Json::objectValue ),
            ctx2, makePartition32(),
            TileRunDeterminism::BitExact, kernel2, sink2, options );

        HARNESS_ASSERT( resumeResult.totalTiles == 32, "Total tiles must be 32" );
        HARNESS_ASSERT( resumeResult.tilesReused == preservedTiles, "Reused tiles must exactly match preserved tiles" );
        HARNESS_ASSERT( resumeResult.tilesComputed == 32 - preservedTiles, "Computed tiles must exactly equal remaining" );
        HARNESS_ASSERT( kernelCalls2.load() == 32 - preservedTiles, "Kernel must be invoked ONLY for remaining tiles" );
        HARNESS_ASSERT( sinkCount2 == 32, "Sink must receive all 32 tiles on resume" );
        HARNESS_ASSERT( sinkOutput2 == truth, "Assembled output from resumed run must match closed-form truth bit-for-bit" );

        // Post-conditions: stable file exists, asset registered, marker exists, scratch swept
        HARNESS_ASSERT( QFile::exists( stableOut ), "Stable output must exist after successful resume" );
        HARNESS_ASSERT( !QFile::exists( tempOut ), "Temp file must be consumed" );
        HARNESS_ASSERT( !resumeResult.publishedAssetId.isEmpty(), "Published AssetId must be populated" );
        HARNESS_ASSERT( std::filesystem::exists( dir / "state" / "run.published" ), "Published marker must exist" );
        HARNESS_ASSERT( !std::filesystem::exists( dir / "state" / "run.journal" ), "Journal must be swept after publish" );

        // Scratch tile files must be swept
        bool leftoverTiles = false;
        if ( std::filesystem::exists( dir / "scratch" ) )
        {
            for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir / "scratch" ) )
            {
                if ( entry.path().extension() == ".tl" )
                    leftoverTiles = true;
            }
        }
        HARNESS_ASSERT( !leftoverTiles, "All scratch .tl files must be swept by cleanupAfterPublish" );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }

    std::cout << "  -> Suite 1 PASSED." << std::endl;
}

// ============================================================================
// Suite 2: Rerun Bypass and Exactly-Once Invariant Oracle
// ============================================================================
void testRerunBypassAndExactlyOnce()
{
    std::cout << "[*] Running Suite 2: Rerun Bypass and Exactly-Once Invariant Oracle..." << std::endl;

    const auto dir = createTempDir( "rerun_bypass" );
    const auto qDir = QString::fromStdString( dir.generic_string() );
    const QString tempOut = qDir + QStringLiteral( "/temp_output.tif" );
    const QString stableOut = qDir + QStringLiteral( "/stable_output.tif" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    std::atomic<int> displayRequestedSignals{ 0 };
    QObject::connect( &committer, &sicnu::OutputCommitter::displayRequested,
                      [&]( sicnu::data::AssetId ) {
                          displayRequestedSignals.fetch_add( 1 );
                      } );

    ChunkedRunOptions options;
    options.scratchRoot = ( dir / "scratch" ).generic_string();
    options.resumeStateBase = ( dir / "state" / "run" ).generic_string();

    ChunkedOutputCommitSpec spec;
    spec.committer = &committer;
    spec.tempPath = tempOut;
    spec.stablePath = stableOut;
    spec.autoLoad = true;
    options.commitSpec = spec;
    std::atomic<int> publishCallbacks{ 0 };
    options.publish = [&]() {
        publishCallbacks.fetch_add( 1 );
        writeSyntheticGeoTiff( tempOut, 32, 16 );
    };

    std::atomic<std::uint64_t> kernelCalls{ 0 };
    ChunkTileKernel kernel = [&]( const TileSpec &s ) {
        kernelCalls.fetch_add( 1 );
        return generateReferenceKernel( s );
    };
    std::atomic<std::uint64_t> sinkCalls{ 0 };
    std::function<void( const TilePayload & )> sink = [&]( const TilePayload & ) {
        sinkCalls.fetch_add( 1 );
    };

    // First run: executes full kernel, commits via OutputCommitter
    RSOperatorContext ctx1( dir.generic_string() );
    const auto result1 = runChunkedOperator(
        "test:adopter", Json::Value( Json::objectValue ),
        ctx1, makePartition32(),
        TileRunDeterminism::BitExact, kernel, sink, options );

    HARNESS_ASSERT( !result1.alreadyPublished, "First run must not be marked alreadyPublished" );
    HARNESS_ASSERT( result1.tilesComputed == 32, "First run must compute all 32 tiles" );
    HARNESS_ASSERT( kernelCalls.load() == 32, "Kernel must be called 32 times" );
    HARNESS_ASSERT( sinkCalls.load() == 32, "Sink must be called 32 times" );
    HARNESS_ASSERT( publishCallbacks.load() == 1, "Publish callback must be called exactly once" );
    HARNESS_ASSERT( displayRequestedSignals.load() == 1, "displayRequested signal must be emitted exactly once" );
    HARNESS_ASSERT( !result1.publishedAssetId.isEmpty(), "Published AssetId must not be empty" );

    // Second run with IDENTICAL identity: must bypass completely
    kernelCalls.store( 0 );
    sinkCalls.store( 0 );
    RSOperatorContext ctx2( dir.generic_string() );
    const auto result2 = runChunkedOperator(
        "test:adopter", Json::Value( Json::objectValue ),
        ctx2, makePartition32(),
        TileRunDeterminism::BitExact, kernel, sink, options );

    HARNESS_ASSERT( result2.alreadyPublished, "Second run MUST be marked alreadyPublished" );
    HARNESS_ASSERT( result2.tilesComputed == 0, "Second run must compute 0 tiles" );
    HARNESS_ASSERT( result2.tilesReused == 0, "Second run must reuse 0 tiles (short-circuited)" );
    HARNESS_ASSERT( kernelCalls.load() == 0, "Kernel must NOT be called on bypass" );
    HARNESS_ASSERT( sinkCalls.load() == 0, "Sink must NOT be called on bypass" );
    HARNESS_ASSERT( publishCallbacks.load() == 1, "Publish callback must NOT be re-executed" );
    HARNESS_ASSERT( displayRequestedSignals.load() == 1, "displayRequested signal must NOT be re-emitted" );

    // Third run with IDENTICAL identity: verify stability across multiple executions
    RSOperatorContext ctx3( dir.generic_string() );
    const auto result3 = runChunkedOperator(
        "test:adopter", Json::Value( Json::objectValue ),
        ctx3, makePartition32(),
        TileRunDeterminism::BitExact, kernel, sink, options );
    HARNESS_ASSERT( result3.alreadyPublished, "Third run MUST be marked alreadyPublished" );
    HARNESS_ASSERT( kernelCalls.load() == 0, "Kernel must NOT be called on bypass" );
    HARNESS_ASSERT( displayRequestedSignals.load() == 1, "displayRequested signal must NOT be re-emitted" );

    // Fourth run with IDENTITY DRIFT: changed parameters must NOT bypass!
    Json::Value alteredParams( Json::objectValue );
    alteredParams["filter_threshold"] = 0.85;

    const QString stableOutDrift = qDir + QStringLiteral( "/stable_drift.tif" );
    const QString tempOutDrift = qDir + QStringLiteral( "/temp_drift.tif" );
    options.commitSpec->stablePath = stableOutDrift;
    options.commitSpec->tempPath = tempOutDrift;
    options.publish = [&]() {
        publishCallbacks.fetch_add( 1 );
        writeSyntheticGeoTiff( tempOutDrift, 32, 16 );
    };

    RSOperatorContext ctx4( dir.generic_string() );
    const auto result4 = runChunkedOperator(
        "test:adopter", alteredParams,
        ctx4, makePartition32(),
        TileRunDeterminism::BitExact, kernel, sink, options );

    HARNESS_ASSERT( !result4.alreadyPublished, "Drifted identity MUST NOT bypass" );
    HARNESS_ASSERT( result4.tilesComputed == 32, "Drifted identity must compute all 32 tiles" );
    HARNESS_ASSERT( kernelCalls.load() == 32, "Kernel must run for drifted identity" );
    HARNESS_ASSERT( displayRequestedSignals.load() == 2, "displayRequested signal must be emitted for the new run" );
    HARNESS_ASSERT( QFile::exists( stableOutDrift ), "Stable drift output must exist" );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
    std::cout << "  -> Suite 2 PASSED." << std::endl;
}

// ============================================================================
// Suite 3: Torn Journal Tail and Header Recovery Oracle
// ============================================================================
void testTornJournalRecovery()
{
    std::cout << "[*] Running Suite 3: Torn Journal Tail and Header Recovery Oracle..." << std::endl;

    // Subcase 3A: Torn commit line at tail (simulating process death mid-write)
    {
        const auto dir = createTempDir( "torn_tail" );
        TileRunSpec spec;
        spec.identity.operatorIdentity = 0x1234567812345678ull;
        spec.identity.inputIdentity = 0x8765432187654321ull;
        spec.partition = makePartition32();
        spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );

        ResumableTileRun::Config cfg;
        cfg.scratchRoot = ( dir / "scratch" ).generic_string();
        cfg.statePath = ( dir / "state" / "run" ).generic_string();
        const std::string journalPath = cfg.statePath + ".journal";

        // Step 1: Run 15 tiles and interrupt
        {
            ResumableTileRun run( spec, cfg );
            std::atomic<int> computes{ 0 };
            ResumableTileRun::Callbacks cb;
            cb.compute = [&]( const TileSpec &s ) {
                if ( ++computes > 15 )
                    throw ChunkCancelled();
                auto buf = std::make_shared<std::vector<float>>( generateReferenceKernel( s ) );
                return TilePayload{ s, std::move( buf ) };
            };
            cb.consume = []( const TilePayload & ) {};
            cb.publish = [] {};
            TileRunCancelSource cancel;
            bool caught = false;
            try
            {
                run.execute( cancel, cb );
            }
            catch ( const ChunkCancelled & )
            {
                caught = true;
            }
            HARNESS_ASSERT( caught, "Must catch cancel" );
        }

        // Verify 15 commit lines exist in journal
        std::error_code ec;
        const auto origSize = std::filesystem::file_size( journalPath, ec );
        HARNESS_ASSERT( !ec && origSize > 0, "Journal must exist with non-zero size" );

        // Read last line to verify format: "C 14 tile-14.tl\n"
        // Deliberately truncate the last 6 bytes to create a mid-append torn tail: "C 14 tile-"
        std::filesystem::resize_file( journalPath, origSize - 6, ec );
        HARNESS_ASSERT( !ec, "Journal resize must succeed" );

        // Step 2: Resume with torn tail.
        // It must detect torn line, truncate journal back to commit line 13, and reuse 14 tiles (0..13)
        std::atomic<std::uint64_t> resumedComputes{ 0 };
        std::atomic<std::uint64_t> resumedReuses{ 0 };
        std::vector<float> finalPixels;
        {
            ResumableTileRun run( spec, cfg );
            ResumableTileRun::Callbacks cb;
            cb.compute = [&]( const TileSpec &s ) {
                resumedComputes.fetch_add( 1 );
                auto buf = std::make_shared<std::vector<float>>( generateReferenceKernel( s ) );
                return TilePayload{ s, std::move( buf ) };
            };
            cb.consume = [&]( const TilePayload &p ) {
                for ( int b = 0; b < p.spec.bands; ++b )
                    for ( int y = 0; y < p.spec.height; ++y )
                        for ( int x = 0; x < p.spec.width; ++x )
                            finalPixels.push_back( ( *p.pixels )[static_cast<size_t>( b )
                                                                      * p.spec.bufferWidth
                                                                          * p.spec.bufferHeight
                                                                  + static_cast<size_t>( y )
                                                                        * p.spec.bufferWidth
                                                                  + x] );
            };
            cb.publish = [] {};
            TileRunCancelSource cancel;
            const auto r = run.execute( cancel, cb );

            HARNESS_ASSERT( r.totalTiles == 32, "Total tiles must be 32" );
            HARNESS_ASSERT( r.tilesReused == 14, "Must reuse 14 tiles (0 through 13)" );
            HARNESS_ASSERT( r.tilesComputed == 18, "Must compute remaining 18 tiles (14 through 31)" );
            HARNESS_ASSERT( resumedComputes.load() == 18, "Kernel must be called exactly 18 times" );
            HARNESS_ASSERT( finalPixels == generateTruthRaster(), "Output must be bit-exact identical to truth" );
        }

        std::filesystem::remove_all( dir, ec );
    }

    // Subcase 3B: Torn header (crash during initial creation)
    {
        const auto dir = createTempDir( "torn_header" );
        TileRunSpec spec;
        spec.identity.operatorIdentity = 0xAAAAAAAAAAAAAAAAull;
        spec.identity.inputIdentity = 0xBBBBBBBBBBBBBBBBull;
        spec.partition = makePartition32();
        spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );

        ResumableTileRun::Config cfg;
        cfg.scratchRoot = ( dir / "scratch" ).generic_string();
        cfg.statePath = ( dir / "state" / "run" ).generic_string();
        const std::string journalPath = cfg.statePath + ".journal";

        std::error_code ec;
        std::filesystem::create_directories( dir / "state", ec );
        // Write torn header without newline or identity key
        {
            std::ofstream out( journalPath, std::ios::binary | std::ios::trunc );
            out << "V 1"; // torn header!
            out.flush();
        }

        // Resume: must truncate to 0, start fresh, and succeed
        std::atomic<std::uint64_t> computes{ 0 };
        std::vector<float> finalPixels;
        {
            ResumableTileRun run( spec, cfg );
            ResumableTileRun::Callbacks cb;
            cb.compute = [&]( const TileSpec &s ) {
                computes.fetch_add( 1 );
                auto buf = std::make_shared<std::vector<float>>( generateReferenceKernel( s ) );
                return TilePayload{ s, std::move( buf ) };
            };
            cb.consume = [&]( const TilePayload &p ) {
                for ( int b = 0; b < p.spec.bands; ++b )
                    for ( int y = 0; y < p.spec.height; ++y )
                        for ( int x = 0; x < p.spec.width; ++x )
                            finalPixels.push_back( ( *p.pixels )[static_cast<size_t>( b )
                                                                      * p.spec.bufferWidth
                                                                          * p.spec.bufferHeight
                                                                  + static_cast<size_t>( y )
                                                                        * p.spec.bufferWidth
                                                                  + x] );
            };
            cb.publish = [] {};
            TileRunCancelSource cancel;
            const auto r = run.execute( cancel, cb );

            HARNESS_ASSERT( r.totalTiles == 32, "Total tiles must be 32" );
            HARNESS_ASSERT( r.tilesReused == 0, "Must reuse 0 tiles after torn header" );
            HARNESS_ASSERT( r.tilesComputed == 32, "Must compute all 32 tiles after torn header" );
            HARNESS_ASSERT( computes.load() == 32, "Kernel must run 32 times" );
            HARNESS_ASSERT( finalPixels == generateTruthRaster(), "Output must be bit-exact identical to truth" );
        }

        std::filesystem::remove_all( dir, ec );
    }

    // Subcase 3C: Mid-file corruption MUST fail closed (throw ChunkCorruptTile)
    {
        const auto dir = createTempDir( "mid_corrupt" );
        TileRunSpec spec;
        spec.identity.operatorIdentity = 0x1111111111111111ull;
        spec.identity.inputIdentity = 0x2222222222222222ull;
        spec.partition = makePartition32();
        spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );

        ResumableTileRun::Config cfg;
        cfg.scratchRoot = ( dir / "scratch" ).generic_string();
        cfg.statePath = ( dir / "state" / "run" ).generic_string();
        const std::string journalPath = cfg.statePath + ".journal";

        // Step 1: Run 10 tiles and interrupt
        {
            ResumableTileRun run( spec, cfg );
            std::atomic<int> computes{ 0 };
            ResumableTileRun::Callbacks cb;
            cb.compute = [&]( const TileSpec &s ) {
                if ( ++computes > 10 )
                    throw ChunkCancelled();
                auto buf = std::make_shared<std::vector<float>>( generateReferenceKernel( s ) );
                return TilePayload{ s, std::move( buf ) };
            };
            cb.consume = []( const TilePayload & ) {};
            cb.publish = [] {};
            TileRunCancelSource cancel;
            try { run.execute( cancel, cb ); } catch ( ... ) {}
        }

        // Invalidate a line in the MIDDLE of the journal (not the tail)
        {
            std::ifstream in( journalPath );
            std::vector<std::string> lines;
            std::string line;
            while ( std::getline( in, line ) )
                lines.push_back( line );
            in.close();

            HARNESS_ASSERT( lines.size() >= 5, "Journal must have at least 5 lines" );
            lines[3] = "CORRUPT_INVALID_RECORD 99999 junk";

            std::ofstream out( journalPath, std::ios::trunc );
            for ( const auto &l : lines )
                out << l << '\n';
            out.close();
        }

        // Step 2: Attempt execute - MUST fail closed with ChunkCorruptTile
        bool caughtCorruptTile = false;
        try
        {
            ResumableTileRun run( spec, cfg );
            ResumableTileRun::Callbacks cb;
            cb.compute = []( const TileSpec &s ) {
                auto buf = std::make_shared<std::vector<float>>( generateReferenceKernel( s ) );
                return TilePayload{ s, std::move( buf ) };
            };
            cb.consume = []( const TilePayload & ) {};
            cb.publish = [] {};
            TileRunCancelSource cancel;
            run.execute( cancel, cb );
        }
        catch ( const ChunkCorruptTile & )
        {
            caughtCorruptTile = true;
        }

        HARNESS_ASSERT( caughtCorruptTile, "Mid-file journal corruption MUST throw ChunkCorruptTile (fail closed)" );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }

    std::cout << "  -> Suite 3 PASSED." << std::endl;
}

// ============================================================================
// Suite 4: Corrupt Tile Self-Healing Oracle (R4)
// ============================================================================
void testCorruptTileSelfHealing()
{
    std::cout << "[*] Running Suite 4: Corrupt Tile Self-Healing Oracle (R4)..." << std::endl;

    const auto dir = createTempDir( "self_heal" );
    TileRunSpec spec;
    spec.identity.operatorIdentity = 0x3333333333333333ull;
    spec.identity.inputIdentity = 0x4444444444444444ull;
    spec.partition = makePartition32();
    spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );

    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();

    // Step 1: Run 12 tiles and interrupt
    {
        ResumableTileRun run( spec, cfg );
        std::atomic<int> computes{ 0 };
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            if ( ++computes > 12 )
                throw ChunkCancelled();
            auto buf = std::make_shared<std::vector<float>>( generateReferenceKernel( s ) );
            return TilePayload{ s, std::move( buf ) };
        };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        TileRunCancelSource cancel;
        try { run.execute( cancel, cb ); } catch ( ... ) {}
    }

    // Step 2: Corrupt tile-5.tl on disk (flip payload bytes near the end)
    ResumableTileRun probeRun( spec, cfg );
    const auto tileFile = std::filesystem::path( cfg.scratchRoot )
                          / probeRun.runKey() / "tile-5.tl";
    HARNESS_ASSERT( std::filesystem::exists( tileFile ), "tile-5.tl must exist before corruption" );

    {
        const auto size = std::filesystem::file_size( tileFile );
        std::fstream f( tileFile, std::ios::in | std::ios::out | std::ios::binary );
        f.seekg( static_cast<std::streamoff>( size - 4 ) );
        char b = 0;
        f.read( &b, 1 );
        f.seekp( static_cast<std::streamoff>( size - 4 ) );
        b ^= 0x7F; // flip bits
        f.write( &b, 1 );
        f.close();
    }

    // Step 3: Resume.
    // 12 tiles were in journal (0..11).
    // tile-5 is corrupted -> self-heals by recomputing.
    // other 11 tiles (0..4, 6..11) are reused.
    // remaining 20 tiles (12..31) are computed.
    // Total computed: 1 + 20 = 21. Total reused: 11.
    std::atomic<std::uint64_t> computes{ 0 };
    std::vector<float> finalPixels;
    {
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            computes.fetch_add( 1 );
            auto buf = std::make_shared<std::vector<float>>( generateReferenceKernel( s ) );
            return TilePayload{ s, std::move( buf ) };
        };
        cb.consume = [&]( const TilePayload &p ) {
            for ( int b = 0; b < p.spec.bands; ++b )
                for ( int y = 0; y < p.spec.height; ++y )
                    for ( int x = 0; x < p.spec.width; ++x )
                        finalPixels.push_back( ( *p.pixels )[static_cast<size_t>( b )
                                                                  * p.spec.bufferWidth
                                                                      * p.spec.bufferHeight
                                                              + static_cast<size_t>( y )
                                                                    * p.spec.bufferWidth
                                                              + x] );
        };
        cb.publish = [] {};
        TileRunCancelSource cancel;
        const auto r = run.execute( cancel, cb );

        HARNESS_ASSERT( r.totalTiles == 32, "Total tiles must be 32" );
        HARNESS_ASSERT( r.tilesReused == 11, "Must reuse 11 intact tiles" );
        HARNESS_ASSERT( r.tilesComputed == 21, "Must compute 21 tiles (1 self-healed + 20 remaining)" );
        HARNESS_ASSERT( computes.load() == 21, "Kernel must run exactly 21 times" );
        HARNESS_ASSERT( finalPixels == generateTruthRaster(), "Final healed output must match truth bit-for-bit" );
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
    std::cout << "  -> Suite 4 PASSED." << std::endl;
}

// ============================================================================
// Suite 5: Scratch Directory Lifecycle and Error Scenarios
// ============================================================================
void testScratchLifecycleAndErrors()
{
    std::cout << "[*] Running Suite 5: Scratch Directory Lifecycle and Error Scenarios..." << std::endl;

    // Subcase 5A: Abnormal exception in kernel -> abandon() sweeps scratch + discards temp output
    {
        const auto dir = createTempDir( "abnormal_kernel" );
        const auto qDir = QString::fromStdString( dir.generic_string() );
        const QString tempFile = qDir + QStringLiteral( "/temp_discard.tif" );
        writeSyntheticGeoTiff( tempFile, 32, 16 );
        HARNESS_ASSERT( QFile::exists( tempFile ), "tempFile must exist before test" );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "run" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempFile;
        spec.stablePath = qDir + QStringLiteral( "/stable.tif" );
        opts.commitSpec = spec;

        ChunkTileKernel badKernel = []( const TileSpec &s ) -> std::vector<float> {
            if ( s.index >= 5 )
                throw std::runtime_error( "simulated unhandled GPU driver exception" );
            return generateReferenceKernel( s );
        };
        std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

        RSOperatorContext ctx( dir.generic_string() );
        bool caught = false;
        try
        {
            runChunkedOperator( "test:adopter", Json::Value( Json::objectValue ),
                                ctx, makePartition32(),
                                TileRunDeterminism::BitExact, badKernel, sink, opts );
        }
        catch ( const std::exception & )
        {
            caught = true;
        }

        HARNESS_ASSERT( caught, "Must catch unhandled kernel exception" );
        HARNESS_ASSERT( !QFile::exists( tempFile ), "Temporary file must be discarded via committer->discardTemporary" );
        HARNESS_ASSERT( !std::filesystem::exists( dir / "state" / "run.journal" ), "Journal must be wiped by run.abandon()" );
        HARNESS_ASSERT( !std::filesystem::exists( dir / "state" / "run.published" ), "Published marker must NOT be written" );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }

    // Subcase 5B: Commit failure preserves scratch and temp for diagnosis, does NOT write .published
    {
        const auto dir = createTempDir( "commit_failure" );
        const auto qDir = QString::fromStdString( dir.generic_string() );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "run" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        // Point tempPath to a nonexistent file so commit fails validation
        spec.tempPath = qDir + QStringLiteral( "/nonexistent_temp.tif" );
        spec.stablePath = qDir + QStringLiteral( "/stable.tif" );
        opts.commitSpec = spec;

        ChunkTileKernel kernel = []( const TileSpec &s ) { return generateReferenceKernel( s ); };
        std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

        RSOperatorContext ctx( dir.generic_string() );
        bool caughtFileNotWritable = false;
        try
        {
            runChunkedOperator( "test:adopter", Json::Value( Json::objectValue ),
                                ctx, makePartition32(),
                                TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            caughtFileNotWritable = ( e.code() == ErrorCode::FileNotWritable );
        }

        HARNESS_ASSERT( caughtFileNotWritable, "Commit failure must throw FileNotWritable error" );
        HARNESS_ASSERT( !std::filesystem::exists( dir / "state" / "run.published" ), ".published marker must NOT be written" );
        HARNESS_ASSERT( std::filesystem::exists( dir / "state" / "run.journal" ), "Journal MUST be preserved for diagnosis" );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }

    std::cout << "  -> Suite 5 PASSED." << std::endl;
}

// ============================================================================
// Suite 6: Pipeline Mode OutputCommitter & Discard On Error Oracle
// ============================================================================
void testPipelineModeCommitter()
{
    std::cout << "[*] Running Suite 6: Pipeline Mode OutputCommitter & Discard Oracle..." << std::endl;

    // Subcase 6A: Successful pipeline run with commitSpec
    {
        const auto dir = createTempDir( "pipeline_commit" );
        const auto qDir = QString::fromStdString( dir.generic_string() );
        const QString tempFile = qDir + QStringLiteral( "/temp_pipe.tif" );
        const QString stableFile = qDir + QStringLiteral( "/stable_pipe.tif" );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        ChunkedRunOptions opts;
        opts.mode = ChunkedRunOptions::Mode::Pipeline;

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempFile;
        spec.stablePath = stableFile;
        opts.commitSpec = spec;
        opts.publish = [&]() {
            writeSyntheticGeoTiff( tempFile, 32, 16 );
        };

        ChunkTileKernel kernel = []( const TileSpec &s ) { return generateReferenceKernel( s ); };
        std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

        RSOperatorContext ctx( dir.generic_string() );
        const auto result = runChunkedOperator(
            "test:adopter", Json::Value( Json::objectValue ),
            ctx, makePartition32(),
            TileRunDeterminism::BitExact, kernel, sink, opts );

        HARNESS_ASSERT( result.tilesComputed == 32, "Pipeline mode must compute 32 tiles" );
        HARNESS_ASSERT( !result.publishedAssetId.isEmpty(), "Pipeline mode must return publishedAssetId" );
        HARNESS_ASSERT( QFile::exists( stableFile ), "Stable pipeline file must exist" );
        HARNESS_ASSERT( !QFile::exists( tempFile ), "Temp pipeline file must be renamed away" );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }

    // Subcase 6B: Pipeline failure discards temporary file
    {
        const auto dir = createTempDir( "pipeline_fail" );
        const auto qDir = QString::fromStdString( dir.generic_string() );
        const QString tempFile = qDir + QStringLiteral( "/temp_pipe_fail.tif" );
        const QString stableFile = qDir + QStringLiteral( "/stable_pipe_fail.tif" );
        writeSyntheticGeoTiff( tempFile, 32, 16 );
        HARNESS_ASSERT( QFile::exists( tempFile ), "tempFile must exist before run" );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        ChunkedRunOptions opts;
        opts.mode = ChunkedRunOptions::Mode::Pipeline;

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempFile;
        spec.stablePath = stableFile;
        opts.commitSpec = spec;

        ChunkTileKernel badKernel = []( const TileSpec &s ) -> std::vector<float> {
            if ( s.index >= 4 )
                throw std::runtime_error( "pipeline streaming error" );
            return generateReferenceKernel( s );
        };
        std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

        RSOperatorContext ctx( dir.generic_string() );
        bool caught = false;
        try
        {
            runChunkedOperator(
                "test:adopter", Json::Value( Json::objectValue ),
                ctx, makePartition32(),
                TileRunDeterminism::BitExact, badKernel, sink, opts );
        }
        catch ( const std::exception & )
        {
            caught = true;
        }

        HARNESS_ASSERT( caught, "Pipeline must propagate failure" );
        HARNESS_ASSERT( !QFile::exists( tempFile ), "Temporary pipeline file must be unlinked on failure" );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }

    std::cout << "  -> Suite 6 PASSED." << std::endl;
}

// ============================================================================
// Suite 7: Multi-Stage Cascade Interruption & Resume Oracle
// ============================================================================
void testMultiStageCascadeInterruptionAndResume()
{
    std::cout << "[*] Running Suite 7: Multi-Stage Cascade Interruption & Resume Oracle..." << std::endl;

    const auto dir = createTempDir( "cascade_interrupt" );
    const auto qDir = QString::fromStdString( dir.generic_string() );
    const QString tempOut = qDir + QStringLiteral( "/temp_output.tif" );
    const QString stableOut = qDir + QStringLiteral( "/stable_output.tif" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    ChunkedRunOptions options;
    options.scratchRoot = ( dir / "scratch" ).generic_string();
    options.resumeStateBase = ( dir / "state" / "run" ).generic_string();

    ChunkedOutputCommitSpec spec;
    spec.committer = &committer;
    spec.tempPath = tempOut;
    spec.stablePath = stableOut;
    options.commitSpec = spec;
    options.publish = [&]() {
        writeSyntheticGeoTiff( tempOut, 32, 16 );
    };

    // Stage 1: Cancel after 7 tiles
    std::uint64_t sinkCount1 = 0;
    std::atomic<std::uint64_t> kernelCalls1{ 0 };
    RSOperatorContext ctx1( dir.generic_string() );
    ctx1.setCancelCallback( [&]() { return sinkCount1 >= 7; } );

    ChunkTileKernel kernel1 = [&]( const TileSpec &s ) {
        kernelCalls1.fetch_add( 1 );
        return generateReferenceKernel( s );
    };
    std::function<void( const TilePayload & )> sink1 = [&]( const TilePayload & ) {
        ++sinkCount1;
    };

    bool caught1 = false;
    try
    {
        runChunkedOperator( "test:adopter", Json::Value( Json::objectValue ),
                            ctx1, makePartition32(),
                            TileRunDeterminism::BitExact, kernel1, sink1, options );
    }
    catch ( const RSOperatorError &e )
    {
        if ( e.code() == ErrorCode::Cancelled )
            caught1 = true;
    }
    HARNESS_ASSERT( caught1, "Stage 1 must be cancelled" );
    HARNESS_ASSERT( kernelCalls1.load() == 7, "Stage 1 must invoke kernel exactly 7 times" );

    // Stage 2: Resume, cancel after 19 tiles total
    std::uint64_t sinkCount2 = 0;
    std::atomic<std::uint64_t> kernelCalls2{ 0 };
    RSOperatorContext ctx2( dir.generic_string() );
    ctx2.setCancelCallback( [&]() { return sinkCount2 >= 19; } );

    ChunkTileKernel kernel2 = [&]( const TileSpec &s ) {
        kernelCalls2.fetch_add( 1 );
        return generateReferenceKernel( s );
    };
    std::function<void( const TilePayload & )> sink2 = [&]( const TilePayload & ) {
        ++sinkCount2;
    };

    bool caught2 = false;
    try
    {
        runChunkedOperator( "test:adopter", Json::Value( Json::objectValue ),
                            ctx2, makePartition32(),
                            TileRunDeterminism::BitExact, kernel2, sink2, options );
    }
    catch ( const RSOperatorError &e )
    {
        if ( e.code() == ErrorCode::Cancelled )
            caught2 = true;
    }
    HARNESS_ASSERT( caught2, "Stage 2 must be cancelled" );
    HARNESS_ASSERT( kernelCalls2.load() == 12, "Stage 2 must compute only 12 tiles (tiles 7..18)" );

    // Stage 3: Resume to final completion (remaining 13 tiles)
    std::uint64_t sinkCount3 = 0;
    std::atomic<std::uint64_t> kernelCalls3{ 0 };
    RSOperatorContext ctx3( dir.generic_string() );

    ChunkTileKernel kernel3 = [&]( const TileSpec &s ) {
        kernelCalls3.fetch_add( 1 );
        return generateReferenceKernel( s );
    };
    std::vector<float> finalPixels;
    std::function<void( const TilePayload & )> sink3 = [&]( const TilePayload &p ) {
        ++sinkCount3;
        for ( int b = 0; b < p.spec.bands; ++b )
            for ( int y = 0; y < p.spec.height; ++y )
                for ( int x = 0; x < p.spec.width; ++x )
                    finalPixels.push_back( ( *p.pixels )[static_cast<size_t>( b )
                                                              * p.spec.bufferWidth
                                                                  * p.spec.bufferHeight
                                                          + static_cast<size_t>( y )
                                                                * p.spec.bufferWidth
                                                          + x] );
    };

    const auto result3 = runChunkedOperator(
        "test:adopter", Json::Value( Json::objectValue ),
        ctx3, makePartition32(),
        TileRunDeterminism::BitExact, kernel3, sink3, options );

    HARNESS_ASSERT( result3.totalTiles == 32, "Total tiles must be 32" );
    HARNESS_ASSERT( result3.tilesReused == 19, "Stage 3 must reuse exactly 19 tiles" );
    HARNESS_ASSERT( result3.tilesComputed == 13, "Stage 3 must compute exactly 13 tiles" );
    HARNESS_ASSERT( kernelCalls3.load() == 13, "Stage 3 kernel must be called exactly 13 times" );
    HARNESS_ASSERT( sinkCount3 == 32, "Stage 3 sink must receive all 32 tiles" );
    HARNESS_ASSERT( finalPixels == generateTruthRaster(), "Final assembled output must match truth bit-for-bit" );

    // Grand total computations across all 3 stages: 7 + 12 + 13 == 32
    HARNESS_ASSERT( kernelCalls1.load() + kernelCalls2.load() + kernelCalls3.load() == 32,
                    "Grand total computations across all 3 cascaded stages must equal 32" );

    HARNESS_ASSERT( QFile::exists( stableOut ), "Stable output must exist after stage 3 completion" );
    HARNESS_ASSERT( std::filesystem::exists( dir / "state" / "run.published" ), "Published marker must exist" );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
    std::cout << "  -> Suite 7 PASSED." << std::endl;
}

// ============================================================================
// Suite 8: Zero-Byte & Corrupt .published Marker Self-Recovery Oracle
// ============================================================================
void testCorruptMarkerSelfRecovery()
{
    std::cout << "[*] Running Suite 8: Zero-Byte & Corrupt .published Marker Self-Recovery Oracle..." << std::endl;

    // Subcase 8A: 0-byte .published marker
    {
        const auto dir = createTempDir( "zerobyte_marker" );
        const auto qDir = QString::fromStdString( dir.generic_string() );
        const QString tempOut = qDir + QStringLiteral( "/temp_output.tif" );
        const QString stableOut = qDir + QStringLiteral( "/stable_output.tif" );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        ChunkedRunOptions options;
        options.scratchRoot = ( dir / "scratch" ).generic_string();
        options.resumeStateBase = ( dir / "state" / "run" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        options.commitSpec = spec;
        options.publish = [&]() {
            writeSyntheticGeoTiff( tempOut, 32, 16 );
        };

        // Create 0-byte marker before running
        std::error_code ec;
        std::filesystem::create_directories( dir / "state", ec );
        {
            std::ofstream markerFile( dir / "state" / "run.published", std::ios::binary | std::ios::trunc );
            markerFile.flush();
        }
        HARNESS_ASSERT( std::filesystem::file_size( dir / "state" / "run.published" ) == 0,
                        "Marker must be 0 bytes" );

        std::atomic<std::uint64_t> kernelCalls{ 0 };
        ChunkTileKernel kernel = [&]( const TileSpec &s ) {
            kernelCalls.fetch_add( 1 );
            return generateReferenceKernel( s );
        };
        std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

        RSOperatorContext ctx1( dir.generic_string() );
        const auto result1 = runChunkedOperator(
            "test:adopter", Json::Value( Json::objectValue ),
            ctx1, makePartition32(),
            TileRunDeterminism::BitExact, kernel, sink, options );

        HARNESS_ASSERT( !result1.alreadyPublished, "Zero-byte marker must NOT trigger bypass" );
        HARNESS_ASSERT( result1.tilesComputed == 32, "Run must compute all 32 tiles" );
        HARNESS_ASSERT( kernelCalls.load() == 32, "Kernel must run 32 times" );
        HARNESS_ASSERT( std::filesystem::file_size( dir / "state" / "run.published" ) > 0,
                        "Published marker must be re-written with valid payload" );

        // Second run must now bypass
        kernelCalls.store( 0 );
        RSOperatorContext ctx2( dir.generic_string() );
        const auto result2 = runChunkedOperator(
            "test:adopter", Json::Value( Json::objectValue ),
            ctx2, makePartition32(),
            TileRunDeterminism::BitExact, kernel, sink, options );

        HARNESS_ASSERT( result2.alreadyPublished, "Second run must now bypass with valid marker" );
        HARNESS_ASSERT( kernelCalls.load() == 0, "Second run must not call kernel" );

        std::filesystem::remove_all( dir, ec );
    }

    // Subcase 8B: Corrupt payload in .published marker
    {
        const auto dir = createTempDir( "corrupt_marker" );
        const auto qDir = QString::fromStdString( dir.generic_string() );
        const QString tempOut = qDir + QStringLiteral( "/temp_output.tif" );
        const QString stableOut = qDir + QStringLiteral( "/stable_output.tif" );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        ChunkedRunOptions options;
        options.scratchRoot = ( dir / "scratch" ).generic_string();
        options.resumeStateBase = ( dir / "state" / "run" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        options.commitSpec = spec;
        options.publish = [&]() {
            writeSyntheticGeoTiff( tempOut, 32, 16 );
        };

        std::error_code ec;
        std::filesystem::create_directories( dir / "state", ec );
        {
            std::ofstream markerFile( dir / "state" / "run.published", std::ios::binary | std::ios::trunc );
            markerFile << "BAD_VERSION 99999999999999999999\n";
            markerFile.flush();
        }

        std::atomic<std::uint64_t> kernelCalls{ 0 };
        ChunkTileKernel kernel = [&]( const TileSpec &s ) {
            kernelCalls.fetch_add( 1 );
            return generateReferenceKernel( s );
        };
        std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

        RSOperatorContext ctx1( dir.generic_string() );
        const auto result1 = runChunkedOperator(
            "test:adopter", Json::Value( Json::objectValue ),
            ctx1, makePartition32(),
            TileRunDeterminism::BitExact, kernel, sink, options );

        HARNESS_ASSERT( !result1.alreadyPublished, "Corrupt marker must NOT trigger bypass" );
        HARNESS_ASSERT( result1.tilesComputed == 32, "Run must compute all 32 tiles" );
        HARNESS_ASSERT( kernelCalls.load() == 32, "Kernel must run 32 times" );

        std::filesystem::remove_all( dir, ec );
    }

    std::cout << "  -> Suite 8 PASSED." << std::endl;
}

// ============================================================================
// Suite 9: Pre-Publish Interruption & Recovery Oracle
// ============================================================================
void testPrePublishCrashResume()
{
    std::cout << "[*] Running Suite 9: Pre-Publish Interruption & Recovery Oracle..." << std::endl;

    const auto dir = createTempDir( "prepublish_cancel" );
    const auto qDir = QString::fromStdString( dir.generic_string() );
    const QString tempOut = qDir + QStringLiteral( "/temp_output.tif" );
    const QString stableOut = qDir + QStringLiteral( "/stable_output.tif" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    ChunkedRunOptions options;
    options.scratchRoot = ( dir / "scratch" ).generic_string();
    options.resumeStateBase = ( dir / "state" / "run" ).generic_string();

    ChunkedOutputCommitSpec spec;
    spec.committer = &committer;
    spec.tempPath = tempOut;
    spec.stablePath = stableOut;
    options.commitSpec = spec;
    options.publish = [&]() {
        writeSyntheticGeoTiff( tempOut, 32, 16 );
    };

    // Stage 1: All 32 tiles are computed and sunk, but cancel triggers right before publish
    std::uint64_t sinkCount1 = 0;
    std::atomic<std::uint64_t> kernelCalls1{ 0 };
    RSOperatorContext ctx1( dir.generic_string() );
    ctx1.setCancelCallback( [&]() { return sinkCount1 >= 32; } );

    ChunkTileKernel kernel1 = [&]( const TileSpec &s ) {
        kernelCalls1.fetch_add( 1 );
        return generateReferenceKernel( s );
    };
    std::function<void( const TilePayload & )> sink1 = [&]( const TilePayload & ) {
        ++sinkCount1;
    };

    bool caughtCancel = false;
    try
    {
        runChunkedOperator( "test:adopter", Json::Value( Json::objectValue ),
                            ctx1, makePartition32(),
                            TileRunDeterminism::BitExact, kernel1, sink1, options );
    }
    catch ( const RSOperatorError &e )
    {
        if ( e.code() == ErrorCode::Cancelled )
            caughtCancel = true;
    }
    HARNESS_ASSERT( caughtCancel, "Must catch Cancelled right before publish" );
    HARNESS_ASSERT( kernelCalls1.load() == 32, "All 32 tiles were computed before publish cancellation" );
    HARNESS_ASSERT( !std::filesystem::exists( dir / "state" / "run.published" ), "Marker must NOT exist after cancellation" );
    HARNESS_ASSERT( std::filesystem::exists( dir / "state" / "run.journal" ), "Journal must exist with all 32 commits" );

    // Step 2: Resume with clean context
    // ALL 32 tiles must be reused from verified scratch on disk; ZERO kernel calls!
    std::atomic<std::uint64_t> kernelCalls2{ 0 };
    ChunkTileKernel kernel2 = [&]( const TileSpec &s ) {
        kernelCalls2.fetch_add( 1 );
        return generateReferenceKernel( s );
    };
    std::function<void( const TilePayload & )> sink2 = []( const TilePayload & ) {};

    RSOperatorContext ctx2( dir.generic_string() );
    const auto result2 = runChunkedOperator(
        "test:adopter", Json::Value( Json::objectValue ),
        ctx2, makePartition32(),
        TileRunDeterminism::BitExact, kernel2, sink2, options );

    HARNESS_ASSERT( result2.totalTiles == 32, "Total tiles must be 32" );
    HARNESS_ASSERT( result2.tilesReused == 32, "Must reuse ALL 32 committed tiles" );
    HARNESS_ASSERT( result2.tilesComputed == 0, "Must compute ZERO tiles on resume" );
    HARNESS_ASSERT( kernelCalls2.load() == 0, "Kernel must NOT be called on resume" );
    HARNESS_ASSERT( !result2.publishedAssetId.isEmpty(), "Published AssetId must be set" );
    HARNESS_ASSERT( QFile::exists( stableOut ), "Stable output must exist" );
    HARNESS_ASSERT( std::filesystem::exists( dir / "state" / "run.published" ), "Published marker must be written" );

    // Step 3: Third run must bypass
    RSOperatorContext ctx3( dir.generic_string() );
    const auto result3 = runChunkedOperator(
        "test:adopter", Json::Value( Json::objectValue ),
        ctx3, makePartition32(),
        TileRunDeterminism::BitExact, kernel2, sink2, options );

    HARNESS_ASSERT( result3.alreadyPublished, "Third run must bypass" );
    HARNESS_ASSERT( result3.tilesComputed == 0, "Must compute 0 tiles" );
    HARNESS_ASSERT( kernelCalls2.load() == 0, "Kernel must not run" );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
    std::cout << "  -> Suite 9 PASSED." << std::endl;
}

// ============================================================================
// Suite 10: Zero-Byte Journal File Recovery Oracle
// ============================================================================
void testZeroByteJournalRecovery()
{
    std::cout << "[*] Running Suite 10: Zero-Byte Journal File Recovery Oracle..." << std::endl;

    const auto dir = createTempDir( "zerobyte_journal" );
    TileRunSpec spec;
    spec.identity.operatorIdentity = 0x5555555555555555ull;
    spec.identity.inputIdentity = 0x6666666666666666ull;
    spec.partition = makePartition32();
    spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );

    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();
    const std::string journalPath = cfg.statePath + ".journal";

    std::error_code ec;
    std::filesystem::create_directories( dir / "state", ec );
    // Create empty 0-byte journal file
    {
        std::ofstream out( journalPath, std::ios::binary | std::ios::trunc );
        out.flush();
    }
    HARNESS_ASSERT( std::filesystem::file_size( journalPath ) == 0, "Journal must be 0 bytes" );

    // Execute run: ResumableTileRun must detect empty file, write header, and succeed
    std::atomic<std::uint64_t> computes{ 0 };
    std::vector<float> finalPixels;
    {
        ResumableTileRun run( spec, cfg );
        ResumableTileRun::Callbacks cb;
        cb.compute = [&]( const TileSpec &s ) {
            computes.fetch_add( 1 );
            auto buf = std::make_shared<std::vector<float>>( generateReferenceKernel( s ) );
            return TilePayload{ s, std::move( buf ) };
        };
        cb.consume = [&]( const TilePayload &p ) {
            for ( int b = 0; b < p.spec.bands; ++b )
                for ( int y = 0; y < p.spec.height; ++y )
                    for ( int x = 0; x < p.spec.width; ++x )
                        finalPixels.push_back( ( *p.pixels )[static_cast<size_t>( b )
                                                                  * p.spec.bufferWidth
                                                                      * p.spec.bufferHeight
                                                              + static_cast<size_t>( y )
                                                                    * p.spec.bufferWidth
                                                              + x] );
        };
        cb.publish = [] {};
        TileRunCancelSource cancel;
        const auto r = run.execute( cancel, cb );

        HARNESS_ASSERT( r.totalTiles == 32, "Total tiles must be 32" );
        HARNESS_ASSERT( r.tilesReused == 0, "Must reuse 0 tiles on fresh run" );
        HARNESS_ASSERT( r.tilesComputed == 32, "Must compute all 32 tiles" );
        HARNESS_ASSERT( computes.load() == 32, "Kernel must run 32 times" );
        HARNESS_ASSERT( finalPixels == generateTruthRaster(), "Output must match truth bit-exact" );
    }

    std::filesystem::remove_all( dir, ec );
    std::cout << "  -> Suite 10 PASSED." << std::endl;
}

int main( int argc, char **argv )
{
    QCoreApplication app( argc, argv );

    std::cout << "========================================================" << std::endl;
    std::cout << "Milestone 2 Empirical Challenger Stress Verification" << std::endl;
    std::cout << "========================================================" << std::endl;

    testInterruptedRunAndResume();
    testRerunBypassAndExactlyOnce();
    testTornJournalRecovery();
    testCorruptTileSelfHealing();
    testScratchLifecycleAndErrors();
    testPipelineModeCommitter();
    testMultiStageCascadeInterruptionAndResume();
    testCorruptMarkerSelfRecovery();
    testPrePublishCrashResume();
    testZeroByteJournalRecovery();

    std::cout << "\n[+] ALL 10 CHALLENGE SUITES PASSED EMPIRICALLY!" << std::endl;
    return 0;
}


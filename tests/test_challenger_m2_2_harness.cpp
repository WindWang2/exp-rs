// tests/test_challenger_m2_2_harness.cpp
// Empirical Challenger Stress Harness for Milestone 2 Scratch Lifecycle & Error Paths
//
// Target Invariants:
// 1. Scratch sweeping on success: no orphaned .tl or rt-* under scratchRoot when cleanupScratchOnSuccess = true.
// 2. Scratch preservation on success: all .tl, .journal, .ckpt files retained when cleanupScratchOnSuccess = false.
// 3. Resumption from preserved scratch: 100% reuse without redundant recomputation.
// 4. Parameter validation: null committer, empty/whitespace paths, null kernel/sink reject with ErrorCode::InvalidParameter.
// 5. Zero side-effects oracle: failed validation produces zero disk artifacts and invokes no callbacks.
// 6. Error abandonment on abnormal failure: unhandled kernel/sink/publish exceptions invoke run.abandon() and discard temp files.
// 7. Commit failure vs cancellation diagnostic preservation: commit failures and cancellations retain state as intended.

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
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "platform/portable.h"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace sicnu::operators;
using namespace sicnu::runtime::chunk;

namespace
{

int selfPid()
{
    return static_cast<int>( sicnu::portable::pid() );
}

std::filesystem::path makeIsolatedTempDir( const char *tag )
{
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path( ec );
    if ( ec )
        base = std::filesystem::current_path();
    static std::atomic<unsigned> n{ 0 };
    const auto dir = base / ( "challenger-m2-2-" + std::string( tag ) + "-"
                              + std::to_string( selfPid() ) + "-" + std::to_string( n++ ) );
    std::filesystem::create_directories( dir, ec );
    return dir;
}

void createSyntheticTiff( const QString &path, int w = 16, int h = 8 )
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
    std::vector<float> line( static_cast<size_t>( w ), 0.5f );
    for ( int row = 0; row < h; ++row )
    {
        const CPLErr ioErr = GDALRasterIO( band, GF_Write, 0, row, w, 1, line.data(), w, 1, GDT_Float32, 0, 0 );
        REQUIRE( ioErr == CE_None );
    }
    GDALClose( ds );
}

std::vector<std::filesystem::path> findFilesWithExtension( const std::filesystem::path &dir,
                                                          const std::string &ext )
{
    std::vector<std::filesystem::path> matches;
    if ( !std::filesystem::exists( dir ) )
        return matches;
    for ( const auto &entry : std::filesystem::recursive_directory_iterator( dir ) )
    {
        if ( entry.is_regular_file() && entry.path().extension() == ext )
        {
            matches.push_back( entry.path() );
        }
    }
    return matches;
}

std::vector<std::filesystem::path> findDirectoriesWithPrefix( const std::filesystem::path &dir,
                                                              const std::string &prefix )
{
    std::vector<std::filesystem::path> matches;
    if ( !std::filesystem::exists( dir ) )
        return matches;
    for ( const auto &entry : std::filesystem::directory_iterator( dir ) )
    {
        if ( entry.is_directory() && entry.path().filename().string().rfind( prefix, 0 ) == 0 )
        {
            matches.push_back( entry.path() );
        }
    }
    return matches;
}

TileRunPartition makePartition( int rasterW = 16, int rasterH = 8, int tileW = 4, int tileH = 2,
                                int bands = 2, int halo = 0 )
{
    TileRunPartition p;
    p.rasterWidth = rasterW;
    p.rasterHeight = rasterH;
    p.tileWidth = tileW;
    p.tileHeight = tileH;
    p.bands = bands;
    p.halo = halo;
    return p;
}

std::vector<float> referenceKernel( const TileSpec &s )
{
    std::vector<float> buf( s.bufferElementCount() );
    for ( size_t i = 0; i < buf.size(); ++i )
    {
        buf[i] = static_cast<float>( ( s.index * 137 + i * 17 ) % 256 ) / 256.0f;
    }
    return buf;
}

ChunkTileKernel makeDeterministicKernel( std::shared_ptr<std::uint64_t> counter = nullptr )
{
    return [counter]( const TileSpec &s ) -> std::vector<float> {
        if ( counter )
            ++*counter;
        return referenceKernel( s );
    };
}

} // namespace

// ============================================================================
// Suite 1: Scratch Tile Sweeping (cleanupScratchOnSuccess = true)
// ============================================================================

TEST_CASE( "Challenger M2-S1: Scratch sweeping removes rt-* and all .tl files under explicit and default scratchRoot",
           "[challenger][m2][scratch_sweep]" )
{
    SECTION( "Explicit scratchRoot: sweeps rt-* directory and all .tl tiles" )
    {
        const auto dir = makeIsolatedTempDir( "sweep_explicit" );
        const auto scratch = dir / "scratch";
        const auto state = dir / "state" / "run_state";
        const auto qDir = QString::fromStdString( dir.generic_string() );
        const QString tempOut = qDir + QStringLiteral( "/temp.tif" );
        const QString stableOut = qDir + QStringLiteral( "/stable.tif" );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        RSOperatorContext ctx( dir.generic_string() );
        ChunkedRunOptions opts;
        opts.scratchRoot = scratch.generic_string();
        opts.resumeStateBase = state.generic_string();
        opts.cleanupScratchOnSuccess = true;

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        opts.commitSpec = spec;
        opts.publish = [&]() { createSyntheticTiff( tempOut, 16, 8 ); };

        auto counter = std::make_shared<std::uint64_t>( 0 );
        const auto partition = makePartition( 16, 8, 4, 2, 2, 0 ); // 16 tiles
        const auto result = runChunkedOperator( "test:m2_sweep", Json::Value( Json::objectValue ),
                                                ctx, partition, TileRunDeterminism::BitExact,
                                                makeDeterministicKernel( counter ),
                                                []( const TilePayload & ) {}, opts );

        CHECK( result.totalTiles == 16 );
        CHECK( result.tilesComputed == 16 );
        CHECK( *counter == 16 );

        // 1. Output files
        CHECK( QFile::exists( stableOut ) );
        CHECK_FALSE( QFile::exists( tempOut ) );

        // 2. Published marker exists
        CHECK( std::filesystem::exists( state.string() + ".published" ) );

        // 3. Journal and ckpt MUST be deleted
        CHECK_FALSE( std::filesystem::exists( state.string() + ".journal" ) );
        CHECK_FALSE( std::filesystem::exists( state.string() + ".ckpt" ) );

        // 4. Scratch root must NOT contain any rt-* directories or .tl files
        const auto orphanedTiles = findFilesWithExtension( scratch, ".tl" );
        CHECK( orphanedTiles.empty() );
        const auto rtDirs = findDirectoriesWithPrefix( scratch, "rt-" );
        CHECK( rtDirs.empty() );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }

    SECTION( "Default scratchRoot (empty): sweeps inside context.workDir() without touching user files" )
    {
        const auto workDir = makeIsolatedTempDir( "sweep_default" );
        const auto state = workDir / "state_default";
        const auto qDir = QString::fromStdString( workDir.generic_string() );
        const QString tempOut = qDir + QStringLiteral( "/temp_def.tif" );
        const QString stableOut = qDir + QStringLiteral( "/stable_def.tif" );

        // Pre-create a user file in workDir that must NOT be swept
        const auto userFile = workDir / "user_input.dat";
        {
            std::ofstream ofs( userFile );
            ofs << "preservation test data";
        }
        REQUIRE( std::filesystem::exists( userFile ) );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        RSOperatorContext ctx( workDir.generic_string() );
        ChunkedRunOptions opts;
        // scratchRoot left empty on purpose!
        opts.resumeStateBase = state.generic_string();
        opts.cleanupScratchOnSuccess = true;

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        opts.commitSpec = spec;
        opts.publish = [&]() { createSyntheticTiff( tempOut, 16, 8 ); };

        const auto partition = makePartition( 16, 8, 4, 2, 2, 0 );
        const auto result = runChunkedOperator( "test:m2_default_sweep", Json::Value( Json::objectValue ),
                                                ctx, partition, TileRunDeterminism::BitExact,
                                                makeDeterministicKernel(),
                                                []( const TilePayload & ) {}, opts );

        CHECK( result.totalTiles == 16 );
        CHECK( QFile::exists( stableOut ) );

        // User file preserved
        CHECK( std::filesystem::exists( userFile ) );

        // No orphaned rt-* directories or .tl files in workDir
        const auto orphanedTiles = findFilesWithExtension( workDir, ".tl" );
        CHECK( orphanedTiles.empty() );
        const auto rtDirs = findDirectoriesWithPrefix( workDir, "rt-" );
        CHECK( rtDirs.empty() );

        std::error_code ec;
        std::filesystem::remove_all( workDir, ec );
    }

    SECTION( "Repeated run with alreadyPublished = true cleanly handles cleanupScratchOnSuccess" )
    {
        const auto dir = makeIsolatedTempDir( "sweep_repeat" );
        const auto scratch = dir / "scratch";
        const auto state = dir / "state" / "run_state";
        const auto qDir = QString::fromStdString( dir.generic_string() );
        const QString tempOut = qDir + QStringLiteral( "/temp.tif" );
        const QString stableOut = qDir + QStringLiteral( "/stable.tif" );

        sicnu::data::DataManager manager;
        sicnu::OutputCommitter committer( &manager );

        RSOperatorContext ctx( dir.generic_string() );
        ChunkedRunOptions opts;
        opts.scratchRoot = scratch.generic_string();
        opts.resumeStateBase = state.generic_string();
        opts.cleanupScratchOnSuccess = true;

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        opts.commitSpec = spec;
        opts.publish = [&]() { createSyntheticTiff( tempOut, 16, 8 ); };

        auto counter = std::make_shared<std::uint64_t>( 0 );
        const auto partition = makePartition( 16, 8, 4, 2, 1, 0 );

        // Run 1
        const auto r1 = runChunkedOperator( "test:m2_repeat", Json::Value( Json::objectValue ),
                                            ctx, partition, TileRunDeterminism::BitExact,
                                            makeDeterministicKernel( counter ),
                                            []( const TilePayload & ) {}, opts );
        CHECK( r1.tilesComputed == 16 );
        CHECK_FALSE( r1.alreadyPublished );

        // Run 2: marker hit
        *counter = 0;
        RSOperatorContext ctx2( dir.generic_string() );
        const auto r2 = runChunkedOperator( "test:m2_repeat", Json::Value( Json::objectValue ),
                                            ctx2, partition, TileRunDeterminism::BitExact,
                                            makeDeterministicKernel( counter ),
                                            []( const TilePayload & ) {}, opts );
        CHECK( r2.alreadyPublished );
        CHECK( r2.tilesComputed == 0 );
        CHECK( *counter == 0 );
        CHECK( std::filesystem::exists( state.string() + ".published" ) );

        const auto orphanedTiles = findFilesWithExtension( scratch, ".tl" );
        CHECK( orphanedTiles.empty() );

        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
    }
}

// ============================================================================
// Suite 2: Scratch Preservation (cleanupScratchOnSuccess = false)
// ============================================================================

TEST_CASE( "Challenger M2-S2: Scratch preservation retains all tiles, journal, and ckpt on disk",
           "[challenger][m2][scratch_preserve]" )
{
    const auto dir = makeIsolatedTempDir( "preserve" );
    const auto scratch = dir / "scratch";
    const auto state = dir / "state" / "run_state";
    const auto qDir = QString::fromStdString( dir.generic_string() );
    const QString tempOut = qDir + QStringLiteral( "/temp_pres.tif" );
    const QString stableOut = qDir + QStringLiteral( "/stable_pres.tif" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    RSOperatorContext ctx( dir.generic_string() );
    ChunkedRunOptions opts;
    opts.scratchRoot = scratch.generic_string();
    opts.resumeStateBase = state.generic_string();
    opts.cleanupScratchOnSuccess = false; // PRESERVE!

    ChunkedOutputCommitSpec spec;
    spec.committer = &committer;
    spec.tempPath = tempOut;
    spec.stablePath = stableOut;
    opts.commitSpec = spec;
    opts.publish = [&]() { createSyntheticTiff( tempOut, 16, 8 ); };

    auto counter = std::make_shared<std::uint64_t>( 0 );
    const auto partition = makePartition( 16, 8, 4, 2, 2, 0 ); // 16 tiles
    const auto r1 = runChunkedOperator( "test:m2_preserve", Json::Value( Json::objectValue ),
                                        ctx, partition, TileRunDeterminism::BitExact,
                                        makeDeterministicKernel( counter ),
                                        []( const TilePayload & ) {}, opts );

    CHECK( r1.totalTiles == 16 );
    CHECK( r1.tilesComputed == 16 );
    CHECK( *counter == 16 );

    // Invariants with cleanupScratchOnSuccess = false:
    // 1. .published exists
    CHECK( std::filesystem::exists( state.string() + ".published" ) );
    // 2. .journal MUST be preserved
    CHECK( std::filesystem::exists( state.string() + ".journal" ) );
    // 3. .ckpt MUST be preserved
    CHECK( std::filesystem::exists( state.string() + ".ckpt" ) );

    // 4. Exact tile count must be preserved on disk
    const auto preservedTiles = findFilesWithExtension( scratch, ".tl" );
    REQUIRE( preservedTiles.size() == 16 );

    // 5. Each preserved tile must be readable and uncorrupted
    for ( const auto &tilePath : preservedTiles )
    {
        const auto payload = DiskTileStore::readFile( tilePath.string() );
        CHECK( payload.pixels != nullptr );
        CHECK( payload.pixels->size() == static_cast<size_t>( 4 * 2 * 2 ) );
        CHECK( payload.spec.bands == 2 );
        CHECK( payload.spec.width == 4 );
        CHECK( payload.spec.height == 2 );
    }

    // 6. Adversarial Recovery Challenge:
    // If the .published marker is removed, the engine must recover 100% of tiles from
    // the preserved scratch files without recomputing ANY kernel work!
    std::filesystem::remove( state.string() + ".published" );
    REQUIRE_FALSE( std::filesystem::exists( state.string() + ".published" ) );

    *counter = 0;
    RSOperatorContext ctx2( dir.generic_string() );
    std::uint64_t sunkCount = 0;
    std::function<void( const TilePayload & )> sink = [&]( const TilePayload & ) {
        ++sunkCount;
    };

    const auto r2 = runChunkedOperator( "test:m2_preserve", Json::Value( Json::objectValue ),
                                        ctx2, partition, TileRunDeterminism::BitExact,
                                        makeDeterministicKernel( counter ),
                                        sink, opts );

    CHECK( r2.totalTiles == 16 );
    CHECK( r2.tilesReused == 16 );
    CHECK( r2.tilesComputed == 0 );
    CHECK( *counter == 0 ); // ZERO kernel recomputation
    CHECK( sunkCount == 16 );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// ============================================================================
// Suite 3: Parameter Validation & Zero Side-Effects Oracle
// ============================================================================

TEST_CASE( "Challenger M2-S3: Parameter validation rejects invalid inputs and guarantees zero side-effects",
           "[challenger][m2][validation]" )
{
    const auto dir = makeIsolatedTempDir( "validation" );
    const auto qDir = QString::fromStdString( dir.generic_string() );
    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    RSOperatorContext ctx( dir.generic_string() );
    const auto partition = makePartition();
    auto kernel = makeDeterministicKernel();
    std::function<void( const TilePayload & )> sink = []( const TilePayload & ) {};

    auto assertZeroSideEffects = [&]() {
        // Assert no .tl files, no journal, no ckpt, no published marker
        CHECK( findFilesWithExtension( dir, ".tl" ).empty() );
        CHECK( findFilesWithExtension( dir, ".journal" ).empty() );
        CHECK( findFilesWithExtension( dir, ".ckpt" ).empty() );
        CHECK( findFilesWithExtension( dir, ".published" ).empty() );
        // Assert no assets registered in DataManager
        CHECK( manager.assets().empty() );
    };

    SECTION( "Null committer in commitSpec throws InvalidParameter" )
    {
        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();
        opts.resumeStateBase = ( dir / "state" / "run" ).generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = nullptr; // NULL!
        spec.tempPath = qDir + QStringLiteral( "/temp.tif" );
        spec.stablePath = qDir + QStringLiteral( "/stable.tif" );
        opts.commitSpec = spec;

        bool caught = false;
        try
        {
            runChunkedOperator( "test:val", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            caught = ( e.code() == ErrorCode::InvalidParameter );
        }
        CHECK( caught );
        assertZeroSideEffects();
    }

    SECTION( "Empty and whitespace tempPath/stablePath throw InvalidParameter" )
    {
        const std::vector<std::pair<QString, QString>> invalidPaths = {
            { QStringLiteral( "" ), qDir + QStringLiteral( "/stable.tif" ) },
            { QStringLiteral( "   " ), qDir + QStringLiteral( "/stable.tif" ) },
            { QStringLiteral( "\t\n  " ), qDir + QStringLiteral( "/stable.tif" ) },
            { qDir + QStringLiteral( "/temp.tif" ), QStringLiteral( "" ) },
            { qDir + QStringLiteral( "/temp.tif" ), QStringLiteral( "   " ) },
            { qDir + QStringLiteral( "/temp.tif" ), QStringLiteral( "\r\n \t" ) }
        };

        for ( const auto &[tPath, sPath] : invalidPaths )
        {
            ChunkedRunOptions opts;
            opts.scratchRoot = ( dir / "scratch" ).generic_string();
            opts.resumeStateBase = ( dir / "state" / "run" ).generic_string();

            ChunkedOutputCommitSpec spec;
            spec.committer = &committer;
            spec.tempPath = tPath;
            spec.stablePath = sPath;
            opts.commitSpec = spec;

            bool caught = false;
            try
            {
                runChunkedOperator( "test:val", Json::Value( Json::objectValue ),
                                    ctx, partition, TileRunDeterminism::BitExact, kernel, sink, opts );
            }
            catch ( const RSOperatorError &e )
            {
                caught = ( e.code() == ErrorCode::InvalidParameter );
            }
            CHECK( caught );
            assertZeroSideEffects();
        }
    }

    SECTION( "Empty operatorId throws InvalidParameter with zero side-effects" )
    {
        ChunkedRunOptions opts;
        bool caught = false;
        try
        {
            runChunkedOperator( "", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            caught = ( e.code() == ErrorCode::InvalidParameter );
        }
        CHECK( caught );
        assertZeroSideEffects();
    }

    SECTION( "Null kernel or null sink throws InvalidParameter with zero side-effects" )
    {
        ChunkedRunOptions opts;
        bool caughtKernel = false;
        try
        {
            runChunkedOperator( "test:val", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact, nullptr, sink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            caughtKernel = ( e.code() == ErrorCode::InvalidParameter );
        }
        CHECK( caughtKernel );
        assertZeroSideEffects();

        bool caughtSink = false;
        try
        {
            runChunkedOperator( "test:val", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact, kernel, nullptr, opts );
        }
        catch ( const RSOperatorError &e )
        {
            caughtSink = ( e.code() == ErrorCode::InvalidParameter );
        }
        CHECK( caughtSink );
        assertZeroSideEffects();
    }

    SECTION( "Pipeline mode validation parity" )
    {
        ChunkedRunOptions opts;
        opts.mode = ChunkedRunOptions::Mode::Pipeline;
        ChunkedOutputCommitSpec spec;
        spec.committer = nullptr;
        spec.tempPath = qDir + QStringLiteral( "/temp.tif" );
        spec.stablePath = qDir + QStringLiteral( "/stable.tif" );
        opts.commitSpec = spec;

        bool caught = false;
        try
        {
            runChunkedOperator( "test:pipe_val", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact, kernel, sink, opts );
        }
        catch ( const RSOperatorError &e )
        {
            caught = ( e.code() == ErrorCode::InvalidParameter );
        }
        CHECK( caught );
        assertZeroSideEffects();
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// ============================================================================
// Suite 4: Error Abandonment on Abnormal Failures
// ============================================================================

TEST_CASE( "Challenger M2-S4: Error abandonment sweeps scratch and unlinks temporary output on abnormal failure",
           "[challenger][m2][abandonment]" )
{
    const auto dir = makeIsolatedTempDir( "abandonment" );
    const auto scratch = dir / "scratch";
    const auto state = dir / "state" / "run_state";
    const auto qDir = QString::fromStdString( dir.generic_string() );
    const QString tempOut = qDir + QStringLiteral( "/temp_abandon.tif" );
    const QString stableOut = qDir + QStringLiteral( "/stable_abandon.tif" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    RSOperatorContext ctx( dir.generic_string() );
    const auto partition = makePartition( 16, 8, 4, 2, 1, 0 ); // 16 tiles

    SECTION( "Mid-stream kernel exception: wipes scratch tiles, journal, ckpt, and temp file" )
    {
        createSyntheticTiff( tempOut, 16, 8 );
        REQUIRE( QFile::exists( tempOut ) );

        ChunkedRunOptions opts;
        opts.scratchRoot = scratch.generic_string();
        opts.resumeStateBase = state.generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        opts.commitSpec = spec;

        // Fails after 7 tiles were computed and committed
        ChunkTileKernel faultKernel = []( const TileSpec &s ) -> std::vector<float> {
            if ( s.index >= 7 )
                throw std::runtime_error( "simulated unhandled kernel fault at tile 7" );
            return referenceKernel( s );
        };

        bool threw = false;
        try
        {
            runChunkedOperator( "test:abandon_kernel", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact, faultKernel,
                                []( const TilePayload & ) {}, opts );
        }
        catch ( const std::exception & )
        {
            threw = true;
        }
        CHECK( threw );

        // 1. Temporary file must be removed by discardTemporary
        CHECK_FALSE( QFile::exists( tempOut ) );
        // 2. Stable file was never published
        CHECK_FALSE( QFile::exists( stableOut ) );
        // 3. Scratch directory must be wiped by run.abandon()
        CHECK( findFilesWithExtension( scratch, ".tl" ).empty() );
        CHECK_FALSE( std::filesystem::exists( state.string() + ".journal" ) );
        CHECK_FALSE( std::filesystem::exists( state.string() + ".ckpt" ) );
        CHECK_FALSE( std::filesystem::exists( state.string() + ".published" ) );
    }

    SECTION( "Mid-stream sink exception: wipes scratch tiles and temp file" )
    {
        createSyntheticTiff( tempOut, 16, 8 );
        REQUIRE( QFile::exists( tempOut ) );

        ChunkedRunOptions opts;
        opts.scratchRoot = scratch.generic_string();
        opts.resumeStateBase = state.generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        opts.commitSpec = spec;

        std::uint64_t sunkCount = 0;
        std::function<void( const TilePayload & )> faultSink = [&]( const TilePayload & ) {
            if ( ++sunkCount >= 5 )
                throw std::runtime_error( "sink I/O failure" );
        };

        bool threw = false;
        try
        {
            runChunkedOperator( "test:abandon_sink", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact,
                                makeDeterministicKernel(), faultSink, opts );
        }
        catch ( const std::exception & )
        {
            threw = true;
        }
        CHECK( threw );

        CHECK_FALSE( QFile::exists( tempOut ) );
        CHECK( findFilesWithExtension( scratch, ".tl" ).empty() );
        CHECK_FALSE( std::filesystem::exists( state.string() + ".journal" ) );
        CHECK_FALSE( std::filesystem::exists( state.string() + ".published" ) );
    }

    SECTION( "Publish callback exception: wipes scratch tiles and temp file" )
    {
        createSyntheticTiff( tempOut, 16, 8 );
        REQUIRE( QFile::exists( tempOut ) );

        ChunkedRunOptions opts;
        opts.scratchRoot = scratch.generic_string();
        opts.resumeStateBase = state.generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        opts.commitSpec = spec;
        opts.publish = []() {
            throw std::runtime_error( "publish callback failed unexpectedly" );
        };

        bool threw = false;
        try
        {
            runChunkedOperator( "test:abandon_publish", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact,
                                makeDeterministicKernel(), []( const TilePayload & ) {}, opts );
        }
        catch ( const std::exception & )
        {
            threw = true;
        }
        CHECK( threw );

        CHECK_FALSE( QFile::exists( tempOut ) );
        CHECK( findFilesWithExtension( scratch, ".tl" ).empty() );
        CHECK_FALSE( std::filesystem::exists( state.string() + ".published" ) );
    }

    SECTION( "Kernel corrupt buffer size mismatch: triggers run.abandon() and temp discard" )
    {
        createSyntheticTiff( tempOut, 16, 8 );
        REQUIRE( QFile::exists( tempOut ) );

        ChunkedRunOptions opts;
        opts.scratchRoot = scratch.generic_string();
        opts.resumeStateBase = state.generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        opts.commitSpec = spec;

        ChunkTileKernel badKernel = []( const TileSpec &s ) -> std::vector<float> {
            if ( s.index == 3 )
                return std::vector<float>( 1, 0.0f ); // INVALID size: 1 instead of bufferElementCount()
            return makeDeterministicKernel()( s );
        };

        bool threw = false;
        try
        {
            runChunkedOperator( "test:abandon_mismatch", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact,
                                badKernel, []( const TilePayload & ) {}, opts );
        }
        catch ( const std::exception & )
        {
            threw = true;
        }
        CHECK( threw );

        CHECK_FALSE( QFile::exists( tempOut ) );
        CHECK( findFilesWithExtension( scratch, ".tl" ).empty() );
    }

    SECTION( "Pipeline mode: unhandled exception discards temporary output" )
    {
        createSyntheticTiff( tempOut, 16, 8 );
        REQUIRE( QFile::exists( tempOut ) );

        ChunkedRunOptions opts;
        opts.mode = ChunkedRunOptions::Mode::Pipeline;
        opts.scratchRoot = scratch.generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        opts.commitSpec = spec;

        ChunkTileKernel faultKernel = []( const TileSpec &s ) -> std::vector<float> {
            if ( s.index >= 4 )
                throw std::runtime_error( "pipeline producer fault" );
            return referenceKernel( s );
        };

        bool threw = false;
        try
        {
            runChunkedOperator( "test:abandon_pipe", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact,
                                faultKernel, []( const TilePayload & ) {}, opts );
        }
        catch ( const std::exception & )
        {
            threw = true;
        }
        CHECK( threw );

        // Temporary file discarded
        CHECK_FALSE( QFile::exists( tempOut ) );
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// ============================================================================
// Suite 5: Diagnostic State Retention on Commit Failure vs. Cancellation
// ============================================================================

TEST_CASE( "Challenger M2-S5: Diagnostic state retention differentiates commit failure vs cancellation",
           "[challenger][m2][diagnostic_retention]" )
{
    const auto dir = makeIsolatedTempDir( "diag_retention" );
    const auto scratch = dir / "scratch";
    const auto state = dir / "state" / "run_state";
    const auto qDir = QString::fromStdString( dir.generic_string() );
    const QString tempOut = qDir + QStringLiteral( "/temp_diag.tif" );
    const QString stableOut = qDir + QStringLiteral( "/stable_diag.tif" );

    sicnu::data::DataManager manager;
    sicnu::OutputCommitter committer( &manager );

    RSOperatorContext ctx( dir.generic_string() );
    const auto partition = makePartition( 16, 8, 4, 2, 1, 0 ); // 16 tiles

    SECTION( "OutputCommitter validation failure: translates to FileNotWritable and preserves scratch & temp" )
    {
        // Notice: tempOut does NOT exist, so OutputCommitter validation will fail
        REQUIRE_FALSE( QFile::exists( tempOut ) );

        ChunkedRunOptions opts;
        opts.scratchRoot = scratch.generic_string();
        opts.resumeStateBase = state.generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        opts.commitSpec = spec;

        bool caughtFileNotWritable = false;
        try
        {
            runChunkedOperator( "test:commit_fail", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact,
                                makeDeterministicKernel(), []( const TilePayload & ) {}, opts );
        }
        catch ( const RSOperatorError &e )
        {
            caughtFileNotWritable = ( e.code() == ErrorCode::FileNotWritable );
        }
        CHECK( caughtFileNotWritable );

        // Scratch files and journal MUST be preserved for post-mortem debugging
        CHECK( std::filesystem::exists( state.string() + ".journal" ) );
        const auto preservedTiles = findFilesWithExtension( scratch, ".tl" );
        CHECK( preservedTiles.size() == 16 );

        // Marker must NOT be written (commit failed!)
        CHECK_FALSE( std::filesystem::exists( state.string() + ".published" ) );
    }

    SECTION( "Cooperative cancellation: translates to Cancelled and preserves scratch & journal for resume" )
    {
        createSyntheticTiff( tempOut, 16, 8 );
        REQUIRE( QFile::exists( tempOut ) );

        ChunkedRunOptions opts;
        opts.scratchRoot = scratch.generic_string();
        opts.resumeStateBase = state.generic_string();

        ChunkedOutputCommitSpec spec;
        spec.committer = &committer;
        spec.tempPath = tempOut;
        spec.stablePath = stableOut;
        opts.commitSpec = spec;

        std::uint64_t sunkCount = 0;
        ctx.setCancelCallback( [&sunkCount] { return sunkCount >= 6; } );

        bool caughtCancelled = false;
        try
        {
            runChunkedOperator( "test:cancel_pres", Json::Value( Json::objectValue ),
                                ctx, partition, TileRunDeterminism::BitExact,
                                makeDeterministicKernel(),
                                [&]( const TilePayload & ) { ++sunkCount; }, opts );
        }
        catch ( const RSOperatorError &e )
        {
            caughtCancelled = ( e.code() == ErrorCode::Cancelled );
        }
        CHECK( caughtCancelled );

        // Journal and scratch tiles preserved for subsequent resume
        CHECK( std::filesystem::exists( state.string() + ".journal" ) );
        const auto preservedTiles = findFilesWithExtension( scratch, ".tl" );
        CHECK( preservedTiles.size() >= 6 );

        // Temp output was NOT discarded (cooperative cancellation allows resume)
        CHECK( QFile::exists( tempOut ) );
        CHECK_FALSE( std::filesystem::exists( state.string() + ".published" ) );
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

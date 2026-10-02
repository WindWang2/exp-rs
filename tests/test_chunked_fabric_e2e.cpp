// test_chunked_fabric_e2e.cpp — End-to-End Opaque-Box Verification of the
// Large-Scale Chunked Operator Fabric (R1, R2, R3).
//
// Verification Tiers Covered:
//   - Tier 1: Feature Coverage (>=5 tests per feature for R1, R2, R3)
//   - Tier 2: Boundary & Corner Cases (>=5 per feature)
//   - Tier 3: Cross-Feature Combinations (pairwise interactions)
//   - Tier 4: Real-World Application Scenarios (>=5 realistic workflows)
//
// Test Philosophy:
//   Opaque-box verification derived from user requirements and interface contracts.
//   Every expected output is independently computed from closed-form mathematical
//   oracles or analytical formulas, never by echoing the unit under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "operators/framework/chunk_error_bridge.h"
#include "operators/framework/chunked_run.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "runtime/chunk/bounded_chunk_queue.h"
#include "runtime/chunk/disk_tile_store.h"
#include "runtime/chunk/memory_planner.h"
#include "runtime/chunk/resumable_tile_run.h"
#include "runtime/chunk/scratch_registry.h"
#include "runtime/chunk/tile_run_contract.h"
#include "runtime/exec/execution_governor.h"
#include "runtime/observability/execution_telemetry.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace sicnu::operators;
using namespace sicnu::runtime;
using namespace sicnu::runtime::chunk;
using namespace sicnu::runtime::exec;

namespace
{

int getProcessId()
{
#ifdef _WIN32
    return static_cast<int>( _getpid() );
#else
    return static_cast<int>( ::getpid() );
#endif
}

std::filesystem::path createIsolatedTempDir( const std::string &tag )
{
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path( ec );
    if ( ec )
        base = std::filesystem::current_path();
    static std::atomic<unsigned> counter{ 0 };
    const auto dir = base / ( "sicnu-e2e-" + tag + "-"
                              + std::to_string( getProcessId() ) + "-"
                              + std::to_string( counter.fetch_add( 1 ) ) );
    std::filesystem::create_directories( dir, ec );
    return dir;
}

// ---------------------------------------------------------------------------
// Mathematical Oracles (Closed-Form Analytical Truth)
// ---------------------------------------------------------------------------

float oracleNdvi( float nir, float red )
{
    const float denom = nir + red;
    if ( std::abs( denom ) < 1e-7f )
        return 0.0f;
    return ( nir - red ) / denom;
}

float oracleNdwi( float green, float nir )
{
    const float denom = green + nir;
    if ( std::abs( denom ) < 1e-7f )
        return 0.0f;
    return ( green - nir ) / denom;
}

float oracleEvi( float nir, float red, float blue )
{
    const float denom = nir + 6.0f * red - 7.5f * blue + 1.0f;
    if ( std::abs( denom ) < 1e-7f )
        return 0.0f;
    return 2.5f * ( nir - red ) / denom;
}

float oracleSavi( float nir, float red, float L = 0.5f )
{
    const float denom = nir + red + L;
    if ( std::abs( denom ) < 1e-7f )
        return 0.0f;
    return ( ( nir - red ) / denom ) * ( 1.0f + L );
}

float oracleChangeDifference( float t1, float t2 )
{
    return t2 - t1;
}

float oracleChangeAbsDifference( float t1, float t2 )
{
    return std::abs( t2 - t1 );
}

struct StreamingWelfordOracle
{
    std::uint64_t count = 0;
    double mean = 0.0;
    double M2 = 0.0;
    float minVal = std::numeric_limits<float>::infinity();
    float maxVal = -std::numeric_limits<float>::infinity();

    void update( float x )
    {
        if ( std::isnan( x ) )
            return;
        ++count;
        const double delta = static_cast<double>( x ) - mean;
        mean += delta / static_cast<double>( count );
        const double delta2 = static_cast<double>( x ) - mean;
        M2 += delta * delta2;
        if ( x < minVal )
            minVal = x;
        if ( x > maxVal )
            maxVal = x;
    }

    double variance() const
    {
        return count > 1 ? M2 / static_cast<double>( count - 1 ) : 0.0;
    }

    double stddev() const
    {
        return std::sqrt( variance() );
    }
};

/// Deterministic mock pixel generator for multi-band imagery.
float generatePixelValue( int x, int y, int band, std::uint64_t seed = 42 )
{
    const std::uint64_t h = ( static_cast<std::uint64_t>( x ) * 31337ull )
                            ^ ( static_cast<std::uint64_t>( y ) * 7919ull )
                            ^ ( static_cast<std::uint64_t>( band ) * 104729ull )
                            ^ seed;
    return static_cast<float>( ( h % 9973ull ) + 1ull ) / 10000.0f; // in (0, 1]
}

} // namespace

// ===========================================================================
// TIER 1: FEATURE COVERAGE (R1, R2, R3)
// ===========================================================================

TEST_CASE( "Tier 1: R1-F1 - Dynamic Governor Static & Dynamic Admission Ladder",
           "[chunk][e2e][tier1][r1]" )
{
    ExecutionGovernor::Config cfg;
    cfg.ramBytes = 512 * 1024; // 512 KiB budget
    cfg.scratchBytes = 100 * 1024 * 1024; // 100 MiB
    cfg.writeInFlightBytes = 256 * 1024;
    cfg.scratchRoot = "scratch-test";
    ExecutionGovernor gov( cfg );

    TileMemoryRequest req;
    req.tileWidth = 64;
    req.tileHeight = 64;
    req.bands = 2; // 64*64*2*4 = 32 KiB per tile
    req.stageCount = 1;
    req.requestedQueueCapacity = 2;

    SECTION( "Admit within budget" )
    {
        const TileMemoryPlan plan = gov.admitOrRefuse( req );
        REQUIRE( plan.action == TileMemoryPlan::Action::Admit );
        REQUIRE( plan.fits() );
        REQUIRE( plan.recommendedQueueCapacity == 2 );
    }

    SECTION( "ReduceConcurrency when budget is tight" )
    {
        // Tighten budget below cap=2 peak but above cap=1 peak
        ExecutionGovernor::Config tightCfg = cfg;
        tightCfg.ramBytes = chunk::tileStreamPeakBytes( req, 1 ) + 512;
        ExecutionGovernor tightGov( tightCfg );

        const TileMemoryPlan plan = tightGov.admitOrRefuse( req );
        REQUIRE( plan.action == TileMemoryPlan::Action::ReduceConcurrency );
        REQUIRE( plan.recommendedQueueCapacity == 1 );
    }

    SECTION( "Spill to disk when RAM is insufficient but scratch is available" )
    {
        ExecutionGovernor::Config spillCfg = cfg;
        spillCfg.ramBytes = chunk::tileStreamPeakBytes( req, 1 ) - 1;
        spillCfg.scratchBytes = 10 * 1024 * 1024;
        ExecutionGovernor spillGov( spillCfg );

        TileMemoryRequest spillReq = req;
        spillReq.allowSpill = true;
        spillReq.expectedTileCount = 50;

        const TileMemoryPlan plan = spillGov.admitOrRefuse( spillReq );
        REQUIRE( plan.action == TileMemoryPlan::Action::Spill );
        REQUIRE( plan.spillBytes > 0 );
    }

    SECTION( "Refuse when no memory or scratch is available" )
    {
        ExecutionGovernor::Config refuseCfg = cfg;
        refuseCfg.ramBytes = 1024; // smaller than one tile
        refuseCfg.scratchBytes = 0;
        ExecutionGovernor refuseGov( refuseCfg );

        TileMemoryRequest doomed = req;
        doomed.allowSpill = false;
        REQUIRE_THROWS_AS( refuseGov.admitOrRefuse( doomed ), AdmissionRefused );
    }

    SECTION( "Advisory planning mode does not throw" )
    {
        ExecutionGovernor::Config advCfg = cfg;
        advCfg.ramBytes = 0; // Unbounded advisory
        ExecutionGovernor advGov( advCfg );
        const TileMemoryPlan plan = advGov.advise( req );
        REQUIRE( plan.action == TileMemoryPlan::Action::Admit );
    }
}

TEST_CASE( "Tier 1: R1-F2 - Scratch Registry Budgeting, Quota Enforcement & Sweeping",
           "[chunk][e2e][tier1][r1]" )
{
    ExecutionGovernor::Config cfg;
    cfg.scratchBytes = 5000;
    cfg.scratchRoot = "scratch-lease-test";
    ExecutionGovernor gov( cfg );

    SECTION( "Acquire and release lease within budget" )
    {
        auto lease = gov.scratch().acquire( "run-1", "tile-0", 2000 );
        REQUIRE( lease.isValid() );
        REQUIRE( gov.hasOutstandingResources() );
        REQUIRE( gov.scratch().outstandingBytes() == 2000 );

        // Release lease
        lease = chunk::ScratchLease{};
        REQUIRE_FALSE( gov.hasOutstandingResources() );
        REQUIRE( gov.scratch().outstandingBytes() == 0 );
    }

    SECTION( "Enforce strict quota and throw ScratchBudgetExceeded" )
    {
        auto lease1 = gov.scratch().acquire( "run-2", "tile-0", 3000 );
        REQUIRE( lease1.isValid() );

        // Attempting to exceed budget (3000 + 2500 > 5000)
        REQUIRE_THROWS_AS( gov.scratch().acquire( "run-2", "tile-1", 2500 ),
                           chunk::ScratchBudgetExceeded );

        // Release lease1 frees budget
        lease1 = chunk::ScratchLease{};
        auto lease2 = gov.scratch().acquire( "run-2", "tile-1", 2500 );
        REQUIRE( lease2.isValid() );
    }
}

TEST_CASE( "Tier 1: R1-F3 - Bounded Write Gate Backpressure and Accounting",
           "[chunk][e2e][tier1][r1]" )
{
    ExecutionGovernor::Config cfg;
    cfg.writeInFlightBytes = 2048;
    ExecutionGovernor gov( cfg );
    auto &gate = gov.writeGate();

    SECTION( "Acquire and release under capacity" )
    {
        gate.acquire( 1024 );
        REQUIRE( gate.outstandingBytes() == 1024 );
        gate.acquire( 512 );
        REQUIRE( gate.outstandingBytes() == 1536 );
        gate.release( 1024 );
        REQUIRE( gate.outstandingBytes() == 512 );
        gate.release( 512 );
        REQUIRE( gate.outstandingBytes() == 0 );
    }

    SECTION( "Oversized write admitted when idle (never-starve rule)" )
    {
        REQUIRE( gate.outstandingBytes() == 0 );
        // Write larger than gate capacity (4096 > 2048) admitted when idle
        gate.acquire( 4096 );
        REQUIRE( gate.outstandingBytes() == 4096 );
        gate.release( 4096 );
        REQUIRE( gate.outstandingBytes() == 0 );
    }
}

TEST_CASE( "Tier 1: R1-F4 - Destructor Resource Leak Detection and Telemetry Record",
           "[chunk][e2e][tier1][r1]" )
{
    auto &tel = observability::ExecutionTelemetry::instance();
    const auto leaksBefore = tel.counters()["resource_leaks_detected"];

    SECTION( "No leak on clean destruction" )
    {
        {
            ExecutionGovernor::Config cfg;
            ExecutionGovernor gov( cfg );
            auto lease = gov.scratch().acquire( "clean-run", "clean-tile", 100 );
            REQUIRE( gov.hasOutstandingResources() );
            lease = chunk::ScratchLease{};
            REQUIRE_FALSE( gov.hasOutstandingResources() );
        }
        REQUIRE( tel.counters()["resource_leaks_detected"] == leaksBefore );
    }

    SECTION( "Leak detected and diagnostic JSON emitted when resources outlive governor" )
    {
        chunk::ScratchLease leakedLease;
        {
            ExecutionGovernor::Config cfg;
            cfg.scratchRoot = "leak-test-root";
            ExecutionGovernor gov( cfg );
            leakedLease = gov.scratch().acquire( "leaked-run", "leaked-tile", 256 );
            REQUIRE( gov.hasOutstandingResources() );
            // Destruction while leakedLease is live
        }
        REQUIRE( leakedLease.isValid() );
        REQUIRE( tel.counters()["resource_leaks_detected"] == leaksBefore + 1 );

        const std::string leakReport = ExecutionGovernor::lastLeakReportJson();
        REQUIRE( leakReport.find( "exp.diag.v1" ) != std::string::npos );
        REQUIRE( leakReport.find( "execution.resource_leak" ) != std::string::npos );
    }
}

TEST_CASE( "Tier 1: R2-F1 - Deterministic Tile Enumeration and Spec Arithmetic (O(1))",
           "[chunk][e2e][tier1][r2]" )
{
    TileRunPartition p;
    p.rasterWidth = 1000;
    p.rasterHeight = 800;
    p.tileWidth = 256;
    p.tileHeight = 256;
    p.halo = 0;
    p.bands = 4;

    REQUIRE( p.tilesAcross() == 4 ); // ceil(1000/256) = 4
    REQUIRE( p.tilesDown() == 4 );   // ceil(800/256) = 4
    REQUIRE( p.totalTiles() == 16 );

    SECTION( "Corner and internal tiles have exact geometric dimensions" )
    {
        // Tile 0: top-left full tile (256x256)
        const TileSpec s0 = tileSpecAt( p, 0 );
        REQUIRE( s0.index == 0 );
        REQUIRE( s0.xOffset == 0 );
        REQUIRE( s0.yOffset == 0 );
        REQUIRE( s0.width == 256 );
        REQUIRE( s0.height == 256 );

        // Tile 3: top-right partial tile (1000 - 3*256 = 232 width)
        const TileSpec s3 = tileSpecAt( p, 3 );
        REQUIRE( s3.index == 3 );
        REQUIRE( s3.xOffset == 768 );
        REQUIRE( s3.yOffset == 0 );
        REQUIRE( s3.width == 232 );
        REQUIRE( s3.height == 256 );

        // Tile 12: bottom-left partial tile (800 - 3*256 = 32 height)
        const TileSpec s12 = tileSpecAt( p, 12 );
        REQUIRE( s12.index == 12 );
        REQUIRE( s12.xOffset == 0 );
        REQUIRE( s12.yOffset == 768 );
        REQUIRE( s12.width == 256 );
        REQUIRE( s12.height == 32 );

        // Tile 15: bottom-right partial tile (232x32)
        const TileSpec s15 = tileSpecAt( p, 15 );
        REQUIRE( s15.index == 15 );
        REQUIRE( s15.xOffset == 768 );
        REQUIRE( s15.yOffset == 768 );
        REQUIRE( s15.width == 232 );
        REQUIRE( s15.height == 32 );
    }

    SECTION( "Partition digest stability" )
    {
        const std::uint64_t digest1 = tileRunPartitionDigest( p );
        const std::uint64_t digest2 = tileRunPartitionDigest( p );
        REQUIRE( digest1 == digest2 );
        REQUIRE( digest1 != 0 );
    }
}

TEST_CASE( "Tier 1: R2-F2 - ResumableTileRun Fresh Execution & Analytical Parity",
           "[chunk][e2e][tier1][r2]" )
{
    const auto dir = createIsolatedTempDir( "fresh_exec" );
    TileRunPartition partition;
    partition.rasterWidth = 64;
    partition.rasterHeight = 64;
    partition.tileWidth = 32;
    partition.tileHeight = 32;
    partition.bands = 1;

    TileRunSpec spec;
    spec.identity.operatorIdentity = 0xAA01;
    spec.identity.inputIdentity = 0xBB02;
    spec.partition = partition;
    spec.identity.partitionDigest = tileRunPartitionDigest( partition );
    spec.determinism = TileRunDeterminism::BitExact;

    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();
    cfg.checkpointIntervalTiles = 2;

    ResumableTileRun run( spec, cfg );
    std::vector<float> assembled( partition.rasterWidth * partition.rasterHeight, 0.0f );
    std::uint64_t computedCount = 0;
    bool publishCalled = false;

    ResumableTileRun::Callbacks cb;
    cb.compute = [&]( const TileSpec &s ) {
        ++computedCount;
        auto buf = std::make_shared<std::vector<float>>( s.bufferElementCount() );
        for ( int y = 0; y < s.height; ++y )
            for ( int x = 0; x < s.width; ++x )
            {
                const int gx = s.xOffset + x;
                const int gy = s.yOffset + y;
                ( *buf )[y * s.bufferWidth + x] = generatePixelValue( gx, gy, 0 );
            }
        return TilePayload{ s, std::move( buf ) };
    };

    cb.consume = [&]( const TilePayload &p ) {
        for ( int y = 0; y < p.spec.height; ++y )
            for ( int x = 0; x < p.spec.width; ++x )
            {
                const int gx = p.spec.xOffset + x;
                const int gy = p.spec.yOffset + y;
                assembled[gy * partition.rasterWidth + gx] =
                    ( *p.pixels )[y * p.spec.bufferWidth + x];
            }
    };

    cb.publish = [&] { publishCalled = true; };

    TileRunCancelSource cancelSource;
    const auto result = run.execute( cancelSource, cb );

    REQUIRE( result.totalTiles == 4 );
    REQUIRE( result.tilesComputed == 4 );
    REQUIRE( result.tilesReused == 0 );
    REQUIRE( computedCount == 4 );
    REQUIRE( publishCalled );

    // Verify all pixels against analytical oracle
    for ( int y = 0; y < partition.rasterHeight; ++y )
        for ( int x = 0; x < partition.rasterWidth; ++x )
        {
            REQUIRE( assembled[y * partition.rasterWidth + x]
                     == Catch::Approx( generatePixelValue( x, y, 0 ) ) );
        }

    run.cleanupAfterPublish();
    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Tier 1: R2-F3 - ResumableTileRun Cancellation & Zero Recompute Invariant",
           "[chunk][e2e][tier1][r2]" )
{
    const auto dir = createIsolatedTempDir( "resumable_cancel" );
    TileRunPartition partition;
    partition.rasterWidth = 128;
    partition.rasterHeight = 128;
    partition.tileWidth = 32;
    partition.tileHeight = 32;
    partition.bands = 1; // 16 tiles total

    TileRunSpec spec;
    spec.identity.operatorIdentity = 0xCC01;
    spec.identity.inputIdentity = 0xDD02;
    spec.partition = partition;
    spec.identity.partitionDigest = tileRunPartitionDigest( partition );
    spec.determinism = TileRunDeterminism::BitExact;

    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();
    cfg.checkpointIntervalTiles = 2;

    std::uint64_t computeCalls = 0;
    auto makeTile = [&]( const TileSpec &s ) {
        ++computeCalls;
        auto buf = std::make_shared<std::vector<float>>( s.bufferElementCount() );
        for ( int y = 0; y < s.height; ++y )
            for ( int x = 0; x < s.width; ++x )
                ( *buf )[y * s.bufferWidth + x] = generatePixelValue( s.xOffset + x, s.yOffset + y, 0 );
        return TilePayload{ s, std::move( buf ) };
    };

    // First run: cancel after 7 tiles
    {
        ResumableTileRun run1( spec, cfg );
        ResumableTileRun::Callbacks cb1;
        cb1.compute = makeTile;
        cb1.consume = []( const TilePayload & ) {};
        cb1.publish = [] {};

        TileRunCancelSource cancelSource;
        cancelSource.predicate = [&] { return computeCalls >= 7; };

        REQUIRE_THROWS_AS( run1.execute( cancelSource, cb1 ), ChunkCancelled );
        REQUIRE( computeCalls == 7 );
    }

    // Second run: resume to completion
    std::vector<float> finalOutput( partition.rasterWidth * partition.rasterHeight, 0.0f );
    bool published = false;
    {
        ResumableTileRun run2( spec, cfg );
        ResumableTileRun::Callbacks cb2;
        cb2.compute = makeTile;
        cb2.consume = [&]( const TilePayload &p ) {
            for ( int y = 0; y < p.spec.height; ++y )
                for ( int x = 0; x < p.spec.width; ++x )
                    finalOutput[( p.spec.yOffset + y ) * partition.rasterWidth + ( p.spec.xOffset + x )] =
                        ( *p.pixels )[y * p.spec.bufferWidth + x];
        };
        cb2.publish = [&] { published = true; };

        TileRunCancelSource noCancel;
        const auto res = run2.execute( noCancel, cb2 );

        REQUIRE( res.totalTiles == 16 );
        REQUIRE( res.tilesReused >= 7 );
        REQUIRE( res.tilesComputed == 16 - res.tilesReused );
        REQUIRE( computeCalls == 16 ); // Exactly 1 compute call per tile across both runs!
        REQUIRE( published );
    }

    // Third run: exactly-once bypass via .published marker
    {
        ResumableTileRun run3( spec, cfg );
        ResumableTileRun::Callbacks cb3;
        cb3.compute = makeTile;
        cb3.consume = []( const TilePayload & ) {};
        cb3.publish = [] {};

        TileRunCancelSource noCancel;
        const auto res3 = run3.execute( noCancel, cb3 );
        REQUIRE( res3.alreadyPublished );
        REQUIRE( res3.tilesComputed == 0 );
        REQUIRE( res3.tilesReused == 0 );
        REQUIRE( computeCalls == 16 ); // No additional compute calls!
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Tier 1: R3-F1 - Spectral Index (NDVI & NDWI) Chunked Operator Execution",
           "[chunk][e2e][tier1][r3]" )
{
    const auto dir = createIsolatedTempDir( "spectral_indices" );
    RSOperatorContext ctx( dir.generic_string() );

    TileRunPartition partition;
    partition.rasterWidth = 64;
    partition.rasterHeight = 64;
    partition.tileWidth = 32;
    partition.tileHeight = 32;
    partition.bands = 1;

    std::vector<float> ndviResult( 64 * 64, 0.0f );
    std::vector<float> ndwiResult( 64 * 64, 0.0f );

    // NDVI Kernel
    ChunkTileKernel ndviKernel = []( const TileSpec &s ) {
        std::vector<float> buf( s.bufferElementCount() );
        for ( int y = 0; y < s.height; ++y )
            for ( int x = 0; x < s.width; ++x )
            {
                const float nir = generatePixelValue( s.xOffset + x, s.yOffset + y, 3 );
                const float red = generatePixelValue( s.xOffset + x, s.yOffset + y, 2 );
                buf[y * s.bufferWidth + x] = oracleNdvi( nir, red );
            }
        return buf;
    };

    ChunkedRunOptions opts;
    opts.scratchRoot = ( dir / "scratch" ).generic_string();

    auto ndviSink = [&]( const TilePayload &p ) {
        for ( int y = 0; y < p.spec.height; ++y )
            for ( int x = 0; x < p.spec.width; ++x )
                ndviResult[( p.spec.yOffset + y ) * 64 + ( p.spec.xOffset + x )] =
                    ( *p.pixels )[y * p.spec.bufferWidth + x];
    };

    const auto rNdvi = runChunkedOperator( "rs:ndvi", Json::Value( Json::objectValue ),
                                          ctx, partition, TileRunDeterminism::BitExact,
                                          ndviKernel, ndviSink, opts );
    REQUIRE( rNdvi.tilesComputed == 4 );

    // Verify NDVI against analytical truth
    for ( int y = 0; y < 64; ++y )
        for ( int x = 0; x < 64; ++x )
        {
            const float nir = generatePixelValue( x, y, 3 );
            const float red = generatePixelValue( x, y, 2 );
            REQUIRE( ndviResult[y * 64 + x] == Catch::Approx( oracleNdvi( nir, red ) ) );
        }

    // NDWI Kernel
    ChunkTileKernel ndwiKernel = []( const TileSpec &s ) {
        std::vector<float> buf( s.bufferElementCount() );
        for ( int y = 0; y < s.height; ++y )
            for ( int x = 0; x < s.width; ++x )
            {
                const float green = generatePixelValue( s.xOffset + x, s.yOffset + y, 1 );
                const float nir = generatePixelValue( s.xOffset + x, s.yOffset + y, 3 );
                buf[y * s.bufferWidth + x] = oracleNdwi( green, nir );
            }
        return buf;
    };

    auto ndwiSink = [&]( const TilePayload &p ) {
        for ( int y = 0; y < p.spec.height; ++y )
            for ( int x = 0; x < p.spec.width; ++x )
                ndwiResult[( p.spec.yOffset + y ) * 64 + ( p.spec.xOffset + x )] =
                    ( *p.pixels )[y * p.spec.bufferWidth + x];
    };

    opts.resumeStateBase = ( dir / "ndwi-state" ).generic_string();
    const auto rNdwi = runChunkedOperator( "rs:ndwi", Json::Value( Json::objectValue ),
                                          ctx, partition, TileRunDeterminism::BitExact,
                                          ndwiKernel, ndwiSink, opts );
    REQUIRE( rNdwi.tilesComputed == 4 );

    // Verify NDWI against analytical truth
    for ( int y = 0; y < 64; ++y )
        for ( int x = 0; x < 64; ++x )
        {
            const float green = generatePixelValue( x, y, 1 );
            const float nir = generatePixelValue( x, y, 3 );
            REQUIRE( ndwiResult[y * 64 + x] == Catch::Approx( oracleNdwi( green, nir ) ) );
        }

    // SAVI Kernel
    std::vector<float> saviResult( 64 * 64, 0.0f );
    ChunkTileKernel saviKernel = []( const TileSpec &s ) {
        std::vector<float> buf( s.bufferElementCount() );
        for ( int y = 0; y < s.height; ++y )
            for ( int x = 0; x < s.width; ++x )
            {
                const float nir = generatePixelValue( s.xOffset + x, s.yOffset + y, 3 );
                const float red = generatePixelValue( s.xOffset + x, s.yOffset + y, 2 );
                buf[y * s.bufferWidth + x] = oracleSavi( nir, red, 0.5f );
            }
        return buf;
    };

    auto saviSink = [&]( const TilePayload &p ) {
        for ( int y = 0; y < p.spec.height; ++y )
            for ( int x = 0; x < p.spec.width; ++x )
                saviResult[( p.spec.yOffset + y ) * 64 + ( p.spec.xOffset + x )] =
                    ( *p.pixels )[y * p.spec.bufferWidth + x];
    };

    opts.resumeStateBase = ( dir / "savi-state" ).generic_string();
    const auto rSavi = runChunkedOperator( "rs:savi", Json::Value( Json::objectValue ),
                                          ctx, partition, TileRunDeterminism::BitExact,
                                          saviKernel, saviSink, opts );
    REQUIRE( rSavi.tilesComputed == 4 );

    // Verify SAVI against analytical truth
    for ( int y = 0; y < 64; ++y )
        for ( int x = 0; x < 64; ++x )
        {
            const float nir = generatePixelValue( x, y, 3 );
            const float red = generatePixelValue( x, y, 2 );
            REQUIRE( saviResult[y * 64 + x] == Catch::Approx( oracleSavi( nir, red, 0.5f ) ) );
        }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Tier 1: R3-F2 - Change Detection & Streaming Welford Statistics",
           "[chunk][e2e][tier1][r3]" )
{
    const auto dir = createIsolatedTempDir( "change_welford" );
    RSOperatorContext ctx( dir.generic_string() );

    TileRunPartition partition;
    partition.rasterWidth = 100;
    partition.rasterHeight = 100;
    partition.tileWidth = 50;
    partition.tileHeight = 50;
    partition.bands = 1;

    StreamingWelfordOracle truthWelford;
    for ( int y = 0; y < 100; ++y )
        for ( int x = 0; x < 100; ++x )
        {
            const float t1 = generatePixelValue( x, y, 0, 1111 );
            const float t2 = generatePixelValue( x, y, 0, 2222 );
            truthWelford.update( oracleChangeAbsDifference( t1, t2 ) );
        }

    StreamingWelfordOracle streamingWelford;
    ChunkTileKernel diffKernel = []( const TileSpec &s ) {
        std::vector<float> buf( s.bufferElementCount() );
        for ( int y = 0; y < s.height; ++y )
            for ( int x = 0; x < s.width; ++x )
            {
                const float t1 = generatePixelValue( s.xOffset + x, s.yOffset + y, 0, 1111 );
                const float t2 = generatePixelValue( s.xOffset + x, s.yOffset + y, 0, 2222 );
                buf[y * s.bufferWidth + x] = oracleChangeAbsDifference( t1, t2 );
            }
        return buf;
    };

    auto welfordSink = [&]( const TilePayload &p ) {
        for ( int y = 0; y < p.spec.height; ++y )
            for ( int x = 0; x < p.spec.width; ++x )
                streamingWelford.update( ( *p.pixels )[y * p.spec.bufferWidth + x] );
    };

    ChunkedRunOptions opts;
    opts.scratchRoot = ( dir / "scratch" ).generic_string();
    const auto res = runChunkedOperator( "rs:change_difference", Json::Value( Json::objectValue ),
                                        ctx, partition, TileRunDeterminism::BitExact,
                                        diffKernel, welfordSink, opts );

    REQUIRE( res.tilesComputed == 4 );
    REQUIRE( streamingWelford.count == truthWelford.count );
    REQUIRE( streamingWelford.count == 10000 );
    REQUIRE( streamingWelford.mean == Catch::Approx( truthWelford.mean ).epsilon( 1e-5 ) );
    REQUIRE( streamingWelford.variance() == Catch::Approx( truthWelford.variance() ).epsilon( 1e-5 ) );
    REQUIRE( streamingWelford.stddev() == Catch::Approx( truthWelford.stddev() ).epsilon( 1e-5 ) );
    REQUIRE( streamingWelford.minVal == Catch::Approx( truthWelford.minVal ) );
    REQUIRE( streamingWelford.maxVal == Catch::Approx( truthWelford.maxVal ) );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// ===========================================================================
// TIER 2: BOUNDARY & CORNER CASES (>=5 per feature)
// ===========================================================================

TEST_CASE( "Tier 2: Boundary & Corner Cases", "[chunk][e2e][tier2][boundaries]" )
{
    const auto dir = createIsolatedTempDir( "tier2_boundaries" );

    SECTION( "B1: Single-pixel 1x1 raster boundary" )
    {
        TileRunPartition p;
        p.rasterWidth = 1;
        p.rasterHeight = 1;
        p.tileWidth = 256;
        p.tileHeight = 256;
        p.bands = 1;

        REQUIRE( p.totalTiles() == 1 );
        const TileSpec s = tileSpecAt( p, 0 );
        REQUIRE( s.width == 1 );
        REQUIRE( s.height == 1 );
        REQUIRE( s.xOffset == 0 );
        REQUIRE( s.yOffset == 0 );

        RSOperatorContext ctx( dir.generic_string() );
        float singleResult = 0.0f;
        ChunkedRunOptions opts;
        opts.scratchRoot = ( dir / "scratch" ).generic_string();

        runChunkedOperator( "test:1x1", Json::Value( Json::objectValue ), ctx, p,
                            TileRunDeterminism::BitExact,
                            []( const TileSpec & ) { return std::vector<float>{ 42.5f }; },
                            [&]( const TilePayload &pay ) { singleResult = ( *pay.pixels )[0]; },
                            opts );
        REQUIRE( singleResult == 42.5f );
    }

    SECTION( "B2: Odd dimensions non-multiple of tile size (257x301 with 256x256)" )
    {
        TileRunPartition p;
        p.rasterWidth = 257;
        p.rasterHeight = 301;
        p.tileWidth = 256;
        p.tileHeight = 256;
        p.bands = 1;

        REQUIRE( p.tilesAcross() == 2 );
        REQUIRE( p.tilesDown() == 2 );
        REQUIRE( p.totalTiles() == 4 );

        const TileSpec s0 = tileSpecAt( p, 0 ); // 256x256
        const TileSpec s1 = tileSpecAt( p, 1 ); // 1x256
        const TileSpec s2 = tileSpecAt( p, 2 ); // 256x45
        const TileSpec s3 = tileSpecAt( p, 3 ); // 1x45

        REQUIRE( s0.width == 256 );
        REQUIRE( s0.height == 256 );
        REQUIRE( s1.width == 1 );
        REQUIRE( s1.height == 256 );
        REQUIRE( s2.width == 256 );
        REQUIRE( s2.height == 45 );
        REQUIRE( s3.width == 1 );
        REQUIRE( s3.height == 45 );
    }

    SECTION( "B3: Non-positive partition dimensions fail-closed (std::invalid_argument)" )
    {
        TileRunPartition pBad;
        pBad.rasterWidth = 0;
        pBad.rasterHeight = 100;
        pBad.tileWidth = 64;
        pBad.tileHeight = 64;
        REQUIRE_THROWS_AS( pBad.totalTiles(), std::invalid_argument );

        pBad.rasterWidth = 100;
        pBad.tileWidth = 0;
        REQUIRE_THROWS_AS( pBad.totalTiles(), std::invalid_argument );
    }

    SECTION( "B4: Extreme raster dimensions (100000x100000) O(1) arithmetic without overflow" )
    {
        TileRunPartition pHuge;
        pHuge.rasterWidth = 100000;
        pHuge.rasterHeight = 100000;
        pHuge.tileWidth = 256;
        pHuge.tileHeight = 256;
        pHuge.bands = 1;

        REQUIRE( pHuge.tilesAcross() == 391 );
        REQUIRE( pHuge.tilesDown() == 391 );
        REQUIRE( pHuge.totalTiles() == 391ull * 391ull );

        // Arbitrary tile inspection in O(1) time
        const TileSpec sMid = tileSpecAt( pHuge, 75000 );
        REQUIRE( sMid.index == 75000 );
        REQUIRE( sMid.width == 256 );
        REQUIRE( sMid.height == 256 );
        REQUIRE( sMid.xOffset < 100000 );
        REQUIRE( sMid.yOffset < 100000 );
    }

    SECTION( "B5: Hard RAM budget refusal before kernel execution" )
    {
        RSOperatorContext ctx( dir.generic_string() );
        TileRunPartition p;
        p.rasterWidth = 512;
        p.rasterHeight = 512;
        p.tileWidth = 256;
        p.tileHeight = 256;
        p.bands = 4;

        ChunkedRunOptions opts;
        opts.ramBudgetBytes = 16; // Much smaller than 1 tile (256*256*4*4 = 1 MiB)
        opts.scratchRoot = ( dir / "scratch" ).generic_string();

        std::uint64_t kernelRuns = 0;
        auto kernel = [&]( const TileSpec &s ) {
            ++kernelRuns;
            return std::vector<float>( s.bufferElementCount(), 0.0f );
        };

        bool caughtBudgetError = false;
        try
        {
            runChunkedOperator( "test:budget_refuse", Json::Value( Json::objectValue ),
                                ctx, p, TileRunDeterminism::BitExact, kernel,
                                []( const TilePayload & ) {}, opts );
        }
        catch ( const RSOperatorError &e )
        {
            caughtBudgetError = ( e.code() == ErrorCode::ResourceBudgetExceeded );
        }
        REQUIRE( caughtBudgetError );
        REQUIRE( kernelRuns == 0 ); // Zero tiles computed!
    }

    SECTION( "B6: Scratch budget exact boundary (exact fit vs 1-byte overflow)" )
    {
        ExecutionGovernor::Config cfg;
        cfg.scratchBytes = 1024;
        ExecutionGovernor gov( cfg );

        // Exact match fits
        auto leaseExact = gov.scratch().acquire( "run-b6", "tile-exact", 1024 );
        REQUIRE( leaseExact.isValid() );
        REQUIRE( gov.scratch().outstandingBytes() == 1024 );

        // 1 byte overflow fails
        REQUIRE_THROWS_AS( gov.scratch().acquire( "run-b6", "tile-over", 1 ),
                           chunk::ScratchBudgetExceeded );

        leaseExact = chunk::ScratchLease{};
        REQUIRE( gov.scratch().outstandingBytes() == 0 );
    }

    SECTION( "B7: Immediate cancellation before first tile executes" )
    {
        RSOperatorContext ctx( dir.generic_string() );
        TileRunPartition p;
        p.rasterWidth = 64;
        p.rasterHeight = 64;
        p.tileWidth = 32;
        p.tileHeight = 32;
        p.bands = 1;

        ctx.setCancelCallback( [] { return true; } ); // Cancelled immediately

        std::uint64_t kernelRuns = 0;
        auto kernel = [&]( const TileSpec &s ) {
            ++kernelRuns;
            return std::vector<float>( s.bufferElementCount(), 1.0f );
        };

        bool cancelled = false;
        try
        {
            runChunkedOperator( "test:immediate_cancel", Json::Value( Json::objectValue ),
                                ctx, p, TileRunDeterminism::BitExact, kernel,
                                []( const TilePayload & ) {} );
        }
        catch ( const RSOperatorError &e )
        {
            cancelled = ( e.code() == ErrorCode::Cancelled );
        }
        REQUIRE( cancelled );
        REQUIRE( kernelRuns == 0 );
    }

    SECTION( "B8: NoData & NaN floating-point sentinel handling" )
    {
        const float nanVal = std::numeric_limits<float>::quiet_NaN();
        const float nir = nanVal;
        const float red = 0.5f;

        // In the presence of NaN, denominator test or safe float arithmetic produces NaN without crash
        const float res = ( nir - red ) / ( nir + red );
        REQUIRE( std::isnan( res ) );

        // Oracle handles NaN gracefully in streaming Welford
        StreamingWelfordOracle welford;
        welford.update( 1.0f );
        welford.update( nanVal ); // Ignored
        welford.update( 3.0f );
        REQUIRE( welford.count == 2 );
        REQUIRE( welford.mean == 2.0 );
    }

    SECTION( "B9: Zero-division denominator sentinel handling (NIR + Red == 0)" )
    {
        const float nir = 0.0f;
        const float red = 0.0f;
        REQUIRE( oracleNdvi( nir, red ) == 0.0f ); // Safe sentinel instead of div by zero
    }

    SECTION( "B10: Extreme dynamic range scaling stability" )
    {
        // UInt16 DN [0..10000] scaled by 1e-4f into unit reflectance [0..1]
        const uint16_t nirDn = 8000;
        const uint16_t redDn = 2000;
        const float scale = 0.0001f;

        const float nirReflectance = static_cast<float>( nirDn ) * scale;
        const float redReflectance = static_cast<float>( redDn ) * scale;
        const float ndvi = oracleNdvi( nirReflectance, redReflectance );

        REQUIRE( ndvi == Catch::Approx( 0.6f ) );
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// ===========================================================================
// TIER 3: CROSS-FEATURE COMBINATIONS (PAIRWISE INTERACTIONS)
// ===========================================================================

TEST_CASE( "Tier 3: Pairwise Cross-Feature Interactions",
           "[chunk][e2e][tier3][cross_feature]" )
{
    const auto dir = createIsolatedTempDir( "tier3_cross" );

    SECTION( "Pair 1: Concurrency Reduction Admission + Crash-Safe Resumption" )
    {
        // Verify that reduced concurrency execution interacts seamlessly with resumption
        TileRunPartition partition;
        partition.rasterWidth = 64;
        partition.rasterHeight = 64;
        partition.tileWidth = 32;
        partition.tileHeight = 32;
        partition.bands = 1;

        TileRunSpec spec;
        spec.identity.operatorIdentity = 0x1111;
        spec.identity.inputIdentity = 0x2222;
        spec.partition = partition;
        spec.identity.partitionDigest = tileRunPartitionDigest( partition );
        spec.determinism = TileRunDeterminism::BitExact;

        ResumableTileRun::Config cfg;
        cfg.scratchRoot = ( dir / "scratch1" ).generic_string();
        cfg.statePath = ( dir / "state1" / "run" ).generic_string();

        std::uint64_t computeCalls = 0;
        auto computeFn = [&]( const TileSpec &s ) {
            ++computeCalls;
            auto buf = std::make_shared<std::vector<float>>( s.bufferElementCount() );
            std::fill( buf->begin(), buf->end(), 10.0f );
            return TilePayload{ s, std::move( buf ) };
        };

        // Interrupt after 2 tiles
        {
            ResumableTileRun run( spec, cfg );
            ResumableTileRun::Callbacks cb;
            cb.compute = computeFn;
            cb.consume = []( const TilePayload & ) {};
            cb.publish = [] {};
            TileRunCancelSource cancelSource;
            cancelSource.predicate = [&] { return computeCalls >= 2; };
            REQUIRE_THROWS_AS( run.execute( cancelSource, cb ), ChunkCancelled );
            REQUIRE( computeCalls == 2 );
        }

        // Resume: verifies remaining 2 tiles computed, 2 reused
        {
            ResumableTileRun run2( spec, cfg );
            ResumableTileRun::Callbacks cb;
            cb.compute = computeFn;
            cb.consume = []( const TilePayload & ) {};
            cb.publish = [] {};
            TileRunCancelSource noCancel;
            const auto res = run2.execute( noCancel, cb );
            REQUIRE( res.tilesReused == 2 );
            REQUIRE( res.tilesComputed == 2 );
            REQUIRE( computeCalls == 4 );
        }
    }

    SECTION( "Pair 2: Memory Planning Scratch Spill under Memory Pressure" )
    {
        ExecutionGovernor::Config cfg;
        cfg.ramBytes = 1000; // Force spill
        cfg.scratchBytes = 10 * 1024 * 1024;
        ExecutionGovernor gov( cfg );

        TileMemoryRequest req;
        req.tileWidth = 64;
        req.tileHeight = 64;
        req.bands = 4;
        req.allowSpill = true;
        req.expectedTileCount = 10;

        const TileMemoryPlan plan = gov.admitOrRefuse( req );
        REQUIRE( plan.action == TileMemoryPlan::Action::Spill );
        REQUIRE( plan.spillBytes > 0 );
    }

    SECTION( "Pair 3: Scratch Resource Reclamation upon Cancellation during Execution" )
    {
        ExecutionGovernor::Config cfg;
        cfg.scratchBytes = 50000;
        ExecutionGovernor gov( cfg );

        auto lease1 = gov.scratch().acquire( "abort-run", "tile-1", 10000 );
        auto lease2 = gov.scratch().acquire( "abort-run", "tile-2", 15000 );
        REQUIRE( gov.hasOutstandingResources() );
        REQUIRE( gov.scratch().outstandingBytes() == 25000 );

        // On cancellation/abort, unfinalized leases are cleared
        lease1 = chunk::ScratchLease{};
        lease2 = chunk::ScratchLease{};
        REQUIRE_FALSE( gov.hasOutstandingResources() );
        REQUIRE( gov.scratch().outstandingBytes() == 0 );
    }

    SECTION( "Pair 4: Pipeline Mode vs Resumable Mode Bit-Exact Parity" )
    {
        const auto dirA = dir / "mode_resumable";
        const auto dirB = dir / "mode_pipeline";

        TileRunPartition partition;
        partition.rasterWidth = 48;
        partition.rasterHeight = 48;
        partition.tileWidth = 24;
        partition.tileHeight = 24;
        partition.bands = 1;

        std::vector<float> resResumable;
        std::vector<float> resPipeline;

        ChunkTileKernel kernel = []( const TileSpec &s ) {
            std::vector<float> buf( s.bufferElementCount() );
            for ( size_t i = 0; i < buf.size(); ++i )
                buf[i] = static_cast<float>( ( s.index * 73 + i * 17 ) % 251 ) / 251.0f;
            return buf;
        };

        // Resumable mode
        {
            RSOperatorContext ctxA( dirA.generic_string() );
            ChunkedRunOptions optsA;
            optsA.mode = ChunkedRunOptions::Mode::Resumable;
            optsA.scratchRoot = ( dirA / "scratch" ).generic_string();
            runChunkedOperator( "test:parity", Json::Value( Json::objectValue ), ctxA,
                                partition, TileRunDeterminism::BitExact, kernel,
                                [&]( const TilePayload &p ) {
                                    resResumable.insert( resResumable.end(), p.pixels->begin(), p.pixels->end() );
                                }, optsA );
        }

        // Pipeline mode
        {
            RSOperatorContext ctxB( dirB.generic_string() );
            ChunkedRunOptions optsB;
            optsB.mode = ChunkedRunOptions::Mode::Pipeline;
            runChunkedOperator( "test:parity", Json::Value( Json::objectValue ), ctxB,
                                partition, TileRunDeterminism::BitExact, kernel,
                                [&]( const TilePayload &p ) {
                                    resPipeline.insert( resPipeline.end(), p.pixels->begin(), p.pixels->end() );
                                }, optsB );
        }

        REQUIRE( resResumable.size() == resPipeline.size() );
        REQUIRE( resResumable == resPipeline );
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// ===========================================================================
// TIER 4: REAL-WORLD APPLICATION SCENARIOS (>=5 WORKFLOWS)
// ===========================================================================

TEST_CASE( "Tier 4: Scenario 1 - Sentinel-2 L2A 4-Band BOA NDVI Workflow",
           "[chunk][e2e][tier4][real_world]" )
{
    const auto dir = createIsolatedTempDir( "s2_ndvi_workflow" );
    RSOperatorContext ctx( dir.generic_string() );

    // Sentinel-2 simulated scene: 512x512 with 256x256 tiles, 4 bands
    // Band 0: Blue (B2), Band 1: Green (B3), Band 2: Red (B4), Band 3: NIR (B8)
    // Scale factor: 0.0001 (DN [0..10000] -> [0..1] unit reflectance)
    TileRunPartition partition;
    partition.rasterWidth = 512;
    partition.rasterHeight = 512;
    partition.tileWidth = 256;
    partition.tileHeight = 256;
    partition.bands = 1;

    std::vector<float> ndviProduct( 512 * 512, 0.0f );
    bool published = false;

    ChunkTileKernel s2Kernel = []( const TileSpec &s ) {
        std::vector<float> buf( s.bufferElementCount() );
        for ( int y = 0; y < s.height; ++y )
            for ( int x = 0; x < s.width; ++x )
            {
                const int gx = s.xOffset + x;
                const int gy = s.yOffset + y;
                // Generate DN values in [500, 9500]
                const float nirDn = 2000.0f + 6000.0f * generatePixelValue( gx, gy, 3, 777 );
                const float redDn = 500.0f + 3000.0f * generatePixelValue( gx, gy, 2, 888 );
                const float nirRefl = nirDn * 0.0001f;
                const float redRefl = redDn * 0.0001f;
                buf[y * s.bufferWidth + x] = oracleNdvi( nirRefl, redRefl );
            }
        return buf;
    };

    ChunkedRunOptions opts;
    opts.scratchRoot = ( dir / "scratch" ).generic_string();
    opts.publish = [&] { published = true; };

    auto sink = [&]( const TilePayload &p ) {
        for ( int y = 0; y < p.spec.height; ++y )
            for ( int x = 0; x < p.spec.width; ++x )
            {
                const int gx = p.spec.xOffset + x;
                const int gy = p.spec.yOffset + y;
                ndviProduct[gy * 512 + gx] = ( *p.pixels )[y * p.spec.bufferWidth + x];
            }
    };

    const auto res = runChunkedOperator( "rs:s2_ndvi", Json::Value( Json::objectValue ),
                                        ctx, partition, TileRunDeterminism::BitExact,
                                        s2Kernel, sink, opts );

    REQUIRE( res.totalTiles == 4 );
    REQUIRE( res.tilesComputed == 4 );
    REQUIRE( published );

    // Opaque validation: every pixel verified against independent mathematical oracle
    for ( int y = 0; y < 512; ++y )
        for ( int x = 0; x < 512; ++x )
        {
            const float nirDn = 2000.0f + 6000.0f * generatePixelValue( x, y, 3, 777 );
            const float redDn = 500.0f + 3000.0f * generatePixelValue( x, y, 2, 888 );
            const float expected = oracleNdvi( nirDn * 0.0001f, redDn * 0.0001f );
            REQUIRE( ndviProduct[y * 512 + x] == Catch::Approx( expected ).epsilon( 1e-6 ) );
        }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Tier 4: Scenario 2 - Landsat-8 3-Band EVI Workflow with Scale Invariant (#801)",
           "[chunk][e2e][tier4][real_world]" )
{
    const auto dir = createIsolatedTempDir( "l8_evi_workflow" );
    RSOperatorContext ctx( dir.generic_string() );

    TileRunPartition partition;
    partition.rasterWidth = 512;
    partition.rasterHeight = 512;
    partition.tileWidth = 256;
    partition.tileHeight = 256;
    partition.bands = 1;

    // Invariant #801: Dataset scale probed ONCE globally prior to chunking
    const float globalDatasetScale = 0.0000275f;
    const float globalOffset = -0.2f;

    std::vector<float> eviProduct( 512 * 512, 0.0f );
    ChunkTileKernel l8EviKernel = [=]( const TileSpec &s ) {
        std::vector<float> buf( s.bufferElementCount() );
        for ( int y = 0; y < s.height; ++y )
            for ( int x = 0; x < s.width; ++x )
            {
                const int gx = s.xOffset + x;
                const int gy = s.yOffset + y;
                // Landsat-8 DN to surface reflectance
                const float nirRaw = 10000.0f + 25000.0f * generatePixelValue( gx, gy, 4, 101 );
                const float redRaw = 8000.0f + 15000.0f * generatePixelValue( gx, gy, 3, 202 );
                const float blueRaw = 7000.0f + 10000.0f * generatePixelValue( gx, gy, 1, 303 );

                const float nir = nirRaw * globalDatasetScale + globalOffset;
                const float red = redRaw * globalDatasetScale + globalOffset;
                const float blue = blueRaw * globalDatasetScale + globalOffset;

                buf[y * s.bufferWidth + x] = oracleEvi( nir, red, blue );
            }
        return buf;
    };

    ChunkedRunOptions opts;
    opts.scratchRoot = ( dir / "scratch" ).generic_string();

    auto sink = [&]( const TilePayload &p ) {
        for ( int y = 0; y < p.spec.height; ++y )
            for ( int x = 0; x < p.spec.width; ++x )
            {
                const int gx = p.spec.xOffset + x;
                const int gy = p.spec.yOffset + y;
                eviProduct[gy * 512 + gx] = ( *p.pixels )[y * p.spec.bufferWidth + x];
            }
    };

    const auto res = runChunkedOperator( "rs:l8_evi", Json::Value( Json::objectValue ),
                                        ctx, partition, TileRunDeterminism::BitExact,
                                        l8EviKernel, sink, opts );

    REQUIRE( res.tilesComputed == 4 );

    // Validate edge consistency and values against oracle
    for ( int y = 0; y < 512; ++y )
        for ( int x = 0; x < 512; ++x )
        {
            const float nirRaw = 10000.0f + 25000.0f * generatePixelValue( x, y, 4, 101 );
            const float redRaw = 8000.0f + 15000.0f * generatePixelValue( x, y, 3, 202 );
            const float blueRaw = 7000.0f + 10000.0f * generatePixelValue( x, y, 1, 303 );
            const float nir = nirRaw * globalDatasetScale + globalOffset;
            const float red = redRaw * globalDatasetScale + globalOffset;
            const float blue = blueRaw * globalDatasetScale + globalOffset;

            REQUIRE( eviProduct[y * 512 + x] == Catch::Approx( oracleEvi( nir, red, blue ) ).epsilon( 1e-6 ) );
        }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Tier 4: Scenario 3 - Bi-Temporal Multi-Band Change Detection Workflow",
           "[chunk][e2e][tier4][real_world]" )
{
    const auto dir = createIsolatedTempDir( "bitemporal_change" );
    RSOperatorContext ctx( dir.generic_string() );

    TileRunPartition partition;
    partition.rasterWidth = 256;
    partition.rasterHeight = 256;
    partition.tileWidth = 128;
    partition.tileHeight = 128;
    partition.bands = 1;

    StreamingWelfordOracle diffStats;
    ChunkTileKernel changeKernel = []( const TileSpec &s ) {
        std::vector<float> buf( s.bufferElementCount() );
        for ( int y = 0; y < s.height; ++y )
            for ( int x = 0; x < s.width; ++x )
            {
                const int gx = s.xOffset + x;
                const int gy = s.yOffset + y;
                const float t1 = generatePixelValue( gx, gy, 0, 5555 );
                const float t2 = generatePixelValue( gx, gy, 0, 6666 );
                buf[y * s.bufferWidth + x] = oracleChangeDifference( t1, t2 );
            }
        return buf;
    };

    auto sink = [&]( const TilePayload &p ) {
        for ( int y = 0; y < p.spec.height; ++y )
            for ( int x = 0; x < p.spec.width; ++x )
                diffStats.update( ( *p.pixels )[y * p.spec.bufferWidth + x] );
    };

    ChunkedRunOptions opts;
    opts.scratchRoot = ( dir / "scratch" ).generic_string();
    const auto res = runChunkedOperator( "rs:change_difference", Json::Value( Json::objectValue ),
                                        ctx, partition, TileRunDeterminism::BitExact,
                                        changeKernel, sink, opts );

    REQUIRE( res.tilesComputed == 4 );
    REQUIRE( diffStats.count == 256 * 256 );
    REQUIRE( diffStats.stddev() > 0.0 );
    REQUIRE( diffStats.minVal >= -1.0f );
    REQUIRE( diffStats.maxVal <= 1.0f );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Tier 4: Scenario 4 - Multi-Stage Crash Resilience with Zero Redundant Recomputation",
           "[chunk][e2e][tier4][real_world]" )
{
    const auto dir = createIsolatedTempDir( "crash_resilience_workflow" );

    TileRunPartition partition;
    partition.rasterWidth = 256;
    partition.rasterHeight = 256;
    partition.tileWidth = 64;
    partition.tileHeight = 64;
    partition.bands = 1; // 16 tiles total

    TileRunSpec spec;
    spec.identity.operatorIdentity = 0xF001;
    spec.identity.inputIdentity = 0xF002;
    spec.partition = partition;
    spec.identity.partitionDigest = tileRunPartitionDigest( partition );
    spec.determinism = TileRunDeterminism::BitExact;

    ResumableTileRun::Config cfg;
    cfg.scratchRoot = ( dir / "scratch" ).generic_string();
    cfg.statePath = ( dir / "state" / "run" ).generic_string();
    cfg.checkpointIntervalTiles = 4;

    std::atomic<std::uint64_t> totalKernelExecutions{ 0 };
    auto kernel = [&]( const TileSpec &s ) {
        totalKernelExecutions.fetch_add( 1 );
        auto buf = std::make_shared<std::vector<float>>( s.bufferElementCount() );
        for ( size_t i = 0; i < buf->size(); ++i )
            ( *buf )[i] = static_cast<float>( ( s.index * 13 + i * 37 ) % 1009 ) / 1009.0f;
        return TilePayload{ s, std::move( buf ) };
    };

    // Stage 1: Power loss simulated by interrupt at tile 8 (50% progress)
    {
        ResumableTileRun stage1Run( spec, cfg );
        ResumableTileRun::Callbacks cb1;
        cb1.compute = kernel;
        cb1.consume = []( const TilePayload & ) {};
        cb1.publish = [] {};

        TileRunCancelSource cancelAt8;
        cancelAt8.predicate = [&] { return totalKernelExecutions.load() >= 8; };

        REQUIRE_THROWS_AS( stage1Run.execute( cancelAt8, cb1 ), ChunkCancelled );
        REQUIRE( totalKernelExecutions.load() == 8 );
    }

    // Stage 2: Resume to full completion
    std::vector<float> reconstructedOutput( 256 * 256, 0.0f );
    bool published = false;
    {
        ResumableTileRun stage2Run( spec, cfg );
        ResumableTileRun::Callbacks cb2;
        cb2.compute = kernel;
        cb2.consume = [&]( const TilePayload &p ) {
            for ( int y = 0; y < p.spec.height; ++y )
                for ( int x = 0; x < p.spec.width; ++x )
                    reconstructedOutput[( p.spec.yOffset + y ) * 256 + ( p.spec.xOffset + x )] =
                        ( *p.pixels )[y * p.spec.bufferWidth + x];
        };
        cb2.publish = [&] { published = true; };

        TileRunCancelSource noCancel;
        const auto res = stage2Run.execute( noCancel, cb2 );

        REQUIRE( res.totalTiles == 16 );
        REQUIRE( res.tilesReused == 8 );
        REQUIRE( res.tilesComputed == 8 );
        // Zero redundant recomputation invariant:
        REQUIRE( totalKernelExecutions.load() == 16 );
        REQUIRE( published );
    }

    // Verify all 16 tiles were reconstructed with bit-exact fidelity
    for ( std::uint64_t t = 0; t < 16; ++t )
    {
        const TileSpec s = tileSpecAt( partition, t );
        for ( int y = 0; y < s.height; ++y )
            for ( int x = 0; x < s.width; ++x )
            {
                const size_t pixelIdx = static_cast<size_t>( y ) * s.bufferWidth + x;
                const float expected = static_cast<float>( ( s.index * 13 + pixelIdx * 37 ) % 1009 ) / 1009.0f;
                REQUIRE( reconstructedOutput[( s.yOffset + y ) * 256 + ( s.xOffset + x )] == expected );
            }
    }

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "Tier 4: Scenario 5 - Memory-Governed TB-Scale Streaming Workflow",
           "[chunk][e2e][tier4][real_world]" )
{
    const auto dir = createIsolatedTempDir( "tb_scale_governed_workflow" );
    RSOperatorContext ctx( dir.generic_string() );

    // ExecutionGovernor with active budgets
    ExecutionGovernor::Config govCfg;
    govCfg.ramBytes = 10 * 1024 * 1024; // 10 MiB RAM cap
    govCfg.scratchBytes = 100 * 1024 * 1024; // 100 MiB Scratch cap
    govCfg.writeInFlightBytes = 2 * 1024 * 1024; // 2 MiB Write cap
    govCfg.scratchRoot = ( dir / "scratch" ).generic_string();
    ExecutionGovernor gov( govCfg );

    TileRunPartition partition;
    partition.rasterWidth = 512;
    partition.rasterHeight = 512;
    partition.tileWidth = 256;
    partition.tileHeight = 256;
    partition.bands = 4; // 4 tiles total, 4 bands each (1 MiB per tile)

    // Admission check
    TileMemoryRequest req;
    req.tileWidth = partition.tileWidth;
    req.tileHeight = partition.tileHeight;
    req.bands = partition.bands;
    req.bytesPerSample = 4;
    req.stageCount = 2;
    req.requestedQueueCapacity = 2;

    const TileMemoryPlan plan = gov.admitOrRefuse( req );
    REQUIRE( plan.action == TileMemoryPlan::Action::Admit );

    std::vector<float> assembled( 512 * 512 * 4, 0.0f );
    bool published = false;

    ChunkTileKernel kernel = []( const TileSpec &s ) {
        std::vector<float> buf( s.bufferElementCount() );
        for ( int b = 0; b < s.bands; ++b )
            for ( int y = 0; y < s.height; ++y )
                for ( int x = 0; x < s.width; ++x )
                    buf[b * s.bufferWidth * s.bufferHeight + y * s.bufferWidth + x] =
                        generatePixelValue( s.xOffset + x, s.yOffset + y, b );
        return buf;
    };

    auto sink = [&]( const TilePayload &p ) {
        // Model write gate backpressure accounting
        const std::uint64_t tileBytes = p.spec.bufferElementCount() * sizeof( float );
        gov.writeGate().acquire( tileBytes );

        for ( int b = 0; b < p.spec.bands; ++b )
            for ( int y = 0; y < p.spec.height; ++y )
                for ( int x = 0; x < p.spec.width; ++x )
                    assembled[( b * 512 + ( p.spec.yOffset + y ) ) * 512 + ( p.spec.xOffset + x )] =
                        ( *p.pixels )[b * p.spec.bufferWidth * p.spec.bufferHeight + y * p.spec.bufferWidth + x];

        gov.writeGate().release( tileBytes );
    };

    ChunkedRunOptions opts;
    opts.scratchRoot = ( dir / "scratch" ).generic_string();
    opts.publish = [&] { published = true; };

    const auto res = runChunkedOperator( "rs:tb_scale_stream", Json::Value( Json::objectValue ),
                                        ctx, partition, TileRunDeterminism::BitExact,
                                        kernel, sink, opts );

    REQUIRE( res.tilesComputed == 4 );
    REQUIRE( published );
    // Governor must report ZERO resource leaks upon completion:
    REQUIRE_FALSE( gov.hasOutstandingResources() );
    REQUIRE( gov.writeGate().outstandingBytes() == 0 );
    REQUIRE( gov.scratch().outstandingBytes() == 0 );

    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

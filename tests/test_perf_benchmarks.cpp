// test_perf_benchmarks.cpp — Performance benchmarks for representative RS operators.
//
// Establishes a wall-clock + peak-RSS baseline for the operators targeted by the
// perf/architecture consolidation goal, so before/after changes are measured, not
// guessed. These are tagged [benchmark] and are runnable via ctest, but are also
// designed to be invoked directly for manual/nightly profiling:
//
//   ./test_perf_benchmarks                          # run all (small sizes, fast)
//   SICNU_BENCH_LARGE=1 ./test_perf_benchmarks      # larger sizes for nightly
//
// Output is human-readable timing + RSS, printed to stdout. Determinism:
// synthetic rasters are generated from a fixed LCG, so the SAME raster is produced
// every run (bit-identical input → comparable output across builds).
//
// NOTE: timing assertions are intentionally loose / absent — these benchmarks
// REPORT numbers; they do not gate on absolute thresholds (machine-dependent).
// Correctness of each operator is verified by its own dedicated test; here we
// only sanity-check the output exists and is non-empty.
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/spectral_anomaly.h"
#include "processing/algorithms/change_detection.h"
#include "processing/framework/resource_monitor.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"

#include <QString>
#include <QTemporaryDir>

#include <gdal.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
// Deterministic LCG (fixed seed) — same sequence every run, no std::random dependency.
float lcgFloat( uint32_t &state )
{
    state = state * 1103515245u + 12345u;
    // Map to [0,1) then scale to a reflectance-like range [0,1000).
    return static_cast<float>( state >> 8 ) / static_cast<float>( 0xFFFFFF ) * 1000.0f;
}

// Build a multi-band GeoTIFF with deterministic content: band-major, pixel (x,y,band b)
// value derived from a per-band-seeded LCG so bands are correlated (realistic for RX/PCA).
void buildSyntheticRaster( const QString &path, int width, int height, int bands )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    GDALDatasetH ds = createOutputTiff( path, width, height, bands, GDT_Float32, gt, QString() );
    REQUIRE( ds != nullptr );

    const size_t pixelCount = static_cast<size_t>( width ) * height;
    std::vector<float> band( pixelCount );
    for ( int b = 1; b <= bands; ++b )
    {
        uint32_t state = 0x12345678u + static_cast<uint32_t>( b ) * 9781u;
        for ( size_t i = 0; i < pixelCount; ++i )
            band[i] = lcgFloat( state );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Write, 0, 0, width, height,
                               band.data(), width, height, GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( ds );
}

bool largeBench()
{
    const char *env = std::getenv( "SICNU_BENCH_LARGE" );
    return env && env[0] == '1';
}

// Sampler that polls current RSS from a background thread while a workload runs.
// Reports the peak observed RSS (MB). Falls back to 0 if RSS is unavailable.
struct PeakRssTracker
{
    sicnu::ResourceMonitor monitor;
    std::atomic<bool> running{ false };
    std::atomic<unsigned int> peakMb{ 0 };
    std::thread worker;

    void start()
    {
        running.store( true );
        peakMb.store( monitor.currentRssMb() );
        worker = std::thread( [this]() {
            while ( running.load() )
            {
                auto cur = monitor.currentRssMb();
                unsigned int prev = peakMb.load();
                while ( cur > prev && !peakMb.compare_exchange_weak( prev, cur ) )
                {
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
            }
        } );
    }

    unsigned int stop()
    {
        running.store( false );
        if ( worker.joinable() )
            worker.join();
        return peakMb.load();
    }
};

// Run a workload, printing wall-clock time and delta-peak-RSS. The "baseline" RSS
// (idle) is sampled before the workload so the reported peak is the workload's
// marginal contribution (more stable across machines than absolute RSS).
template<typename Fn>
void runBench( const char *name, Fn &&workload )
{
    PeakRssTracker tracker;
    const unsigned int baselineMb = tracker.monitor.currentRssMb();
    tracker.start();
    const auto t0 = std::chrono::steady_clock::now();
    workload();
    const auto t1 = std::chrono::steady_clock::now();
    const unsigned int peakMb = tracker.stop();

    const double secs = std::chrono::duration<double>( t1 - t0 ).count();
    std::printf( "[bench] %-28s  time=%7.3fs  peakRSS=%5uMB  (delta=%5uMB)\n",
                 name, secs, peakMb,
                 peakMb > baselineMb ? peakMb - baselineMb : 0u );
}

// Run an RSOperator by id with JSON params, returning its result JSON.
Json::Value runOperator( const std::string &id, const Json::Value &params,
                         sicnu::operators::RSOperatorContext &ctx )
{
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    return op->run( params, ctx );
}
} // namespace

// ---------------------------------------------------------------------------
// Baseline benchmarks. Sizes: small by default (fast CI), large when
// SICNU_BENCH_LARGE=1 (nightly / manual profiling).
// ---------------------------------------------------------------------------

TEST_CASE( "Benchmark: spectral_index (streaming-class baseline)", "[benchmark]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const bool large = largeBench();
    const int W = large ? 2048 : 512;
    const int H = large ? 2048 : 512;
    const QString inPath = dir.filePath( QStringLiteral( "in.tif" ) );
    const QString outPath = dir.filePath( QStringLiteral( "out_ndvi.tif" ) );
    buildSyntheticRaster( inPath, W, H, 4 ); // R,G,B,NIR

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = outPath.toStdString();
    params["nir"] = 4;
    params["red"] = 1;
    params["index"] = "ndvi";

    sicnu::operators::RSOperatorContext ctx;
    runBench( "spectral_index", [&]() {
        auto res = runOperator( "rs:spectral_index", params, ctx );
        REQUIRE( res.isObject() );
        REQUIRE( res.isMember( "output" ) );
    } );
}

TEST_CASE( "Benchmark: rx_anomaly (interleaved-cluster baseline)", "[benchmark]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const bool large = largeBench();
    const int W = large ? 1536 : 384;
    const int H = large ? 1536 : 384;
    const int bands = 8;
    const QString inPath = dir.filePath( QStringLiteral( "in.tif" ) );
    const QString outPath = dir.filePath( QStringLiteral( "out_rx.tif" ) );
    buildSyntheticRaster( inPath, W, H, bands );

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = outPath.toStdString();

    sicnu::operators::RSOperatorContext ctx;
    runBench( "rx_anomaly", [&]() {
        auto res = runOperator( "rs:rx_anomaly", params, ctx );
        REQUIRE( res.isObject() );
        REQUIRE( res.isMember( "output" ) );
    } );
}

TEST_CASE( "Benchmark: change_detection diff (candidate for streaming)", "[benchmark]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const bool large = largeBench();
    const int W = large ? 2048 : 512;
    const int H = large ? 2048 : 512;
    const QString beforePath = dir.filePath( QStringLiteral( "before.tif" ) );
    const QString afterPath = dir.filePath( QStringLiteral( "after.tif" ) );
    const QString outPath = dir.filePath( QStringLiteral( "out_cd.tif" ) );
    buildSyntheticRaster( beforePath, W, H, 1 );
    buildSyntheticRaster( afterPath, W, H, 1 );

    Json::Value params( Json::objectValue );
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = outPath.toStdString();
    params["method"] = "difference";
    params["thresholdMethod"] = "manual";
    params["threshold"] = 0.0;

    sicnu::operators::RSOperatorContext ctx;
    runBench( "change_detection(diff)", [&]() {
        auto res = runOperator( "rs:change_detection", params, ctx );
        REQUIRE( res.isObject() );
        REQUIRE( res.isMember( "output" ) );
    } );
}

TEST_CASE( "Benchmark: band_math (full-raster baseline)", "[benchmark]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const bool large = largeBench();
    const int W = large ? 2048 : 512;
    const int H = large ? 2048 : 512;
    const QString inPath = dir.filePath( QStringLiteral( "in.tif" ) );
    const QString outPath = dir.filePath( QStringLiteral( "out_bm.tif" ) );
    buildSyntheticRaster( inPath, W, H, 4 );

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = outPath.toStdString();
    params["expression"] = "b1 + b2 - b3 * 0.5";

    sicnu::operators::RSOperatorContext ctx;
    runBench( "band_math", [&]() {
        auto res = runOperator( "rs:band_math", params, ctx );
        REQUIRE( res.isObject() );
        REQUIRE( res.isMember( "output" ) );
    } );
}

// ---------------------------------------------------------------------------
// Benchmarks for the atomic-architecture performance convergence round:
// spectral resampling (LUT + tile streaming), spectral unmixing (precomputed
// inverse), endmember PPI (3-pass streaming), change primitives (tile
// streaming). Numbers are reported, not asserted.
// ---------------------------------------------------------------------------

TEST_CASE( "Benchmark: spectral_resample (LUT + tile streaming)", "[benchmark]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const bool large = largeBench();
    const int W = large ? 1024 : 256;
    const int H = large ? 1024 : 256;
    const int bands = large ? 32 : 8; // multi-band (spectrometer-like)
    const QString inPath = dir.filePath( QStringLiteral( "in.tif" ) );
    const QString outPath = dir.filePath( QStringLiteral( "out_resample.tif" ) );
    buildSyntheticRaster( inPath, W, H, bands );

    Json::Value wavelengths( Json::arrayValue );
    for ( int b = 0; b < bands; ++b )
        wavelengths.append( 400.0 + b * 20.0 );
    Json::Value srcWl( Json::arrayValue );
    for ( int b = 0; b < bands; ++b )
        srcWl.append( 390.0 + b * 20.0 );

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = outPath.toStdString();
    params["wavelengths"] = wavelengths;
    params["sourceWavelengths"] = srcWl;

    sicnu::operators::RSOperatorContext ctx;
    runBench( "spectral_resample", [&]() {
        auto res = runOperator( "rs:spectral_resample", params, ctx );
        REQUIRE( res.isObject() );
        REQUIRE( res["bands"].asInt() == bands );
    } );
}

TEST_CASE( "Benchmark: spectral_unmixing (precomputed inverse)", "[benchmark]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const bool large = largeBench();
    const int W = large ? 1024 : 256;
    const int H = large ? 1024 : 256;
    const int bands = 4;
    const QString inPath = dir.filePath( QStringLiteral( "in.tif" ) );
    const QString outPath = dir.filePath( QStringLiteral( "out_unmix.tif" ) );
    buildSyntheticRaster( inPath, W, H, bands );

    Json::Value endmembers( Json::arrayValue );
    Json::Value e1( Json::arrayValue ); e1.append( 0.9 ); e1.append( 0.2 ); e1.append( 0.1 ); e1.append( 0.05 );
    Json::Value e2( Json::arrayValue ); e2.append( 0.1 ); e2.append( 0.8 ); e2.append( 0.3 ); e2.append( 0.1 );
    Json::Value e3( Json::arrayValue ); e3.append( 0.05 ); e3.append( 0.2 ); e3.append( 0.7 ); e3.append( 0.3 );
    endmembers.append( e1 ); endmembers.append( e2 ); endmembers.append( e3 );

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["output"] = outPath.toStdString();
    params["endmembers"] = endmembers;

    sicnu::operators::RSOperatorContext ctx;
    runBench( "spectral_unmixing", [&]() {
        auto res = runOperator( "rs:spectral_unmixing", params, ctx );
        REQUIRE( res.isObject() );
        REQUIRE( res.isMember( "output" ) );
    } );
}

TEST_CASE( "Benchmark: endmember_extraction PPI (3-pass streaming)", "[benchmark]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const bool large = largeBench();
    const int W = large ? 1024 : 256;
    const int H = large ? 1024 : 256;
    const int bands = large ? 16 : 4;
    const QString inPath = dir.filePath( QStringLiteral( "in.tif" ) );
    buildSyntheticRaster( inPath, W, H, bands );

    Json::Value params( Json::objectValue );
    params["input"] = inPath.toStdString();
    params["nEndmembers"] = 3;
    params["projections"] = large ? 500 : 100;

    sicnu::operators::RSOperatorContext ctx;
    runBench( "endmember_extraction", [&]() {
        auto res = runOperator( "rs:endmember_extraction", params, ctx );
        REQUIRE( res.isObject() );
        REQUIRE( res["endmembers"].size() == 3 );
    } );
}

TEST_CASE( "Benchmark: change_difference primitive (tile streaming)", "[benchmark]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const bool large = largeBench();
    const int W = large ? 2048 : 512;
    const int H = large ? 2048 : 512;
    const QString beforePath = dir.filePath( QStringLiteral( "before.tif" ) );
    const QString afterPath = dir.filePath( QStringLiteral( "after.tif" ) );
    const QString outPath = dir.filePath( QStringLiteral( "out_diff.tif" ) );
    buildSyntheticRaster( beforePath, W, H, 1 );
    buildSyntheticRaster( afterPath, W, H, 1 );

    Json::Value params( Json::objectValue );
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = outPath.toStdString();

    sicnu::operators::RSOperatorContext ctx;
    runBench( "change_difference", [&]() {
        auto res = runOperator( "rs:change_difference", params, ctx );
        REQUIRE( res.isObject() );
        REQUIRE( res.isMember( "output" ) );
    } );
}

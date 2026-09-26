// test_perf_operator_observatory.cpp — operator-path wing of the ds41
// performance & memory observatory (same harness: tests/perf/perf_observatory.h).
//
// Two kinds of cases live here:
//
//   1. "obs op <workload>" baseline records (WP-E): the execution-plane
//      benchmark catalog (test_execution_benchmarks) measured through the
//      observatory harness, so the committed obs_*.json family also covers the
//      real operator hot paths (spectral index, qa_mask, recode, majority
//      filter, temporal composite, change detection). Same rules as the rest
//      of the observatory: records are written to SICNU_OBS_OUT (never the
//      repo), gates are structural, numbers are evidence not budgets.
//
//   2. "memory guard <workload>" cases (WP-D): executable guards for the
//      documented O(tile) promises. Each guard pins GDAL's block cache to a
//      known constant, feeds a fixed-seed fixed-size input to the REAL
//      operator invoked directly in-process (no TaskCenter/JobEngine — the
//      measured working set is the operator's own), and asserts — via the
//      harness's external PeakRssTracker, never via self-reported numbers —
//      that the process peak-RSS delta stays inside the declared bound. The
//      bound arithmetic lives in each guard's comment next to the promise it
//      cites:
//        * task_resource_budget.cpp — Streaming = 64 MiB default estimate
//        * docs/USER_GUIDE.md:1227  — temporal operators: T×tile×4B ≤ 256 MiB
//        * docs/adr/0073-large-raster-memory-policy-classification.md —
//          Streaming = O(tile) out-of-core
//      A full-raster materialization of the guard inputs would blow every
//      bound below, so the guards discriminate O(tile) from O(raster).
//
// All fixtures are deterministic LCG rasters in a private temp directory; the
// GDAL block-cache pin is local to the guard that needs it.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "processing/framework/task_center.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"
#include "jobs/job_engine.h"
#include "data/data_manager.h"
#include "data/data_asset.h"
#include "data/execution_fingerprint.h"
#include "data/internal/source_provider_registry.h"
#include "data/internal/source_provider.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include "perf/perf_observatory.h"

#include <cpl_conv.h>
#include <gdal.h>
#include <gdal_priv.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QVariantMap>

#include <json/json.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using sicnu::testing::perf::Ladder;
using sicnu::testing::perf::Lcg;
using sicnu::testing::perf::Sample;

namespace
{
// Full QGIS/TaskCenter teardown at process exit is a destruction-order
// minefield on this link set (same atexit segfault class test_layout_tools
// documents): report the results, then leave via _Exit before any static
// destructor runs.
class FastExitListener : public Catch::EventListenerBase
{
  public:
    using Catch::EventListenerBase::EventListenerBase;
    void testRunEnded( const Catch::TestRunStats &stats ) override
    {
        std::_Exit( stats.aborting || stats.totals.testCases.failed > 0 ? 1 : 0 );
    }
};
CATCH_REGISTER_LISTENER( FastExitListener )
} // namespace

namespace
{

//------------------------------------------------------------------------------
// Fixture machinery — mirrors test_execution_benchmarks (real operators via
// TaskCenter) with the observatory's deterministic-raster discipline.
//------------------------------------------------------------------------------

float lcgFloat( uint32_t &state )
{
    state = state * 1103515245u + 12345u;
    return static_cast<float>( state >> 8 ) / static_cast<float>( 0xFFFFFFu ) * 1000.0f;
}

void writeFloatTiff( const QString &path, int width, int height, int bands, uint32_t seed )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 120.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
    GDALDatasetH ds = createOutputTiff( path, width, height, bands, GDT_Float32, gt,
                                        QStringLiteral( "EPSG:4326" ) );
    REQUIRE( ds != nullptr );
    const size_t pixels = static_cast<size_t>( width ) * height;
    std::vector<float> buf( pixels );
    for ( int b = 1; b <= bands; ++b )
    {
        uint32_t st = seed + static_cast<uint32_t>( b ) * 9781u;
        for ( size_t i = 0; i < pixels; ++i )
            buf[i] = lcgFloat( st );
        REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Write, 0, 0, width, height,
                               buf.data(), width, height, GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( ds );
}

void writeU16Tiff( const QString &path, int width, int height, uint32_t seed, int kind )
{
    // kind 0: QA bit patterns; kind 1: class labels 1..7
    ensureGdalInit();
    std::array<double, 6> gt = { 120.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
    GDALDatasetH ds = createOutputTiff( path, width, height, 1, GDT_UInt16, gt,
                                        QStringLiteral( "EPSG:4326" ) );
    REQUIRE( ds != nullptr );
    const size_t pixels = static_cast<size_t>( width ) * height;
    std::vector<uint16_t> buf( pixels );
    uint32_t st = seed;
    for ( size_t i = 0; i < pixels; ++i )
    {
        st = st * 1103515245u + 12345u;
        const int v = static_cast<int>( ( st >> 8 ) % 256u );
        buf[i] = kind == 0 ? static_cast<uint16_t>( v )
                           : static_cast<uint16_t>( 1 + v % 7 );
    }
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, width, height,
                           buf.data(), width, height, GDT_UInt16, 0, 0 ) == CE_None );
    GDALClose( ds );
}

void record( const std::string &name, Sample &&sample, Json::Value structural,
             const Json::Value &complexity = Json::Value() )
{
    sicnu::testing::perf::writeRecord( sicnu::testing::perf::outputDir(), name, sample,
                                        complexity, structural );
}

Json::Value scaleBlock( std::size_t items, std::size_t probes )
{
    Json::Value o( Json::objectValue );
    o["kind"] = sicnu::testing::perf::scaleName( sicnu::testing::perf::scaleFromEnv() );
    o["items"] = static_cast<Json::UInt64>( items );
    o["probes"] = static_cast<Json::UInt64>( probes );
    return o;
}

struct OpObsFixture
{
    QTemporaryDir dir;
    sicnu::data::DataManager dataManager;
    int width = 1024;
    int height = 1024;

    OpObsFixture()
    {
        if ( sicnu::testing::perf::scaleFromEnv() != sicnu::testing::perf::Scale::Small )
        {
            width = 2048;
            height = 2048;
        }
        // Same argc/argv discipline as test_execution_benchmarks: the app must
        // outlive this frame, worker threads read applicationFilePath().
        static int argc = 1;
        static char arg0[] = "test_perf_operator_observatory";
        static char *argv[] = { arg0, nullptr };
        if ( !QCoreApplication::instance() )
            new QCoreApplication( argc, argv );

        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );

        sicnu::TaskCenter::instance().shutdownForTests();
        sicnu::TaskCenter::instance().setCatalog( &dataManager );

        sicnu::operators::RSOperatorRegistry::instance(); // call_once chain
        sicnu::operators::rs::installRsOperatorProvider();
        sicnu::processing::AtomicAlgorithmRegistry::instance().initialize();

        auto &cache = sicnu::data::ExecutionResultCache::instance();
        cache.clear();
        cache.setEnabled( true );
    }

    QString path( const QString &name ) const { return dir.filePath( name ); }
};

sicnu::data::AssetId registerRaster( sicnu::data::DataManager &dm, const QString &path )
{
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = path;
    sicnu::data::RegisterRequest request;
    request.source = source;
    request.persistence = sicnu::data::PersistencePolicy::TaskTemporary;
    request.notifyUpdateOnReuse = true;
    return dm.registerSource( request ).assetId;
}

Json::Value runOperatorTask( const std::string &operatorId, const QVariantMap &params )
{
    auto &center = sicnu::TaskCenter::instance();
    const long taskId = center.enqueueTask( QString::fromStdString( operatorId ), params,
                                            /*autoLoad=*/false, sicnu::TaskPriority::Normal,
                                            {}, /*autoDispatch=*/true );
    REQUIRE( taskId > 0 );
    const auto info = center.waitForTask( taskId, std::chrono::minutes( 20 ) );
    INFO( "task error: " << info.errorMessage.toStdString() );
    INFO( "last log: "
          << ( info.logBuffer.isEmpty() ? QString() : info.logBuffer.last() ).toStdString() );
    REQUIRE( info.status == sicnu::TaskStatus::Completed );
    return info.resultPayload;
}

/// Direct in-process operator invocation for the memory guards: no TaskCenter,
/// no JobEngine worker pool — the working set measured is the operator's own.
Json::Value runOperatorDirect( const std::string &operatorId, const Json::Value &params,
                               sicnu::operators::RSOperatorContext &ctx )
{
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( operatorId );
    REQUIRE( op != nullptr );
    return op->run( params, ctx );
}

/// RAII pin of GDAL's block cache so RSS guards measure the implementation's
/// own working set instead of GDAL's default cache (5% of RAM ≈ GBs here,
/// which would swallow a full-raster materialization and blank the guard).
struct PinnedGdalCache
{
    explicit PinnedGdalCache( const char *size )
    {
        const char *prior = CPLGetConfigOption( "GDAL_CACHEMAX", nullptr );
        m_hadPrior = prior != nullptr;
        if ( m_hadPrior )
            m_prior = prior;
        CPLSetConfigOption( "GDAL_CACHEMAX", size );
    }
    ~PinnedGdalCache()
    {
        CPLSetConfigOption( "GDAL_CACHEMAX", m_hadPrior ? m_prior.c_str() : nullptr );
    }
    PinnedGdalCache( const PinnedGdalCache & ) = delete;
    PinnedGdalCache &operator=( const PinnedGdalCache & ) = delete;

    bool m_hadPrior = false;
    std::string m_prior;
};

/// Semantic oracle shared by the operator baselines: the output raster exists
/// with the requested geometry (proves the kernel really ran end-to-end).
void requireOutputGeometry( const QString &path, int width, int height )
{
    ensureGdalInit();
    GdalDatasetWrapper out;
    REQUIRE( out.open( path ) );
    REQUIRE( out.isValid() );
    CHECK( out.width() == width );
    CHECK( out.height() == height );
}

} // namespace

//------------------------------------------------------------------------------
// WP-E: operator-path baseline records (obs op *)
//
// Same contract as the rest of the observatory: the record is the evidence,
// the CHECKs are structural (geometry/counts), numbers are recorded not gated.
// Rungs follow the shared Ladder so records state their scale; ctest runs the
// small rung. Workloads mirror the execution-bench catalog — these are
// existing hot paths, measured, not new scenarios.
//------------------------------------------------------------------------------

TEST_CASE( "obs op spectral index streaming stays tile-bounded",
           "[perf_observatory][operator][spectral]" )
{
    OpObsFixture fx;
    const int side = fx.width;
    const QString refl = fx.path( "obs_refl.tif" );
    const QString out = fx.path( "obs_ndvi_out.tif" );
    writeFloatTiff( refl, side, side, 4, 0x12345678u );
    registerRaster( fx.dataManager, refl );

    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        QVariantMap params;
        params.insert( "input", refl );
        params.insert( "output", out );
        params.insert( "index", "NDVI" );
        params.insert( "nir", 1 );
        params.insert( "red", 2 );
        const Json::Value payload = runOperatorTask( "rs:spectral_index", params );
        REQUIRE( payload.isMember( "output" ) );
        sample.counts.filesWritten = 1;
        sample.counts.rowsMaterialized = side;
    } );

    CHECK( QFileInfo::exists( out ) );
    requireOutputGeometry( out, side, side );
    Json::Value structural( Json::objectValue );
    structural["tile_pixels"] = 256 * 256;
    structural["memory_unit"] = "tile";
    s.scale = scaleBlock( static_cast<std::size_t>( side ) * side, 1 );
    record( "obs_op_spectral_index_stream", std::move( s ), structural );
}

TEST_CASE( "obs op qa mask stream stays tile-bounded",
           "[perf_observatory][operator][qa_mask]" )
{
    OpObsFixture fx;
    const int side = fx.width;
    const QString qa = fx.path( "obs_qa.tif" );
    const QString out = fx.path( "obs_qa_mask_out.tif" );
    writeU16Tiff( qa, side, side, 0x55555555u, 0 );
    registerRaster( fx.dataManager, qa );

    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        QVariantMap params;
        params.insert( "input", qa );
        params.insert( "output", out );
        params.insert( "source", "generic_bitmask" );
        params.insert( "bits", 8 );
        params.insert( "qa_band", 1 );
        const Json::Value payload = runOperatorTask( "rs:qa_mask", params );
        REQUIRE( payload.isMember( "output" ) );
        sample.counts.filesWritten = 1;
        sample.counts.rowsMaterialized = side;
    } );

    CHECK( QFileInfo::exists( out ) );
    requireOutputGeometry( out, side, side );
    Json::Value structural( Json::objectValue );
    structural["tile_pixels"] = 256 * 256;
    structural["memory_unit"] = "tile";
    s.scale = scaleBlock( static_cast<std::size_t>( side ) * side, 1 );
    record( "obs_op_qa_mask_stream", std::move( s ), structural );
}

TEST_CASE( "obs op recode stream stays tile-bounded",
           "[perf_observatory][operator][recode]" )
{
    OpObsFixture fx;
    const int side = fx.width;
    const QString labels = fx.path( "obs_labels.tif" );
    const QString out = fx.path( "obs_recode_out.tif" );
    writeU16Tiff( labels, side, side, 0x22222222u, 1 );
    registerRaster( fx.dataManager, labels );

    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        QVariantMap params;
        params.insert( "input", labels );
        params.insert( "output", out );
        params.insert( "recode_map", QString( "{\"1\":2,\"2\":3,\"3\":1,\"4\":4,\"5\":1}" ) );
        const Json::Value payload = runOperatorTask( "rs:recode", params );
        REQUIRE( payload.isMember( "output" ) );
        sample.counts.filesWritten = 1;
        sample.counts.rowsMaterialized = side;
    } );

    CHECK( QFileInfo::exists( out ) );
    requireOutputGeometry( out, side, side );
    Json::Value structural( Json::objectValue );
    structural["tile_pixels"] = 256 * 256;
    structural["memory_unit"] = "tile";
    s.scale = scaleBlock( static_cast<std::size_t>( side ) * side, 1 );
    record( "obs_op_recode_stream", std::move( s ), structural );
}

TEST_CASE( "obs op majority filter window stays tile-bounded",
           "[perf_observatory][operator][majority_filter]" )
{
    OpObsFixture fx;
    const int side = fx.width;
    const QString labels = fx.path( "obs_majority_labels.tif" );
    const QString out = fx.path( "obs_majority_out.tif" );
    writeU16Tiff( labels, side, side, 0x22222222u, 1 );
    registerRaster( fx.dataManager, labels );

    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        QVariantMap params;
        params.insert( "input", labels );
        params.insert( "output", out );
        params.insert( "kernel", 3 );
        const Json::Value payload = runOperatorTask( "rs:majority_filter", params );
        REQUIRE( payload.isMember( "output" ) );
        sample.counts.filesWritten = 1;
        sample.counts.rowsMaterialized = side;
    } );

    CHECK( QFileInfo::exists( out ) );
    requireOutputGeometry( out, side, side );
    Json::Value structural( Json::objectValue );
    structural["tile_pixels"] = 256 * 256;
    structural["halo_tiles"] = true;
    structural["memory_unit"] = "tile";
    s.scale = scaleBlock( static_cast<std::size_t>( side ) * side, 1 );
    record( "obs_op_majority_filter_window", std::move( s ), structural );
}

TEST_CASE( "obs op temporal composite stays tile-bounded across scenes",
           "[perf_observatory][operator][temporal]" )
{
    OpObsFixture fx;
    // Sentinel-2-style filename fragments give each scene its acquisition
    // instant via the filename time source (same convention as ebench).
    constexpr int kScenes = 6;
    const int side = fx.width;
    QVariantList scenes;
    for ( int i = 0; i < kScenes; ++i )
    {
        const QString scene =
            fx.path( QStringLiteral( "obs_scene_2024010%1T100000.tif" ).arg( i + 1 ) );
        writeFloatTiff( scene, side, side, 1, 0x0BBC0DE0u + static_cast<uint32_t>( i ) * 31u );
        registerRaster( fx.dataManager, scene );
        scenes.append( scene );
    }
    const QString out = fx.path( "obs_composite_out.tif" );

    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        QVariantMap params;
        params.insert( "scenes", scenes );
        params.insert( "method", "mean" );
        params.insert( "output", out );
        const Json::Value payload = runOperatorTask( "rs:temporal_composite", params );
        REQUIRE( payload.isMember( "output" ) );
        sample.counts.filesWritten = 1;
        sample.counts.rowsMaterialized = side;
        sample.extra["scenes"] = kScenes;
    } );

    CHECK( QFileInfo::exists( out ) );
    requireOutputGeometry( out, side, side );
    Json::Value structural( Json::objectValue );
    structural["tile_pixels"] = 256 * 256;
    structural["scenes"] = kScenes;
    structural["memory_unit"] = "tile";
    s.scale = scaleBlock( static_cast<std::size_t>( side ) * side, kScenes );
    record( "obs_op_temporal_composite", std::move( s ), structural );
}

TEST_CASE( "obs op change detection difference stays tile-bounded",
           "[perf_observatory][operator][change]" )
{
    OpObsFixture fx;
    const int side = fx.width;
    const QString before = fx.path( "obs_change_before.tif" );
    const QString after = fx.path( "obs_change_after.tif" );
    const QString out = fx.path( "obs_change_out.tif" );
    writeFloatTiff( before, side, side, 1, 0xC0FFEE00u );
    writeFloatTiff( after, side, side, 1, 0xF00DBABEu );
    registerRaster( fx.dataManager, before );
    registerRaster( fx.dataManager, after );

    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        QVariantMap params;
        params.insert( "before", before );
        params.insert( "after", after );
        params.insert( "output", out );
        params.insert( "method", "difference" );
        const Json::Value payload = runOperatorTask( "rs:change_detection", params );
        REQUIRE( payload.isMember( "output" ) );
        sample.counts.filesWritten = 1;
        sample.counts.rowsMaterialized = side;
    } );

    CHECK( QFileInfo::exists( out ) );
    requireOutputGeometry( out, side, side );
    Json::Value structural( Json::objectValue );
    structural["tile_pixels"] = 256 * 256;
    structural["memory_unit"] = "tile";
    s.scale = scaleBlock( static_cast<std::size_t>( side ) * side, 2 );
    record( "obs_op_change_detection", std::move( s ), structural );
}

//------------------------------------------------------------------------------
// WP-D: O(tile) memory guards ("memory guard *" cases).
//
// Measurement is external (PeakRssTracker samples the process), the cache pin
// removes GDAL's block cache as a confounder, and the bound arithmetic cites
// the promise. Super-linear blowups fail the guard; sub-bound results record
// the measured numbers into the observatory family as evidence.
//------------------------------------------------------------------------------

TEST_CASE( "memory guard tiled inference peak RSS stays within declared O(tile) bound",
           "[memory][guard][inference]" )
{
    // Promise: docs/adr/0073 (Streaming = O(tile) out-of-core) and
    // src/processing/framework/task_resource_budget.cpp — Streaming default
    // estimate 64 MiB. Input: 4096×4096 Float32 (64 MiB on disk) streamed at
    // the default 256×256 tile; the kernel keeps two tile buffers
    // (2×256²×4 B = 0.5 MiB, cf. obs_tiled_inference). GDAL block cache is
    // pinned to 64 MiB so it is a constant, not a confounder.
    // Bound = pinned cache 64 + working set 0.5 + interpreter/GDAL driver
    // margin 32 ≈ 96 MiB. Materializing just the input raster would add its
    // full 64 MiB on top of the pinned cache and break the bound.
    PinnedGdalCache pin( "64MB" );

    // Guards run at one fixed discriminating size, independent of
    // SICNU_OBS_SCALE: at the small rung the working set is under the RSS
    // sampler's 1 MiB / 2 ms resolution and the bound would pass vacuously.
    const int side = 4096;
    const int tile = 256;
    const double boundMb = 96.0;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString inPath = dir.filePath( QStringLiteral( "guard_infer_input.tif" ) );
    const QString outPath = dir.filePath( QStringLiteral( "guard_infer_output.tif" ) );
    writeFloatTiff( inPath, side, side, 1, 0x5EEDF00Du );

    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        GdalDatasetWrapper input;
        REQUIRE( input.open( inPath ) );
        REQUIRE( input.isValid() );
        std::array<double, 6> gt = { 120.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
        GdalStreamingOutput output( outPath, side, side, 1, GDT_Float32, gt,
                                    QStringLiteral( "EPSG:4326" ) );
        REQUIRE( output.isOpen() );

        GdalBlockStream stream( input, 1, tile, tile, /*halo=*/0 );
        long long tiles = 0;
        const bool ok = stream.forEach( [&]( const GdalBlockStream::Tile &t,
                                             const float *pixels ) {
            static thread_local std::vector<float> scratch;
            scratch.resize( static_cast<size_t>( t.width ) * t.height );
            double sum = 0.0;
            for ( size_t i = 0; i < scratch.size(); ++i )
            {
                scratch[i] = pixels[i] * 2.0f;
                sum += scratch[i];
            }
            CHECK( output.writeTile( 1, t, scratch.data() ) );
            sample.extra["checksum"] = sum; // keeps the kernel from folding away
            ++tiles;
            return true;
        } );
        REQUIRE( ok );
        output.close();
        sample.counts.filesWritten = 1;
        sample.extra["tiles"] = static_cast<Json::Int64>( tiles );
    } );

    const long long expectedTiles =
        ( ( side + tile - 1 ) / tile ) * ( ( side + tile - 1 ) / tile );
    CHECK( s.extra["tiles"].asInt64() == expectedTiles );
    CHECK( QFileInfo::exists( outPath ) );

    Json::Value structural( Json::objectValue );
    structural["bound_mb"] = boundMb;
    structural["gdal_cache_pinned_mb"] = 64;
    structural["tile_pixels"] = tile * tile;
    structural["promise"] = "ADR 0073 Streaming O(tile); task_resource_budget.cpp Streaming=64MiB";
    s.scale = scaleBlock( static_cast<std::size_t>( side ) * side, 1 );
    record( "guard_tiled_inference_rss", std::move( s ), structural );

    const double deltaMb = static_cast<double>( s.peakRssDeltaMb() );
    INFO( "peak RSS delta: " << deltaMb << " MB (bound " << boundMb << " MB)" );
    CHECK( deltaMb <= boundMb );
}

TEST_CASE( "memory guard temporal composite peak RSS stays within documented 256 MiB window",
           "[memory][guard][temporal]" )
{
    // Promise: docs/USER_GUIDE.md:1227 — the six temporal operators are
    // "受 O(tile) 有界内存约束（默认 256×256），中位/分位数按 T×tile×4B ≤ 256 MiB
    // 自动收缩瓦块" — the multi-scene working window must never exceed
    // 256 MiB regardless of scene count. Input: 6 scenes of 4096×4096 Float32
    // (6×64 MiB = 384 MiB total — a full-cube materialization would break any
    // sane bound). GDAL cache pinned at 64 MiB.
    // Bound = documented window 256 + pinned cache 64 + output/margin 32
    // = 352 MiB; the O(tile×T) window the docs promise is far below it, while
    // full-cube materialization (384+ MiB) is above it.
    PinnedGdalCache pin( "64MB" );

    constexpr int kScenes = 6;
    // Guards run at one fixed discriminating size, independent of
    // SICNU_OBS_SCALE: at the small rung the working set is under the RSS
    // sampler's 1 MiB / 2 ms resolution and the bound would pass vacuously.
    const int side = 4096;
    const double boundMb = 352.0;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QVariantList scenes;
    for ( int i = 0; i < kScenes; ++i )
    {
        const QString scene =
            dir.filePath( QStringLiteral( "guard_scene_2024010%1T100000.tif" ).arg( i + 1 ) );
        writeFloatTiff( scene, side, side, 1, 0x0BBC0DE0u + static_cast<uint32_t>( i ) * 31u );
        scenes.append( scene );
    }
    const QString out = dir.filePath( QStringLiteral( "guard_composite_out.tif" ) );

    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        Json::Value params( Json::objectValue );
        for ( const QVariant &scene : scenes )
            params["scenes"].append( scene.toString().toStdString() );
        params["method"] = "mean";
        params["output"] = out.toStdString();
        sicnu::operators::RSOperatorContext ctx;
        const Json::Value payload = runOperatorDirect( "rs:temporal_composite", params, ctx );
        REQUIRE( payload.isMember( "output" ) );
        sample.counts.filesWritten = 1;
        sample.extra["scenes"] = kScenes;
    } );

    CHECK( QFileInfo::exists( out ) );
    requireOutputGeometry( out, side, side );

    Json::Value structural( Json::objectValue );
    structural["bound_mb"] = boundMb;
    structural["gdal_cache_pinned_mb"] = 64;
    structural["scenes"] = kScenes;
    structural["tile_pixels"] = 256 * 256;
    structural["promise"] = "docs/USER_GUIDE.md:1227 T×tile×4B ≤ 256MiB shrink rule";
    s.scale = scaleBlock( static_cast<std::size_t>( side ) * side, kScenes );
    record( "guard_temporal_composite_rss", std::move( s ), structural );

    const double deltaMb = static_cast<double>( s.peakRssDeltaMb() );
    INFO( "peak RSS delta: " << deltaMb << " MB (bound " << boundMb << " MB)" );
    CHECK( deltaMb <= boundMb );
}

TEST_CASE( "memory guard change detection peak RSS stays within declared O(tile) bound",
           "[memory][guard][change]" )
{
    // Promise: docs/adr/0089-post-classification-change.md:23 (block-wise
    // passes, O(tile)) and rs_change_primitives.cpp:233 (256×256 tile in/out
    // buffers, O(tile) independent of raster size). Input: two 4096×4096
    // Float32 rasters (2×64 MiB) — materializing the pair adds 128 MiB.
    // GDAL cache pinned at 64 MiB.
    // Bound = pinned cache 64 + two input tiles + output tile ≈ 1 MiB + margin
    // 32 ≈ 97 MiB; a pair materialization lands ≥ 192 MiB and fails.
    PinnedGdalCache pin( "64MB" );

    // Guards run at one fixed discriminating size, independent of
    // SICNU_OBS_SCALE: at the small rung the working set is under the RSS
    // sampler's 1 MiB / 2 ms resolution and the bound would pass vacuously.
    const int side = 4096;
    const double boundMb = 97.0;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString before = dir.filePath( QStringLiteral( "guard_change_before.tif" ) );
    const QString after = dir.filePath( QStringLiteral( "guard_change_after.tif" ) );
    const QString out = dir.filePath( QStringLiteral( "guard_change_out.tif" ) );
    writeFloatTiff( before, side, side, 1, 0xC0FFEE00u );
    writeFloatTiff( after, side, side, 1, 0xF00DBABEu );

    Sample s = sicnu::testing::perf::measure( [&]( Sample &sample ) {
        Json::Value params( Json::objectValue );
        params["before"] = before.toStdString();
        params["after"] = after.toStdString();
        params["output"] = out.toStdString();
        params["method"] = "difference";
        sicnu::operators::RSOperatorContext ctx;
        const Json::Value payload = runOperatorDirect( "rs:change_detection", params, ctx );
        REQUIRE( payload.isMember( "output" ) );
        sample.counts.filesWritten = 1;
    } );

    CHECK( QFileInfo::exists( out ) );
    requireOutputGeometry( out, side, side );

    Json::Value structural( Json::objectValue );
    structural["bound_mb"] = boundMb;
    structural["gdal_cache_pinned_mb"] = 64;
    structural["tile_pixels"] = 256 * 256;
    structural["promise"] = "ADR 0089 block-wise O(tile); rs_change_primitives 256² double buffer";
    s.scale = scaleBlock( static_cast<std::size_t>( side ) * side, 2 );
    record( "guard_change_detection_rss", std::move( s ), structural );

    const double deltaMb = static_cast<double>( s.peakRssDeltaMb() );
    INFO( "peak RSS delta: " << deltaMb << " MB (bound " << boundMb << " MB)" );
    CHECK( deltaMb <= boundMb );
}

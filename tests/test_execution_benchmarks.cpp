// test_execution_benchmarks.cpp — Execution & Data Plane 3.0 benchmark harness.
//
// In-process, deterministic, machine-readable counterpart to
// test_perf_benchmarks (operator kernels) and scripts/benchmark_harness.py
// (MCP end-to-end). This harness exercises the EXECUTION PLANE itself:
// TaskCenter admission, JobEngine dispatch, real RSOperators, the execution
// cache, remote /vsicurl/ reads, and DataManager queries — and emits one JSON
// record per workload:
//
//   { hardware, workload, wall_ms, cpu_ms, peak_rss_mb, read_bytes,
//     write_bytes, cache }
//
// Set SICNU_EXEC_BENCH_OUT to a DIRECTORY to collect per-workload JSON files;
// scripts/perf_report.py merges and diffs two collections. Without the env
// var the harness only prints [ebench] lines.
//
// Determinism: synthetic rasters come from a fixed LCG; no wall-clock seeds.
// Benchmarks REPORT, they never gate on absolute thresholds — before/after
// comparisons on the same machine are the contract.
//
// Sizes: default small (ctest friendly, seconds). SICNU_BENCH_LARGE=1 selects
// the larger nightly sizes.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/task_center.h"
#include "processing/framework/resource_monitor.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "jobs/job_engine.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "data/data_manager.h"
#include "data/data_asset.h"
#include "data/derivation_record.h"
#include "data/execution_fingerprint.h"
#include "data/internal/source_provider_registry.h"
#include "data/internal/source_provider.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "workflow/workflow_definition.h"
#include "support/mini_cog_server.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace
{
//------------------------------------------------------------------------------
// Deterministic fixtures
//------------------------------------------------------------------------------

float lcgFloat( uint32_t &state )
{
    state = state * 1103515245u + 12345u;
    return static_cast<float>( state >> 8 ) / static_cast<float>( 0xFFFFFFu ) * 1000.0f;
}

uint16_t lcgPattern( uint32_t &state, const uint16_t *patterns, int count )
{
    state = state * 1103515245u + 12345u;
    return patterns[( state >> 8 ) % static_cast<uint32_t>( count )];
}

void writeFloatTiff( const QString &path, int width, int height, int bands,
                     double originX, double originY, uint32_t seed )
{
    ensureGdalInit();
    std::array<double, 6> gt = { originX, 1.0, 0.0, originY, 0.0, -1.0 };
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
    // kind 0: QA bit patterns; kind 1: class labels
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    GDALDatasetH ds = createOutputTiff( path, width, height, 1, GDT_UInt16, gt,
                                        QStringLiteral( "EPSG:4326" ) );
    REQUIRE( ds != nullptr );
    static const uint16_t qaPatterns[] = { 0, 0, 0, 1, 2, 4, 8, 10 };
    static const int qaPatternCount = 8;
    const size_t pixels = static_cast<size_t>( width ) * height;
    std::vector<uint16_t> buf( pixels );
    for ( size_t i = 0; i < pixels; ++i )
    {
        if ( kind == 0 )
        {
            buf[i] = lcgPattern( seed, qaPatterns, qaPatternCount );
        }
        else
        {
            seed = seed * 1103515245u + 12345u;
            buf[i] = static_cast<uint16_t>( 1 + ( seed >> 8 ) % 5 );
        }
    }
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, width, height,
                           buf.data(), width, height, GDT_UInt16, 0, 0 ) == CE_None );
    GDALClose( ds );
}

bool largeBench()
{
    const char *env = std::getenv( "SICNU_BENCH_LARGE" );
    return env && env[0] == '1';
}

//------------------------------------------------------------------------------
// Metrics
//------------------------------------------------------------------------------

qint64 procIoCounter( const char *key )
{
    std::ifstream f( "/proc/self/io" );
    std::string line;
    while ( std::getline( f, line ) )
    {
        if ( line.compare( 0, std::strlen( key ), key ) == 0 )
            return std::atoll( line.c_str() + std::strlen( key ) + 1 );
    }
    return 0;
}

unsigned currentRssMb()
{
    static sicnu::ResourceMonitor monitor;
    return monitor.currentRssMb();
}

struct BenchSample
{
    double wallMs = 0;
    double cpuMs = 0;
    unsigned peakRssDeltaMb = 0;
    qint64 readBytes = 0;
    qint64 writeBytes = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
    Json::Value extra;
};

class RssPeakTracker
{
  public:
    void start()
    {
        m_running = true;
        m_peak = currentRssMb();
        m_thread = std::thread( [this] {
            while ( m_running )
            {
                const unsigned cur = currentRssMb();
                unsigned prev = m_peak.load();
                while ( cur > prev && !m_peak.compare_exchange_weak( prev, cur ) ) {}
                std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
            }
        } );
    }
    unsigned stop()
    {
        m_running = false;
        if ( m_thread.joinable() )
            m_thread.join();
        return m_peak.load();
    }
    ~RssPeakTracker() { stop(); }

  private:
    std::atomic<bool> m_running{ false };
    std::atomic<unsigned> m_peak{ 0 };
    std::thread m_thread;
};

void writeBenchJson( const std::string &workload, const BenchSample &s )
{
    std::printf( "[ebench] %-32s wall=%9.1fms cpu=%9.1fms peakRSSd=%5uMB r=%10lldB w=%10lldB cache=%d/%d\n",
                 workload.c_str(), s.wallMs, s.cpuMs, s.peakRssDeltaMb,
                 static_cast<long long>( s.readBytes ), static_cast<long long>( s.writeBytes ),
                 s.cacheHits, s.cacheMisses );
    std::fflush( stdout );
    const char *outDir = std::getenv( "SICNU_EXEC_BENCH_OUT" );
    if ( !outDir || !outDir[0] )
        return;
    QDir().mkpath( QString::fromUtf8( outDir ) );

    Json::Value hw;
    hw[ "cores" ] = static_cast<Json::Int>( std::thread::hardware_concurrency() );
    std::ifstream cpuinfo( "/proc/cpuinfo" );
    std::string line, model = "unknown";
    while ( std::getline( cpuinfo, line ) )
        if ( line.compare( 0, 10, "model name" ) == 0 )
        {
            model = line.substr( line.find( ':' ) + 2 );
            break;
        }
    hw[ "cpu_model" ] = model;
    hw[ "build_type" ] = "Release";

    Json::Value rec;
    rec[ "schema" ] = "execution-bench/1";
    rec[ "generated_at" ] = QDateTime::currentDateTimeUtc().toString( Qt::ISODate ).toStdString();
    rec[ "hardware" ] = hw;
    rec[ "workload" ] = workload;
    rec[ "wall_ms" ] = s.wallMs;
    rec[ "cpu_ms" ] = s.cpuMs;
    rec[ "peak_rss_mb" ] = s.peakRssDeltaMb;
    rec[ "read_bytes" ] = static_cast<Json::Int64>( s.readBytes );
    rec[ "write_bytes" ] = static_cast<Json::Int64>( s.writeBytes );
    Json::Value cache;
    cache[ "hits" ] = s.cacheHits;
    cache[ "misses" ] = s.cacheMisses;
    rec[ "cache" ] = cache;
    if ( s.extra.isObject() )
        rec[ "extra" ] = s.extra;

    const QString path = QDir( QString::fromUtf8( outDir ) )
                         .filePath( QString::fromStdString( workload ) + ".json" );
    std::ofstream out( path.toStdString(), std::ios::trunc );
    out << Json::writeString( Json::StreamWriterBuilder(), rec );
}

//------------------------------------------------------------------------------
// Fixture: execution plane wired like production (TaskCenter → JobEngine →
// RSOperatorRegistry, catalog attached, execution cache enabled)
//------------------------------------------------------------------------------

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

struct BenchFixture
{
    QTemporaryDir dir;
    sicnu::data::DataManager dataManager;
    int width = 1024;
    int height = 1024;

    BenchFixture()
    {
        if ( largeBench() )
        {
            width = 3072;
            height = 3072;
        }
        int argc = 1;
        static char arg0[] = "test_execution_benchmarks";
        char *argv[] = { arg0, nullptr };
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

//------------------------------------------------------------------------------
// Timed runners
//------------------------------------------------------------------------------

int cacheFlagOf( const Json::Value &payload )
{
    if ( !payload.isObject() )
        return -1;
    const std::string flag = payload.get( "cache", "-" ).asString();
    if ( flag == "hit" )
        return 1;
    if ( flag == "miss" )
        return 0;
    return -1;
}

Json::Value runOperatorTask( const std::string &operatorId, const QVariantMap &params,
                             int &cacheHits, int &cacheMisses )
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
    const int flag = cacheFlagOf( info.resultPayload );
    if ( flag == 1 )
        ++cacheHits;
    else if ( flag == 0 )
        ++cacheMisses;
    return info.resultPayload;
}

// Runs a workflow pipeline through TaskCenter and waits for completion.
// Counts per-step cache flags. Returns per-step payloads keyed by step id.
std::map<std::string, Json::Value> runPipelineTask( const sicnu::workflow::WorkflowDefinition &def,
                                                    int &cacheHits, int &cacheMisses )
{
    auto &center = sicnu::TaskCenter::instance();
    const long pipelineId = center.submitPipeline( def, /*autoLoad=*/false );
    REQUIRE( pipelineId > 0 );
    const auto pipeline = center.waitForPipeline( pipelineId, std::chrono::minutes( 20 ) );
    INFO( "pipeline error: " << pipeline.errorMessage.toStdString() );
    REQUIRE_FALSE( pipeline.isFailed );

    std::map<std::string, Json::Value> payloads;
    // PipelineExecutionInfo.stepToTaskId is a QMap<std::string,long>; ranging
    // over it yields the taskId values, and taskToStepId inverts them.
    for ( const long taskId : pipeline.stepToTaskId )
    {
        const auto info = center.getTaskInfo( taskId );
        INFO( "step " << pipeline.taskToStepId.value( taskId )
                      << " error: " << info.errorMessage.toStdString() );
        REQUIRE( sicnu::isTerminalStatus( info.status ) );
        REQUIRE( info.status == sicnu::TaskStatus::Completed );
        payloads[ pipeline.taskToStepId.value( taskId ) ] = info.resultPayload;
        const int flag = cacheFlagOf( info.resultPayload );
        if ( flag == 1 )
            ++cacheHits;
        else if ( flag == 0 )
            ++cacheMisses;
    }
    return payloads;
}

BenchSample timeWorkload( const std::function<void( BenchSample & )> &fn )
{
    BenchSample s;
    RssPeakTracker tracker;
    const unsigned baseMb = currentRssMb();
    const qint64 r0 = procIoCounter( "read_bytes" );
    const qint64 w0 = procIoCounter( "write_bytes" );
    const clock_t c0 = clock();
    const auto t0 = std::chrono::steady_clock::now();
    tracker.start();
    fn( s );
    const auto t1 = std::chrono::steady_clock::now();
    const unsigned peakMb = tracker.stop();
    s.wallMs = std::chrono::duration<double, std::milli>( t1 - t0 ).count();
    s.cpuMs = 1000.0 * static_cast<double>( clock() - c0 ) / CLOCKS_PER_SEC;
    s.peakRssDeltaMb = peakMb > baseMb ? peakMb - baseMb : 0;
    s.readBytes = procIoCounter( "read_bytes" ) - r0;
    s.writeBytes = procIoCounter( "write_bytes" ) - w0;
    return s;
}

//------------------------------------------------------------------------------
// MiniCogServer lives in tests/support/mini_cog_server.h (shared with the
// Phase F remote cache tests).
//------------------------------------------------------------------------------

using MiniCogServer = sicnu_test::MiniCogServer;

//------------------------------------------------------------------------------
// Fake model-inference operator: deterministic per-tile "forward pass"
// (fixed nonlinear map) standing in for a segmentation/classification model,
// including the per-call model lifecycle cost this epic removes.
//------------------------------------------------------------------------------
class FakeTileInferOperator final : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "bench:model_tile_infer"; }
    std::string displayName() const override { return "Bench tile inference"; }
    std::string determinismGrade() const override { return "bit-exact"; }
    Json::Value executionEstimate() const override
    {
        Json::Value v;
        v[ "tileWidth" ] = 256;
        v[ "tileHeight" ] = 256;
        v[ "estimatedRamBytes" ] = static_cast<Json::Int64>( 64ull << 20 );
        return v;
    }
    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &context ) override
    {
        const std::string input = params[ "input" ].asString();
        const std::string output = params[ "output" ].asString();
        const int classes = std::max( 2, params.get( "classes", 4 ).asInt() );

        // "Model load" cost: deterministic transform over a fixed weight block.
        volatile double warm = 0.0;
        for ( int i = 0; i < 200000; ++i )
            warm += std::sin( i * 0.001 );
        Q_UNUSED( warm );

        GDALAllRegister();
        GDALDataset *in = static_cast<GDALDataset *>( GDALOpen( input.c_str(), GA_ReadOnly ) );
        if ( !in )
            throw std::runtime_error( "cannot open input" );
        const int w = in->GetRasterXSize();
        const int h = in->GetRasterYSize();
        const int bands = in->GetRasterCount();
        std::array<double, 6> gtBuf = { 0, 1, 0, 0, 0, -1 };
        in->GetGeoTransform( gtBuf.data() );
        GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
        GDALDataset *out = driver->Create( output.c_str(), w, h, 1, GDT_UInt16, nullptr );
        if ( !out )
        {
            GDALClose( in );
            throw std::runtime_error( "cannot create output" );
        }
        out->SetGeoTransform( gtBuf.data() );

        const int tile = 256;
        std::vector<float> window( static_cast<size_t>( tile ) * tile * bands );
        std::vector<uint16_t> tileOut( static_cast<size_t>( tile ) * tile );
        int tilesDone = 0;
        const int totalTiles = ( ( h + tile - 1 ) / tile ) * ( ( w + tile - 1 ) / tile );
        for ( int y = 0; y < h; y += tile )
        {
            for ( int x = 0; x < w; x += tile )
            {
                const int tw = std::min( tile, w - x );
                const int th = std::min( tile, h - y );
                for ( int b = 1; b <= bands; ++b )
                    in->GetRasterBand( b )->RasterIO( GF_Read, x, y, tw, th,
                                                      window.data() + ( b - 1 ), tw, th,
                                                      GDT_Float32, sizeof( float ) * bands,
                                                      0, nullptr );
                for ( int py = 0; py < th; ++py )
                {
                    for ( int px = 0; px < tw; ++px )
                    {
                        double acc = 0.0;
                        for ( int b = 0; b < bands; ++b )
                        {
                            const float v = window[static_cast<size_t>( py ) * tw * bands
                                                   + px * bands + b];
                            acc += std::sin( v * 0.001 + b ) * std::cos( v * 0.0007 );
                        }
                        const double norm = 0.5 + 0.5 * acc / bands;
                        tileOut[static_cast<size_t>( py ) * tw + px] =
                            static_cast<uint16_t>( 1 + static_cast<int>( norm * ( classes - 1 ) ) );
                    }
                }
                out->GetRasterBand( 1 )->RasterIO( GF_Write, x, y, tw, th, tileOut.data(),
                                                   tw, th, GDT_UInt16, 0, 0, nullptr );
                ++tilesDone;
                context.reportProgress( static_cast<double>( tilesDone ) / totalTiles, "tile" );
            }
        }
        GDALClose( in );
        GDALClose( out );

        Json::Value result;
        result[ "output" ] = output;
        result[ "tiles" ] = tilesDone;
        return result;
    }
};

void installFakeInferOperator()
{
    static const bool installed = [] {
        sicnu::operators::RSOperatorRegistry::instance().registerOperator(
            "bench:model_tile_infer",
            [] { return std::make_unique<FakeTileInferOperator>(); } );
        return true;
    }();
    Q_UNUSED( installed );
}

//------------------------------------------------------------------------------
// Minimal in-memory source provider for DataManager bench registration.
//------------------------------------------------------------------------------
class BenchMemorySourceProvider final : public sicnu::data::internal::SourceProvider
{
  public:
    bool supports( const sicnu::data::SourceDescriptor &source ) const override
    {
        return source.providerKey == QStringLiteral( "memory-raster" );
    }
    sicnu::data::Result<sicnu::data::internal::ResolvedSource>
    resolve( const sicnu::data::SourceDescriptor &source ) const override
    {
        using namespace sicnu::data;
        internal::ResolvedSource resolved;
        resolved.kind = AssetKind::Raster;
        resolved.state = AssetState::Ready;
        resolved.capabilities = AssetCapability::Renderable | AssetCapability::ReadablePixels;
        resolved.storageKind = StorageKind::Memory;
        resolved.displayName = QStringLiteral( "Bench asset" );
        resolved.canonicalSource = source.canonicalSource;
        resolved.canonicalProviderKey = source.providerKey;
        resolved.structure = AssetStructure{ RasterStructure{} };
        return Result<internal::ResolvedSource>::success( resolved );
    }
};

//------------------------------------------------------------------------------
// Shared raster bundle used by several workloads
//------------------------------------------------------------------------------
struct BenchRasters
{
    QString reflectance;
    QString labels;
    QString qa;

    explicit BenchRasters( BenchFixture &fx )
    {
        reflectance = fx.path( "refl.tif" );
        labels = fx.path( "labels.tif" );
        qa = fx.path( "qa.tif" );
        writeFloatTiff( reflectance, fx.width, fx.height, 4, 0.0, 0.0, 0x12345678u );
        writeU16Tiff( labels, fx.width, fx.height, 0x22222222u, 1 );
        writeU16Tiff( qa, fx.width, fx.height, 0x55555555u, 0 );
        registerRaster( fx.dataManager, reflectance );
        registerRaster( fx.dataManager, labels );
        registerRaster( fx.dataManager, qa );
    }
};

} // namespace

//------------------------------------------------------------------------------
// Single-operator workloads
//------------------------------------------------------------------------------

TEST_CASE( "ebench spectral_index streaming", "[execution_bench]" )
{
    BenchFixture fx;
    BenchRasters rasters( fx );

    const BenchSample s = timeWorkload( [&]( BenchSample &sample ) {
        QVariantMap params;
        params.insert( "input", rasters.reflectance );
        params.insert( "output", fx.path( "ndvi_out.tif" ) );
        params.insert( "index", "NDVI" );
        params.insert( "nir", 1 );
        params.insert( "red", 2 );
        const Json::Value payload = runOperatorTask( "rs:spectral_index", params,
                                                     sample.cacheHits, sample.cacheMisses );
        REQUIRE( payload.isMember( "output" ) );
    } );
    writeBenchJson( "spectral_index_streaming", s );
}

TEST_CASE( "ebench qa_mask", "[execution_bench]" )
{
    BenchFixture fx;
    BenchRasters rasters( fx );

    const BenchSample s = timeWorkload( [&]( BenchSample &sample ) {
        QVariantMap params;
        params.insert( "input", rasters.qa );
        params.insert( "output", fx.path( "mask_out.tif" ) );
        params.insert( "source", "generic_bitmask" );
        params.insert( "bits", 8 );
        params.insert( "qa_band", 1 );
        const Json::Value payload = runOperatorTask( "rs:qa_mask", params,
                                                     sample.cacheHits, sample.cacheMisses );
        REQUIRE( payload.isMember( "output" ) );
    } );
    writeBenchJson( "qa_mask", s );
}

TEST_CASE( "ebench recode", "[execution_bench]" )
{
    BenchFixture fx;
    BenchRasters rasters( fx );

    const BenchSample s = timeWorkload( [&]( BenchSample &sample ) {
        QVariantMap params;
        params.insert( "input", rasters.labels );
        params.insert( "output", fx.path( "recoded_out.tif" ) );
        params.insert( "recode_map", QString( "{\"1\":2,\"2\":3,\"3\":1,\"4\":4,\"5\":1}" ) );
        const Json::Value payload = runOperatorTask( "rs:recode", params,
                                                     sample.cacheHits, sample.cacheMisses );
        REQUIRE( payload.isMember( "output" ) );
    } );
    writeBenchJson( "recode", s );
}

TEST_CASE( "ebench majority_filter", "[execution_bench]" )
{
    BenchFixture fx;
    BenchRasters rasters( fx );

    const BenchSample s = timeWorkload( [&]( BenchSample &sample ) {
        QVariantMap params;
        params.insert( "input", rasters.labels );
        params.insert( "output", fx.path( "majority_out.tif" ) );
        params.insert( "kernel", 3 );
        const Json::Value payload = runOperatorTask( "rs:majority_filter", params,
                                                     sample.cacheHits, sample.cacheMisses );
        REQUIRE( payload.isMember( "output" ) );
    } );
    writeBenchJson( "majority_filter", s );
}

TEST_CASE( "ebench temporal_composite", "[execution_bench]" )
{
    BenchFixture fx;
    const int sceneSize = largeBench() ? 1024 : 512;
    QVariantList scenes;
    for ( int i = 0; i < 6; ++i )
    {
        // Sentinel-2-style filename fragment (_YYYYMMDDTHHMMSS) gives each
        // scene its acquisition instant via the filename time source.
        const QString scene =
            fx.path( QStringLiteral( "scene_2024010%1T100000.tif" ).arg( i + 1 ) );
        writeFloatTiff( scene, sceneSize, sceneSize, 2, 0.0, 0.0,
                        0x0BBC0DE0u + static_cast<uint32_t>( i ) * 31u );
        registerRaster( fx.dataManager, scene );
        scenes.append( scene );
    }

    const BenchSample s = timeWorkload( [&]( BenchSample &sample ) {
        QVariantMap params;
        params.insert( "scenes", scenes );
        params.insert( "method", "mean" );
        params.insert( "output", fx.path( "composite_out.tif" ) );
        const Json::Value payload = runOperatorTask( "rs:temporal_composite", params,
                                                     sample.cacheHits, sample.cacheMisses );
        REQUIRE( payload.isMember( "output" ) );
    } );
    writeBenchJson( "temporal_composite", s );
}

//------------------------------------------------------------------------------
// Multi-step DAG: QA -> mask -> NDVI -> threshold (file-level dependencies)
//------------------------------------------------------------------------------

sicnu::workflow::WorkflowDefinition makeQaToThresholdPipeline( const BenchFixture &fx,
                                                               const BenchRasters &rasters,
                                                               const QString &outDir )
{
    using namespace sicnu::workflow;
    WorkflowDefinition def;
    def.id = "ebench_qa_to_threshold";
    def.title = "QA to threshold benchmark chain";

    StepDef qa;
    qa.id = "qa";
    qa.kind = StepKind::Operator;
    qa.operatorId = "rs:qa_mask";
    qa.params[ "input" ] = rasters.qa.toStdString();
    qa.params[ "output" ] = outDir.toStdString() + "/dag_mask.tif";
    qa.params[ "source" ] = "generic_bitmask";
    qa.params[ "bits" ] = 8;
    qa.params[ "qa_band" ] = 1;

    StepDef apply;
    apply.id = "apply";
    apply.kind = StepKind::Operator;
    apply.operatorId = "rs:apply_mask";
    apply.params[ "input" ] = rasters.reflectance.toStdString();
    apply.params[ "mask" ] = "$qa.output";
    apply.params[ "output" ] = outDir.toStdString() + "/dag_masked.tif";
    apply.params[ "no_data" ] = -9999.0;
    StepConnection toMask;
    toMask.fromStepId = "qa";
    toMask.fromPort = "output";
    toMask.toPort = "mask";
    apply.inputs.push_back( toMask );

    StepDef ndvi;
    ndvi.id = "ndvi";
    ndvi.kind = StepKind::Operator;
    ndvi.operatorId = "rs:spectral_index";
    ndvi.params[ "input" ] = "$apply.output";
    ndvi.params[ "output" ] = outDir.toStdString() + "/dag_ndvi.tif";
    ndvi.params[ "index" ] = "NDVI";
    ndvi.params[ "nir" ] = 1;
    ndvi.params[ "red" ] = 2;
    StepConnection toNdvi;
    toNdvi.fromStepId = "apply";
    toNdvi.fromPort = "output";
    toNdvi.toPort = "input";
    ndvi.inputs.push_back( toNdvi );

    StepDef thr;
    thr.id = "thr";
    thr.kind = StepKind::Operator;
    thr.operatorId = "rs:threshold_raster";
    thr.params[ "input" ] = "$ndvi.output";
    thr.params[ "output" ] = outDir.toStdString() + "/dag_final.tif";
    thr.params[ "threshold" ] = 500.0;
    StepConnection toThr;
    toThr.fromStepId = "ndvi";
    toThr.fromPort = "output";
    toThr.toPort = "input";
    thr.inputs.push_back( toThr );

    def.steps = { qa, apply, ndvi, thr };
    return def;
}

TEST_CASE( "ebench dag multi-step cold", "[execution_bench]" )
{
    BenchFixture fx;
    BenchRasters rasters( fx );
    const QString outDir = fx.path( "dag_cold" );
    QDir().mkpath( outDir );
    const auto def = makeQaToThresholdPipeline( fx, rasters, outDir );

    const BenchSample s = timeWorkload( [&]( BenchSample &sample ) {
        const auto payloads = runPipelineTask( def, sample.cacheHits, sample.cacheMisses );
        REQUIRE( payloads.size() == 4 );
        REQUIRE( payloads.count( "thr" ) == 1 );
    } );
    writeBenchJson( "dag_multi_step_cold", s );
}

TEST_CASE( "ebench dag multi-step cache hit", "[execution_bench]" )
{
    BenchFixture fx;
    BenchRasters rasters( fx );
    const QString outDir = fx.path( "dag_warm" );
    QDir().mkpath( outDir );
    const auto def = makeQaToThresholdPipeline( fx, rasters, outDir );
    {
        int hits = 0, misses = 0;
        const auto payloads = runPipelineTask( def, hits, misses );
        REQUIRE( payloads.size() == 4 );
    }

    // Second submission, same logical identity (destinations are excluded
    // from the fingerprint), same destinations: every step should serve from
    // the execution cache instead of recomputing.
    const BenchSample s = timeWorkload( [&]( BenchSample &sample ) {
        const auto payloads = runPipelineTask( def, sample.cacheHits, sample.cacheMisses );
        REQUIRE( payloads.size() == 4 );
        REQUIRE( QFileInfo( outDir + "/dag_final.tif" ).exists() );
    } );
    writeBenchJson( "dag_multi_step_cache_hit", s );
    REQUIRE( s.cacheHits >= 1 );
}

//------------------------------------------------------------------------------
// Remote COG read over /vsicurl/
//------------------------------------------------------------------------------

TEST_CASE( "ebench remote cog read", "[execution_bench]" )
{
    BenchFixture fx;
    // Tiled TIFF remote source: 512x512 float, single band.
    const QString cogPath = fx.path( "remote_cog.tif" );
    {
        ensureGdalInit();
        const char *options[] = { "TILED=YES", "BLOCKXSIZE=256", "BLOCKYSIZE=256", nullptr };
        GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
        GDALDataset *ds = driver->Create( cogPath.toUtf8().constData(), 512, 512, 1,
                                          GDT_Float32, const_cast<char **>( options ) );
        REQUIRE( ds != nullptr );
        uint32_t seed = 0x0DEFACEDu;
        std::vector<float> buf( 512ull * 512 );
        for ( size_t i = 0; i < buf.size(); ++i )
            buf[i] = lcgFloat( seed );
        REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 512, 512, buf.data(),
                                                   512, 512, GDT_Float32, 0, 0, nullptr )
                 == CE_None );
        GDALClose( ds );
    }

    MiniCogServer server( cogPath );
    REQUIRE( server.start() );
    const QString url = QString( "http://127.0.0.1:%1/cog.tif" ).arg( server.port() );

    BenchSample s = timeWorkload( [&]( BenchSample &sample ) {
        QVariantMap params;
        params.insert( "input", "/vsicurl/" + url );
        params.insert( "output", fx.path( "remote_ndvi.tif" ) );
        params.insert( "index", "NDVI" );
        params.insert( "nir", 1 );
        params.insert( "red", 1 );
        const Json::Value payload = runOperatorTask( "rs:spectral_index", params,
                                                     sample.cacheHits, sample.cacheMisses );
        REQUIRE( payload.isMember( "output" ) );
    } );
    s.extra[ "http_requests" ] = static_cast<Json::Int64>( server.requests() );
    s.extra[ "http_range_requests" ] = static_cast<Json::Int64>( server.rangeRequests() );
    writeBenchJson( "remote_cog_read", s );
}

//------------------------------------------------------------------------------
// Model tile inference (fake deterministic model)
//------------------------------------------------------------------------------

TEST_CASE( "ebench model tile inference", "[execution_bench]" )
{
    BenchFixture fx;
    installFakeInferOperator();
    BenchRasters rasters( fx );

    const BenchSample s = timeWorkload( [&]( BenchSample &sample ) {
        QVariantMap params;
        params.insert( "input", rasters.reflectance );
        params.insert( "output", fx.path( "infer_out.tif" ) );
        params.insert( "classes", 4 );
        const Json::Value payload = runOperatorTask( "bench:model_tile_infer", params,
                                                     sample.cacheHits, sample.cacheMisses );
        REQUIRE( payload.isMember( "output" ) );
    } );
    writeBenchJson( "model_tile_inference", s );
}

//------------------------------------------------------------------------------
// Large DataManager query
//------------------------------------------------------------------------------

TEST_CASE( "ebench data manager query", "[execution_bench]" )
{
    BenchFixture fx;
    // A manager wired with an in-memory provider (the cheap no-GDAL-open
    // path) — registration cost itself is setup, the CONTRACT measured below
    // is query performance.
    sicnu::data::internal::SourceProviderRegistry providers;
    providers.add( std::make_unique<BenchMemorySourceProvider>() );
    auto managerPtr = providers.createDataManager();
    sicnu::data::DataManager &manager = *managerPtr;
    const int count = largeBench() ? 100000 : 20000;

    // Registration itself is setup (timed separately below); memory-raster
    // sources keep it cheap (no GDAL open per asset).
    BenchSample registerSample;
    {
        sicnu::data::AssetId firstAssetId;
        const auto t0 = std::chrono::steady_clock::now();
        for ( int i = 0; i < count; ++i )
        {
            sicnu::data::SourceDescriptor source;
            source.providerKey = QStringLiteral( "memory-raster" );
            source.canonicalSource =
                QStringLiteral( "mem://bench/asset_%1.tif" ).arg( i, 6, 10, QLatin1Char( '0' ) );
            sicnu::data::RegisterRequest request;
            request.source = source;
            request.persistence = sicnu::data::PersistencePolicy::ProjectPersistent;
            const auto result = manager.registerSource( request );
            if ( result.assetId.isNull() )
            {
                for ( const auto &d : result.diagnostics )
                    INFO( qPrintable( d.code + ": " + d.message ) );
                FAIL( "registerSource failed at i=" << i );
            }
            if ( i == 0 )
                firstAssetId = result.assetId;
            if ( i % 10 == 0 )
            {
                sicnu::data::DerivationInput input;
                input.assetId = firstAssetId;
                QJsonObject emptyParams;
                manager.attachDerivationRecord(
                    result.assetId,
                    sicnu::data::makeTaskDerivation( QStringLiteral( "bench:producer" ),
                                                     emptyParams,
                                                     QStringLiteral( "bench-task-%1" ).arg( i ),
                                                     { input } ) );
            }
        }
        registerSample.wallMs = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - t0 ).count();
        registerSample.extra[ "assets_registered" ] = count;
        writeBenchJson( "data_manager_register", registerSample );
    }

    // Full listing (the 100k-asset GUI/blocking-listing scenario).
    BenchSample listing;
    {
        const auto t0 = std::chrono::steady_clock::now();
        const auto all = manager.assets( sicnu::data::AssetQuery{} );
        listing.wallMs =
            std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - t0 ).count();
        listing.extra[ "assets_listed" ] = static_cast<Json::Int64>( all.size() );
        REQUIRE( static_cast<int>( all.size() ) == count );
    }
    writeBenchJson( "data_manager_list_all", listing );

    // Point lookups by path (the fingerprint/lineage hot path; linear scan +
    // canonicalFilePath stat per record today).
    const int probes = 200;
    BenchSample findByPath = timeWorkload( [&]( BenchSample &sample ) {
        int found = 0;
        for ( int i = 0; i < probes; ++i )
        {
            const QString p = QStringLiteral( "mem://bench/asset_%1.tif" )
                              .arg( i * 97 % count, 6, 10, QLatin1Char( '0' ) );
            if ( manager.findByPath( p ).has_value() )
                ++found;
        }
        sample.extra[ "probes" ] = probes;
        sample.extra[ "found" ] = found;
        REQUIRE( found == probes );
    } );
    findByPath.extra[ "per_probe_ms" ] = findByPath.wallMs / probes;
    writeBenchJson( "data_manager_find_by_path", findByPath );

    // Lineage fan-out scan.
    BenchSample derived = timeWorkload( [&]( BenchSample &sample ) {
        const auto outputs = manager.derivedOutputsOf(
            manager.findByPath( QStringLiteral( "mem://bench/asset_000000.tif" ) )->id() );
        sample.extra[ "outputs_of_first" ] = static_cast<Json::Int64>( outputs.size() );
        REQUIRE( !outputs.isEmpty() );
    } );
    writeBenchJson( "data_manager_derived_outputs", derived );
}

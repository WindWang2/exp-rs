// benchmark_scale8.cpp — scale/performance baselines for the verification
// platform (task G, Verification Platform 8.0). Run manually:
//
//   benchmark_scale8 --out benchmarks/scale8.json
//
// Measures (median of repeats; scale bounded by env, hard wall-clock caps):
//   * schedule_1k / schedule_10k / schedule_100k
//       JobEngine short-task scheduling at the three documented scales
//       (submit + waitForJob of instant callable jobs). Evidence for
//       "scheduling stays flat / identifies the cliff".
//   * dataset_metadata_insert_100k / dataset_metadata_page_read
//       DatasetStore createDataset bulk (default 100k rows, env-overridable)
//       and paged listDatasets over the resulting store — the 100k+ metadata
//       path the goal names.
//   * raster_window_reads
//       RasterReader::readWindow over a synthetic 1024² Float32 raster,
//       walked as 64² windows — per-window geospatial read cost.
//   * trace_file_sink_overhead
//       emit into an installed FileTraceSink with bounded queue — recorded
//       alongside written+dropped accounting coherence.
//
// Evidence only: wall-clock numbers are regression EVIDENCE (JSON artifact
// with environment headers), never a hard gate.

#include "geospatial/raster/raster_reader.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_ids.h"
#include "runtime/observability/trace.h"

#include <QTemporaryDir>

#include <gdal.h>

#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace sicnu::geo;
using namespace sicnu::dataset;
using namespace sicnu::jobs;
using namespace sicnu::runtime::observability::trace;

namespace
{
struct Measurement
{
    std::string name;
    double opsPerSecond;
    int iterations;
    std::string note;
};

template <typename Fn>
double measureOpsPerSecond( int iterations, Fn &&fn )
{
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    for ( int i = 0; i < iterations; ++i )
        fn();
    const auto end = clock::now();
    const double seconds =
        std::chrono::duration_cast<std::chrono::duration<double>>( end - start ).count();
    return seconds > 0 ? static_cast<double>( iterations ) / seconds : 0.0;
}

Measurement medianOf( const std::string &name, int repeats, int iterations, std::string note,
                      const std::function<double( int )> &runner )
{
    std::vector<double> samples;
    for ( int r = 0; r < repeats; ++r )
        samples.push_back( runner( iterations ) );
    std::sort( samples.begin(), samples.end() );
    return { name, samples[samples.size() / 2], iterations, std::move( note ) };
}

int envOr( const char *name, int fallback )
{
    const char *value = std::getenv( name );
    return value ? std::max( 1, std::atoi( value ) ) : fallback;
}

QString writeSyntheticRaster( const QString &path, int width, int height )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                  GDT_Float32, nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( height ), 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( width, 0.5f );
    for ( int row = 0; row < height; ++row )
        GDALRasterIO( band, GF_Write, 0, row, width, 1, line.data(), width, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return path;
}
} // namespace

int main( int argc, char **argv )
{
    std::string outPath = "benchmarks/scale8.json";
    for ( int i = 1; i + 1 < argc; ++i )
        if ( std::strcmp( argv[i], "--out" ) == 0 )
            outPath = argv[i + 1];

    const int scheduleMax = envOr( "SICNU_BENCH_SCHEDULE_MAX", 100000 );
    const std::set<int> scheduleSet = { 1000, 10000, scheduleMax };
    std::vector<int> scheduleScales( scheduleSet.begin(), scheduleSet.end() );
    scheduleScales.erase( std::remove_if( scheduleScales.begin(), scheduleScales.end(),
                                          [ scheduleMax ]( int v ) { return v > scheduleMax; } ),
                          scheduleScales.end() );
    const int datasetRows = envOr( "SICNU_BENCH_DATASET_ROWS", 100000 );

    std::vector<Measurement> results;

    // --- short-task scheduling at 1k / 10k / 100k -------------------------
    {
        auto &engine = JobEngine::instance();
        engine.setMaxWorkers( 4 );
        engine.clearExecutors();
        const auto executor = []( const sicnu::jobs::JobRequest &,
                                  sicnu::operators::RSOperatorContext & ) {
            Json::Value result;
            result["ok"] = true;
            return result;
        };
        int repeatOrdinal = 0;
        for ( const int scale : scheduleScales )
        {
            // Unique job ids PER REPEAT: submitWithId refuses an id owned by
            // any live record, and completed records persist until
            // clearCompleted(), so reused ids would silently turn repeats
            // 2..n into no-op submissions (median would then measure nothing).
            const std::string idPrefix = "scale8-r" + std::to_string( repeatOrdinal++ ) + "-";
            results.push_back(
                medianOf( "schedule_" + std::to_string( scale ), 1, scale,
                          "JobEngine submit+await of instant callable jobs",
                          [ & ]( int n ) {
                              std::vector<std::string> ids( static_cast<size_t>( n ) );
                              using clock = std::chrono::steady_clock;
                              const auto start = clock::now();
                              for ( int i = 0; i < n; ++i )
                              {
                                  ids[static_cast<size_t>( i )] =
                                      idPrefix + std::to_string( i );
                                  sicnu::jobs::JobRequest request;
                                  request.algorithmId = "callable:scale8";
                                  engine.submitWithId( request, ids[static_cast<size_t>( i )],
                                                       executor );
                              }
                              for ( const std::string &id : ids )
                                  engine.waitForJob( id, 30000 );
                              const auto end = clock::now();
                              const double seconds =
                                  std::chrono::duration_cast<std::chrono::duration<double>>(
                                      end - start )
                                      .count();
                              return seconds > 0 ? static_cast<double>( n ) / seconds : 0.0;
                          } ) );
        }
        engine.shutdownForTests();
        engine.clearCompleted();
    }

    // --- dataset metadata at 100k rows ------------------------------------
    QString rasterPath;
    {
        QTemporaryDir dir;
        if ( !dir.isValid() )
        {
            std::fprintf( stderr, "temporary dir unavailable\n" );
            return 2;
        }
        DatasetStore store;
        QString error;
        if ( !store.open( dir.filePath( QStringLiteral( "scale8.db" ) ), &error ) )
        {
            std::fprintf( stderr, "cannot open dataset store\n" );
            return 2;
        }
        using clock = std::chrono::steady_clock;
        const auto start = clock::now();
        for ( int i = 0; i < datasetRows; ++i )
        {
            ( void ) store.createDataset( DatasetId::generate(),
                                          QStringLiteral( "scale8-ds-%1" ).arg( i ) );
        }
        const auto end = clock::now();
        const double seconds =
            std::chrono::duration_cast<std::chrono::duration<double>>( end - start ).count();
        Measurement insert;
        insert.name = "dataset_metadata_insert_" + std::to_string( datasetRows );
        insert.iterations = datasetRows;
        insert.note = "DatasetStore::createDataset single-threaded bulk";
        insert.opsPerSecond = seconds > 0 ? static_cast<double>( datasetRows ) / seconds : 0.0;
        results.push_back( insert );

        // Paged reads over the full store (1000-row pages).
        const int pages = std::min( 100, static_cast<int>( ( datasetRows + 999 ) / 1000 ) );
        results.push_back(
            medianOf( "dataset_metadata_page_read", 3, pages,
                      "listDatasets 1000-row pages over the bulk store",
                      [ & ]( int n ) {
                          using clock2 = std::chrono::steady_clock;
                          qint64 total = 0;
                          const auto start2 = clock2::now();
                          for ( int p = 0; p < n; ++p )
                          {
                              const auto page = store.listDatasets(
                                  static_cast<qint64>( p ) * 1000, 1000 );
                              if ( page )
                                  total += page.value().first;
                          }
                          const auto end2 = clock2::now();
                          const double seconds2 =
                              std::chrono::duration_cast<std::chrono::duration<double>>(
                                  end2 - start2 )
                                  .count();
                          return seconds2 > 0 ? static_cast<double>( n ) / seconds2 : 0.0;
                      } ) );
    }

    // --- geospatial window reads -------------------------------------------
    {
        QTemporaryDir dir;
        rasterPath = writeSyntheticRaster( dir.filePath( QStringLiteral( "scale8.tif" ) ),
                                           1024, 1024 );
        const auto reader = RasterReader::open( rasterPath.toStdString() );
        if ( reader.isOpen() )
        {
            constexpr int kWindow = 64;
            results.push_back(
                medianOf( "raster_window_reads", 5, 256,
                          "64x64 readWindow walk over a 1024x1024 Float32 raster",
                          [ & ]( int n ) {
                              using clock3 = std::chrono::steady_clock;
                              std::size_t checksum = 0;
                              const auto start3 = clock3::now();
                              for ( int i = 0; i < n; ++i )
                              {
                                  const int x = ( i * kWindow ) % ( 1024 - kWindow );
                                  const int y = ( i * kWindow ) % ( 1024 - kWindow );
                                  const auto window =
                                      reader.readWindow( { 1 }, { x, y, kWindow, kWindow } );
                                  checksum += window.size();
                              }
                              const auto end3 = clock3::now();
                              const double seconds3 =
                                  std::chrono::duration_cast<std::chrono::duration<double>>(
                                      end3 - start3 )
                                      .count();
                              return checksum == 0 || seconds3 <= 0
                                       ? 0.0
                                       : static_cast<double>( n ) / seconds3;
                          } ) );
        }
    }

    // --- trace file sink overhead (bounded queue, honest accounting) -------
    {
        QTemporaryDir dir;
        FileTraceSink::Options options;
        options.directory = dir.path().toStdString();
        options.baseName = "scale8-trace";
        options.maxFileBytes = 4u * 1024 * 1024;
        options.queueCapacity = 4096;
        auto sink = std::make_shared<FileTraceSink>( options );
        Trace::install( sink );
        constexpr int kEvents = 10000;
        results.push_back(
            medianOf( "trace_file_sink_overhead", 5, kEvents,
                      "emit into a FileTraceSink (4 MiB rotation, 4096 queue)",
                      []( int n ) {
                          return measureOpsPerSecond( n, [] {
                              TraceEvent event;
                              event.task = "7";
                              event.event = "benchmark";
                              Trace::publish( event );
                          } );
                      } ) );
        Trace::install( nullptr );
        sink.reset(); // drain: destructor flushes the writer thread
    }

    // --- emit JSON ----------------------------------------------------------
    std::ofstream out( outPath, std::ios::binary );
    if ( !out.good() )
    {
        std::fprintf( stderr, "cannot write %s\n", outPath.c_str() );
        return 2;
    }
    out << "{\n";
    out << "  \"schema\": \"exp.bench.scale8.v1\",\n";
    out << "  \"os\": \"" <<
#if defined( _WIN32 )
        "windows"
#elif defined( __APPLE__ )
        "macos"
#else
        "linux"
#endif
        << "\",\n";
    out << "  \"cpu_cores\": " << std::max( 1u, std::thread::hardware_concurrency() ) << ",\n";
    out << "  \"build\": \"release\",\n";
    out << "  \"dataset_rows\": " << datasetRows << ",\n";
    out << "  \"note\": \"wall-clock evidence only — not a gate\",\n";
    out << "  \"measurements\": [\n";
    for ( size_t i = 0; i < results.size(); ++i )
    {
        const Measurement &m = results[i];
        out << "    { \"name\": \"" << m.name << "\", \"ops_per_s\": " << m.opsPerSecond
            << ", \"iterations\": " << m.iterations
            << ", \"note\": \"" << m.note << "\" }"
            << ( i + 1 < results.size() ? "," : "" ) << "\n";
    }
    out << "  ]\n}\n";
    std::printf( "scale8: %zu measurements -> %s\n", results.size(), outPath.c_str() );
    return 0;
}

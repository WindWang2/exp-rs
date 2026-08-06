// test_python_plugin_host.cpp — headless Python Plugin Host seam tests (ADR 0023)
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "python_plugin_host.h"
#include "python_plugin_adapter.h"
#include "python_worker_process_pool.h"
#include "shared_memory_segment.h"
#include "data/data_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonObject>
#include <QTimer>

#include <chrono>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

using namespace sicnu::python::isolated;
using Catch::Approx;

// QLocalSocket-based IPC needs a QCoreApplication event loop — without one the
// worker handshake never completes and the suite spins (see test_python_plugin_manager).
int main( int argc, char *argv[] )
{
  QCoreApplication app( argc, argv );
  return Catch::Session().run( argc, argv );
}

TEST_CASE( "PythonPluginHost loads a Python plugin headlessly with a real DataManager", "[python][host]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString error;
  PythonPluginAdapter *adapter = host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &error );
  INFO( error.toStdString() );
  REQUIRE( adapter != nullptr );
  CHECK( !adapter->name().isEmpty() );
  CHECK( host.loadedPlugins() == QStringList{ adapter->name() } );

  host.unloadAll();
  CHECK( host.loadedPlugins().isEmpty() );
}

TEST_CASE( "PythonPluginHost reports a clean error for a missing plugin directory", "[python][host]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  QString error;
  CHECK( host.loadPlugin( QStringLiteral( "/nonexistent/plugin/dir" ), &dataManager, nullptr, nullptr, &error ) == nullptr );
  CHECK( !error.isEmpty() );
}

TEST_CASE( "PythonWorkerProcessPool poolSize and poolHealth queries work correctly", "[python][pool]" )
{
  PythonWorkerProcessPool pool( 3 );
  CHECK( pool.poolSize() == 3 );

  const auto health = pool.poolHealth();
  CHECK( health.total == 0 );

  SECTION( "setPoolSize grows the pool" )
  {
    CHECK( pool.setPoolSize( 5 ) );
    CHECK( pool.poolSize() == 5 );
  }

  SECTION( "setPoolSize shrinks the pool" )
  {
    CHECK( pool.setPoolSize( 1 ) );
    CHECK( pool.poolSize() == 1 );
  }

  SECTION( "setPoolSize rejects zero" )
  {
    CHECK_FALSE( pool.setPoolSize( 0 ) );
    CHECK( pool.poolSize() == 3 ); // unchanged
  }

  SECTION( "setPoolSize rejects negative" )
  {
    CHECK_FALSE( pool.setPoolSize( -1 ) );
    CHECK( pool.poolSize() == 3 ); // unchanged
  }
}

#include "python_ipc_server.h"
#include "python_worker_process_pool.h"
#include "processing/framework/algorithm_engine.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"

#include <QThread>
#include <chrono>

TEST_CASE( "py: prefix executor executes directly on the worker thread (no main-thread marshal)",
           "[python][host][exec]" )
{
  using namespace sicnu::jobs;

  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  // Register py:echo_test on the worker hosting the plugin: only a worker with
  // a resident plugin adapter has a PythonAppInterfaceProxy on its IPC server,
  // and the proxy is what lands processing.register_algorithm in AlgorithmEngine.
  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString loadError;
  PythonPluginAdapter *pluginAdapter = host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &loadError );
  REQUIRE( pluginAdapter != nullptr );

  WorkerNode *node = pluginAdapter->workerNode();
  REQUIRE( node != nullptr );
  REQUIRE( node->server != nullptr );
  REQUIRE( node->server->hasClient() );

  QJsonObject regResult;
  bool regIsError = false;
  REQUIRE( node->server->sendRequestAndAwait( QStringLiteral( "processing.test_register_algorithm" ),
                                              QJsonObject(), regResult, regIsError, 10000 )
           == AwaitStatus::Ok );
  REQUIRE( !regIsError );

  SECTION( "registered py: algorithm succeeds via JobEngine (worker thread caller)" )
  {
    JobRequest req;
    req.algorithmId = "py:echo_test";
    req.params["value"] = 42;
    const std::string jobId = JobEngine::instance().submit( req );

    // The worker now runs sendRequestSync directly (no main-thread marshal),
    // so no processEvents pump is required for the job to progress. We still
    // poll snapshots to observe completion (CLI runner pattern).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );
    for ( ;; )
    {
      QCoreApplication::processEvents();
      const auto snap = JobEngine::instance().snapshot( jobId );
      if ( snap && ( snap->state == JobState::Succeeded || snap->state == JobState::Failed ) )
      {
        INFO( snap->error );
        CHECK( snap->state == JobState::Succeeded );
        break;
      }
      if ( std::chrono::steady_clock::now() > deadline )
      {
        FAIL( "py: job did not finish within 30 s (sendRequestSync deadlock?)" );
        break;
      }
      QThread::msleep( 5 );
    }
  }

  SECTION( "unknown py: algorithm fails cleanly via JobEngine" )
  {
    JobRequest req;
    req.algorithmId = "py:ghost_unknown";
    const std::string jobId = JobEngine::instance().submit( req );

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );
    for ( ;; )
    {
      QCoreApplication::processEvents();
      const auto snap = JobEngine::instance().snapshot( jobId );
      if ( snap && ( snap->state == JobState::Succeeded || snap->state == JobState::Failed ) )
      {
        CHECK( snap->state == JobState::Failed );
        CHECK_FALSE( snap->error.empty() );
        break;
      }
      if ( std::chrono::steady_clock::now() > deadline )
      {
        FAIL( "py: job did not finish within 30 s" );
        break;
      }
      QThread::msleep( 5 );
    }
  }
  // NOTE: the plugin adapter owns its worker — no releaseWorker here.
}

#include "cli/rs_pipeline_runner.h"

TEST_CASE( "CLI runner executes a py: pipeline step end-to-end", "[python][host][pipeline]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "../tests/data/plugins/echo_plugin" ) );
  QString error;
  INFO( error.toStdString() );
  REQUIRE( host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &error ) != nullptr );

  sicnu::cli::RsPipelineRunner runner;
  Json::Value pipeline( Json::objectValue );
  pipeline["name"] = "echo-pipeline";
  Json::Value step( Json::objectValue );
  step["id"] = "s1";
  step["operator"] = "py:echo_plugin";
  step["params"]["value"] = 7;
  pipeline["steps"].append( step );

  const auto result = runner.runFromJson( pipeline );
  INFO( result.errorMessage );
  CHECK( result.success );
  REQUIRE( result.steps.size() == 1 );
  CHECK( result.steps[0].success );
}

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"

#include <gdal.h>
#include <gdal_priv.h>

namespace
{

/// Test operator writing a tiny valid GeoTIFF to params["output"].
class TiffMakerOperator : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "test:tiff_maker"; }

    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &context ) override
    {
      Q_UNUSED( context );
      const std::string output = params["output"].asString();
      GDALAllRegister();
      GDALDriverH driver = GDALGetDriverByName( "GTiff" );
      if ( !driver )
        throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::GdalError, "GTiff driver unavailable" );
      GDALDatasetH ds = GDALCreate( driver, output.c_str(), 4, 4, 1, GDT_Byte, nullptr );
      if ( !ds )
        throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::GdalError, "Failed to create " + output );
      GDALClose( ds );
      Json::Value result( Json::objectValue );
      result["output"] = output;
      return result;
    }
};

REGISTER_RS_OPERATOR( TiffMakerOperator, "test:tiff_maker" )

} // namespace

TEST_CASE( "CLI runner registers completed step outputs as Data Assets", "[python][host][assets]" )
{
  sicnu::data::DataManager dataManager;
  // No plugin host needed for this case — registration is operator-driven.
  const QString outputPath = QDir::temp().filePath( QStringLiteral( "sicnu_tiff_maker_test.tif" ) );
  QFile::remove( outputPath );

  sicnu::cli::RsPipelineRunner runner;
  runner.setAssetRegistry( &dataManager );

  Json::Value pipeline( Json::objectValue );
  pipeline["name"] = "tiff-pipeline";
  Json::Value step( Json::objectValue );
  step["id"] = "s1";
  step["operator"] = "test:tiff_maker";
  step["params"]["output"] = outputPath.toStdString();
  pipeline["steps"].append( step );

  const auto result = runner.runFromJson( pipeline );
  INFO( result.errorMessage );
  CHECK( result.success );

  const auto assets = dataManager.assets();
  REQUIRE( assets.size() == 1 );
  CHECK( assets[0].source().canonicalSource == outputPath );
  CHECK( dataManager.provenance( assets[0].id() ).has_value() );

  QFile::remove( outputPath );
}

// ---------------------------------------------------------------------------
// ADR 0064 - Shared memory zero-copy data channel: C++ writes a raster block
// into a SharedMemorySegment, the Python worker reads it via
// multiprocessing.shared_memory + numpy.frombuffer, and returns a checksum.
// We verify the checksum matches what C++ computes independently.
// ---------------------------------------------------------------------------
TEST_CASE( "SharedMemorySegment: C++ writes, Python reads via shm.read, checksum matches",
           "[python][shm]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  // Load the sample plugin so a worker is running and its IPC server is live.
  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString loadError;
  PythonPluginAdapter *pluginAdapter = host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &loadError );
  REQUIRE( pluginAdapter != nullptr );

  WorkerNode *node = pluginAdapter->workerNode();
  REQUIRE( node != nullptr );
  REQUIRE( node->server != nullptr );
  REQUIRE( node->server->hasClient() );

  // 16x16x1 float32 raster: values 0.0, 1.0, 2.0, ... 255.0
  constexpr int W = 16, H = 16, B = 1;
  std::vector<float> data( W * H * B );
  std::iota( data.begin(), data.end(), 0.0f );
  const double expectedSum = static_cast<double>( ( W * H - 1 ) ) * ( W * H ) / 2.0; // sum(0..255)

  SharedMemorySegment seg;
  REQUIRE( seg.create( W, H, B, SharedMemorySegment::DType::Float32 ) );
  REQUIRE( seg.isAttached() );
  REQUIRE( seg.write( data.data(), data.size() * sizeof( float ) ) );

  // Ask the Python worker to read the shared memory and compute a checksum.
  QJsonObject params;
  params[QStringLiteral("key")] = seg.nativeKey();
  params[QStringLiteral( "width" )] = W;
  params[QStringLiteral( "height" )] = H;
  params[QStringLiteral( "bands" )] = B;
  params[QStringLiteral( "dtype" )] = static_cast<int>( SharedMemorySegment::DType::Float32 );

  QJsonObject result;
  bool isError = false;
  const AwaitStatus status = node->server->sendRequestSync(
    QStringLiteral( "shm.read" ), params, result, isError, 10000 );

  REQUIRE( status == AwaitStatus::Ok );
  REQUIRE_FALSE( isError );
  REQUIRE( result[QStringLiteral( "status" )].toString() == QStringLiteral( "ok" ) );
  REQUIRE( result[QStringLiteral( "checksum" )].toDouble() == Approx( expectedSum ).margin( 1e-3 ) );
}

TEST_CASE( "SharedMemorySegment: uint8 dtype round-trips correctly", "[python][shm]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString loadError;
  PythonPluginAdapter *pluginAdapter = host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &loadError );
  REQUIRE( pluginAdapter != nullptr );

  WorkerNode *node = pluginAdapter->workerNode();
  REQUIRE( node != nullptr );
  REQUIRE( node->server->hasClient() );

  // 8x8x1 uint8: values 0..63
  constexpr int W = 8, H = 8, B = 1;
  std::vector<uint8_t> data( W * H * B );
  std::iota( data.begin(), data.end(), 0 );
  const double expectedSum = static_cast<double>( ( W * H - 1 ) ) * ( W * H ) / 2.0;

  SharedMemorySegment seg;
  REQUIRE( seg.create( W, H, B, SharedMemorySegment::DType::UInt8 ) );
  REQUIRE( seg.write( data.data(), data.size() * sizeof( uint8_t ) ) );

  QJsonObject params;
  params[QStringLiteral("key")] = seg.nativeKey();
  params[QStringLiteral( "width" )] = W;
  params[QStringLiteral( "height" )] = H;
  params[QStringLiteral( "bands" )] = B;
  params[QStringLiteral( "dtype" )] = static_cast<int>( SharedMemorySegment::DType::UInt8 );

  QJsonObject result;
  bool isError = false;
  const AwaitStatus status = node->server->sendRequestSync(
    QStringLiteral( "shm.read" ), params, result, isError, 10000 );

  REQUIRE( status == AwaitStatus::Ok );
  REQUIRE_FALSE( isError );
  REQUIRE( result[QStringLiteral( "checksum" )].toDouble() == Approx( expectedSum ).margin( 1e-3 ) );
}

// ---------------------------------------------------------------------------
// ADR 0064 follow-up — lifetime correctness. A segment created and read once
// must not leave a backing object behind in /dev/shm once the owning side has
// finished. Before the unlink fix this leaks: each create() mints a new POSIX
// shm object that nobody unlinks, so /dev/shm/sicnu_shm_* grows without bound.
// ---------------------------------------------------------------------------
TEST_CASE( "SharedMemorySegment: no /dev/shm leak after a read round-trip and detach",
           "[python][shm][lifetime]" )
{
  // Snapshot any pre-existing sicnu_shm_* entries so the assertion is robust
  // to other tests running concurrently in the same /dev/shm.
  const auto baseline = QDir( QStringLiteral( "/dev/shm" ) )
                          .entryList( QStringList() << QStringLiteral( "sicnu_shm_*" ) );

  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString loadError;
  PythonPluginAdapter *pluginAdapter = host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &loadError );
  REQUIRE( pluginAdapter != nullptr );

  WorkerNode *node = pluginAdapter->workerNode();
  REQUIRE( node != nullptr );
  REQUIRE( node->server->hasClient() );

  constexpr int W = 12, H = 12, B = 1;
  std::vector<float> data( W * H * B );
  std::iota( data.begin(), data.end(), 0.0f );
  const double expectedSum = static_cast<double>( ( W * H - 1 ) * ( W * H ) / 2.0 );

  SharedMemorySegment seg;
  REQUIRE( seg.create( W, H, B, SharedMemorySegment::DType::Float32 ) );
  REQUIRE( seg.write( data.data(), data.size() * sizeof( float ) ) );

  QJsonObject params;
  params[QStringLiteral( "key" )] = seg.nativeKey();
  params[QStringLiteral( "width" )] = W;
  params[QStringLiteral( "height" )] = H;
  params[QStringLiteral( "bands" )] = B;
  params[QStringLiteral( "dtype" )] = static_cast<int>( SharedMemorySegment::DType::Float32 );

  QJsonObject result;
  bool isError = false;
  const AwaitStatus status = node->server->sendRequestSync(
    QStringLiteral( "shm.read" ), params, result, isError, 10000 );
  REQUIRE( status == AwaitStatus::Ok );
  REQUIRE_FALSE( isError );
  REQUIRE( result[QStringLiteral( "checksum" )].toDouble() == Approx( expectedSum ).margin( 1e-3 ) );

  // The owning side is done with the segment; releasing it must reclaim the
  // POSIX shm object (the leak this test guards against).
  seg.detach();

  // Give the kernel/Python side a brief moment to settle any close/unlink.
  for ( int attempt = 0; attempt < 40; ++attempt )
  {
    const auto now = QDir( QStringLiteral( "/dev/shm" ) )
                       .entryList( QStringList() << QStringLiteral( "sicnu_shm_*" ) );
    // Subtract entries that existed before this test ran.
    auto remaining = now;
    for ( const QString &b : baseline )
      remaining.removeAll( b );
    if ( remaining.isEmpty() )
      break;
    QThread::msleep( 25 );
  }

  auto after = QDir( QStringLiteral( "/dev/shm" ) )
                 .entryList( QStringList() << QStringLiteral( "sicnu_shm_*" ) );
  for ( const QString &b : baseline )
    after.removeAll( b );
  REQUIRE( after.isEmpty() );
}

// ---------------------------------------------------------------------------
// ADR 0064 — concurrent isolation across the worker pool. The production model
// is one dedicated worker (and IPC socket) per plugin, so true concurrency is
// across distinct workers, not on one socket. We acquire N workers from the
// pool and have N threads each create its own segment and read it through its
// own worker's IPC server. Every checksum must match with no cross-segment
// corruption — the invariant the unique-key + per-segment lifetime design
// exists to provide.
// ---------------------------------------------------------------------------
TEST_CASE( "SharedMemorySegment: N distinct segments on N workers read concurrently all checksum correctly",
           "[python][shm][concurrency]" )
{
  constexpr int N = 3;
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( N + 1 ); // room for the plugin's worker + N-1 more

  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString loadError;
  PythonPluginAdapter *pluginAdapter = host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &loadError );
  REQUIRE( pluginAdapter != nullptr );

  // Gather N workers, each with its own IPC server. The plugin already holds
  // one; acquire the rest from the pool.
  std::vector<WorkerNode *> nodes;
  nodes.push_back( pluginAdapter->workerNode() );
  for ( int i = 1; i < N; ++i )
  {
    WorkerNode *wn = host.pool()->acquireWorker();
    REQUIRE( wn != nullptr );
    nodes.push_back( wn );
  }
  for ( WorkerNode *wn : nodes )
    REQUIRE( wn->server->hasClient() );

  constexpr int W = 10, H = 10, B = 1;

  struct Segment
  {
    SharedMemorySegment seg;
    std::vector<float> data;
    double expectedSum = 0.0;
  };
  std::vector<Segment> segments( N );
  for ( int i = 0; i < N; ++i )
  {
    auto &s = segments[i];
    s.data.resize( W * H * B );
    // Distinct fill per segment so cross-contamination would change the sum.
    std::iota( s.data.begin(), s.data.end(), static_cast<float>( i * 1000 ) );
    s.expectedSum = 0.0;
    for ( float v : s.data ) s.expectedSum += v;
    REQUIRE( s.seg.create( W, H, B, SharedMemorySegment::DType::Float32 ) );
    REQUIRE( s.seg.write( s.data.data(), s.data.size() * sizeof( float ) ) );
  }

  std::vector<double> checksums( N, -1.0 );
  std::vector<AwaitStatus> statuses( N, AwaitStatus::Disconnected );
  std::vector<std::thread> threads;
  for ( int i = 0; i < N; ++i )
  {
    threads.emplace_back( [&, i]() {
      QJsonObject params;
      params[QStringLiteral( "key" )] = segments[i].seg.nativeKey();
      params[QStringLiteral( "width" )] = W;
      params[QStringLiteral( "height" )] = H;
      params[QStringLiteral( "bands" )] = B;
      params[QStringLiteral( "dtype" )] = static_cast<int>( SharedMemorySegment::DType::Float32 );
      QJsonObject result;
      bool isError = true;
      statuses[i] = nodes[i]->server->sendRequestSync(
        QStringLiteral( "shm.read" ), params, result, isError, 15000 );
      if ( statuses[i] == AwaitStatus::Ok && !isError )
        checksums[i] = result[QStringLiteral( "checksum" )].toDouble();
    } );
  }
  for ( auto &t : threads ) t.join();

  for ( int i = 0; i < N; ++i )
  {
    INFO( "segment " << i << " status=" << static_cast<int>( statuses[i] ) );
    REQUIRE( statuses[i] == AwaitStatus::Ok );
    REQUIRE( checksums[i] == Approx( segments[i].expectedSum ).margin( 1e-3 ) );
  }

  // Release the workers we acquired directly (the plugin owns the first).
  for ( int i = 1; i < N; ++i )
    host.pool()->releaseWorker( nodes[i] );
}

// ---------------------------------------------------------------------------
// ADR 0064 — zero-copy throughput proof. Round-trips a large raster block
// (2048x2048 float32 ≈ 16 MiB, and an int32 variant) through the shared
// memory channel and reports wall-clock throughput. Correctness is asserted
// to the byte (Python's arr.sum() == independent C++ sum).
//
// What "copy overhead ≈ 0" means here, and how this test encodes it: the only
// per-byte CPU copy on the data path is the single SharedMemorySegment::write()
// memcpy into the segment (unavoidable — the source vector must land in shm).
// Python mounts the payload with numpy.ndarray(..., buffer=shm.buf, offset=32)
// with NO copy, and computes arr.sum() directly over the shared mapping. The
// reported total round-trip time is therefore dominated by the IPC socket +
// the Python sum, not by redundant buffer copies; throughput is printed so a
// regression (e.g. an accidental copy landing on the path) shows up as a
// large drop versus the baseline. This is a functional+timing assertion, not a
// microbenchmark of memcpy itself.
// ---------------------------------------------------------------------------
TEST_CASE( "SharedMemorySegment: large-matrix zero-copy round-trip throughput",
           "[python][shm][benchmark]" )
{
  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString loadError;
  PythonPluginAdapter *pluginAdapter = host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &loadError );
  REQUIRE( pluginAdapter != nullptr );

  WorkerNode *node = pluginAdapter->workerNode();
  REQUIRE( node != nullptr );
  REQUIRE( node->server->hasClient() );

  auto roundTrip = [&]( int W, int H, int B, SharedMemorySegment::DType dtype, double &outThroughputMiBps )
  {
    const size_t elemSize = SharedMemorySegment::dtypeSize( dtype );
    const size_t bytes = static_cast<size_t>( W ) * H * B * elemSize;

    SharedMemorySegment seg;
    REQUIRE( seg.create( W, H, B, dtype ) );

    // Fill with a known pattern and compute the reference sum in C++.
    double expectedSum = 0.0;
    if ( dtype == SharedMemorySegment::DType::Float32 )
    {
      std::vector<float> data( W * H * B );
      for ( size_t i = 0; i < data.size(); ++i )
      {
        data[i] = static_cast<float>( i % 1000 );
        expectedSum += data[i];
      }
      REQUIRE( seg.write( data.data(), bytes ) );
    }
    else // Int32
    {
      std::vector<int32_t> data( W * H * B );
      for ( size_t i = 0; i < data.size(); ++i )
      {
        data[i] = static_cast<int32_t>( i % 1000 );
        expectedSum += data[i];
      }
      REQUIRE( seg.write( data.data(), bytes ) );
    }

    QJsonObject params;
    params[QStringLiteral( "key" )] = seg.nativeKey();
    params[QStringLiteral( "width" )] = W;
    params[QStringLiteral( "height" )] = H;
    params[QStringLiteral( "bands" )] = B;
    params[QStringLiteral( "dtype" )] = static_cast<int>( dtype );

    QJsonObject result;
    bool isError = false;
    const auto t0 = std::chrono::steady_clock::now();
    const AwaitStatus status = node->server->sendRequestSync(
      QStringLiteral( "shm.read" ), params, result, isError, 60000 );
    const auto t1 = std::chrono::steady_clock::now();
    REQUIRE( status == AwaitStatus::Ok );
    REQUIRE_FALSE( isError );

    // Zero-copy correctness: Python summed the shared buffer directly; it must
    // match the C++ reference to the byte (margin covers float accumulation).
    REQUIRE( result[QStringLiteral( "checksum" )].toDouble() == Approx( expectedSum ).epsilon( 1e-6 ) );

    const double secs = std::chrono::duration<double>( t1 - t0 ).count();
    const double mib = static_cast<double>( bytes ) / ( 1024.0 * 1024.0 );
    outThroughputMiBps = secs > 0.0 ? mib / secs : 0.0;
    seg.detach();
  };

  double f32Throughput = 0.0, i32Throughput = 0.0;
  SECTION( "float32 2048x2048" ) { roundTrip( 2048, 2048, 1, SharedMemorySegment::DType::Float32, f32Throughput ); }
  SECTION( "int32 2048x2048" ) { roundTrip( 2048, 2048, 1, SharedMemorySegment::DType::Int32, i32Throughput ); }

  // Report (visible in the test's -s success output / CI logs). No hard
  // threshold assertion: the value is host-dependent; the point is to surface
  // a baseline and catch regressions. Correctness above is the real gate.
  if ( f32Throughput > 0.0 )
    std::cout << "[benchmark] shm zero-copy float32 2048x2048 (16 MiB): "
              << f32Throughput << " MiB/s round-trip" << std::endl;
  if ( i32Throughput > 0.0 )
    std::cout << "[benchmark] shm zero-copy int32 2048x2048 (16 MiB): "
              << i32Throughput << " MiB/s round-trip" << std::endl;
}

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

#include <numeric>
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

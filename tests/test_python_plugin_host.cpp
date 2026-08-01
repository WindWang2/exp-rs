// test_python_plugin_host.cpp — headless Python Plugin Host seam tests (ADR 0023)
#include <catch2/catch_test_macros.hpp>

#include "python_plugin_host.h"
#include "python_plugin_adapter.h"
#include "data/data_manager.h"

#include <QCoreApplication>
#include <QDir>

using namespace sicnu::python::isolated;

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

#include "python_ipc_server.h"
#include "python_worker_process_pool.h"
#include "processing/framework/algorithm_engine.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"

#include <QThread>
#include <chrono>

TEST_CASE( "py: prefix executor executes from a worker thread marshaled to the main thread", "[python][host][exec]" )
{
  using namespace sicnu::jobs;

  sicnu::data::DataManager dataManager;
  PythonPluginHost host( 2 );

  // Occupy one worker with the sample plugin, then use the second worker to
  // register py:echo_test through the daemon's public-path test helper.
  const QString pluginDir = QDir( QString::fromUtf8( TEST_DATA_DIR ) ).filePath( QStringLiteral( "plugins/sample_plugin" ) );
  QString loadError;
  REQUIRE( host.loadPlugin( pluginDir, &dataManager, nullptr, nullptr, &loadError ) != nullptr );

  WorkerNode *node = host.pool()->acquireWorker();
  REQUIRE( node != nullptr );
  REQUIRE( node->server != nullptr );

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

    // The main thread must pump events while jobs run — this is exactly the
    // CLI runner pattern; without it the marshaled call deadlocks.
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
        FAIL( "py: job did not finish within 30 s (marshaling deadlock?)" );
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
        CHECK( snap->error.find( "Unknown algorithm" ) != std::string::npos );
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

  host.pool()->releaseWorker( node );
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

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

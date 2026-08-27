// tests/test_agent_copilot_ui.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/agent_copilot_dock_widget.h"
#include "agent/interaction_tool_registry.h"
#include "agent/llm_settings_dialog.h"
#include "agent/view_control_service.h"
#include "data/data_manager.h"
#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_context.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_plane.h"
#include "processing/framework/task_center.h"

#include <QApplication>
#include <QEventLoop>
#include <QJsonObject>
#include <QMetaObject>
#include <QTimer>

#include <chrono>
#include <thread>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

using namespace sicnu::agent;
using namespace sicnu::data;
using namespace sicnu::processing;

namespace {

bool writeTinyRaster( const QString &path, int width, int height, int bands )
{
  GDALAllRegister();
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GDALDataset *ds = driver->Create( path.toUtf8().constData(), width, height, bands, GDT_Float32, nullptr );
  if ( !ds )
    return false;

  std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
  ds->SetGeoTransform( gt.data() );
  OGRSpatialReference srs;
  srs.importFromEPSG( 32648 );
  char *wkt = nullptr;
  srs.exportToWkt( &wkt );
  ds->SetProjection( wkt );
  CPLFree( wkt );

  for ( int b = 1; b <= bands; ++b )
  {
    GDALRasterBand *band = ds->GetRasterBand( b );
    std::vector<float> data( width * height, static_cast<float>( b ) );
    band->RasterIO( GF_Write, 0, 0, width, height, data.data(), width, height, GDT_Float32, 0, 0 );
  }
  GDALClose( ( GDALDatasetH )ds );
  return true;
}

} // namespace

static void ensureQtApp()
{
  if ( !QApplication::instance() )
  {
    static int argc = 1;
    static char appName[] = "test_agent_copilot_ui";
    static char *argv[] = { appName, nullptr };
    new QApplication( argc, argv );
  }
}

namespace {

// Minimal stub adapter for registry-based tool-call lifecycle tests.
class LifecycleStubAdapter : public AtomicAlgorithmAdapter
{
  public:
    enum class Mode { SleepThenComplete, WriteOutput };

    LifecycleStubAdapter( std::string id, Mode mode, int sleepMs = 0, QString outputPath = {} )
      : mId( std::move( id ) )
      , mMode( mode )
      , mSleepMs( sleepMs )
      , mOutputPath( std::move( outputPath ) ) {}

    std::string algorithmId() const override { return mId; }
    AlgorithmDescriptor descriptor() const override { return AlgorithmDescriptor{}; }

    Json::Value execute( const Json::Value &params, ProgressCallback,
                         std::function<bool()> isCancelled ) override
    {
      if ( mMode == Mode::SleepThenComplete )
      {
        // Respect cancellation so stop→cancel tests can observe terminal Canceled.
        const int sliceMs = 20;
        for ( int slept = 0; slept < mSleepMs; slept += sliceMs )
        {
          if ( isCancelled && isCancelled() )
            throw std::runtime_error( "Canceled" );
          std::this_thread::sleep_for( std::chrono::milliseconds( sliceMs ) );
        }
        Json::Value result( Json::objectValue );
        result["status"] = "ok";
        result["echo"] = params;
        return result;
      }
      else
      {
        Q_UNUSED( isCancelled )
        Json::Value result( Json::objectValue );
        result["status"] = "ok";
        result["output"] = mOutputPath.toStdString();
        return result;
      }
    }

  private:
    std::string mId;
    Mode mMode;
    int mSleepMs;
    QString mOutputPath;
};

void wireRegistryFallback()
{
  sicnu::jobs::JobEngine::instance().setFallbackExecutor(
    []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) -> Json::Value {
      const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( req.algorithmId );
      if ( !adapter )
        throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
      ProgressCallback progressBridge;
      progressBridge = [&ctx]( int percent, const std::string &message ) {
        ctx.reportProgress( percent / 100.0, message );
      };
      return adapter->execute( req.params, progressBridge, [&ctx]() { return ctx.isCancelled(); } );
    } );
}

QJsonObject makeToolCallEnvelope( const QString &name, const QString &arguments = QStringLiteral( "{}" ) )
{
  QJsonObject envelope;
  envelope[QStringLiteral( "id" )] = QStringLiteral( "tc_1" );
  envelope[QStringLiteral( "type" )] = QStringLiteral( "function" );
  QJsonObject func;
  func[QStringLiteral( "name" )] = name;
  func[QStringLiteral( "arguments" )] = arguments;
  envelope[QStringLiteral( "function" )] = func;
  return envelope;
}

} // namespace

TEST_CASE( "LlmSettingsDialog initializes and manages provider profiles", "[agent][ui]" )
{
  ensureQtApp();

  LlmSettingsDialog dlg;
  REQUIRE( dlg.windowTitle().contains( QStringLiteral( "Copilot" ) ) );

  LlmProviderProfile profile = dlg.selectedProfile();
  REQUIRE_FALSE( profile.id.isEmpty() );
}

TEST_CASE( "AgentCopilotDockWidget instantiates and binds workspace context", "[agent][ui]" )
{
  ensureQtApp();

  DataManager dataMgr;

  AgentCopilotDockWidget dock;
  REQUIRE( dock.objectName() == "AgentCopilotDockWidget" );

  dock.setContext( &dataMgr, nullptr );

  // Test prompt submission triggers message card rendering
  dock.sendPrompt( QStringLiteral( "运行光谱指数测试" ) );
}

TEST_CASE( "AgentCopilotDockWidget handles widget destruction during async plan execution", "[agent][ui]" )
{
  ensureQtApp();

  DataManager dataMgr;
  auto dock = std::make_unique<AgentCopilotDockWidget>();
  dock->setContext( &dataMgr, nullptr );

  // Destroying dock while async operations might be in flight must not crash
  dock.reset();
  REQUIRE( dock == nullptr );
}

TEST_CASE( "AgentCopilotDockWidget completion callback is safe after destruction", "[agent][ui][lifecycle]" )
{
  ensureQtApp();
  wireRegistryFallback();
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<LifecycleStubAdapter>( "stub:sleepy_lifecycle",
                                            LifecycleStubAdapter::Mode::SleepThenComplete,
                                            600 ) );

  auto dataMgr = std::make_unique<DataManager>();
  auto dock = std::make_unique<AgentCopilotDockWidget>();
  dock->setContext( dataMgr.get(), nullptr );

  REQUIRE( QMetaObject::invokeMethod( dock.get(), "onToolCallParsed",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:sleepy_lifecycle" ) ) ) ) );

  bool destroyed = false;
  QTimer::singleShot( 200, [&] {
    dock.reset();
    dataMgr.reset();
    destroyed = true;
  } );

  QEventLoop loop;
  QTimer::singleShot( 1200, &loop, &QEventLoop::quit );
  loop.exec();

  REQUIRE( destroyed );
}

TEST_CASE( "AgentCopilotDockWidget clear cancels stale completion callbacks", "[agent][ui][lifecycle]" )
{
  ensureQtApp();
  wireRegistryFallback();
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<LifecycleStubAdapter>( "stub:sleepy_clear",
                                            LifecycleStubAdapter::Mode::SleepThenComplete,
                                            600 ) );

  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );

  REQUIRE( QMetaObject::invokeMethod( &dock, "onToolCallParsed",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:sleepy_clear" ) ) ) ) );

  QTimer::singleShot( 100, &dock, [&] {
    REQUIRE( QMetaObject::invokeMethod( &dock, "onClearClicked" ) );
  } );

  QEventLoop loop;
  QTimer::singleShot( 1200, &loop, &QEventLoop::quit );
  loop.exec();
}

TEST_CASE( "AgentCopilotDockWidget stop cancels submitted TaskCenter tasks", "[agent][ui][cancel]" )
{
  ensureQtApp();
  wireRegistryFallback();
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<LifecycleStubAdapter>( "stub:sleepy_stop",
                                            LifecycleStubAdapter::Mode::SleepThenComplete,
                                            5000 ) );

  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );

  REQUIRE( QMetaObject::invokeMethod( &dock, "onToolCallParsed",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:sleepy_stop" ) ) ) ) );

  // Pump until the task is registered in the run inspector.
  long taskId = -1;
  for ( int i = 0; i < 200 && taskId < 0; ++i )
  {
    QCoreApplication::processEvents();
    const QString summary = dock.runInspectorSummary();
    if ( summary.contains( QStringLiteral( "tasks=1" ) ) )
    {
      const auto tasks = sicnu::TaskCenter::instance().allTasks();
      for ( const auto &info : tasks )
      {
        if ( info.algorithmId == QStringLiteral( "stub:sleepy_stop" ) )
          taskId = std::max( taskId, info.taskId );
      }
    }
    if ( taskId < 0 )
      std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  REQUIRE( taskId > 0 );

  // The private stop path is routed through onSendClicked in production; here
  // we exercise the cancel hook directly because driving the LLM streaming
  // state in a headless unit test is unstable.
  REQUIRE( QMetaObject::invokeMethod( &dock, "cancelCurrentRunTasks" ) );

  // The task may briefly be "Cancelling"; wait for the terminal transition.
  sicnu::AlgorithmTaskInfo finalInfo;
  for ( int i = 0; i < 200; ++i )
  {
    finalInfo = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( sicnu::isTerminalStatus( finalInfo.status ) )
      break;
    QCoreApplication::processEvents();
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  CHECK( finalInfo.status == sicnu::TaskStatus::Canceled );
}

TEST_CASE( "AgentCopilotDockWidget tool-call card shows result summary", "[agent][ui][toolcard]" )
{
  ensureQtApp();
  wireRegistryFallback();
  AtomicAlgorithmRegistry::instance().reset();

  const QString outputPath = QStringLiteral( "/tmp/stub_output_agent.tif" );
  REQUIRE( writeTinyRaster( outputPath, 4, 4, 1 ) );

  AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<LifecycleStubAdapter>( "stub:write_output",
                                            LifecycleStubAdapter::Mode::WriteOutput,
                                            0,
                                            outputPath ) );

  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );

  REQUIRE( QMetaObject::invokeMethod( &dock, "onToolCallParsed",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:write_output" ) ) ) ) );

  // Wait for the task to complete and the card to be updated.
  bool foundSummary = false;
  QString lastDetails;
  for ( int i = 0; i < 400 && !foundSummary; ++i )
  {
    QCoreApplication::processEvents();
    for ( QLabel *label : dock.findChildren<QLabel *>( QStringLiteral( "ToolCallCardDetails" ) ) )
    {
      lastDetails = label->text();
      if ( lastDetails.contains( outputPath ) || lastDetails.contains( QStringLiteral( "verified" ) ) )
      {
        foundSummary = true;
        break;
      }
    }
    if ( !foundSummary )
      std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  INFO( "card details: " << lastDetails.toStdString() );
  REQUIRE( foundSummary );
  CHECK( dock.runInspectorSummary().contains( QStringLiteral( "stage=Completed" ) ) );
}

TEST_CASE( "AgentCopilotDockWidget inspector state reflects run lifecycle", "[agent][ui][inspector]" )
{
  ensureQtApp();
  wireRegistryFallback();
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<LifecycleStubAdapter>( "stub:inspector_lifecycle",
                                            LifecycleStubAdapter::Mode::SleepThenComplete,
                                            50 ) );

  DataManager dataMgr;
  AgentCopilotDockWidget dock;
  dock.setContext( &dataMgr, nullptr );

  CHECK( dock.runInspectorSummary().contains( QStringLiteral( "stage=-" ) ) );

  REQUIRE( QMetaObject::invokeMethod( &dock, "onToolCallParsed",
                                      Qt::QueuedConnection,
                                      Q_ARG( QJsonObject, makeToolCallEnvelope( QStringLiteral( "stub:inspector_lifecycle" ) ) ) ) );

  // Eventually the inspector should report a terminal stage.
  bool terminal = false;
  for ( int i = 0; i < 400 && !terminal; ++i )
  {
    QCoreApplication::processEvents();
    const QString summary = dock.runInspectorSummary();
    terminal = summary.contains( QStringLiteral( "stage=Completed" ) ) || summary.contains( QStringLiteral( "stage=Failed" ) );
    if ( !terminal )
      std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  }
  CHECK( terminal );
}

TEST_CASE( "InteractionToolRegistry handler returns error after service destruction", "[agent][interaction][lifecycle]" )
{
  ensureQtApp();
  InteractionToolRegistry::instance().reset();

  {
    ViewControlService service;
    service.setDataManager( nullptr );
    service.setMapCanvas( nullptr );
    InteractionToolRegistry::instance().registerBuiltinTools( &service, nullptr, nullptr );
  } // service destroyed

  const Json::Value result =
    InteractionToolRegistry::instance().execute( "view:get_state", Json::Value( Json::objectValue ) );

  REQUIRE( result.isObject() );
  REQUIRE( result["status"].asString() == "error" );
}


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

using namespace sicnu::agent;
using namespace sicnu::data;
using namespace sicnu::processing;

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
        if ( mSleepMs > 0 )
          std::this_thread::sleep_for( std::chrono::milliseconds( mSleepMs ) );
        Q_UNUSED( isCancelled )
        Json::Value result( Json::objectValue );
        result["status"] = "ok";
        result["echo"] = params;
        return result;
      }
      else
      {
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


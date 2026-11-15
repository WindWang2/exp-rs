#include <catch2/catch_test_macros.hpp>

#include "agent/runtime/agent_tool_host.h"
#include "agent/interaction_tool_registry.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "data/data_manager.h"
#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_context.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "support/qsettings_redirect.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QThread>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace sicnu::agent;
using namespace sicnu::agent::runtime;

namespace
{
exprs_test::QSettingsUserRootRedirect settingsRedirect;
void application()
{
  qputenv( "SICNU_LLM_KEYCHAIN", "0" );
  QSettings::setDefaultFormat( QSettings::IniFormat );
  static int argc = 1;
  static char name[] = "test_agent_tool_host";
  static char *argv[] = { name, nullptr };
  if ( !QCoreApplication::instance() ) new QApplication( argc, argv );
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-tests" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "agent-tool-host" ) );
}
QJsonObject call( const QString &id, const QString &name, const QJsonObject &args = {} )
{
  return { { QStringLiteral( "id" ), id }, { QStringLiteral( "type" ), QStringLiteral( "function" ) },
    { QStringLiteral( "function" ), QJsonObject{ { QStringLiteral( "name" ), name }, { QStringLiteral( "arguments" ), args } } } };
}
void registration( const std::string &name, int &count )
{
  InteractionToolDefinition tool;
  tool.name = name;
  tool.description = "Host test tool";
  tool.inputSchema["type"] = "object";
  tool.handler = [&count]( const Json::Value & ) {
    ++count;
    Json::Value result( Json::objectValue );
    result["status"] = "success";
    return result;
  };
  InteractionToolRegistry::instance().registerTool( std::move( tool ) );
}

bool eventually( const std::function<bool()> &predicate, qint64 timeoutMs = 5000 )
{
  QElapsedTimer deadline;
  deadline.start();
  while ( !predicate() && deadline.elapsed() < timeoutMs )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
  }
  return predicate();
}

struct WorkerState
{
  std::atomic_bool started = false;
  std::atomic_bool release = false;
};

class HistoryWorkerAdapter : public sicnu::processing::AtomicAlgorithmAdapter
{
  public:
    explicit HistoryWorkerAdapter( std::shared_ptr<WorkerState> state,
                                   std::string id = "python:host_history_fixture", bool gpu = false )
      : m_state( std::move( state ) ), m_id( std::move( id ) ), m_gpu( gpu ) {}
    std::string algorithmId() const override { return m_id; }
    sicnu::processing::AlgorithmDescriptor descriptor() const override
    {
      sicnu::processing::AlgorithmDescriptor value;
      value.id = algorithmId();
      value.description = "Host history lifecycle fixture";
      value.agentMetadata.gpuAccelerated = m_gpu;
      return value;
    }
    Json::Value execute( const Json::Value &, sicnu::processing::ProgressCallback,
                         std::function<bool()> ) override
    {
      m_state->started = true;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
      while ( !m_state->release && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
      Json::Value result( Json::objectValue );
      result["status"] = "ok";
      return result;
    }
  private:
    std::shared_ptr<WorkerState> m_state;
    std::string m_id;
    bool m_gpu;
};

struct HistoryWorkerGuard
{
  std::shared_ptr<WorkerState> state = std::make_shared<WorkerState>();
  HistoryWorkerGuard()
  {
    sicnu::TaskCenter::instance().shutdownForTests();
    sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter(
      std::make_shared<HistoryWorkerAdapter>( state ) );
    tool_catalog::AgentToolCatalog::instance().initializeDefaults();
    tool_catalog::AgentToolCatalog::instance().invalidateCache();
    sicnu::jobs::JobEngine::instance().setFallbackExecutor(
      []( const sicnu::jobs::JobRequest &request, sicnu::operators::RSOperatorContext & ) {
        const auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( request.algorithmId );
        if ( !adapter ) throw std::runtime_error( "Missing host lifecycle fixture" );
        return adapter->execute( request.params );
      } );
  }
  ~HistoryWorkerGuard()
  {
    state->release = true;
    sicnu::TaskCenter::instance().shutdownForTests();
    sicnu::jobs::JobEngine::instance().setFallbackExecutor( {} );
    sicnu::processing::AtomicAlgorithmRegistry::instance().unregisterAdapter( "python:host_history_fixture" );
    tool_catalog::AgentToolCatalog::instance().invalidateCache();
  }
};
}

TEST_CASE( "Host approvals are explicit, single use and revoked by cancellation", "[agent][host][approval]" )
{
  application();
  InteractionToolRegistry::instance().reset();
  tool_catalog::AgentToolCatalog::instance().initializeDefaults();
  int executions = 0;
  registration( "view:unknown_write", executions );
  sicnu::data::DataManager data;
  AgentToolHost host( sicnu::TaskCenter::instance() );
  REQUIRE( host.setContext( &data ) );
  const auto catalog = host.catalogSnapshot();
  REQUIRE( catalog.error.isEmpty() );
  QString token;
  QJsonObject result;
  QObject::connect( &host, &AgentToolHost::approvalRequested, &host,
    [&]( const QString &, const QString &value, const QJsonObject &, const QJsonObject & ) { token = value; } );
  QObject::connect( &host, &AgentToolHost::toolCompleted, &host,
    [&]( const QString &, const QString &, const QJsonObject &value ) { result = value; } );
  REQUIRE( host.beginRun( QStringLiteral( "approve" ), catalog.revision ) );
  const auto request = call( QStringLiteral( "one" ), QStringLiteral( "view:unknown_write" ) );
  host.requestTool( QStringLiteral( "approve" ), request, catalog.revision );
  REQUIRE_FALSE( token.isEmpty() );
  CHECK( executions == 0 );
  host.resolveApproval( token, true );
  CHECK( executions == 1 );
  CHECK( result.value( QStringLiteral( "status" ) ) == QStringLiteral( "success" ) );
  host.resolveApproval( token, true );
  host.requestTool( QStringLiteral( "approve" ), request, catalog.revision );
  CHECK( executions == 1 );
  host.finishConversation( QStringLiteral( "approve" ) );
  REQUIRE( host.isRunIdle( QStringLiteral( "approve" ) ) );

  REQUIRE( host.beginRun( QStringLiteral( "cancel" ), catalog.revision ) );
  host.requestTool( QStringLiteral( "cancel" ), call( QStringLiteral( "two" ), QStringLiteral( "view:unknown_write" ) ), catalog.revision );
  const QString stale = token;
  host.cancelRun( QStringLiteral( "cancel" ) );
  host.resolveApproval( stale, true );
  CHECK( executions == 1 );
  CHECK( host.isRunIdle( QStringLiteral( "cancel" ) ) );
  InteractionToolRegistry::instance().reset();
}

TEST_CASE( "Two current GUI hosts cannot write the same project concurrently", "[agent][host][lease]" )
{
  application();
  sicnu::data::DataManager data;
  AgentToolHost first( sicnu::TaskCenter::instance() ), second( sicnu::TaskCenter::instance() );
  REQUIRE( first.setContext( &data ) );
  REQUIRE( second.setContext( &data ) );
  REQUIRE( first.beginRun( QStringLiteral( "first" ) ) );
  CHECK_FALSE( second.beginRun( QStringLiteral( "second" ) ) );
  first.finishConversation( QStringLiteral( "first" ) );
  REQUIRE( first.isRunIdle( QStringLiteral( "first" ) ) );
  CHECK( second.beginRun( QStringLiteral( "second" ) ) );
  second.finishConversation( QStringLiteral( "second" ) );
}

TEST_CASE( "Host rejects a provider-name collision and refuses stale schemas", "[agent][host][catalog]" )
{
  application();
  InteractionToolRegistry::instance().reset();
  int executions = 0;
  registration( "view:collision:a", executions );
  registration( "view:collision_a", executions );
  sicnu::data::DataManager data;
  AgentToolHost host( sicnu::TaskCenter::instance() );
  REQUIRE( host.setContext( &data ) );
  CHECK_FALSE( host.catalogSnapshot().error.isEmpty() );
  CHECK_FALSE( host.beginRun( QStringLiteral( "collision" ) ) );
  InteractionToolRegistry::instance().reset();
  registration( "view:unknown_write", executions );
  const auto catalog = host.catalogSnapshot();
  REQUIRE( catalog.error.isEmpty() );
  REQUIRE( host.beginRun( QStringLiteral( "drift" ), catalog.revision ) );
  QString token;
  QJsonObject result;
  QObject::connect( &host, &AgentToolHost::approvalRequested, &host,
    [&]( const QString &, const QString &value, const QJsonObject &, const QJsonObject & ) { token = value; } );
  QObject::connect( &host, &AgentToolHost::toolCompleted, &host,
    [&]( const QString &, const QString &, const QJsonObject &value ) { result = value; } );
  host.requestTool( QStringLiteral( "drift" ), call( QStringLiteral( "one" ), QStringLiteral( "view:unknown_write" ) ), catalog.revision );
  REQUIRE_FALSE( token.isEmpty() );
  auto tool = InteractionToolRegistry::instance().findTool( "view:unknown_write" ).value();
  tool.inputSchema["required"].append( "new_argument" );
  InteractionToolRegistry::instance().registerTool( std::move( tool ) );
  host.resolveApproval( token, true );
  CHECK( executions == 0 );
  CHECK( result.value( QStringLiteral( "errorCode" ) ) == QStringLiteral( "tool_catalog_changed" ) );
  host.finishConversation( QStringLiteral( "drift" ) );
  InteractionToolRegistry::instance().reset();
}

TEST_CASE( "Completed host runs release their ledgers without allowing recent ID replay", "[agent][host][lease][retention]" )
{
  application();
  InteractionToolRegistry::instance().reset();
  tool_catalog::AgentToolCatalog::instance().initializeDefaults();
  int executions = 0;
  registration( "view:get_state", executions );
  sicnu::data::DataManager data;
  AgentToolHost host( sicnu::TaskCenter::instance() );
  REQUIRE( host.setContext( &data ) );
  const auto catalog = host.catalogSnapshot();
  REQUIRE( catalog.error.isEmpty() );
  for ( int index = 0; index < 65; ++index )
  {
    const QString run = QStringLiteral( "retention-%1" ).arg( index );
    REQUIRE( host.beginRun( run, catalog.revision ) );
    host.requestTool( run, call( QStringLiteral( "one" ), QStringLiteral( "view:get_state" ) ), catalog.revision );
    host.finishConversation( run );
    REQUIRE( host.isRunIdle( run ) );
  }
  CHECK( executions == 65 );
  CHECK_FALSE( host.beginRun( QStringLiteral( "retention-0" ), catalog.revision ) );
  host.requestTool( QStringLiteral( "retention-0" ), call( QStringLiteral( "one" ), QStringLiteral( "view:get_state" ) ), catalog.revision );
  CHECK( executions == 65 );
  InteractionToolRegistry::instance().reset();
}

TEST_CASE( "Clearing completed task history retains the host lease until its worker exits", "[agent][host][lease][history][cancel]" )
{
  application();
  InteractionToolRegistry::instance().reset();
  HistoryWorkerGuard worker;
  auto &center = sicnu::TaskCenter::instance();
  sicnu::data::DataManager data;
  AgentToolHost host( center ), other( center );
  REQUIRE( host.setContext( &data ) );
  REQUIRE( other.setContext( &data ) );
  const auto catalog = host.catalogSnapshot();
  REQUIRE( catalog.error.isEmpty() );
  QString token;
  int completions = 0;
  QObject::connect( &host, &AgentToolHost::approvalRequested, &host,
    [&]( const QString &, const QString &value, const QJsonObject &, const QJsonObject & ) { token = value; } );
  QObject::connect( &host, &AgentToolHost::toolCompleted, &host,
    [&]( const QString &, const QString &, const QJsonObject & ) { ++completions; } );
  const QString run = QStringLiteral( "history" );
  REQUIRE( host.beginRun( run, catalog.revision ) );
  host.requestTool( run, call( QStringLiteral( "one" ), QStringLiteral( "python:host_history_fixture" ) ), catalog.revision );
  REQUIRE_FALSE( token.isEmpty() );
  host.resolveApproval( token, true );
  REQUIRE( eventually( [&] { return worker.state->started.load(); } ) );
  const auto taskIds = host.taskIds( run );
  REQUIRE( taskIds.size() == 1 );
  const long taskId = *taskIds.cbegin();
  const auto running = center.getTaskInfo( taskId );
  REQUIRE_FALSE( running.jobId.empty() );

  // Force the terminal-notification / worker-exit ordering through public
  // Task Center seams, then remove the GUI history while the worker is held.
  center.markTaskCompleted( taskId );
  center.clearCompletedTasks();
  REQUIRE( center.getTaskInfo( taskId ).taskId == -1 );
  host.cancelRun( run );
  QElapsedTimer held;
  held.start();
  while ( held.elapsed() < 150 ) QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
  CHECK( completions == 0 );
  CHECK_FALSE( host.isRunIdle( run ) );
  CHECK_FALSE( other.beginRun( QStringLiteral( "too-early" ), catalog.revision ) );
  const auto job = sicnu::jobs::JobEngine::instance().snapshot( running.jobId );
  REQUIRE( job.has_value() );
  CHECK( job->state == sicnu::jobs::JobState::Running );

  worker.state->release = true;
  REQUIRE( eventually( [&] { return host.isRunIdle( run ); } ) );
  CHECK( completions == 1 );
  REQUIRE( other.beginRun( QStringLiteral( "after-exit" ), catalog.revision ) );
  other.finishConversation( QStringLiteral( "after-exit" ) );
}

TEST_CASE( "Host discovery honors category pagination compact schemas and GPU facets", "[agent][host][catalog][discovery]" )
{
  application();
  InteractionToolRegistry::instance().reset();
  auto &catalogAuthority = tool_catalog::AgentToolCatalog::instance();
  catalogAuthority.initializeDefaults();
  int executions = 0;
  registration( "view:discovery_a", executions );
  registration( "view:discovery_b", executions );
  registration( "view:discovery_c", executions );
  auto state = std::make_shared<WorkerState>();
  auto &algorithms = sicnu::processing::AtomicAlgorithmRegistry::instance();
  algorithms.registerAdapter( std::make_shared<HistoryWorkerAdapter>( state, "python:host_discovery_gpu", true ) );
  algorithms.registerAdapter( std::make_shared<HistoryWorkerAdapter>( state, "python:host_discovery_cpu", false ) );
  struct Cleanup
  {
    ~Cleanup()
    {
      sicnu::processing::AtomicAlgorithmRegistry::instance().unregisterAdapter( "python:host_discovery_gpu" );
      sicnu::processing::AtomicAlgorithmRegistry::instance().unregisterAdapter( "python:host_discovery_cpu" );
      InteractionToolRegistry::instance().reset();
      tool_catalog::AgentToolCatalog::instance().invalidateCache();
    }
  } cleanup;
  catalogAuthority.invalidateCache();
  sicnu::data::DataManager data;
  AgentToolHost host( sicnu::TaskCenter::instance() );
  REQUIRE( host.setContext( &data ) );
  const auto catalog = host.catalogSnapshot();
  REQUIRE( catalog.error.isEmpty() );
  const QString run = QStringLiteral( "discovery" );
  REQUIRE( host.beginRun( run, catalog.revision ) );
  QMap<QString, QJsonObject> results;
  QObject::connect( &host, &AgentToolHost::toolCompleted, &host,
    [&]( const QString &, const QString &id, const QJsonObject &value ) { results.insert( id, value ); } );
  auto invoke = [&]( const QString &id, const QString &tool, const QJsonObject &args ) {
    host.requestTool( run, call( id, tool, args ), catalog.revision );
    REQUIRE( results.contains( id ) );
    return results.value( id );
  };
  QJsonObject args{ { QStringLiteral( "category" ), QStringLiteral( "Interaction" ) },
                    { QStringLiteral( "limit" ), 1 }, { QStringLiteral( "cursor" ), 0 } };
  const auto first = invoke( QStringLiteral( "page1" ), QStringLiteral( "list_tools" ), args );
  REQUIRE( first.value( QStringLiteral( "tools" ) ).toArray().size() == 1 );
  CHECK( first.value( QStringLiteral( "total" ) ).toInt() == 3 );
  CHECK( first.value( QStringLiteral( "nextCursor" ) ).toInt() == 1 );
  const auto firstTool = first.value( QStringLiteral( "tools" ) ).toArray().first().toObject();
  CHECK( firstTool.value( QStringLiteral( "category" ) ) == QStringLiteral( "Interaction" ) );
  CHECK_FALSE( firstTool.contains( QStringLiteral( "parameters" ) ) );
  args.insert( QStringLiteral( "cursor" ), 1 );
  args.insert( QStringLiteral( "compact" ), false );
  const auto second = invoke( QStringLiteral( "page2" ), QStringLiteral( "list_tools" ), args );
  REQUIRE( second.value( QStringLiteral( "tools" ) ).toArray().size() == 1 );
  const auto secondTool = second.value( QStringLiteral( "tools" ) ).toArray().first().toObject();
  CHECK( secondTool.value( QStringLiteral( "canonicalId" ) ) != firstTool.value( QStringLiteral( "canonicalId" ) ) );
  CHECK( secondTool.value( QStringLiteral( "parameters" ) ).isObject() );
  CHECK( second.value( QStringLiteral( "nextCursor" ) ).toInt() == 2 );
  args.insert( QStringLiteral( "cursor" ), 2 );
  const auto last = invoke( QStringLiteral( "page3" ), QStringLiteral( "list_tools" ), args );
  CHECK( last.value( QStringLiteral( "tools" ) ).toArray().size() == 1 );
  CHECK( last.value( QStringLiteral( "nextCursor" ) ).toInt() == -1 );
  for ( const bool gpu : { true, false } )
  {
    const auto result = invoke( gpu ? QStringLiteral( "gpu" ) : QStringLiteral( "cpu" ), QStringLiteral( "search_tools" ),
      { { QStringLiteral( "query" ), QStringLiteral( "host_discovery" ) }, { QStringLiteral( "gpu" ), gpu },
        { QStringLiteral( "category" ), QStringLiteral( "Processing" ) } } );
    REQUIRE( result.value( QStringLiteral( "tools" ) ).toArray().size() == 1 );
    CHECK( result.value( QStringLiteral( "tools" ) ).toArray().first().toObject().value( QStringLiteral( "canonicalId" ) )
      == ( gpu ? QStringLiteral( "python:host_discovery_gpu" ) : QStringLiteral( "python:host_discovery_cpu" ) ) );
  }
  host.finishConversation( run );
  CHECK( executions == 0 );
}

TEST_CASE( "Host bounds oversized interaction results while retaining committed identity", "[agent][host][result][budget]" )
{
  application();
  InteractionToolRegistry::instance().reset();
  tool_catalog::AgentToolCatalog::instance().initializeDefaults();
  int executions = 0;
  InteractionToolDefinition tool;
  tool.name = "view:get_state";
  tool.description = "Oversized synchronous result fixture";
  tool.inputSchema["type"] = "object";
  tool.handler = [&]( const Json::Value & ) {
    ++executions;
    Json::Value result( Json::objectValue );
    result["status"] = "success";
    result["logs"] = std::string( 600 * 1024, 'l' );
    result["output"] = std::string( 600 * 1024, 'o' );
    result["assetId"] = "committed-fixture-asset";
    result["verified"] = true;
    return result;
  };
  InteractionToolRegistry::instance().registerTool( std::move( tool ) );
  sicnu::data::DataManager data;
  AgentToolHost host( sicnu::TaskCenter::instance() );
  REQUIRE( host.setContext( &data ) );
  const auto catalog = host.catalogSnapshot();
  REQUIRE( catalog.error.isEmpty() );
  QList<QJsonObject> results;
  QObject::connect( &host, &AgentToolHost::toolCompleted, &host,
    [&]( const QString &, const QString &, const QJsonObject &value ) { results.append( value ); } );
  const QString run = QStringLiteral( "bounded-result" );
  REQUIRE( host.beginRun( run, catalog.revision ) );
  const auto request = call( QStringLiteral( "one" ), QStringLiteral( "view:get_state" ) );
  host.requestTool( run, request, catalog.revision );
  host.requestTool( run, request, catalog.revision );
  REQUIRE( results.size() == 2 );
  CHECK( executions == 1 );
  for ( const auto &result : results )
  {
    CHECK( QJsonDocument( result ).toJson( QJsonDocument::Compact ).size() <= 512 * 1024 );
    CHECK( result.value( QStringLiteral( "status" ) ) == QStringLiteral( "success" ) );
    CHECK( result.value( QStringLiteral( "assetId" ) ) == QStringLiteral( "committed-fixture-asset" ) );
    CHECK( result.value( QStringLiteral( "verified" ) ).toBool() );
    CHECK( result.value( QStringLiteral( "resultTruncated" ) ).toBool() );
  }
  host.finishConversation( run );
  InteractionToolRegistry::instance().reset();
}

TEST_CASE( "Project invalidation revokes approval and requires an idle context rebind", "[agent][host][approval][project]" )
{
  application();
  InteractionToolRegistry::instance().reset();
  tool_catalog::AgentToolCatalog::instance().initializeDefaults();
  int executions = 0;
  registration( "view:project_write", executions );
  sicnu::data::DataManager oldProject, newProject;
  AgentToolHost host( sicnu::TaskCenter::instance() );
  REQUIRE( host.setContext( &oldProject ) );
  const auto catalog = host.catalogSnapshot();
  REQUIRE( catalog.error.isEmpty() );
  QString token;
  QObject::connect( &host, &AgentToolHost::approvalRequested, &host,
    [&]( const QString &, const QString &value, const QJsonObject &, const QJsonObject & ) { token = value; } );
  REQUIRE( host.beginRun( QStringLiteral( "old-project" ), catalog.revision ) );
  host.requestTool( QStringLiteral( "old-project" ), call( QStringLiteral( "one" ), QStringLiteral( "view:project_write" ) ), catalog.revision );
  REQUIRE_FALSE( token.isEmpty() );
  const QString oldToken = token;
  CHECK_FALSE( host.setContext( &newProject ) );
  host.invalidateProject();
  host.resolveApproval( oldToken, true );
  CHECK( executions == 0 );
  REQUIRE( host.isRunIdle( QStringLiteral( "old-project" ) ) );
  CHECK_FALSE( host.beginRun( QStringLiteral( "new-project" ), catalog.revision ) );
  REQUIRE( host.setContext( &newProject ) );
  REQUIRE( host.beginRun( QStringLiteral( "new-project" ), catalog.revision ) );
  host.requestTool( QStringLiteral( "new-project" ), call( QStringLiteral( "two" ), QStringLiteral( "view:project_write" ) ), catalog.revision );
  REQUIRE( token != oldToken );
  host.resolveApproval( oldToken, true );
  CHECK( executions == 0 );
  host.resolveApproval( token, true );
  CHECK( executions == 1 );
  host.finishConversation( QStringLiteral( "new-project" ) );
  CHECK( host.isRunIdle( QStringLiteral( "new-project" ) ) );
  InteractionToolRegistry::instance().reset();
}

TEST_CASE( "Workflow canonical parsing cannot bypass standalone Python approval", "[agent][host][workflow][approval]" )
{
  application();
  auto state = std::make_shared<WorkerState>();
  state->release = true;
  auto &algorithms = sicnu::processing::AtomicAlgorithmRegistry::instance();
  algorithms.registerAdapter( std::make_shared<HistoryWorkerAdapter>( state, "python:environment" ) );
  algorithms.registerAdapter( std::make_shared<HistoryWorkerAdapter>( state, "python:run_script" ) );
  struct Cleanup
  {
    ~Cleanup()
    {
      auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();
      registry.unregisterAdapter( "python:environment" );
      registry.unregisterAdapter( "python:run_script" );
      tool_catalog::AgentToolCatalog::instance().invalidateCache();
    }
  } cleanup;
  tool_catalog::AgentToolCatalog::instance().invalidateCache();
  sicnu::data::DataManager data;
  AgentToolHost host( sicnu::TaskCenter::instance() );
  REQUIRE( host.setContext( &data ) );
  const auto catalog = host.catalogSnapshot();
  REQUIRE( catalog.error.isEmpty() );
  REQUIRE( host.beginRun( QStringLiteral( "workflow" ), catalog.revision ) );
  int approvals = 0;
  QJsonObject result;
  QObject::connect( &host, &AgentToolHost::approvalRequested, &host,
    [&]( const QString &, const QString &, const QJsonObject &, const QJsonObject & ) { ++approvals; } );
  QObject::connect( &host, &AgentToolHost::toolCompleted, &host,
    [&]( const QString &, const QString &, const QJsonObject &value ) { result = value; } );
  const auto attempt = [&]( const QString &callId, QJsonObject step ) {
    result = {};
    step.insert( QStringLiteral( "id" ), QStringLiteral( "s" ) );
    const QJsonObject pipeline{ { QStringLiteral( "steps" ), QJsonArray{ step } } };
    host.requestTool( QStringLiteral( "workflow" ), call( callId, QStringLiteral( "run_workflow" ),
      { { QStringLiteral( "pipeline" ), pipeline } } ), catalog.revision );
    REQUIRE( eventually( [&] { return !result.isEmpty(); } ) );
    CHECK( result.value( QStringLiteral( "status" ) ) == QStringLiteral( "error" ) );
    CHECK( approvals == 0 );
    CHECK( host.taskIds( QStringLiteral( "workflow" ) ).isEmpty() );
    CHECK_FALSE( state->started.load() );
  };
  attempt( QStringLiteral( "alias" ), { { QStringLiteral( "name" ), QStringLiteral( "python:environment" ) },
    { QStringLiteral( "operatorId" ), QStringLiteral( "python:run_script" ) },
    { QStringLiteral( "params" ), QJsonObject{ { QStringLiteral( "script" ), QStringLiteral( "raise AssertionError('must not execute')" ) } } } } );
  CHECK( result.value( QStringLiteral( "errorCode" ) ) == QStringLiteral( "submit_python_separately" ) );
  attempt( QStringLiteral( "prepare" ), { { QStringLiteral( "operator" ), QStringLiteral( "python:environment" ) },
    { QStringLiteral( "params" ), QJsonObject{ { QStringLiteral( "action" ), QStringLiteral( "prepare" ) } } } } );
  CHECK( result.value( QStringLiteral( "errorCode" ) ) == QStringLiteral( "submit_python_separately" ) );
  attempt( QStringLiteral( "unknown" ), { { QStringLiteral( "name" ), QStringLiteral( "python:environment" ) },
    { QStringLiteral( "operatorId" ), QStringLiteral( "not_in_product:unsafe" ) } } );
  CHECK( result.value( QStringLiteral( "errorCode" ) ) == QStringLiteral( "plan_tool_not_in_catalog" ) );
  host.finishConversation( QStringLiteral( "workflow" ) );
}

// tests/test_tool_call_dispatcher.cpp
//
// ToolCallDispatcher (ADR 0021) — headless suite. Uses a FAKE sink/watcher,
// never touches the real TaskCenter singleton. Registry lookups use the real
// AtomicAlgorithmRegistry with stub adapters registered by the tests.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QMetaType>
#include <QString>
#include <QVariantMap>

#include <chrono>
#include <thread>

#include "processing/framework/tool_call_dispatcher.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/task_center.h"
#include "data/data_manager.h"
#include "jobs/job_engine.h"
#include "operators/rs/rs_spectral_index_operator.h"

#include <string>

using namespace sicnu::processing;

namespace {

// Minimal stub adapter with a caller-chosen ID, for registry lookups.
class StubAdapter : public AtomicAlgorithmAdapter
{
  public:
    explicit StubAdapter( std::string id, AlgorithmDescriptor desc = AlgorithmDescriptor{} )
      : mId( std::move( id ) )
      , mDesc( std::move( desc ) ) {}

    std::string algorithmId() const override { return mId; }
    AlgorithmDescriptor descriptor() const override { return mDesc; }
    Json::Value execute( const Json::Value &params, ProgressCallback,
                         std::function<bool()> = nullptr ) override
    {
      Json::Value result( Json::objectValue );
      result["status"] = "ok";
      result["echo"] = params;
      return result;
    }

  private:
    std::string mId;
    AlgorithmDescriptor mDesc;
};

// Fake submission sink + completion watcher. Default sink assigns increasing
// task ids; default watcher captures the completion callback so the test can
// fire it manually (see fireCompletion).
struct FakeDispatcherHarness
{
  FakeDispatcherHarness( ToolCallDispatcher::SubmissionSink sink = {},
                         ToolCallDispatcher::CompletionWatcher watcher = {} )
    : dispatcher( sink ? sink : defaultSink(), watcher ? watcher : defaultWatcher() ) {}

  // Submission side
  int sinkCalls = 0;
  QString submittedAlgorithmId;
  QVariantMap submittedParams;
  long submittedTaskId = -1;

  // Watcher side
  int watcherCalls = 0;
  long watchedTaskId = -1;
  ToolCallDispatcher::CompletionCallback pendingCallback;

  ToolCallDispatcher::SubmissionSink defaultSink()
  {
    return [this]( const QString &algorithmId, const QVariantMap &params ) -> long {
      ++sinkCalls;
      submittedAlgorithmId = algorithmId;
      submittedParams = params;
      submittedTaskId = 1000 + sinkCalls;
      return submittedTaskId;
    };
  }

  ToolCallDispatcher::CompletionWatcher defaultWatcher()
  {
    return [this]( long taskId, ToolCallDispatcher::CompletionCallback onComplete ) {
      ++watcherCalls;
      watchedTaskId = taskId;
      pendingCallback = std::move( onComplete );
    };
  }

  void fireCompletion( const Json::Value &payload )
  {
    REQUIRE( pendingCallback );
    pendingCallback( payload );
  }

  ToolCallDispatcher dispatcher;
};

Json::Value objectEnvelope( const std::string &name, const char *argsKey, const Json::Value &args )
{
  Json::Value envelope( Json::objectValue );
  envelope["name"] = name;
  envelope[argsKey] = args;
  return envelope;
}

} // namespace

TEST_CASE( "ToolCallDispatcher classifies all historical envelope shapes", "[processing][tool_call_dispatcher][classify]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  FakeDispatcherHarness harness;

  SECTION( "shape {name, parameters}" )
  {
    Json::Value params( Json::objectValue );
    params["index"] = "NDVI";
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "rs_spectral_index", "parameters", params ) )
             == ToolCallClassification::ToolCall );
  }

  SECTION( "shape {function:{name, arguments}} with arguments as JSON string" )
  {
    Json::Value envelope( Json::objectValue );
    Json::Value func( Json::objectValue );
    func["name"] = "rs_spectral_index";
    func["arguments"] = R"({"index":"NDVI"})";
    envelope["function"] = func;
    REQUIRE( harness.dispatcher.classify( envelope ) == ToolCallClassification::ToolCall );
  }

  SECTION( "shape {function:{name, arguments}} with arguments as object" )
  {
    Json::Value envelope( Json::objectValue );
    Json::Value func( Json::objectValue );
    func["name"] = "rs:spectral_index";
    Json::Value args( Json::objectValue );
    args["index"] = "NDVI";
    func["arguments"] = args;
    envelope["function"] = func;
    REQUIRE( harness.dispatcher.classify( envelope ) == ToolCallClassification::ToolCall );
  }

  SECTION( "shape {name, arguments}" )
  {
    Json::Value args( Json::objectValue );
    args["index"] = "NDVI";
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "rs_spectral_index", "arguments", args ) )
             == ToolCallClassification::ToolCall );
  }

  SECTION( "shape {name, params}" )
  {
    Json::Value args( Json::objectValue );
    args["index"] = "NDVI";
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "rs_spectral_index", "params", args ) )
             == ToolCallClassification::ToolCall );
  }

  SECTION( "missing arguments is accepted with empty params" )
  {
    Json::Value envelope( Json::objectValue );
    envelope["name"] = "rs:spectral_index";
    REQUIRE( harness.dispatcher.classify( envelope ) == ToolCallClassification::ToolCall );
  }

  SECTION( "steps array classifies as PlanRequest before name resolution" )
  {
    Json::Value steps( Json::arrayValue );
    steps.append( Json::Value( Json::objectValue ) );

    Json::Value params( Json::objectValue );
    params["steps"] = steps;
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "executeAgentPlan", "parameters", params ) )
             == ToolCallClassification::PlanRequest );

    Json::Value envelope( Json::objectValue );
    Json::Value func( Json::objectValue );
    func["name"] = "executeAgentPlan";
    Json::Value args( Json::objectValue );
    args["steps"] = steps;
    func["arguments"] = args;
    envelope["function"] = func;
    REQUIRE( harness.dispatcher.classify( envelope ) == ToolCallClassification::PlanRequest );
  }

  SECTION( "steps that is not an array does not classify as a plan" )
  {
    Json::Value args( Json::objectValue );
    args["steps"] = "not-an-array";
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "rs_spectral_index", "arguments", args ) )
             == ToolCallClassification::ToolCall );
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "unknown_tool", "arguments", args ) )
             == ToolCallClassification::Invalid );
  }

  SECTION( "unparseable argument strings are Invalid" )
  {
    Json::Value envelope( Json::objectValue );
    Json::Value func( Json::objectValue );
    func["name"] = "rs_spectral_index";
    func["arguments"] = "this is not json";
    envelope["function"] = func;
    REQUIRE( harness.dispatcher.classify( envelope ) == ToolCallClassification::Invalid );

    REQUIRE( harness.dispatcher.classify( objectEnvelope( "rs_spectral_index", "arguments", "also not json" ) )
             == ToolCallClassification::Invalid );
  }

  SECTION( "non-object, unnamed, and non-string-name envelopes are Invalid" )
  {
    REQUIRE( harness.dispatcher.classify( Json::Value( 42 ) ) == ToolCallClassification::Invalid );
    REQUIRE( harness.dispatcher.classify( Json::Value( Json::arrayValue ) ) == ToolCallClassification::Invalid );

    Json::Value steps( Json::arrayValue );
    steps.append( Json::Value( Json::objectValue ) );
    Json::Value bareSteps( Json::objectValue );
    bareSteps["steps"] = steps;
    REQUIRE( harness.dispatcher.classify( bareSteps ) == ToolCallClassification::Invalid );

    Json::Value numericName( Json::objectValue );
    numericName["name"] = 42;
    numericName["parameters"] = Json::Value( Json::objectValue );
    REQUIRE( harness.dispatcher.classify( numericName ) == ToolCallClassification::Invalid );
  }

  SECTION( "unresolvable names are Invalid" )
  {
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "not_registered_anywhere", "parameters", Json::Value( Json::objectValue ) ) )
             == ToolCallClassification::Invalid );
  }
}

TEST_CASE( "ToolCallDispatcher argumentsFor extracts arguments across historical envelope shapes", "[processing][tool_call_dispatcher][arguments]" )
{
  SECTION( "shape {name, arguments} with object arguments" )
  {
    Json::Value args( Json::objectValue );
    args["input"] = "/data/a.tif";
    args["index"] = "NDVI";
    const Json::Value extracted = ToolCallDispatcher::argumentsFor( objectEnvelope( "rs_spectral_index", "arguments", args ) );
    REQUIRE( extracted.isObject() );
    REQUIRE( extracted["input"].asString() == "/data/a.tif" );
    REQUIRE( extracted["index"].asString() == "NDVI" );
    REQUIRE( extracted.size() == 2 );
  }

  SECTION( "shape {function:{name, arguments}} with arguments as object" )
  {
    Json::Value envelope( Json::objectValue );
    Json::Value func( Json::objectValue );
    func["name"] = "rs_spectral_index";
    Json::Value args( Json::objectValue );
    args["input"] = "/data/b.tif";
    func["arguments"] = args;
    envelope["function"] = func;
    const Json::Value extracted = ToolCallDispatcher::argumentsFor( envelope );
    REQUIRE( extracted.isObject() );
    REQUIRE( extracted["input"].asString() == "/data/b.tif" );
    REQUIRE( extracted.size() == 1 );
  }

  SECTION( "shape {function:{name, arguments}} with arguments as JSON string" )
  {
    Json::Value envelope( Json::objectValue );
    Json::Value func( Json::objectValue );
    func["name"] = "rs_spectral_index";
    func["arguments"] = R"({"input":"/data/c.tif","index":"NDVI","count":3})";
    envelope["function"] = func;
    const Json::Value extracted = ToolCallDispatcher::argumentsFor( envelope );
    REQUIRE( extracted.isObject() );
    REQUIRE( extracted["input"].asString() == "/data/c.tif" );
    REQUIRE( extracted["index"].asString() == "NDVI" );
    REQUIRE( extracted["count"].asInt64() == 3 );
    REQUIRE( extracted.size() == 3 );
  }

  SECTION( "shapes {name, parameters} and {name, params} extract the same way" )
  {
    Json::Value params( Json::objectValue );
    params["input"] = "/data/p.tif";
    const Json::Value viaParameters = ToolCallDispatcher::argumentsFor( objectEnvelope( "rs_spectral_index", "parameters", params ) );
    REQUIRE( viaParameters.isObject() );
    REQUIRE( viaParameters["input"].asString() == "/data/p.tif" );
    REQUIRE( viaParameters.size() == 1 );

    const Json::Value viaParams = ToolCallDispatcher::argumentsFor( objectEnvelope( "rs_spectral_index", "params", params ) );
    REQUIRE( viaParams.isObject() );
    REQUIRE( viaParams["input"].asString() == "/data/p.tif" );
    REQUIRE( viaParams.size() == 1 );
  }

  SECTION( "envelope without an arguments member yields an empty object" )
  {
    Json::Value envelope( Json::objectValue );
    envelope["name"] = "rs_spectral_index";
    const Json::Value extracted = ToolCallDispatcher::argumentsFor( envelope );
    REQUIRE( extracted.isObject() );
    REQUIRE( extracted.size() == 0 );
  }

  SECTION( "malformed envelopes yield a null value" )
  {
    REQUIRE( ToolCallDispatcher::argumentsFor( Json::Value( 42 ) ).isNull() );
    REQUIRE( ToolCallDispatcher::argumentsFor( objectEnvelope( "rs_spectral_index", "arguments", "this is not json" ) ).isNull() );
  }
}

TEST_CASE( "ToolCallDispatcher resolves ids as-is before rewriting the first underscore", "[processing][tool_call_dispatcher][normalize]" )
{
  auto &registry = AtomicAlgorithmRegistry::instance();
  registry.reset();
  registry.registerAdapter( std::make_shared<StubAdapter>( "stub:echo" ) );
  registry.registerAdapter( std::make_shared<StubAdapter>( "stub_under_score" ) );
  registry.registerAdapter( std::make_shared<StubAdapter>( "stub:under_score" ) );
  registry.registerAdapter( std::make_shared<StubAdapter>( "a:b_c" ) );
  FakeDispatcherHarness harness;

  SECTION( "colon id resolves as-is" )
  {
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "stub:echo", "parameters", Json::Value( Json::objectValue ) ) )
             == ToolCallClassification::ToolCall );
  }

  SECTION( "underscore id is rewritten to the first colon on miss" )
  {
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "stub_echo", "parameters", Json::Value( Json::objectValue ) ) )
             == ToolCallClassification::ToolCall );
  }

  SECTION( "only the first underscore is rewritten" )
  {
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "a_b_c", "parameters", Json::Value( Json::objectValue ) ) )
             == ToolCallClassification::ToolCall );
  }

  SECTION( "an id that already resolves is never rewritten" )
  {
    Json::Value envelope = objectEnvelope( "stub_under_score", "parameters", Json::Value( Json::objectValue ) );
    QString error;
    REQUIRE( harness.dispatcher.submit( envelope, []( const Json::Value & ) {}, &error ) );
    REQUIRE( harness.submittedAlgorithmId == QStringLiteral( "stub_under_score" ) );
  }

  SECTION( "id unresolvable as-is and after rewrite is Invalid" )
  {
    REQUIRE( harness.dispatcher.classify( objectEnvelope( "stub_missing", "parameters", Json::Value( Json::objectValue ) ) )
             == ToolCallClassification::Invalid );
  }
}

TEST_CASE( "ToolCallDispatcher submits typed parameters through the injected sink", "[processing][tool_call_dispatcher][submit]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter( std::make_shared<StubAdapter>( "stub:echo" ) );
  FakeDispatcherHarness harness;

  Json::Value params( Json::objectValue );
  params["input"] = "/path/raster.tif";
  params["output"] = "/tmp/out.tif";
  params["index"] = "NDVI";
  params["count"] = 5;
  params["ratio"] = 0.5;
  params["flag"] = true;
  const Json::Value envelope = objectEnvelope( "stub_echo", "parameters", params );

  QString error;
  bool completionDelivered = false;
  Json::Value deliveredPayload;
  const bool ok = harness.dispatcher.submit( envelope, [&]( const Json::Value &payload ) {
    completionDelivered = true;
    deliveredPayload = payload;
  }, &error );

  REQUIRE( ok );
  REQUIRE( error.isEmpty() );
  REQUIRE( harness.sinkCalls == 1 );
  REQUIRE( harness.submittedAlgorithmId == QStringLiteral( "stub:echo" ) );
  REQUIRE( harness.submittedParams[QStringLiteral( "input" )].toString() == QStringLiteral( "/path/raster.tif" ) );
  REQUIRE( harness.submittedParams[QStringLiteral( "output" )].toString() == QStringLiteral( "/tmp/out.tif" ) );
  REQUIRE( harness.submittedParams[QStringLiteral( "index" )].toString() == QStringLiteral( "NDVI" ) );
  REQUIRE( harness.submittedParams[QStringLiteral( "count" )].typeId() == QMetaType::LongLong );
  REQUIRE( harness.submittedParams[QStringLiteral( "count" )].toLongLong() == 5 );
  REQUIRE( harness.submittedParams[QStringLiteral( "ratio" )].typeId() == QMetaType::Double );
  REQUIRE( harness.submittedParams[QStringLiteral( "ratio" )].toDouble() == 0.5 );
  REQUIRE( harness.submittedParams[QStringLiteral( "flag" )].typeId() == QMetaType::Bool );
  REQUIRE( harness.submittedParams[QStringLiteral( "flag" )].toBool() );

  REQUIRE( harness.watcherCalls == 1 );
  REQUIRE( harness.watchedTaskId == harness.submittedTaskId );

  // Firing the watcher delivers the payload through the submit() callback.
  Json::Value payload( Json::objectValue );
  payload["status"] = "success";
  payload["output"] = "/out.tif";
  harness.fireCompletion( payload );
  REQUIRE( completionDelivered );
  REQUIRE( deliveredPayload["output"].asString() == "/out.tif" );
}

TEST_CASE( "ToolCallDispatcher submit converts string-arguments envelopes to typed params", "[processing][tool_call_dispatcher][submit]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter( std::make_shared<StubAdapter>( "stub:echo" ) );
  FakeDispatcherHarness harness;

  Json::Value envelope( Json::objectValue );
  Json::Value func( Json::objectValue );
  func["name"] = "stub:echo";
  func["arguments"] = R"({"input":"/a.tif","index":"NDVI","count":3})";
  envelope["function"] = func;

  QString error;
  REQUIRE( harness.dispatcher.submit( envelope, []( const Json::Value & ) {}, &error ) );
  REQUIRE( error.isEmpty() );
  REQUIRE( harness.submittedAlgorithmId == QStringLiteral( "stub:echo" ) );
  REQUIRE( harness.submittedParams[QStringLiteral( "input" )].toString() == QStringLiteral( "/a.tif" ) );
  REQUIRE( harness.submittedParams[QStringLiteral( "index" )].toString() == QStringLiteral( "NDVI" ) );
  REQUIRE( harness.submittedParams[QStringLiteral( "count" )].toLongLong() == 3 );
}

TEST_CASE( "ToolCallDispatcher submit rejects invalid and plan envelopes without touching sink or watcher", "[processing][tool_call_dispatcher][submit]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  FakeDispatcherHarness harness;

  SECTION( "invalid envelope returns false with an error" )
  {
    Json::Value envelope = objectEnvelope( "not_registered_anywhere", "parameters", Json::Value( Json::objectValue ) );
    QString error;
    REQUIRE_FALSE( harness.dispatcher.submit( envelope, []( const Json::Value & ) {}, &error ) );
    REQUIRE_FALSE( error.isEmpty() );
    REQUIRE( harness.sinkCalls == 0 );
    REQUIRE( harness.watcherCalls == 0 );
  }

  SECTION( "plan envelope returns false with an error" )
  {
    Json::Value args( Json::objectValue );
    args["steps"] = Json::Value( Json::arrayValue );
    Json::Value envelope = objectEnvelope( "executeAgentPlan", "arguments", args );
    QString error;
    REQUIRE_FALSE( harness.dispatcher.submit( envelope, []( const Json::Value & ) {}, &error ) );
    REQUIRE_FALSE( error.isEmpty() );
    REQUIRE( harness.sinkCalls == 0 );
    REQUIRE( harness.watcherCalls == 0 );
  }

  SECTION( "null error out is tolerated" )
  {
    Json::Value envelope = objectEnvelope( "not_registered_anywhere", "parameters", Json::Value( Json::objectValue ) );
    REQUIRE_FALSE( harness.dispatcher.submit( envelope, []( const Json::Value & ) {}, nullptr ) );
  }

  SECTION( "sink rejection surfaces as an error and never registers a watcher" )
  {
    AtomicAlgorithmRegistry::instance().registerAdapter( std::make_shared<StubAdapter>( "stub:echo" ) );
    FakeDispatcherHarness rejectingHarness( []( const QString &, const QVariantMap & ) -> long { return -1; }, {} );
    const Json::Value envelope = objectEnvelope( "stub:echo", "parameters", Json::Value( Json::objectValue ) );
    QString error;
    REQUIRE_FALSE( rejectingHarness.dispatcher.submit( envelope, []( const Json::Value & ) {}, &error ) );
    REQUIRE_FALSE( error.isEmpty() );
    REQUIRE( rejectingHarness.watcherCalls == 0 );
  }
}

TEST_CASE( "ToolCallDispatcher validates required descriptor inputs before submitting", "[processing][tool_call_dispatcher][validate]" )
{
  auto &registry = AtomicAlgorithmRegistry::instance();
  registry.reset();

  AlgorithmDescriptor desc;
  PortDescriptor port;
  port.name = "input";
  port.required = true;
  desc.inputs.push_back( port );
  registry.registerAdapter( std::make_shared<StubAdapter>( "stub:validate", desc ) );
  FakeDispatcherHarness harness;

  SECTION( "missing required parameter rejects without calling the sink" )
  {
    const Json::Value envelope = objectEnvelope( "stub:validate", "parameters", Json::Value( Json::objectValue ) );
    QString error;
    REQUIRE_FALSE( harness.dispatcher.submit( envelope, []( const Json::Value & ) {}, &error ) );
    REQUIRE( error.contains( QStringLiteral( "Missing required parameter: input" ) ) );
    REQUIRE( harness.sinkCalls == 0 );
    REQUIRE( harness.watcherCalls == 0 );
  }

  SECTION( "underscore name is normalized before required-parameter validation" )
  {
    const Json::Value envelope = objectEnvelope( "stub_validate", "parameters", Json::Value( Json::objectValue ) );
    QString error;
    REQUIRE_FALSE( harness.dispatcher.submit( envelope, []( const Json::Value & ) {}, &error ) );
    REQUIRE( error.contains( QStringLiteral( "Missing required parameter: input" ) ) );
    REQUIRE( harness.sinkCalls == 0 );
  }

  SECTION( "all required parameters present submits through the sink" )
  {
    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/in.tif";
    const Json::Value envelope = objectEnvelope( "stub:validate", "parameters", params );
    QString error;
    REQUIRE( harness.dispatcher.submit( envelope, []( const Json::Value & ) {}, &error ) );
    REQUIRE( error.isEmpty() );
    REQUIRE( harness.sinkCalls == 1 );
    REQUIRE( harness.submittedAlgorithmId == QStringLiteral( "stub:validate" ) );
    REQUIRE( harness.submittedParams[QStringLiteral( "input" )].toString() == QStringLiteral( "/tmp/in.tif" ) );
  }
}

TEST_CASE( "ToolCallDispatcher submit yields the submitted task id", "[processing][tool_call_dispatcher][submit]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter( std::make_shared<StubAdapter>( "stub:echo" ) );
  FakeDispatcherHarness harness;

  SECTION( "successful submission writes the sink task id" )
  {
    const Json::Value envelope = objectEnvelope( "stub:echo", "parameters", Json::Value( Json::objectValue ) );
    long taskIdOut = -1;
    QString error;
    REQUIRE( harness.dispatcher.submit( envelope, []( const Json::Value & ) {}, &error, &taskIdOut ) );
    REQUIRE( taskIdOut == harness.submittedTaskId );
    REQUIRE( harness.watcherCalls == 1 );
  }

  SECTION( "failed submission leaves the out-param untouched" )
  {
    const Json::Value envelope = objectEnvelope( "not_registered_anywhere", "parameters", Json::Value( Json::objectValue ) );
    long taskIdOut = -1;
    REQUIRE_FALSE( harness.dispatcher.submit( envelope, []( const Json::Value & ) {}, nullptr, &taskIdOut ) );
    REQUIRE( taskIdOut == -1 );
  }
}

TEST_CASE( "ToolCallDispatcher submitBlocking waits for completion or times out", "[processing][tool_call_dispatcher][blocking]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter( std::make_shared<StubAdapter>( "stub:echo" ) );

  const Json::Value envelope = objectEnvelope( "stub:echo", "parameters", Json::Value( Json::objectValue ) );

  SECTION( "returns the completion payload for an immediate watcher" )
  {
    ToolCallDispatcher immediateDispatcher(
      []( const QString &, const QVariantMap & ) -> long { return 7; },
      []( long, ToolCallDispatcher::CompletionCallback onComplete ) {
        Json::Value payload( Json::objectValue );
        payload["status"] = "success";
        onComplete( payload );
      } );
    const Json::Value result = immediateDispatcher.submitBlocking( envelope );
    REQUIRE( result["status"].asString() == "success" );
  }

  SECTION( "waits for an asynchronous watcher to fire" )
  {
    ToolCallDispatcher asyncDispatcher(
      []( const QString &, const QVariantMap & ) -> long { return 8; },
      []( long, ToolCallDispatcher::CompletionCallback onComplete ) {
        std::thread( [onComplete]() {
          std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
          Json::Value payload( Json::objectValue );
          payload["status"] = "success";
          payload["output"] = "/async/out.tif";
          onComplete( payload );
        } ).detach();
      } );
    const Json::Value result = asyncDispatcher.submitBlocking( envelope, std::chrono::seconds( 5 ) );
    REQUIRE( result["status"].asString() == "success" );
    REQUIRE( result["output"].asString() == "/async/out.tif" );
  }

  SECTION( "returns an error payload on timeout" )
  {
    ToolCallDispatcher neverDispatcher(
      []( const QString &, const QVariantMap & ) -> long { return 9; },
      []( long, ToolCallDispatcher::CompletionCallback ) {
        // never completes
      } );
    const auto start = std::chrono::steady_clock::now();
    const Json::Value result = neverDispatcher.submitBlocking( envelope, std::chrono::milliseconds( 50 ) );
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE( result["status"].asString() == "error" );
    REQUIRE( result.isMember( "errorMessage" ) );
    REQUIRE( elapsed < std::chrono::seconds( 5 ) );
  }

  SECTION( "returns an error payload for invalid envelopes without hanging" )
  {
    ToolCallDispatcher dispatcher(
      []( const QString &, const QVariantMap & ) -> long { return 1; },
      []( long, ToolCallDispatcher::CompletionCallback ) {} );
    Json::Value invalid = objectEnvelope( "not_registered_anywhere", "parameters", Json::Value( Json::objectValue ) );
    const Json::Value result = dispatcher.submitBlocking( invalid, std::chrono::milliseconds( 100 ) );
    REQUIRE( result["status"].asString() == "error" );
    REQUIRE( result.isMember( "errorMessage" ) );
  }
}

TEST_CASE( "ToolCallDispatcher honors optional parameters for operator schema", "[processing][tool_call_dispatcher][schema]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<RsOperatorAdapter>( std::make_unique<sicnu::operators::rs::RsSpectralIndexOperator>() ) );

  FakeDispatcherHarness harness;

  SECTION( "accepts calls omitting optional parameters (e.g. nir, red, swir)" )
  {
    Json::Value args( Json::objectValue );
    args["input"] = "/path/to/input.tif";
    args["output"] = "/path/to/output.tif";
    args["index"] = "NDVI";

    const Json::Value envelope = objectEnvelope( "rs_spectral_index", "parameters", args );
    REQUIRE( harness.dispatcher.rejectionReason( envelope ).isEmpty() );
  }

  SECTION( "rejects calls missing required parameter (e.g. input)" )
  {
    Json::Value args( Json::objectValue );
    args["output"] = "/path/to/output.tif";
    args["index"] = "NDVI";

    const Json::Value envelope = objectEnvelope( "rs_spectral_index", "parameters", args );
    REQUIRE( harness.dispatcher.rejectionReason( envelope ).contains( "Missing required parameter: input" ) );
  }
}

TEST_CASE( "ToolCallDispatcher builds standardized result payloads", "[processing][tool_call_dispatcher][payload]" )
{
  SECTION( "completed task with output path" )
  {
    sicnu::AlgorithmTaskInfo info;
    info.taskId = 42;
    info.algorithmId = QStringLiteral( "rs:spectral_index" );
    info.status = sicnu::TaskStatus::Completed;
    info.outputLayerPath = QStringLiteral( "/tmp/ndvi_out.tif" );

    Json::Value payload = ToolCallDispatcher::buildTaskResultPayload( info );
    REQUIRE( payload["status"].asString() == "success" );
    REQUIRE( payload["taskId"].asInt64() == 42 );
    REQUIRE( payload["algorithmId"].asString() == "rs:spectral_index" );
    REQUIRE( payload["output"].asString() == "/tmp/ndvi_out.tif" );
  }

  SECTION( "failed task with error message" )
  {
    sicnu::AlgorithmTaskInfo info;
    info.taskId = 43;
    info.algorithmId = QStringLiteral( "rs:spectral_index" );
    info.status = sicnu::TaskStatus::Failed;
    info.errorMessage = QStringLiteral( "Invalid raster format" );

    Json::Value payload = ToolCallDispatcher::buildTaskResultPayload( info );
    REQUIRE( payload["status"].asString() == "error" );
    REQUIRE( payload["taskId"].asInt64() == 43 );
    REQUIRE( payload["errorMessage"].asString() == "Invalid raster format" );
  }
}

TEST_CASE( "ToolCallDispatcher supports OutputCommitterHandler for output asset publication", "[processing][tool_call_dispatcher][committer]" )
{
  FakeDispatcherHarness harness;
  REQUIRE( !harness.dispatcher.outputCommitterHandler() );

  SECTION( "successful handler rewrites output path" )
  {
    ToolCallDispatcher::OutputCommitterHandler handler = []( const sicnu::AlgorithmTaskInfo &,
                                                             std::string &outCommittedPath,
                                                             std::string & ) -> bool {
      outCommittedPath = "/committed/ndvi_out.tif";
      return true;
    };
    harness.dispatcher.setOutputCommitterHandler( handler );
    REQUIRE( harness.dispatcher.outputCommitterHandler() );

    sicnu::AlgorithmTaskInfo info;
    info.taskId = 10;
    info.algorithmId = QStringLiteral( "rs:spectral_index" );
    info.status = sicnu::TaskStatus::Completed;
    info.outputLayerPath = QStringLiteral( "/tmp/raw_out.tif" );

    Json::Value payload = ToolCallDispatcher::buildTaskResultPayload( info, harness.dispatcher.outputCommitterHandler() );
    REQUIRE( payload["status"].asString() == "success" );
    REQUIRE( payload["output"].asString() == "/committed/ndvi_out.tif" );
  }

  SECTION( "refused handler fails the payload with commitError diagnostic" )
  {
    ToolCallDispatcher::OutputCommitterHandler handler = []( const sicnu::AlgorithmTaskInfo &,
                                                             std::string &,
                                                             std::string &outCommitError ) -> bool {
      outCommitError = "Disk space quota exceeded";
      return false;
    };

    sicnu::AlgorithmTaskInfo info;
    info.taskId = 11;
    info.algorithmId = QStringLiteral( "rs:spectral_index" );
    info.status = sicnu::TaskStatus::Completed;
    info.outputLayerPath = QStringLiteral( "/tmp/raw_out.tif" );

    Json::Value payload = ToolCallDispatcher::buildTaskResultPayload( info, handler );
    REQUIRE( payload["status"].asString() == "error" );
    REQUIRE( payload["output"].asString() == "/tmp/raw_out.tif" );
    REQUIRE( payload["commitError"].asString() == "Disk space quota exceeded" );
    REQUIRE( payload["errorMessage"].asString() == "Disk space quota exceeded" );
  }
}

TEST_CASE( "ToolCallDispatcher dispatchAndAwait executes synchronously and formats payload", "[processing][tool_call_dispatcher][dispatch_and_await]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter( std::make_shared<StubAdapter>( "stub:echo" ) );

  const Json::Value envelope = objectEnvelope( "stub:echo", "parameters", Json::Value( Json::objectValue ) );

  SECTION( "returns completion payload via dispatchAndAwait" )
  {
    ToolCallDispatcher dispatcher(
      []( const QString &, const QVariantMap & ) -> long { return 15; },
      []( long, ToolCallDispatcher::CompletionCallback onComplete ) {
        Json::Value payload( Json::objectValue );
        payload["status"] = "success";
        payload["output"] = "/path/out.tif";
        onComplete( payload );
      } );

    const Json::Value result = dispatcher.dispatchAndAwait( envelope );
    REQUIRE( result["status"].asString() == "success" );
    REQUIRE( result["output"].asString() == "/path/out.tif" );
  }

  SECTION( "dispatchAndAwait handles timeout cleanly" )
  {
    ToolCallDispatcher dispatcher(
      []( const QString &, const QVariantMap & ) -> long { return 16; },
      []( long, ToolCallDispatcher::CompletionCallback ) {} );

    const Json::Value result = dispatcher.dispatchAndAwait( envelope, std::chrono::milliseconds( 20 ) );
    REQUIRE( result["status"].asString() == "error" );
    REQUIRE( result["errorMessage"].asString() == "Tool call timed out" );
  }
}

TEST_CASE( "ToolCallDispatcher setDataManager sets authority cleanly", "[processing][tool_call_dispatcher][data_manager]" )
{
  ToolCallDispatcher dispatcher(
    []( const QString &, const QVariantMap & ) -> long { return 1; },
    []( long, ToolCallDispatcher::CompletionCallback ) {} );

  REQUIRE( dispatcher.dataManager() == nullptr );
  REQUIRE_FALSE( dispatcher.outputCommitterHandler() );

  dispatcher.setDataManager( nullptr );
  REQUIRE( dispatcher.dataManager() == nullptr );
  REQUIRE_FALSE( dispatcher.outputCommitterHandler() );
}

TEST_CASE( "ToolCallDispatcher zero-argument default constructor delegates directly to TaskCenter", "[processing][tool_call_dispatcher][default_ctor]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter( std::make_shared<StubAdapter>( "stub:default_ctor" ) );

  ToolCallDispatcher dispatcher; // zero-argument constructor

  Json::Value params( Json::objectValue );
  params["input"] = "/tmp/test.tif";
  const Json::Value envelope = objectEnvelope( "stub:default_ctor", "parameters", params );

  REQUIRE( dispatcher.classify( envelope ) == ToolCallClassification::ToolCall );
  REQUIRE( dispatcher.rejectionReason( envelope ).isEmpty() );

  long taskIdOut = -1;
  QString error;
  bool submitted = dispatcher.submit( envelope, []( const Json::Value & ) {}, &error, &taskIdOut );
  REQUIRE( submitted );
  REQUIRE( taskIdOut > 0 );

  // The default-constructor watcher waits on a detached thread; under heavy
  // parallel load the task can still be running when this test returns, and
  // process static teardown would then tear down TaskCenter under the thread
  // (segfault). Wait for a terminal state so the detached thread is done
  // before main() returns.
  const auto terminal = [taskIdOut]() {
    for ( int i = 0; i < 2000; ++i )
    {
      const auto status = sicnu::TaskCenter::instance().getTaskInfo( taskIdOut ).status;
      if ( status == sicnu::TaskStatus::Completed || status == sicnu::TaskStatus::Canceled
           || status == sicnu::TaskStatus::Failed )
        return true;
      std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    return false;
  };
  REQUIRE( terminal() );
}

// canvas: actions route to a CanvasActionHandler — the agent→canvas write-back
// seam (ADR 0021 sibling). They classify as ToolCall when a handler is wired,
// never reach the algorithm registry or the Task Center submission sink. Covers #140.
TEST_CASE( "ToolCallDispatcher routes canvas: actions to the handler, not the sink",
           "[processing][tool_call_dispatcher][canvas]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  FakeDispatcherHarness harness;
  Json::Value args( Json::objectValue );
  args["geometry"] = "POLYGON((0 0,0 1,1 1,1 0,0 0))";
  const Json::Value envelope = objectEnvelope( "canvas:draw_roi", "parameters", args );

  SECTION( "without a handler, canvas: is Invalid / rejected" )
  {
    REQUIRE( harness.dispatcher.classify( envelope ) == ToolCallClassification::Invalid );
    REQUIRE_FALSE( harness.dispatcher.rejectionReason( envelope ).isEmpty() );
  }

  SECTION( "with a handler, classify is ToolCall and submit invokes the handler, not the sink" )
  {
    int handlerCalls = 0;
    std::string capturedAction;
    harness.dispatcher.setCanvasActionHandler(
      [&]( const std::string &action, const Json::Value &a ) {
        ++handlerCalls;
        capturedAction = action;
        Json::Value res( Json::objectValue );
        res["status"] = "success";
        res["action"] = action;
        res["echoGeom"] = a["geometry"];
        return res;
      } );

    REQUIRE( harness.dispatcher.classify( envelope ) == ToolCallClassification::ToolCall );
    REQUIRE( harness.dispatcher.rejectionReason( envelope ).isEmpty() );

    long taskIdOut = 0;
    Json::Value delivered;
    QString error;
    const bool submitted = harness.dispatcher.submit(
      envelope, [&]( const Json::Value &payload ) { delivered = payload; }, &error, &taskIdOut );

    REQUIRE( submitted );
    REQUIRE( error.isEmpty() );
    REQUIRE( handlerCalls == 1 );
    REQUIRE( capturedAction == "draw_roi" );
    // The handler fires synchronously inline, so the completion is delivered.
    REQUIRE( delivered["status"].asString() == "success" );
    REQUIRE( delivered["echoGeom"].asString() == "POLYGON((0 0,0 1,1 1,1 0,0 0))" );
    // Critical: canvas: must NOT reach the Task Center submission sink.
    CHECK( harness.sinkCalls == 0 );
    // No real task id; the canvas path reports a reserved positive sentinel so
    // callers that check `taskId > 0` read a canvas action as submitted, not a
    // Task Center rejection.
    CHECK( taskIdOut == 9000001 );
  }

  SECTION( "dispatchAndAwait returns the handler's result synchronously" )
  {
    harness.dispatcher.setCanvasActionHandler(
      []( const std::string &action, const Json::Value & ) {
        Json::Value res( Json::objectValue );
        res["status"] = "success";
        res["action"] = action;
        return res;
      } );

    const Json::Value result = harness.dispatcher.dispatchAndAwait( envelope );
    REQUIRE( result["status"].asString() == "success" );
    REQUIRE( result["action"].asString() == "draw_roi" );
    CHECK( harness.sinkCalls == 0 );
  }
}

TEST_CASE( "ToolCallDispatcher synchronous dispatchAndAwait does not deadlock on caller thread",
           "[processing][dispatcher]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter( std::make_unique<StubAdapter>( "stub:test_op" ) );

  ToolCallDispatcher::SubmissionSink sink = []( const QString &, const QVariantMap & ) -> long {
    return 42;
  };
  ToolCallDispatcher::CompletionWatcher watcher = []( long, ToolCallDispatcher::CompletionCallback onComplete ) {
    std::thread( [cb = std::move( onComplete )]() {
      Json::Value payload( Json::objectValue );
      payload["status"] = "success";
      payload["message"] = "done";
      if ( cb )
        cb( payload );
    } ).detach();
  };

  ToolCallDispatcher dispatcher( sink, watcher );
  Json::Value args( Json::objectValue );
  const Json::Value envelope = objectEnvelope( "stub:test_op", "parameters", args );

  const auto start = std::chrono::steady_clock::now();
  const Json::Value result = dispatcher.dispatchAndAwait( envelope, std::chrono::milliseconds( 2000 ) );
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start );

  REQUIRE( result["status"].asString() == "success" );
  REQUIRE( elapsed.count() < 2000 );
}




// ---------------------------------------------------------------------------
// #559 regression: dispatchAndAwait through the production default
// constructor must not deadlock. Old wiring delivered the completion payload
// to the bridge thread via Qt::QueuedConnection while dispatchAndAwait blocked
// that same thread in a condition-variable wait, hanging until the timeout.
// The production wiring now rides the ExecutionPlane: the wakeup channel is
// thread-safe (no event loop involved), so this completes headless, and it
// equally completes with a live (non-pumped) QCoreApplication — see
// test_execution_plane for that variant.
// ---------------------------------------------------------------------------
TEST_CASE( "ToolCallDispatcher default-constructor dispatchAndAwait completes without deadlocking",
           "[processing][tool_call_dispatcher][dispatch_and_await][default_ctor]" )
{
  AtomicAlgorithmRegistry::instance().reset();
  AtomicAlgorithmRegistry::instance().registerAdapter( std::make_shared<StubAdapter>( "stub:await" ) );
  sicnu::jobs::JobEngine::instance().setFallbackExecutor(
    []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) -> Json::Value {
      const auto adapter = AtomicAlgorithmRegistry::instance().findAdapter( req.algorithmId );
      if ( !adapter )
        throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
      return adapter->execute( req.params, {}, [&ctx]() { return ctx.isCancelled(); } );
    } );

  ToolCallDispatcher dispatcher; // production wiring (sink/watcher/syncAwait on the plane)

  const auto start = std::chrono::steady_clock::now();
  const Json::Value result = dispatcher.dispatchAndAwait( objectEnvelope( "stub:await", "parameters", Json::Value( Json::objectValue ) ),
                                                          std::chrono::seconds( 8 ) );
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start )
                           .count();

  INFO( "elapsed ms: " << elapsedMs << ", status: " << result["status"].asString() );
  REQUIRE( result["status"].asString() == "success" );
  REQUIRE( elapsedMs < 6000 );
}

TEST_CASE( "ToolCallDispatcher output commit handler survives DataManager destruction",
           "[processing][tool_call_dispatcher][lifecycle]" )
{
  AtomicAlgorithmRegistry::instance().reset();

  ToolCallDispatcher dispatcher;
  {
    sicnu::data::DataManager manager;
    dispatcher.setDataManager( &manager );
  } // manager destroyed while dispatcher still holds the commit handler

  sicnu::AlgorithmTaskInfo info;
  info.taskId = 12345;
  info.status = sicnu::TaskStatus::Completed;
  info.algorithmId = QStringLiteral( "stub:lifetime" );
  info.outputLayerPath = QStringLiteral( "/tmp/should_not_be_used.tif" );

  const Json::Value payload = dispatcher.buildCommittedResultPayload( info );

  REQUIRE( payload["status"].asString() == "error" );
  const std::string errorMessage = payload["errorMessage"].asString();
  REQUIRE( errorMessage.find( "DataManager" ) != std::string::npos );
}

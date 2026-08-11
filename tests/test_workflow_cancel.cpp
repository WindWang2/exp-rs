// tests/test_workflow_cancel.cpp
//
// Workflow runtime cooperative cancellation: requestCancel() must be observed
// by a long-running operator step mid-run through RSOperatorContext.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <atomic>
#include <chrono>
#include <thread>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_runtime.h"

using namespace sicnu::workflow;
using namespace sicnu::operators;

namespace {

int &cancelAppArgc()
{
  static int argc = 1;
  return argc;
}
char cancelAppArgv0[] = "test_workflow_cancel";
char *cancelAppArgv[] = {cancelAppArgv0, nullptr};

void ensureQtApp()
{
  if ( !QCoreApplication::instance() )
    new QCoreApplication( cancelAppArgc(), cancelAppArgv );
}

/// Long-running operator that polls the cooperative cancel flag.
class SlowOperator : public RSOperator
{
public:
    std::string name() const override { return "test:slow"; }
    std::string displayName() const override { return "Slow"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Loops until cancelled."; }

    Json::Value schema() const override { return Json::Value( Json::objectValue ); }
    Json::Value metadata() const override
    {
        Json::Value meta( Json::objectValue );
        meta["supportsCancellation"] = true;
        return meta;
    }

    Json::Value run( const Json::Value &, RSOperatorContext &context ) override
    {
        for ( int i = 0; i < 1'000'000; ++i )
        {
            context.throwIfCancelled();
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        Json::Value result( Json::objectValue );
        result["result"] = "finished-unexpectedly";
        return result;
    }
};

} // namespace

TEST_CASE( "WorkflowRuntime::requestCancel aborts a running operator step", "[workflow][cancel]" )
{
    ensureQtApp();
    auto &reg = RSOperatorRegistry::instance();
    if ( !reg.hasOperator( "test:slow" ) )
    {
        reg.registerOperator( "test:slow", []() -> std::unique_ptr<RSOperator> {
            return std::make_unique<SlowOperator>();
        } );
    }

    WorkflowRuntime runtime( /*loadBuiltins=*/false );
    WorkflowDefinition def;
    def.id = "wf:cancel_test";
    def.title = "Cancel Test";
    StepDef step;
    step.id = "slow";
    step.title = "Slow";
    step.kind = StepKind::Operator;
    step.operatorId = "test:slow";
    def.steps.push_back( step );
    runtime.registerDefinition( def );

    const std::string sessionId = runtime.open( "wf:cancel_test" );
    REQUIRE_FALSE( sessionId.empty() );

    std::atomic<bool> started{ false };
    std::string runError;
    bool threw = false;

    std::thread runner( [&]() {
        started.store( true );
        try
        {
            runtime.runStep( sessionId, "slow" );
        }
        catch ( const std::exception &e )
        {
            threw = true;
            runError = e.what();
        }
    } );

    // Wait for the step to actually start, then cancel it.
    while ( !started.load() )
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
    runtime.requestCancel( sessionId );

    runner.join();

    REQUIRE( threw );
    REQUIRE( runError.find( "cancelled" ) != std::string::npos );
}

// tests/fixtures/isolation_plugin/isolation_plugin.cpp — fixture native
// plugin for the host-process runtime suites (isolation runtime 5.0).
// Operators deliberately exercise the crash/hang/flood/slow paths so the
// launcher's typed failures, kill ladder and restart policy can be tested
// against a REAL misbehaving plugin — the fixture itself is the adversary.
//
//   test:iso-echo    progress + echo (the healthy round-trip)
//   test:iso-crash   abort()s the worker process
//   test:iso-hang    spins until cancelled (deadline/kill-ladder target)
//   test:iso-flood   returns a 64 MiB string (frame-cap target)
//   test:iso-slow    sleeps 3 s, then returns (cancel target)
#include "exprs/plugin_interface.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <chrono>
#include <fstream>
#include <json/json.h>
#include <string>
#include <thread>

namespace {

using sicnu::operators::RSOperator;
using sicnu::operators::RSOperatorContext;

class EchoOperator : public RSOperator
{
public:
    std::string name() const override { return "test:iso-echo"; }
    std::string displayName() const override { return "Isolation Echo"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Host-process round-trip fixture"; }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
        context.reportProgress( 0.5, "halfway" );
        context.reportProgress( 1.0, "done" );
        Json::Value result( Json::objectValue );
        result["success"] = true;
        result["echo"] = params;
        result["worker"] = "isolation_plugin";
        return result;
    }
};

class CrashOperator : public RSOperator
{
public:
    std::string name() const override { return "test:iso-crash"; }
    std::string displayName() const override { return "Isolation Crash"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Aborts the worker process"; }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }

    Json::Value run( const Json::Value &, RSOperatorContext & ) override
    {
        std::fprintf( stderr, "isolation_plugin: deliberate crash\n" );
        std::fflush( stderr );
        // Hard fault (NOT std::abort): the debug CRT turns abort() into a
        // modal report dialog / cooperative exit that would not produce a
        // real crash — the suite needs a genuine fault for the kill and
        // recovery ladder.
#ifdef _WIN32
        *( volatile int * ) nullptr = 0;
#else
        std::abort();
#endif
        return Json::Value( Json::objectValue ); // not reached
    }
};

class HangOperator : public RSOperator
{
public:
    std::string name() const override { return "test:iso-hang"; }
    std::string displayName() const override { return "Isolation Hang"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Spins until cancelled"; }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }

    Json::Value run( const Json::Value &, RSOperatorContext &context ) override
    {
        while ( !context.isCancelled() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::Cancelled,
                                                 "hang released by cancel" );
    }
};

class FloodOperator : public RSOperator
{
public:
    std::string name() const override { return "test:iso-flood"; }
    std::string displayName() const override { return "Isolation Flood"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Returns a 64 MiB payload"; }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }

    Json::Value run( const Json::Value &, RSOperatorContext & ) override
    {
        Json::Value result( Json::objectValue );
        result["success"] = true;
        result["blob"] = std::string( 64L * 1024L * 1024L, 'f' );
        return result;
    }
};

class SlowOperator : public RSOperator
{
public:
    std::string name() const override { return "test:iso-slow"; }
    std::string displayName() const override { return "Isolation Slow"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Sleeps 3 s, then returns"; }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }

    Json::Value run( const Json::Value &, RSOperatorContext &context ) override
    {
        for ( int step = 0; step < 30 && !context.isCancelled(); ++step )
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        if ( context.isCancelled() )
            throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::Cancelled,
                                                     "slow operator cancelled" );
        Json::Value result( Json::objectValue );
        result["success"] = true;
        result["slept"] = true;
        return result;
    }
};

class IsolationPlugin : public exprs::PluginV1
{
public:
    std::string pluginId() const override { return "org.exprs.test.isolation-plugin"; }
    bool initialize( exprs::HostServicesV1 & ) override { return true; }

    void registerContributions( exprs::ContributionContextV1 &context ) override
    {
        auto factory = []( const char *id ) {
            return [id]() -> std::unique_ptr<RSOperator> {
                if ( std::string( id ) == "test:iso-echo" )
                    return std::make_unique<EchoOperator>();
                if ( std::string( id ) == "test:iso-crash" )
                    return std::make_unique<CrashOperator>();
                if ( std::string( id ) == "test:iso-hang" )
                    return std::make_unique<HangOperator>();
                if ( std::string( id ) == "test:iso-flood" )
                    return std::make_unique<FloodOperator>();
                return std::make_unique<SlowOperator>();
            };
        };
        context.registerOperatorFactory( "test:iso-echo", factory( "test:iso-echo" ) );
        context.registerOperatorFactory( "test:iso-crash", factory( "test:iso-crash" ) );
        context.registerOperatorFactory( "test:iso-hang", factory( "test:iso-hang" ) );
        context.registerOperatorFactory( "test:iso-flood", factory( "test:iso-flood" ) );
        context.registerOperatorFactory( "test:iso-slow", factory( "test:iso-slow" ) );
    }

    void shutdown() override { mShutdownCalled = true; }
    bool mShutdownCalled = false;
};

} // namespace

EXPRS_EXPORT_PLUGIN( IsolationPlugin )

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
//   test:iso-gate    blocks until N instances run CONCURRENTLY (protocol
//                    1.1 dispatch-width target; returns the observed peak)
//   test:iso-spawn   spawns a sleeping grandchild process (orphan-kill
//                    target for the process-group kill ladder)
#include "exprs/plugin_interface.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <json/json.h>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
        // Bounded, parameterized duration (protocol 1.1 suites use short
        // slices; default stays the historical 3 s).
        int seconds = params.get( "seconds", 3 ).asInt();
        seconds = std::max( 1, std::min( 30, seconds ) );
        for ( int step = 0; step < seconds * 10 && !context.isCancelled(); ++step )
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        if ( context.isCancelled() )
            throw sicnu::operators::RSOperatorError( sicnu::operators::ErrorCode::Cancelled,
                                                     "slow operator cancelled" );
        Json::Value result( Json::objectValue );
        result["success"] = true;
        result["slept"] = true;
        result["seconds"] = seconds;
        return result;
    }
};

/// Process-global rendezvous for the concurrency gate: counts instances
/// currently inside run() and tracks the peak.
class GateCounter
{
public:
    static GateCounter &instance()
    {
        static GateCounter counter;
        return counter;
    }

    void enter()
    {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( ++mActive > mPeak )
            mPeak = mActive;
        mCv.notify_all();
    }
    void leave()
    {
        std::lock_guard<std::mutex> lock( mMutex );
        --mActive;
        mCv.notify_all();
    }
    /// Waits until the running peak reaches @p expected (bounded).
    bool awaitPeak( int expected, int waitMs )
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( waitMs );
        std::unique_lock<std::mutex> lock( mMutex );
        while ( mPeak < expected && std::chrono::steady_clock::now() < deadline )
            mCv.wait_until( lock, deadline );
        return mPeak >= expected;
    }
    int peak() const
    {
        std::lock_guard<std::mutex> lock( mMutex );
        return mPeak;
    }

private:
    mutable std::mutex mMutex;
    std::condition_variable mCv;
    int mActive = 0;
    int mPeak = 0;
};

class GateOperator : public RSOperator
{
public:
    std::string name() const override { return "test:iso-gate"; }
    std::string displayName() const override { return "Isolation Gate"; }
    std::string group() const override { return "test"; }
    std::string description() const override
    {
        return "Blocks until N instances run concurrently; reports the peak";
    }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override
    {
        const int expected = params.get( "expected", 2 ).asInt();
        const int waitMs = params.get( "waitMs", 5000 ).asInt();
        GateCounter::instance().enter();
        // Report whether the rendezvous was reached before our slice ends.
        for ( int step = 0; step * 50 < waitMs && !context.isCancelled(); ++step )
        {
            if ( GateCounter::instance().peak() >= expected )
                break;
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
        const int peak = GateCounter::instance().peak();
        GateCounter::instance().leave();
        Json::Value result( Json::objectValue );
        result["success"] = true;
        result["expected"] = expected;
        result["observedPeak"] = peak;
        result["rendezvous"] = peak >= expected;
        return result;
    }
};

class SpawnChildOperator : public RSOperator
{
public:
    std::string name() const override { return "test:iso-spawn"; }
    std::string displayName() const override { return "Isolation Spawn"; }
    std::string group() const override { return "test"; }
    std::string description() const override
    {
        return "Spawns a sleeping grandchild; returns its pid (orphan-kill target)";
    }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }

    Json::Value run( const Json::Value &params, RSOperatorContext & ) override
    {
        const int childSeconds = params.get( "childSeconds", 120 ).asInt();
        Json::Value result( Json::objectValue );
#ifdef _WIN32
        (void)childSeconds;
        result["supported"] = false; // Windows lane covers the job object instead
#else
        const pid_t pid = ::fork();
        if ( pid == 0 )
        {
            // Grandchild: sleep in a signal-safe loop, then _exit.
            for ( int i = 0; i < childSeconds * 10; ++i )
            {
                struct timespec slice { 0, 100 * 1000 * 1000 };
                ::nanosleep( &slice, nullptr );
            }
            ::_exit( 0 );
        }
        if ( pid < 0 )
        {
            result["supported"] = false;
            return result;
        }
        result["supported"] = true;
        result["childPid"] = static_cast<Json::Int64>( pid );
        result["childSeconds"] = childSeconds;
        // Keep the pid findable for the orphan check even after a crash of
        // the worker: persist it next to the workDir-free params space.
        const char *marker = std::getenv( "SICNU_ISO_SPAWN_MARKER" );
        if ( marker && *marker )
        {
            std::ofstream out( marker, std::ios::trunc );
            out << pid << "\n";
        }
#endif
        result["success"] = true;
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
                if ( std::string( id ) == "test:iso-gate" )
                    return std::make_unique<GateOperator>();
                if ( std::string( id ) == "test:iso-spawn" )
                    return std::make_unique<SpawnChildOperator>();
                return std::make_unique<SlowOperator>();
            };
        };
        context.registerOperatorFactory( "test:iso-echo", factory( "test:iso-echo" ) );
        context.registerOperatorFactory( "test:iso-crash", factory( "test:iso-crash" ) );
        context.registerOperatorFactory( "test:iso-hang", factory( "test:iso-hang" ) );
        context.registerOperatorFactory( "test:iso-flood", factory( "test:iso-flood" ) );
        context.registerOperatorFactory( "test:iso-gate", factory( "test:iso-gate" ) );
        context.registerOperatorFactory( "test:iso-spawn", factory( "test:iso-spawn" ) );
        context.registerOperatorFactory( "test:iso-slow", factory( "test:iso-slow" ) );
    }

    void shutdown() override { mShutdownCalled = true; }
    bool mShutdownCalled = false;
};

} // namespace

EXPRS_EXPORT_PLUGIN( IsolationPlugin )

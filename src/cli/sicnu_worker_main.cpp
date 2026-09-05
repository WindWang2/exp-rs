// sicnu_worker_main.cpp — isolated local worker process (Data Plane 3.0,
// Phase K). Speaks worker_protocol v1 over stdin/stdout; executes RSOperators
// through the same registry as the CLI/desktop app. Crash in here never takes
// the host down — that is the entire point (FAILURE_MATRIX: SIGKILL worker,
// worker disconnect, GPU OOM inside the worker).
//
// Built-in test hooks (used by the fault-injection suite):
//   "__hang__"  — loops until cancelled (simulates an unresponsive operator)
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "runtime/worker/worker_protocol.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <json/json.h>

#include <atomic>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

namespace
{
bool readLine( std::string &line )
{
    if ( !std::getline( std::cin, line ) )
        return false;
    while ( !line.empty() && ( line.back() == '\r' || line.back() == '\n' ) )
        line.pop_back();
    return true;
}

bool writeLine( const std::string &line )
{
    std::fputs( ( line + "\n" ).c_str(), stdout );
    std::fflush( stdout );
    return true;
}

void emitReady()
{
    Json::Value frame;
    frame["v"] = 1;
    frame["op"] = "ready";
    writeLine( sicnu::runtime::worker::compactFrame( frame ) );
}

void emitFrame( const std::string &op, const std::string &jobId, const Json::Value &extra )
{
    Json::Value frame;
    frame["v"] = 1;
    frame["op"] = op;
    frame["jobId"] = jobId;
    if ( extra.isObject() )
        for ( const auto &key : extra.getMemberNames() )
            frame[key] = extra[key];
    writeLine( sicnu::runtime::worker::compactFrame( frame ) );
}

int runHangJob( const std::string &jobId, const std::atomic<bool> &cancel )
{
    emitFrame( "progress", jobId, Json::Value{} );
    while ( !cancel.load() )
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    emitFrame( "error", jobId, [] {
        Json::Value e;
        e["message"] = "cancelled";
        return e;
    }() );
    return 0;
}
} // namespace

int main( int argc, char **argv )
{
    int protocolArg = 0; // position of "--protocol" if present
    for ( int i = 1; i < argc - 1; ++i )
        if ( std::string( argv[i] ) == "--protocol" )
            protocolArg = i + 1;
    if ( !protocolArg || std::string( argv[protocolArg] ) != "1" )
    {
        std::fputs( "{\"v\":1,\"op\":\"error\",\"jobId\":\"\",\"message\":"
                    "\"protocol version unsupported\"}\n",
                    stdout );
        return 2;
    }

    int qArgc = 1;
    char qArg0[] = "sicnu_worker";
    char *qArgv[] = { qArg0, nullptr };
    QCoreApplication app( qArgc, qArgv );

    sicnu::operators::RSOperatorRegistry::instance(); // call_once chain
    sicnu::operators::rs::installRsOperatorProvider();
    emitReady();

    std::atomic<bool> cancelFlag{ false };
    while ( true )
    {
        std::string line;
        if ( !readLine( line ) )
            break; // EOF = shutdown
        Json::Value frame;
        if ( !sicnu::runtime::worker::parseFrame( line, frame ) )
            continue;
        const std::string op = frame["op"].asString();
        const std::string jobId = frame["jobId"].asString();
        if ( op == "shutdown" )
            break;
        if ( op == "cancel" )
        {
            cancelFlag = true;
            continue;
        }
        if ( op != "run" )
            continue;

        const std::string algorithmId = frame["algorithmId"].asString();
        const Json::Value params = frame["params"];

        if ( algorithmId == "__hang__" )
        {
            cancelFlag = false;
            runHangJob( jobId, cancelFlag );
            continue;
        }

        try
        {
            auto operatorPtr = sicnu::operators::RSOperatorRegistry::instance().create( algorithmId );
            if ( !operatorPtr )
                throw std::runtime_error( "unknown algorithm: " + algorithmId );
            sicnu::operators::RSOperatorContext context;
            context.setCancelFlag( &cancelFlag );
            context.setProgressCallback( [&jobId]( double value, const std::string & ) {
                Json::Value extra;
                extra["value"] = value;
                emitFrame( "progress", jobId, extra );
            } );
            context.setLogCallback( []( const std::string &, const std::string & ) {} );
            const Json::Value payload = operatorPtr->run( params, context );
            emitFrame( "result", jobId, [] ( const Json::Value &p ) {
                Json::Value e;
                e["payload"] = p;
                return e;
            }( payload ) );
        }
        catch ( const std::exception &e )
        {
            Json::Value extra;
            extra["message"] = e.what();
            emitFrame( "error", jobId, extra );
        }
    }
    return 0;
}

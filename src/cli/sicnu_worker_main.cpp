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
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <json/json.h>

#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#if __has_include(<opencv2/core/utils/logger.hpp>)
#include <opencv2/core/utils/logger.hpp>
#define SICNU_WORKER_HAS_OPENCV_LOG 1
#endif

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
    // Capability advertisement (7.0): a legacy host ignores the "caps" member.
    writeLine( sicnu::runtime::worker::makeReadyFrame( {
        sicnu::runtime::worker::kWorkerCapProgress,
        sicnu::runtime::worker::kWorkerCapCancelAck,
        sicnu::runtime::worker::kWorkerCapStructuredErrors,
        sicnu::runtime::worker::kWorkerCapOutputIdentity,
    } ) );
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

/// Output identity manifest (7.0): every distinct string member at the top
/// level of the payload (plus any string entries under an "outputs" key)
/// that names an existing file is reported with size+mtime so the host can
/// bind/verify produced artifacts. Bounded; never fails the run.
Json::Value collectOutputIdentity( const Json::Value &payload )
{
    Json::Value manifest( Json::arrayValue );
    if ( !payload.isObject() )
        return manifest;
    auto addPath = [ &manifest ]( const std::string &candidate ) {
        if ( candidate.empty() || manifest.size() >= 64 )
            return;
        const QFileInfo info( QString::fromStdString( candidate ) );
        if ( !info.isFile() )
            return;
        Json::Value entry;
        entry["path"] = candidate;
        entry["sizeBytes"] = static_cast<Json::Int64>( info.size() );
        entry["lastModifiedMsec"] = static_cast<Json::Int64>( info.lastModified().toMSecsSinceEpoch() );
        manifest.append( entry );
    };
    for ( const auto &key : payload.getMemberNames() )
    {
        const Json::Value &value = payload[key];
        if ( value.isString() )
        {
            addPath( value.asString() );
        }
        else if ( value.isArray() )
        {
            for ( const auto &entry : value )
                if ( entry.isString() )
                    addPath( entry.asString() );
        }
    }
    return manifest;
}

int runHangJob( const std::string &jobId, const std::atomic<bool> &cancel,
                std::mutex &stdoutMutex )
{
    {
        std::lock_guard<std::mutex> lock( stdoutMutex );
        emitFrame( "progress", jobId, Json::Value{} );
    }
    while ( !cancel.load() )
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    std::lock_guard<std::mutex> lock( stdoutMutex );
    emitFrame( "error", jobId, [] {
        Json::Value e;
        e["message"] = "cancelled";
        // The structured code mirrors the legacy exact text; a MASTER host
        // (no "code" support) still matches message=="cancelled" byte-for-
        // byte, so mixed host/worker installs keep their cancellation
        // semantics.
        e["code"] = "cancelled";
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

    // STDOUT HYGIENE (7.0): stdout carries protocol frames ONLY. OpenCV's
    // parallel-backend registry logs INFO lines to STDOUT when an operator
    // initializes it, which desynchronized the frame stream (observed as a
    // "malformed frame" hard refusal on the first real operator run). Route
    // every OpenCV log line to stderr and drop INFO noise before any
    // operator can initialize the library.
#ifdef SICNU_WORKER_HAS_OPENCV_LOG
    cv::utils::logging::setLogLevel( cv::utils::logging::LOG_LEVEL_ERROR );
#endif

    sicnu::operators::RSOperatorRegistry::instance(); // call_once chain
    sicnu::operators::rs::installRsOperatorProvider();
    emitReady();

    // Concurrency model (7.0): jobs run on a WORKER THREAD so the main loop
    // keeps reading the frame stream. Cooperative cancellation is only real
    // if a cancel frame can be received AND acked WHILE the job runs — the
    // previous single-threaded design could not read stdin during a job, so
    // every cancel degenerated into the host's kill escalation.
    std::atomic<bool> cancelFlag{ false };
    std::atomic<bool> jobActive{ false };
    std::string currentJobId; // job a cancel refers to (ack target)
    std::thread jobThread;
    auto finishJobThread = [ &jobThread ]() {
        if ( jobThread.joinable() )
            jobThread.join();
    };
    // stdout carries frames from two threads (progress/result from the job
    // thread, ack/error from the main loop): serialize whole lines.
    std::mutex stdoutMutex;
    while ( true )
    {
        std::string line;
        if ( !readLine( line ) )
        {
            // EOF = shutdown: arm the flag so a running operator winds down,
            // then wait for it (the host kills us anyway if we stall).
            cancelFlag = true;
            finishJobThread();
            break;
        }
        Json::Value frame;
        if ( !sicnu::runtime::worker::parseFrame( line, frame ) )
            continue;
        const std::string op = frame["op"].asString();
        const std::string jobId = frame["jobId"].asString();
        if ( op == "shutdown" )
        {
            cancelFlag = true;
            finishJobThread();
            break;
        }
        if ( op == "cancel" )
        {
            // Known job: ack before arming so the host sees the receipt even
            // when the operator exits fast (a legacy host ignores "ack").
            // Unknown job: protocol rule — answered, not ignored.
            std::lock_guard<std::mutex> lock( stdoutMutex );
            if ( !currentJobId.empty() && jobId == currentJobId )
            {
                emitFrame( "ack", jobId, [] {
                    Json::Value e;
                    e["kind"] = "cancel";
                    return e;
                }() );
                cancelFlag = true;
            }
            else
            {
                emitFrame( "error", jobId, [] {
                    Json::Value e;
                    e["message"] = "cancel for unknown jobId";
                    e["code"] = "unknownJob";
                    return e;
                }() );
            }
            continue;
        }
        if ( op != "run" )
            continue;

        if ( jobActive.load() )
        {
            // Protocol rule: one run at a time. A second concurrent run is a
            // host/worker contract violation — answered, never queued (this
            // worker is an executor, not a scheduler).
            std::lock_guard<std::mutex> lock( stdoutMutex );
            emitFrame( "error", jobId, [] {
                Json::Value e;
                e["message"] = "worker busy: one run at a time";
                e["code"] = "busy";
                return e;
            }() );
            continue;
        }

        const std::string algorithmId = frame["algorithmId"].asString();
        const Json::Value params = frame["params"];
        currentJobId = jobId;
        // Each run starts with a fresh cancel window: a cancelled predecessor
        // must not disarm its successor on a reused worker.
        cancelFlag = false;
        finishJobThread(); // reap the previous (finished) job thread

        jobActive = true;
        jobThread = std::thread( [ &, jobId, algorithmId, params ]() {
            struct ActiveGuard
            {
                std::atomic<bool> &flag;
                ~ActiveGuard() { flag = false; }
            } activeGuard{ jobActive };
            if ( algorithmId == "__hang__" )
            {
                runHangJob( jobId, cancelFlag, stdoutMutex );
                return;
            }
            try
            {
                auto operatorPtr =
                    sicnu::operators::RSOperatorRegistry::instance().create( algorithmId );
                if ( !operatorPtr )
                    throw std::runtime_error( "unknown algorithm: " + algorithmId );
                sicnu::operators::RSOperatorContext context;
                context.setCancelFlag( &cancelFlag );
                context.setProgressCallback( [ &jobId, &stdoutMutex ]( double value, const std::string & ) {
                    Json::Value extra;
                    extra["value"] = value;
                    std::lock_guard<std::mutex> lock( stdoutMutex );
                    emitFrame( "progress", jobId, extra );
                } );
                context.setLogCallback( []( const std::string &, const std::string & ) {} );
                const Json::Value payload = operatorPtr->run( params, context );
                // Result + optional output identity manifest in ONE frame
                // (7.0): a legacy host reads "payload" and ignores "outputs".
                std::lock_guard<std::mutex> lock( stdoutMutex );
                writeLine( sicnu::runtime::worker::makeResultFrame(
                    jobId, payload, collectOutputIdentity( payload ) ) );
            }
            catch ( const std::exception &e )
            {
                Json::Value extra;
                extra["message"] = e.what();
                // Structured error class (7.0): cancellations are
                // distinguished from operator failures so the host does not
                // string-match.
                const std::string message = e.what();
                std::string code = "operatorError";
                if ( message.find( "cancelled" ) != std::string::npos )
                    code = "cancelled";
                else if ( message.find( "unknown algorithm" ) != std::string::npos )
                    code = "unknownAlgorithm";
                extra["code"] = code;
                std::lock_guard<std::mutex> lock( stdoutMutex );
                emitFrame( "error", jobId, extra );
            }
        } );
    }
    finishJobThread();
    return 0;
}

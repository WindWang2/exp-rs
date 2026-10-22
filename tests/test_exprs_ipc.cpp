// tests/test_exprs_ipc.cpp — frame codec, envelope contract, channel semantics
// (isolation runtime 5.0). Runs over in-memory pipe pairs so the production
// framing/protocol logic is exercised without spawning processes.
#include <catch2/catch_test_macros.hpp>

#include "exprs/host_protocol.h"
#include "exprs/ipc_channel.h"
#include "exprs/ipc_envelope.h"
#include "exprs/ipc_frame.h"
#include "exprs/ipc_stream.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace exprs;

namespace {

Json::Value objectWith( std::string key, std::string value )
{
    Json::Value json( Json::objectValue );
    json[key] = value;
    return json;
}

} // namespace

TEST_CASE( "frames round-trip through the memory pipe", "[ipc][frame]" )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    makeIpcMemoryPipePair( a, b );

    IpcFrameLimits limits;
    std::string error;
    REQUIRE( IpcFrame::writeJson( *a, objectWith( "hello", "world" ), limits, error ) );
    REQUIRE( error.empty() );

    std::string payload;
    REQUIRE( IpcFrame::read( *b, payload, limits, 1000, error ) == IpcFrame::ReadStatus::Ok );
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string parseError;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( payload.data(), payload.data() + payload.size(), &parsed, &parseError ) );
    REQUIRE( parsed["hello"].asString() == "world" );

    // A large-but-legal frame survives too.
    Json::Value big( Json::objectValue );
    big["blob"] = std::string( 1024 * 1024, 'x' );
    REQUIRE( IpcFrame::writeJson( *a, big, limits, error ) );
    REQUIRE( IpcFrame::read( *b, payload, limits, 1000, error ) == IpcFrame::ReadStatus::Ok );
    REQUIRE( payload.size() > 1024 * 1024 );
}

TEST_CASE( "frame caps refuse oversize payloads on both sides", "[ipc][frame]" )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    makeIpcMemoryPipePair( a, b );

    IpcFrameLimits tiny;
    tiny.maxFrameBytes = 16;
    std::string error;
    Json::Value big = objectWith( "blob", std::string( 64, 'y' ) );
    REQUIRE_FALSE( IpcFrame::writeJson( *a, big, tiny, error ) );
    REQUIRE( error.find( "E6003" ) != std::string::npos );
    REQUIRE( error.find( "negotiated cap" ) != std::string::npos );

    // Reader side: a peer that announces an over-cap length is refused.
    const uint8_t prefix[4] = { 0xFF, 0xFF, 0xFF, 0x7F }; // ~2 GiB
    REQUIRE( a->writeAll( reinterpret_cast<const char *>( prefix ), 4, error ) );
    REQUIRE( IpcFrame::read( *b, error.empty() ? error : error, tiny, 500, error )
             == IpcFrame::ReadStatus::TooLarge );
    REQUIRE( error.find( "E6003" ) != std::string::npos );

    // The salvaged prefix rides out in `payload`: the channel's TooLarge
    // hexdump ("stream head: ...") needs real bytes — pre-fix the buffer
    // was cleared and the dump was always empty.
    std::unique_ptr<IIpcStream> c;
    std::unique_ptr<IIpcStream> d;
    makeIpcMemoryPipePair( c, d );
    const uint8_t prefix2[4] = { 0xAB, 0xCD, 0xEF, 0x7F };
    REQUIRE( c->writeAll( reinterpret_cast<const char *>( prefix2 ), 4, error ) );
    std::string salvaged;
    REQUIRE( IpcFrame::read( *d, salvaged, tiny, 500, error )
             == IpcFrame::ReadStatus::TooLarge );
    REQUIRE( salvaged.size() == 4 );
    CHECK( static_cast<unsigned char>( salvaged[0] ) == 0xAB );
    CHECK( static_cast<unsigned char>( salvaged[1] ) == 0xCD );
    CHECK( static_cast<unsigned char>( salvaged[2] ) == 0xEF );
    CHECK( static_cast<unsigned char>( salvaged[3] ) == 0x7F );
}

TEST_CASE( "frame length prefix bytes 128-255 decode without sign extension", "[ipc][frame]" )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    makeIpcMemoryPipePair( a, b );

    IpcFrameLimits limits;
    std::string error;
    Json::Value mid( Json::objectValue );
    // Compact JSON of this object is in the 128–255 window where byte0 of the
    // little-endian length has the high bit set (MSVC signed char trap).
    mid["blob"] = std::string( 180, 'z' );
    REQUIRE( IpcFrame::writeJson( *a, mid, limits, error ) );

    std::string payload;
    REQUIRE( IpcFrame::read( *b, payload, limits, 1000, error ) == IpcFrame::ReadStatus::Ok );
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string parseError;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( payload.data(), payload.data() + payload.size(), &parsed, &parseError ) );
    REQUIRE( parsed["blob"].asString().size() == 180 );
}

TEST_CASE( "frame read resumes a mid-payload timeout without dropping the length prefix",
           "[ipc][frame]" )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    makeIpcMemoryPipePair( a, b );

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    writerBuilder["commentStyle"] = "None";
    Json::Value body( Json::objectValue );
    body["blob"] = std::string( 64, 'q' );
    const std::string json = Json::writeString( writerBuilder, body );
    REQUIRE( json.size() > 16 );

    const uint32_t length = static_cast<uint32_t>( json.size() );
    uint8_t prefix[ 4 ] = {
        static_cast<uint8_t>( length & 0xFFu ),
        static_cast<uint8_t>( ( length >> 8 ) & 0xFFu ),
        static_cast<uint8_t>( ( length >> 16 ) & 0xFFu ),
        static_cast<uint8_t>( ( length >> 24 ) & 0xFFu )
    };
    std::string error;
    REQUIRE( a->writeAll( reinterpret_cast<const char *>( prefix ), 4, error ) );
    REQUIRE( a->writeAll( json.data(), 8, error ) );

    std::string pending;
    std::string payload;
    IpcFrameLimits limits;
    REQUIRE( IpcFrame::read( *b, payload, limits, 80, error, &pending )
             == IpcFrame::ReadStatus::Timeout );
    REQUIRE( pending.size() == 4 + 8 );

    REQUIRE( a->writeAll( json.data() + 8, json.size() - 8, error ) );
    REQUIRE( IpcFrame::read( *b, payload, limits, 1000, error, &pending )
             == IpcFrame::ReadStatus::Ok );
    REQUIRE( payload == json );
}

TEST_CASE( "frame reads time out and detect EOF", "[ipc][frame]" )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    makeIpcMemoryPipePair( a, b );

    std::string payload;
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    REQUIRE( IpcFrame::read( *b, payload, {}, 120, error ) == IpcFrame::ReadStatus::Timeout );
    REQUIRE( std::chrono::steady_clock::now() - start < std::chrono::milliseconds( 1000 ) );

    a->close();
    REQUIRE( IpcFrame::read( *b, payload, {}, 500, error ) == IpcFrame::ReadStatus::Eof );
}

TEST_CASE( "envelope round-trips every message type", "[ipc][envelope]" )
{
    using MT = Ipc::MessageType;
    for ( const MT type : { MT::Request, MT::Response, MT::Progress, MT::Cancel, MT::Event } )
    {
        Ipc::Envelope envelope;
        envelope.type = type;
        envelope.id = 42;
        envelope.method = "operator.execute";
        envelope.params = objectWith( "k", "v" );
        envelope.ok = false;
        envelope.error.code = "E6004";
        envelope.error.message = "timeout";
        envelope.progress = 0.5;
        envelope.message = "halfway";
        envelope.deadlineMs = 1234;

        std::string error;
        Ipc::Envelope decoded;
        REQUIRE( Ipc::decodeEnvelope( Ipc::encodeEnvelope( envelope ), decoded, error ) );
        REQUIRE( decoded.type == type );
        if ( type == MT::Request )
        {
            REQUIRE( decoded.method == "operator.execute" );
            REQUIRE( decoded.params["k"].asString() == "v" );
            REQUIRE( decoded.deadlineMs == 1234 );
        }
        if ( type == MT::Response )
        {
            REQUIRE_FALSE( decoded.ok );
            REQUIRE( decoded.error.code == "E6004" );
        }
    }
}

TEST_CASE( "envelope validation refuses malformed input", "[ipc][envelope]" )
{
    std::string error;
    Ipc::Envelope decoded;

    // Wrong protocol major.
    Json::Value json( Json::objectValue );
    json["v"] = 99;
    json["type"] = "request";
    json["id"] = 1;
    json["method"] = "x";
    REQUIRE_FALSE( Ipc::decodeEnvelope( json, decoded, error ) );
    REQUIRE( error.find( "E6001" ) != std::string::npos );

    // Unknown type.
    json["v"] = 1;
    json["type"] = "broadcast";
    REQUIRE_FALSE( Ipc::decodeEnvelope( json, decoded, error ) );
    REQUIRE( error.find( "E6002" ) != std::string::npos );

    // Request without method.
    json.removeMember( "method" );
    json["type"] = "request";
    REQUIRE_FALSE( Ipc::decodeEnvelope( json, decoded, error ) );

    // Failed response without error code.
    json["type"] = "response";
    json["ok"] = false;
    json["error"] = objectWith( "message", "no code" );
    REQUIRE_FALSE( Ipc::decodeEnvelope( json, decoded, error ) );

    // Unknown extra fields are ignored (additive evolution).
    Json::Value future = Ipc::encodeEnvelope( Ipc::makeErrorResponse( 7, "E6004", "late" ) );
    future["newFieldInV1_1"] = true;
    REQUIRE( Ipc::decodeEnvelope( future, decoded, error ) );
    REQUIRE( decoded.error.code == "E6004" );
}

TEST_CASE( "successful response envelope refuses a non-object result (E6002)", "[ipc][envelope]" )
{
    // Worker frames are untrusted input: the host's own contract declares
    // the worker process untrusted (plugin_host_process_runtime.cpp), and
    // host consumers index `outcome.result` with jsoncpp's NON-CONST
    // operator[] (plugin_host_proxies.cpp HostProcessOperatorProxy::run,
    // plugin_host_process_runtime.cpp loadPlugin/inspectUiSchema). On a
    // non-object value that operator is resolveReference UB — a debug
    // JSON_ASSERT abort or a wild-pointer read through the value union —
    // which no try/catch on the consumer path can intercept. The only
    // enforceable seam is decode time, mirroring the Request side's
    // params-must-be-an-object gate.
    std::string error;
    Ipc::Envelope decoded;

    Json::Value json( Json::objectValue );
    json["v"] = 1;
    json["type"] = "response";
    json["id"] = 1;
    json["ok"] = true;

    json["result"] = "boom";
    REQUIRE_FALSE( Ipc::decodeEnvelope( json, decoded, error ) );
    REQUIRE( error.find( "E6002" ) != std::string::npos );
    REQUIRE( error.find( "result" ) != std::string::npos );

    json["result"] = Json::Value( Json::arrayValue );
    REQUIRE_FALSE( Ipc::decodeEnvelope( json, decoded, error ) );

    json["result"] = 42;
    REQUIRE_FALSE( Ipc::decodeEnvelope( json, decoded, error ) );

    json["result"] = true;
    REQUIRE_FALSE( Ipc::decodeEnvelope( json, decoded, error ) );

    // null stays legal: worker code paths default-construct
    // `Json::Value result;` (nullValue) before filling it
    // (plugin_host_worker_main.cpp model-runtime load), and a null result
    // is safe to index — jsoncpp promotes null to an object on the first
    // non-const access. Same object-or-null vocabulary as `params`.
    json["result"] = Json::Value();
    REQUIRE( Ipc::decodeEnvelope( json, decoded, error ) );

    // Absent result defaults to an empty object.
    json.removeMember( "result" );
    REQUIRE( Ipc::decodeEnvelope( json, decoded, error ) );
    REQUIRE( decoded.result.isObject() );
    REQUIRE( decoded.result.empty() );

    // Object results keep round-tripping.
    Json::Value okResult( Json::objectValue );
    okResult["done"] = true;
    json["result"] = okResult;
    REQUIRE( Ipc::decodeEnvelope( json, decoded, error ) );
    REQUIRE( decoded.result["done"].asBool() );
}

TEST_CASE( "protocol compatibility gate", "[ipc][envelope]" )
{
    std::string reason;
    REQUIRE( Ipc::isProtocolCompatible( 1, 0, 1, 0, reason ) );
    REQUIRE( Ipc::isProtocolCompatible( 1, 2, 1, 0, reason ) );
    REQUIRE_FALSE( Ipc::isProtocolCompatible( 1, 0, 2, 0, reason ) );
    REQUIRE( reason.find( "major" ) != std::string::npos );
    REQUIRE_FALSE( Ipc::isProtocolCompatible( 1, 0, 1, 1, reason ) );
    REQUIRE( reason.find( "minor" ) != std::string::npos );
}

TEST_CASE( "host protocol header agrees with the envelope constants", "[ipc][version]" )
{
    REQUIRE( hostProtocolVersionMajor() == Ipc::kProtocolVersionMajor );
    REQUIRE( hostProtocolVersionMinor() == Ipc::kProtocolVersionMinor );
}

TEST_CASE( "channel correlates a request with its response", "[ipc][channel]" )
{
    std::unique_ptr<IIpcStream> hostSide;
    std::unique_ptr<IIpcStream> workerSide;
    makeIpcMemoryPipePair( hostSide, workerSide );

    IpcChannel host( std::move( hostSide ) );
    IpcChannel worker( std::move( workerSide ) );

    // Minimal worker: echo service.
    std::thread serve( [&worker] {
        Ipc::Envelope request;
        while ( worker.nextRequest( request, 2000 ) )
        {
            std::string sendError;
            if ( request.method == "echo" )
                worker.sendResponse( request.id, request.params, sendError );
            else
                worker.sendError( request.id, { "E6008", "unsupported method" } );
        }
    } );

    auto outcome = host.request( "echo", objectWith( "ping", "pong" ), 2000 );
    REQUIRE( outcome.status == IpcChannel::Outcome::Status::Ok );
    REQUIRE( outcome.result["ping"].asString() == "pong" );

    auto unsupported = host.request( "no.such.method", {}, 2000 );
    REQUIRE( unsupported.status == IpcChannel::Outcome::Status::Error );
    REQUIRE( unsupported.statusCode() == "E6008" );

    host.close();
    worker.close();
    serve.join();
}

TEST_CASE( "channel reports a typed timeout for a hung worker", "[ipc][channel]" )
{
    std::unique_ptr<IIpcStream> hostSide;
    std::unique_ptr<IIpcStream> workerSide;
    makeIpcMemoryPipePair( hostSide, workerSide );

    IpcChannel host( std::move( hostSide ) );
    // No worker: nobody answers.
    auto outcome = host.request( "hang", {}, 150 );
    REQUIRE( outcome.status == IpcChannel::Outcome::Status::Timeout );
    REQUIRE( outcome.statusCode() == "E6004" );
    host.close();
}

TEST_CASE( "channel cancel predicate yields a typed cancellation", "[ipc][channel]" )
{
    std::unique_ptr<IIpcStream> hostSide;
    std::unique_ptr<IIpcStream> workerSide;
    makeIpcMemoryPipePair( hostSide, workerSide );

    IpcChannel host( std::move( hostSide ) );
    IpcChannel worker( std::move( workerSide ) );

    std::atomic<bool> sawCancel{ false };
    worker.setCancelSink( [&]( long long ) { sawCancel = true; } );
    std::thread serve( [&worker] {
        Ipc::Envelope request;
        while ( worker.nextRequest( request, 2000 ) )
        {
            // deliberately never answers: exercises the cancel path
        }
    } );

    std::atomic<int> polls{ 0 };
    auto outcome = host.request(
        "slow", {}, 10000, [&polls] { return ++polls > 2; } );
    REQUIRE( outcome.status == IpcChannel::Outcome::Status::Cancelled );
    REQUIRE( outcome.statusCode() == "E6009" );
    REQUIRE( sawCancel.load() );

    host.close();
    worker.close();
    serve.join();
}

TEST_CASE( "channel fails in-flight requests when the peer dies", "[ipc][channel]" )
{
    std::unique_ptr<IIpcStream> hostSide;
    std::unique_ptr<IIpcStream> workerSide;
    makeIpcMemoryPipePair( hostSide, workerSide );

    IpcChannel host( std::move( hostSide ) );
    // Simulate the worker process dying mid-request: the peer stream breaks
    // while a request is in flight.
    std::thread closer( [&workerSide] {
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        workerSide->close();
    } );
    auto outcome = host.request( "anything", {}, 5000 );
    REQUIRE( outcome.status == IpcChannel::Outcome::Status::ChannelClosed );
    REQUIRE( outcome.statusCode() == "E6005" );
    REQUIRE_FALSE( host.isOpen() );
    host.close();
    closer.join();
}

TEST_CASE( "channel interleaves progress events with the response", "[ipc][channel]" )
{
    std::unique_ptr<IIpcStream> hostSide;
    std::unique_ptr<IIpcStream> workerSide;
    makeIpcMemoryPipePair( hostSide, workerSide );

    IpcChannel host( std::move( hostSide ) );
    IpcChannel worker( std::move( workerSide ) );

    std::thread serve( [&worker] {
        Ipc::Envelope request;
        while ( worker.nextRequest( request, 2000 ) )
        {
            for ( double value : { 0.25, 0.5, 0.75 } )
                worker.sendProgress( request.id, value, "step" );
            Json::Value result( Json::objectValue );
            result["done"] = true;
            std::string sendError;
            worker.sendResponse( request.id, result, sendError );
        }
    } );

    std::vector<double> seen;
    auto outcome = host.request( "work", {}, 3000, {}, [&]( double value, const std::string & ) {
        seen.push_back( value );
    } );
    REQUIRE( outcome.status == IpcChannel::Outcome::Status::Ok );
    REQUIRE( seen.size() == 3 );
    REQUIRE( seen[0] == 0.25 );
    REQUIRE( outcome.result["done"].asBool() );

    host.close();
    worker.close();
    serve.join();
}

TEST_CASE( "pre-sink events replay in order before live delivery", "[ipc][channel]" )
{
    std::unique_ptr<IIpcStream> hostSide;
    std::unique_ptr<IIpcStream> workerSide;
    makeIpcMemoryPipePair( hostSide, workerSide );

    IpcChannel host( std::move( hostSide ) );
    IpcChannel worker( std::move( workerSide ) );

    std::thread serve( [&worker] {
        Ipc::Envelope request;
        while ( worker.nextRequest( request, 2000 ) )
        {
            std::string sendError;
            worker.sendResponse( request.id, request.params, sendError );
        }
    } );

    // "queued" is sent while NO sink exists; the request round-trip that
    // follows is the barrier: once it answers, the reader has processed the
    // event frame, and — no sink installed — parked it in the pending queue.
    REQUIRE( worker.sendEvent( "worker.hello", objectWith( "seq", "queued" ) ) );
    REQUIRE( host.request( "echo", {}, 2000 ).status == IpcChannel::Outcome::Status::Ok );

    std::vector<std::string> order;
    std::mutex orderMutex;
    host.setEventSink( [&]( const Ipc::Envelope &event ) {
        std::lock_guard<std::mutex> lock( orderMutex );
        order.push_back( event.params["seq"].asString() );
    } );

    // "live" arrives after the sink is installed and must not overtake the
    // queued event replayed above it (#1449 hello-first ordering).
    REQUIRE( worker.sendEvent( "worker.hello", objectWith( "seq", "live" ) ) );
    for ( int waited = 0; waited < 100; ++waited )
    {
        std::lock_guard<std::mutex> lock( orderMutex );
        if ( order.size() == 2 )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }
    {
        std::lock_guard<std::mutex> lock( orderMutex );
        INFO( "order size: " << order.size() );
        for ( size_t i = 0; i < order.size(); ++i )
            INFO( "order[" << i << "]: " << order[i] );
    }
    host.close();
    worker.close();
    if ( serve.joinable() )
        serve.join();
    {
        std::lock_guard<std::mutex> lock( orderMutex );
        REQUIRE( order.size() == 2 );
        REQUIRE( order[0] == "queued" );
        REQUIRE( order[1] == "live" );
    }
}

TEST_CASE( "event-sink replay tolerates re-entrant channel calls", "[ipc][channel]" )
{
    std::unique_ptr<IIpcStream> hostSide;
    std::unique_ptr<IIpcStream> workerSide;
    makeIpcMemoryPipePair( hostSide, workerSide );

    // Raw pointer ownership: on the pre-fix deadlock path the installer
    // thread is wedged inside the channel holding its state lock — the
    // destructor would hang too, so the channel is deliberately leaked and
    // only the typed REQUIRE below reports the failure. On the fixed path
    // the unique_ptr cleans up normally.
    auto host = std::make_unique<IpcChannel>( std::move( hostSide ) );
    IpcChannel * const hostRaw = host.get();
    IpcChannel worker( std::move( workerSide ) );

    std::thread serve( [&worker] {
        Ipc::Envelope request;
        while ( worker.nextRequest( request, 2000 ) )
        {
            std::string sendError;
            worker.sendResponse( request.id, request.params, sendError );
        }
    } );

    for ( int i = 0; i < 3; ++i )
        REQUIRE( worker.sendEvent( "worker.hello", objectWith( "seq", std::to_string( i ) ) ) );
    // Barrier: the response is the 4th frame, so once the request answers,
    // all three events are parked in the pending queue.
    REQUIRE( host->request( "echo", {}, 2000 ).status == IpcChannel::Outcome::Status::Ok );

    std::atomic<int> delivered{ 0 };
    std::atomic<bool> reentered{ false };
    std::atomic<bool> installed{ false };
    std::thread installer( [&] {
        host->setEventSink( [&]( const Ipc::Envelope & ) {
            if ( delivered.fetch_add( 1 ) == 0 )
            {
                // Re-enter the channel from inside a replayed callback: both
                // calls take the channel state lock and self-deadlocked when
                // the replay still ran under it (PR #1469 review finding).
                (void) hostRaw->protocolFailure();
                hostRaw->close();
                reentered = true;
            }
        } );
        installed = true;
    } );

    for ( int waited = 0; waited < 100 && !installed.load(); ++waited )
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    if ( !installed.load() )
    {
        // Pre-fix deadlock: leave the wedged thread and the channel (it
        // holds the state lock; joining or destroying either would hang)
        // and let the REQUIRE report the failure.
        installer.detach();
        host.release();
    }
    REQUIRE( installed.load() );
    installer.join();
    REQUIRE( reentered.load() );
    REQUIRE( delivered.load() == 3 );
    REQUIRE_FALSE( host->isOpen() );

    worker.close();
    serve.join();
}

TEST_CASE( "a protocol-corrupting peer closes the channel with E6002", "[ipc][channel]" )
{
    std::unique_ptr<IIpcStream> hostSide;
    std::unique_ptr<IIpcStream> workerSide;
    makeIpcMemoryPipePair( hostSide, workerSide );

    IpcChannel host( std::move( hostSide ) );
    // The request must be IN FLIGHT before the poison frame is written:
    // otherwise the reader may process the corruption before the request
    // registers a pending slot and the caller would see the post-close
    // E6005 instead of the typed violation under test.
    IpcChannel::Outcome outcome;
    std::thread requester( [&host, &outcome] {
        outcome = host.request( "work", {}, 5000 );
    } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    {
        // Raw corrupt envelope through the peer stream.
        IpcFrameLimits limits;
        std::string error;
        Ipc::Envelope bad;
        bad.type = Ipc::MessageType::Response;
        bad.id = 1;
        Json::Value json = Ipc::encodeEnvelope( bad );
        json.removeMember( "ok" ); // structurally invalid response
        REQUIRE( IpcFrame::writeJson( *workerSide, json, limits, error ) );
    }

    requester.join();
    REQUIRE( outcome.status == IpcChannel::Outcome::Status::ProtocolError );
    REQUIRE( outcome.statusCode() == "E6002" );
    // Channel is dead for good; the next request refuses typed.
    auto second = host.request( "work", {}, 2000 );
    REQUIRE( second.status == IpcChannel::Outcome::Status::ChannelClosed );
    host.close();
}

// -- protocol 1.2: per-direction frame caps ----------------------------------

namespace {

/// Writes one raw frame with the given payload directly on a stream,
/// bypassing channel caps (simulates a peer's write side).
bool writeRawFrame( IIpcStream &stream, const std::string &payload, std::string &error )
{
    const uint32_t length = static_cast<uint32_t>( payload.size() );
    const char prefix[ 4 ] = {
        static_cast<char>( length & 0xFF ), static_cast<char>( ( length >> 8 ) & 0xFF ),
        static_cast<char>( ( length >> 16 ) & 0xFF ), static_cast<char>( ( length >> 24 ) & 0xFF )
    };
    return stream.writeAll( prefix, 4, error ) && stream.writeAll( payload.data(), payload.size(), error );
}

} // namespace

TEST_CASE( "directional frame caps are independent per direction", "[ipc][channel][p12]" )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    makeIpcMemoryPipePair( a, b );

    IpcChannel host( std::move( a ) );
    // Protocol 1.2 negotiation shape: a small response bound (host recv) and
    // a generous request bound (host send). The 1.1 defect was that the
    // small response bound ALSO capped the send direction.
    host.setDirectionalFrameCaps( 0, 4096 );

    REQUIRE( host.sendFrameCap() > 4096 );      // send untouched by recv bound
    REQUIRE( host.recvFrameCap() == 4096 );
    REQUIRE( host.frameCap() == 4096 );         // min() diagnostics view

    // A frame the host SENDS may exceed its own recv bound without a local
    // E6003 (the worker side owns enforcement of that direction).
    Json::Value big( Json::objectValue );
    big["blob"] = std::string( 8192, 'q' );
    REQUIRE( host.sendEvent( "big.event", big ) );
    host.close();
}

TEST_CASE( "the send cap refuses oversized local frames (E6003)", "[ipc][channel][p12]" )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    makeIpcMemoryPipePair( a, b );

    IpcChannel host( std::move( a ) );
    host.setDirectionalFrameCaps( 4096, 0 );
    REQUIRE( host.sendFrameCap() == 4096 );
    REQUIRE( host.recvFrameCap() > 4096 );

    // A send above the local cap fails typed and ends the channel (the
    // untrusted-writer contract, same as the shared cap).
    Json::Value big( Json::objectValue );
    big["blob"] = std::string( 8192, 'w' );
    REQUIRE_FALSE( host.sendEvent( "big.event", big ) );
    REQUIRE( host.protocolFailure().find( "E6003" ) != std::string::npos );
    host.close();
}

TEST_CASE( "the recv cap refuses oversized peer frames (E6003)", "[ipc][channel][p12]" )
{
    std::unique_ptr<IIpcStream> hostSide;
    std::unique_ptr<IIpcStream> workerSide;
    makeIpcMemoryPipePair( hostSide, workerSide );

    IpcChannel host( std::move( hostSide ) );
    host.setDirectionalFrameCaps( 0, 1024 );

    // The request must be IN FLIGHT before the oversized frame is written,
    // or the reader could tear the channel down before the caller registers
    // (yielding the post-close E6005/E6002 instead of the typed E6003 under
    // test). Instead of a timing window (a sleep loses the race under heavy
    // machine load), the writer DRAINS the request frame from the peer side
    // first: sendEnvelope only returns after the pending entry is
    // registered, so once the request bytes are observable the registry
    // state the reader's failAllPending walks is stable.
    IpcChannel::Outcome outcome;
    std::thread requester( [&host, &outcome] {
        outcome = host.request( "work", {}, 5000 );
    } );
    // A failed REQUIRE unwinds the case while `requester` is joinable — the
    // std::thread destructor would then std::terminate the whole binary.
    struct JoinGuard
    {
        std::thread &thread;
        ~JoinGuard() { if ( thread.joinable() ) thread.join(); }
    } joinGuard{ requester };
    auto readFull = [ &workerSide ]( char *data, size_t len ) {
        size_t got = 0;
        while ( got < len )
        {
            const int n = workerSide->readSome( data + got, len - got, 1000 );
            if ( n <= 0 )
                return false;
            got += static_cast<size_t>( n );
        }
        return true;
    };
    char prefix[ 4 ];
    REQUIRE( readFull( prefix, 4 ) ); // the in-flight request's length prefix
    uint32_t length = static_cast<uint32_t>( static_cast<unsigned char>( prefix[ 0 ] ) )
                      | ( static_cast<uint32_t>( static_cast<unsigned char>( prefix[ 1 ] ) ) << 8 )
                      | ( static_cast<uint32_t>( static_cast<unsigned char>( prefix[ 2 ] ) ) << 16 )
                      | ( static_cast<uint32_t>( static_cast<unsigned char>( prefix[ 3 ] ) ) << 24 );
    std::string requestPayload( length, '\0' );
    REQUIRE( readFull( requestPayload.data(), length ) );

    std::string error;
    const std::string payload = std::string( 2048, 'z' );
    REQUIRE( writeRawFrame( *workerSide, payload, error ) );

    requester.join();
    REQUIRE( outcome.status == IpcChannel::Outcome::Status::ProtocolError );
    REQUIRE( outcome.statusCode() == "E6003" );
    REQUIRE( host.protocolFailure().find( "E6003" ) != std::string::npos );
    host.close();
}

TEST_CASE( "lowerFrameCap keeps the shared 1.1 semantics on both directions",
           "[ipc][channel][p12]" )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    makeIpcMemoryPipePair( a, b );

    IpcChannel host( std::move( a ) );
    host.lowerFrameCap( 2048 );
    REQUIRE( host.sendFrameCap() == 2048 );
    REQUIRE( host.recvFrameCap() == 2048 );
    // Monotonicity per direction: a larger directional value is ignored.
    host.setDirectionalFrameCaps( 4096, 8192 );
    REQUIRE( host.sendFrameCap() == 2048 );
    REQUIRE( host.recvFrameCap() == 2048 );
    host.close();
}

// -- protocol 1.2: adversarial fuzz on the envelope codec --------------------

TEST_CASE( "seeded fuzz: mutated envelopes never crash and fail typed",
           "[ipc][envelope][fuzz]" )
{
    // Deterministic xorshift; the corpus and seeds are fixed so failures are
    // reproducible on any lane (no rand()/time dependence).
    auto next = [state = 0x9E3779B97F4A7C15ull]() mutable {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<uint32_t>( state >> 32 );
    };

    Ipc::Envelope valid;
    valid.type = Ipc::MessageType::Request;
    valid.id = 7;
    valid.method = "operator.execute";
    valid.params = objectWith( "operatorId", "fuzz:target" );
    valid.deadlineMs = 1000;
    const std::string validJson =
        Json::writeString( Json::StreamWriterBuilder(), Ipc::encodeEnvelope( valid ) );

    Ipc::Envelope decoded;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );

    for ( int iteration = 0; iteration < 4096; ++iteration )
    {
        std::string corpus = validJson;
        const int mutations = 1 + static_cast<int>( next() % 8 );
        for ( int m = 0; m < mutations && !corpus.empty(); ++m )
        {
            switch ( next() % 4 )
            {
            case 0: // bit flip
                corpus[ next() % corpus.size() ] ^= static_cast<char>( 1u << ( next() % 8 ) );
                break;
            case 1: // byte overwrite with hostile bytes
                corpus[ next() % corpus.size() ] =
                    static_cast<char>( next() % 256 );
                break;
            case 2: // truncation
                corpus.resize( next() % corpus.size() );
                break;
            case 3: // duplication (framing/length confusion)
                if ( corpus.size() < 4096 )
                    corpus += corpus;
                break;
            }
        }
        // The codec may accept or reject — but must never throw or crash.
        Json::Value parsed;
        std::string parseError;
        if ( reader->parse( corpus.data(), corpus.data() + corpus.size(), &parsed,
                            &parseError ) )
        {
            std::string error;
            (void)Ipc::decodeEnvelope( parsed, decoded, error ); // typed result only
        }
    }
    // The unmutated corpus still decodes after the fuzz storm.
    Json::Value parsed;
    std::string parseError;
    REQUIRE( reader->parse( validJson.data(), validJson.data() + validJson.size(), &parsed,
                            &parseError ) );
    std::string error;
    REQUIRE( Ipc::decodeEnvelope( parsed, decoded, error ) );
    REQUIRE( decoded.method == "operator.execute" );
}

TEST_CASE( "seeded fuzz: random raw frames never crash the frame reader",
           "[ipc][frame][fuzz]" )
{
    auto next = [state = 0xDEADBEEFCAFEF00Dull]() mutable {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<uint32_t>( state >> 32 );
    };

    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    makeIpcMemoryPipePair( a, b );

    for ( int iteration = 0; iteration < 256; ++iteration )
    {
        std::string garbage;
        const size_t size = next() % 128;
        for ( size_t i = 0; i < size; ++i )
            garbage.push_back( static_cast<char>( next() % 256 ) );
        std::string error;
        (void)writeRawFrame( *a, garbage, error );

        std::string payload;
        // Bounded read; Ok/TooLarge/Eof/Error are all acceptable outcomes,
        // a crash or unbounded wait is not (cap of 64 KiB absorbs junk).
        IpcFrameLimits limits;
        limits.maxFrameBytes = 64 * 1024;
        const IpcFrame::ReadStatus status = IpcFrame::read( *b, payload, limits, 20, error );
        (void)status;
    }
}

// -- issue #1036: descriptor ownership of the platform stream -----------------

TEST_CASE( "handle streams own and release their OS descriptors (issue #1036)",
           "[ipc][stream][resource]" )
{
    // Regression: the launcher-side pipe ends handed to makeIpcHandleStream
    // were never closed on ANY session lifecycle, leaking 2 descriptors per
    // worker spawn/shutdown/respawn. The stream now owns them; close() must
    // release both exactly once, and destruction must not double-close.
#ifdef _WIN32
    HANDLE hostToWorkerRead = nullptr;
    HANDLE hostToWorkerWrite = nullptr;
    HANDLE workerToHostRead = nullptr;
    HANDLE workerToHostWrite = nullptr;
    REQUIRE( ::CreatePipe( &hostToWorkerRead, &hostToWorkerWrite, nullptr, 0 ) );
    REQUIRE( ::CreatePipe( &workerToHostRead, &workerToHostWrite, nullptr, 0 ) );
    // Mirror the session: only the launcher-side ends reach the stream.
    ::CloseHandle( hostToWorkerRead );
    ::CloseHandle( workerToHostWrite );

    auto stream = makeIpcHandleStream( workerToHostRead, hostToWorkerWrite );
    stream->close();
    stream->close(); // idempotent: exactly one closer per handle
    DWORD flags = 0;
    REQUIRE_FALSE( ::GetHandleInformation( workerToHostRead, &flags ) );
    REQUIRE_FALSE( ::GetHandleInformation( hostToWorkerWrite, &flags ) );
    stream.reset(); // destructor must not double-close
#else
    int hostToWorker[ 2 ] = { -1, -1 };
    int workerToHost[ 2 ] = { -1, -1 };
    REQUIRE( ::pipe( hostToWorker ) == 0 );
    REQUIRE( ::pipe( workerToHost ) == 0 );
    // Mirror the session: only the launcher-side ends reach the stream.
    ::close( hostToWorker[ 0 ] );
    ::close( workerToHost[ 1 ] );

    auto stream = makeIpcHandleStream(
        reinterpret_cast<void *>( static_cast<intptr_t>( workerToHost[ 0 ] ) ),
        reinterpret_cast<void *>( static_cast<intptr_t>( hostToWorker[ 1 ] ) ) );
    stream->close();
    stream->close(); // idempotent: exactly one closer per descriptor
    errno = 0;
    REQUIRE( ::fcntl( workerToHost[ 0 ], F_GETFD ) == -1 );
    REQUIRE( errno == EBADF );
    errno = 0;
    REQUIRE( ::fcntl( hostToWorker[ 1 ], F_GETFD ) == -1 );
    REQUIRE( errno == EBADF );
    stream.reset(); // destructor must not double-close
#endif
}

// -- protocol 1.2: version matrix --------------------------------------------

TEST_CASE( "protocol compatibility matrix (1.2)", "[ipc][envelope][p12]" )
{
    std::string reason;
    // Equal versions and lower peers are compatible; anything newer is not.
    REQUIRE( Ipc::isProtocolCompatible( 1, 2, 1, 2, reason ) );
    REQUIRE( Ipc::isProtocolCompatible( 1, 2, 1, 1, reason ) ); // stale worker
    REQUIRE( Ipc::isProtocolCompatible( 1, 2, 1, 0, reason ) ); // v1.0 worker
    REQUIRE_FALSE( Ipc::isProtocolCompatible( 1, 1, 1, 2, reason ) ); // 1.1 host vs 1.2 worker
    REQUIRE( reason.find( "minor" ) != std::string::npos );
    REQUIRE_FALSE( Ipc::isProtocolCompatible( 1, 2, 2, 0, reason ) );
    REQUIRE( reason.find( "major" ) != std::string::npos );
    REQUIRE( hostProtocolVersionMajor() == 1 );
    REQUIRE( hostProtocolVersionMinor() >= 2 );
}


// -- track 13.0 WP4: concurrent close() must never double-join the reader ----

TEST_CASE( "concurrent close() calls never throw and always reap the reader",
           "[ipc][channel][race][p13]" )
{
    // Regression oracle for the untyped "no such process" flake: killProcess
    // paths (timeout ladder racing confirm-death, shutdown racing a
    // destructor) all funnel into IpcChannel::close(). Without join
    // serialization, two closers both observe joinable()==true and the
    // second join() throws std::system_error(errc::no_such_process) —
    // escaped killProcess untyped. N threads barrier-release on close();
    // any throw is captured and fails the test.
    for ( int round = 0; round < 20; ++round )
    {
        std::unique_ptr<IIpcStream> a;
        std::unique_ptr<IIpcStream> b;
        makeIpcMemoryPipePair( a, b );
        auto channel = std::make_unique<IpcChannel>( std::move( a ) );

        constexpr int kClosers = 8;
        std::atomic<int> ready{ 0 };
        std::atomic<bool> go{ false };
        std::vector<std::exception_ptr> thrown( kClosers );
        std::vector<std::thread> closers;
        closers.reserve( kClosers );
        for ( int i = 0; i < kClosers; ++i )
        {
            closers.emplace_back( [&, i] {
                ready.fetch_add( 1 );
                while ( !go.load() )
                    std::this_thread::yield();
                try
                {
                    channel->close();
                }
                catch ( ... )
                {
                    thrown[ i ] = std::current_exception();
                }
            } );
        }
        while ( ready.load() < kClosers )
            std::this_thread::yield();
        go.store( true );
        for ( std::thread &thread : closers )
            thread.join();

        for ( int i = 0; i < kClosers; ++i )
        {
            if ( thrown[ i ] )
            {
                try
                {
                    std::rethrow_exception( thrown[ i ] );
                }
                catch ( const std::exception &exception )
                {
                    FAIL( "round " << round << " closer " << i
                                   << " threw untyped: " << exception.what() );
                }
            }
        }
        REQUIRE_FALSE( channel->isOpen() );
        channel.reset(); // destructor re-enters close() — must stay quiet
        b->close();
    }
}

TEST_CASE( "request with empty method fails closed immediately with E6002",
           "[ipc][channel][validation]" )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    makeIpcMemoryPipePair( a, b );
    IpcChannel channel( std::move( a ) );

    IpcChannel::Outcome outcome = channel.request( "", Json::Value(), 100 );
    REQUIRE( outcome.status == IpcChannel::Outcome::Status::ProtocolError );
    REQUIRE( outcome.statusCode() == "E6002" );
}

TEST_CASE( "incoming request queue cap bounds memory and answers backpressure E6008",
           "[ipc][channel][backpressure]" )
{
    std::unique_ptr<IIpcStream> clientStream;
    std::unique_ptr<IIpcStream> serverStream;
    makeIpcMemoryPipePair( clientStream, serverStream );

    IpcChannel::Options serverOpts;
    serverOpts.maxQueuedRequests = 1;
    IpcChannel server( std::move( serverStream ), serverOpts );
    IpcChannel client( std::move( clientStream ) );

    std::thread t1( [&] {
        client.request( "heavy_task", objectWith( "k", "1" ), 500 );
    } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );

    IpcChannel::Outcome outcome2 = client.request( "heavy_task", objectWith( "k", "2" ), 500 );
    REQUIRE( outcome2.status == IpcChannel::Outcome::Status::Error );
    REQUIRE( outcome2.statusCode() == "E6008" );
    REQUIRE( outcome2.error.retryable == true );
    REQUIRE( server.droppedRequests() >= 1 );

    Ipc::Envelope req;
    REQUIRE( server.nextRequest( req, 500 ) );
    std::string err;
    server.sendResponse( req.id, objectWith( "status", "ok" ), err );

    t1.join();
}

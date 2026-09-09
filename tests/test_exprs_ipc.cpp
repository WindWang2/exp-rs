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

TEST_CASE( "a protocol-corrupting peer closes the channel with E6002", "[ipc][channel]" )
{
    std::unique_ptr<IIpcStream> hostSide;
    std::unique_ptr<IIpcStream> workerSide;
    makeIpcMemoryPipePair( hostSide, workerSide );

    IpcChannel host( std::move( hostSide ) );
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

    auto outcome = host.request( "work", {}, 2000 );
    REQUIRE( outcome.status == IpcChannel::Outcome::Status::ProtocolError );
    REQUIRE( outcome.statusCode() == "E6002" );
    // Channel is dead for good; the next request refuses typed.
    auto second = host.request( "work", {}, 2000 );
    REQUIRE( second.status == IpcChannel::Outcome::Status::ChannelClosed );
    host.close();
}

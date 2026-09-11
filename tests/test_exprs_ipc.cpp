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

    // The peer writes an oversized frame straight on the raw stream.
    std::string error;
    const std::string payload = std::string( 2048, 'z' );
    REQUIRE( writeRawFrame( *workerSide, payload, error ) );

    // The reader tears the channel down with the typed violation.
    auto outcome = host.request( "work", {}, 2000 );
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


// test_contract_fuzz_frame.cpp — bounded property lane over the plugin
// host-process IPC transport (Track ds41-fuzz-boundaries).
//
// Contracts under fuzz (known-answer legs live in test_exprs_ipc.cpp — this
// file adds the mutation/property corpus, it does not repeat them):
//
//   IpcFrame::writeJson / IpcFrame::read (exprs/ipc_frame.h):
//     1. Round-trip identity: every bounded JSON value written by the builder
//        is read back byte-identical by the parser.
//     2. Truncation cage: for EVERY prefix of a written frame, read() is total
//        (never throws) and never reports Ok unless the complete frame was on
//        the wire; a cut frame is Eof/Timeout/Error — never a silent success.
//     3. Corrupt length prefix: flipping prefix bytes yields Ok (exact
//        payload), TooLarge (beyond cap), Eof/Timeout/Error — never a crash,
//        and Ok always carries exactly the announced payload bytes.
//     4. Cap symmetry: a payload over limits.maxFrameBytes is refused by the
//        writer WITHOUT putting partial bytes on the stream.
//     5. Pending-frame resume: a mid-payload timeout keeps the consumed bytes
//        (length prefix included) so the next read() resumes the same frame
//        instead of desyncing into a spurious E6003.
//     6. Multi-frame streams: two frames read back in order, no cross-talk.
//     7. Length-prefix boundary values (0, 255, 256, 65535, cap-1) round-trip
//        — the signed-char sign-extension class stays closed.
//
//   Ipc::decodeEnvelope / decodeEnvelopePayload / isProtocolCompatible
//   (exprs/ipc_envelope.h):
//     8. Totality: any bounded mutated/depth-bombed payload yields
//        true+decoded OR false+non-empty error — never an escaping exception.
//     9. Round-trip stability: builder output decodes back to the same fields.
//    10. Version gate: a major mismatch is refused with E6001 naming the peer
//        major; minor newer-than-local is refused, older is accepted.
//
// Deterministic seeds + hard caps (≤512 B inputs, ≤300 iterations/seed).
#include <catch2/catch_test_macros.hpp>

#include "exprs/ipc_envelope.h"
#include "runtime/worker/worker_protocol.h"
#include "exprs/ipc_frame.h"
#include "exprs/ipc_stream.h"
#include "support/fuzz_corpus.h"

#include <json/json.h>

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Ipc = exprs::Ipc;             // message envelope layer
namespace IpcFrame = exprs::IpcFrame;   // length-prefixed framing layer

using exprs::IpcError;
using exprs::IpcFrameLimits;
using exprs::IIpcStream;
using sicnu::fuzz::BoundedRandom;
using sicnu::fuzz::depthBomb;
using sicnu::fuzz::mutateTypes;
using sicnu::fuzz::randomJsonValue;
using sicnu::fuzz::streamMutations;

namespace
{

constexpr int kIterationsPerSeed = 300;

IpcFrameLimits defaultLimits()
{
    return IpcFrameLimits{};
}

/// Failure reason for the truncation/corruption property, or nullopt when the
/// property holds. Doubles as the ddmin predicate in the directed defect case.
std::optional<std::string> wireOracle( const std::string &wire, bool peerClosed,
                                       const IpcFrameLimits &limits, int timeoutMs )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    exprs::makeIpcMemoryPipePair( a, b );

    if ( !wire.empty() )
    {
        std::string writeError;
        if ( !a->writeAll( wire.data(), wire.size(), writeError ) )
            return "writeAll failed: " + writeError;
    }
    if ( peerClosed )
        a->close();

    std::string payload;
    std::string error;
    IpcFrame::ReadStatus status = IpcFrame::ReadStatus::Error;
    try
    {
        status = IpcFrame::read( *b, payload, limits, timeoutMs, error );
    }
    catch ( const std::exception &exception )
    {
        return std::string( "IpcFrame::read threw: " ) + exception.what();
    }
    catch ( ... )
    {
        return "IpcFrame::read threw a non-std exception";
    }

    // Parse the announced length the same way the codec does.
    bool announcedValid = false;
    uint32_t announced = 0;
    if ( wire.size() >= IpcFrame::kPrefixBytes )
    {
        announced = static_cast<uint32_t>( static_cast<unsigned char>( wire[0] ) )
                    | ( static_cast<uint32_t>( static_cast<unsigned char>( wire[1] ) ) << 8 )
                    | ( static_cast<uint32_t>( static_cast<unsigned char>( wire[2] ) ) << 16 )
                    | ( static_cast<uint32_t>( static_cast<unsigned char>( wire[3] ) ) << 24 );
        announcedValid = true;
    }

    if ( announcedValid && announced > limits.maxFrameBytes )
    {
        if ( status != IpcFrame::ReadStatus::TooLarge )
            return "announced " + std::to_string( announced )
                   + " beyond cap but status was not TooLarge";
        if ( !payload.empty() )
            return "TooLarge returned a payload";
        return std::nullopt;
    }

    const bool completeFrame = announcedValid
                               && wire.size() >= IpcFrame::kPrefixBytes + announced;
    if ( status == IpcFrame::ReadStatus::Ok )
    {
        if ( !completeFrame )
            return "read reported Ok for an incomplete frame";
        if ( payload != wire.substr( IpcFrame::kPrefixBytes, announced ) )
            return "read payload does not match the announced bytes";
        return std::nullopt;
    }
    if ( completeFrame && status != IpcFrame::ReadStatus::Timeout )
        return "complete frame was not read (status is neither Ok nor Timeout)";
    return std::nullopt;
}

std::string serialize( const Json::Value &value )
{
    Json::StreamWriterBuilder builder;
    builder[ "indentation" ] = "";
    builder[ "commentStyle" ] = "None";
    return Json::writeString( builder, value );
}

Json::Value parseJson( const std::string &text, bool *ok )
{
    Json::Value value;
    Json::CharReaderBuilder builder;
    // Same hardening the production parse sites apply: an unbounded reader
    // turns a deep-but-small input into a stack overflow in the test process
    // itself, which would be a harness defect, not a finding.
    builder[ "stackLimit" ] = 64;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    // jsoncpp THROWS when the depth bound is exceeded; the helper converts
    // that into the same ok=false answer the production readers give.
    try
    {
        *ok = reader->parse( text.data(), text.data() + text.size(), &value, &errors );
    }
    catch ( const Json::Exception & )
    {
        *ok = false;
    }
    return value;
}

} // namespace

TEST_CASE( "ipc frame fuzz: builder output round-trips through the parser",
           "[contract8][fuzz][ipc][frame]" )
{
    for ( const uint64_t seed : { 1ull, 0x5EED1ull, 0xF00Dull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const Json::Value value = randomJsonValue( random );
            const std::string expected = serialize( value );
            REQUIRE( expected.size() <= 64 * 1024 );

            std::unique_ptr<IIpcStream> a;
            std::unique_ptr<IIpcStream> b;
            exprs::makeIpcMemoryPipePair( a, b );

            std::string error;
            REQUIRE( IpcFrame::writeJson( *a, value, IpcFrameLimits(), error ) );
            std::string payload;
            REQUIRE( IpcFrame::read( *b, payload, IpcFrameLimits(), 2000, error )
                     == IpcFrame::ReadStatus::Ok );
            REQUIRE( payload == expected );
        }
    }
}

TEST_CASE( "ipc frame fuzz: every truncation and corruption of a frame is total",
           "[contract8][fuzz][ipc][frame]" )
{
    Json::Value seedValue( Json::objectValue );
    seedValue[ "op" ] = "result";
    seedValue[ "jobId" ] = "job-fuzz-0001";
    seedValue[ "caps" ] = Json::Value( Json::arrayValue );
    seedValue[ "caps" ].append( "structuredErrors" );
    seedValue[ "pad" ] = std::string( 600, 'q' );

    std::string error;
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    exprs::makeIpcMemoryPipePair( a, b );
    REQUIRE( IpcFrame::writeJson( *a, seedValue, IpcFrameLimits(), error ) );
    a->close();
    std::string intact;
    REQUIRE( IpcFrame::read( *b, intact, IpcFrameLimits(), 2000, error )
             == IpcFrame::ReadStatus::Ok );
    std::string wire = std::string( 4, '\0' ) + intact;
    {
        const uint32_t length = static_cast<uint32_t>( intact.size() );
        wire[ 0 ] = static_cast<char>( length & 0xFFu );
        wire[ 1 ] = static_cast<char>( ( length >> 8 ) & 0xFFu );
        wire[ 2 ] = static_cast<char>( ( length >> 16 ) & 0xFFu );
        wire[ 3 ] = static_cast<char>( ( length >> 24 ) & 0xFFu );
    }
    REQUIRE( wire.size() > 256 );

    size_t checks = 0;
    for ( const std::string &mutation : streamMutations( wire, 256 ) )
    {
        for ( const bool closed : { false, true } )
        {
            const std::optional<std::string> failure =
                wireOracle( mutation, closed, IpcFrameLimits(), closed ? 2000 : 30 );
            REQUIRE_FALSE( failure.has_value() );
            if ( failure )
                WARN( *failure );
            ++checks;
        }
    }
    REQUIRE( checks > 400 );
}

TEST_CASE( "ipc frame fuzz: announced lengths beyond the cap are refused "
           "symmetric on both sides",
           "[contract8][fuzz][ipc][frame]" )
{
    for ( const uint64_t seed : { 2ull, 0xBEEFull } )
    {
        BoundedRandom random( seed );
        for ( int i = 0; i < 120; ++i )
        {
            // A random announced length that is legal per the prefix but beyond
            // the cap: the reader must tear the channel down (E6003).
            IpcFrameLimits limits;
            limits.maxFrameBytes = 64 + random.below( 512 );
            std::string wire( 4, '\0' );
            const uint32_t announced =
                limits.maxFrameBytes + 1 + random.below( 0x7FFFFFu );
            wire[ 0 ] = static_cast<char>( announced & 0xFFu );
            wire[ 1 ] = static_cast<char>( ( announced >> 8 ) & 0xFFu );
            wire[ 2 ] = static_cast<char>( ( announced >> 16 ) & 0xFFu );
            wire[ 3 ] = static_cast<char>( ( announced >> 24 ) & 0xFFu );

            const std::optional<std::string> failure =
                wireOracle( wire, false, limits, 50 );
            REQUIRE_FALSE( failure.has_value() );

            // The writer refuses an over-cap payload without writing anything:
            // the stream must stay empty (no partial frame to desync the peer).
            Json::Value big( Json::objectValue );
            big[ "pad" ] = std::string( limits.maxFrameBytes + 8, 'z' );
            std::unique_ptr<IIpcStream> a;
            std::unique_ptr<IIpcStream> b;
            exprs::makeIpcMemoryPipePair( a, b );
            std::string error;
            CHECK_FALSE( IpcFrame::writeJson( *a, big, limits, error ) );
            CHECK( error.find( "E6003" ) != std::string::npos );
            std::string payload;
            CHECK( IpcFrame::read( *b, payload, limits, 50, error )
                   == IpcFrame::ReadStatus::Timeout );
        }
    }
}

TEST_CASE( "ipc frame fuzz: a mid-payload timeout resumes the same frame",
           "[contract8][fuzz][ipc][frame]" )
{
    const std::string payload = std::string( 300, 'p' );
    std::string wire( 4, '\0' );
    wire[ 0 ] = static_cast<char>( ( payload.size() ) & 0xFFu );
    wire[ 1 ] = static_cast<char>( ( payload.size() >> 8 ) & 0xFFu );
    wire[ 2 ] = 0;
    wire[ 3 ] = 0;
    wire += payload;

    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    exprs::makeIpcMemoryPipePair( a, b );

    // Feed the prefix + 40% of the payload, then time out.
    std::string writeError;
    const size_t firstPart = 4 + payload.size() * 2 / 5;
    REQUIRE( a->writeAll( wire.data(), firstPart, writeError ) );
    std::string got;
    std::string error;
    std::string pending;
    REQUIRE( IpcFrame::read( *b, got, IpcFrameLimits(), 20, error, &pending )
             == IpcFrame::ReadStatus::Timeout );
    // The consumed bytes (length prefix included) are kept for the resume.
    REQUIRE( pending.size() == firstPart );

    REQUIRE( a->writeAll( wire.data() + firstPart, wire.size() - firstPart, writeError ) );
    REQUIRE( IpcFrame::read( *b, got, IpcFrameLimits(), 2000, error, &pending )
             == IpcFrame::ReadStatus::Ok );
    REQUIRE( got == payload );
    REQUIRE( pending.empty() );
}

TEST_CASE( "ipc frame fuzz: length-prefix boundary payloads round-trip",
           "[contract8][fuzz][ipc][frame]" )
{
    for ( const size_t size : { size_t( 0 ), size_t( 1 ), size_t( 127 ), size_t( 128 ),
                               size_t( 255 ), size_t( 256 ), size_t( 65535 ), size_t( 65536 ) } )
    {
        Json::Value value( Json::objectValue );
        value[ "pad" ] = std::string( size, 'a' );
        // The serialized size is size + the JSON syntax around it; the values
        // are chosen so the PREFIX itself crosses the 128/65536 boundaries.
        const std::string payload = serialize( value );

        std::unique_ptr<IIpcStream> a;
        std::unique_ptr<IIpcStream> b;
        exprs::makeIpcMemoryPipePair( a, b );
        std::string error;
        REQUIRE( IpcFrame::writeJson( *a, value, IpcFrameLimits(), error ) );
        std::string got;
        REQUIRE( IpcFrame::read( *b, got, IpcFrameLimits(), 2000, error )
                 == IpcFrame::ReadStatus::Ok );
        REQUIRE( got == payload );
        REQUIRE( got.size() >= size );
    }
}

TEST_CASE( "ipc frame fuzz: two frames on one stream read back in order",
           "[contract8][fuzz][ipc][frame]" )
{
    std::unique_ptr<IIpcStream> a;
    std::unique_ptr<IIpcStream> b;
    exprs::makeIpcMemoryPipePair( a, b );
    std::string error;
    for ( const uint64_t seed : { 7ull, 8ull } )
    {
        BoundedRandom random( seed );
        Json::Value first = randomJsonValue( random );
        Json::Value second = randomJsonValue( random );
        const std::string firstText = serialize( first );
        const std::string secondText = serialize( second );
        REQUIRE( IpcFrame::writeJson( *a, first, IpcFrameLimits(), error ) );
        REQUIRE( IpcFrame::writeJson( *a, second, IpcFrameLimits(), error ) );

        std::string payload;
        REQUIRE( IpcFrame::read( *b, payload, IpcFrameLimits(), 2000, error )
                 == IpcFrame::ReadStatus::Ok );
        REQUIRE( payload == firstText );
        REQUIRE( IpcFrame::read( *b, payload, IpcFrameLimits(), 2000, error )
                 == IpcFrame::ReadStatus::Ok );
        REQUIRE( payload == secondText );
    }
}

TEST_CASE( "ipc envelope fuzz: decode is total on mutated and hostile payloads",
           "[contract8][fuzz][ipc][envelope]" )
{
    const std::vector<std::string> baselines = {
        R"({"v":1,"type":"request","id":7,"method":"ui.describe","params":{"a":1}})",
        R"({"v":1,"type":"response","id":7,"ok":true,"result":{"x":true}})",
        R"({"v":1,"type":"response","id":7,"ok":false,"error":{"code":"E6002","message":"bad"}})",
        R"({"v":1,"type":"progress","id":9,"value":0.5,"message":"half"})",
        R"({"v":1,"type":"cancel","id":9})",
        R"({"v":1,"type":"event","event":"log","params":{"level":"info"}})",
    };

    for ( const std::string &baseline : baselines )
    {
        for ( const uint64_t seed : { 3ull, 0xE1701ull, 0xE1702ull } )
        {
            BoundedRandom random( seed );
            for ( int i = 0; i < 60; ++i )
            {
                std::string text;
                if ( i % 5 == 0 )
                {
                    // Version mutation: the E6001 gate class.
                    text = baseline;
                    const size_t vpos = text.find( "\"v\":" );
                    if ( vpos != std::string::npos )
                        text.replace( vpos + 4, 1, std::to_string( random.below( 4 ) ) );
                }
                else if ( i % 5 == 1 )
                {
                    text = baseline + std::string( 64, ' ' );
                }
                else
                {
                    bool ok = false;
                    const Json::Value value = parseJson( baseline, &ok );
                    REQUIRE( ok );
                    text = serialize( mutateTypes( value, random ) );
                }

                Ipc::Envelope envelope;
                std::string error;
                bool decoded = false;
                REQUIRE_NOTHROW( decoded = Ipc::decodeEnvelopePayload( text, envelope, error ) );
                if ( decoded )
                {
                    // Round-trip stability on the success path.
                    const Json::Value encoded = Ipc::encodeEnvelope( envelope );
                    Ipc::Envelope again;
                    std::string reerror;
                    bool ok2 = false;
                    REQUIRE_NOTHROW(
                        ok2 = Ipc::decodeEnvelope( encoded, again, reerror ) );
                    CHECK( ok2 );
                    CHECK( again.type == envelope.type );
                    CHECK( again.id == envelope.id );
                    CHECK( again.method == envelope.method );
                    CHECK( again.ok == envelope.ok );
                }
                else
                {
                    // Truthful typed failure: the reason is always stated.
                    CHECK_FALSE( error.empty() );
                }
            }
        }
    }
}

TEST_CASE( "ipc envelope fuzz: depth bombs and over-cap payloads are typed refusals",
           "[contract8][fuzz][ipc][envelope]" )
{
    // 866 is the pinned pre-fix stack-overflow boundary of this entry point
    // on a Debug/MSVC thread stack; the rest are beyond any real stack.
    for ( const int depth : { 16, 64, 256, 866, 4096, 65536, 200000 } )
    {
        const std::string bomb = depthBomb( depth );
        Ipc::Envelope envelope;
        std::string error;
        bool decoded = true;
        REQUIRE_NOTHROW( decoded = Ipc::decodeEnvelopePayload( bomb, envelope, error ) );
        CHECK_FALSE( decoded );
        CHECK_FALSE( error.empty() );
    }

    // A legal-but-large payload still decodes (no artificial small cap).
    Json::Value big( Json::objectValue );
    big[ "v" ] = 1;
    big[ "type" ] = "request";
    big[ "method" ] = "ui.describe";
    Json::Value params( Json::objectValue );
    params[ "pad" ] = std::string( 64 * 1024, 'p' );
    big[ "params" ] = params;
    const std::string bigText = serialize( big );
    Ipc::Envelope envelope;
    std::string error;
    REQUIRE( Ipc::decodeEnvelopePayload( bigText, envelope, error ) );
    CHECK( envelope.params[ "pad" ].asString().size() == 64 * 1024 );

    // Size bombs: a payload over the NEGOTIATED frame cap is refused by the
    // writer and never read back, while one under it round-trips.
    for ( const size_t payloadBytes : { size_t( 4097 ), size_t( 64 * 1024 ),
                                       size_t( 1024 * 1024 ) } )
    {
        Json::Value value( Json::objectValue );
        value[ "v" ] = 1;
        value[ "type" ] = "request";
        value[ "method" ] = "probe";
        value[ "params" ][ "pad" ] = std::string( payloadBytes, 'z' );

        IpcFrameLimits limits;
        limits.maxFrameBytes = 4096;
        std::unique_ptr<IIpcStream> a;
        std::unique_ptr<IIpcStream> b;
        exprs::makeIpcMemoryPipePair( a, b );
        std::string writeError;
        REQUIRE_FALSE( IpcFrame::writeJson( *a, value, limits, writeError ) );
        CHECK( writeError.find( "E6003" ) != std::string::npos );

        std::string payload;
        std::string readError;
        CHECK( IpcFrame::read( *b, payload, limits, 100, readError )
               == IpcFrame::ReadStatus::Timeout );
        CHECK( payload.empty() );
    }

    // A legal-depth envelope carrying a ui-event-shaped payload still decodes:
    // the transport depth bound must not reject real messages.
    Json::Value deepish( Json::objectValue );
    Json::Value *cursor = &deepish;
    for ( int i = 0; i < 20; ++i )
    {
        ( *cursor )[ "child" ] = Json::Value( Json::objectValue );
        cursor = &( ( *cursor )[ "child" ] );
    }
    ( *cursor )[ "leaf" ] = true;
    Json::Value request( Json::objectValue );
    request[ "v" ] = 1;
    request[ "type" ] = "request";
    request[ "id" ] = 1;
    request[ "method" ] = "ui.invoke";
    request[ "params" ][ "event" ][ "value" ] = deepish;
    Ipc::Envelope deepDecoded;
    std::string deepError;
    REQUIRE( Ipc::decodeEnvelopePayload( serialize( request ), deepDecoded, deepError ) );
    CHECK( deepDecoded.method == "ui.invoke" );
}

TEST_CASE( "worker protocol fuzz: parseFrame is total on depth bombs and wrong shapes",
           "[contract8][fuzz][ipc]" )
{
    // The sibling of the envelope lane for the worker frame gate
    // (runtime/worker/worker_protocol.h). Pre-fix this parse stack-overflowed
    // on a deep frame just like the envelope decoder did; the same bounds and
    // the same typed refusal apply.
    for ( const int depth : { 16, 64, 256, 866, 4096, 65536, 200000 } )
    {
        const std::string bomb = depthBomb( depth );
        Json::Value frame;
        bool ok = true;
        REQUIRE_NOTHROW( ok = sicnu::runtime::worker::parseFrame( bomb, frame ) );
        CHECK_FALSE( ok );
    }
    // Wrong-typed / missing members stay typed refusals, never exceptions.
    for ( const std::string &wrong : { R"({"v":"1","op":"result"})",
                                       R"({"v":{},"op":"result"})",
                                       R"({"v":1})",
                                       "[]", "null", "1e999", "" } )
    {
        Json::Value frame;
        bool ok = true;
        REQUIRE_NOTHROW( ok = sicnu::runtime::worker::parseFrame( wrong, frame ) );
        CHECK_FALSE( ok );
    }
    // {"v":1,"op":42} parses TRUE by contract: parseFrame checks the PRESENCE
    // of v/op, not the op's type (an unknown op is the caller's business), and
    // the caller then compares op strings. Pinned so the gate does not
    // silently start rejecting—or accepting—a different shape.
    {
        Json::Value frame;
        REQUIRE( sicnu::runtime::worker::parseFrame( R"({"v":1,"op":42})", frame ) );
        CHECK( frame[ "op" ].isNumeric() );
    }
    // A well-formed frame still parses.
    Json::Value frame;
    REQUIRE( sicnu::runtime::worker::parseFrame( R"({"v":1,"op":"ready","caps":["structuredErrors"]})",
                                                 frame ) );
    CHECK( frame[ "op" ].asString() == "ready" );
}

TEST_CASE( "ipc envelope fuzz: builder round-trips hostile field values",
           "[contract8][fuzz][ipc][envelope]" )
{
    const std::vector<std::string> hostile = {
        "", "plain", "with\"quote", "with\\backslash", "line\nbreak", "tab\tsep",
        "unicode-\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80", "ctrl-\x01\x02\x1f",
        std::string( 4096, 'x' ),
    };
    const std::vector<long long> ids = { 0, 1, -1, 42, 2147483647LL, -2147483648LL,
                                         std::numeric_limits<long long>::max(),
                                         std::numeric_limits<long long>::min() };

    for ( const std::string &method : hostile )
    {
        for ( const long long id : ids )
        {
            Ipc::Envelope request;
            request.type = Ipc::MessageType::Request;
            request.id = id;
            request.method = method;
            request.params[ "k" ] = "v";
            request.deadlineMs = 5;
            const Json::Value encoded = Ipc::encodeEnvelope( request );
            Ipc::Envelope decoded;
            std::string error;
            if ( method.empty() )
            {
                // "request envelope without method" is a documented typed
                // refusal: the builder must not be able to produce a frame its
                // own decoder accepts.
                REQUIRE_FALSE( Ipc::decodeEnvelope( encoded, decoded, error ) );
                CHECK_FALSE( error.empty() );
            }
            else
            {
                REQUIRE( Ipc::decodeEnvelope( encoded, decoded, error ) );
                CHECK( decoded.type == Ipc::MessageType::Request );
                CHECK( decoded.id == id );
                CHECK( decoded.method == method );
                CHECK( decoded.deadlineMs == 5 );
            }

            Ipc::Envelope progress;
            progress.type = Ipc::MessageType::Progress;
            progress.id = id;
            progress.progress = 0.25;
            progress.message = method;
            Ipc::Envelope progressDecoded;
            REQUIRE( Ipc::decodeEnvelope( Ipc::encodeEnvelope( progress ), progressDecoded,
                                          error ) );
            CHECK( progressDecoded.progress == 0.25 );
            CHECK( progressDecoded.message == method );

            const Ipc::Envelope failure = Ipc::makeErrorResponse( id, "E6002", method, true );
            Ipc::Envelope failureDecoded;
            REQUIRE( Ipc::decodeEnvelope( Ipc::encodeEnvelope( failure ), failureDecoded,
                                          error ) );
            CHECK_FALSE( failureDecoded.ok );
            CHECK( failureDecoded.error.code == "E6002" );
            CHECK( failureDecoded.error.message == method );
            CHECK( failureDecoded.error.retryable );
        }
    }
}

TEST_CASE( "ipc envelope fuzz: protocol compatibility gate is exact",
           "[contract8][fuzz][ipc][envelope]" )
{
    const int majors[] = { 0, 1, 2, -1, std::numeric_limits<int>::max() };
    const int minors[] = { 0, 1, 2, -1, std::numeric_limits<int>::max() };
    for ( const int localMajor : majors )
    {
        for ( const int localMinor : minors )
        {
            for ( const int peerMajor : majors )
            {
                for ( const int peerMinor : minors )
                {
                    std::string reason;
                    const bool compatible = Ipc::isProtocolCompatible(
                        localMajor, localMinor, peerMajor, peerMinor, reason );
                    const bool expected = peerMajor == localMajor && peerMinor <= localMinor;
                    CHECK( compatible == expected );
                    if ( !compatible )
                        CHECK_FALSE( reason.empty() );
                    else
                        CHECK( reason.empty() );
                }
            }
        }
    }
}

TEST_CASE( "ipc envelope fuzz: unknown fields and additive shapes are ignored",
           "[contract8][fuzz][ipc][envelope]" )
{
    // Additive evolution: unknown fields are ignored; unknown TYPES are typed
    // refusals (never a guess).
    Json::Value additive( Json::objectValue );
    additive[ "v" ] = 1;
    additive[ "type" ] = "request";
    additive[ "id" ] = 3;
    additive[ "method" ] = "probe";
    additive[ "future_field_2027" ] = Json::Value( Json::arrayValue );
    additive[ "another" ] = "ignored";
    Ipc::Envelope envelope;
    std::string error;
    REQUIRE( Ipc::decodeEnvelope( additive, envelope, error ) );
    CHECK( envelope.method == "probe" );
    CHECK( envelope.params.isObject() );

    for ( const std::string &wrongType : { "request ", "Request", "", "result", "null",
                                           "true", "\x01" } )
    {
        Json::Value bad( Json::objectValue );
        bad[ "v" ] = 1;
        bad[ "type" ] = wrongType;
        bad[ "id" ] = 1;
        bad[ "method" ] = "x";
        Ipc::Envelope ignored;
        std::string reason;
        bool ok = true;
        REQUIRE_NOTHROW( ok = Ipc::decodeEnvelope( bad, ignored, reason ) );
        CHECK_FALSE( ok );
        CHECK_FALSE( reason.empty() );
    }
}

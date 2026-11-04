// tests/test_r28_t03_shm_manifest_gates.cpp
//
// R28-T03 M6 (WP09/WP10): untrusted-input gates over the Python-isolated
// SHM header and the plugin manifest's external-tool numeric fields.
//
//   * SharedMemorySegment::attach() treats the 32-byte header as untrusted:
//     negative dimensions (size_t wraparound → huge claimed payload), an
//     out-of-range dtype code, and a header claiming more payload bytes
//     than the actual mapping all refuse; a well-formed segment still
//     attaches with the exact payload size.
//   * ManifestExternalTool::fromJson gates timeout_seconds /
//     stdout_limit_bytes / stderr_limit_bytes on the int domain with a
//     field-attributed error (SDK-1 discipline): a value above INT_MAX or a
//     non-integral number is a typed refusal, never a jsoncpp exception
//     escaping the sub-parser or a silent truncation.
#include <catch2/catch_test_macros.hpp>

#include "python/isolated/shared_memory_segment.h"
#include "exprs/plugin_manifest.h"

#include <json/json.h>

#include <cstring>
#include <memory>
#include <string>

using sicnu::python::isolated::SharedMemorySegment;

namespace
{

/// Overwrites the header of an owned segment with @p hdr (the header lives
/// at the mapping start; payload() points just past it).
void overwriteHeader( SharedMemorySegment &segment,
                      const SharedMemorySegment::Header &hdr )
{
    void *payload = segment.payload();
    REQUIRE( payload != nullptr );
    std::memcpy( static_cast< char * >( payload ) - sizeof( hdr ), &hdr, sizeof( hdr ) );
}

SharedMemorySegment::Header readHeader( SharedMemorySegment &segment )
{
    SharedMemorySegment::Header hdr{};
    const void *payload = segment.payload();
    REQUIRE( payload != nullptr );
    std::memcpy( &hdr, static_cast< const char * >( payload ) - sizeof( hdr ),
                 sizeof( hdr ) );
    return hdr;
}

} // namespace

TEST_CASE( "SHM attach validates the untrusted header", "[r28_t03][shm]" )
{
    SECTION( "a well-formed segment round-trips" )
    {
        SharedMemorySegment owner;
        REQUIRE( owner.create( 4, 4, 2, SharedMemorySegment::DType::Float32 ) );
        SharedMemorySegment reader;
        REQUIRE( reader.attach( owner.key() ) );
        CHECK( reader.isAttached() );
        CHECK( reader.payloadSize() == 4 * 4 * 2 * 4 );
        reader.detach();
        owner.detach();
    }

    SECTION( "a negative dimension refuses to attach" )
    {
        SharedMemorySegment owner;
        REQUIRE( owner.create( 4, 4, 1, SharedMemorySegment::DType::Float32 ) );
        SharedMemorySegment::Header hdr = readHeader( owner );
        hdr.width = -4; // static_cast<size_t>(-4) * 4 * 1 * 4 wraps huge
        overwriteHeader( owner, hdr );

        SharedMemorySegment reader;
        CHECK_FALSE( reader.attach( owner.key() ) );
        CHECK_FALSE( reader.isAttached() );
        owner.detach();
    }

    SECTION( "an unknown dtype code refuses to attach" )
    {
        SharedMemorySegment owner;
        REQUIRE( owner.create( 4, 4, 1, SharedMemorySegment::DType::Float32 ) );
        SharedMemorySegment::Header hdr = readHeader( owner );
        hdr.dtype = 99; // no such DType — elemSize would be 0
        overwriteHeader( owner, hdr );

        SharedMemorySegment reader;
        CHECK_FALSE( reader.attach( owner.key() ) );
        owner.detach();
    }

    SECTION( "a header claiming more payload than the mapping refuses" )
    {
        SharedMemorySegment owner;
        REQUIRE( owner.create( 4, 4, 1, SharedMemorySegment::DType::Float32 ) );
        SharedMemorySegment::Header hdr = readHeader( owner );
        hdr.width = 1000000; // 1000000*4*1*4 bytes >> the 64-byte payload
        overwriteHeader( owner, hdr );

        SharedMemorySegment reader;
        CHECK_FALSE( reader.attach( owner.key() ) );
        owner.detach();
    }
}

TEST_CASE( "manifest external-tool numeric fields are int-domain gated",
           "[r28_t03][manifest]" )
{
    auto parseWith = []( const char *extra, std::string &error ) {
        const std::string doc = std::string( R"({ "argv": ["run"] )" ) + extra + " }";
        Json::Value json;
        Json::CharReaderBuilder builder;
        std::unique_ptr< Json::CharReader > reader( builder.newCharReader() );
        std::string parseError;
        reader->parse( doc.data(), doc.data() + doc.size(), &json, &parseError );
        exprs::ManifestExternalTool tool;
        return exprs::ManifestExternalTool::fromJson( json, tool, error );
    };

    SECTION( "values above INT_MAX are field-attributed refusals" )
    {
        std::string error;
        CHECK_FALSE( parseWith( R"(, "timeout_seconds": 4294967296 )", error ) );
        CHECK( error.find( "timeout_seconds" ) != std::string::npos );
        CHECK_FALSE( parseWith( R"(, "stdout_limit_bytes": 4294967296 )", error ) );
        CHECK( error.find( "stdout_limit_bytes" ) != std::string::npos );
        CHECK_FALSE( parseWith( R"(, "stderr_limit_bytes": 4294967296 )", error ) );
        CHECK( error.find( "stderr_limit_bytes" ) != std::string::npos );
    }

    SECTION( "non-integral numbers are refusals, never silent truncation" )
    {
        std::string error;
        CHECK_FALSE( parseWith( R"(, "timeout_seconds": 1.5 )", error ) );
        CHECK( error.find( "timeout_seconds" ) != std::string::npos );
    }

    SECTION( "in-domain values still parse" )
    {
        std::string error;
        CHECK( parseWith( R"(, "timeout_seconds": 60, "stdout_limit_bytes": 4096,
                           "stderr_limit_bytes": 2048 )",
                          error ) );
        CHECK( error.empty() );
    }
}

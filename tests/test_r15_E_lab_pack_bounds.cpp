// test_r15_E_lab_pack_bounds.cpp — R15-E / E09 oracle: 实验包解析必须有界.
//
// Reproduced on master 46791bc65c6f552ebbcf723bb91892736d56f319:
// PackVerifier::loadFromBytes() called Json::CharReaderBuilder without ever
// setting "stackLimit", and put no bound on the manifest byte count. A
// corrupt / hostile pack (`[[[[[...`) therefore reached the recursive parser
// with the library default stack allowance, and an arbitrarily large buffer
// was parsed before any schema check could reject it.
//
// The pack manifest is a file index — a few kilobytes at most — so both a
// byte cap and an explicit nesting cap are safe for every real pack.
#include "lab_pack/lab_pack.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using sicnu::labpack::PackVerifier;

namespace
{

std::string deepNested( int depth )
{
    std::string s;
    s.reserve( static_cast<std::size_t>( depth ) * 2 + 1 );
    for ( int i = 0; i < depth; ++i )
        s += '[';
    for ( int i = 0; i < depth; ++i )
        s += ']';
    return s;
}

} // namespace

TEST_CASE( "lab pack loader rejects a manifest larger than the byte cap",
           "[r15e][e09][lab-pack]" )
{
    // 9 MiB of otherwise well-formed JSON text — far beyond any file index.
    const std::string huge = std::string( 9u * 1024u * 1024u, ' ' );
    const auto result = PackVerifier::loadFromBytes( huge, "test:oversize" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_schema" );
    REQUIRE( result.errorMessage.find( "too large" ) != std::string::npos );
}

TEST_CASE( "lab pack loader rejects nesting beyond the stack limit",
           "[r15e][e09][lab-pack]" )
{
    const auto result = PackVerifier::loadFromBytes( deepNested( 5000 ), "test:deep" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_schema" );
}

TEST_CASE( "lab pack loader still rejects a small, shallow, wrong-schema document",
           "[r15e][e09][lab-pack]" )
{
    // Negative control: the caps must not turn every failure into a size
    // error — ordinary schema rejection keeps its own path.
    const auto result = PackVerifier::loadFromBytes( R"({"schema_version":"nope"})",
                                                     "test:schema" );
    REQUIRE( !result.ok );
    REQUIRE( result.errorCode == "lab.pack_schema" );
    REQUIRE( result.errorMessage.find( "too large" ) == std::string::npos );
}

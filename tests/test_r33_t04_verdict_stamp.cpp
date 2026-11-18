// test_r33_t04_verdict_stamp.cpp — R33-T04 / WP13 (#1953 residual):
//
//   makeVerdictStamp read the artifact once for the digest (accumulating
//   the WHOLE file in memory) and then took the size from a SECOND,
//   unchecked open whose failed tellg() cast -1 into a huge unsigned — so
//   the stamped size could describe a file that was never observed. The
//   stamp is now built from ONE streaming pass: digest and byte count come
//   from the same stream, memory stays bounded, and any open/read failure
//   yields an INVALID (empty) stamp.
#include "lab/sha256.h"
#include "teaching/verdict_stamp.h"

#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace sicnu::teaching;

namespace
{

void writeBytes( const QString &path, const std::string &bytes )
{
    std::ofstream out( path.toStdString(), std::ios::binary );
    out.write( bytes.data(), static_cast<std::streamsize>( bytes.size() ) );
}

std::string patternBytes( std::size_t length )
{
    std::string bytes( length, '\0' );
    for ( std::size_t i = 0; i < length; ++i )
        bytes[i] = static_cast<char>( ( i * 7 + 3 ) & 0xff );
    return bytes;
}
} // namespace

TEST_CASE( "streaming LabSha256 matches the one-shot digest across chunk boundaries",
           "[teaching][verdict][r33t04]" )
{
    using sicnu::lab::LabSha256;
    using sicnu::lab::sha256Hex;

    // Known-answer: FIPS 180-4 empty + "abc".
    CHECK( sha256Hex( "" ) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );
    CHECK( sha256Hex( "abc" )
           == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );

    // Streaming must equal one-shot for sizes crossing the internal 64-byte
    // block boundary and odd tails.
    for ( const std::size_t size : { 0u, 1u, 63u, 64u, 65u, 127u, 128u, 4096u } )
    {
        const std::string bytes = patternBytes( size );
        LabSha256 streaming;
        // Feed in deliberately awkward sub-chunks to exercise buffering.
        std::size_t fed = 0;
        while ( fed < bytes.size() )
        {
            const std::size_t chunk = std::min<std::size_t>( 17, bytes.size() - fed );
            streaming.update( bytes.data() + fed, chunk );
            fed += chunk;
        }
        CHECK( streaming.finalizeHex() == sha256Hex( bytes ) );
    }
}

TEST_CASE( "makeVerdictStamp size and digest come from the same single pass",
           "[teaching][verdict][r33t04]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "artifact.bin" ) );
    const std::string summary( 64, 'd' );

    for ( const std::size_t size :
          { 1u, 63u, 64u, 65u, 1024u, 64u * 1024u + 3u, 3u * 64u * 1024u + 1u } )
    {
        const std::string bytes = patternBytes( size );
        writeBytes( path, bytes );

        const std::string stamp = makeVerdictStamp( summary, path.toStdString() );
        REQUIRE_FALSE( stamp.empty() );

        const VerdictStamp parsed = parseVerdictStamp( stamp );
        REQUIRE( parsed.parsed );
        // The stamped size is the file's REAL byte count — not a -1 cast
        // from a failed second-open tellg, and not a stale first-look size.
        CHECK( parsed.size == size );
        CHECK( parsed.contentDigest == sicnu::lab::sha256Hex( bytes ) );
        CHECK( parsed.summaryDigest == summary );
    }
}

TEST_CASE( "stamp failures are explicit: empty path, missing file, empty artifact",
           "[teaching][verdict][r33t04]" )
{
    const std::string summary( 64, 'd' );

    CHECK( makeVerdictStamp( summary, "" ).empty() );
    CHECK( makeVerdictStamp( summary, "/nonexistent/r33t04/artifact.bin" ).empty() );
    CHECK( verdictArtifactContentDigest( "/nonexistent/r33t04/artifact.bin" ).empty() );

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString empty = dir.filePath( QStringLiteral( "empty.bin" ) );
    writeBytes( empty, "" );
    CHECK( verdictArtifactContentDigest( empty.toStdString() ).empty() );
    CHECK( makeVerdictStamp( summary, empty.toStdString() ).empty() );
}

TEST_CASE( "a mid-file rewrite changes the digest (full coverage, single pass)",
           "[teaching][verdict][r33t04]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( QStringLiteral( "drift.bin" ) );
    std::string bytes = patternBytes( 256u * 1024u );
    writeBytes( path, bytes );
    const std::string before = verdictArtifactContentDigest( path.toStdString() );
    REQUIRE_FALSE( before.empty() );

    // Flip one byte near the middle — the digest must drift.
    bytes[bytes.size() / 2] = static_cast<char>( ~bytes[bytes.size() / 2] );
    writeBytes( path, bytes );
    CHECK( verdictArtifactContentDigest( path.toStdString() ) != before );
}

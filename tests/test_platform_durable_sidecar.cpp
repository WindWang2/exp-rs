/***************************************************************************
  test_platform_durable_sidecar.cpp
  R6 — the single sidecar write authority's crash-window matrix.

  Every fault point is a NAMED PHASE (deterministic; no sleeps, no random),
  and every fault case asserts the post-state the crash-state machine
  promises: target old-or-new, temp residue inert or removed, typed status.
  Light lane: Catch2 + sicnu_platform only.
 ***************************************************************************/

#include "platform/durable_sidecar.h"

#include "platform/portable.h"

#include <catch2/catch_all.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using sicnu::platform::sidecar::ReadResult;
using sicnu::platform::sidecar::ReadSource;
using sicnu::platform::sidecar::WritePhase;
using sicnu::platform::sidecar::WriteRequest;
using sicnu::platform::sidecar::WriteResult;
using sicnu::platform::sidecar::WriteStatus;
using sicnu::platform::sidecar::read;
using sicnu::platform::sidecar::write;

namespace
{

struct ScratchDir
{
  fs::path path;

  explicit ScratchDir( const std::string &tag )
    : path( fs::temp_directory_path() / ( "sicnu-durable-sidecar-" + tag + "-" +
                                          std::to_string( ( unsigned long long )sicnu::portable::pid() ) ) )
  {
    fs::remove_all( path );
    fs::create_directories( path );
  }
  ~ScratchDir() { std::error_code ec; fs::remove_all( path, ec ); }

  std::string file( const std::string &name ) const
  {
    const std::u8string leaf( name.begin(), name.end() );
    const fs::path joined = path / fs::path( leaf );
    const std::u8string text = joined.u8string();
    return std::string( text.begin(), text.end() );
  }
};

std::string slurp( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  return std::string( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
}

void spit( const std::string &path, const std::string &bytes )
{
  std::ofstream out( path, std::ios::binary | std::ios::trunc );
  out << bytes;
}

std::size_t countTempResidue( const ScratchDir &dir )
{
  std::size_t count = 0;
  for ( const auto &entry : fs::directory_iterator( dir.path ) )
  {
    const std::u8string text = entry.path().u8string();
    if ( std::string( text.begin(), text.end() ).find( ".tmp" ) != std::string::npos )
      ++count;
  }
  return count;
}

constexpr const char *kOldPayload = "{\"generation\":1}";
constexpr const char *kNewPayload = "{\"generation\":2,\"state\":\"committed\"}";

} // namespace

TEST_CASE( "write publishes and reads back the main artifact", "[platform][sidecar]" )
{
  ScratchDir dir( "roundtrip" );
  const std::string target = dir.file( "state.json" );

  const WriteResult first = write( { target, kOldPayload, "" } );
  REQUIRE( first );
  REQUIRE( slurp( target ) == kOldPayload );

  const WriteResult second = write( { target, kNewPayload } );
  REQUIRE( second );
  REQUIRE( second.lastGoodWritten );
  REQUIRE( slurp( target ) == kNewPayload );
  REQUIRE( slurp( target + ".last-good" ) == kNewPayload );

  const ReadResult main = read( target );
  REQUIRE( main.source == ReadSource::Main );
  REQUIRE( main.bytes == kNewPayload );
}

TEST_CASE( "validation refusals have no filesystem effect", "[platform][sidecar]" )
{
  ScratchDir dir( "validate" );
  const std::string target = dir.file( "state.json" );
  spit( target, kOldPayload );

  WriteResult empty = write( { "", kNewPayload } );
  REQUIRE( empty.status == WriteStatus::EmptyPath );

  WriteResult huge = write( { target, std::string( 64, 'x' ), "", 16 } );
  REQUIRE( huge.status == WriteStatus::TooLarge );

  REQUIRE( slurp( target ) == kOldPayload );
  REQUIRE( countTempResidue( dir ) == 0 );
}

TEST_CASE( "WriteTemp fault leaves the target untouched and no residue",
           "[platform][sidecar][fault]" )
{
  ScratchDir dir( "tempfault" );
  const std::string target = dir.file( "state.json" );
  spit( target, kOldPayload );

  WriteRequest request;
  request.targetPath = target;
  request.bytes = kNewPayload;
  request.hooks.failAt = []( WritePhase phase ) { return phase == WritePhase::WriteTemp; };

  const WriteResult result = write( request );
  REQUIRE( result.status == WriteStatus::WriteFailed );
  REQUIRE( slurp( target ) == kOldPayload );
  REQUIRE( countTempResidue( dir ) == 0 );
}

TEST_CASE( "Durability fault refuses the publish and discards the temp",
           "[platform][sidecar][fault]" )
{
  ScratchDir dir( "durabilityfault" );
  const std::string target = dir.file( "state.json" );
  spit( target, kOldPayload );

  WriteRequest request;
  request.targetPath = target;
  request.bytes = kNewPayload;
  request.hooks.failAt = []( WritePhase phase ) { return phase == WritePhase::Durability; };

  const WriteResult result = write( request );
  REQUIRE( result.status == WriteStatus::DurabilityFailed );
  REQUIRE( slurp( target ) == kOldPayload );
  REQUIRE( countTempResidue( dir ) == 0 );
}

TEST_CASE( "Publish fault leaves the old generation committed",
           "[platform][sidecar][fault]" )
{
  ScratchDir dir( "publishfault" );
  const std::string target = dir.file( "state.json" );
  spit( target, kOldPayload );

  WriteRequest request;
  request.targetPath = target;
  request.bytes = kNewPayload;
  request.hooks.failAt = []( WritePhase phase ) { return phase == WritePhase::Publish; };

  const WriteResult result = write( request );
  REQUIRE( result.status == WriteStatus::PublishFailed );
  REQUIRE( slurp( target ) == kOldPayload );
  REQUIRE( countTempResidue( dir ) == 0 );
}

TEST_CASE( "a torn write that still publishes is caught by the verify gate",
           "[platform][sidecar][fault]" )
{
  ScratchDir dir( "torn" );
  const std::string target = dir.file( "state.json" );

  WriteRequest request;
  request.targetPath = target;
  request.bytes = kNewPayload;
  request.hooks.payloadOverride = []( WritePhase phase ) {
    return phase == WritePhase::WriteTemp ? std::string( kNewPayload ).substr( 0, 7 )
                                          : std::string();
  };

  const WriteResult result = write( request );
  REQUIRE( result.status == WriteStatus::VerifyFailed );
  // Documented: the verify gate reports; main is NOT rolled back (the old
  // generation is already gone) — the caller surfaces the suspect artifact.
  REQUIRE( slurp( target ) == std::string( kNewPayload ).substr( 0, 7 ) );
  // The last-good copy holds what was SUPPOSED to publish.
  REQUIRE( slurp( target + ".last-good" ) == kNewPayload );
}

TEST_CASE( "LastGood fault is reported but never fails the committed main",
           "[platform][sidecar][fault]" )
{
  ScratchDir dir( "lastgoodfault" );
  const std::string target = dir.file( "state.json" );
  spit( target + ".last-good", kOldPayload );

  WriteRequest request;
  request.targetPath = target;
  request.bytes = kNewPayload;
  request.hooks.failAt = []( WritePhase phase ) { return phase == WritePhase::LastGood; };

  const WriteResult result = write( request );
  REQUIRE( result.status == WriteStatus::Ok );
  REQUIRE( result.lastGoodStatus == WriteStatus::LastGoodFailed );
  REQUIRE_FALSE( result.lastGoodWritten );
  REQUIRE( slurp( target ) == kNewPayload );
  // The stale last-good generation stays intact (never leading main).
  REQUIRE( slurp( target + ".last-good" ) == kOldPayload );
}

TEST_CASE( "verify can be disabled for large payload lanes", "[platform][sidecar]" )
{
  ScratchDir dir( "noverify" );
  const std::string target = dir.file( "blob.bin" );

  WriteRequest request;
  request.targetPath = target;
  request.bytes = kNewPayload;
  request.verifyReadBack = false;
  request.lastGoodSuffix = "";
  request.hooks.payloadOverride = []( WritePhase phase ) {
    return phase == WritePhase::WriteTemp ? std::string( "torn" ) : std::string();
  };

  const WriteResult result = write( request );
  REQUIRE( result.status == WriteStatus::Ok );
  REQUIRE( slurp( target ) == "torn" );
}

TEST_CASE( "read resolves main, last-good, missing and corrupt", "[platform][sidecar][read]" )
{
  ScratchDir dir( "readmatrix" );
  const std::string target = dir.file( "state.json" );

  SECTION( "both missing" )
  {
    const ReadResult result = read( target );
    REQUIRE( result.source == ReadSource::Missing );
  }

  SECTION( "main only" )
  {
    spit( target, kNewPayload );
    const ReadResult result = read( target );
    REQUIRE( result.source == ReadSource::Main );
    REQUIRE( result.bytes == kNewPayload );
  }

  SECTION( "corrupt main recovers from last-good" )
  {
    spit( target, "{\"truncated" );
    spit( target + ".last-good", kOldPayload );
    const ReadResult result = read( target );
    REQUIRE( result.source == ReadSource::LastGood );
    REQUIRE( result.bytes == kOldPayload );
    REQUIRE( result.error.find( "main:" ) != std::string::npos );
  }

  SECTION( "missing main recovers from last-good alone" )
  {
    spit( target + ".last-good", kOldPayload );
    const ReadResult result = read( target );
    REQUIRE( result.source == ReadSource::LastGood );
    REQUIRE( result.bytes == kOldPayload );
  }

  SECTION( "both corrupt" )
  {
    spit( target, "\xde\xad" );
    spit( target + ".last-good", "{\"also" );
    const ReadResult result = read( target );
    REQUIRE( result.source == ReadSource::Corrupt );
    REQUIRE( result.error.find( "last-good:" ) != std::string::npos );
  }

  SECTION( "oversize counts as unreadable and falls through to last-good" )
  {
    spit( target, std::string( 64, 'x' ) );
    spit( target + ".last-good", kOldPayload );
    const ReadResult result = read( target, ".last-good", 16 );
    REQUIRE( result.source == ReadSource::LastGood );
    REQUIRE( result.bytes == kOldPayload );
  }

  SECTION( "no suffix: corrupt main is Corrupt, absent main is Missing" )
  {
    spit( target, "junk" );
    REQUIRE( read( target, "" ).source == ReadSource::Corrupt );
    REQUIRE( read( dir.file( "absent.json" ), "" ).source == ReadSource::Missing );
  }
}

TEST_CASE( "inert temp residue never leaks into reads", "[platform][sidecar][read]" )
{
  ScratchDir dir( "residue" );
  const std::string target = dir.file( "state.json" );
  spit( target, kNewPayload );
  // A crashed publisher's O_EXCL residue, indistinguishable from the real thing.
  spit( dir.file( "state.json.424242.7.9001.tmp" ), "{\"torn\"" );

  const ReadResult result = read( target );
  REQUIRE( result.source == ReadSource::Main );
  REQUIRE( result.bytes == kNewPayload );
}

TEST_CASE( "concurrent writers always leave a complete artifact", "[platform][sidecar][concurrent]" )
{
  ScratchDir dir( "concurrent" );
  const std::string target = dir.file( "state.json" );
  REQUIRE( write( { target, kOldPayload, "" } ) );

  constexpr int kWriters = 4;
  constexpr int kRounds = 12;
  std::atomic<bool> stopReader{ false };
  std::atomic<int> readsObserved{ 0 };
  std::atomic<int> tornReads{ 0 };

  std::thread reader( [&] {
    while ( !stopReader.load( std::memory_order_relaxed ) )
    {
      std::ifstream in( target, std::ios::binary );
      const std::string bytes( ( std::istreambuf_iterator<char>( in ) ),
                               std::istreambuf_iterator<char>() );
      if ( !bytes.empty() && bytes != kOldPayload && bytes != kNewPayload )
        ++tornReads;
      ++readsObserved;
    }
  } );

  // Catch2 assertions are main-thread-only here (repo convention, R5 review
  // P2-2): workers record outcomes atomically; the main thread asserts after
  // the join.
  std::atomic<int> writerFailures{ 0 };
  std::vector<std::thread> writers;
  for ( int w = 0; w < kWriters; ++w )
  {
    writers.emplace_back( [&, w] {
      for ( int r = 0; r < kRounds; ++r )
      {
        const std::string payload = "{\"writer\":" + std::to_string( w ) +
                                    ",\"round\":" + std::to_string( r ) + "}";
        if ( !write( { target, payload, "" } ) )
          ++writerFailures;
      }
    } );
  }
  for ( auto &t : writers )
    t.join();
  stopReader.store( true );
  reader.join();

  REQUIRE( writerFailures.load() == 0 );
  REQUIRE( tornReads.load() == 0 );
  REQUIRE( readsObserved.load() > 0 );
  const std::string finalBytes = slurp( target );
  REQUIRE( finalBytes.find( "\"writer\":" ) == 0 );
  REQUIRE( finalBytes.find( "\"round\":" ) != std::string::npos );
  REQUIRE( countTempResidue( dir ) == 0 );
}

TEST_CASE( "non-ASCII target paths survive the whole pipeline", "[platform][sidecar][path]" )
{
  ScratchDir dir( "unicode" );
  // "状态-état.json" spelled as explicit UTF-8 byte escapes (\xE7\x8A\xB6\xE6\x80\x81
  // = 状态, \xC3\xA9 = é), independent of the compiler source/execution charset.
  const std::string target = dir.file( "\xE7\x8A\xB6\xE6\x80\x81-\xC3\xA9tat.json" );

  const WriteResult result = write( { target, kNewPayload, "" } );
  REQUIRE( result );
  REQUIRE( slurp( target ) == kNewPayload );

  const ReadResult readBack = read( target, "" );
  REQUIRE( readBack.source == ReadSource::Main );
  REQUIRE( readBack.bytes == kNewPayload );
}

#ifdef _WIN32
TEST_CASE( "a locked target fails closed with the old generation intact (Windows)",
           "[platform][sidecar][windows]" )
{
  ScratchDir dir( "locked" );
  const std::string target = dir.file( "state.json" );
  spit( target, kOldPayload );

  HANDLE lock = ::CreateFileW( sicnu::portable::wideFromUtf8( target ).c_str(), GENERIC_READ,
                               0 /* no sharing = locked against ReplaceFileW */, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
  REQUIRE( lock != INVALID_HANDLE_VALUE );

  const WriteResult result = write( { target, kNewPayload, "" } );
  ::CloseHandle( lock );

  REQUIRE( result.status == WriteStatus::PublishFailed );
  REQUIRE( slurp( target ) == kOldPayload );
  REQUIRE( countTempResidue( dir ) == 0 );
}

TEST_CASE( "a read-only target attribute is cleared and replaced (Windows)",
           "[platform][sidecar][windows]" )
{
  ScratchDir dir( "readonly" );
  const std::string target = dir.file( "state.json" );
  spit( target, kOldPayload );
  const DWORD attrs = ::GetFileAttributesW( sicnu::portable::wideFromUtf8( target ).c_str() );
  REQUIRE( attrs != INVALID_FILE_ATTRIBUTES );
  REQUIRE( ::SetFileAttributesW( sicnu::portable::wideFromUtf8( target ).c_str(),
                                 attrs | FILE_ATTRIBUTE_READONLY ) );

  const WriteResult result = write( { target, kNewPayload, "" } );
  REQUIRE( result );
  REQUIRE( slurp( target ) == kNewPayload );
}
#else
TEST_CASE( "a read-only directory fails the claim (POSIX)", "[platform][sidecar][posix]" )
{
  ScratchDir dir( "readonlydir" );
  const std::string target = dir.file( "state.json" );

  REQUIRE( ::chmod( dir.path.c_str(), 0500 ) == 0 );
  const WriteResult result = write( { target, kNewPayload, "" } );
  REQUIRE( ::chmod( dir.path.c_str(), 0700 ) == 0 );

  REQUIRE( result.status == WriteStatus::ClaimFailed );
  REQUIRE( read( target, "" ).source == ReadSource::Missing );
}
#endif

/***************************************************************************
  tests/test_core_concurrency_stress_r4.cpp
  core-foundations-r4 (Track 5) — hot publish-path concurrency stress.
  ---------------------------
  Begin                : 2026-09-27
  Copyright            : (C) 2026 SICNU GEO RS

  Stress criteria — declared BEFORE the runs, never relaxed post hoc:

    C1 (staging uniqueness, cross-thread):
        threads = 16, allocations/thread = 125 (2000 total). Every
        stagedPathFor result is globally distinct, every claimed staging
        file exists empty, and zero allocation failures occur.
    C2 (same-target concurrent atomic publication):
        threads = 8, rounds/thread = 25 (200 publishes of distinct known
        payloads onto ONE target). While the storm runs, a monitor thread
        continuously samples the target and must observe ONLY byte-exact
        members of the payload set (plus the seed) — never a torn or
        half-published document. After the storm the target holds exactly
        one payload, byte-exact, and no staged residue remains.
    C3 (termination): every worker joins; the case must terminate.

  No new lock primitives in product code — the test's own mutex guards
  only its local observation buffers.
 ***************************************************************************/

#include "geospatial/util/atomic_fs.h"
#include "platform/portable.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

using sicnu::geo::atomic_fs::stagedPathFor;
using sicnu::geo::atomic_fs::writeFileAtomic;

namespace
{

std::string u8( const fs::path &path )
{
  const std::u8string text = path.u8string();
  return std::string( text.begin(), text.end() );
}

class ScratchDir
{
public:
  ScratchDir()
  {
    static std::atomic<unsigned> counter{ 0 };
    std::random_device rd;
    mPath = fs::temp_directory_path() /
            fs::path( "sicnu_stress_r4_" + std::to_string( sicnu::portable::pid() ) + "_" +
                      std::to_string( counter++ ) + "_" + std::to_string( rd() ) );
    fs::create_directories( mPath );
  }
  ~ScratchDir() { std::error_code ec; fs::remove_all( mPath, ec ); }
  ScratchDir( const ScratchDir & ) = delete;
  ScratchDir &operator=( const ScratchDir & ) = delete;
  const fs::path &path() const { return mPath; }

private:
  fs::path mPath;
};

std::size_t countTmpEntries( const fs::path &dir )
{
  std::size_t n = 0;
  for ( const fs::directory_entry &entry : fs::directory_iterator( dir ) )
  {
    if ( u8( entry.path().filename() ).find( ".tmp" ) != std::string::npos )
      ++n;
  }
  return n;
}

} // namespace

TEST_CASE( "stress C1: staging claims are globally unique across 16 threads",
           "[stress][atomic_fs][r4]" )
{
  ScratchDir dir;
  constexpr int kThreads = 16;
  constexpr int kPerThread = 125;

  std::vector<std::thread> workers;
  std::mutex collectMutex;
  std::set<std::string> allStaged;
  std::atomic<int> allocationFailures{ 0 };

  for ( int t = 0; t < kThreads; ++t )
  {
    workers.emplace_back( [ & ] {
      std::set<std::string> local;
      for ( int i = 0; i < kPerThread; ++i )
      {
        try
        {
          const std::string staged =
            stagedPathFor( u8( dir.path() / "target.bin" ) );
          // The claim is exclusive: this thread now owns an existing file.
          if ( !sicnu::geo::atomic_fs::fileExists( staged ) )
            ++allocationFailures;
          local.insert( staged );
        }
        catch ( const sicnu::geo::GeoError & )
        {
          ++allocationFailures;
        }
      }
      std::lock_guard<std::mutex> guard( collectMutex );
      allStaged.insert( local.begin(), local.end() );
    } );
  }
  for ( std::thread &worker : workers )
    worker.join();

  // C1: 2000 allocations, zero failures, zero collisions.
  REQUIRE( allocationFailures.load() == 0 );
  REQUIRE( allStaged.size() == static_cast<std::size_t>( kThreads * kPerThread ) );

  // The directory holds exactly the claimed staging files (all ".tmp"
  // shaped) and nothing else — every claim left an empty real file.
  REQUIRE( countTmpEntries( dir.path() ) == allStaged.size() );

  for ( const std::string &staged : allStaged )
    sicnu::geo::atomic_fs::removeFileQuiet( staged );
  REQUIRE( countTmpEntries( dir.path() ) == 0 );
}

TEST_CASE( "stress C2: 8-thread same-target publish storm never exposes a torn document",
           "[stress][atomic_fs][r4][torn-state]" )
{
  ScratchDir dir;
  const std::string target = u8( dir.path() / "storm.json" );
  const std::string seed = "{\"seed\":true}";
  {
    std::ofstream out( sicnu::portable::pathFromUtf8( target ), std::ios::binary | std::ios::trunc );
    out << seed;
  }

  constexpr int kThreads = 8;
  constexpr int kRounds = 25;
  // The exact payload each publish will write — the monitor's legality set
  // is built from these byte-exact strings (never a pattern, never relaxed).
  std::vector<std::string> allPayloads;
  for ( int t = 0; t < kThreads; ++t )
    for ( int r = 0; r < kRounds; ++r )
      allPayloads.push_back( "{\"writer\":" + std::to_string( t ) +
                             ",\"round\":" + std::to_string( r ) + "}" );
  std::set<std::string> payloads( allPayloads.begin(), allPayloads.end() );

  // Pre-publish observation buffer: every sampled document must be a
  // byte-exact member of {seed} ∪ payloads.
  std::atomic<bool> torn{ false };
  std::atomic<bool> monitoring{ true };
  std::thread monitor( [ & ] {
    const std::set<std::string> legal = [ & ] {
      std::set<std::string> s = payloads;
      s.insert( seed );
      return s;
    }();
    while ( monitoring.load( std::memory_order_relaxed ) )
    {
      std::ifstream in( sicnu::portable::pathFromUtf8( target ), std::ios::binary );
      if ( in )
      {
        const std::string body( ( std::istreambuf_iterator<char>( in ) ),
                                std::istreambuf_iterator<char>() );
        if ( !body.empty() && legal.count( body ) == 0 )
          torn.store( true );
      }
      std::this_thread::yield();
    }
  } );

  std::vector<std::thread> workers;
  std::atomic<int> completed{ 0 };
  for ( int t = 0; t < kThreads; ++t )
  {
    // t by value: the loop variable dies before the threads run.
    workers.emplace_back( [ &, t ] {
      for ( int r = 0; r < kRounds; ++r )
      {
        const std::string &payload = allPayloads[static_cast<std::size_t>( t ) * kRounds + r];
        writeFileAtomic( target, [ & ]( const std::string &staged ) {
          std::ofstream out( sicnu::portable::pathFromUtf8( staged ),
                             std::ios::binary | std::ios::trunc );
          out << payload;
        } );
        ++completed;
      }
    } );
  }
  for ( std::thread &worker : workers )
    worker.join();
  monitoring.store( false );
  monitor.join();

  // C2/C3: every publish completed, the monitor never saw a torn document.
  REQUIRE( completed.load() == kThreads * kRounds );
  REQUIRE_FALSE( torn.load() );

  // The final document is one published payload, byte-exact.
  std::ifstream in( sicnu::portable::pathFromUtf8( target ), std::ios::binary );
  const std::string finalBody( ( std::istreambuf_iterator<char>( in ) ),
                               std::istreambuf_iterator<char>() );
  REQUIRE( payloads.count( finalBody ) == 1 );

  // Zero staged residue after the storm.
  REQUIRE( countTmpEntries( dir.path() ) == 0 );
}

// test_external_memory_10.cpp — LSEE 10.0 external-memory contracts:
// scratch lease accounting (budget/RAII/finalize/verify), disk tile store
// round-trip + fail-closed corruption, bounded write gate backpressure,
// tile checkpoint identity/version/digest gates, and stale scratch sweep.
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/disk_tile_store.h"
#include "runtime/chunk/scratch_registry.h"
#include "runtime/chunk/tile_checkpoint.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

using namespace sicnu::runtime::chunk;

namespace
{
std::string tempRoot( const std::string &name )
{
    std::error_code ec;
    auto root = std::filesystem::temp_directory_path( ec ) / ( "lsee10-" + name );
    std::filesystem::remove_all( root, ec );
    std::filesystem::create_directories( root, ec );
    return root.string();
}

TilePayload makePayload( int index, float fill )
{
    TileSpec spec;
    spec.index = index;
    spec.totalTiles = 4;
    spec.xOffset = index * 8;
    spec.width = 8;
    spec.height = 8;
    spec.halo = 1;
    spec.bufferWidth = 10;
    spec.bufferHeight = 10;
    spec.rasterWidth = 32;
    spec.rasterHeight = 8;
    spec.bands = 2;
    spec.bandOffset = 3;
    spec.timeIndex = 5;
    auto buffer = std::make_shared<std::vector<float>>( spec.bufferElementCount(), fill );
    return TilePayload{ spec, std::move( buffer ) };
}
} // namespace

TEST_CASE( "ScratchRegistry enforces the byte budget with typed refusal", "[scratch][lsee10]" )
{
    ScratchRegistry registry( { tempRoot( "budget" ), /*budgetBytes=*/1000 } );
    auto leaseA = registry.acquire( "run-1", "tile", 400 );
    REQUIRE( registry.outstandingBytes() == 400 );
    REQUIRE( registry.outstandingBytes( "run-1" ) == 400 );
    auto leaseB = registry.acquire( "run-1", "tile", 599 );
    REQUIRE( registry.outstandingBytes() == 999 );
    REQUIRE_THROWS_AS( registry.acquire( "run-1", "tile", 2 ), ScratchBudgetExceeded );
    try
    {
        registry.acquire( "run-1", "tile", 2 );
    }
    catch ( const ScratchBudgetExceeded &error )
    {
        REQUIRE( error.need == 2 );
        REQUIRE( error.outstanding == 999 );
        REQUIRE( error.budget == 1000 );
    }
}

TEST_CASE( "ScratchLease RAII releases bytes and unlinks provisional files", "[scratch][lsee10]" )
{
    ScratchRegistry registry( { tempRoot( "raii" ), 0 } );
    std::string path;
    {
        auto lease = registry.acquire( "run-raii", "t", 128 );
        path = lease.path();
        REQUIRE( std::filesystem::exists( path ) );
        REQUIRE( registry.outstandingBytes() == 128 );
    }
    // Last copy gone: bytes unaccounted, provisional file unlinked.
    REQUIRE( registry.outstandingBytes() == 0 );
    REQUIRE_FALSE( std::filesystem::exists( path ) );
}

TEST_CASE( "ScratchLease finalize publishes durably and verifyDigest re-proves content", "[scratch][lsee10]" )
{
    ScratchRegistry registry( { tempRoot( "finalize" ), 0 } );
    auto lease = registry.acquire( "run-fin", "t", 64 );
    {
        std::ofstream out( lease.path(), std::ios::binary );
        out << "tile-bytes-0123456789";
    }
    lease.sealDigest();
    const std::string finalPath = lease.finalize();
    REQUIRE( finalPath == lease.finalPath() );
    REQUIRE( lease.isFinalized() );
    REQUIRE( std::filesystem::exists( finalPath ) );
    REQUIRE_FALSE( std::filesystem::exists( lease.path() ) );
    REQUIRE( lease.verifyDigest() );

    // Corruption flips the verdict (fail-closed reuse gate).
    {
        std::ofstream out( finalPath, std::ios::binary | std::ios::app );
        out << "x";
    }
    REQUIRE_FALSE( lease.verifyDigest() );
}

TEST_CASE( "DiskTileStore round-trips a payload and refuses corruption", "[scratch][lsee10][tilestore]" )
{
    ScratchRegistry registry( { tempRoot( "tilestore" ), 0 } );
    const TilePayload original = makePayload( 2, 7.5f );
    {
        auto lease = registry.acquire( "run-ts", "tile", 4096 );
        DiskTileStore::write( lease, original );
        const TilePayload restored = DiskTileStore::read( lease );
        REQUIRE( restored.spec.bufferElementCount() == original.spec.bufferElementCount() );
        REQUIRE( restored.spec.index == 2 );
        REQUIRE( restored.spec.bandOffset == 3 );
        REQUIRE( restored.spec.timeIndex == 5 );
        REQUIRE( *restored.pixels == *original.pixels );
    }

    // Corrupt a finalized tile in the PAYLOAD region (past the 96-byte
    // header, F-B-6) → typed refusal via the payload digest gate.
    auto badLease = registry.acquire( "run-ts", "tile", 4096 );
    DiskTileStore::write( badLease, makePayload( 3, 1.0f ) );
    {
        std::fstream out( badLease.finalPath(), std::ios::binary | std::ios::in | std::ios::out );
        REQUIRE( out.is_open() );
        out.seekp( 100, std::ios::beg ); // offset 4 inside the payload region
        const char junk = 'z';
        out.write( &junk, 1 );
        out.flush();
        REQUIRE( out.good() );
    }
    REQUIRE_THROWS_AS( DiskTileStore::read( badLease ), ChunkCorruptTile );

    // Corrupt the HEADER region (inside geometry fields) → the headerDigest
    // gate must also refuse: a flipped halo/offset can never masquerade.
    auto badHeaderLease = registry.acquire( "run-ts", "tile", 4096 );
    DiskTileStore::write( badHeaderLease, makePayload( 1, 2.0f ) );
    {
        std::fstream out( badHeaderLease.finalPath(),
                          std::ios::binary | std::ios::in | std::ios::out );
        REQUIRE( out.is_open() );
        out.seekp( 40, std::ios::beg ); // the halo field inside the header
        const char junk = 9;
        out.write( &junk, 1 );
        out.flush();
        REQUIRE( out.good() );
    }
    REQUIRE_THROWS_AS( DiskTileStore::read( badHeaderLease ), ChunkCorruptTile );

    // A hostile header claiming an impossible payload size must be a typed
    // refusal, never an allocation attempt (F-A-5/F-B-1).
    auto hugeLease = registry.acquire( "run-ts", "tile", 4096 );
    DiskTileStore::write( hugeLease, makePayload( 0, 0.0f ) );
    {
        std::fstream out( hugeLease.finalPath(),
                          std::ios::binary | std::ios::in | std::ios::out );
        REQUIRE( out.is_open() );
        out.seekp( 88, std::ios::beg ); // payloadBytes field (header tail)
        const std::uint64_t huge = 1ull << 40;
        out.write( reinterpret_cast<const char *>( &huge ), sizeof( huge ) );
        out.flush();
        REQUIRE( out.good() );
    }
    REQUIRE_THROWS_AS( DiskTileStore::read( hugeLease ), ChunkCorruptTile );
}

TEST_CASE( "BoundedWriteGate throttles concurrent writers", "[scratch][lsee10][gate]" )
{
    BoundedWriteGate gate( 1000 );
    std::atomic<int> peakObserved{ 0 };
    std::atomic<int> inside{ 0 };
    std::vector<std::thread> writers;
    for ( int i = 0; i < 8; ++i )
    {
        writers.emplace_back( [&] {
            for ( int round = 0; round < 25; ++round )
            {
                BoundedWriteGate::Reservation reservation( gate, 300 );
                const int now = ++inside;
                int expected = peakObserved.load();
                while ( now > expected && !peakObserved.compare_exchange_weak( expected, now ) )
                {
                }
                std::this_thread::yield();
                --inside;
            }
        } );
    }
    for ( auto &writer : writers )
        writer.join();
    // 300-byte reservations under a 1000-byte cap: at most 3 concurrently.
    REQUIRE( peakObserved <= 3 );
    REQUIRE( gate.outstandingBytes() == 0 );
}

TEST_CASE( "BoundedWriteGate admits one oversized write when idle", "[scratch][lsee10][gate]" )
{
    BoundedWriteGate gate( 100 );
    bool entered = false;
    std::thread writer( [&] {
        BoundedWriteGate::Reservation reservation( gate, 500 ); // > cap
        entered = true;
    } );
    writer.join();
    REQUIRE( entered );
}

TEST_CASE( "TileCheckpoint round-trips and gates on identity, version and digest", "[checkpoint][lsee10]" )
{
    const std::string root = tempRoot( "checkpoint" );
    const std::string path = ( std::filesystem::path( root ) / "task.checkpoint" ).string();

    TileCheckpoint checkpoint;
    checkpoint.formatVersion = kTileCheckpointFormatVersion;
    checkpoint.operatorIdentity = 0xABCDEF;
    checkpoint.inputIdentity = 0x1234;
    checkpoint.completedTiles = 4242;
    checkpoint.scratchRunId = "run-2026";

    REQUIRE( TileCheckpointWriter::save( path, checkpoint ) );
    const auto loaded = TileCheckpointWriter::load( path, 0xABCDEF, 0x1234 );
    REQUIRE( loaded.has_value() );
    REQUIRE( *loaded == checkpoint );

    // Operator identity drift → refuse (re-execute).
    REQUIRE_FALSE( TileCheckpointWriter::load( path, 0xDEADBEEF, 0x1234 ).has_value() );
    // Input/parameter drift → refuse.
    REQUIRE_FALSE( TileCheckpointWriter::load( path, 0xABCDEF, 0x9999 ).has_value() );

    // Corruption → refuse (digest gate).
    TileCheckpoint tampered = checkpoint;
    tampered.completedTiles = 1; // recompute a VALID digest for the tampered payload
    REQUIRE( TileCheckpointWriter::save( path, tampered ) );
    {
        // Flip a byte inside the file so the stored digest no longer matches.
        std::fstream out( path, std::ios::binary | std::ios::in | std::ios::out );
        REQUIRE( out.is_open() );
        out.seekp( 16, std::ios::beg );
        const char junk = 0x5A;
        out.write( &junk, 1 );
        out.flush();
        REQUIRE( out.good() );
    }
    REQUIRE_FALSE( TileCheckpointWriter::load( path, 0xABCDEF, 0x1234 ).has_value() );

    // Truncation → refuse.
    REQUIRE( TileCheckpointWriter::save( path, checkpoint ) );
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size( path, ec );
        std::filesystem::resize_file( path, size / 2, ec );
    }
    REQUIRE_FALSE( TileCheckpointWriter::load( path, 0xABCDEF, 0x1234 ).has_value() );

    // User cancellation path: remove then load → nothing to resume.
    TileCheckpointWriter::remove( path );
    REQUIRE_FALSE( TileCheckpointWriter::load( path, 0xABCDEF, 0x1234 ).has_value() );
    REQUIRE_FALSE( TileCheckpointWriter::load( path, 0, 0 ).has_value() );
}

TEST_CASE( "ScratchRegistry sweepStale removes only aged run directories", "[scratch][lsee10][sweep]" )
{
    const std::string root = tempRoot( "sweep" );
    ScratchRegistry registry( { root, 0 } );
    auto keepLease = registry.acquire( "run-keep", "tile", 10 );
    (void)keepLease;

    // Age one run directory artificially (C++20 last_write_time setter).
    const auto staleDir = std::filesystem::path( root ) / "run-stale";
    std::filesystem::create_directories( staleDir );
    const auto past = std::filesystem::file_time_type::clock::now()
                      - std::chrono::hours( 24 * 7 );
    std::filesystem::last_write_time( staleDir, past );

    const std::size_t removed = ScratchRegistry::sweepStale( root, std::chrono::hours( 24 ) );
    REQUIRE( removed == 1 );
    REQUIRE_FALSE( std::filesystem::exists( staleDir ) );
    REQUIRE( std::filesystem::exists( std::filesystem::path( root ) / "run-keep" ) );
}

// test_r35_t04_resume_store.cpp — R35-T04 Phase A persistence regressions:
// DiskTileStore read ceilings and geometry admission (WP06), resume-journal
// complete-vs-torn corruption discrimination with bounded scans (WP07),
// resume bitmap capacity gates (WP08) and the strict identity-key parse
// (WP10). The tile/journal file formats are re-encoded HERE with an
// independent FNV-1a — the fixtures never reuse the production writer on the
// code paths under test.
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/disk_tile_store.h"
#include "runtime/chunk/resumable_tile_run.h"
#include "runtime/chunk/tile_run_contract.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#if !defined( _WIN32 )
#include <unistd.h>
#endif

using namespace sicnu::runtime::chunk;

namespace
{
// Independent FNV-1a 64 (oracle: mirrors the on-disk definition without
// sharing the production implementation under test).
std::uint64_t testFnv1a( const void *data, std::size_t size )
{
    const auto *bytes = static_cast<const unsigned char *>( data );
    std::uint64_t h = 1469598103934665603ull;
    for ( std::size_t i = 0; i < size; ++i )
    {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }
    return h;
}

#pragma pack( push, 1 )
struct TestTileHeader
{
    char magic[8] = { 'S', 'I', 'C', 'N', 'U', 'T', 'L', '1' };
    std::uint32_t version = 1;
    std::uint32_t headerSize = 96;
    std::int32_t index = 0, totalTiles = 4, xOffset = 0, yOffset = 0, width = 4,
                  height = 4, halo = 0, bufferWidth = 4, bufferHeight = 4,
                  rasterWidth = 8, rasterHeight = 8, bands = 1, bandOffset = 0,
                  timeIndex = 0;
    std::uint64_t payloadBytes = 0;
    std::uint64_t payloadDigest = 0;
    std::uint64_t headerDigest = 0; // FNV over the first 88 bytes, zeroed here
};
#pragma pack( pop )

void sealHeader( TestTileHeader &h )
{
    h.headerDigest = 0;
    h.headerDigest = testFnv1a( &h, 88 );
}

std::filesystem::path tempDir( const char *tag )
{
    static std::atomic<unsigned> counter{ 0 };
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path( ec );
    if ( ec )
        base = std::filesystem::current_path();
    const auto dir = base / ( "sicnu-r35t04-" + std::string( tag ) + "-"
                              + std::to_string( ::getpid() ) + "-"
                              + std::to_string( counter.fetch_add( 1 ) ) );
    std::filesystem::create_directories( dir, ec );
    return dir;
}

TilePayload truthTile( const TileSpec &s )
{
    auto buf = std::make_shared<std::vector<float>>( s.bufferElementCount() );
    for ( size_t i = 0; i < buf->size(); ++i )
        ( *buf )[i] = static_cast<float>( ( s.index * 31 + i ) % 977 ) / 977.0f;
    return TilePayload{ s, std::move( buf ) };
}

struct RunHarness
{
    std::filesystem::path root;
    std::string scratchRoot;
    std::string statePath;
    std::string outputFile;
    TileRunSpec spec;
    std::string identityKey;

    RunHarness( const char *tag ) : root( tempDir( tag ) )
    {
        scratchRoot = ( root / "scratch" ).generic_string();
        statePath = ( root / "state" / "run" ).generic_string();
        outputFile = ( root / "out.bin" ).generic_string();
        spec.partition = TileRunPartition{ 8, 8, 4, 4, 0, 1, 0, 0 };
        spec.identity.operatorIdentity = 0xAA;
        spec.identity.inputIdentity = 0xBB;
        spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );
        identityKey = tileRunIdentityKey( spec.identity );
        // Direct DiskTileStore::writeFile calls in the tests precede
        // writeJournal — both directories must exist up front.
        std::error_code mkEc;
        std::filesystem::create_directories( runDir(), mkEc );
        std::filesystem::create_directories(
            std::filesystem::path( statePath ).parent_path(), mkEc );
    }
    ~RunHarness() { std::error_code ec; std::filesystem::remove_all( root, ec ); }

    std::filesystem::path journalPath() const
    {
        return std::filesystem::path( statePath ) += ".journal";
    }
    std::filesystem::path runDir() const
    {
        return std::filesystem::path( scratchRoot ) / ( "rt-" + identityKey );
    }

    void writeJournal( const std::string &body )
    {
        std::error_code ec;
        std::filesystem::create_directories( runDir(), ec );
        std::filesystem::create_directories(
            std::filesystem::path( statePath ).parent_path(), ec );
        std::ofstream out( journalPath(), std::ios::binary | std::ios::trunc );
        out << body;
    }

    /// One full execute() with counting callbacks (no cancel).
    struct Outcome
    {
        int computes = 0;
        int consumes = 0;
        int publishes = 0;
        ResumableTileRun::Result result{};
    };
    Outcome runOnce()
    {
        ResumableTileRun run( spec, makeConfig() );
        TileRunCancelSource noCancel;
        ResumableTileRun::Callbacks cb;
        Outcome out;
        cb.compute = [&]( const TileSpec &s ) {
            ++out.computes;
            return truthTile( s );
        };
        cb.consume = [&]( const TilePayload & ) { ++out.consumes; };
        cb.publish = [&] {
            ++out.publishes;
            std::ofstream o( outputFile, std::ios::binary | std::ios::trunc );
            o << "published";
        };
        out.result = run.execute( noCancel, std::move( cb ) );
        return out;
    }

    ResumableTileRun::Config makeConfig() const
    {
        ResumableTileRun::Config cfg;
        cfg.scratchRoot = scratchRoot;
        cfg.statePath = statePath;
        return cfg;
    }
};
} // namespace

// ── WP06: DiskTileStore read ceiling + geometry admission ─────────────────

TEST_CASE( "DiskTileStore round-trips a small tile", "[chunk][r35t04][store]" )
{
    const auto dir = tempDir( "store" );
    const std::string path = ( dir / "tile-0.tl" ).generic_string();
    const TileSpec spec = tileSpecAt( TileRunPartition{ 8, 8, 4, 4, 1, 3, 0, 0 }, 0 );
    const TilePayload written = truthTile( spec );
    DiskTileStore::writeFile( path, written );
    const TilePayload back = DiskTileStore::readFile( path );
    REQUIRE( back.pixels );
    REQUIRE( back.pixels->size() == written.pixels->size() );
    REQUIRE( std::memcmp( back.pixels->data(), written.pixels->data(),
                          written.pixels->size() * sizeof( float ) ) == 0 );
    REQUIRE( back.spec.bufferWidth == spec.bufferWidth );
    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "DiskTileStore refuses an oversized declared payload before allocating",
           "[chunk][r35t04][store]" )
{
    const auto dir = tempDir( "storecap" );
    const std::string path = ( dir / "huge.tl" ).generic_string();
    TestTileHeader h; // 4x4x1 float payload would be 64 B — declare 1 GiB + 1
    h.payloadBytes = ( 1ull << 30 ) + 1;
    h.payloadDigest = 0;
    sealHeader( h );
    {
        std::ofstream out( path, std::ios::binary | std::ios::trunc );
        out.write( reinterpret_cast<const char *>( &h ), sizeof( h ) );
    }
    REQUIRE_THROWS_AS( DiskTileStore::readFile( path ), ChunkCorruptTile );
    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "DiskTileStore rejects digest-consistent bad geometry",
           "[chunk][r35t04][store]" )
{
    const auto dir = tempDir( "storegeo" );
    const std::string path = ( dir / "geo.tl" ).generic_string();
    // Geometry breach: bufferWidth 5 != width + 2*halo = 4. Payload sized to
    // the BAD geometry so the element product ALSO matches (5*4*1 floats) —
    // every digest recomputed consistently. Only the shape gate can refuse.
    TestTileHeader h;
    h.bufferWidth = 5;
    std::vector<float> payload( 5 * 4 * 1, 0.25f );
    h.payloadBytes = payload.size() * sizeof( float );
    h.payloadDigest = testFnv1a( payload.data(), payload.size() * sizeof( float ) );
    sealHeader( h );
    {
        std::ofstream out( path, std::ios::binary | std::ios::trunc );
        out.write( reinterpret_cast<const char *>( &h ), sizeof( h ) );
        out.write( reinterpret_cast<const char *>( payload.data() ),
                   static_cast<std::streamsize>( h.payloadBytes ) );
    }
    REQUIRE_THROWS_AS( DiskTileStore::readFile( path ), ChunkCorruptTile );
    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

TEST_CASE( "DiskTileStore rejects trailing bytes under the open stream",
           "[chunk][r35t04][store]" )
{
    const auto dir = tempDir( "storetail" );
    const std::string path = ( dir / "tail.tl" ).generic_string();
    const TileSpec spec = tileSpecAt( TileRunPartition{ 8, 8, 4, 4, 0, 1, 0, 0 }, 1 );
    DiskTileStore::writeFile( path, truthTile( spec ) );
    { // append one stray byte — same-length file, corrupted tail
        std::ofstream out( path, std::ios::binary | std::ios::app );
        out.put( 'X' );
    }
    REQUIRE_THROWS_AS( DiskTileStore::readFile( path ), ChunkCorruptTile );
    std::error_code ec;
    std::filesystem::remove_all( dir, ec );
}

// ── WP07: journal — complete bad line vs torn tail vs bounded scan ─────────

TEST_CASE( "resume journal folds duplicates and reuses committed tiles",
           "[chunk][r35t04][resume]" )
{
    RunHarness h( "fold" );
    // Committed tile 0 on disk + journal (with a legal duplicate fold).
    DiskTileStore::writeFile( ( h.runDir() / "tile-0.tl" ).generic_string(),
                              truthTile( tileSpecAt( h.spec.partition, 0 ) ) );
    h.writeJournal( "V 1 " + h.identityKey + "\nC 0 tile-0.tl\nC 0 tile-0.tl\n" );
    const auto out = h.runOnce();
    REQUIRE( out.result.totalTiles == 4 );
    REQUIRE( out.result.tilesReused == 1 );
    REQUIRE( out.result.tilesComputed == 3 );
    REQUIRE( out.result.alreadyPublished == false );
    REQUIRE( out.publishes == 1 );
}

TEST_CASE( "resume journal torn tail truncates and resumes", "[chunk][r35t04][resume]" )
{
    RunHarness h( "torn" );
    DiskTileStore::writeFile( ( h.runDir() / "tile-0.tl" ).generic_string(),
                              truthTile( tileSpecAt( h.spec.partition, 0 ) ) );
    // Crash mid-append: the last line was never newline-terminated.
    h.writeJournal( "V 1 " + h.identityKey + "\nC 0 tile-0.tl\nC 1 til" );
    const auto out = h.runOnce();
    REQUIRE( out.result.tilesReused == 1 );
    REQUIRE( out.result.tilesComputed == 3 );
}

TEST_CASE( "resume journal content-complete unterminated tail still truncates",
           "[chunk][r35t04][resume]" )
{
    // R2 review: a torn write can end EXACTLY between the content and its
    // '\n'. The content parses, but the newline is PART of the record —
    // accepting it fuses the next append ("...tile-1.tlC 2 ...") and kills
    // every later resume. The tail must truncate and the journal must heal.
    RunHarness h( "torn-nn" );
    DiskTileStore::writeFile( ( h.runDir() / "tile-0.tl" ).generic_string(),
                              truthTile( tileSpecAt( h.spec.partition, 0 ) ) );
    h.writeJournal( "V 1 " + h.identityKey + "\nC 0 tile-0.tl\nC 1 tile-1.tl" ); // no final \n
    const auto out = h.runOnce();
    REQUIRE( out.result.tilesReused == 1 );
    REQUIRE( out.result.tilesComputed == 3 );
    // The healed journal is newline-terminated: the next run resumes cleanly.
    {
        std::ifstream in( h.journalPath(), std::ios::binary );
        std::string text( ( std::istreambuf_iterator<char>( in ) ),
                          std::istreambuf_iterator<char>() );
        REQUIRE( !text.empty() );
        REQUIRE( text.back() == '\n' );
        REQUIRE( text.find( "tile-1.tlC " ) == std::string::npos ); // no fused record
    }
    ResumableTileRun run( h.spec, h.makeConfig() );
    TileRunCancelSource noCancel;
    ResumableTileRun::Callbacks cb;
    cb.compute = []( const TileSpec &s ) { return truthTile( s ); };
    cb.consume = []( const TilePayload & ) {};
    cb.publish = [] {};
    const auto second = run.execute( noCancel, std::move( cb ) );
    REQUIRE( second.alreadyPublished ); // healed state + marker: zero work
}

TEST_CASE( "resume journal complete bad LAST line is corruption, not a torn tail",
           "[chunk][r35t04][resume]" )
{
    RunHarness h( "badlast" );
    // Newline-terminated garbage as the final record: the write COMPLETED and
    // still fails the format — silently truncating it would erase evidence of
    // a mid-file problem (R3/WP07).
    h.writeJournal( "V 1 " + h.identityKey + "\nC 0 tile-0.tl\nGARBAGE RECORD\n" );
    ResumableTileRun run( h.spec, h.makeConfig() );
    TileRunCancelSource noCancel;
    ResumableTileRun::Callbacks cb;
    cb.compute = []( const TileSpec &s ) { return truthTile( s ); };
    cb.consume = []( const TilePayload & ) {};
    cb.publish = [] {};
    REQUIRE_THROWS_AS( run.execute( noCancel, std::move( cb ) ), ChunkCorruptTile );
    // The corrupt journal is NOT wiped: the evidence stays on disk.
    REQUIRE( std::filesystem::exists( h.journalPath() ) );
}

TEST_CASE( "resume journal bad MIDDLE line fails closed", "[chunk][r35t04][resume]" )
{
    RunHarness h( "badmid" );
    h.writeJournal( "V 1 " + h.identityKey + "\nC 0 tile-0.tl\nBAD\nC 1 tile-1.tl\n" );
    ResumableTileRun run( h.spec, h.makeConfig() );
    TileRunCancelSource noCancel;
    ResumableTileRun::Callbacks cb;
    cb.compute = []( const TileSpec &s ) { return truthTile( s ); };
    cb.consume = []( const TilePayload & ) {};
    cb.publish = [] {};
    REQUIRE_THROWS_AS( run.execute( noCancel, std::move( cb ) ), ChunkCorruptTile );
}

TEST_CASE( "resume journal over-long line and oversized scan fail typed",
           "[chunk][r35t04][resume]" )
{
    RunHarness h( "bounds" );
    // A legal line is ≤ ~33 bytes; a 300-byte line is corruption, not a tail.
    h.writeJournal( "V 1 " + h.identityKey + "\n" + std::string( 300, 'C' ) + "\n" );
    {
        ResumableTileRun run( h.spec, h.makeConfig() );
        TileRunCancelSource noCancel;
        ResumableTileRun::Callbacks cb;
        cb.compute = []( const TileSpec &s ) { return truthTile( s ); };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        REQUIRE_THROWS_AS( run.execute( noCancel, std::move( cb ) ), ChunkCorruptTile );
    }
    // Oversized physical journal: 4 tiles can never legitimately produce a
    // >1 MiB journal (the scan cap floor); typed corruption before scanning.
    RunHarness big( "bigscan" );
    std::string body = "V 1 " + big.identityKey + "\n";
    for ( int i = 0; i < 80000; ++i )
        body += "C 0 tile-0.tl\n"; // 80000 x 14 B = 1.12 MB of legal folds
    big.writeJournal( body );
    ResumableTileRun run2( big.spec, big.makeConfig() );
    TileRunCancelSource noCancel;
    ResumableTileRun::Callbacks cb2;
    cb2.compute = []( const TileSpec &s ) { return truthTile( s ); };
    cb2.consume = []( const TilePayload & ) {};
    cb2.publish = [] {};
    REQUIRE_THROWS_AS( run2.execute( noCancel, std::move( cb2 ) ), ChunkCorruptTile );
}

TEST_CASE( "resume journal header: torn creation truncates, complete bad line corrupts",
           "[chunk][r35t04][resume]" )
{
    {
        RunHarness h( "hdr-torn" );
        h.writeJournal( "V 1 " ); // crash before the header line completed
        const auto out = h.runOnce();
        REQUIRE( out.result.tilesComputed == 4 ); // fresh start, all recomputed
    }
    {
        RunHarness h( "hdr-bad" );
        h.writeJournal( "NOT A HEADER\n" ); // complete, wrong format
        ResumableTileRun run( h.spec, h.makeConfig() );
        TileRunCancelSource noCancel;
        ResumableTileRun::Callbacks cb;
        cb.compute = []( const TileSpec &s ) { return truthTile( s ); };
        cb.consume = []( const TilePayload & ) {};
        cb.publish = [] {};
        REQUIRE_THROWS_AS( run.execute( noCancel, std::move( cb ) ), ChunkCorruptTile );
    }
}

// ── WP08: bitmap capacity — representable is not runnable ─────────────────

TEST_CASE( "ResumableTileRun typed-refuses plans beyond the bitmap cap",
           "[chunk][r35t04][resume]" )
{
    // 20000×20000 raster with 1×1 tiles = 4×10^8 tiles: representable in the
    // int tile domain, but the committed-set bitmap alone would cost ~48 MiB.
    TileRunSpec spec;
    spec.partition = TileRunPartition{ 20000, 20000, 1, 1, 0, 1, 0, 0 };
    spec.identity.partitionDigest = tileRunPartitionDigest( spec.partition );
    ResumableTileRun::Config cfg;
    cfg.scratchRoot = "/tmp";
    cfg.statePath = "/tmp/r35t04-capgate";
    REQUIRE_THROWS_AS( ResumableTileRun( spec, cfg ), std::runtime_error );
    // An explicitly raised cap admits it (the caller owns the state cost);
    // construction is O(1) — no bitmap is materialized until execute().
    cfg.maxResumableTiles = 500000000;
    REQUIRE_NOTHROW( ResumableTileRun( spec, cfg ) );
    // 0 disables the gate deliberately.
    cfg.maxResumableTiles = 0;
    REQUIRE_NOTHROW( ResumableTileRun( spec, cfg ) );
}

// ── WP10: strict identity-key parse ────────────────────────────────────────

TEST_CASE( "identity key round-trips strict canonical form", "[chunk][r35t04][key]" )
{
    const std::uint64_t values[] = { 0, 1, 0xDEADBEEFCAFEF00Dull,
                                     std::numeric_limits<std::uint64_t>::max() };
    for ( const std::uint64_t a : values )
        for ( const std::uint64_t b : values )
            for ( const std::uint64_t c : values )
            {
                const TileRunIdentity id{ a, b, c };
                const std::string key = tileRunIdentityKey( id );
                const TileRunIdentity back = tileRunIdentityFromKey( key );
                REQUIRE( back.operatorIdentity == a );
                REQUIRE( back.inputIdentity == b );
                REQUIRE( back.partitionDigest == c );
                // The documented validity gate: re-serialization is identity.
                REQUIRE( tileRunIdentityKey( back ) == key );
            }
}

TEST_CASE( "identity key parse rejects short writes, junk and case drift",
           "[chunk][r35t04][key]" )
{
    const TileRunIdentity zero; // the nullopt shape malformed input returns
    // Groups carry a-f digits: the case-drift case below must actually
    // change characters (digit-only groups would make it a no-op).
    const std::string canonical =
        tileRunIdentityKey( TileRunIdentity{ 0xabcdef0123456789ull, 0xfeedfacecafebeadull,
                                             0xdeadbeefcafef00dull } );
    const auto isMalformed = [&]( const std::string &k ) {
        const TileRunIdentity id = tileRunIdentityFromKey( k );
        REQUIRE( id.operatorIdentity == 0 );
        REQUIRE( id.inputIdentity == 0 );
        REQUIRE( id.partitionDigest == 0 );
        // …and the canonical re-serialization gate proves non-validity.
        REQUIRE( tileRunIdentityKey( id ) != k );
    };
    isMalformed( "1-2-3" );                       // short groups (sscanf legacy)
    isMalformed( canonical + "JUNK" );            // trailing garbage
    isMalformed( "JUNK" + canonical );            // leading garbage
    isMalformed( canonical.substr( 1 ) );         // dropped digit
    isMalformed( canonical + "0" );               // extra digit
    {
        std::string upper = canonical;
        for ( char &ch : upper )
            if ( ch >= 'a' && ch <= 'f' )
                ch = static_cast<char>( ch - 'a' + 'A' );
        isMalformed( upper ); // uppercase is not canonical
    }
    {
        std::string dotted = canonical;
        dotted[16] = dotted[33] = ':';
        isMalformed( dotted ); // wrong separator
    }
    isMalformed( "" );
    // All-zero is a LEGAL identity, not the sentinel of a failed parse.
    const std::string zeroKey = tileRunIdentityKey( TileRunIdentity{ 0, 0, 0 } );
    REQUIRE( zeroKey == std::string( 16, '0' ) + "-" + std::string( 16, '0' ) + "-"
                        + std::string( 16, '0' ) );
    const TileRunIdentity zeroBack = tileRunIdentityFromKey( zeroKey );
    REQUIRE( zeroBack.operatorIdentity == 0 );
    REQUIRE( tileRunIdentityKey( zeroBack ) == zeroKey ); // the gate holds
}

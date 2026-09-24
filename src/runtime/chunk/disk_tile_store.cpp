// disk_tile_store.cpp — see disk_tile_store.h.
#include "disk_tile_store.h"
#include "fsync_compat.h"

#include "platform/portable.h" // UTF-8 <-> fs::path boundary (ACP-safe on Windows)

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if !defined( _WIN32 )
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sicnu::runtime::chunk
{

namespace
{
constexpr std::size_t kMagicSize = 8;
constexpr std::uint32_t kFormatVersion = 1;
const char kMagic[kMagicSize] = { 'S', 'I', 'C', 'N', 'U', 'T', 'L', '1' };

#pragma pack( push, 1 )
struct TileFileHeader
{
    char magic[kMagicSize];
    std::uint32_t version;
    std::uint32_t headerSize;
    // TileSpec geometry (int fields, declared order).
    std::int32_t index;
    std::int32_t totalTiles;
    std::int32_t xOffset;
    std::int32_t yOffset;
    std::int32_t width;
    std::int32_t height;
    std::int32_t halo;
    std::int32_t bufferWidth;
    std::int32_t bufferHeight;
    std::int32_t rasterWidth;
    std::int32_t rasterHeight;
    std::int32_t bands;
    std::int32_t bandOffset;
    std::int32_t timeIndex;
    std::uint64_t payloadBytes;
    std::uint64_t payloadDigest;
    // FNV over EVERY preceding byte of this header (field zeroed while
    // hashing): a bitflip in the geometry fields cannot masquerade as a
    // valid tile — the payload digest alone does not cover them.
    std::uint64_t headerDigest;
};
#pragma pack( pop )

std::uint64_t fnv1a( const void *data, std::size_t size )
{
    const auto *bytes = static_cast<const std::uint8_t *>( data );
    std::uint64_t hash = 1469598103934665603ull;
    for ( std::size_t i = 0; i < size; ++i )
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

TileFileHeader makeHeader( const TilePayload &payload, std::uint64_t payloadBytes,
                           std::uint64_t digest )
{
    TileFileHeader header{};
    std::memcpy( header.magic, kMagic, kMagicSize );
    header.version = kFormatVersion;
    header.headerSize = static_cast<std::uint32_t>( sizeof( TileFileHeader ) );
    header.index = payload.spec.index;
    header.totalTiles = payload.spec.totalTiles;
    header.xOffset = payload.spec.xOffset;
    header.yOffset = payload.spec.yOffset;
    header.width = payload.spec.width;
    header.height = payload.spec.height;
    header.halo = payload.spec.halo;
    header.bufferWidth = payload.spec.bufferWidth;
    header.bufferHeight = payload.spec.bufferHeight;
    header.rasterWidth = payload.spec.rasterWidth;
    header.rasterHeight = payload.spec.rasterHeight;
    header.bands = payload.spec.bands;
    header.bandOffset = payload.spec.bandOffset;
    header.timeIndex = payload.spec.timeIndex;
    header.payloadBytes = payloadBytes;
    header.payloadDigest = digest;
    header.headerDigest = 0;
    header.headerDigest = fnv1a( &header, offsetof( TileFileHeader, headerDigest ) );
    return header;
}

/// Validates the framing and the header digest BEFORE any payload-size
/// allocation (F-A-5/F-B-1): a corrupted/hostile header must surface as a
/// typed ChunkCorruptTile, never as bad_alloc.
void validateHeader( TileFileHeader header, const std::string &path )
{
    if ( std::memcmp( header.magic, kMagic, kMagicSize ) != 0
         || header.version != kFormatVersion
         || header.headerSize != sizeof( TileFileHeader ) )
        throw ChunkCorruptTile( path );
    const std::uint64_t stored = header.headerDigest;
    header.headerDigest = 0;
    if ( fnv1a( &header, offsetof( TileFileHeader, headerDigest ) ) != stored )
        throw ChunkCorruptTile( path );
}

TilePayload decode( TileFileHeader header, std::vector<char> payloadBytes,
                    const std::string &path )
{
    validateHeader( header, path );
    if ( fnv1a( payloadBytes.data(), payloadBytes.size() ) != header.payloadDigest
         || payloadBytes.size() != header.payloadBytes )
        throw ChunkCorruptTile( path );

    TileSpec spec;
    spec.index = header.index;
    spec.totalTiles = header.totalTiles;
    spec.xOffset = header.xOffset;
    spec.yOffset = header.yOffset;
    spec.width = header.width;
    spec.height = header.height;
    spec.halo = header.halo;
    spec.bufferWidth = header.bufferWidth;
    spec.bufferHeight = header.bufferHeight;
    spec.rasterWidth = header.rasterWidth;
    spec.rasterHeight = header.rasterHeight;
    spec.bands = header.bands;
    spec.bandOffset = header.bandOffset;
    spec.timeIndex = header.timeIndex;
    if ( spec.bufferElementCount() * sizeof( float ) != payloadBytes.size() )
        throw ChunkCorruptTile( path );

    auto buffer = std::make_shared<std::vector<float>>( payloadBytes.size() / sizeof( float ) );
    if ( !payloadBytes.empty() )
        std::memcpy( buffer->data(), payloadBytes.data(), payloadBytes.size() );
    return TilePayload{ spec, std::move( buffer ) };
}

/// Durability helper for writeFile: real fsync / FlushFileBuffers (#1228).
/// Best-effort wrapper keeps publish/rename progressing on exotic volumes.
void fsyncBestEffort( const std::string &path, bool directory = false )
{
    fsyncPathBestEffort( path, directory );
}

/// Shared read path for read()/readFile(): full fail-closed validation.
TilePayload readFileImpl( const std::string &path )
{
    TileFileHeader header{};
    std::ifstream in( sicnu::portable::pathFromUtf8( path ), std::ios::binary );
    if ( !in )
        throw ChunkCorruptTile( path );
    in.read( reinterpret_cast<char *>( &header ), sizeof( header ) );
    if ( static_cast<std::size_t>( in.gcount() ) != sizeof( header ) )
        throw ChunkCorruptTile( path );
    validateHeader( header, path );
    // The stored payload size must match the actual trailing bytes, so the
    // allocation below is bounded by the real file, never by a hostile
    // header field.
    {
        std::error_code sizeEc;
        const auto fileSize =
            std::filesystem::file_size( sicnu::portable::pathFromUtf8( path ), sizeEc );
        if ( sizeEc || fileSize != sizeof( TileFileHeader ) + header.payloadBytes )
            throw ChunkCorruptTile( path );
    }
    std::vector<char> payloadBytes( static_cast<std::size_t>( header.payloadBytes ) );
    if ( !payloadBytes.empty() )
    {
        in.read( payloadBytes.data(), static_cast<std::streamsize>( payloadBytes.size() ) );
        if ( static_cast<std::size_t>( in.gcount() ) != payloadBytes.size() )
            throw ChunkCorruptTile( path );
    }
    return decode( header, std::move( payloadBytes ), path );
}
} // namespace

void DiskTileStore::write( const ScratchLease &lease, const TilePayload &payload )
{
    if ( !lease.isValid() || !payload.pixels )
        throw std::runtime_error( "disk tile write: invalid lease or payload" );
    const auto *data = payload.pixels->data();
    const std::size_t bytes = payload.pixels->size() * sizeof( float );
    const TileFileHeader header = makeHeader( payload, bytes, fnv1a( data, bytes ) );

    {
        std::ofstream out( sicnu::portable::pathFromUtf8( lease.path() ),
                           std::ios::binary | std::ios::trunc );
        if ( !out )
            throw std::runtime_error( "disk tile write: cannot open " + lease.path() );
        out.write( reinterpret_cast<const char *>( &header ), sizeof( header ) );
        if ( bytes )
            out.write( reinterpret_cast<const char *>( data ),
                       static_cast<std::streamsize>( bytes ) );
        out.flush();
        if ( !out )
            throw std::runtime_error( "disk tile write: short write on " + lease.path() );
        // Scope exit closes the stream BEFORE finalize: Windows cannot rename
        // a file that is still open (verified: std::filesystem::rename on an
        // open ofstream fails with a sharing violation on MSVC).
    }
    lease.sealDigest();
    lease.finalize();
}

TilePayload DiskTileStore::read( const ScratchLease &lease )
{
    if ( !lease.isValid() )
        throw ChunkCorruptTile( "<null lease>" );
    return readFileImpl( lease.finalPath() );
}

TilePayload DiskTileStore::readProvisional( const ScratchLease &lease )
{
    if ( !lease.isValid() )
        throw ChunkCorruptTile( "<null lease>" );
    return readFileImpl( lease.path() );
}

void DiskTileStore::writeFile( const std::string &finalPath, const TilePayload &payload )
{
    if ( finalPath.empty() || !payload.pixels )
        throw std::runtime_error( "disk tile write: empty path or null payload" );
    const auto *data = payload.pixels->data();
    const std::size_t bytes = payload.pixels->size() * sizeof( float );
    const TileFileHeader header = makeHeader( payload, bytes, fnv1a( data, bytes ) );

    const std::string tmpPath = finalPath + ".part";
    {
        std::ofstream out( sicnu::portable::pathFromUtf8( tmpPath ),
                           std::ios::binary | std::ios::trunc );
        if ( !out )
            throw std::runtime_error( "disk tile write: cannot open " + tmpPath );
        out.write( reinterpret_cast<const char *>( &header ), sizeof( header ) );
        if ( bytes )
            out.write( reinterpret_cast<const char *>( data ),
                       static_cast<std::streamsize>( bytes ) );
        out.flush();
        if ( !out )
        {
            out.close();
            std::error_code removeEc;
            std::filesystem::remove( sicnu::portable::pathFromUtf8( tmpPath ), removeEc );
            throw std::runtime_error( "disk tile write: short write on " + tmpPath );
        }
    }
    fsyncBestEffort( tmpPath );
    std::error_code renameEc;
    // Rename is the atomicity boundary (Windows fsync is best-effort; the
    // same contract as ScratchLease::finalize / TileCheckpointWriter::save).
    std::filesystem::rename( sicnu::portable::pathFromUtf8( tmpPath ),
                             sicnu::portable::pathFromUtf8( finalPath ), renameEc );
    if ( renameEc )
    {
        std::error_code removeEc;
        std::filesystem::remove( sicnu::portable::pathFromUtf8( tmpPath ), removeEc );
        throw std::runtime_error( "disk tile write: rename failed on " + finalPath + " ("
                                  + renameEc.message() + ")" );
    }
    // Durability of the rename itself: flush the PARENT DIRECTORY entry (not
    // the file — that was fsynced above) so a committed tile cannot vanish
    // under the journal on power loss (POSIX; no-op on Windows).
    std::error_code parentEc;
    const auto parent = sicnu::portable::pathFromUtf8( finalPath ).parent_path();
    fsyncBestEffort( parent.empty() ? std::string( "." ) : sicnu::portable::pathToUtf8( parent ),
                     /*directory=*/true );
    (void)parentEc;
}

TilePayload DiskTileStore::readFile( const std::string &finalPath )
{
    return readFileImpl( finalPath );
}

} // namespace sicnu::runtime::chunk

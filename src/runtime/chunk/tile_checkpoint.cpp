// tile_checkpoint.cpp — see tile_checkpoint.h.
#include "tile_checkpoint.h"

#include <cstddef>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#if !defined( _WIN32 )
#include <fcntl.h>
#include <unistd.h>
#else
#include <process.h>
#endif

#include <filesystem>

namespace sicnu::runtime::chunk
{

namespace
{
int currentPid()
{
#if !defined( _WIN32 )
    return ::getpid();
#else
    return _getpid();
#endif
}

#pragma pack( push, 1 )
struct TileCheckpointFile
{
    char magic[8];
    std::uint32_t formatVersion;
    std::uint32_t headerSize;
    std::uint64_t operatorIdentity;
    std::uint64_t inputIdentity;
    std::uint64_t completedTiles;
    std::uint64_t payloadDigest;
    // scratchRunId follows as a length-prefixed blob:
    std::uint32_t runIdBytes;
    // FNV over EVERY preceding byte of this struct (field zeroed while
    // hashing): protects the framing fields too, not just the payload.
    std::uint64_t fileDigest;
};
#pragma pack( pop )

constexpr char kMagic[8] = { 'S', 'I', 'C', 'N', 'U', 'T', 'C', '1' };

std::uint64_t payloadDigestOf( const TileCheckpoint &checkpoint )
{
    const std::uint64_t parts[3] = { checkpoint.operatorIdentity, checkpoint.inputIdentity,
                                     checkpoint.completedTiles };
    std::uint64_t digest = tileCheckpointHash( parts, sizeof( parts ) );
    digest = tileCheckpointHash( checkpoint.scratchRunId.data(), checkpoint.scratchRunId.size() )
             ^ ( digest << 1 );
    return digest;
}

void fsyncPath( const std::string &path, bool directory )
{
#if !defined( _WIN32 )
    const int flags = directory ? ( O_RDONLY | O_DIRECTORY ) : O_RDONLY;
    const int fd = ::open( path.c_str(), flags );
    if ( fd >= 0 )
    {
        ::fsync( fd );
        ::close( fd );
    }
#else
    (void)path;
    (void)directory;
#endif
}
} // namespace

std::uint64_t tileCheckpointHash( const void *data, std::size_t size )
{
    return tileCheckpointHashStep( tileCheckpointHashInit(), data, size );
}

bool TileCheckpointWriter::save( const std::string &path, const TileCheckpoint &checkpoint )
{
    TileCheckpointFile file{};
    std::memcpy( file.magic, kMagic, sizeof( kMagic ) );
    file.formatVersion = kTileCheckpointFormatVersion;
    file.headerSize = static_cast<std::uint32_t>( sizeof( TileCheckpointFile ) );
    file.operatorIdentity = checkpoint.operatorIdentity;
    file.inputIdentity = checkpoint.inputIdentity;
    file.completedTiles = checkpoint.completedTiles;
    file.payloadDigest = payloadDigestOf( checkpoint );
    file.runIdBytes = static_cast<std::uint32_t>( checkpoint.scratchRunId.size() );
    file.fileDigest = 0;
    file.fileDigest = tileCheckpointHash( &file, offsetof( TileCheckpointFile, fileDigest ) );

    // Unique tmp in the same directory (rename stays on one volume), then
    // fsync file + dir, then atomic replace — the checkpoint family.
    // Unique per CALL (F-A-15): pid alone collides for two threads saving
    // the same path concurrently — the counter makes each attempt distinct.
    static std::atomic<unsigned long long> saveCounter{ 0 };
    const std::string tmp = path + ".tmp." + std::to_string( currentPid() ) + "."
                            + std::to_string( saveCounter.fetch_add( 1 ) );
    {
        std::ofstream out( tmp, std::ios::binary | std::ios::trunc );
        if ( !out )
            return false;
        out.write( reinterpret_cast<const char *>( &file ), sizeof( file ) );
        out.write( checkpoint.scratchRunId.data(),
                   static_cast<std::streamsize>( checkpoint.scratchRunId.size() ) );
        out.flush();
        if ( !out )
        {
            out.close();
            std::remove( tmp.c_str() );
            return false;
        }
    }
    fsyncPath( tmp, /*directory=*/false );
    std::error_code ec;
    std::filesystem::rename( tmp, path, ec );
    if ( ec )
    {
        std::error_code removeEc;
        std::filesystem::remove( tmp, removeEc );
        return false;
    }
    if ( auto dir = std::filesystem::path( path ).parent_path(); !dir.empty() )
        fsyncPath( dir.string(), /*directory=*/true );
    return true;
}

std::optional<TileCheckpoint> TileCheckpointWriter::load(
    const std::string &path, std::uint64_t expectedOperatorIdentity,
    std::uint64_t expectedInputIdentity )
{
    std::error_code ec;
    if ( !std::filesystem::exists( path, ec ) )
        return std::nullopt;

    std::ifstream in( path, std::ios::binary );
    if ( !in )
        return std::nullopt;
    TileCheckpointFile file{};
    in.read( reinterpret_cast<char *>( &file ), sizeof( file ) );
    if ( static_cast<std::size_t>( in.gcount() ) != sizeof( file ) )
        return std::nullopt;
    if ( std::memcmp( file.magic, kMagic, sizeof( kMagic ) ) != 0 )
        return std::nullopt;
    if ( file.formatVersion != kTileCheckpointFormatVersion )
        return std::nullopt;
    if ( file.headerSize != sizeof( TileCheckpointFile ) )
        return std::nullopt;
    if ( file.runIdBytes > 4096 )
        return std::nullopt;
    {
        const std::uint64_t stored = file.fileDigest;
        file.fileDigest = 0;
        if ( tileCheckpointHash( &file, offsetof( TileCheckpointFile, fileDigest ) ) != stored )
            return std::nullopt;
    }

    TileCheckpoint checkpoint;
    checkpoint.formatVersion = file.formatVersion;
    checkpoint.operatorIdentity = file.operatorIdentity;
    checkpoint.inputIdentity = file.inputIdentity;
    checkpoint.completedTiles = file.completedTiles;
    checkpoint.scratchRunId.resize( file.runIdBytes );
    if ( file.runIdBytes )
    {
        in.read( checkpoint.scratchRunId.data(),
                 static_cast<std::streamsize>( file.runIdBytes ) );
        if ( static_cast<std::size_t>( in.gcount() ) != file.runIdBytes )
            return std::nullopt;
    }
    if ( file.payloadDigest != payloadDigestOf( checkpoint ) )
        return std::nullopt;
    // Fail-closed identity gates: ANY drift re-executes from scratch.
    if ( checkpoint.operatorIdentity != expectedOperatorIdentity )
        return std::nullopt;
    if ( checkpoint.inputIdentity != expectedInputIdentity )
        return std::nullopt;
    return checkpoint;
}

void TileCheckpointWriter::remove( const std::string &path )
{
    std::error_code ec;
    std::filesystem::remove( path, ec );
    // Best-effort tmp sweep from a crashed save in the same directory.
    for ( const auto &entry : std::filesystem::directory_iterator(
              std::filesystem::path( path ).parent_path(), ec ) )
    {
        if ( ec )
            break;
        const auto name = entry.path().filename().string();
        if ( name.rfind( std::filesystem::path( path ).filename().string() + ".tmp.", 0 ) == 0 )
        {
            std::error_code removeEc;
            std::filesystem::remove( entry.path(), removeEc );
        }
    }
}

} // namespace sicnu::runtime::chunk

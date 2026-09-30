// tile_checkpoint.cpp — see tile_checkpoint.h.
#include "tile_checkpoint.h"
#include "platform/durable_sidecar.h" // R6: the single sidecar write authority
#include "platform/portable.h"

#include <cstddef>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <filesystem>

namespace sicnu::runtime::chunk
{

namespace
{
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

/// Staged-name stem of a target: "<filename>" minus its last extension, the
/// same split the sidecar authority applies when it claims a staged temp.
/// Mirrors platform::sidecar::claimStagedPath so remove()'s residue sweep
/// matches the names the authority actually leaves behind.
std::string stagedStemOf( const std::string &path )
{
    const std::string filename =
        portable::pathToUtf8( portable::pathFromUtf8( path ).filename() );
    const std::size_t dot = filename.rfind( '.' );
    return dot == std::string::npos ? filename : filename.substr( 0, dot );
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

    // R6: the single sidecar write authority (unique tmp claimed in the
    // target directory -> fsync -> atomic publish -> parent-directory sync)
    // owns the "checkpoint family" durability this writer used to hand-roll.
    // No last-good rotation (""): a refused checkpoint must stay
    // indistinguishable from "no checkpoint" so the task restarts clean.
    std::string payload( reinterpret_cast<const char *>( &file ), sizeof( file ) );
    payload.append( checkpoint.scratchRunId );

    const sicnu::platform::sidecar::WriteResult written = sicnu::platform::sidecar::write(
      { path, std::move( payload ), "" } );
    return static_cast<bool>( written );
}

std::optional<TileCheckpoint> TileCheckpointWriter::load(
    const std::string &path, std::uint64_t expectedOperatorIdentity,
    std::uint64_t expectedInputIdentity )
{
    std::error_code ec;
    if ( !std::filesystem::exists( portable::pathFromUtf8( path ), ec ) )
        return std::nullopt;

    std::ifstream in( portable::pathFromUtf8( path ), std::ios::binary );
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

/// True when @p leaf is a staged temp the sidecar authority would leave for a
/// target whose staged stem is @p stem: "<stem>.<pid>.<ctr>.<rng>.tmp<ext>".
/// Never matches the target itself, and never a sibling sharing the stem.
bool isAuthorityStagedLeaf( const std::string &leaf, const std::string &stem )
{
    if ( leaf.rfind( stem + ".", 0 ) != 0 )
        return false;
    const std::string head = leaf.substr( stem.size() + 1 );
    const std::size_t tmpAt = head.find( ".tmp" );
    if ( tmpAt == std::string::npos )
        return false;
    int fields = 1;
    for ( const char c : head.substr( 0, tmpAt ) )
    {
        if ( c == '.' )
        {
            ++fields;
            continue;
        }
        if ( c < '0' || c > '9' )
            return false;
    }
    return fields == 3; // pid.counter.rng
}

void TileCheckpointWriter::remove( const std::string &path )
{
    std::error_code ec;
    std::filesystem::remove( portable::pathFromUtf8( path ), ec );
    // Best-effort staged-residue sweep from a crashed save in the same
    // directory. R6: save() stages through the sidecar authority, whose
    // staged name is "<stem>.<pid>.<ctr>.<rng>.tmp<ext>" (the extension is
    // kept), so the sweep follows that shape — the "<name>.tmp.*" family this
    // writer produced before convergence can no longer be left behind.
    // Names are compared as UTF-8 bytes on both sides (path::string() would
    // be ACP on Windows and never match the narrow base name).
    const std::string stem = stagedStemOf( path );
    for ( const auto &entry : std::filesystem::directory_iterator(
              portable::pathFromUtf8( path ).parent_path(), ec ) )
    {
        if ( ec )
            break;
        const auto name = portable::pathToUtf8( entry.path().filename() );
        if ( !isAuthorityStagedLeaf( name, stem ) )
            continue;
        std::error_code removeEc;
        std::filesystem::remove( entry.path(), removeEc );
    }
}

} // namespace sicnu::runtime::chunk

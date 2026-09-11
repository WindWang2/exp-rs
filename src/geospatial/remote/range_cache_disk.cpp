/***************************************************************************
  geospatial/remote/range_cache_disk.cpp — optional disk block layer (M3).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/remote/range_cache_disk.h"

#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/sha256.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace sicnu::geo
{

namespace
{

constexpr char kBlockMagic[4] = { 'S', 'R', 'B', '2' }; // sicnu range block, v2
constexpr std::uint64_t kEvictionScanEntryCap = 200000;  // bounded directory walk

struct DiskState
{
    std::mutex mutex; // guards name allocation, stats mutation, eviction scan
    std::string directory;
    std::uint64_t maxBytes = 0;
    bool enabled = false;
    RangeDiskCacheStats stats;
};

DiskState &disk()
{
  static DiskState state;
  return state;
}

/// Block file name: content-keyed, fixed layout
/// "<basis-hash>.<blockIndex>.blk".
std::string blockFileName( const std::string &basisHash, std::uint64_t blockIndex )
{
  return basisHash + "." + std::to_string( blockIndex ) + ".blk";
}

/// Serialized block: magic | blockIndex | dataLen | sha256(key||data) | data.
std::vector<unsigned char> serializeBlock( const std::string &basisHash, std::uint64_t blockIndex,
                                           const unsigned char *data, std::size_t size )
{
  Sha256 integrity;
  integrity.update( basisHash );
  integrity.update( std::to_string( blockIndex ) );
  integrity.update( data, size );

  std::vector<unsigned char> out;
  out.reserve( 4 + 8 + 8 + 32 + size );
  const auto appendU64 = [ &out ]( std::uint64_t value ) {
    for ( int shift = 56; shift >= 0; shift -= 8 )
      out.push_back( static_cast<unsigned char>( ( value >> shift ) & 0xFF ) );
  };
  out.insert( out.end(), kBlockMagic, kBlockMagic + 4 );
  appendU64( blockIndex );
  appendU64( static_cast<std::uint64_t>( size ) );
  const std::vector<unsigned char> digest = integrity.finalize();
  out.insert( out.end(), digest.begin(), digest.end() );
  out.insert( out.end(), data, data + size );
  return out;
}

struct ParsedBlock
{
  bool ok = false;
  std::uint64_t blockIndex = 0;
  std::size_t dataSize = 0;
  std::vector<unsigned char> data;
};

ParsedBlock parseBlock( const std::string &basisHash, const std::vector<unsigned char> &raw )
{
  ParsedBlock parsed;
  if ( raw.size() < 4 + 8 + 8 + 32 )
    return parsed;
  if ( std::memcmp( raw.data(), kBlockMagic, 4 ) != 0 )
    return parsed;
  const auto readU64 = [ &raw ]( std::size_t offset ) {
    std::uint64_t value = 0;
    for ( std::size_t i = 0; i < 8; ++i )
      value = ( value << 8 ) | raw[offset + i];
    return value;
  };
  parsed.blockIndex = readU64( 4 );
  parsed.dataSize = static_cast<std::size_t>( readU64( 12 ) );
  if ( parsed.dataSize == 0 || raw.size() != 4 + 8 + 8 + 32 + parsed.dataSize )
    return parsed;

  // Integrity: the stored digest must recompute over (basis, index, data).
  const unsigned char *storedDigest = raw.data() + 20;
  const unsigned char *data = raw.data() + 4 + 8 + 8 + 32;
  Sha256 integrity;
  integrity.update( basisHash );
  integrity.update( std::to_string( parsed.blockIndex ) );
  integrity.update( data, parsed.dataSize );
  const std::vector<unsigned char> computed = integrity.finalize();
  if ( std::memcmp( storedDigest, computed.data(), 32 ) != 0 )
    return parsed;

  parsed.data.assign( data, data + parsed.dataSize );
  parsed.ok = true;
  return parsed;
}

void fsyncPath( const fs::path &path )
{
#ifdef _WIN32
  const HANDLE handle = CreateFileW( path.wstring().c_str(), GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
  if ( handle != INVALID_HANDLE_VALUE )
  {
    FlushFileBuffers( handle );
    CloseHandle( handle );
  }
#else
  const int fd = ::open( path.c_str(), O_RDONLY );
  if ( fd >= 0 )
  {
    ::fsync( fd );
    ::close( fd );
  }
#endif
}

/// LRU (mtime) eviction under the byte cap. Bounded walk: a pathological
/// directory beyond the entry cap is trimmed by age ordering of what the
/// walk saw — never an unbounded scan.
void evictUnderCap( DiskState &state, std::uint64_t &bytesOnDiskOut )
{
  std::error_code ec;
  std::vector<std::pair<std::uint64_t, fs::path>> entries; // (mtime ticks, path)
  std::uint64_t total = 0;
  std::size_t scanned = 0;
  for ( const fs::directory_entry &entry : fs::directory_iterator( fs::u8path( state.directory ), ec ) )
  {
    if ( ec )
      break;
    if ( ++scanned > kEvictionScanEntryCap )
      break;
    if ( !entry.is_regular_file( ec ) || ec )
      continue;
    const std::string name = [&] {
      const std::u8string u8 = entry.path().filename().u8string();
      return std::string( u8.begin(), u8.end() );
    }();
    if ( name.size() < 6 || name.substr( name.size() - 4 ) != ".blk" )
      continue;
    total += entry.file_size( ec );
    if ( ec )
      continue;
    entries.emplace_back( entry.last_write_time( ec ).time_since_epoch().count(), entry.path() );
  }
  if ( total <= state.maxBytes )
  {
    bytesOnDiskOut = total;
    return;
  }
  std::sort( entries.begin(), entries.end(),
             []( const auto &a, const auto &b ) { return a.first < b.first; } ); // oldest first
  for ( const auto &[ mtime, path ] : entries )
  {
    if ( total <= state.maxBytes )
      break;
    std::error_code removeEc;
    const std::uintmax_t size = fs::file_size( path, removeEc );
    if ( fs::remove( path, removeEc ) && !removeEc )
    {
      total = total > size ? total - size : 0;
      state.stats.evictions += 1;
    }
  }
  bytesOnDiskOut = total;
}

} // namespace

Json::Value RangeDiskCacheStats::toJson() const
{
  Json::Value json;
  json["hits"] = static_cast<Json::UInt64>( hits );
  json["puts"] = static_cast<Json::UInt64>( puts );
  json["corrupt"] = static_cast<Json::UInt64>( corrupt );
  json["evictions"] = static_cast<Json::UInt64>( evictions );
  json["bytes_stored"] = static_cast<Json::UInt64>( bytesStored );
  return json;
}

void RangeDiskBlockStore::configure( const std::string &directory, std::uint64_t maxBytes )
{
  DiskState &state = disk();
  std::lock_guard<std::mutex> lock( state.mutex );
  state.directory = directory;
  state.maxBytes = maxBytes;
  state.enabled = !directory.empty();
  state.stats = RangeDiskCacheStats{};
  if ( state.enabled )
  {
    std::error_code ec;
    fs::create_directories( fs::u8path( directory ), ec );
    // An unusable directory disables the layer (an optimization, never a
    // correctness gate): reads/writes become honest no-op misses.
    state.enabled = !ec || fs::is_directory( fs::u8path( directory ) );
  }
}

bool RangeDiskBlockStore::enabled()
{
  DiskState &state = disk();
  std::lock_guard<std::mutex> lock( state.mutex );
  return state.enabled;
}

void RangeDiskBlockStore::clear()
{
  DiskState &state = disk();
  std::lock_guard<std::mutex> lock( state.mutex );
  if ( !state.enabled )
    return;
  std::error_code ec;
  for ( const fs::directory_entry &entry : fs::directory_iterator( fs::u8path( state.directory ), ec ) )
  {
    if ( ec )
      break;
    const std::string name = [&] {
      const std::u8string u8 = entry.path().filename().u8string();
      return std::string( u8.begin(), u8.end() );
    }();
    if ( name.size() >= 6 && name.substr( name.size() - 4 ) == ".blk" )
      fs::remove( entry.path(), ec );
  }
  state.stats = RangeDiskCacheStats{};
}

RangeDiskCacheStats RangeDiskBlockStore::stats()
{
  DiskState &state = disk();
  std::lock_guard<std::mutex> lock( state.mutex );
  return state.stats;
}

std::string RangeDiskBlockStore::identityBasis( const std::string &requestUrl, bool hasStrongEtag,
                                                const std::string &etag, bool hasSize,
                                                std::uint64_t sizeBytes,
                                                const std::string &lastModified )
{
  // Strong ETag first (byte-level provability). Without one, size +
  // Last-Modified is the declared trust basis; with neither, the resource is
  // NOT disk-cacheable — unprovable identity never masquerades as cacheable.
  if ( hasStrongEtag && !etag.empty() )
    return "etag\n" + requestUrl + "\n" + etag;
  if ( hasSize && !lastModified.empty() )
    return "lm\n" + requestUrl + "\n" + std::to_string( sizeBytes ) + "\n" + lastModified;
  return std::string();
}

bool RangeDiskBlockStore::readBlock( const std::string &basis, std::uint64_t blockIndex,
                                     std::vector<unsigned char> &outData )
{
  DiskState &state = disk();
  if ( basis.empty() )
    return false;
  {
    std::lock_guard<std::mutex> lock( state.mutex );
    if ( !state.enabled )
      return false;
  }
  const std::string basisHash = sha256Hex( basis );
  const std::string path = ( fs::u8path( state.directory ) / blockFileName( basisHash, blockIndex ) ).string();

  // Read OUTSIDE every lock: open/read is the slow part and the file layout
  // is immutable once published under its final name.
  std::FILE *file = std::fopen( path.c_str(), "rb" );
  if ( !file )
    return false;
  std::vector<unsigned char> raw;
  unsigned char buffer[64 * 1024];
  std::size_t got = 0;
  bool readError = false;
  while ( ( got = std::fread( buffer, 1, sizeof( buffer ), file ) ) > 0 )
    raw.insert( raw.end(), buffer, buffer + got );
  readError = std::ferror( file ) != 0;
  std::fclose( file );
  if ( readError )
    return false;

  const ParsedBlock parsed = parseBlock( basisHash, raw );
  if ( !parsed.ok )
  {
    // Corrupt or torn: refuse and unlink. A miss, never wrong bytes.
    std::lock_guard<std::mutex> lock( state.mutex );
    state.stats.corrupt += 1;
    std::error_code ec;
    fs::remove( fs::u8path( path ), ec );
    return false;
  }
  if ( parsed.blockIndex != blockIndex )
  {
    std::lock_guard<std::mutex> lock( state.mutex );
    state.stats.corrupt += 1;
    return false;
  }
  outData = std::move( parsed.data );
  {
    std::lock_guard<std::mutex> lock( state.mutex );
    state.stats.hits += 1;
  }
  return true;
}

void RangeDiskBlockStore::putBlock( const std::string &basis, std::uint64_t blockIndex,
                                    const unsigned char *data, std::size_t size )
{
  DiskState &state = disk();
  if ( basis.empty() || size == 0 )
    return;
  std::lock_guard<std::mutex> lock( state.mutex );
  if ( !state.enabled )
    return;

  const std::string basisHash = sha256Hex( basis );
  const std::vector<unsigned char> serialized = serializeBlock( basisHash, blockIndex, data, size );
  const std::string finalName = blockFileName( basisHash, blockIndex );
  const fs::path directory = fs::u8path( state.directory );
  const std::string tempPath = ( directory / ( finalName + ".tmp" ) ).string();
  const std::string finalPath = ( directory / finalName ).string();

  std::FILE *file = std::fopen( tempPath.c_str(), "wb" );
  if ( !file )
    return; // optimization layer: unusable disk degrades to misses
  const std::size_t written = std::fwrite( serialized.data(), 1, serialized.size(), file );
  std::fclose( file );
  if ( written != serialized.size() )
  {
    std::error_code ec;
    fs::remove( fs::u8path( tempPath ), ec );
    return;
  }
  fsyncPath( fs::u8path( tempPath ) );
  std::error_code ec;
  fs::rename( fs::u8path( tempPath ), fs::u8path( finalPath ), ec );
  if ( ec )
  {
    fs::remove( fs::u8path( tempPath ), ec );
    return;
  }
  state.stats.puts += 1;
  std::uint64_t bytesOnDisk = 0;
  evictUnderCap( state, bytesOnDisk );
  state.stats.bytesStored = bytesOnDisk;
}

} // namespace sicnu::geo

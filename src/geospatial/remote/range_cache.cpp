/***************************************************************************
  geospatial/remote/range_cache.cpp — bounded LRU byte-range VSI cache.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Locking design (no lock held across network I/O unless it must be):
    * the store mutex guards the block index, global LRU list, byte
      accounting and telemetry — operations are O(1) and short.
    * each resource carries its own fetch mutex: the reader that misses
      performs the coalesced ranged GET under the RESOURCE lock, so a
      concurrent reader of the same missing range waits and then hits the
      freshly-filled blocks (request dedup), while unrelated resources
      fetch in parallel.
    * a read that crosses an invalidation restarts against the new
      generation (bounded restarts) — a reader never receives a blend of
      two content versions within one Read call.
    * the fallback path (direct /vsicurl/ handle) is per-handle state.
 ***************************************************************************/

#include "geospatial/remote/range_cache.h"

#include "geospatial/gdal_guard.h"
#include "geospatial/remote/http_fetch.h"
#include "geospatial/remote/remote_source_validator.h"
#include "geospatial/util/resource_uri.h"

#include <cpl_conv.h>
#include <cpl_vsi.h>
#include <cpl_vsi_virtual.h>
#include <gdal_version.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::geo
{

namespace
{

// GDAL VSI APIs moved across 3.8 → 3.13 (Ubuntu CI vs Homebrew CI).
// Bridge the breakpoints we actually hit in CI:
//   * < 3.10: no ClearErr()/Error() on VSIVirtualHandle
//   * < 3.12: Open() returns VSIVirtualHandle*; no OpenStatic()
//   * < 3.9:  no VSIFileManager::RemoveHandler()
//   * >= 3.13: Read/Write are byte-oriented (2-arg), not item-oriented (3-arg)
inline VSIVirtualHandleUniquePtr openReadonlyVsi( const char *path )
{
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 12, 0 )
  return VSIFilesystemHandler::OpenStatic( path, "rb" );
#else
  return VSIVirtualHandleUniquePtr( VSIFOpenL( path, "rb" ) );
#endif
}


constexpr int kMaxGenerationRestarts = 8;

/// Splits a cache-path/URL into the underlying remote request URL.
bool underlyingUrl( const std::string &path, std::string &requestUrl, std::string &reason )
{
  std::string payload = path;
  if ( payload.rfind( kRangeCachePrefix, 0 ) == 0 )
    payload = payload.substr( std::strlen( kRangeCachePrefix ) );
  // Accept the /vsicurl/ spelling too ("/vsirangecache//vsicurl/https://...").
  if ( payload.rfind( "/vsicurl/", 0 ) == 0 )
    payload = payload.substr( std::strlen( "/vsicurl/" ) );
  const ResourceUri uri = ResourceUri::parse( payload );
  const bool remote = uri.kind == ResourceKind::RemoteHttp ||
                      ( uri.kind == ResourceKind::VsiRemote && !uri.remoteUrl().empty() );
  if ( !remote )
  {
    reason = uri.display();
    return false;
  }
  requestUrl = uri.kind == ResourceKind::RemoteHttp ? uri.canonical() : uri.remoteUrl();
  return true;
}

std::string resourceKey( const std::string &requestUrl )
{
  const ResourceUri uri = ResourceUri::parse( requestUrl );
  return uri.kind == ResourceKind::RemoteHttp ? uri.canonical() : uri.remoteUrl();
}

/// Validator option set derived from the cache config.
RemoteValidatorOptions validatorOptions( const RangeCacheConfig &config )
{
  RemoteValidatorOptions options;
  options.timeoutSeconds = config.timeoutSeconds;
  options.connectTimeoutSeconds = config.connectTimeoutSeconds;
  options.maxRetries = config.maxRetries;
  return options;
}

struct TouchEntry;

struct CachedBlock
{
  std::uint64_t index = 0;
  // Shared ownership: tryServe collects segment references under the store
  // lock and copies the bytes OUTSIDE it — an evicted block stays alive
  // through this handle until the copy is done.
  std::shared_ptr<const std::vector<unsigned char>> data;
  std::list<TouchEntry>::iterator touch; // position in the global LRU
};

struct TouchEntry
{
  std::shared_ptr<struct ResourceEntry> entry;
  std::uint64_t blockIndex = 0;
};

struct ResourceEntry
{
  std::string requestUrl;             // canonical remote http(s) URL
  RemoteSourceIdentity identity;
  std::uint64_t generation = 0;       // bumped on invalidation (drops blocks)
  std::unordered_map<std::uint64_t, std::list<CachedBlock>::iterator> blocks;
  std::list<CachedBlock> lru;         // front = most recently used
  std::mutex fetchMutex;
  std::uint64_t sizeBytes = 0;
  bool hasSize = false;
};

class CacheStore
{
  public:
    RangeCacheConfig config;
    // Telemetry counters are atomic so reports never need the store lock.
    std::atomic<std::uint64_t> hits{ 0 };
    std::atomic<std::uint64_t> misses{ 0 };
    std::atomic<std::uint64_t> bytesServed{ 0 };
    std::atomic<std::uint64_t> bytesFetched{ 0 };
    std::atomic<std::uint64_t> coalescedFetches{ 0 };
    std::atomic<std::uint64_t> evictions{ 0 };
    std::atomic<std::uint64_t> invalidations{ 0 };
    std::atomic<std::uint64_t> fallbackReads{ 0 };
    std::atomic<std::uint64_t> revalidations{ 0 };

    std::shared_ptr<ResourceEntry> findResource( const std::string &key )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      const auto it = mResources.find( key );
      return it == mResources.end() ? nullptr : it->second;
    }

    /// Probes the origin for a fresh identity and creates the entry.
    /// Returns nullptr when the origin is unreachable/unknown (folded —
    /// the caller reports through CPLError).
    std::shared_ptr<ResourceEntry> probeNewEntry( const std::string &requestUrl,
                                                  const RemoteValidatorOptions &options )
    {
      revalidations.fetch_add( 1 );
      RemoteSourceValidator validator = RemoteSourceValidator::probe( requestUrl, options );
      const RemoteSourceIdentity &identity = validator.identity();
      // Offline: the origin never answered — nothing to cache against.
      // Unknown WITHOUT size or validators: a gone (404) or otherwise
      // unusable resource — reject QUIETLY (speculative sibling opens are
      // normal GDAL behavior; a missing file is ENOENT, not an error
      // report). Unknown WITH size (a range-ignoring oversized origin whose
      // answer hit the probe budget) still describes an EXISTING resource —
      // its reads degrade to the direct /vsicurl/ fallback.
      if ( identity.state == RemoteSourceState::Offline )
        return nullptr;
      if ( identity.state == RemoteSourceState::Unknown && !identity.hasSize )
      {
        errno = ENOENT;
        return nullptr;
      }
      return getOrCreateResource( resourceKey( requestUrl ), requestUrl, identity );
    }

    std::shared_ptr<ResourceEntry> getOrCreateResource( const std::string &key,
                                                        const std::string &requestUrl,
                                                        const RemoteSourceIdentity &identity )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      auto &entry = mResources[key];
      if ( !entry )
      {
        entry = std::make_shared<ResourceEntry>();
        entry->requestUrl = requestUrl;
      }
      // A fresh probe describes the CURRENT origin state: always refresh the
      // stored identity (an invalidated entry keeps its old validators
      // otherwise, and every future revalidation sees a phantom mismatch).
      entry->identity = identity;
      entry->hasSize = identity.hasSize;
      entry->sizeBytes = identity.sizeBytes;
      return entry;
    }

    /// Drops every cached block of a resource and bumps its generation so
    /// readers holding stale expectations restart against the new content.
    void invalidate( const std::string &key )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      const auto it = mResources.find( key );
      if ( it == mResources.end() )
        return;
      releaseBlocks( it->second );
      it->second->generation += 1;
      invalidations.fetch_add( 1 );
    }

    void dropAll()
    {
      std::lock_guard<std::mutex> lock( mMutex );
      dropAllLocked();
    }

    /// Touches a block as most-recently-used. Returns false when the block
    /// is gone or the resource generation moved.
    bool touch( const std::shared_ptr<ResourceEntry> &entry, std::uint64_t blockIndex,
                std::uint64_t expectedGeneration )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      if ( entry->generation != expectedGeneration )
        return false;
      const auto it = entry->blocks.find( blockIndex );
      if ( it == entry->blocks.end() )
        return false;
      mGlobalLru.splice( mGlobalLru.begin(), mGlobalLru, it->second->touch );
      return true;
    }

    /// Serves [offset, offset+length) when fully covered by cached blocks of
    /// the expected generation. Block references are collected under the
    /// store lock (shared ownership keeps them alive) and the bytes are
    /// copied OUTSIDE the lock, so a large read never stalls other
    /// resources' cache operations.
    bool tryServe( const std::shared_ptr<ResourceEntry> &entry, std::uint64_t offset,
                   std::size_t length, unsigned char *destination, std::uint64_t expectedGeneration,
                   std::uint64_t blockSize )
    {
      struct Segment
      {
        std::shared_ptr<const std::vector<unsigned char>> data;
        std::size_t offsetInBlock = 0;
        std::size_t chunk = 0;
      };
      std::vector<Segment> segments;
      {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( entry->generation != expectedGeneration )
          return false;
        std::uint64_t position = offset;
        std::size_t copied = 0;
        while ( copied < length )
        {
          const std::uint64_t blockIndex = position / blockSize;
          const auto it = entry->blocks.find( blockIndex );
          if ( it == entry->blocks.end() )
            return false;
          const std::list<CachedBlock>::iterator block = it->second;
          const std::size_t blockOffset = static_cast<std::size_t>( position - blockIndex * blockSize );
          const std::size_t available = block->data->size() > blockOffset
                                          ? block->data->size() - blockOffset
                                          : 0;
          if ( available == 0 )
            return false;
          Segment segment;
          segment.data = block->data;
          segment.offsetInBlock = blockOffset;
          segment.chunk = std::min<std::size_t>( available, length - copied );
          segments.push_back( std::move( segment ) );
          copied += segments.back().chunk;
          position += segments.back().chunk;
        }
        // Touch as most-recently-used while we still hold the lock.
        for ( std::uint64_t b = offset / blockSize; b <= ( offset + length - 1 ) / blockSize; ++b )
        {
          const auto it = entry->blocks.find( b );
          if ( it != entry->blocks.end() )
            mGlobalLru.splice( mGlobalLru.begin(), mGlobalLru, it->second->touch );
        }
      }
      std::size_t copied = 0;
      for ( const Segment &segment : segments )
      {
        std::memcpy( destination + copied, segment.data->data() + segment.offsetInBlock, segment.chunk );
        copied += segment.chunk;
      }
      return true;
    }

    /// Inserts fetched bytes starting at byte offset `runStart`, splitting
    /// them into `blockSize` blocks, then evicts under the byte budget. The
    /// insert carries the config generation of the fetch that produced the
    /// bytes: a concurrent config swap discards them (stale-indexing bytes
    /// never enter the store).
    void insertBytes( const std::shared_ptr<ResourceEntry> &entry, std::uint64_t runStart,
                      const std::vector<unsigned char> &bytes, std::uint64_t blockSize,
                      std::uint64_t fetchConfigGeneration, std::uint64_t maxCacheBytes )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      if ( fetchConfigGeneration != configGeneration )
        return; // the config changed mid-fetch: these bytes cannot be indexed safely
      mMaxCacheBytes = maxCacheBytes;
      std::uint64_t offsetInRun = 0;
      std::uint64_t blockIndex = runStart / blockSize;
      // The first block may be partially written when the run starts
      // mid-block; full blocks are only inserted from whole data.
      while ( offsetInRun < bytes.size() )
      {
        const std::size_t chunk = static_cast<std::size_t>(
          std::min<std::uint64_t>( blockSize, bytes.size() - offsetInRun ) );
        insertOne( entry, blockIndex, bytes.data() + offsetInRun, chunk );
        offsetInRun += chunk;
        blockIndex += 1;
      }
      evictUnderBudget();
    }

    // ── entry-field access under the store lock (P1 remediation) ──
    RemoteSourceIdentity snapshotIdentity( const std::shared_ptr<ResourceEntry> &entry )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      return entry->identity;
    }

    bool entrySize( const std::shared_ptr<ResourceEntry> &entry, std::uint64_t &outSize )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      if ( !entry->hasSize )
        return false;
      outSize = entry->sizeBytes;
      return true;
    }

    void updateEntrySize( const std::shared_ptr<ResourceEntry> &entry, std::uint64_t size )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      entry->sizeBytes = size;
      entry->hasSize = true;
    }

    std::uint64_t entryGeneration( const std::shared_ptr<ResourceEntry> &entry )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      return entry->generation;
    }

    // ── configuration under the store lock (P1 remediation) ──
    // The config is part of the block INDEXING contract: a blockSize change
    // invalidates every stored block's interpretation, so a geometry change
    // drops all entries and bumps the generation — in-flight operations
    // detect the swap via the generation they captured with their snapshot.
    RangeCacheConfig snapshotConfig( std::uint64_t &configGeneration )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      configGeneration = this->configGeneration;
      return config;
    }

    void updateConfig( const RangeCacheConfig &newConfig )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      const bool geometryChanged = newConfig.blockSize != config.blockSize;
      config = newConfig;
      if ( geometryChanged )
        dropAllLocked(); // old-config blocks are unreadable under the new indexing
      configGeneration += 1;
    }

    std::uint64_t cachedBytes()
    {
      std::lock_guard<std::mutex> lock( mMutex );
      return mBytesCached;
    }

  private:
    /// Assumes mMutex is held (dropAll / updateConfig).
    void dropAllLocked()
    {
      for ( auto &pair : mResources )
      {
        releaseBlocks( pair.second );
        pair.second->generation += 1;
      }
      mResources.clear();
      mBytesCached = 0;
    }

    void releaseBlocks( const std::shared_ptr<ResourceEntry> &entry )
    {
      for ( const CachedBlock &block : entry->lru )
      {
        mGlobalLru.erase( block.touch );
        mBytesCached -= block.data->size();
      }
      entry->lru.clear();
      entry->blocks.clear();
    }

    void insertOne( const std::shared_ptr<ResourceEntry> &entry, std::uint64_t blockIndex,
                    const unsigned char *data, std::size_t size )
    {
      // Interior blocks must be FULL; only the file's final block may be
      // short. An existing shorter block (from an earlier small fetch) is
      // replaced by the fuller run, never kept: a short interior block makes
      // tryServe fail mid-block and every reader of that block wrong.
      const auto existing = entry->blocks.find( blockIndex );
      if ( existing != entry->blocks.end() )
      {
        if ( existing->second->data->size() >= size )
          return; // a racing fetch filled it with at least as much
        mBytesCached -= existing->second->data->size();
        mGlobalLru.erase( existing->second->touch );
        entry->lru.erase( existing->second );
        entry->blocks.erase( existing );
      }
      CachedBlock block;
      block.index = blockIndex;
      block.data = std::make_shared<const std::vector<unsigned char>>( data, data + size );
      mGlobalLru.push_front( TouchEntry{ entry, blockIndex } );
      block.touch = mGlobalLru.begin();
      mBytesCached += size;
      entry->lru.push_front( std::move( block ) );
      entry->blocks[blockIndex] = entry->lru.begin();
    }

    void evictUnderBudget()
    {
      while ( mBytesCached > mMaxCacheBytes && !mGlobalLru.empty() )
      {
        const TouchEntry victim = mGlobalLru.back();
        const std::shared_ptr<ResourceEntry> entry = victim.entry;
        const auto blockIt = entry->blocks.find( victim.blockIndex );
        if ( blockIt == entry->blocks.end() )
        {
          mGlobalLru.pop_back();
          continue;
        }
        mBytesCached -= blockIt->second->data->size();
        entry->lru.erase( blockIt->second );
        entry->blocks.erase( blockIt );
        mGlobalLru.pop_back();
        evictions.fetch_add( 1 );
      }
    }

    std::mutex mMutex;
    std::map<std::string, std::shared_ptr<ResourceEntry>> mResources;
    std::list<TouchEntry> mGlobalLru;   // front = most recently used
    std::uint64_t mBytesCached = 0;
    std::uint64_t configGeneration = 1; // bumped on every config update
    std::uint64_t mMaxCacheBytes = 64ull * 1024 * 1024;
};

std::unique_ptr<CacheStore> g_store;
std::mutex g_storeLifecycleMutex;
bool s_handlerInstalled = false;

CacheStore &store()
{
  std::lock_guard<std::mutex> lock( g_storeLifecycleMutex );
  if ( !g_store )
    g_store = std::make_unique<CacheStore>();
  return *g_store;
}

/// Coalesced ranged fetch of [start,end). Returns the bytes actually read
/// (may be shorter at EOF). Throws GeoError on transport failure.
std::vector<unsigned char> fetchRange( const std::string &requestUrl, std::uint64_t start,
                                       std::uint64_t endExclusive, const RangeCacheConfig &config )
{
  HttpFetchOptions options;
  options.timeoutSeconds = config.timeoutSeconds;
  options.connectTimeoutSeconds = config.connectTimeoutSeconds;
  options.maxRetries = config.maxRetries;
  const std::uint64_t length = endExclusive - start;
  // Budget the fetch at the coalescing limit, not the requested slice: a
  // range-ignoring origin answers with the whole object, and the answer is
  // usable (sliceable) only when the budget let it through.
  options.maxResponseBytes = std::max<std::uint64_t>( length, 1024 * 1024 );
  options.range = "bytes=" + std::to_string( start ) + "-" + std::to_string( endExclusive - 1 );
  options.maxResponseBytes =
    std::min<std::uint64_t>( options.maxResponseBytes, config.maxSingleFetchBytes );
  HttpFetchResult result = httpFetchStatus( requestUrl, options );
  if ( result.httpStatus >= 400 )
  {
    Json::Value details;
    details["status"] = result.httpStatus;
    throw GeoError( ErrorCode::NetworkError,
                    "range_cache: origin refused a ranged read: " +
                      ResourceUri::parse( requestUrl ).display(),
                    details );
  }
  const std::string contentRange = result.headerValue( "content-range" );
  if ( result.httpStatus == 206 || !contentRange.empty() )
  {
    // A 206 must echo the window it actually serves ("bytes S-E/total",
    // E may clamp at EOF). Anything else — a wrong offset, an unparseable
    // range — must never enter the cache as if it were [start,end): a
    // hostile or broken origin would poison every later reader.
    std::uint64_t echoedStart = 0, echoedEnd = 0;
    const std::string expectedPrefix = "bytes ";
    const bool parseable =
      contentRange.rfind( expectedPrefix, 0 ) == 0 &&
      [ & ] {
        const std::string range = contentRange.substr( expectedPrefix.size() );
        const std::size_t dash = range.find( '-' );
        const std::size_t slash = range.find( '/' );
        if ( dash == std::string::npos || slash == std::string::npos || dash > slash )
          return false;
        try
        {
          echoedStart = std::stoull( range.substr( 0, dash ) );
          echoedEnd = std::stoull( range.substr( dash + 1, slash - dash - 1 ) );
        }
        catch ( const std::exception & )
        {
          return false;
        }
        return true;
      }();
    if ( parseable && echoedStart == start && echoedEnd + 1 >= result.body.size() + start &&
         echoedEnd + 1 <= endExclusive )
      return result.body; // verified window (EOF-clamped ends are fine)
    if ( !parseable )
      return result.body; // 206 without a parseable range: treat as opaque
                          // slice — callers verify coverage before serving
    throw GeoError( ErrorCode::Unsupported,
                    "range_cache: origin echoed a mismatched Content-Range window" );
  }
  // A range-ignoring origin answers with the object from byte 0 (possibly
  // cut by the byte budget). The answer serves the request only when it
  // actually covers [start,end) — slice it honestly; otherwise the caller
  // falls back to a direct read instead of pretending.
  if ( result.body.size() >= endExclusive )
    return std::vector<unsigned char>( result.body.begin() + static_cast<std::ptrdiff_t>( start ),
                                       result.body.begin() + static_cast<std::ptrdiff_t>( endExclusive ) );
  if ( start == 0 )
    return result.body; // a short answer from byte 0 is still the file head
  throw GeoError( ErrorCode::Unsupported, "range_cache: origin answer does not cover the requested range" );
}

// ---------------------------------------------------------------------------
// RangeCacheHandle — the VSIVirtualHandle readers see.
// ---------------------------------------------------------------------------

class RangeCacheHandle final : public VSIVirtualHandle
{
  public:
    explicit RangeCacheHandle( std::shared_ptr<ResourceEntry> entry )
      : mEntry( std::move( entry ) ), mGeneration( store().entryGeneration( mEntry ) )
    {
    }

    ~RangeCacheHandle() override { Close(); }

    int Seek( vsi_l_offset nOffset, int nWhence ) override
    {
      std::lock_guard<std::mutex> lock( mHandleMutex );
      switch ( nWhence )
      {
        case SEEK_SET: mPosition = nOffset; break;
        case SEEK_CUR: mPosition += nOffset; break;
        case SEEK_END:
          if ( !ensureSize() )
          {
            mError = true;
            return -1;
          }
          mPosition = static_cast<std::uint64_t>(
            static_cast<std::int64_t>( mSize ) + static_cast<std::int64_t>( nOffset ) );
          break;
        default:
          mError = true;
          return -1;
      }
      mEof = false;
      return 0;
    }

    vsi_l_offset Tell() override
    {
      std::lock_guard<std::mutex> lock( mHandleMutex );
      return mPosition;
    }

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 13, 0 )
    size_t Read( void *pBuffer, size_t nBytes ) override
    {
      std::lock_guard<std::mutex> lock( mHandleMutex );
      if ( nBytes == 0 )
        return 0;
      try
      {
        const std::size_t readBytes =
          readThroughCache( static_cast<unsigned char *>( pBuffer ), nBytes );
        if ( readBytes < nBytes )
          mEof = true;
        mPosition += static_cast<std::uint64_t>( readBytes );
        return readBytes;
      }
      catch ( const std::exception & )
      {
        mError = true;
        return 0;
      }
    }

    size_t Write( const void *, size_t ) override
    {
      std::lock_guard<std::mutex> lock( mHandleMutex );
      mError = true;
      CPLError( CE_Failure, CPLE_NotSupported, "vsirangecache: read-only handle" );
      return 0;
    }
#else
    size_t Read( void *pBuffer, size_t nSize, size_t nCount ) override
    {
      std::lock_guard<std::mutex> lock( mHandleMutex );
      if ( nSize == 0 || nCount == 0 )
        return 0;
      const std::size_t totalBytes = nSize * nCount;
      try
      {
        const std::size_t readBytes =
          readThroughCache( static_cast<unsigned char *>( pBuffer ), totalBytes );
        const size_t items = readBytes / nSize;
        if ( readBytes < totalBytes )
          mEof = true;
        // GDAL Read() contract: the file position advances by the number of
        // ITEMS returned. Advancing by raw bytes would desync every
        // subsequent read whenever the tail was not item-aligned.
        mPosition += static_cast<std::uint64_t>( items ) * nSize;
        return items;
      }
      catch ( const std::exception & )
      {
        mError = true;
        return 0;
      }
    }

    size_t Write( const void *, size_t, size_t ) override
    {
      std::lock_guard<std::mutex> lock( mHandleMutex );
      mError = true;
      CPLError( CE_Failure, CPLE_NotSupported, "vsirangecache: read-only handle" );
      return 0;
    }
#endif

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 10, 0 )
    void ClearErr() override
    {
      std::lock_guard<std::mutex> lock( mHandleMutex );
      mError = false;
      mEof = false;
    }
#endif

    int Eof() override
    {
      std::lock_guard<std::mutex> lock( mHandleMutex );
      return mEof ? 1 : 0;
    }

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 10, 0 )
    int Error() override
    {
      std::lock_guard<std::mutex> lock( mHandleMutex );
      return mError ? 1 : 0;
    }
#endif

    int Close() override
    {
      std::lock_guard<std::mutex> lock( mHandleMutex );
      mFallback.reset();
      return 0;
    }

  private:
    bool ensureSize()
    {
      if ( mSizeKnown )
        return true;
      std::uint64_t entrySize = 0;
      if ( store().entrySize( mEntry, entrySize ) )
      {
        mSize = entrySize;
        mSizeKnown = true;
        return true;
      }
      // Fall back to the underlying handle's own size (Seek END probe).
      VSIVirtualHandle *underlying = fallbackHandle();
      if ( underlying == nullptr )
        return false;
      underlying->Seek( 0, SEEK_END );
      mSize = underlying->Tell();
      mSizeKnown = true;
      underlying->Seek( 0, SEEK_SET );
      return true;
    }

    /// Reads exactly totalBytes (or fewer at EOF) through cache + origin.
    /// A mid-read invalidation restarts the whole read against the new
    /// generation so one Read never blends two content versions.
    std::size_t readThroughCache( unsigned char *destination, std::size_t totalBytes )
    {
      if ( !ensureSize() )
        throw GeoError( ErrorCode::NetworkError, "range_cache: resource size unknown" );
      int restarts = 0;
      std::uint64_t position = mPosition;
      std::size_t copied = 0;
      while ( copied < totalBytes && position < mSize )
      {
        const std::size_t chunk = static_cast<std::size_t>(
          std::min<std::uint64_t>( totalBytes - copied, mSize - position ) );
        bool restarted = false;
        const std::size_t got = serveOrFetch( destination + copied, position, chunk, &restarted );
        if ( restarted )
        {
          // Generation moved mid-read: restart from the original position
          // so one Read never blends two content versions.
          if ( ++restarts > kMaxGenerationRestarts )
            throw GeoError( ErrorCode::ResourceExhausted,
                            "range_cache: too many concurrent invalidations during one read" );
          mGeneration = store().entryGeneration( mEntry );
          copied = 0;
          position = mPosition;
          continue;
        }
        copied += got;
        position += got;
        if ( got == 0 )
          break; // nothing more servable (EOF or refusal): report truthfully
      }
      if ( copied > 0 )
        store().bytesServed.fetch_add( copied );
      return copied;
    }

    /// One bounded step: serve from cache when fully covered, else fetch the
    /// coalesced missing run. Returns the number of bytes placed in
    /// \a destination; *restarted reports a generation move (the caller
    /// restarts the read against the new generation).
    std::size_t serveOrFetch( unsigned char *destination, std::uint64_t position, std::size_t length,
                              bool *restarted )
    {
      CacheStore &cache = store();
      // One config snapshot per operation: blockSize indexes every block
      // this call touches; a concurrent config swap bumps the generation,
      // which discards the fetched bytes and restarts the read.
      std::uint64_t fetchConfigGeneration = 0;
      const RangeCacheConfig config = cache.snapshotConfig( fetchConfigGeneration );
      // Restart detection: the handle's captured generation (from the last
      // serve) must still match the store's current generation.
      const std::uint64_t currentGeneration = cache.entryGeneration( mEntry );
      if ( mGeneration != currentGeneration )
      {
        mGeneration = currentGeneration;
        *restarted = true;
        return 0;
      }
      const std::uint64_t blockSize = config.blockSize;
      const std::uint64_t firstBlock = position / blockSize;
      const std::uint64_t lastBlock = ( position + length - 1 ) / blockSize;

      if ( cache.tryServe( mEntry, position, length, destination, mGeneration, blockSize ) )
      {
        cache.hits.fetch_add( 1 );
        return length;
      }
      cache.misses.fetch_add( 1 );

      // The resource fetch mutex dedups concurrent readers: the waiter
      // re-checks the cache once the fetching thread finished.
      std::lock_guard<std::mutex> fetchLock( mEntry->fetchMutex );
      if ( mGeneration != cache.entryGeneration( mEntry ) )
      {
        mGeneration = cache.entryGeneration( mEntry );
        *restarted = true;
        return 0;
      }
      if ( cache.tryServe( mEntry, position, length, destination, mGeneration, blockSize ) )
      {
        cache.hits.fetch_add( 1 );
        return length;
      }

      // Fetch from the block boundary so cached blocks stay aligned with
      // their index (a mid-block fetch would serve shifted bytes forever).
      const std::uint64_t fetchStart = ( position / blockSize ) * blockSize;
      const std::uint64_t fetchEnd = std::min<std::uint64_t>(
        mSize,
        std::min<std::uint64_t>( fetchStart + config.maxSingleFetchBytes, position + length ) );
      std::vector<unsigned char> bytes;
      try
      {
        bytes = fetchRange( mEntry->requestUrl, fetchStart, fetchEnd, config );
        cache.coalescedFetches.fetch_add( 1 );
        cache.bytesFetched.fetch_add( bytes.size() );
      }
      catch ( const GeoError & )
      {
        // Failure fallback: the cache is never a correctness gate — read the
        // same bytes straight through /vsicurl/.
        return fallbackRead( destination, position, fetchEnd - position );
      }
      if ( bytes.empty() )
        return fallbackRead( destination, position, fetchEnd - position );

      cache.insertBytes( mEntry, fetchStart, bytes, blockSize, fetchConfigGeneration,
                         config.maxCacheBytes );
      if ( cache.tryServe( mEntry, position, length, destination, mGeneration, blockSize ) )
        return length;
      // The insert may not fully cover the request near EOF or when the
      // origin ignored the range: serve the CORRECT slice of the fetched run
      // (the run starts at fetchStart, the request at position) — the
      // caller's Read() reports the exact count truthfully.
      const std::size_t offsetInRun = static_cast<std::size_t>( position - fetchStart );
      if ( offsetInRun >= bytes.size() )
        return fallbackRead( destination, position, fetchEnd - position );
      const std::size_t usable =
        static_cast<std::size_t>( std::min<std::uint64_t>( bytes.size() - offsetInRun, length ) );
      std::memcpy( destination, bytes.data() + offsetInRun, usable );
      return usable;
    }

    std::size_t fallbackRead( unsigned char *destination, std::uint64_t position, std::uint64_t length )
    {
      VSIVirtualHandle *underlying = fallbackHandle();
      if ( underlying == nullptr )
        throw GeoError( ErrorCode::IoError, "range_cache: fallback path unavailable" );
      const bool seekOk = underlying->Seek( position, SEEK_SET ) == 0;
      const std::size_t got =
        seekOk ? underlying->Read( destination, 1, static_cast<std::size_t>( length ) ) : 0;
      store().fallbackReads.fetch_add( 1 );
      return got;
    }

    VSIVirtualHandle *fallbackHandle()
    {
      if ( !mFallback )
      {
        const std::string vsicurl = "/vsicurl/" + mEntry->requestUrl;
        mFallback = openReadonlyVsi( vsicurl.c_str() );
      }
      return mFallback.get();
    }

    std::shared_ptr<ResourceEntry> mEntry;
    std::mutex mHandleMutex;
    std::uint64_t mGeneration;
    std::uint64_t mPosition = 0;
    std::uint64_t mSize = 0;
    bool mSizeKnown = false;
    bool mEof = false;
    bool mError = false;
    VSIVirtualHandleUniquePtr mFallback;
};

// ---------------------------------------------------------------------------
// RangeCacheFilesystemHandler — the /vsirangecache/ prefix.
// ---------------------------------------------------------------------------

class RangeCacheFilesystemHandler final : public VSIFilesystemHandler
{
  public:
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 12, 0 )
    VSIVirtualHandleUniquePtr Open( const char *pszFilename, const char *pszAccess,
                                    bool bSetError, CSLConstList ) override
#else
    VSIVirtualHandle *Open( const char *pszFilename, const char *pszAccess,
                            bool bSetError, CSLConstList ) override
#endif
    {
      ( void ) bSetError;
      if ( std::strchr( pszAccess, 'w' ) != nullptr || std::strchr( pszAccess, '+' ) != nullptr )
      {
        CPLError( CE_Failure, CPLE_NotSupported, "vsirangecache: read-only filesystem" );
        return nullptr;
      }
      std::string requestUrl;
      std::string reason;
      if ( !underlyingUrl( pszFilename, requestUrl, reason ) )
      {
        CPLError( CE_Failure, CPLE_AppDefined, "vsirangecache: not a remote http(s) resource: %s",
                  reason.c_str() );
        return nullptr;
      }

      CacheStore &cache = store();
      const std::string key = resourceKey( requestUrl );

      // Identity: probe once, then revalidate per the declared policy. A
      // validator mismatch invalidates the resource's cached bytes.
      std::shared_ptr<ResourceEntry> entry = cache.findResource( key );
      if ( entry != nullptr && cache.config.stalePolicy == RangeCacheStalePolicy::RevalidateOnOpen )
      {
        // Revalidate against the ENTRY'S stored validators — a fresh probe
        // would always compare equal to itself and never see a change.
        cache.revalidations.fetch_add( 1 );
        RemoteSourceValidator validator = RemoteSourceValidator::fromIdentity(
          cache.snapshotIdentity( entry ), requestUrl );
        const RevalidationResult result = validator.revalidate( validatorOptions( cache.config ) );
        if ( result.outcome == RevalidationOutcome::Changed )
        {
          cache.invalidate( key );
          entry = cache.probeNewEntry( requestUrl, validatorOptions( cache.config ) );
          if ( entry == nullptr )
            return nullptr;
        }
        else if ( result.outcome == RevalidationOutcome::Unchanged )
        {
          cache.updateEntrySize( entry, validator.identity().sizeBytes );
        }
        // Inconclusive (offline, size-only origins): keep serving — this is
        // the caller's declared trust level, and the revalidation attempt is
        // visible in telemetry.
      }
      if ( entry == nullptr )
      {
        entry = cache.probeNewEntry( requestUrl, validatorOptions( cache.config ) );
        if ( entry == nullptr )
        {
          // Missing (ENOENT) or unreachable: a quiet null like any local
          // filesystem would answer for a path that does not exist — GDAL's
          // speculative sibling opens depend on this staying silent, and the
          // probe's own CPL error text must not leak into the caller's open.
          CPLErrorReset();
          errno = ENOENT;
          return nullptr;
        }
      }

#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 12, 0 )
      return VSIVirtualHandleUniquePtr( new RangeCacheHandle( entry ) );
#else
      return new RangeCacheHandle( entry );
#endif
    }

    int Stat( const char *pszFilename, VSIStatBufL *pStatBuf, int ) override
    {
      std::memset( pStatBuf, 0, sizeof( VSIStatBufL ) );
      std::string requestUrl;
      std::string reason;
      if ( !underlyingUrl( pszFilename, requestUrl, reason ) )
        return -1;
      const std::string key = resourceKey( requestUrl );
      CacheStore &cache = store();
      std::shared_ptr<ResourceEntry> entry = cache.findResource( key );
      if ( entry == nullptr || !entry->hasSize )
      {
        // A bounded identity probe (never a download) to learn the size.
        RemoteSourceValidator validator =
          RemoteSourceValidator::probe( requestUrl, validatorOptions( cache.config ) );
        const RemoteSourceIdentity &identity = validator.identity();
        // Gone / unusable / unreachable: a quiet stat miss — speculative
        // sibling probes (\.aux, \.ovr, …) are normal GDAL behavior and
        // must stay silent, never pollute the CPL error state.
        if ( identity.state == RemoteSourceState::Offline ||
             ( identity.state == RemoteSourceState::Unknown && !identity.hasSize ) )
        {
          CPLErrorReset();
          errno = ENOENT;
          return -1;
        }
        entry = cache.getOrCreateResource( key, requestUrl, identity );
      }
      if ( !entry->hasSize )
      {
        // Range-ignoring oversized origin: learn the size from the
        // underlying handle (open + Seek END — no content transfer).
        VSIVirtualHandleUniquePtr underlying =
          openReadonlyVsi( ( std::string( "/vsicurl/" ) + requestUrl ).c_str() );
        if ( underlying == nullptr )
        {
          errno = ENOENT;
          return -1;
        }
        underlying->Seek( 0, SEEK_END );
        cache.updateEntrySize( entry, underlying->Tell() );
      }
      pStatBuf->st_size = static_cast<decltype( pStatBuf->st_size )>( entry->sizeBytes );
      pStatBuf->st_mode = S_IFREG;
      return 0;
    }
};

} // namespace

/// Heap-allocated when installed, owned by GDAL from InstallHandler() onward:
/// VSIFileManager::RemoveHandler() DELETES the registered handler, so this
/// must never be static storage (a static would be deleted by GDAL and then
/// destroyed again at process exit — use-after-free + double free) and must
/// never be deleted here. Re-install cycles lazily allocate a fresh handler.
/// (Function-local in the accessor below; the pointer lives process-global.)
namespace
{
RangeCacheFilesystemHandler *&cachedHandler()
{
  static RangeCacheFilesystemHandler *s_handler = nullptr;
  return s_handler;
}
}

const char *rangeCacheStalePolicyName( RangeCacheStalePolicy policy )
{
  switch ( policy )
  {
    case RangeCacheStalePolicy::RevalidateOnOpen: return "revalidate_on_open";
    case RangeCacheStalePolicy::ValidateOnce: return "validate_once";
    case RangeCacheStalePolicy::TrustForever: return "trust_forever";
  }
  return "revalidate_on_open";
}

RangeCacheStalePolicy rangeCacheStalePolicyFromName( const std::string &name )
{
  if ( name == "revalidate_on_open" ) return RangeCacheStalePolicy::RevalidateOnOpen;
  if ( name == "validate_once" ) return RangeCacheStalePolicy::ValidateOnce;
  if ( name == "trust_forever" ) return RangeCacheStalePolicy::TrustForever;
  Json::Value details;
  details["name"] = name;
  throw GeoError( ErrorCode::InvalidArgument, "rangeCacheStalePolicyFromName: unknown policy", details );
}

Json::Value RangeCacheConfig::toJson() const
{
  Json::Value json;
  json["max_cache_bytes"] = static_cast<Json::UInt64>( maxCacheBytes );
  json["max_single_fetch_bytes"] = static_cast<Json::UInt64>( maxSingleFetchBytes );
  json["block_size"] = static_cast<Json::UInt64>( blockSize );
  json["stale_policy"] = rangeCacheStalePolicyName( stalePolicy );
  json["timeout_seconds"] = timeoutSeconds;
  json["connect_timeout_seconds"] = connectTimeoutSeconds;
  json["max_retries"] = maxRetries;
  return json;
}

Json::Value RangeCacheTelemetry::toJson() const
{
  Json::Value json;
  json["hits"] = static_cast<Json::UInt64>( hits );
  json["misses"] = static_cast<Json::UInt64>( misses );
  json["bytes_served"] = static_cast<Json::UInt64>( bytesServed );
  json["bytes_fetched"] = static_cast<Json::UInt64>( bytesFetched );
  json["coalesced_fetches"] = static_cast<Json::UInt64>( coalescedFetches );
  json["evictions"] = static_cast<Json::UInt64>( evictions );
  json["invalidations"] = static_cast<Json::UInt64>( invalidations );
  json["fallback_reads"] = static_cast<Json::UInt64>( fallbackReads );
  json["revalidations"] = static_cast<Json::UInt64>( revalidations );
  return json;
}

void RemoteRangeCache::install( const RangeCacheConfig &config )
{
  if ( config.blockSize == 0 || config.maxCacheBytes == 0 )
    throw GeoError( ErrorCode::InvalidArgument, "RemoteRangeCache: blockSize and budget must be positive" );
  {
    std::lock_guard<std::mutex> lock( g_storeLifecycleMutex );
    if ( !g_store )
      g_store = std::make_unique<CacheStore>();
    g_store->updateConfig( config ); // blockSize change drops entries
    if ( !s_handlerInstalled )
    {
      // Register exactly once per install cycle: re-registering while
      // installed would hand GDAL a second (or deleted) handler instance.
      // GDAL owns the handler from here on (RemoveHandler deletes it).
      cachedHandler() = new RangeCacheFilesystemHandler();
      VSIFileManager::InstallHandler( kRangeCachePrefix, cachedHandler() );
      s_handlerInstalled = true;
    }
  }
  ensureGdalRegistered();
}

void RemoteRangeCache::uninstall()
{
  std::lock_guard<std::mutex> lock( g_storeLifecycleMutex );
  if ( !g_store )
    return;
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION( 3, 9, 0 )
  VSIFileManager::RemoveHandler( kRangeCachePrefix );
  // GDAL deleted the handler above — forget the pointer (a later install()
  // lazily allocates a fresh one; touching the old pointer would be a
  // use-after-free).
  cachedHandler() = nullptr;
#endif
  g_store->dropAll();
  s_handlerInstalled = false;
}

bool RemoteRangeCache::installed()
{
  std::lock_guard<std::mutex> lock( g_storeLifecycleMutex );
  return s_handlerInstalled;
}

void RemoteRangeCache::clearEntries()
{
  std::lock_guard<std::mutex> lock( g_storeLifecycleMutex );
  if ( g_store )
    g_store->dropAll();
}

void RemoteRangeCache::invalidateResource( const std::string &url )
{
  std::lock_guard<std::mutex> lock( g_storeLifecycleMutex );
  if ( !g_store )
    return;
  std::string requestUrl;
  std::string reason;
  if ( !underlyingUrl( url, requestUrl, reason ) )
    throw GeoError( ErrorCode::InvalidArgument, "RemoteRangeCache: not a remote resource: " + reason );
  g_store->invalidate( resourceKey( requestUrl ) );
}

Json::Value RemoteRangeCache::telemetryJson()
{
  std::lock_guard<std::mutex> lock( g_storeLifecycleMutex );
  if ( !g_store )
    return Json::Value();
  RangeCacheTelemetry telemetry;
  telemetry.hits = g_store->hits.load();
  telemetry.misses = g_store->misses.load();
  telemetry.bytesServed = g_store->bytesServed.load();
  telemetry.bytesFetched = g_store->bytesFetched.load();
  telemetry.coalescedFetches = g_store->coalescedFetches.load();
  telemetry.evictions = g_store->evictions.load();
  telemetry.invalidations = g_store->invalidations.load();
  telemetry.fallbackReads = g_store->fallbackReads.load();
  telemetry.revalidations = g_store->revalidations.load();
  Json::Value json = telemetry.toJson();
  json["cached_bytes"] = static_cast<Json::UInt64>( g_store->cachedBytes() );
  json["config"] = g_store->config.toJson();
  return json;
}

RangeCacheConfig RemoteRangeCache::currentConfig()
{
  std::lock_guard<std::mutex> lock( g_storeLifecycleMutex );
  return g_store ? g_store->config : RangeCacheConfig();
}

std::string RemoteRangeCache::cachedPath( const std::string &url )
{
  std::string requestUrl;
  std::string reason;
  if ( !underlyingUrl( url, requestUrl, reason ) )
    throw GeoError( ErrorCode::InvalidArgument, "RemoteRangeCache: not a remote resource: " + reason );
  return std::string( kRangeCachePrefix ) + requestUrl;
}

bool RemoteRangeCache::isCachePath( const std::string &path )
{
  return path.rfind( kRangeCachePrefix, 0 ) == 0;
}

} // namespace sicnu::geo

/***************************************************************************
  geospatial/remote/range_cache.cpp — bounded LRU byte-range VSI cache.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Locking design (no lock held across network I/O unless it must be):
    * the store mutex guards the block index, global LRU list, byte
      accounting and telemetry — operations are O(1) and short.
    * each resource carries its own fetch mutex + in-progress flag: the
      reader that misses owns the coalesced ranged GET, but RELEASES the
      mutex for the entire transfer (and every retry backoff) so the
      per-resource lock never spans the retry budget. Concurrent readers of
      the same missing range wait on fetchCv and then hit the freshly-filled
      blocks (request dedup); unrelated resources fetch in parallel.
    * a read that crosses an invalidation restarts against the new
      generation (bounded restarts) — a reader never receives a blend of
      two content versions within one Read call.
    * the fallback path (direct /vsicurl/ handle) is per-handle state.
 ***************************************************************************/

#include "geospatial/remote/range_cache.h"

#include "geospatial/gdal_guard.h"
#include "geospatial/remote/http_fetch.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/range_cache_disk.h"
#include "geospatial/remote/remote_source_validator.h"
#include "geospatial/remote/vsi_object_identity.h"
#include "geospatial/util/gdal_compat.h"
#include "geospatial/util/resource_uri.h"

#include <cpl_conv.h>
#include <cpl_vsi.h>
#include <cpl_vsi_virtual.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sicnu::geo
{

namespace
{

// GDAL VSI API breakpoints are named in geospatial/util/gdal_compat.h —
// the version ladder below asks the compat seam instead of re-deriving
// GDAL_VERSION_NUM breakpoints per TU.
inline VSIVirtualHandleUniquePtr openReadonlyVsi( const char *path )
{
#if SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR
  return VSIFilesystemHandler::OpenStatic( path, "rb" );
#else
  return VSIVirtualHandleUniquePtr( VSIFOpenL( path, "rb" ) );
#endif
}


constexpr int kMaxGenerationRestarts = 8;

bool startsWithHttpScheme( const std::string &text )
{
  return text.rfind( "http://", 0 ) == 0 || text.rfind( "https://", 0 ) == 0;
}

/// Splits a cache-path/URL into the underlying remote request URL.
/// \a vsiPathOut (when non-null) receives the payload VSI spelling for
/// network-VSI payloads ("/vsis3/bucket/key"), "" otherwise — the 11.0
/// object-store mode (D-1102) reads through that spelling instead of the
/// http fetcher.
bool underlyingUrl( const std::string &path, std::string &requestUrl, std::string &reason,
                    std::string *vsiPathOut = nullptr )
{
  if ( vsiPathOut != nullptr )
    vsiPathOut->clear();
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
  if ( uri.kind == ResourceKind::VsiRemote && !startsWithHttpScheme( uri.remoteUrl() ) )
  {
    // Object-store (and any other network-VSI) payload: the FULL spelling
    // is both the request identity and the handle GDAL opens — remoteUrl()
    // would drop the /vsis3//vsigs//vsiaz prefix and compose an unopenable
    // path (the 10.0 M-R2 defect, restated).
    requestUrl = payload;
    if ( vsiPathOut != nullptr )
      *vsiPathOut = payload;
    return true;
  }
  requestUrl = uri.kind == ResourceKind::RemoteHttp ? uri.canonical() : uri.remoteUrl();
  return true;
}

/// Lowercase ASCII helper (host / scheme already lower in parse for scheme).
std::string asciiLower( const std::string &text )
{
  std::string out = text;
  for ( char &c : out )
    c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
  return out;
}

/// Uppercase percent-encoding hex digits ("%2f" → "%2F") so equivalent
/// encodings collapse under one cache key without decoding reserved bytes.
std::string normalizePercentHex( const std::string &text )
{
  std::string out = text;
  for ( std::size_t i = 0; i + 2 < out.size(); ++i )
  {
    if ( out[i] != '%' )
      continue;
    out[i + 1] = static_cast<char>( std::toupper( static_cast<unsigned char>( out[i + 1] ) ) );
    out[i + 2] = static_cast<char>( std::toupper( static_cast<unsigned char>( out[i + 2] ) ) );
    i += 2;
  }
  return out;
}

/// Sort query pairs by key then value so "?b=2&a=1" and "?a=1&b=2" share a key.
/// Pairs without '=' stay bare keys; pairs with '=' keep the equals even when
/// the value is empty ("flag" vs "flag=" stay distinct after sorting).
std::string canonicalizeQueryOrder( const std::string &query )
{
  if ( query.empty() )
    return query;
  struct Pair
  {
      std::string key;
      std::string value;
      bool hasEquals = false;
      bool operator<( const Pair &other ) const
      {
        if ( key != other.key )
          return key < other.key;
        if ( hasEquals != other.hasEquals )
          return !hasEquals && other.hasEquals;
        return value < other.value;
      }
  };
  std::vector<Pair> pairs;
  std::size_t start = 0;
  while ( start <= query.size() )
  {
    const std::size_t end = query.find( '&', start );
    const std::string piece =
      query.substr( start, end == std::string::npos ? std::string::npos : end - start );
    if ( !piece.empty() )
    {
      Pair pair;
      const std::size_t eq = piece.find( '=' );
      if ( eq == std::string::npos )
      {
        pair.key = piece;
      }
      else
      {
        pair.key = piece.substr( 0, eq );
        pair.value = piece.substr( eq + 1 );
        pair.hasEquals = true;
      }
      pairs.push_back( std::move( pair ) );
    }
    if ( end == std::string::npos )
      break;
    start = end + 1;
  }
  std::sort( pairs.begin(), pairs.end() );
  std::string out;
  for ( const Pair &pair : pairs )
  {
    if ( !out.empty() )
      out.push_back( '&' );
    out += pair.key;
    if ( pair.hasEquals )
    {
      out.push_back( '=' );
      out += pair.value;
    }
  }
  return out;
}

/// Cache-identity spelling for http(s): lowercased host, stripped default
/// ports, percent-hex normalized path, sorted query. Fragment is omitted
/// (never affects ranged bytes). Signed-URL query VALUES stay intact so
/// distinct signatures never collapse; only pair ORDER is normalized.
std::string httpCacheIdentityKey( const ResourceUri &uri )
{
  std::string authority = asciiLower( uri.host );
  std::string hostname = authority;
  std::string port;
  if ( !authority.empty() && authority.front() == '[' )
  {
    const std::size_t rb = authority.find( ']' );
    if ( rb != std::string::npos )
    {
      hostname = authority.substr( 0, rb + 1 );
      if ( rb + 1 < authority.size() && authority[rb + 1] == ':' )
        port = authority.substr( rb + 2 );
    }
  }
  else
  {
    const std::size_t colon = authority.rfind( ':' );
    // A single colon ⇒ host:port. Multiple colons without brackets ⇒ leave
    // as-is (malformed / rare); do not strip.
    if ( colon != std::string::npos && authority.find( ':' ) == colon )
    {
      hostname = authority.substr( 0, colon );
      port = authority.substr( colon + 1 );
    }
  }
  if ( ( uri.scheme == "https" && port == "443" ) || ( uri.scheme == "http" && port == "80" ) )
    port.clear();

  std::string path = normalizePercentHex( uri.path );
  if ( path.empty() )
    path = "/";
  const std::string query = canonicalizeQueryOrder( uri.query );

  std::string out = uri.scheme + "://";
  if ( !uri.userinfo.empty() )
    out += uri.userinfo + "@";
  out += hostname;
  if ( !port.empty() )
    out += ":" + port;
  out += path;
  if ( !query.empty() )
    out += "?" + query;
  return out;
}

std::string resourceKey( const std::string &requestUrl )
{
  const ResourceUri uri = ResourceUri::parse( requestUrl );
  if ( uri.kind == ResourceKind::RemoteHttp )
    return httpCacheIdentityKey( uri );
  return uri.remoteUrl();
}

/// 11.0 object-entry key (D-1102): canonical VSI spelling + the creating
/// window's credential-context fingerprint. Spellings canonicalize through
/// the fabric (s3a/s3c → /vsis3/) BEFORE wrapping, so one object has one
/// key per principal; the fingerprint keeps different accounts' (or a
/// session's vs. anonymous) blocks from ever being served to each other.
/// Neither component carries a secret.
std::string objectResourceKey( const std::string &vsiPath, const std::string &credentialContext )
{
  const ResourceUri uri = ResourceUri::parse( vsiPath );
  std::string key = uri.canonical();
  if ( !credentialContext.empty() )
    key += "|" + credentialContext;
  return key;
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
  std::string requestUrl;             // canonical remote http(s) URL (object
                                      // entries: canonical VSI spelling)
  RemoteSourceIdentity identity;
  std::uint64_t generation = 0;       // bumped on invalidation (drops blocks)
  std::unordered_map<std::uint64_t, std::list<CachedBlock>::iterator> blocks;
  std::list<CachedBlock> lru;         // front = most recently used
  std::uint64_t bytesCached = 0;      // 12.0: sum of this entry's block bytes
                                      // (per-resource budget basis)
  std::mutex fetchMutex;
  std::condition_variable fetchCv; // waiters wake when an owner finishes
  bool fetchInProgress = false;    // owner holds origin transfer; mutex released
  std::uint64_t sizeBytes = 0;
  bool hasSize = false;
  // 12.0 TTL basis: the steady-clock millisecond stamp of the last
  // successful identity proof (creation or fresh probe). Atomic: Open
  // reads it for expiry while a probe may refresh it concurrently.
  std::atomic<std::int64_t> provenAtMs{ 0 };
  // 12.0 fetch cancellation: when set, in-flight fetch results are
  // discarded and new fetches degrade to the fallback (see cancelFetches).
  std::atomic<bool> fetchCancelled{ false };
  // 11.0 object-store mode (D-1102): the payload is a network VSI object —
  // fetched through the VSI stack under the live credential window (GDAL
  // signs), with the fallback re-opening that same spelling.
  bool vsiObject = false;
  std::string vsiPath;                // original "/vsis3/…" spelling
  std::string credentialContext;      // creating window's principal fingerprint
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
    std::atomic<std::uint64_t> dedupHits{ 0 };
    std::atomic<std::uint64_t> maxInFlightBytes{ 0 };
    std::atomic<std::uint64_t> diskHits{ 0 };
    std::atomic<std::uint64_t> retriedFetches{ 0 };
    std::atomic<std::uint64_t> maxCachedBytes{ 0 };
    // 13.0 maintenance telemetry (see RangeCacheTelemetry).
    std::atomic<std::uint64_t> fetchAttempts{ 0 };
    std::atomic<std::uint64_t> backoffWaits{ 0 };
    std::atomic<std::uint64_t> admissionWaits{ 0 };
    std::atomic<std::uint64_t> cancelledFetches{ 0 };
    std::atomic<std::uint64_t> ttlExpirations{ 0 };
    std::atomic<std::uint64_t> ttlRefreshes{ 0 };

    // ── 9.0 global fetch admission ─────────────────────────────────────────
    // Bounds the bytes concurrently in flight across all ranged GETs. The
    // gate never holds the store mutex and never holds a resource's
    // fetchMutex ACROSS the wait of a different resource — a waiter blocks
    // only until other resources' fetches drain. A request larger than the
    // cap is admitted when NOTHING else is in flight (head-of-line). Under
    // sustained small-fetch load an over-cap fetch can still be starved by
    // barging new arrivals — see RangeCacheConfig::maxConcurrentFetchBytes.
    // 13.0: the admission scope is ONE ORIGIN TRANSFER (one attempt of a
    // ranged GET) — a retry's backoff sleep runs with the slot released,
    // so the cap bounds bytes on the wire, never patience.
    void admitFetch( std::uint64_t requestedBytes, std::uint64_t cap )
    {
      if ( cap == 0 )
        return; // unlimited
      std::unique_lock<std::mutex> lock( mAdmissionMutex );
      bool waited = false;
      while ( mInFlightBytes > 0 && mInFlightBytes + requestedBytes > cap )
      {
        waited = true;
        mAdmissionCv.wait( lock );
      }
      if ( waited )
        admissionWaits.fetch_add( 1 );
      mInFlightBytes += requestedBytes;
      // Track the observed peak against the declared bound.
      std::uint64_t current = mInFlightBytes;
      std::uint64_t peak = maxInFlightBytes.load();
      while ( current > peak && !maxInFlightBytes.compare_exchange_weak( peak, current ) )
      {
      }
    }

    void completeFetch( std::uint64_t admittedBytes, std::uint64_t cap )
    {
      if ( cap == 0 )
        return;
      {
        std::lock_guard<std::mutex> lock( mAdmissionMutex );
        mInFlightBytes -= std::min( admittedBytes, mInFlightBytes );
      }
      mAdmissionCv.notify_all();
    }

    std::uint64_t inFlightBytes()
    {
      std::lock_guard<std::mutex> lock( mAdmissionMutex );
      return mInFlightBytes;
    }

    static std::int64_t steadyNowMs()
    {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch() ).count();
    }

    /// True when the entry's identity proof is older than the TTL (seconds;
    /// 0 = never expires). Read under the store lock for a consistent
    /// decision against a concurrent re-probe.
    /// 12.0: refreshes the TTL basis after a successful revalidation (13.0:
    /// counted — the refresh rate is the observable cost of the stat/read
    /// trust horizon).
    void touchEntryProven( const std::shared_ptr<ResourceEntry> &entry )
    {
      entry->provenAtMs.store( steadyNowMs() );
      ttlRefreshes.fetch_add( 1 );
    }

    bool entryExpired( const std::shared_ptr<ResourceEntry> &entry, int ttlSeconds )
    {
      if ( ttlSeconds <= 0 )
        return false;
      std::lock_guard<std::mutex> lock( mMutex );
      const std::int64_t ageMs = steadyNowMs() - entry->provenAtMs.load();
      const bool expired = ageMs > static_cast<std::int64_t>( ttlSeconds ) * 1000;
      if ( expired )
        ttlExpirations.fetch_add( 1 );
      return expired;
    }

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
        // A veto recorded before the first open survives creation.
        entry->fetchCancelled.store( mCancelledFetches.count( key ) > 0 );
      }
      // A fresh probe describes the CURRENT origin state: always refresh the
      // stored identity (an invalidated entry keeps its old validators
      // otherwise, and every future revalidation sees a phantom mismatch).
      entry->identity = identity;
      entry->hasSize = identity.hasSize;
      entry->sizeBytes = identity.sizeBytes;
      entry->provenAtMs.store( steadyNowMs() );
      return entry;
    }

    /// 11.0 object-entry creator (D-1102): identical discipline to
    /// getOrCreateResource plus the VSI spelling and creating principal.
    std::shared_ptr<ResourceEntry> getOrCreateVsiObject( const std::string &key,
                                                         const std::string &vsiPath,
                                                         const std::string &credentialContext,
                                                         const RemoteSourceIdentity &identity )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      auto &entry = mResources[key];
      if ( !entry )
      {
        entry = std::make_shared<ResourceEntry>();
        entry->requestUrl = identity.url;   // redacted display form
        entry->vsiObject = true;
        entry->vsiPath = vsiPath;
        entry->credentialContext = credentialContext;
        entry->fetchCancelled.store( mCancelledFetches.count( key ) > 0 );
      }
      entry->identity = identity;
      entry->hasSize = identity.hasSize;
      entry->sizeBytes = identity.sizeBytes;
      entry->provenAtMs.store( steadyNowMs() );
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

    /// 12.0 fetch-cancel veto (see RemoteRangeCache::cancelFetches). The
    /// veto is recorded per KEY (not per entry object): cancelling before
    /// the resource exists seeds the set, and every entry created later
    /// inherits it. Unknown resources are a no-op only for resume.
    void setFetchCancelled( const std::string &key, bool cancelled )
    {
      {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( cancelled )
        {
          mCancelledFetches.insert( key );
          const auto it = mResources.find( key );
          if ( it != mResources.end() )
            it->second->fetchCancelled.store( true );
        }
        else
        {
          mCancelledFetches.erase( key );
          const auto it = mResources.find( key );
          if ( it != mResources.end() )
            it->second->fetchCancelled.store( false );
        }
      }
      if ( cancelled )
      {
        // 13.0: wake every backoff sleeper — each rechecks its own entry's
        // flag. The backoff mutex is held across the notify so a sleeper's
        // predicate-check → wait transition cannot miss the wake (the flag
        // store above already happened; the notify is what it waits for).
        std::lock_guard<std::mutex> lock( mBackoffMutex );
        mBackoffCv.notify_all();
      }
    }

    bool fetchCancelled( const std::shared_ptr<ResourceEntry> &entry )
    {
      // The flag lives on the entry (shared ownership keeps it alive); the
      // store lock is not needed for an atomic load.
      return entry->fetchCancelled.load();
    }

    /// 13.0 retry backoff: sleeps up to \a delayMs with NO admission slot
    /// held, waking early when the entry's cancel veto lands. Returns true
    /// when the veto fired (the caller aborts the retry into the fallback).
    /// One CV serves every entry: a cancel of any resource wakes all
    /// sleepers and each rechecks its own flag — spurious wakes are cheap
    /// and rare (sleeps are short, cancels rarer).
    bool waitBackoffOrCancelled( const std::shared_ptr<ResourceEntry> &entry,
                                 std::uint64_t delayMs )
    {
      if ( entry->fetchCancelled.load() )
        return true;
      std::unique_lock<std::mutex> lock( mBackoffMutex );
      return mBackoffCv.wait_for( lock, std::chrono::milliseconds( delayMs ),
                                  [ &entry ] { return entry->fetchCancelled.load(); } );
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
                      std::uint64_t fetchConfigGeneration, std::uint64_t expectedGeneration,
                      std::uint64_t maxCacheBytes, std::uint64_t maxBytesPerResource )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      if ( fetchConfigGeneration != configGeneration )
        return; // the config changed mid-fetch: these bytes cannot be indexed safely
      if ( entry->generation != expectedGeneration )
        return; // 9.0 review: the resource was invalidated mid-fetch — these are
                // OLD-CONTENT bytes and must never enter the fresh generation
      mMaxCacheBytes = maxCacheBytes;
      mMaxBytesPerResource = maxBytesPerResource;
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
      evictUnderBudget( entry );
    }

    /// 9.0 M3 — disk layer: the content-identity basis of a resource, or ""
    /// when the layer is disabled or the identity is unprovable (fail-closed:
    /// unprovable identity is never disk-cached).
    std::string diskBasis( const std::shared_ptr<ResourceEntry> &entry )
    {
      if ( !RangeDiskBlockStore::enabled() )
        return std::string();
      const RemoteSourceIdentity identity = snapshotIdentity( entry );
      return RangeDiskBlockStore::identityBasis( entry->requestUrl, identity.validator.hasStrongEtag(),
                                                 identity.validator.etag, identity.hasSize,
                                                 identity.sizeBytes, identity.validator.lastModified );
    }

    /// Fills missing blocks of [firstBlock,lastBlock] from the checksummed
    /// disk layer into the memory store (generation-checked per block).
    /// Disk reads happen OUTSIDE the store lock; only the per-block insert
    /// takes it (short). Returns true when at least one block landed.
    bool loadBlocksFromDisk( const std::shared_ptr<ResourceEntry> &entry, std::uint64_t firstBlock,
                             std::uint64_t lastBlock, std::uint64_t expectedGeneration,
                             std::uint64_t blockSize, std::uint64_t fetchConfigGeneration,
                             std::uint64_t maxCacheBytes, std::uint64_t maxBytesPerResource )
    {
      const std::string basis = diskBasis( entry );
      if ( basis.empty() )
        return false;
      std::vector<std::uint64_t> missing;
      {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( entry->generation != expectedGeneration )
          return false;
        // Install the fetch's config snapshot (same discipline as
        // insertBytes): the eviction below must run against the caps the
        // caller's snapshot declared, not a stale store value.
        mMaxCacheBytes = maxCacheBytes;
        mMaxBytesPerResource = maxBytesPerResource;
        for ( std::uint64_t b = firstBlock; b <= lastBlock; ++b )
          if ( entry->blocks.find( b ) == entry->blocks.end() )
            missing.push_back( b );
      }
      bool loaded = false;
      for ( const std::uint64_t blockIndex : missing )
      {
        std::vector<unsigned char> data;
        if ( !RangeDiskBlockStore::readBlock( basis, blockIndex, data ) )
          continue;
        {
          std::lock_guard<std::mutex> lock( mMutex );
          if ( fetchConfigGeneration != configGeneration || entry->generation != expectedGeneration )
            return loaded; // config/generation moved: stop feeding stale blocks
          insertOne( entry, blockIndex, data.data(), data.size() );
          evictUnderBudget( entry );
        }
        loaded = true;
      }
      return loaded;
    }

    /// Publishes a fetched run to the disk layer, block by block (called
    /// AFTER the memory insert — the run bytes stay alive in the caller).
    void putRunToDisk( const std::shared_ptr<ResourceEntry> &entry, std::uint64_t runStart,
                       const std::vector<unsigned char> &bytes, std::uint64_t blockSize,
                       std::uint64_t expectedGeneration )
    {
      // Capture generation AND identity under one lock hold. diskBasis()
      // re-locks via snapshotIdentity — calling it after releasing would
      // reopen a TOCTOU window where invalidate()+refresh swaps the ETag
      // and old-content bytes land under a new basis (checksum-valid poison).
      std::string basis;
      {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( entry->generation != expectedGeneration )
          return;
        if ( !RangeDiskBlockStore::enabled() )
          return;
        const RemoteSourceIdentity &identity = entry->identity;
        basis = RangeDiskBlockStore::identityBasis(
          entry->requestUrl, identity.validator.hasStrongEtag(), identity.validator.etag,
          identity.hasSize, identity.sizeBytes, identity.validator.lastModified );
      }
      if ( basis.empty() )
        return;
      std::uint64_t offsetInRun = 0;
      std::uint64_t blockIndex = runStart / blockSize;
      while ( offsetInRun < bytes.size() )
      {
        const std::size_t chunk = static_cast<std::size_t>(
          std::min<std::uint64_t>( blockSize, bytes.size() - offsetInRun ) );
        RangeDiskBlockStore::putBlock( basis, blockIndex, bytes.data() + offsetInRun, chunk );
        offsetInRun += chunk;
        blockIndex += 1;
      }
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
      // The high-water is a GAUGE of the store's content: when the content
      // is fully dropped, the observed peak resets with it (counters like
      // evictions stay lifetime; the peak describes the current population).
      maxCachedBytes.store( 0 );
    }

    void releaseBlocks( const std::shared_ptr<ResourceEntry> &entry )
    {
      for ( const CachedBlock &block : entry->lru )
      {
        mGlobalLru.erase( block.touch );
        mBytesCached -= block.data->size();
      }
      entry->bytesCached = 0;
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
        entry->bytesCached -= existing->second->data->size();
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
      entry->bytesCached += size;
      entry->lru.push_front( std::move( block ) );
      entry->blocks[blockIndex] = entry->lru.begin();
    }

    /// Evicts under the GLOBAL byte budget and (12.0) the per-resource cap
    /// of the entry that just grew. Assumes mMutex is held.
    void evictUnderBudget( const std::shared_ptr<ResourceEntry> &entry )
    {
      while ( mBytesCached > mMaxCacheBytes && !mGlobalLru.empty() )
      {
        const TouchEntry victim = mGlobalLru.back();
        const std::shared_ptr<ResourceEntry> victimEntry = victim.entry;
        const auto blockIt = victimEntry->blocks.find( victim.blockIndex );
        if ( blockIt == victimEntry->blocks.end() )
        {
          mGlobalLru.pop_back();
          continue;
        }
        mBytesCached -= blockIt->second->data->size();
        victimEntry->bytesCached -= blockIt->second->data->size();
        victimEntry->lru.erase( blockIt->second );
        victimEntry->blocks.erase( blockIt );
        mGlobalLru.pop_back();
        evictions.fetch_add( 1 );
      }
      // 12.0: one resource cannot hog the global budget — evict the
      // resource's OWN LRU tail while it is over its cap.
      if ( mMaxBytesPerResource > 0 )
      {
        while ( entry->bytesCached > mMaxBytesPerResource && !entry->lru.empty() )
        {
          const CachedBlock &victim = entry->lru.back();
          mBytesCached -= victim.data->size();
          entry->bytesCached -= victim.data->size();
          mGlobalLru.erase( victim.touch );
          entry->blocks.erase( victim.index );
          entry->lru.pop_back();
          evictions.fetch_add( 1 );
        }
      }
      // 12.0 high-water mark of total cached bytes (updated while the
      // insert's lock is still held, right after eviction settled).
      std::uint64_t peak = maxCachedBytes.load();
      while ( mBytesCached > peak && !maxCachedBytes.compare_exchange_weak( peak, mBytesCached ) )
      {
      }
    }

    std::mutex mMutex;
    std::map<std::string, std::shared_ptr<ResourceEntry>> mResources;
    /// 12.0: resources whose fetches are vetoed, keyed independently of the
    /// entry lifetime — a cancel that lands before the first open (or across
    /// a TTL/invalidate recreation) must survive entry churn.
    std::set<std::string> mCancelledFetches;
    std::list<TouchEntry> mGlobalLru;   // front = most recently used
    std::uint64_t mBytesCached = 0;
    std::uint64_t configGeneration = 1; // bumped on every config update
    std::uint64_t mMaxCacheBytes = 64ull * 1024 * 1024;
    std::uint64_t mMaxBytesPerResource = 0; // 12.0: 0 = unlimited

    std::mutex mAdmissionMutex;
    std::condition_variable mAdmissionCv;
    std::uint64_t mInFlightBytes = 0;

    // 13.0 cancel-aware backoff: a dedicated mutex for the sleep CV so the
    // notifier can hold it across notify_all() (closing the predicate-check
    // → wait race) without touching the store/admission lock order.
    std::mutex mBackoffMutex;
    std::condition_variable mBackoffCv;
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

/// RAII for the global in-flight byte admission (9.0 gate). The slot
/// covers exactly ONE origin transfer — a retry's backoff sleep runs with
/// nothing held so other fetches can be admitted (13.0 D-1301).
struct InFlightAdmission
{
  CacheStore &cache;
  std::uint64_t bytes;
  std::uint64_t cap;
  InFlightAdmission( CacheStore &c, std::uint64_t b, std::uint64_t cp )
    : cache( c ), bytes( b ), cap( cp )
  {
    cache.admitFetch( bytes, cap );
  }
  ~InFlightAdmission() { cache.completeFetch( bytes, cap ); }
  InFlightAdmission( const InFlightAdmission & ) = delete;
  InFlightAdmission &operator=( const InFlightAdmission & ) = delete;
};

/// Coalesced ranged fetch of [start,end) — a SINGLE attempt. Returns the
/// bytes actually read (may be shorter at EOF). Throws GeoError on
/// transport failure. (Retry/backoff lives in fetchRange.)
std::vector<unsigned char> fetchRangeOnce( const std::string &requestUrl, std::uint64_t start,
                                           std::uint64_t endExclusive,
                                           const RangeCacheConfig &config )
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
    // A 206 must echo the window it actually serves ("bytes S-E/total").
    // Anything else — a wrong offset, an unparseable range — must never
    // enter the cache as if it were [start,end): a hostile or broken origin
    // would poison every later reader.
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
    if ( parseable )
    {
      // The echoed window must start exactly where the request started and
      // never reach past the requested window.
      if ( echoedStart != start || echoedEnd + 1 > endExclusive )
        throw GeoError( ErrorCode::Unsupported,
                        "range_cache: origin echoed a mismatched Content-Range window" );
      // 9.0 truncation gate: the body must carry the WHOLE echoed window.
      // The only honest shortfall is EOF — an honest origin echoes the
      // CLAMPED window ("bytes S-E/total" with E below the requested end),
      // so the echoed window itself is the completeness yardstick; a short
      // body against any echoed window is a torn transfer and must never be
      // served or cached.
      const std::uint64_t windowBytes = echoedEnd - echoedStart + 1;
      if ( result.body.size() < windowBytes )
      {
        Json::Value details;
        details["status"] = result.httpStatus;
        details["echoed_bytes"] = static_cast<Json::UInt64>( windowBytes );
        details["body_bytes"] = static_cast<Json::UInt64>( result.body.size() );
        throw GeoError( ErrorCode::NetworkError,
                        "range_cache: origin sent a truncated ranged response",
                        details );
      }
      // 9.0 review: an over-long body must not leak past the echoed window —
      // insertBytes indexes whatever it is handed, so bytes beyond the
      // window would land in block indexes that were never fetched. Slice
      // to exactly the echoed window.
      result.body.resize( static_cast<std::size_t>( windowBytes ) );
      return result.body; // verified window (EOF-clamped ends are fine)
    }
    // 206 without a parseable Content-Range must NEVER enter the cache as
    // an opaque slice at the requested offset (#1228 / #1186): a hostile or
    // broken origin would poison every later reader. Refuse and let the
    // caller fall back to a direct read.
    throw GeoError( ErrorCode::Unsupported,
                    "range_cache: 206 response missing a parseable Content-Range" );
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

/// Coalesced ranged fetch of [start,end) with the configured retry/backoff.
/// Only transport-shaped failures (NetworkError, Timeout) are retried — a
/// refused or unsupported answer would fail identically on every attempt.
/// The sleep is bounded (base << attempt, capped) and the attempt count is
/// clamped, so a hostile origin cannot pin a reader forever.
/// 13.0: the in-flight admission covers ONE attempt — it is released
/// BEFORE the backoff sleep, so the global byte cap bounds wire bytes and
/// a sleeping retry blocks nobody (fairness: re-admission queues through
/// the same gate). The sleep itself is cancel-aware: a veto that lands
/// during backoff wakes the retry promptly instead of waiting it out.
std::vector<unsigned char> fetchRange( const std::string &requestUrl, std::uint64_t start,
                                       std::uint64_t endExclusive, const RangeCacheConfig &config,
                                       CacheStore &cache,
                                       const std::shared_ptr<ResourceEntry> &entry )
{
  const int attempts =
    config.fetchAttempts < 1 ? 1 : ( config.fetchAttempts > 8 ? 8 : config.fetchAttempts );
  for ( int attempt = 1;; ++attempt )
  {
    // A veto that landed between attempts starts no new origin fetch —
    // the read degrades to the fallback exactly like a pre-fetch veto.
    if ( cache.fetchCancelled( entry ) )
    {
      cache.cancelledFetches.fetch_add( 1 );
      throw GeoError( ErrorCode::Cancelled,
                      "range_cache: fetch vetoed before an attempt" );
    }
    try
    {
      InFlightAdmission admission( cache, endExclusive - start,
                                   config.maxConcurrentFetchBytes );
      cache.fetchAttempts.fetch_add( 1 );
      return fetchRangeOnce( requestUrl, start, endExclusive, config );
    }
    catch ( const GeoError &error )
    {
      // Transport-shaped failures retry; refused answers do not. A >=400
      // carries its status in details — a 403/404/410/416 would fail
      // identically on every attempt, so only server-side/transient codes
      // (5xx, 429) deserve a second try.
      bool refused4xx = false;
      if ( error.details().isMember( "status" ) )
      {
        const Json::Value &status = error.details()["status"];
        refused4xx = status.isInt() && status.asInt() >= 400 && status.asInt() < 500 &&
                     status.asInt() != 429;
      }
      const bool retryable = ( error.code() == ErrorCode::NetworkError ||
                               error.code() == ErrorCode::Timeout ) &&
                             !refused4xx;
      if ( !retryable || attempt >= attempts )
        throw;
      cache.retriedFetches.fetch_add( 1 );
      const int baseMs = config.retryBackoffBaseMs > 0 ? config.retryBackoffBaseMs : 0;
      const int capMs = config.retryBackoffMaxMs > 0 ? config.retryBackoffMaxMs : 0;
      std::uint64_t delayMs = 0;
      if ( baseMs > 0 )
      {
        const int shift = attempt - 1 > 6 ? 6 : attempt - 1; // clamp the shift
        delayMs = static_cast<std::uint64_t>( baseMs ) << shift;
        if ( capMs > 0 && delayMs > static_cast<std::uint64_t>( capMs ) )
          delayMs = static_cast<std::uint64_t>( capMs );
      }
      if ( delayMs > 0 )
      {
        // The admission from the failed attempt is already released (its
        // RAII ended with the catch) — the sleep holds no origin slot.
        cache.backoffWaits.fetch_add( 1 );
        if ( cache.waitBackoffOrCancelled( entry, delayMs ) )
        {
          cache.cancelledFetches.fetch_add( 1 );
          throw GeoError( ErrorCode::Cancelled,
                          "range_cache: fetch cancelled during retry backoff" );
        }
      }
    }
  }
}

/// Builds the entry identity of an object probe from VSI-stack HEAD facts.
/// probed=false maps to Offline (the store never answered — nothing to
/// cache against); probed with no usable validator keeps Unknown so
/// revalidation treats it as inconclusive rather than "changed".
RemoteSourceIdentity vsiObjectIdentity( const std::string &vsiPath,
                                        const VsiObjectIdentityFacts &facts )
{
  RemoteSourceIdentity identity;
  identity.url = ResourceUri::parse( vsiPath ).display();   // redacted form
  identity.state = facts.probed ? RemoteSourceState::Fresh : RemoteSourceState::Offline;
  identity.validator.etag = facts.etag;
  identity.hasSize = facts.hasSize;
  identity.sizeBytes = facts.sizeBytes;
  return identity;
}

/// Ranged fetch of [start,end) from a network VSI object: open + seek +
/// read — GDAL signs every request and honors the ambient credential
/// window (the D-1003 serialization covers this exactly like any other
/// /vsi* open). The handle is opened per fetch: a cached handle would keep
/// serving its object version across invalidations, defeating generation
/// discipline. Short answers are EOF-honest exactly like the http path.
std::vector<unsigned char> fetchRangeVsi( const std::string &vsiPath, std::uint64_t start,
                                          std::uint64_t endExclusive )
{
  // Same offline contract as the http fetcher: a forced-offline process
  // never dispatches an origin read.
  if ( offline::enabled() && offline::isRemoteTarget( vsiPath ) )
    throw GeoError( ErrorCode::NetworkError, offline::refusalMessage( vsiPath ) );
  VSIVirtualHandleUniquePtr handle = openReadonlyVsi( vsiPath.c_str() );
  if ( handle == nullptr )
    throw GeoError( ErrorCode::NetworkError,
                    "range_cache: object open failed: " + ResourceUri::parse( vsiPath ).display() );
  if ( handle->Seek( start, SEEK_SET ) != 0 )   // vsi_l_offset: 64-bit — a
                                                // long cast truncates ≥2 GiB
    throw GeoError( ErrorCode::IoError,
                    "range_cache: object seek failed: " + ResourceUri::parse( vsiPath ).display() );
  std::vector<unsigned char> bytes( static_cast<std::size_t>( endExclusive - start ) );
  const std::size_t got = handle->Read( bytes.data(), 1, bytes.size() );
  if ( got == 0 && start != 0 )
    throw GeoError( ErrorCode::NetworkError,
                    "range_cache: object read refused the range: " +
                      ResourceUri::parse( vsiPath ).display() );
  bytes.resize( got );
  return bytes;
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

#if SICNU_GDAL_VSI_HANDLE_READ_BYTES
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

#if SICNU_GDAL_VSI_HANDLE_ERR_API
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

#if SICNU_GDAL_VSI_HANDLE_ERR_API
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

      // 9.0 M3: memory miss — try the checksummed disk layer before any
      // network work. Disk blocks are content-identity keyed; a hit fills
      // the memory blocks and serves without an origin request.
      if ( cache.loadBlocksFromDisk( mEntry, firstBlock, lastBlock, mGeneration, blockSize,
                                     fetchConfigGeneration, config.maxCacheBytes,
                                     config.maxBytesPerResource )
           && cache.tryServe( mEntry, position, length, destination, mGeneration, blockSize ) )
      {
        cache.hits.fetch_add( 1 );
        cache.diskHits.fetch_add( 1 );
        return length;
      }

      // Fetch ownership: waiters sleep on fetchCv while one thread owns the
      // origin transfer. The mutex is RELEASED for the entire transfer (and
      // every retry backoff) so the per-resource lock never spans the retry
      // budget (#1228 / #1186 item 32).
      std::unique_lock<std::mutex> fetchLock( mEntry->fetchMutex );
      while ( mEntry->fetchInProgress )
      {
        mEntry->fetchCv.wait( fetchLock );
        if ( mGeneration != cache.entryGeneration( mEntry ) )
        {
          mGeneration = cache.entryGeneration( mEntry );
          *restarted = true;
          return 0;
        }
        if ( cache.tryServe( mEntry, position, length, destination, mGeneration, blockSize ) )
        {
          cache.hits.fetch_add( 1 );
          cache.dedupHits.fetch_add( 1 );
          return length;
        }
      }
      if ( mGeneration != cache.entryGeneration( mEntry ) )
      {
        mGeneration = cache.entryGeneration( mEntry );
        *restarted = true;
        return 0;
      }
      if ( cache.tryServe( mEntry, position, length, destination, mGeneration, blockSize ) )
      {
        // A concurrent fetch of the same missing run just filled this in —
        // this read is the request-dedup outcome (no second origin request).
        cache.hits.fetch_add( 1 );
        cache.dedupHits.fetch_add( 1 );
        return length;
      }

      // Fetch from the block boundary so cached blocks stay aligned with
      // their index (a mid-block fetch would serve shifted bytes forever).
      const std::uint64_t fetchStart = ( position / blockSize ) * blockSize;
      const std::uint64_t fetchEnd = std::min<std::uint64_t>(
        mSize,
        std::min<std::uint64_t>( fetchStart + config.maxSingleFetchBytes, position + length ) );

      // 12.0 fetch-cancel veto: a cancelled resource starts no new origin
      // fetch — the reader degrades to the direct fallback (the correctness
      // gate), and no cancelled bytes can ever be published.
      if ( cache.fetchCancelled( mEntry ) )
      {
        cache.cancelledFetches.fetch_add( 1 );
        return fallbackRead( destination, position, fetchEnd - position );
      }

      mEntry->fetchInProgress = true;
      fetchLock.unlock();

      std::vector<unsigned char> bytes;
      bool transportFailed = false;
      try
      {
        // 13.0: the in-flight byte admission covers ONE origin transfer.
        // For the HTTP path the guard lives INSIDE fetchRange — per attempt,
        // released before every backoff sleep — so a retrying fetch holds no
        // origin slot while it waits (the byte cap bounds wire bytes, not
        // patience; retry fairness stays bounded by fetchAttempts).
        // The resource fetchMutex is also released here (see above).
        if ( mEntry->vsiObject )
        {
          // The VSI path has no retry loop: its single transfer admits here.
          InFlightAdmission admission( cache, fetchEnd - fetchStart,
                                       config.maxConcurrentFetchBytes );
          bytes = fetchRangeVsi( mEntry->vsiPath, fetchStart, fetchEnd );
        }
        else
        {
          bytes = fetchRange( mEntry->requestUrl, fetchStart, fetchEnd, config,
                              cache, mEntry );
        }
      }
      catch ( const GeoError & )
      {
        transportFailed = true;
      }

      fetchLock.lock();
      mEntry->fetchInProgress = false;
      mEntry->fetchCv.notify_all();

      if ( transportFailed )
      {
        // Failure fallback: the cache is never a correctness gate — read the
        // same bytes straight through /vsicurl/.
        return fallbackRead( destination, position, fetchEnd - position );
      }
      // Generation may have moved while the mutex was released.
      if ( mGeneration != cache.entryGeneration( mEntry ) )
      {
        mGeneration = cache.entryGeneration( mEntry );
        *restarted = true;
        return 0;
      }
      // 12.0: the fetch completed but the veto landed while it was in
      // flight — these bytes are DISCARDED (never inserted into the memory
      // store or the disk layer, never served, never counted as fetched —
      // the transfer's outcome was "cancelled", not "delivered"). The
      // fallback read answers this reader; a later resumeFetches()
      // re-enables caching honestly.
      if ( cache.fetchCancelled( mEntry ) )
      {
        cache.cancelledFetches.fetch_add( 1 );
        return fallbackRead( destination, position, fetchEnd - position );
      }
      if ( bytes.empty() )
        return fallbackRead( destination, position, fetchEnd - position );
      cache.coalescedFetches.fetch_add( 1 );
      cache.bytesFetched.fetch_add( bytes.size() );

      cache.insertBytes( mEntry, fetchStart, bytes, blockSize, fetchConfigGeneration,
                         mGeneration, config.maxCacheBytes, config.maxBytesPerResource );
      // 9.0 M3 write-through: publish the fetched run to the disk layer
      // (atomically, checksummed). A no-op when the layer is disabled or the
      // identity is unprovable, or when the resource was invalidated while
      // the fetch was in flight (stale bytes never land anywhere).
      cache.putRunToDisk( mEntry, fetchStart, bytes, blockSize, mGeneration );
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
        // Object entries re-open their own VSI spelling — GDAL signs the
        // fallback read under the live credential window. An http fallback
        // for an object path would be an UNAUTHENTICATED request (and for
        // "bucket/key" payloads not even a valid one) — that composition
        // is exactly what 10.0's M-R2 refused.
        if ( mEntry->vsiObject )
          mFallback = openReadonlyVsi( mEntry->vsiPath.c_str() );
        else
          mFallback = openReadonlyVsi( ( std::string( "/vsicurl/" ) + mEntry->requestUrl ).c_str() );
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
// applyTrustPolicy — the shared Open/Stat trust horizon (13.0).
// ---------------------------------------------------------------------------
//
// One implementation answers both "open for read" and "stat for metadata",
// so a cached entry can never be fresh enough to read yet stale enough to
// misreport. Ordered contract:
//   1. TTL: an entry past entryTtlSeconds is dropped first — the declared
//      trust horizon applies regardless of the stale policy, and the
//      caller re-proves the resource from scratch (fresh identity, fresh
//      provenAtMs basis).
//   2. Stale policy: RevalidateOnOpen re-proves against the ORIGIN (a VSI
//      HEAD for object entries, a conditional request for http) — Changed
//      invalidates and returns nullptr so the caller's normal probe path
//      re-proves uniformly; Unchanged refreshes the size and the TTL
//      basis; Inconclusive (offline, weak validators) keeps the entry —
//      the caller's declared trust level. ValidateOnce / TrustForever
//      add no origin traffic at all.
// The Unchanged/Inconclusive arms cost at most ONE identity request per
// call; a proven-Changed entry additionally re-probes once through the
// caller's normal no-entry path — no storm beyond what a cold Open paid
// (D-1303).
std::shared_ptr<ResourceEntry> applyTrustPolicy( CacheStore &cache, const std::string &key,
                                                 const std::string &requestUrl,
                                                 const std::string &vsiPath,
                                                 const std::string &credentialContext,
                                                 const RangeCacheConfig &config )
{
  std::shared_ptr<ResourceEntry> entry = cache.findResource( key );
  if ( entry != nullptr && cache.entryExpired( entry, config.entryTtlSeconds ) )
  {
    cache.invalidate( key );
    entry = nullptr;
  }
  if ( entry == nullptr || config.stalePolicy != RangeCacheStalePolicy::RevalidateOnOpen )
    return entry;

  if ( !vsiPath.empty() )
  {
    // Object entries revalidate through the same VSI-stack HEAD that
    // created them: a strong-ETag mismatch drops the blocks, an
    // unprovable answer (offline, weak) is inconclusive and keeps
    // serving — the caller's declared trust level.
    const VsiObjectIdentityFacts facts = probeVsiObjectIdentity( vsiPath );
    // Identity fields are rewritten under the store lock by the probe
    // paths — read them through the same snapshot discipline the http
    // arm uses, or a racing getOrCreateVsiObject can tear a std::string
    // read here into UB / a phantom etag mismatch.
    const RemoteSourceIdentity storedIdentity = cache.snapshotIdentity( entry );
    if ( facts.provable() && storedIdentity.validator.hasStrongEtag() &&
         facts.etag != storedIdentity.validator.etag )
    {
      cache.invalidate( key );
      entry = cache.getOrCreateVsiObject( key, vsiPath, credentialContext,
                                          vsiObjectIdentity( vsiPath, facts ) );
    }
    else
    {
      // #1162: an unprovable identity (weak/absent/multipart-"null" ETag —
      // exactly the objects the identity layer supports) must still catch
      // the one change signal a HEAD always carries: the SIZE. Serving old
      // blocks under the NEW entry size is coherent-but-stale — mirror the
      // http arm's size_mismatch fallback and drop the blocks instead.
      if ( facts.hasSize && storedIdentity.hasSize
           && facts.sizeBytes != storedIdentity.sizeBytes )
      {
        cache.invalidate( key );
        return cache.getOrCreateVsiObject( key, vsiPath, credentialContext,
                                           vsiObjectIdentity( vsiPath, facts ) );
      }
      if ( facts.hasSize )
        cache.updateEntrySize( entry, facts.sizeBytes );
      // A provable HEAD with a matching strong ETag is the VSI
      // equivalent of the http Unchanged outcome — refresh the TTL
      // basis the same way, or an actively re-proven object still ages
      // out on wall clock (entryTtlSeconds counts from last proof).
      if ( facts.provable() && storedIdentity.validator.hasStrongEtag() )
        cache.touchEntryProven( entry );
    }
    return entry;
  }

  // Revalidate against the ENTRY'S stored validators — a fresh probe
  // would always compare equal to itself and never see a change.
  cache.revalidations.fetch_add( 1 );
  RemoteSourceValidator validator = RemoteSourceValidator::fromIdentity(
    cache.snapshotIdentity( entry ), requestUrl );
  const RevalidationResult result = validator.revalidate( validatorOptions( config ) );
  if ( result.outcome == RevalidationOutcome::Changed )
  {
    // The cached identity is proven stale: invalidate and let the
    // caller's normal no-entry path re-probe (identity, telemetry, and
    // quiet-miss handling then live in exactly one place).
    cache.invalidate( key );
    return nullptr;
  }
  if ( result.outcome == RevalidationOutcome::Unchanged )
  {
    // Only refresh the size when the revalidation answer actually
    // carried one: an Unchanged verdict never implies a known size
    // (a 304 has no entity headers), and writing a fabricated size 0
    // would collapse every later read to EOF.
    if ( validator.identity().hasSize )
      cache.updateEntrySize( entry, validator.identity().sizeBytes );
    // The origin just re-proved the content (12.0): that refreshes
    // the TTL basis too — an actively-revalidated resource is not
    // aged out by wall-clock time alone.
    cache.touchEntryProven( entry );
  }
  // Inconclusive (offline, size-only origins): keep serving — this is
  // the caller's declared trust level, and the revalidation attempt is
  // visible in telemetry.
  return entry;
}

// ---------------------------------------------------------------------------
// RangeCacheFilesystemHandler — the /vsirangecache/ prefix.
// ---------------------------------------------------------------------------

class RangeCacheFilesystemHandler final : public VSIFilesystemHandler
{
  public:
#if SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR
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
      std::string vsiPath;
      if ( !underlyingUrl( pszFilename, requestUrl, reason, &vsiPath ) )
      {
        CPLError( CE_Failure, CPLE_AppDefined, "vsirangecache: not a remote http(s) resource: %s",
                  reason.c_str() );
        return nullptr;
      }
      const bool vsiObject = !vsiPath.empty();

      CacheStore &cache = store();
      const std::string credentialContext = vsiObject ? currentRangeCacheCredentialContext()
                                                      : std::string();
      const std::string key =
        vsiObject ? objectResourceKey( vsiPath, credentialContext ) : resourceKey( requestUrl );

      // One config snapshot per open, taken under the store lock (P1
      // remediation discipline): updateConfig() swaps the config under the
      // same mutex, so the policy check and the validator options below
      // must read the snapshot, never the live field.
      std::uint64_t openConfigGeneration = 0;
      const RangeCacheConfig config = cache.snapshotConfig( openConfigGeneration );

      // Identity: probe once, then revalidate per the declared policy. A
      // validator mismatch invalidates the resource's cached bytes.
      // 12.0/13.0: the TTL horizon and the stale policy share ONE
      // implementation with Stat (applyTrustPolicy) — an expired entry is
      // dropped first and the open re-proves the resource from scratch.
      std::shared_ptr<ResourceEntry> entry =
        applyTrustPolicy( cache, key, requestUrl, vsiPath, credentialContext, config );
      if ( entry == nullptr )
      {
        if ( vsiObject )
        {
          const VsiObjectIdentityFacts facts = probeVsiObjectIdentity( vsiPath );
          // The store never answered (offline gate, refused auth): nothing
          // to cache against — a quiet miss, exactly like the http path.
          if ( !facts.probed )
          {
            CPLErrorReset();
            errno = ENOENT;
            return nullptr;
          }
          entry = cache.getOrCreateVsiObject( key, vsiPath, credentialContext,
                                              vsiObjectIdentity( vsiPath, facts ) );
        }
        else
        {
          entry = cache.probeNewEntry( requestUrl, validatorOptions( config ) );
          if ( entry == nullptr )
          {
            // Missing (ENOENT) or unreachable: a quiet null like any local
            // filesystem would answer for a path that does not exist — GDAL's
            // speculative sibling opens depend on this staying silent, and
            // the probe's own CPL error text must not leak into the caller's
            // open.
            CPLErrorReset();
            errno = ENOENT;
            return nullptr;
          }
        }
      }

#if SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR
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
      std::string vsiPath;
      if ( !underlyingUrl( pszFilename, requestUrl, reason, &vsiPath ) )
        return -1;
      const bool vsiObject = !vsiPath.empty();
      const std::string credentialContext = vsiObject ? currentRangeCacheCredentialContext()
                                                      : std::string();
      const std::string key = vsiObject ? objectResourceKey( vsiPath, credentialContext )
                                        : resourceKey( requestUrl );
      CacheStore &cache = store();
      // Config and entry size under the store lock (P1 remediation
      // discipline): updateConfig() / updateEntrySize() write these fields
      // under the same mutex — copy the small values once, then work on the
      // copies so Stat never reads a racing live field.
      std::uint64_t statConfigGeneration = 0;
      const RangeCacheConfig config = cache.snapshotConfig( statConfigGeneration );
      // 13.0: Stat answers from the SAME trust horizon as Open — an entry
      // past its TTL is re-proven, and RevalidateOnOpen revalidates
      // (unchanged → refreshed horizon; changed → invalidate + the normal
      // probe path; inconclusive → declared trust level). One conditional
      // request per stat at most — no extra storm beyond what Open pays.
      std::shared_ptr<ResourceEntry> entry =
        applyTrustPolicy( cache, key, requestUrl, vsiPath, credentialContext, config );
      std::uint64_t sizeBytes = 0;
      bool haveSize = entry != nullptr && cache.entrySize( entry, sizeBytes );
      if ( entry == nullptr || !haveSize )
      {
        // A bounded identity probe (never a download) to learn the size.
        if ( vsiObject )
        {
          const VsiObjectIdentityFacts facts = probeVsiObjectIdentity( vsiPath );
          // Unreachable / gone: a quiet stat miss — speculative sibling
          // probes stay silent, never pollute the CPL error state.
          if ( !facts.probed || !facts.hasSize )
          {
            CPLErrorReset();
            errno = ENOENT;
            return -1;
          }
          entry = cache.getOrCreateVsiObject( key, vsiPath,
                                              credentialContext,
                                              vsiObjectIdentity( vsiPath, facts ) );
          haveSize = true;
          sizeBytes = facts.sizeBytes;
        }
        else
        {
          RemoteSourceValidator validator =
            RemoteSourceValidator::probe( requestUrl, validatorOptions( config ) );
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
          haveSize = identity.hasSize;
          sizeBytes = identity.sizeBytes;
        }
      }
      if ( !haveSize )
      {
        // Range-ignoring oversized origin: learn the size from the
        // underlying handle (open + Seek END — no content transfer).
        const std::string underlyingSpelling =
          vsiObject ? vsiPath : std::string( "/vsicurl/" ) + requestUrl;
        VSIVirtualHandleUniquePtr underlying = openReadonlyVsi( underlyingSpelling.c_str() );
        if ( underlying == nullptr )
        {
          errno = ENOENT;
          return -1;
        }
        underlying->Seek( 0, SEEK_END );
        sizeBytes = underlying->Tell();
        cache.updateEntrySize( entry, sizeBytes );
      }
      pStatBuf->st_size = static_cast<decltype( pStatBuf->st_size )>( sizeBytes );
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
  json["max_bytes_per_resource"] = static_cast<Json::UInt64>( maxBytesPerResource );
  json["max_single_fetch_bytes"] = static_cast<Json::UInt64>( maxSingleFetchBytes );
  json["block_size"] = static_cast<Json::UInt64>( blockSize );
  json["stale_policy"] = rangeCacheStalePolicyName( stalePolicy );
  json["entry_ttl_seconds"] = entryTtlSeconds;
  json["timeout_seconds"] = timeoutSeconds;
  json["connect_timeout_seconds"] = connectTimeoutSeconds;
  json["max_retries"] = maxRetries;
  json["max_concurrent_fetch_bytes"] = static_cast<Json::UInt64>( maxConcurrentFetchBytes );
  json["fetch_attempts"] = fetchAttempts;
  json["retry_backoff_base_ms"] = retryBackoffBaseMs;
  json["retry_backoff_max_ms"] = retryBackoffMaxMs;
  json["disk_directory"] = diskDirectory;
  json["disk_max_bytes"] = static_cast<Json::UInt64>( diskMaxBytes );
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
  json["dedup_hits"] = static_cast<Json::UInt64>( dedupHits );
  json["max_in_flight_fetch_bytes"] = static_cast<Json::UInt64>( maxInFlightFetchBytes );
  json["retried_fetches"] = static_cast<Json::UInt64>( retriedFetches );
  json["max_cached_bytes"] = static_cast<Json::UInt64>( maxCachedBytes );
  // 13.0 maintenance observability: retry occupancy, admission contention,
  // cancel-veto outcomes, and the TTL trust horizon.
  json["fetch_attempts"] = static_cast<Json::UInt64>( fetchAttempts );
  json["backoff_waits"] = static_cast<Json::UInt64>( backoffWaits );
  json["admission_waits"] = static_cast<Json::UInt64>( admissionWaits );
  json["cancelled_fetches"] = static_cast<Json::UInt64>( cancelledFetches );
  json["ttl_expirations"] = static_cast<Json::UInt64>( ttlExpirations );
  json["ttl_refreshes"] = static_cast<Json::UInt64>( ttlRefreshes );
  // 9.0 read amplification: origin bytes pulled per byte served. 0 when
  // nothing was served yet (no denominator — never fabricate a ratio).
  json["read_amplification"] =
    bytesServed > 0 ? static_cast<double>( bytesFetched ) / static_cast<double>( bytesServed ) : 0.0;
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
    // 9.0 M3: the disk layer follows the config (empty directory = off).
    RangeDiskBlockStore::configure( config.diskDirectory, config.diskMaxBytes );
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
#if SICNU_GDAL_VSI_REMOVE_HANDLER
  // RemoveHandler deletes the handler object (see install): forget our
  // pointer so a re-install allocates a fresh one.
  VSIFileManager::RemoveHandler( kRangeCachePrefix );
  // GDAL deleted the handler above — forget the pointer (a later install()
  // lazily allocates a fresh one; touching the old pointer would be a
  // use-after-free).
  cachedHandler() = nullptr;
  g_store->dropAll();
  s_handlerInstalled = false;
  RangeDiskBlockStore::clear();
#else
  // GDAL < 3.9 has no RemoveHandler: the prefix cannot be deregistered, so
  // uninstall only drops the bytes and KEEPS the handler installed/registered
  // (reporting installed()==false while /vsirangecache/ still resolves, or
  // re-registering a fresh handler per cycle, would leak and lie).
  g_store->dropAll();
  RangeDiskBlockStore::clear();
#endif
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
  std::string vsiPath;
  if ( !underlyingUrl( url, requestUrl, reason, &vsiPath ) )
    throw GeoError( ErrorCode::InvalidArgument, "RemoteRangeCache: not a remote resource: " + reason );
  if ( !vsiPath.empty() )
  {
    // Object entries are keyed per creating principal: invalidate under the
    // CURRENT context, and when a context is set also the SHARED (no
    // window) entry — a context-less open may have created one first.
    g_store->invalidate( objectResourceKey( vsiPath, currentRangeCacheCredentialContext() ) );
    if ( !currentRangeCacheCredentialContext().empty() )
      g_store->invalidate( objectResourceKey( vsiPath, std::string() ) );
    return;
  }
  g_store->invalidate( resourceKey( requestUrl ) );
}

void RemoteRangeCache::cancelFetches( const std::string &url )
{
  std::lock_guard<std::mutex> lock( g_storeLifecycleMutex );
  if ( !g_store )
    return;
  std::string requestUrl;
  std::string reason;
  std::string vsiPath;
  if ( !underlyingUrl( url, requestUrl, reason, &vsiPath ) )
    throw GeoError( ErrorCode::InvalidArgument, "RemoteRangeCache: not a remote resource: " + reason );
  if ( !vsiPath.empty() )
  {
    g_store->setFetchCancelled( objectResourceKey( vsiPath, currentRangeCacheCredentialContext() ),
                                true );
    if ( !currentRangeCacheCredentialContext().empty() )
      g_store->setFetchCancelled( objectResourceKey( vsiPath, std::string() ), true );
    return;
  }
  g_store->setFetchCancelled( resourceKey( requestUrl ), true );
}

void RemoteRangeCache::resumeFetches( const std::string &url )
{
  std::lock_guard<std::mutex> lock( g_storeLifecycleMutex );
  if ( !g_store )
    return;
  std::string requestUrl;
  std::string reason;
  std::string vsiPath;
  if ( !underlyingUrl( url, requestUrl, reason, &vsiPath ) )
    throw GeoError( ErrorCode::InvalidArgument, "RemoteRangeCache: not a remote resource: " + reason );
  if ( !vsiPath.empty() )
  {
    g_store->setFetchCancelled( objectResourceKey( vsiPath, currentRangeCacheCredentialContext() ),
                                false );
    if ( !currentRangeCacheCredentialContext().empty() )
      g_store->setFetchCancelled( objectResourceKey( vsiPath, std::string() ), false );
    return;
  }
  g_store->setFetchCancelled( resourceKey( requestUrl ), false );
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
  telemetry.dedupHits = g_store->dedupHits.load();
  telemetry.maxInFlightFetchBytes = g_store->maxInFlightBytes.load();
  telemetry.retriedFetches = g_store->retriedFetches.load();
  telemetry.maxCachedBytes = g_store->maxCachedBytes.load();
  telemetry.fetchAttempts = g_store->fetchAttempts.load();
  telemetry.backoffWaits = g_store->backoffWaits.load();
  telemetry.admissionWaits = g_store->admissionWaits.load();
  telemetry.cancelledFetches = g_store->cancelledFetches.load();
  telemetry.ttlExpirations = g_store->ttlExpirations.load();
  telemetry.ttlRefreshes = g_store->ttlRefreshes.load();
  Json::Value json = telemetry.toJson();
  json["cached_bytes"] = static_cast<Json::UInt64>( g_store->cachedBytes() );
  json["max_cached_bytes"] = static_cast<Json::UInt64>( g_store->maxCachedBytes.load() );
  json["in_flight_fetch_bytes"] = static_cast<Json::UInt64>( g_store->inFlightBytes() );
  json["disk_hits"] = static_cast<Json::UInt64>( g_store->diskHits.load() );
  json["disk"] = RangeDiskBlockStore::stats().toJson();
  json["disk"]["enabled"] = RangeDiskBlockStore::enabled();
  json["config"] = g_store->config.toJson();
  return json;
}

Json::Value RemoteRangeCache::diskCacheStatsJson()
{
  RangeDiskCacheStats stats = RangeDiskBlockStore::stats();
  Json::Value json = stats.toJson();
  json["enabled"] = RangeDiskBlockStore::enabled();
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

// --- 11.0 credential separation context (DECISIONS D-1102) -----------------

namespace
{

std::mutex g_contextMutex;
std::string g_credentialContext;

} // namespace

void setRangeCacheCredentialContext( const std::string &fingerprint )
{
  std::lock_guard<std::mutex> lock( g_contextMutex );
  g_credentialContext = fingerprint;
}

std::string currentRangeCacheCredentialContext()
{
  std::lock_guard<std::mutex> lock( g_contextMutex );
  return g_credentialContext;
}

} // namespace sicnu::geo

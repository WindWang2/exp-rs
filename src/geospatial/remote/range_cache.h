/***************************************************************************
  geospatial/remote/range_cache.h
  Cloud-Native Geospatial I/O 7.0 — bounded byte-range cache over remote
  raster sources (task B), exposed as a GDAL VSI filesystem handler.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Opening "/vsirangecache/https://host/file.tif" gives the FULL GDAL stack
  (COG driver, overviews, window reads) over a cache this layer controls:

    * bounded LRU byte budget across all cached resources
    * request coalescing: adjacent missing blocks merge into one ranged GET;
      a concurrent reader of the same missing range waits for the in-flight
      fetch instead of duplicating it
    * per-resource identity through RemoteSourceValidator: stale validation
      policy decides when revalidation happens and a validator mismatch
      drops the resource's cached blocks (never serves change-blind bytes
      silently — the policy is the caller's declared trust level)
    * telemetry: hits, misses, bytes served/fetched, coalesced fetches,
      evictions, fallbacks — JSON-exported
    * failure fallback: any cache-internal failure degrades to a direct
      /vsicurl/ read — the cache is an optimization, never a correctness
      gate. This is NOT an offline mirror and must never be sold as one.

  Lifecycle: VSI handlers are process-global by nature, so installation is
  explicit and idempotent (install → work → uninstall). Tests use it as a
  scoped fixture. All entry points are thread-safe.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_RANGE_CACHE_H
#define SICNU_GEOSPATIAL_RANGE_CACHE_H

#include "geospatial/common.h"

#include <json/json.h>

#include <cstdint>
#include <string>

namespace sicnu::geo
{

inline constexpr const char *kRangeCachePrefix = "/vsirangecache/";

/// When the cached resource is revalidated against its origin.
enum class RangeCacheStalePolicy
{
    /// Revalidate (conditional request) on every Open; a validator mismatch
    /// drops the resource's cached blocks and refreshes the identity. The
    /// safe default: changed origins are never served from cache.
    RevalidateOnOpen,
    /// Validate once per process (first Open); later Opens trust the
    /// captured identity. Explicit clearEntries()/revalidate() for control.
    ValidateOnce,
    /// Never revalidate: pure best-effort speed (offline-ish). Only for
    /// callers who accept change-blind reads explicitly.
    TrustForever,
};

const char *rangeCacheStalePolicyName( RangeCacheStalePolicy policy );
RangeCacheStalePolicy rangeCacheStalePolicyFromName( const std::string &name ); ///< throws

struct RangeCacheConfig
{
    /// Total byte budget across every cached resource (LRU-evicted).
    std::uint64_t maxCacheBytes = 64ull * 1024 * 1024;
    /// Upper bound for one coalesced ranged GET (the run of missing blocks
    /// fetched together is clamped to this).
    std::uint64_t maxSingleFetchBytes = 8ull * 1024 * 1024;
    /// Cache block granularity (reads fetch whole blocks).
    std::uint64_t blockSize = 64ull * 1024;
    RangeCacheStalePolicy stalePolicy = RangeCacheStalePolicy::RevalidateOnOpen;
    int timeoutSeconds = 15;
    int connectTimeoutSeconds = 5;
    int maxRetries = 1;

    Json::Value toJson() const;
};

struct RangeCacheTelemetry
{
    std::uint64_t hits = 0;                  ///< reads served from cached blocks
    std::uint64_t misses = 0;                ///< reads that needed origin bytes
    std::uint64_t bytesServed = 0;           ///< total bytes returned to readers
    std::uint64_t bytesFetched = 0;          ///< bytes pulled from origins
    std::uint64_t coalescedFetches = 0;      ///< ranged GETs issued (merged runs)
    std::uint64_t evictions = 0;             ///< blocks evicted under the budget
    std::uint64_t invalidations = 0;         ///< resource drops after a mismatch
    std::uint64_t fallbackReads = 0;         ///< reads degraded to direct /vsicurl/
    std::uint64_t revalidations = 0;         ///< conditional requests issued

    Json::Value toJson() const;
};

class RemoteRangeCache
{
  public:
    /// Installs the "/vsirangecache/" handler with the given configuration.
    /// Idempotent: re-install replaces the configuration and clears nothing
    /// (call clearEntries() for that). Throws GeoError(InvalidArgument) for
    /// a zero blockSize/budget.
    static void install( const RangeCacheConfig &config = {} );

    /// Removes the handler and drops every cached byte. After uninstall the
    /// prefix stops resolving.
    static void uninstall();

    static bool installed();

    /// Drops all cached bytes and per-resource identities (telemetry kept).
    static void clearEntries();

    /// Forces a revalidation of one cached resource on its next use.
    static void invalidateResource( const std::string &url );

    static Json::Value telemetryJson();
    static RangeCacheConfig currentConfig();

    /// Maps a caller URL to the cached spelling ("/vsirangecache/<url>").
    static std::string cachedPath( const std::string &url );
    /// True when the path carries the cache prefix.
    static bool isCachePath( const std::string &path );
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_RANGE_CACHE_H

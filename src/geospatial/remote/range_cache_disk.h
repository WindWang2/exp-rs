/***************************************************************************
  geospatial/remote/range_cache_disk.h
  Cloud-Native Geospatial Data Fabric 9.0 (M3) — optional disk block layer
  under the memory range cache.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract:
    * blocks are content-addressed by the RESOURCE's provable identity
      (strong ETag basis, else size+Last-Modified basis) + block index. A
      resource with NO provable identity is never disk-cached (fail-closed,
      same doctrine as the memory layer's identity contract).
    * every block file carries a SHA-256 integrity trailer; a corrupt or
      torn file reads as a MISS and is unlinked — never served.
    * publication is temp-file → fsync → rename (a crash leaves either the
      old directory content or a complete block, never a partial one under
      a final name).
    * byte cap with LRU (file mtime) eviction; reads take no lock (open,
      read, verify), writes take a short global mutex for temp-name
      allocation and eviction only — no network I/O ever happens under it.
    * the disk layer is an OPTIMIZATION under the memory cache: every miss
      falls through to the origin path unchanged.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_RANGE_CACHE_DISK_H
#define SICNU_GEOSPATIAL_RANGE_CACHE_DISK_H

#include "geospatial/common.h"

#include <json/json.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::geo
{

struct RangeDiskCacheStats
{
    std::uint64_t hits = 0;       ///< blocks served from disk
    std::uint64_t puts = 0;       ///< blocks published to disk
    std::uint64_t corrupt = 0;    ///< blocks refused by the integrity check
    std::uint64_t evictions = 0;  ///< blocks unlinked under the byte cap
    std::uint64_t bytesStored = 0; ///< current on-disk block bytes (best effort)

    Json::Value toJson() const;
};

/// One process-wide disk block store. All methods are thread-safe; methods
/// on a disabled store (empty directory) are no-ops that report misses.
class RangeDiskBlockStore
{
  public:
    /// (Re)configures the store. An empty directory disables it; switching
    /// directories keeps the old files on disk (they are content-named and
    /// reusable by a later re-configuration).
    static void configure( const std::string &directory, std::uint64_t maxBytes );
    static bool enabled();

    /// Drops every cached block file of the CURRENT directory (not the
    /// directory itself). Stats are reset.
    static void clear();

    static RangeDiskCacheStats stats();

    /// The canonical content identity basis for a resource: strong ETag when
    /// provable, else size+Last-Modified, else "" (not disk-cacheable).
    /// Callers pass the captured identity fields; the store never probes.
    static std::string identityBasis( const std::string &requestUrl, bool hasStrongEtag,
                                      const std::string &etag, bool hasSize, std::uint64_t sizeBytes,
                                      const std::string &lastModified );

    /// Reads one block. Returns false on any miss/corruption (corrupt files
    /// are unlinked). `expectedSize` tolerates an EOF-clamped final block.
    static bool readBlock( const std::string &basis, std::uint64_t blockIndex,
                           std::vector<unsigned char> &outData );

    /// Publishes one block (temp → fsync → rename), then evicts under the
    /// byte cap. No-op when disabled or the basis is empty.
    static void putBlock( const std::string &basis, std::uint64_t blockIndex,
                          const unsigned char *data, std::size_t size );
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_RANGE_CACHE_DISK_H

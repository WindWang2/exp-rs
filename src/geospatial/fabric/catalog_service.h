/***************************************************************************
  geospatial/fabric/catalog_service.h
  Cloud-Native Data Fabric / Data Cube 10.0 — unified catalog service.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  One query surface over three catalog backends:

    * LocalStacFiles    — a local STAC tree (catalog/collection JSON with
                          rel links, or a directory of Item JSONs); walk is
                          bounded (files, depth) and cycle-safe.
    * RemoteStacApi     — the existing StacClient (search/pagination/bounds
                          reused verbatim — no second STAC transport).
    * InMemoryRecords   — records the caller already holds (the asset_query
                          engine over detached records; still not a store).

  The query vocabulary is the SUPERSET filters (bbox, temporal range, col-
  lections, ids, cloud cover, platform, sensor instruments, asset role,
  media type). Server-pushable filters go to the server (when a server
  exists); platform/sensor/role/media-type/cloud-cover are enforced in the
  service layer on EVERY backend, so one query yields the same records on
  every backend (DECISIONS D-1004) — with honest accounting
  (serverFiltered / clientFilteredOut).

  Bounded by construction: page size, crawl caps, hard result caps — every
  surface reports whether it truncated. Cancellation is cooperative through
  a CancelToken checked between pages/items → GeoError(Cancelled). Offline
  is typed: the remote backend refuses through the offline gate's message;
  local/in-memory backends keep working offline (that is the point).

  Records carry resolved, fetchable asset paths (relative hrefs resolved
  against item provenance — the 9.0 M4 contract) so downstream layers never
  re-parse STAC JSON.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_FABRIC_CATALOG_SERVICE_H
#define SICNU_GEOSPATIAL_FABRIC_CATALOG_SERVICE_H

#include "geospatial/catalog/asset_query.h"
#include "geospatial/common.h"
#include "geospatial/stac/stac_client.h"

#include <json/json.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// Cooperative cancellation flag. Cheap to copy (shared_ptr to the atomic),
/// safe to poll from any thread; cancel() is idempotent.
class CancelToken
{
  public:
    CancelToken() : mState( std::make_shared<std::atomic<bool>>( false ) ) {}
    void cancel() { mState->store( true ); }
    bool cancelled() const { return mState->load(); }

  private:
    std::shared_ptr<std::atomic<bool>> mState;
};

enum class CatalogBackendKind
{
    LocalStacFiles,
    RemoteStacApi,
    InMemoryRecords,
};

const char *catalogBackendKindName( CatalogBackendKind kind );

/// The unified query. Shapes validate up front (bbox arity, unparseable
/// temporal bounds → GeoError(InvalidArgument) before any walk/network).
struct CatalogQuery
{
    /// 4 or 6 values (minx,miny,maxx,maxy[,minz,maxz]); empty = unbounded.
    std::vector<double> bbox;
    /// Temporal bounds as UTC instants ("" = open). Instant-based comparison
    /// (time_normalization); an unparseable bound is a typed error.
    std::string temporalStartUtc;
    std::string temporalEndUtc;
    std::vector<std::string> collections;
    std::vector<std::string> ids;
    /// Cloud cover ceiling in percent. Records without a declared cloud
    /// cover FAIL this filter (absence is not evidence — asset_query parity).
    bool hasCloudCoverMax = false;
    double cloudCoverMax = 100.0;
    /// Case-insensitive equality on the item platform.
    std::string platformEquals;
    /// Any-of (case-insensitive) on item instruments.
    std::vector<std::string> sensorInstruments;
    /// Item qualifies when ANY asset carries this role.
    std::string assetRole;
    /// Case-insensitive containment on the qualifying asset's media type.
    std::string mediaTypeSubstring;
    /// Page size (0 = backend default).
    int limit = 0;
    /// Hard bound for full crawls (searchAll); typed truncation reporting.
    int maxItems = 1000;

    void validate() const;
};

/// Opaque continuation between pages (never a guess: what the backend
/// produced, the caller passes back verbatim).
struct CatalogContinuation
{
    bool hasMore = false;
    // Local backend: offset into the walk order.
    std::size_t localOffset = 0;
    // Remote backend: the StacPage continuation descriptor verbatim.
    std::string remoteMethod;
    std::string remoteHref;
    Json::Value remoteBody;
    bool remoteMerge = false;
};

/// One bounded page.
struct CatalogPage
{
    std::vector<AssetRecord> records;
    /// Matching items whose qualifying asset record is in `records`
    /// (parallel-ish provenance; one entry per record, same order).
    std::vector<StacItem> items;
    CatalogContinuation next;
    std::size_t offset = 0;             ///< this page's walk offset
    bool truncatedByCap = false;        ///< a declared bound stopped the walk
    bool serverFiltered = false;        ///< remote pushdown contributed
    std::uint64_t clientFilteredOut = 0;///< dropped by service-side filters
    Json::Value statsJson() const;
};

struct CatalogServiceOptions
{
    /// Remote backend options (timeouts, byte bounds, response cache).
    StacClientOptions stac;
    /// Local walk bounds.
    int localMaxFiles = 20000;      ///< files inspected (typed stop beyond)
    int localMaxDepth = 8;          ///< directory depth
    /// Hard ceiling on items materialized per page (memory bound).
    int maxRecordsPerPage = 1000;
};

/// Backend descriptor returned by openCatalogService (diagnostics carry it).
struct CatalogServiceInfo
{
    CatalogBackendKind kind = CatalogBackendKind::LocalStacFiles;
    std::string origin;             ///< root path / API root / "<records>"
    std::string displayOrigin;      ///< redacted form for logs/reports
    Json::Value toJson() const;
};

class CatalogService
{
  public:
    ~CatalogService();
    CatalogService( const CatalogService & ) = delete;
    CatalogService &operator=( const CatalogService & ) = delete;
    CatalogService( CatalogService && ) noexcept;
    CatalogService &operator=( CatalogService && ) noexcept;

    const CatalogServiceInfo &info() const { return mInfo; }

    /// One bounded page. First call passes a default continuation.
    CatalogPage searchPage( const CatalogQuery &query, const CatalogContinuation &continuation,
                            const CancelToken &cancel = {} ) const;

    /// Paged crawl bounded by query.maxItems; truncatedByCap reports an
    /// early stop honestly.
    struct SearchAllResult
    {
        std::vector<AssetRecord> records;
        std::vector<StacItem> items;
        bool truncatedByCap = false;
        std::uint64_t clientFilteredOut = 0;
    };
    SearchAllResult searchAll( const CatalogQuery &query, const CancelToken &cancel = {} ) const;

  private:
    friend CatalogService openCatalogService( const std::string &root,
                                              const CatalogServiceOptions &options );
    friend CatalogService catalogServiceOverRecords( std::vector<AssetRecord> records );
    CatalogService() = default;

    struct LocalImpl;   ///< local walk state (visited set, file list)
    struct RemoteImpl;  ///< StacClient instance
    struct MemoryImpl;  ///< records snapshot

    CatalogServiceInfo mInfo;
    CatalogServiceOptions mOptions;
    std::unique_ptr<LocalImpl> mLocal;
    std::unique_ptr<RemoteImpl> mRemote;
    std::shared_ptr<MemoryImpl> mMemory;
};

/// Opens the backend matching the root spelling: LocalDirectory/LocalFile
/// → LocalStacFiles; RemoteHttp(S) → RemoteStacApi (offline → typed refusal
/// at open). Anything else → GeoError(InvalidArgument). Never opens pixel
/// data; local walk stats are metadata-only.
CatalogService openCatalogService( const std::string &root,
                                   const CatalogServiceOptions &options = {} );

/// In-memory backend over records the caller holds (detached, immutable
/// copy). Query runs through the asset_query engine — one vocabulary.
CatalogService catalogServiceOverRecords( std::vector<AssetRecord> records );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_FABRIC_CATALOG_SERVICE_H

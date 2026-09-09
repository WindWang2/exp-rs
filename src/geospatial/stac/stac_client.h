/***************************************************************************
  geospatial/stac/stac_client.h
  Cloud-Native Geospatial I/O 7.0 — STAC API client (task C), built on the
  bounded CPL HTTP layer and the existing StacItem mapping.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Scope: READ-ONLY search / browse over a STAC API (STAC API - Item Search &
  Collections conformance classes):

    * item search: bbox, intersects, datetime, collections, ids, limit,
      query extension (POST), sortby (best-effort, origin-dependent)
    * pagination: rel="next" links (RFC 8288) including POST-merge bodies,
      plus token/next GET params; searchAll() is hard-bounded by maxItems
    * collections: list + single fetch
    * client-side filters: asset roles, media type, cloud cover
    * href safety: fetches use raw hrefs; every DISPLAY surface goes through
      credential redaction (displayAssetHref / redacted display forms)

  Not in scope: writes, transactions, auth flows (CPL owns credentials), and
  any governance decision — items project onto canonical metadata / temporal
  series through the adapter seam, ownership stays with the callers.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_STAC_CLIENT_H
#define SICNU_GEOSPATIAL_STAC_CLIENT_H

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/remote/http_fetch.h"
#include "geospatial/stac/stac_mapper.h"

#include <json/json.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::geo
{

struct StacSearchQuery
{
    /// 4 or 6 values (minx,miny,maxx,maxy[,minz,maxz]).
    std::vector<double> bbox;
    /// GeoJSON geometry object (intersects). Empty when unused.
    Json::Value intersects;
    /// Single instant or "start/end" interval ("" = unbounded). Open ends
    /// use ".." per the STAC spec.
    std::string datetime;
    std::vector<std::string> collections;
    std::vector<std::string> ids;
    int limit = 0;              ///< 0 = origin default
    /// Query extension payload ({"eo:cloud_cover": {"lt": 10}}); forces the
    /// POST form of the request.
    Json::Value query;
    /// Sortby field ("datetime"); origin support varies (best effort).
    std::string sortBy;

    /// Client-side validation. Throws GeoError(InvalidArgument) for an
    /// unusable bbox/datetime shape before anything hits the network.
    void validate() const;
};

struct StacClientOptions
{
    int timeoutSeconds = 15;
    int connectTimeoutSeconds = 5;
    int maxRetries = 1;
    std::uintmax_t maxResponseBytes = kDefaultHttpMaxResponseBytes;
    /// Hard bound for searchAll() — a paged crawl must never run away.
    int maxItems = 1000;
};

/// One page of search results plus everything needed to continue.
struct StacPage
{
    std::vector<StacItem> items;
    /// Continuation descriptor extracted from rel="next" links ("" when this
    /// is the last page). method is "GET" (href carries the token) or "POST"
    /// (jsonBody carries the merged request body).
    std::string nextMethod;
    std::string nextHref;
    Json::Value nextBody;
    bool hasMore() const { return !nextHref.empty(); }
};

struct StacSearchAllResult
{
    std::vector<StacItem> items;
    bool truncatedByLimit = false;  ///< true when maxItems stopped the crawl
};

/// One temporally-ordered entry of a collection series (adapter seam).
struct StacSeriesEntry
{
    std::string datetime;       ///< effective ISO-8601 (item datetime or range start)
    StacItem item;
    RasterMetadata canonical;   ///< stacItemToCanonical projection
};

class StacClient
{
  public:
    /// root is the API root ("https://host/stac"); a trailing "/" is fine.
    explicit StacClient( std::string root, const StacClientOptions &options = {} );

    const std::string &root() const { return mRoot; }
    const StacClientOptions &options() const { return mOptions; }

    /// First search page. GET query params for core filters; POST /search
    /// automatically when the query extension (or intersects) is present.
    StacPage search( const StacSearchQuery &query ) const;

    /// Fetches the next page using the page's continuation descriptor.
    /// Returns an empty page when there is none.
    StacPage nextPage( const StacPage &page ) const;

    /// Paged crawl bounded by options.maxItems (truncatedByLimit reports a
    /// stop-before-exhaustion truthfully).
    StacSearchAllResult searchAll( const StacSearchQuery &query ) const;

    /// Collections surface (raw JSON documents).
    Json::Value collections() const;
    Json::Value collection( const std::string &collectionId ) const;
    /// Items of one collection (first page honoring query.datetime/limit).
    StacPage collectionItems( const std::string &collectionId, const StacSearchQuery &query ) const;

    // --- client-side filters ----------------------------------------------
    /// Assets carrying the given role ("data", "thumbnail", ...).
    static std::vector<StacAsset> assetsWithRole( const StacItem &item, const std::string &role );
    /// Assets whose media type contains the substring ("image/tiff", ...).
    static std::vector<StacAsset> assetsOfMediaType( const StacItem &item,
                                                     const std::string &mediaTypeSubstring );
    /// True when the item declares eo:cloud_cover <= maxPercent (undclared
    /// cloud cover counts as matching — absence is not evidence).
    static bool matchesCloudCover( const StacItem &item, double maxPercent );

    /// Redacted display form of an asset href (credential-safe for logs/UI).
    static std::string displayAssetHref( const StacAsset &asset );

  private:
    StacPage executeSearch( const std::string &method, const std::string &url,
                            const Json::Value &body ) const;
    HttpFetchOptions fetchOptions() const;

    std::string mRoot;
    StacClientOptions mOptions;
};

/// Temporal collection adapter (task C seam): orders items into a series by
/// effective datetime (item datetime, else start/end range, else item id —
/// never dropped silently; undated entries sort last and keep item order).
std::vector<StacSeriesEntry> buildTemporalSeries( const std::vector<StacItem> &items );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_STAC_CLIENT_H

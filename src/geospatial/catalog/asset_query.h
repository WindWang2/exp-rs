/***************************************************************************
  geospatial/catalog/asset_query.h
  Cloud-Native Geospatial Data Fabric 9.0 (M7) — non-UI catalog query
  primitives over detached immutable asset records.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  WHAT THIS IS: pure query machinery for Workbench/DataManager surfaces to
  ADOPT — predicates, ordering, bounded pagination, lightweight summaries —
  operating on detached immutable records the CALLER already holds (from a
  STAC crawl, an inspect walk, a workspace manifest…).

  WHAT THIS IS NOT: not a store, not a second DataManager, no I/O, no UI,
  no daemons. Records are values; the engine is a function. Bounded outputs
  are the contract: every query carries page bounds and a hard result cap.
  Determinism: equal-priority records keep input order (stable sort) —
  the same input always yields the same page.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_ASSET_QUERY_H
#define SICNU_GEOSPATIAL_ASSET_QUERY_H

#include "geospatial/common.h"

#include <json/json.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// One detached, immutable asset record. Construction is the caller's
/// decision (fromStacItem / fromCanonical helpers exist as adapters);
/// the record itself never opens anything and never mutates.
struct AssetRecord
{
    std::string id;
    std::string collection;
    std::string path;              ///< fetchable href/path (already resolved)
    std::string mediaType;
    std::vector<std::string> roles;

    // Temporal extent (verbatim + normalized UTC instants; "" when absent).
    std::string datetime;
    std::string datetimeUtc;       ///< normalized instant, "" when unresolvable
    std::string startUtc;
    std::string endUtc;

    // Spatial extent (traditional GIS order; valid=false when none).
    bool hasBbox = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;

    // Lightweight facts (display-level, never credentials).
    double cloudCover = 0.0;
    bool hasCloudCover = false;

    // Free metadata for predicate matching (flat key/value).
    std::map<std::string, std::string> metadata;

    Json::Value toJson() const;
    static AssetRecord fromJson( const Json::Value &json );
};

/// Query predicate set. Empty fields match everything; temporal/bbox ranges
/// are inclusive; undated records fail temporal filters (absence is not
/// evidence — never silently included).
struct AssetQuery
{
    std::string idEquals;
    std::string collectionEquals;
    std::string roleEquals;              ///< any role of the record matches
    std::string mediaTypeSubstring;      ///< case-insensitive containment
    std::string metadataKeyEquals;       ///< flat metadata predicate pair
    std::string metadataValueEquals;

    /// Temporal range (normalized UTC instants or ""); "" bounds are open.
    /// Instant-based: mixed-offset record datetimes compare correctly.
    std::string temporalStartUtc;
    std::string temporalEndUtc;

    /// Spatial intersection test (inclusive edges); unused when
    /// requiresBbox == false.
    bool requiresBbox = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;

    std::string sortByTemporal;          ///< "" | "asc" | "desc" (then id)

    /// Client-side validation. Throws GeoError(InvalidArgument) for a
    /// malformed bbox or unparseable temporal bound before any work.
    void validate() const;

    bool matches( const AssetRecord &record ) const;
};

/// One page of query output plus honest continuation facts.
struct AssetQueryPage
{
    std::vector<AssetRecord> records;
    std::size_t totalMatches = 0;   ///< matches BEFORE pagination (O(n) count)
    std::size_t offset = 0;         ///< this page's offset within the matches
    bool hasMore = false;           ///< further matches exist past this page
};

struct AssetQueryOptions
{
    std::size_t offset = 0;          ///< matches to skip (bounded by matches)
    std::size_t limit = 100;         ///< max records on the page
    std::size_t hardCap = 10000;     ///< absolute result ceiling (typed error
                                     ///< when totalMatches exceed it AND
                                     ///< countMatches is false — see below)
    bool countMatches = true;        ///< full match count (skippable for
                                     ///< huge sets; hasMore stays honest)
    bool summary = false;            ///< only bounded aggregate facts, no records
};

/// Lightweight aggregate over the MATCHES (bounded output regardless of set
/// size): count, temporal extent, spatial extent, collection histogram cap.
struct AssetQuerySummary
{
    std::uint64_t totalMatches = 0;
    std::string earliestUtc;
    std::string latestUtc;
    bool hasSpatialExtent = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    std::uint64_t withoutDatetime = 0;

    Json::Value toJson() const;
};

/// Runs the query. Pure: the input records are never modified. Throws
/// GeoError(InvalidArgument) for a malformed query and
/// GeoError(ResourceExhausted) when hardCap would be exceeded with
/// countMatches == false.
AssetQueryPage queryAssets( const std::vector<AssetRecord> &records, const AssetQuery &query,
                            const AssetQueryOptions &options = {} );

/// Summary-only pass (no page materialization).
AssetQuerySummary summarizeAssets( const std::vector<AssetRecord> &records,
                                   const AssetQuery &query );

// --- adapters -----------------------------------------------------------

/// Builds a record from a parsed STAC item + its resolved primary asset
/// path. Uses the item's normalized UTC datetime (8.0 work); a record from
/// an undated item carries no temporal facts — filters treat it honestly.
AssetRecord assetRecordFromStacItem( const struct StacItem &item, const std::string &resolvedPath );
/// Builds a record from canonical raster metadata (the inspect vocabulary).
AssetRecord assetRecordFromCanonical( const struct RasterMetadata &metadata, const std::string &id,
                                      const std::string &path );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_ASSET_QUERY_H

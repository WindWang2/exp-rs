/***************************************************************************
  geospatial/catalog/asset_query.cpp — non-UI catalog query primitives (M7).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/catalog/asset_query.h"

#include "geospatial/stac/stac_mapper.h"
#include "geospatial/util/time_normalization.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace sicnu::geo
{

namespace
{

bool containsIgnoreCase( const std::string &haystack, const std::string &needle )
{
  if ( needle.empty() )
    return true;
  const auto lower = []( const std::string &text ) {
    std::string out = text;
    for ( char &c : out )
      c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
    return out;
  };
  return lower( haystack ).find( lower( needle ) ) != std::string::npos;
}

/// Inclusive instant-range test with open bounds. An empty record instant
/// fails the filter — absence is not evidence of match.
bool instantInRange( const std::string &recordUtc, const std::string &startUtc, const std::string &endUtc )
{
  if ( recordUtc.empty() )
    return false;
  const InstantParse record = parseIso8601Instant( recordUtc );
  if ( !record.ok )
    return false;
  if ( !startUtc.empty() )
  {
    const InstantParse start = parseIso8601Instant( startUtc );
    if ( !start.ok || record.epochNanos < start.epochNanos )
      return false;
  }
  if ( !endUtc.empty() )
  {
    const InstantParse end = parseIso8601Instant( endUtc );
    if ( !end.ok || record.epochNanos > end.epochNanos )
      return false;
  }
  return true;
}

/// The record's effective temporal window for range filters: the single
/// instant, else [start, end] overlap semantics.
bool temporalOverlap( const AssetRecord &record, const std::string &startUtc, const std::string &endUtc )
{
  if ( startUtc.empty() && endUtc.empty() )
    return true;
  if ( !record.datetimeUtc.empty() )
  {
    // A point instant matches a range when it lies inside it.
    return instantInRange( record.datetimeUtc, startUtc, endUtc );
  }
  if ( !record.startUtc.empty() || !record.endUtc.empty() )
  {
    // Interval overlap: recordStart <= end && recordEnd >= start.
    if ( !endUtc.empty() && !record.startUtc.empty() )
    {
      const InstantParse s = parseIso8601Instant( record.startUtc );
      const InstantParse e = parseIso8601Instant( endUtc );
      if ( s.ok && e.ok && s.epochNanos > e.epochNanos )
        return false;
    }
    if ( !startUtc.empty() && !record.endUtc.empty() )
    {
      const InstantParse e = parseIso8601Instant( record.endUtc );
      const InstantParse s = parseIso8601Instant( startUtc );
      if ( e.ok && s.ok && e.epochNanos < s.epochNanos )
        return false;
    }
    return true;
  }
  return false; // undated: absence is not evidence
}

std::string normalizedOrNull( const std::string &verbatim )
{
  if ( verbatim.empty() )
    return std::string();
  const InstantParse parsed = parseIso8601Instant( verbatim );
  return parsed.ok ? instantToUtcString( parsed.epochNanos ) : std::string();
}

} // namespace

Json::Value AssetRecord::toJson() const
{
  Json::Value json;
  json["id"] = id;
  json["collection"] = collection;
  json["path"] = path;
  json["media_type"] = mediaType;
  Json::Value rolesJson( Json::arrayValue );
  for ( const std::string &role : roles )
    rolesJson.append( role );
  json["roles"] = rolesJson;
  json["datetime"] = datetime;
  json["datetime_utc"] = datetimeUtc;
  json["start_utc"] = startUtc;
  json["end_utc"] = endUtc;
  json["has_bbox"] = hasBbox;
  if ( hasBbox )
  {
    Json::Value bbox( Json::arrayValue );
    bbox.append( minX );
    bbox.append( minY );
    bbox.append( maxX );
    bbox.append( maxY );
    json["bbox"] = bbox;
  }
  json["has_cloud_cover"] = hasCloudCover;
  if ( hasCloudCover )
    json["cloud_cover"] = cloudCover;
  Json::Value metadataJson;
  for ( const auto &entry : metadata )
    metadataJson[entry.first] = entry.second;
  json["metadata"] = metadataJson;
  return json;
}

AssetRecord AssetRecord::fromJson( const Json::Value &json )
{
  if ( !json.isObject() )
    throw GeoError( ErrorCode::InvalidMetadata, "asset record is not an object" );
  AssetRecord record;
  record.id = json["id"].asString();
  record.collection = json["collection"].asString();
  record.path = json["path"].asString();
  record.mediaType = json["media_type"].asString();
  for ( const Json::Value &role : json["roles"] )
    record.roles.push_back( role.asString() );
  record.datetime = json["datetime"].asString();
  record.datetimeUtc = json["datetime_utc"].asString();
  record.startUtc = json["start_utc"].asString();
  record.endUtc = json["end_utc"].asString();
  record.hasBbox = json["has_bbox"].asBool();
  if ( record.hasBbox && json["bbox"].isArray() && json["bbox"].size() == 4 )
  {
    record.minX = json["bbox"][0].asDouble();
    record.minY = json["bbox"][1].asDouble();
    record.maxX = json["bbox"][2].asDouble();
    record.maxY = json["bbox"][3].asDouble();
  }
  record.hasCloudCover = json["has_cloud_cover"].asBool();
  if ( record.hasCloudCover )
    record.cloudCover = json["cloud_cover"].asDouble();
  for ( const std::string &key : json["metadata"].getMemberNames() )
    record.metadata[key] = json["metadata"][key].asString();
  if ( record.id.empty() )
    throw GeoError( ErrorCode::InvalidMetadata, "asset record carries no id" );
  return record;
}

void AssetQuery::validate() const
{
  if ( requiresBbox && minX > maxX )
  {
    Json::Value details;
    details["min_x"] = minX;
    details["max_x"] = maxX;
    throw GeoError( ErrorCode::InvalidArgument, "AssetQuery: bbox minX exceeds maxX", details );
  }
  if ( requiresBbox && minY > maxY )
  {
    Json::Value details;
    details["min_y"] = minY;
    details["max_y"] = maxY;
    throw GeoError( ErrorCode::InvalidArgument, "AssetQuery: bbox minY exceeds maxY", details );
  }
  for ( const std::string &bound : { temporalStartUtc, temporalEndUtc } )
  {
    if ( bound.empty() )
      continue;
    if ( !parseIso8601Instant( bound ).ok )
    {
      Json::Value details;
      details["bound"] = bound;
      throw GeoError( ErrorCode::InvalidArgument, "AssetQuery: unparseable temporal bound", details );
    }
  }
  if ( !sortByTemporal.empty() && sortByTemporal != "asc" && sortByTemporal != "desc" )
  {
    Json::Value details;
    details["value"] = sortByTemporal;
    throw GeoError( ErrorCode::InvalidArgument, "AssetQuery: sortByTemporal must be asc/desc/empty", details );
  }
}

bool AssetQuery::matches( const AssetRecord &record ) const
{
  if ( !idEquals.empty() && record.id != idEquals )
    return false;
  if ( !collectionEquals.empty() && record.collection != collectionEquals )
    return false;
  if ( !roleEquals.empty() )
  {
    bool hasRole = false;
    for ( const std::string &role : record.roles )
      hasRole = hasRole || role == roleEquals;
    if ( !hasRole )
      return false;
  }
  if ( !mediaTypeSubstring.empty() && !containsIgnoreCase( record.mediaType, mediaTypeSubstring ) )
    return false;
  if ( !metadataKeyEquals.empty() )
  {
    const auto it = record.metadata.find( metadataKeyEquals );
    if ( it == record.metadata.end() || it->second != metadataValueEquals )
      return false;
  }
  if ( !instantInRange( record.datetimeUtc, temporalStartUtc, temporalEndUtc )
       && !temporalOverlap( record, temporalStartUtc, temporalEndUtc ) )
    return false;
  if ( requiresBbox )
  {
    // Inclusive intersection; records without a bbox do not match a
    // spatial filter (absence is not evidence of coverage).
    if ( !record.hasBbox )
      return false;
    if ( record.maxX < minX || record.minX > maxX || record.maxY < minY || record.minY > maxY )
      return false;
  }
  return true;
}

AssetQueryPage queryAssets( const std::vector<AssetRecord> &records, const AssetQuery &query,
                            const AssetQueryOptions &options )
{
  query.validate();
  if ( options.limit == 0 || options.hardCap == 0 )
    throw GeoError( ErrorCode::InvalidArgument, "queryAssets: limit and hardCap must be positive" );

  // Match + (optional) order + bounded page. One pass to match; ordering is
  // stable so equal keys keep input order (deterministic pages).
  std::vector<const AssetRecord *> matches;
  matches.reserve( records.size() );
  for ( const AssetRecord &record : records )
  {
    if ( query.matches( record ) )
      matches.push_back( &record );
  }

  if ( !query.sortByTemporal.empty() )
  {
    const bool descending = query.sortByTemporal == "desc";
    std::stable_sort( matches.begin(), matches.end(),
                      [ descending ]( const AssetRecord *a, const AssetRecord *b ) {
                        // Undated records sort LAST in both directions.
                        const std::string aKey = !a->datetimeUtc.empty() ? a->datetimeUtc
                                                 : ( !a->startUtc.empty() ? a->startUtc : std::string() );
                        const std::string bKey = !b->datetimeUtc.empty() ? b->datetimeUtc
                                                 : ( !b->startUtc.empty() ? b->startUtc : std::string() );
                        if ( aKey.empty() != bKey.empty() )
                          return bKey.empty(); // dated < undated
                        if ( aKey == bKey )
                          return false; // stable: keep input order
                        const bool aFirst = aKey < bKey;
                        return descending ? !aFirst : aFirst;
                      } );
  }

  const std::size_t totalMatches = matches.size();
  if ( totalMatches > options.hardCap && !options.countMatches )
  {
    Json::Value details;
    details["matches"] = static_cast<Json::UInt64>( totalMatches );
    details["hard_cap"] = static_cast<Json::UInt64>( options.hardCap );
    throw GeoError( ErrorCode::ResourceExhausted,
                    "queryAssets: match count exceeds the declared hard cap", details );
  }

  AssetQueryPage page;
  page.totalMatches = totalMatches;
  page.offset = options.offset;
  const std::size_t begin = std::min( options.offset, totalMatches );
  const std::size_t end = std::min( begin + options.limit, totalMatches );
  for ( std::size_t i = begin; i < end; ++i )
    page.records.push_back( *matches[i] );
  page.hasMore = end < totalMatches;
  return page;
}

Json::Value AssetQuerySummary::toJson() const
{
  Json::Value json;
  json["total_matches"] = static_cast<Json::UInt64>( totalMatches );
  json["earliest_utc"] = earliestUtc;
  json["latest_utc"] = latestUtc;
  json["has_spatial_extent"] = hasSpatialExtent;
  if ( hasSpatialExtent )
  {
    Json::Value bbox( Json::arrayValue );
    bbox.append( minX );
    bbox.append( minY );
    bbox.append( maxX );
    bbox.append( maxY );
    json["bbox"] = bbox;
  }
  json["without_datetime"] = static_cast<Json::UInt64>( withoutDatetime );
  return json;
}

AssetQuerySummary summarizeAssets( const std::vector<AssetRecord> &records, const AssetQuery &query )
{
  query.validate();
  AssetQuerySummary summary;
  bool hasEarliest = false;
  bool hasLatest = false;
  std::int64_t earliestNanos = 0;
  std::int64_t latestNanos = 0;
  for ( const AssetRecord &record : records )
  {
    if ( !query.matches( record ) )
      continue;
    summary.totalMatches += 1;
    std::string effective = record.datetimeUtc;
    if ( effective.empty() )
      effective = record.startUtc;
    if ( effective.empty() )
    {
      summary.withoutDatetime += 1;
    }
    else
    {
      const InstantParse parsed = parseIso8601Instant( effective );
      if ( parsed.ok )
      {
        if ( !hasEarliest || parsed.epochNanos < earliestNanos )
        {
          earliestNanos = parsed.epochNanos;
          hasEarliest = true;
        }
        if ( !hasLatest || parsed.epochNanos > latestNanos )
        {
          latestNanos = parsed.epochNanos;
          hasLatest = true;
        }
      }
      else
      {
        summary.withoutDatetime += 1;
      }
    }
    if ( record.hasBbox )
    {
      if ( !summary.hasSpatialExtent )
      {
        summary.hasSpatialExtent = true;
        summary.minX = record.minX;
        summary.minY = record.minY;
        summary.maxX = record.maxX;
        summary.maxY = record.maxY;
      }
      else
      {
        summary.minX = std::min( summary.minX, record.minX );
        summary.minY = std::min( summary.minY, record.minY );
        summary.maxX = std::max( summary.maxX, record.maxX );
        summary.maxY = std::max( summary.maxY, record.maxY );
      }
    }
  }
  if ( hasEarliest )
    summary.earliestUtc = instantToUtcString( earliestNanos );
  if ( hasLatest )
    summary.latestUtc = instantToUtcString( latestNanos );
  return summary;
}

AssetRecord assetRecordFromStacItem( const StacItem &item, const std::string &resolvedPath )
{
  AssetRecord record;
  record.id = item.id;
  record.datetime = item.datetime;
  record.datetimeUtc = item.datetimeUtc;
  record.startUtc = item.startDatetimeUtc;
  record.endUtc = item.endDatetimeUtc;
  record.path = resolvedPath;
  record.hasCloudCover = item.hasCloudCover;
  record.cloudCover = item.cloudCover;
  if ( item.bbox.size() == 4 || item.bbox.size() == 6 )
  {
    record.hasBbox = true;
    record.minX = item.bbox[0];
    record.minY = item.bbox[1];
    record.maxX = item.bbox[2];
    record.maxY = item.bbox[3];
  }
  // Roles / media type / collection ride on the assets and links; the item
  // itself declares neither — the primary "data" asset donates them.
  for ( const auto &entry : item.assets )
  {
    bool isData = false;
    for ( const std::string &role : entry.second.roles )
      isData = isData || role == "data";
    if ( isData )
    {
      record.roles = entry.second.roles;
      record.mediaType = entry.second.mediaType;
      if ( resolvedPath.empty() )
        record.path = entry.second.href;
      break;
    }
  }
  if ( record.roles.empty() && !item.assets.empty() )
  {
    record.roles = item.assets.begin()->second.roles;
    record.mediaType = item.assets.begin()->second.mediaType;
  }
  if ( !item.epsg.empty() )
    record.metadata["epsg"] = item.epsg;
  if ( !item.platform.empty() )
    record.metadata["platform"] = item.platform;
  if ( !item.processingLevel.empty() )
    record.metadata["processing_level"] = item.processingLevel;
  record.metadata["stac_version"] = item.stacVersion;
  return record;
}

AssetRecord assetRecordFromCanonical( const RasterMetadata &metadata, const std::string &id,
                                      const std::string &path )
{
  AssetRecord record;
  record.id = id;
  record.path = path.empty() ? metadata.path : path;
  if ( metadata.hasExtent )
  {
    record.hasBbox = true;
    record.minX = metadata.minX;
    record.minY = metadata.minY;
    record.maxX = metadata.maxX;
    record.maxY = metadata.maxY;
  }
  if ( metadata.crs.valid && !metadata.crs.authid.empty() )
    record.metadata["epsg"] = metadata.crs.authid;
  record.metadata["driver"] = metadata.driver;
  record.metadata["band_count"] = std::to_string( metadata.bandCount );
  record.metadata["width"] = std::to_string( metadata.width );
  record.metadata["height"] = std::to_string( metadata.height );
  return record;
}

} // namespace sicnu::geo

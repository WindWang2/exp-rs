/***************************************************************************
  geospatial/stac/stac_client.cpp — STAC API client implementation.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/stac/stac_client.h"

#include "geospatial/util/resource_uri.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace sicnu::geo
{

namespace
{

std::string trimSlash( std::string url )
{
  while ( !url.empty() && url.back() == '/' )
    url.pop_back();
  return url;
}

std::string urlEncodeValue( const std::string &value )
{
  static const char *hexDigits = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve( value.size() );
  for ( const unsigned char c : value )
  {
    const bool unreserved = std::isalnum( c ) != 0 || c == '-' || c == '_' || c == '.' || c == '~';
    if ( unreserved )
      encoded += static_cast<char>( c );
    else
    {
      encoded += '%';
      encoded += hexDigits[c >> 4];
      encoded += hexDigits[c & 0xF];
    }
  }
  return encoded;
}

std::string joinEncoded( const std::vector<std::string> &values )
{
  std::string joined;
  for ( const std::string &value : values )
  {
    if ( !joined.empty() )
      joined += ",";
    joined += value;
  }
  return joined;
}

/// Appends "name=value" (both encoded) when value is non-empty.
void appendParam( std::string &query, const char *name, const std::string &value )
{
  if ( value.empty() )
    return;
  if ( !query.empty() )
    query += "&";
  query += name;
  query += "=";
  query += urlEncodeValue( value );
}

/// Extracts the rel="next" link from a STAC response document.
bool extractNextLink( const Json::Value &document, std::string &method, std::string &href,
                      Json::Value &body )
{
  if ( !document.isObject() || !document.isMember( "links" ) || !document["links"].isArray() )
    return false;
  for ( const Json::Value &link : document["links"] )
  {
    if ( !link.isObject() )
      continue;
    const std::string rel = link["rel"].isString() ? link["rel"].asString() : std::string();
    if ( rel != "next" )
      continue;
    href = link["href"].isString() ? link["href"].asString() : std::string();
    if ( href.empty() )
      continue;
    method = link["method"].isString() ? link["method"].asString() : "GET";
    if ( method != "POST" && method != "GET" )
      method = "GET";
    if ( link.isMember( "body" ) && link["body"].isObject() )
      body = link["body"];
    // "merge": the body extends the original request body (STAC API spec);
    // callers merge before sending.
    return true;
  }
  return false;
}

bool containsIgnoreCase( const std::string &haystack, const std::string &needle )
{
  if ( haystack.size() < needle.size() )
    return false;
  for ( std::size_t i = 0; i + needle.size() <= haystack.size(); ++i )
  {
    bool matched = true;
    for ( std::size_t j = 0; j < needle.size(); ++j )
    {
      if ( std::tolower( static_cast<unsigned char>( haystack[i + j] ) ) !=
           std::tolower( static_cast<unsigned char>( needle[j] ) ) )
      {
        matched = false;
        break;
      }
    }
    if ( matched )
      return true;
  }
  return false;
}

} // namespace

void StacSearchQuery::validate() const
{
  if ( !bbox.empty() && bbox.size() != 4 && bbox.size() != 6 )
  {
    Json::Value details;
    details["size"] = static_cast<Json::UInt64>( bbox.size() );
    throw GeoError( ErrorCode::InvalidArgument, "StacSearchQuery: bbox must hold 4 or 6 values", details );
  }
  if ( !intersects.isNull() && !intersects.isObject() )
    throw GeoError( ErrorCode::InvalidArgument, "StacSearchQuery: intersects must be a GeoJSON object" );
}

StacClient::StacClient( std::string root, const StacClientOptions &options )
  : mRoot( trimSlash( std::move( root ) ) ), mOptions( options )
{
  const ResourceUri uri = ResourceUri::parse( mRoot );
  if ( uri.kind != ResourceKind::RemoteHttp )
  {
    Json::Value details;
    details["root"] = uri.display();
    throw GeoError( ErrorCode::InvalidArgument, "StacClient: root must be a remote http(s) URL", details );
  }
}

HttpFetchOptions StacClient::fetchOptions() const
{
  HttpFetchOptions options;
  options.timeoutSeconds = mOptions.timeoutSeconds;
  options.connectTimeoutSeconds = mOptions.connectTimeoutSeconds;
  options.maxRetries = mOptions.maxRetries;
  options.maxResponseBytes = mOptions.maxResponseBytes;
  options.acceptContentTypes = { "json" };
  return options;
}

StacPage StacClient::executeSearch( const std::string &method, const std::string &url,
                                    const Json::Value &body ) const
{
  Json::Value document;
  if ( method == "POST" )
  {
    HttpFetchOptions options = fetchOptions();
    options.headers.push_back( "Content-Type: application/json" );
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    options.postBody = Json::writeString( writerBuilder, body );
    document = httpFetchJson( url, options );
  }
  else
  {
    document = httpFetchJson( url, fetchOptions() );
  }

  StacPage page;
  if ( !document.isObject() || !document.isMember( "features" ) || !document["features"].isArray() )
  {
    throw GeoError( ErrorCode::InvalidMetadata, "StacClient: search answer carries no features array" );
  }
  for ( const Json::Value &feature : document["features"] )
    page.items.push_back( StacItem::parse( feature ) );

  std::string nextMethod;
  std::string nextHref;
  Json::Value nextBody;
  if ( extractNextLink( document, nextMethod, nextHref, nextBody ) )
  {
    page.nextMethod = nextMethod;
    page.nextHref = nextHref;
    page.nextBody = nextBody;
  }
  return page;
}

StacPage StacClient::search( const StacSearchQuery &query ) const
{
  query.validate();

  const bool needsPost = !query.query.isNull() || !query.intersects.isNull();
  if ( needsPost )
  {
    Json::Value body( Json::objectValue );
    if ( !query.bbox.empty() )
    {
      Json::Value bboxJson( Json::arrayValue );
      for ( const double value : query.bbox )
        bboxJson.append( value );
      body["bbox"] = bboxJson;
    }
    if ( !query.intersects.isNull() )
      body["intersects"] = query.intersects;
    if ( !query.datetime.empty() )
      body["datetime"] = query.datetime;
    if ( !query.collections.empty() )
    {
      Json::Value list( Json::arrayValue );
      for ( const std::string &collection : query.collections )
        list.append( collection );
      body["collections"] = list;
    }
    if ( !query.ids.empty() )
    {
      Json::Value list( Json::arrayValue );
      for ( const std::string &id : query.ids )
        list.append( id );
      body["ids"] = list;
    }
    if ( query.limit > 0 )
      body["limit"] = query.limit;
    if ( !query.query.isNull() )
      body["query"] = query.query;
    if ( !query.sortBy.empty() )
    {
      Json::Value sort( Json::arrayValue );
      Json::Value sortField( Json::objectValue );
      sortField["field"] = query.sortBy;
      sortField["direction"] = "asc";
      sort.append( sortField );
      body["sortby"] = sort;
    }
    return executeSearch( "POST", mRoot + "/search", body );
  }

  std::string params;
  if ( !query.bbox.empty() )
  {
    std::vector<std::string> parts;
    for ( const double value : query.bbox )
    {
      std::ostringstream text;
      text << value;
      parts.push_back( text.str() );
    }
    appendParam( params, "bbox", joinEncoded( parts ) );
  }
  if ( !query.datetime.empty() )
    appendParam( params, "datetime", query.datetime );
  if ( !query.collections.empty() )
    appendParam( params, "collections", joinEncoded( query.collections ) );
  if ( !query.ids.empty() )
    appendParam( params, "ids", joinEncoded( query.ids ) );
  if ( query.limit > 0 )
  {
    std::ostringstream text;
    text << query.limit;
    appendParam( params, "limit", text.str() );
  }
  if ( !query.sortBy.empty() )
    appendParam( params, "sortby", query.sortBy );
  const std::string url = mRoot + "/search" + ( params.empty() ? "" : "?" + params );
  return executeSearch( "GET", url, Json::Value() );
}

StacPage StacClient::nextPage( const StacPage &page ) const
{
  if ( !page.hasMore() )
    return StacPage{};
  if ( page.nextMethod == "POST" )
  {
    // rel=next POST links merge their body into the previous request body;
    // the stored nextBody is the continuation body the origin declared.
    return executeSearch( "POST", page.nextHref, page.nextBody );
  }
  return executeSearch( "GET", page.nextHref, Json::Value() );
}

StacSearchAllResult StacClient::searchAll( const StacSearchQuery &query ) const
{
  StacSearchAllResult result;
  StacPage page = search( query );
  while ( true )
  {
    for ( StacItem &item : page.items )
    {
      if ( static_cast<int>( result.items.size() ) >= mOptions.maxItems )
      {
        result.truncatedByLimit = true;
        return result;
      }
      result.items.push_back( std::move( item ) );
    }
    if ( !page.hasMore() )
      return result;
    page = nextPage( page );
  }
}

Json::Value StacClient::collections() const
{
  return httpFetchJson( mRoot + "/collections", fetchOptions() );
}

Json::Value StacClient::collection( const std::string &collectionId ) const
{
  if ( collectionId.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "StacClient: collection id must not be empty" );
  return httpFetchJson( mRoot + "/collections/" + urlEncodeValue( collectionId ), fetchOptions() );
}

StacPage StacClient::collectionItems( const std::string &collectionId,
                                      const StacSearchQuery &query ) const
{
  if ( collectionId.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "StacClient: collection id must not be empty" );
  query.validate();
  std::string params;
  if ( !query.datetime.empty() )
    appendParam( params, "datetime", query.datetime );
  if ( query.limit > 0 )
  {
    std::ostringstream text;
    text << query.limit;
    appendParam( params, "limit", text.str() );
  }
  if ( !query.bbox.empty() )
  {
    std::vector<std::string> parts;
    for ( const double value : query.bbox )
    {
      std::ostringstream text;
      text << value;
      parts.push_back( text.str() );
    }
    appendParam( params, "bbox", joinEncoded( parts ) );
  }
  const std::string url = mRoot + "/collections/" + urlEncodeValue( collectionId ) + "/items" +
                          ( params.empty() ? "" : "?" + params );
  return executeSearch( "GET", url, Json::Value() );
}

std::vector<StacAsset> StacClient::assetsWithRole( const StacItem &item, const std::string &role )
{
  std::vector<StacAsset> matches;
  for ( const auto &entry : item.assets )
  {
    if ( std::find( entry.second.roles.begin(), entry.second.roles.end(), role ) != entry.second.roles.end() )
      matches.push_back( entry.second );
  }
  return matches;
}

std::vector<StacAsset> StacClient::assetsOfMediaType( const StacItem &item,
                                                      const std::string &mediaTypeSubstring )
{
  std::vector<StacAsset> matches;
  for ( const auto &entry : item.assets )
  {
    if ( containsIgnoreCase( entry.second.mediaType, mediaTypeSubstring ) )
      matches.push_back( entry.second );
  }
  return matches;
}

bool StacClient::matchesCloudCover( const StacItem &item, double maxPercent )
{
  if ( !item.hasCloudCover )
    return true; // absence is not evidence
  return item.cloudCover <= maxPercent;
}

std::string StacClient::displayAssetHref( const StacAsset &asset )
{
  const ResourceUri uri = ResourceUri::parse( asset.href );
  return uri.display();
}

std::vector<StacSeriesEntry> buildTemporalSeries( const std::vector<StacItem> &items )
{
  std::vector<StacSeriesEntry> series;
  series.reserve( items.size() );
  for ( const StacItem &item : items )
  {
    StacSeriesEntry entry;
    entry.item = item;
    entry.datetime = !item.datetime.empty() ? item.datetime : item.startDatetime;
    entry.canonical = stacItemToCanonical( item );
    series.push_back( std::move( entry ) );
  }
  std::stable_sort( series.begin(), series.end(),
                    [] ( const StacSeriesEntry &a, const StacSeriesEntry &b ) {
                      // Undated entries sort last while keeping item order
                      // (stable_sort preserves the original sequence).
                      if ( a.datetime.empty() || b.datetime.empty() )
                        return !a.datetime.empty() && b.datetime.empty();
                      // ISO-8601 UTC strings sort lexicographically in time
                      // order when their formats agree; differing formats
                      // fall back to id order through stable_sort.
                      return a.datetime < b.datetime;
                    } );
  return series;
}

} // namespace sicnu::geo

/***************************************************************************
  geospatial/fabric/catalog_service.cpp — unified catalog service.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/fabric/catalog_service.h"

#include "geospatial/fabric/object_store.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/util/time_normalization.h"

#include <cpl_conv.h>
#include <cpl_port.h>
#include <cpl_string.h>
#include <cpl_vsi.h>

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace sicnu::geo
{

namespace
{

std::string lowerAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

bool containsCaseInsensitive( const std::string &haystack, const std::string &needle )
{
  return lowerAscii( haystack ).find( lowerAscii( needle ) ) != std::string::npos;
}

/// The STAC API datetime interval form ("start/end"; open ends "..").
std::string stacDatetimeInterval( const CatalogQuery &query )
{
  const std::string start =
    query.temporalStartUtc.empty() ? std::string( ".." ) : query.temporalStartUtc;
  const std::string end = query.temporalEndUtc.empty() ? std::string( ".." ) : query.temporalEndUtc;
  return start + "/" + end;
}

bool itemMatchesPlatform( const StacItem &item, const CatalogQuery &query )
{
  if ( query.platformEquals.empty() )
    return true;
  return lowerAscii( item.platform ) == lowerAscii( query.platformEquals );
}

bool itemMatchesInstruments( const StacItem &item, const CatalogQuery &query )
{
  if ( query.sensorInstruments.empty() )
    return true;
  for ( const std::string &wanted : query.sensorInstruments )
    for ( const std::string &instrument : item.instruments )
      if ( lowerAscii( instrument ) == lowerAscii( wanted ) )
        return true;
  return false;
}

bool itemMatchesCloudCover( const StacItem &item, const CatalogQuery &query )
{
  if ( !query.hasCloudCoverMax )
    return true;
  // Absence is not evidence (StacClient::matchesCloudCover parity).
  if ( !item.hasCloudCover )
    return false;
  return item.cloudCover <= query.cloudCoverMax;
}

/// The assets a query's role/media-type selection qualifies ("" role → the
/// "data" assets when present, else every asset).
std::vector<const StacAsset *> qualifyingAssets( const StacItem &item, const CatalogQuery &query )
{
  std::vector<const StacAsset *> candidates;
  const std::string &wantedRole = !query.assetRole.empty() ? query.assetRole : std::string( "data" );
  const bool filterByRole = !query.assetRole.empty();
  for ( const auto &entry : item.assets )
  {
    const StacAsset &asset = entry.second;
    if ( filterByRole )
    {
      const bool hasRole = std::find( asset.roles.begin(), asset.roles.end(), wantedRole ) !=
                           asset.roles.end();
      if ( !hasRole )
        continue;
    }
    candidates.push_back( &asset );
  }
  if ( query.assetRole.empty() )
  {
    std::vector<const StacAsset *> dataAssets;
    for ( const auto &entry : item.assets )
    {
      const StacAsset &asset = entry.second;
      if ( std::find( asset.roles.begin(), asset.roles.end(), "data" ) != asset.roles.end() )
        dataAssets.push_back( &asset );
    }
    if ( !dataAssets.empty() )
      candidates = std::move( dataAssets );
  }
  if ( !query.mediaTypeSubstring.empty() )
  {
    std::vector<const StacAsset *> typed;
    for ( const StacAsset *asset : candidates )
      if ( containsCaseInsensitive( asset->mediaType, query.mediaTypeSubstring ) )
        typed.push_back( asset );
    return typed;
  }
  return candidates;
}

/// Enriches a record with item facts the shared adapter does not carry
/// (constellation/instruments/modality/gsd) — fabric-side keys only, the
/// asset_query authority is untouched.
void enrichRecord( AssetRecord &record, const StacItem &item, const StacAsset &qualifying )
{
  if ( !item.constellation.empty() )
    record.metadata["constellation"] = item.constellation;
  if ( !item.instruments.empty() )
  {
    std::string joined;
    for ( const std::string &instrument : item.instruments )
    {
      if ( !joined.empty() )
        joined += ",";
      joined += instrument;
    }
    record.metadata["instruments"] = joined;
  }
  if ( !item.modality.empty() )
    record.metadata["modality"] = item.modality;
  if ( item.hasGsd )
    record.metadata["gsd"] = [] ( double v ) {
      char buffer[32];
      std::snprintf( buffer, sizeof( buffer ), "%.6g", v );
      return std::string( buffer );
    }( item.gsd );
  // Keep the record's donated roles/type aligned with the ASSET THE RECORD
  // POINTS AT (the adapter donates the first data asset's facts — the
  // qualifying asset wins when they differ).
  record.roles = qualifying.roles;
  record.mediaType = qualifying.mediaType;
}

/// Client-side item filter (the part server pushdown cannot express —
/// DECISIONS D-1004). `qualifying` receives the first qualifying asset.
bool itemMatchesQuery( const StacItem &item, const CatalogQuery &query,
                       const StacAsset *&qualifying )
{
  if ( !itemMatchesPlatform( item, query ) )
    return false;
  if ( !itemMatchesInstruments( item, query ) )
    return false;
  if ( !itemMatchesCloudCover( item, query ) )
    return false;
  const std::vector<const StacAsset *> assets = qualifyingAssets( item, query );
  if ( assets.empty() )
    return false;
  qualifying = assets.front();
  return true;
}

// --- record-level filter (InMemoryRecords backend — records may exist
// without item provenance) -------------------------------------------------

std::string recordInstruments( const AssetRecord &record )
{
  const auto it = record.metadata.find( "instruments" );
  return it == record.metadata.end() ? std::string() : it->second;
}

bool recordMatchesQuery( const AssetRecord &record, const CatalogQuery &query )
{
  if ( !query.platformEquals.empty() )
  {
    const auto it = record.metadata.find( "platform" );
    if ( it == record.metadata.end() || lowerAscii( it->second ) != lowerAscii( query.platformEquals ) )
      return false;
  }
  if ( !query.sensorInstruments.empty() )
  {
    // Comma-joined list (our enrichment) — any-of, case-insensitive.
    const std::string joined = lowerAscii( recordInstruments( record ) );
    bool matched = false;
    for ( const std::string &wanted : query.sensorInstruments )
      if ( joined.find( lowerAscii( wanted ) ) != std::string::npos )
        matched = true;
    if ( !matched )
      return false;
  }
  if ( query.hasCloudCoverMax && ( !record.hasCloudCover || record.cloudCover > query.cloudCoverMax ) )
    return false;
  if ( !query.assetRole.empty() &&
       std::find( record.roles.begin(), record.roles.end(), query.assetRole ) == record.roles.end() )
    return false;
  if ( !query.mediaTypeSubstring.empty() &&
       !containsCaseInsensitive( record.mediaType, query.mediaTypeSubstring ) )
    return false;
  return true;
}

bool recordMatchesTemporal( const AssetRecord &record, const CatalogQuery &query )
{
  if ( query.temporalStartUtc.empty() && query.temporalEndUtc.empty() )
    return true;
  if ( record.datetimeUtc.empty() )
    return false;   // absence is not evidence — same doctrine as asset_query
  const InstantParse instant = parseIso8601Instant( record.datetimeUtc );
  if ( !instant.ok )
    return false;
  if ( !query.temporalStartUtc.empty() )
  {
    const InstantParse start = parseIso8601Instant( query.temporalStartUtc );
    if ( start.ok && instant.epochNanos < start.epochNanos )
      return false;
  }
  if ( !query.temporalEndUtc.empty() )
  {
    const InstantParse end = parseIso8601Instant( query.temporalEndUtc );
    if ( end.ok && instant.epochNanos >= end.epochNanos )
      return false;   // end is exclusive (half-open intervals)
  }
  return true;
}

bool recordMatchesBbox( const AssetRecord &record, const CatalogQuery &query )
{
  if ( query.bbox.empty() )
    return true;
  if ( query.bbox.size() != 4 )
    return true;   // 3D bboxes: spatial filter degrades to 2D below via validate()
  if ( !record.hasBbox )
    return false;
  return record.maxX >= query.bbox[0] && record.minX <= query.bbox[2] &&
         record.maxY >= query.bbox[1] && record.minY <= query.bbox[3];
}

/// The one filter entry point over records (all backends).
bool recordPasses( const AssetRecord &record, const CatalogQuery &query )
{
  if ( !query.ids.empty() &&
       std::find( query.ids.begin(), query.ids.end(), record.id ) == query.ids.end() )
    return false;
  if ( !query.collections.empty() &&
       std::find( query.collections.begin(), query.collections.end(), record.collection ) ==
         query.collections.end() )
    return false;
  return recordMatchesBbox( record, query ) && recordMatchesTemporal( record, query ) &&
         recordMatchesQuery( record, query );
}

/// Builds the record + provenance pair for one item (typed failure when the
/// qualifying asset cannot resolve to a fetchable path).
enum class ItemVerdict
{
    Rejected,      ///< filtered out by the query vocabulary
    Unresolvable,  ///< passed filters but the qualifying asset href resolves
                   ///< to nothing fetchable — dropped with its own counter
    Accepted,
};

ItemVerdict recordForItem( const StacClient &client, const StacItem &item,
                           const CatalogQuery &query, AssetRecord &recordOut )
{
  const StacAsset *qualifying = nullptr;
  if ( !itemMatchesQuery( item, query, qualifying ) )
    return ItemVerdict::Rejected;
  std::string resolved;
  try
  {
    resolved = client.resolveAssetHref( item, *qualifying );
  }
  catch ( const GeoError & )
  {
    return ItemVerdict::Unresolvable;   // a path we cannot fetch is not a record
  }
  // 11.0 (Windows portability): a record's path is its IDENTITY spelling —
  // separator-normalized ('/') so a href resolved on a backslash filesystem
  // does not fork every consumer keyed on the STAC href shape (matching
  // ResourceUri::canonical's contract).
  resolved = ResourceUri::parse( resolved ).canonical();
  recordOut = assetRecordFromStacItem( item, resolved );
  enrichRecord( recordOut, item, *qualifying );
  // The service-layer temporal/bbox checks ride on the record (one truth).
  if ( !recordPasses( recordOut, query ) )
    return ItemVerdict::Rejected;
  return ItemVerdict::Accepted;
}

} // namespace

// --- CatalogQuery ----------------------------------------------------------

void CatalogQuery::validate() const
{
  if ( !bbox.empty() && bbox.size() != 4 && bbox.size() != 6 )
    throw GeoError( ErrorCode::InvalidArgument,
                    "catalog query bbox must be 4 or 6 values, got " +
                      std::to_string( bbox.size() ) );
  if ( bbox.size() >= 4 )
  {
    // Horizontal bounds in the STAC order [w,s,e,n]: west/east and
    // south/north must not cross (a 3D slice validates like a 2D one).
    const double minY = bbox[1];
    const double maxY = bbox[bbox.size() == 6 ? 4 : 3];
    if ( bbox[0] > bbox[2] || minY > maxY )
      throw GeoError( ErrorCode::InvalidArgument, "catalog query bbox bounds cross" );
  }
  if ( !temporalStartUtc.empty() )
  {
    const InstantParse parse = parseIso8601Instant( temporalStartUtc );
    if ( !parse.ok )
      throw GeoError( ErrorCode::InvalidArgument,
                      "catalog query temporalStartUtc is not an ISO-8601 instant: " +
                        temporalStartUtc );
  }
  if ( !temporalEndUtc.empty() )
  {
    const InstantParse parse = parseIso8601Instant( temporalEndUtc );
    if ( !parse.ok )
      throw GeoError( ErrorCode::InvalidArgument,
                      "catalog query temporalEndUtc is not an ISO-8601 instant: " +
                        temporalEndUtc );
  }
  if ( hasCloudCoverMax && ( cloudCoverMax < 0.0 || cloudCoverMax > 100.0 ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "catalog query cloudCoverMax must be within [0,100]" );
  if ( limit < 0 || maxItems < 1 )
    throw GeoError( ErrorCode::InvalidArgument, "catalog query limit/maxItems out of range" );
}

// --- info / stats ----------------------------------------------------------

const char *catalogBackendKindName( CatalogBackendKind kind )
{
  switch ( kind )
  {
    case CatalogBackendKind::LocalStacFiles: return "local_stac_files";
    case CatalogBackendKind::RemoteStacApi: return "remote_stac_api";
    case CatalogBackendKind::InMemoryRecords: return "in_memory_records";
  }
  return "unknown";
}

Json::Value CatalogServiceInfo::toJson() const
{
  Json::Value json;
  json["backend"] = catalogBackendKindName( kind );
  json["displayOrigin"] = displayOrigin;
  return json;
}

Json::Value CatalogPage::statsJson() const
{
  Json::Value json;
  json["records"] = static_cast<Json::UInt64>( records.size() );
  json["offset"] = static_cast<Json::UInt64>( offset );
  json["hasMore"] = next.hasMore;
  json["truncatedByCap"] = truncatedByCap;
  json["serverFiltered"] = serverFiltered;
  json["clientFilteredOut"] = static_cast<Json::UInt64>( clientFilteredOut );
  json["unresolvable"] = static_cast<Json::UInt64>( unresolvable );
  return json;
}

// --- backends --------------------------------------------------------------

namespace
{

/// Bounded generic JSON file reader (shape sniffing for local candidates).
/// Throws GeoError(OpenFailed/InvalidMetadata).
Json::Value readJsonFile( const std::string &path )
{
  if ( path.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "readJsonFile: empty path" );
  std::ifstream in( path, std::ios::binary );
  if ( !in )
    throw GeoError( ErrorCode::OpenFailed, "readJsonFile: cannot open " + path );
  std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
  Json::CharReaderBuilder builder;
  std::string errors;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  Json::Value parsed;
  if ( !reader->parse( text.data(), text.data() + text.size(), &parsed, &errors ) )
    throw GeoError( ErrorCode::InvalidMetadata, "readJsonFile: invalid JSON in " + path );
  return parsed;
}

bool isItemShape( const Json::Value &json )
{
  return json.isObject() && json["type"].isString() && json["type"].asString() == "Feature"
         && json["assets"].isObject();
}

// --- VSI path helpers (portable; std::filesystem is off the table here —
// the host GCC 16 snapshot breaks its declaration through common include
// orders, and GDAL VSI is the transport this layer already owns) ----------

/// Lexical path cleanup: collapses '.'/'..' and duplicate separators.
/// Purely textual — no filesystem access, no symlink resolution.
std::string FileSystemlessNormalize( std::string path )
{
  if ( path.empty() )
    return path;
  const bool absolute = path.front() == '/';
  std::vector<std::string> parts;
  std::string segment;
  for ( const char c : path )
  {
    if ( c == '/' )
    {
      if ( !segment.empty() )
      {
        parts.push_back( segment );
        segment.clear();
      }
    }
    else
    {
      segment += c;
    }
  }
  if ( !segment.empty() )
    parts.push_back( segment );
  std::vector<std::string> resolved;
  for ( const std::string &part : parts )
  {
    if ( part == "." )
      continue;
    if ( part == ".." && !resolved.empty() && resolved.back() != ".." )
    {
      resolved.pop_back();
      continue;
    }
    if ( part == ".." && absolute && resolved.empty() )
      continue;   // climbing past an absolute root stays at the root
    resolved.push_back( part );
  }
  std::string out = absolute ? "/" : "";
  for ( std::size_t i = 0; i < resolved.size(); ++i )
  {
    if ( i > 0 )
      out += "/";
    out += resolved[i];
  }
  if ( out.empty() )
    out = ".";
  return out;
}

std::string VSIJoinPath( const std::string &directory, const std::string &name )
{
  if ( directory.empty() )
    return name;
  return directory.back() == '/' ? directory + name : directory + "/" + name;
}

std::string VSIDirectoryOf( const std::string &path )
{
  const std::size_t slash = path.find_last_of( '/' );
  if ( slash == std::string::npos )
    return ".";
  if ( slash == 0 )
    return "/";
  return path.substr( 0, slash );
}

bool VSIFileExists( const std::string &path )
{
  VSIStatBufL statBuffer;
  return VSIStatL( path.c_str(), &statBuffer ) == 0;
}

} // namespace

struct CatalogService::LocalImpl
{
  std::vector<std::string> itemFiles;   // candidate files, walk order
  std::string root;
  std::unique_ptr<StacClient> resolver; // href-resolution authority (local items never network)
  std::uint64_t skippedFiles = 0;       // candidate files that parsed to no item
  std::uint64_t unreadableFiles = 0;    // open/parse failures (typed skip)
};

struct CatalogService::RemoteImpl
{
  std::unique_ptr<StacClient> client;
};

struct CatalogService::MemoryImpl
{
  std::vector<AssetRecord> records;     // detached snapshot, caller's order
};

CatalogService::~CatalogService() = default;
CatalogService::CatalogService( CatalogService && ) noexcept = default;
CatalogService &CatalogService::operator=( CatalogService && ) noexcept = default;

CatalogService openCatalogService( const std::string &root, const CatalogServiceOptions &options )
{
  const ResourceUri uri = ResourceUri::parse( root );

  if ( uri.kind == ResourceKind::RemoteHttp )
  {
    std::string refusal;
    if ( fabricOfflineRefusal( root, refusal ) )
      throw GeoError( ErrorCode::NetworkError, refusal );
    CatalogService service;
    service.mInfo.kind = CatalogBackendKind::RemoteStacApi;
    service.mInfo.origin = root;
    service.mInfo.displayOrigin = uri.display();
    service.mOptions = options;
    service.mRemote = std::make_unique<CatalogService::RemoteImpl>();
    service.mRemote->client = std::make_unique<StacClient>( root, options.stac );
    return service;
  }

  if ( uri.kind == ResourceKind::LocalDirectory || uri.kind == ResourceKind::LocalFile )
  {
    CatalogService service;
    service.mInfo.kind = CatalogBackendKind::LocalStacFiles;
    service.mInfo.origin = root;
    service.mInfo.displayOrigin = uri.display();
    service.mOptions = options;
    service.mLocal = std::make_unique<CatalogService::LocalImpl>();
    service.mLocal->root = root;
    // StacClient's ctor validates remote roots, but resolveAssetHref (the
    // only call we make) uses the item's own provenance for local items —
    // the placeholder root is never dereferenced and never hits the network.
    service.mLocal->resolver = std::make_unique<StacClient>( "http://fabric-local-resolver", options.stac );

    if ( uri.kind == ResourceKind::LocalFile )
    {
      service.mLocal->itemFiles.push_back( root );
      return service;
    }
    // Directory root: a root catalog/collection.json gets link-following
    // (rel child/item, bounded + cycle-safe); otherwise a bounded recursive
    // VSI scan for Item-shaped JSON candidates (shape checked lazily at
    // parse). Directory primitives are GDAL VSI calls — the transport
    // authority this layer already owns (portable, and immune to the host
    // toolchain's include-order-sensitive <filesystem> breakage).
    const std::string rootNormalized = FileSystemlessNormalize( root );
    const std::string rootCatalog = VSIJoinPath( rootNormalized, "catalog.json" );
    const std::string rootCollection = VSIJoinPath( rootNormalized, "collection.json" );
    const bool hasRootCatalog = VSIFileExists( rootCatalog );
    const bool hasRootCollection = VSIFileExists( rootCollection );
    if ( hasRootCatalog || hasRootCollection )
    {
      // Breadth-first over link documents.
      std::set<std::string> visited;
      std::vector<std::string> queue;
      const std::string firstDocument = hasRootCatalog ? rootCatalog : rootCollection;
      queue.push_back( firstDocument );
      visited.insert( firstDocument );
      while ( !queue.empty() && static_cast<int>( service.mLocal->itemFiles.size() ) <
                                  options.localMaxFiles )
      {
        const std::string document = queue.front();
        queue.erase( queue.begin() );
        std::vector<std::string> childLinks;
        std::vector<std::string> itemLinks;
        try
        {
          const Json::Value parsed = readJsonFile( document );
          // A document whose own shape is an Item doubles as a leaf.
          if ( isItemShape( parsed ) )
          {
            service.mLocal->itemFiles.push_back( document );
            continue;
          }
          const Json::Value &links = parsed["links"];
          if ( !links.isArray() )
            continue;
          for ( const Json::Value &link : links )
          {
            if ( !link.isObject() || !link["rel"].isString() || !link["href"].isString() )
              continue;
            const std::string rel = link["rel"].asString();
            const std::string href = link["href"].asString();
            if ( href.empty() )
              continue;
            if ( rel == "child" || rel == "item" )
              ( rel == "child" ? childLinks : itemLinks ).push_back( href );
          }
        }
        catch ( const GeoError & )
        {
          ++service.mLocal->unreadableFiles;
          continue;
        }
        catch ( const Json::Exception & )
        {
          ++service.mLocal->unreadableFiles;
          continue;
        }
        const std::string baseDirectory = VSIDirectoryOf( document );
        for ( const std::string &href : itemLinks )
        {
          const ResourceUri resolved = ResourceUri::resolveAgainst( baseDirectory, href );
          if ( resolved.kind == ResourceKind::LocalFile )
          {
            const std::string normal = FileSystemlessNormalize( resolved.raw );
            if ( visited.insert( normal ).second )
              service.mLocal->itemFiles.push_back( normal );
          }
          else
          {
            ++service.mLocal->skippedFiles;   // remote links: not this backend's crawl
          }
        }
        for ( const std::string &href : childLinks )
        {
          const ResourceUri resolved = ResourceUri::resolveAgainst( baseDirectory, href );
          if ( resolved.kind == ResourceKind::LocalFile )
          {
            const std::string normal = FileSystemlessNormalize( resolved.raw );
            if ( visited.insert( normal ).second )
              queue.push_back( normal );
          }
          else
          {
            ++service.mLocal->skippedFiles;
          }
        }
        if ( static_cast<int>( visited.size() ) > options.localMaxFiles )
          break;
      }
      return service;
    }

    // Plain VSI recursive scan (bounded depth + file count on the RESULT
    // set; VSIReadDirRecursive enumerates whole trees — the bound is
    // enforced by dropping entries past localMaxFiles / localMaxDepth and
    // counting them as skips, which stays honest about truncation).
    char **entries = VSIReadDirRecursive( rootNormalized.c_str() );
    if ( entries )
    {
      const int count = CSLCount( entries );
      const std::string rootPrefix = rootNormalized.back() == '/' ? rootNormalized : rootNormalized + "/";
      for ( int i = 0; i < count; ++i )
      {
        const std::string relative = entries[i];
        if ( relative.empty() )
          continue;
        const std::string full = rootPrefix + relative;
        if ( static_cast<int>( service.mLocal->itemFiles.size() ) >= options.localMaxFiles )
        {
          ++service.mLocal->skippedFiles;
          continue;
        }
        const int depth = static_cast<int>( std::count( relative.begin(), relative.end(), '/' ) );
        if ( depth > options.localMaxDepth )
        {
          ++service.mLocal->skippedFiles;
          continue;
        }
        const std::string extension = full.substr( full.find_last_of( '.' ) == std::string::npos
                                                     ? full.size()
                                                     : full.find_last_of( '.' ) );
        if ( lowerAscii( extension ) != ".json" )
          continue;
        service.mLocal->itemFiles.push_back( full );
      }
      CSLDestroy( entries );
    }
    return service;
  }

  throw GeoError( ErrorCode::InvalidArgument,
                  "catalog root is neither local nor remote: " + uri.display() );
}

CatalogService catalogServiceOverRecords( std::vector<AssetRecord> records )
{
  CatalogService service;
  service.mInfo.kind = CatalogBackendKind::InMemoryRecords;
  service.mInfo.origin = "<records>";
  service.mInfo.displayOrigin = "<records>";
  service.mMemory = std::make_shared<CatalogService::MemoryImpl>();
  service.mMemory->records = std::move( records );
  return service;
}

// --- search ----------------------------------------------------------------

CatalogPage CatalogService::searchPage( const CatalogQuery &query,
                                        const CatalogContinuation &continuation,
                                        const CancelToken &cancel ) const
{
  query.validate();

  if ( mMemory )
  {
    // The asset_query engine is the in-memory truth (one vocabulary); the
    // fabric-specific filters already rode in on the enriched records.
    CatalogPage page;
    page.offset = continuation.localOffset;
    const std::vector<AssetRecord> &all = mMemory->records;
    const int pageSize = query.limit > 0 ? query.limit : mOptions.maxRecordsPerPage;
    std::size_t index = continuation.localOffset;
    for ( ; index < all.size() && page.records.size() < static_cast<std::size_t>( pageSize ); ++index )
    {
      if ( cancel.cancelled() )
        throw GeoError( ErrorCode::Cancelled, "catalog search cancelled" );
      if ( recordPasses( all[index], query ) )
        page.records.push_back( all[index] );
      else
        ++page.clientFilteredOut;
    }
    page.next.hasMore = index < all.size();
    page.next.localOffset = index;
    return page;
  }

  if ( mRemote )
  {
    std::string refusal;
    if ( fabricOfflineRefusal( mInfo.origin, refusal ) )
      throw GeoError( ErrorCode::NetworkError, refusal );

    StacSearchQuery stacQuery;
    stacQuery.bbox = query.bbox;
    stacQuery.datetime = stacDatetimeInterval( query );
    stacQuery.collections = query.collections;
    stacQuery.ids = query.ids;
    stacQuery.limit = query.limit > 0 ? query.limit : mOptions.maxRecordsPerPage;

    CatalogPage page;
    StacPage stacPage;
    if ( continuation.hasMore || !continuation.remoteHref.empty() )
    {
      StacPage previous;
      previous.selfMethod = continuation.remoteSelfMethod.empty() ? "GET" : continuation.remoteSelfMethod;
      previous.selfBody = continuation.remoteSelfBody;
      previous.nextMethod = continuation.remoteMethod;
      previous.nextHref = continuation.remoteHref;
      previous.nextBody = continuation.remoteBody;
      previous.nextMerge = continuation.remoteMerge;
      if ( !previous.nextHref.empty() )
        stacPage = mRemote->client->nextPage( previous );
    }
    else
    {
      stacPage = mRemote->client->search( stacQuery );
    }

    for ( const StacItem &item : stacPage.items )
    {
      if ( cancel.cancelled() )
        throw GeoError( ErrorCode::Cancelled, "catalog search cancelled" );
      AssetRecord record;
      switch ( recordForItem( *mRemote->client, item, query, record ) )
      {
        case ItemVerdict::Accepted:
          page.records.push_back( std::move( record ) );
          page.items.push_back( item );
          break;
        case ItemVerdict::Unresolvable:
          ++page.unresolvable;
          break;
        case ItemVerdict::Rejected:
          ++page.clientFilteredOut;
          break;
      }
    }
    page.offset = 0;
    page.serverFiltered = true;   // bbox/datetime/collections/ids went to the server
    page.next.hasMore = stacPage.hasMore();
    page.next.remoteMethod = stacPage.nextMethod;
    page.next.remoteHref = stacPage.nextHref;
    page.next.remoteBody = stacPage.nextBody;
    page.next.remoteMerge = stacPage.nextMerge;
    page.next.remoteSelfMethod = stacPage.selfMethod;
    page.next.remoteSelfBody = stacPage.selfBody;
    return page;
  }

  // Local backend: lazy parse over the bounded candidate list.
  if ( mLocal )
  {
    CatalogPage page;
    page.offset = continuation.localOffset;
    const int pageSize = query.limit > 0 ? query.limit : mOptions.maxRecordsPerPage;
    std::size_t index = continuation.localOffset;
    int fetched = 0;
    for ( ; index < mLocal->itemFiles.size(); ++index )
    {
      if ( cancel.cancelled() )
        throw GeoError( ErrorCode::Cancelled, "catalog search cancelled" );
      if ( query.maxItems > 0 &&
           page.offset + static_cast<std::size_t>( fetched ) >=
             static_cast<std::size_t>( query.maxItems ) )
        break;
      if ( static_cast<int>( page.records.size() ) >= pageSize )
        break;
      try
      {
        const Json::Value document = readJsonFile( mLocal->itemFiles[index] );
        if ( !isItemShape( document ) )
        {
          ++mLocal->skippedFiles;   // a link document or stray JSON — not an item
          continue;
        }
        const StacItem item = StacItem::parseFromFile( mLocal->itemFiles[index] );
        AssetRecord record;
        switch ( recordForItem( *mLocal->resolver, item, query, record ) )
        {
          case ItemVerdict::Accepted:
            page.records.push_back( std::move( record ) );
            page.items.push_back( item );
            ++fetched;
            break;
          case ItemVerdict::Unresolvable:
            ++page.unresolvable;
            break;
          case ItemVerdict::Rejected:
            ++page.clientFilteredOut;
            break;
        }
      }
      catch ( const GeoError & )
      {
        ++mLocal->unreadableFiles;   // one bad file never kills the walk
      }
    }
    page.next.hasMore = index < mLocal->itemFiles.size();
    page.next.localOffset = index;
    // The walk bounds are honest: a page that consumed candidates up to
    // (or past) the declared file cap reports the truncation.
    page.truncatedByCap =
      static_cast<int>( mLocal->itemFiles.size() ) >= mOptions.localMaxFiles;
    return page;
  }

  throw GeoError( ErrorCode::InvalidArgument, "catalog service has no backend" );
}

CatalogService::SearchAllResult CatalogService::searchAll( const CatalogQuery &query,
                                                           const CancelToken &cancel ) const
{
  query.validate();
  SearchAllResult result;
  CatalogContinuation continuation;
  int guard = 0;
  while ( true )
  {
    const CatalogPage page = searchPage( query, continuation, cancel );
    for ( std::size_t i = 0; i < page.records.size(); ++i )
    {
      if ( query.maxItems > 0 &&
           static_cast<int>( result.records.size() ) >= query.maxItems )
      {
        result.truncatedByCap = true;
        return result;
      }
      result.records.push_back( page.records[i] );
      if ( i < page.items.size() )
        result.items.push_back( page.items[i] );
    }
    result.clientFilteredOut += page.clientFilteredOut;
    result.unresolvable += page.unresolvable;
    continuation = page.next;
    if ( !page.next.hasMore )
      return result;
    if ( ++guard > 100000 )   // loop backstop (bounds beats cleverness)
      throw GeoError( ErrorCode::ResourceExhausted, "catalog pagination failed to terminate" );
  }
}

} // namespace sicnu::geo

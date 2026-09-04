// src/agent/spatial_tools/temporal_workspace_tools.cpp
#include "temporal_workspace_tools.h"

#include "data/data_manager.h"
#include "data/temporal_workspace_types.h"
#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/algorithms/temporal/temporal_stac_adapter.h"
#include "processing/algorithms/temporal/temporal_workspace.h"

#include <QFile>
#include <QIODevice>
#include <QSet>

#include <algorithm>
#include <map>
#include <memory>

namespace sicnu::agent::spatial_tools {

namespace {

using sicnu::temporal::TemporalCollection;

constexpr int kDefaultPageLimit = 50;
constexpr int kMaxPageLimit = 200;

sicnu::data::DataManager *catalog()
{
  return sicnu::temporal::workspaceCatalog();
}

SpatialToolResult noCatalog()
{
  return SpatialToolResult::failure(
    "no workspace catalog wired — temporal collections require a DataManager "
    "(GUI project or MCP headless catalog)",
    "TEMPORAL_NO_WORKSPACE", "VALIDATION" );
}

int intOr( const Json::Value &input, const char *key, int fallback )
{
  return input.isMember( key ) && input[key].isNumeric() ? input[key].asInt() : fallback;
}

/// Light descriptor summary — parses the stored document, opens no rasters.
struct RecordSummary
{
  int sceneCount = 0;
  int scenesBound = 0;
  QString timeStart;
  QString timeEnd;
  QStringList platforms;
  QString duplicatePolicy;
  bool valid = false;
  QString error;
};

RecordSummary summarize( const sicnu::data::TemporalCollectionRecord &record )
{
  RecordSummary s;
  TemporalCollection collection;
  QString err;
  if ( !sicnu::temporal::collectionFromDescriptorText( record.descriptor, &collection, &err ) )
  {
    s.error = err;
    return s;
  }
  s.valid = true;
  s.sceneCount = collection.sceneCount();
  s.timeStart = collection.timeRangeStartIso();
  s.timeEnd = collection.timeRangeEndIso();
  s.duplicatePolicy = collection.duplicatePolicy() == temporal::DuplicatePolicy::Reject
                        ? QStringLiteral( "reject" )
                        : QStringLiteral( "keep_all" );
  QSet<QString> platforms;
  for ( const auto &scene : collection.scenes() )
  {
    if ( !scene.assetId.isEmpty() )
      ++s.scenesBound;
    if ( !scene.platform.isEmpty() )
      platforms.insert( scene.platform );
  }
  s.platforms = QStringList( platforms.cbegin(), platforms.cend() );
  std::sort( s.platforms.begin(), s.platforms.end() );
  return s;
}

Json::Value summaryToJson( const sicnu::data::TemporalCollectionRecord &record,
                           const RecordSummary &s )
{
  Json::Value entry( Json::objectValue );
  entry["id"] = record.id.toString().toStdString();
  entry["name"] = record.displayName.toStdString();
  entry["revision"] = Json::Value::UInt64( record.revision );
  entry["scene_count"] = s.sceneCount;
  if ( s.sceneCount > 0 )
    entry["scenes_bound"] = s.scenesBound;
  if ( !s.timeStart.isEmpty() )
  {
    entry["time_start"] = s.timeStart.toStdString();
    entry["time_end"] = s.timeEnd.toStdString();
  }
  if ( !s.platforms.isEmpty() )
  {
    Json::Value plats( Json::arrayValue );
    for ( const QString &p : s.platforms )
      plats.append( p.toStdString() );
    entry["platforms"] = plats;
  }
  entry["duplicate_policy"] = s.duplicatePolicy.toStdString();
  return entry;
}

} // namespace

// ---- list_collections ----

std::string TemporalListCollectionsTool::description() const
{
  return "List the temporal collection records registered in the workspace "
         "(id, name, scene count, time range, platforms). Paged, compact.";
}

std::vector<std::string> TemporalListCollectionsTool::tags() const
{
  return { "temporal", "workspace", "discovery" };
}

Json::Value TemporalListCollectionsTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value offset( Json::objectValue );
  offset["type"] = "integer";
  offset["description"] = "First record to return (default 0)";
  props["offset"] = offset;
  Json::Value limit( Json::objectValue );
  limit["type"] = "integer";
  limit["description"] = "Max records to return (default 50, max 200)";
  props["limit"] = limit;
  schema["properties"] = props;
  return schema;
}

Json::Value TemporalListCollectionsTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value collections( Json::objectValue );
  collections["type"] = "array";
  props["collections"] = collections;
  Json::Value total( Json::objectValue );
  total["type"] = "integer";
  props["total"] = total;
  Json::Value nextOffset( Json::objectValue );
  nextOffset["type"] = "integer";
  props["next_offset"] = nextOffset;
  schema["properties"] = props;
  return schema;
}

SpatialToolResult TemporalListCollectionsTool::execute( const Json::Value &input )
{
  sicnu::data::DataManager *dm = catalog();
  if ( !dm )
    return noCatalog();

  const auto records = dm->temporalCollections();
  const int offset = std::max( 0, intOr( input, "offset", 0 ) );
  int limit = intOr( input, "limit", kDefaultPageLimit );
  limit = std::clamp( limit, 0, kMaxPageLimit );

  Json::Value out( Json::objectValue );
  out["total"] = static_cast<Json::Int>( records.size() );
  Json::Value collections( Json::arrayValue );
  const int end = limit == 0 ? static_cast<int>( records.size() )
                             : std::min<int>( records.size(), offset + limit );
  for ( int i = offset; i < end; ++i )
  {
    const auto &record = records[i];
    const RecordSummary s = summarize( record );
    if ( !s.valid )
    {
      Json::Value broken( Json::objectValue );
      broken["id"] = record.id.toString().toStdString();
      broken["name"] = record.displayName.toStdString();
      broken["error"] = s.error.toStdString();
      collections.append( broken );
      continue;
    }
    collections.append( summaryToJson( record, s ) );
  }
  out["collections"] = collections;
  out["next_offset"] = end < static_cast<int>( records.size() ) ? end : -1;
  return SpatialToolResult::ok( out );
}

// ---- get_collection ----

std::string TemporalGetCollectionTool::description() const
{
  return "Summarize one temporal collection workspace record by id or exact "
         "name: scene count, bound-scene ratio, time range, platforms, "
         "duplicate policy, revision. Opens no rasters.";
}

std::vector<std::string> TemporalGetCollectionTool::tags() const
{
  return { "temporal", "workspace", "discovery" };
}

Json::Value TemporalGetCollectionTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value collection( Json::objectValue );
  collection["type"] = "string";
  collection["description"] = "Workspace collection id (UUID) or exact name";
  props["collection"] = collection;
  schema["properties"] = props;
  Json::Value required( Json::arrayValue );
  required.append( "collection" );
  schema["required"] = required;
  return schema;
}

Json::Value TemporalGetCollectionTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value collection( Json::objectValue );
  collection["type"] = "object";
  props["collection"] = collection;
  schema["properties"] = props;
  return schema;
}

SpatialToolResult TemporalGetCollectionTool::execute( const Json::Value &input )
{
  sicnu::data::DataManager *dm = catalog();
  if ( !dm )
    return noCatalog();

  std::string error;
  const QString key = requireStringField( input, "collection", &error );
  if ( !error.empty() )
    return SpatialToolResult::failure( error, "INVALID_PARAMETER", "VALIDATION" );

  std::optional<sicnu::data::TemporalCollectionRecord> record;
  const auto id = sicnu::data::CollectionId::fromString( key );
  if ( id )
    record = dm->temporalCollection( *id );
  if ( !record )
  {
    for ( const auto &candidate : dm->temporalCollections() )
    {
      if ( candidate.displayName == key )
      {
        record = candidate;
        break;
      }
    }
  }
  if ( !record )
    return SpatialToolResult::failure( "no temporal collection record '" + key.toStdString() + "'",
                                       "TEMPORAL_UNKNOWN_COLLECTION", "VALIDATION" );

  const RecordSummary s = summarize( *record );
  if ( !s.valid )
    return SpatialToolResult::failure( "stored descriptor is invalid: " + s.error.toStdString(),
                                       "TEMPORAL_INVALID_DESCRIPTOR", "DATA" );
  Json::Value out( Json::objectValue );
  out["collection"] = summaryToJson( *record, s );
  return SpatialToolResult::ok( out );
}

// ---- register_collection ----

std::string TemporalRegisterCollectionTool::description() const
{
  return "Register a temporal collection in the workspace from a descriptor "
         "file or an inline scene list. Scene paths bound to registered Data "
         "Assets carry asset id + revision (provenance identity). "
         "Re-registering an identical collection returns the existing id.";
}

std::vector<std::string> TemporalRegisterCollectionTool::tags() const
{
  return { "temporal", "workspace", "discovery" };
}

Json::Value TemporalRegisterCollectionTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value descriptor( Json::objectValue );
  descriptor["type"] = "string";
  descriptor["description"] = "Path to a temporal collection descriptor JSON file";
  props["descriptor"] = descriptor;
  Json::Value scenes( Json::objectValue );
  scenes["type"] = "array";
  scenes["description"] = "Scene entries: path strings or {path, time?, bands?, mask_band?, quality_band?}";
  props["scenes"] = scenes;
  Json::Value name( Json::objectValue );
  name["type"] = "string";
  props["name"] = name;
  schema["properties"] = props;
  return schema;
}

Json::Value TemporalRegisterCollectionTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value cid( Json::objectValue );
  cid["type"] = "string";
  props["collection_id"] = cid;
  Json::Value sc( Json::objectValue );
  sc["type"] = "integer";
  props["scene_count"] = sc;
  Json::Value sb( Json::objectValue );
  sb["type"] = "integer";
  props["scenes_bound"] = sb;
  Json::Value reused( Json::objectValue );
  reused["type"] = "boolean";
  props["reused_existing"] = reused;
  schema["properties"] = props;
  return schema;
}

SpatialToolResult TemporalRegisterCollectionTool::execute( const Json::Value &input )
{
  sicnu::data::DataManager *dm = catalog();
  if ( !dm )
    return noCatalog();

  TemporalCollection collection;
  if ( input.isMember( "descriptor" ) && input["descriptor"].isString() )
  {
    const QString path = QString::fromStdString( input["descriptor"].asString() );
    QString err;
    if ( !TemporalCollection::load( path, &collection, &err ) )
      return SpatialToolResult::failure( err.toStdString(), "INVALID_PARAMETER", "VALIDATION" );
  }
  else if ( input.isMember( "scenes" ) && input["scenes"].isArray() )
  {
    QString err;
    if ( !TemporalCollection::fromInlineScenes(
           input["scenes"], &collection, &err,
           input.isMember( "times" ) ? input["times"] : Json::Value(),
           input.isMember( "bands" ) ? input["bands"] : Json::Value(),
           input.isMember( "name" ) && input["name"].isString()
             ? QString::fromStdString( input["name"].asString() )
             : QString() ) )
    {
      return SpatialToolResult::failure( err.toStdString(), "INVALID_PARAMETER", "VALIDATION" );
    }
  }
  else
  {
    return SpatialToolResult::failure( "provide 'descriptor' (file path) or a non-empty 'scenes' array",
                                       "INVALID_PARAMETER", "VALIDATION" );
  }

  const QString name = input.isMember( "name" ) && input["name"].isString()
                         ? QString::fromStdString( input["name"].asString() )
                         : collection.name();
  QString error;
  bool reused = false;
  const auto id = sicnu::temporal::saveCollectionToWorkspace( *dm, name, collection,
                                                              sicnu::data::CollectionId(),
                                                              &error, &reused );
  if ( id.isNull() )
    return SpatialToolResult::failure( error.toStdString(), "DATA_IO", "IO" );

  // Report the persisted (bound) state.
  auto saved = dm->temporalCollection( id );
  int bound = 0;
  int sceneCount = collection.sceneCount();
  if ( saved )
  {
    TemporalCollection parsed;
    QString err;
    if ( sicnu::temporal::collectionFromDescriptorText( saved->descriptor, &parsed, &err ) )
    {
      sceneCount = parsed.sceneCount();
      for ( const auto &scene : parsed.scenes() )
        if ( !scene.assetId.isEmpty() )
          ++bound;
    }
  }

  Json::Value out( Json::objectValue );
  out["collection_id"] = id.toString().toStdString();
  out["name"] = name.toStdString();
  out["scene_count"] = sceneCount;
  out["scenes_bound"] = bound;
  out["reused_existing"] = reused;
  return SpatialToolResult::ok( out );
}

// ---- remove_collection ----

std::string TemporalRemoveCollectionTool::description() const
{
  return "Remove a temporal collection workspace record by id. Scene Data "
         "Assets are never deleted — the record only references them.";
}

std::vector<std::string> TemporalRemoveCollectionTool::tags() const
{
  return { "temporal", "workspace", "discovery" };
}

Json::Value TemporalRemoveCollectionTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value collection( Json::objectValue );
  collection["type"] = "string";
  collection["description"] = "Workspace collection id (UUID)";
  props["collection"] = collection;
  schema["properties"] = props;
  Json::Value required( Json::arrayValue );
  required.append( "collection" );
  schema["required"] = required;
  return schema;
}

Json::Value TemporalRemoveCollectionTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value rem( Json::objectValue );
  rem["type"] = "boolean";
  props["removed"] = rem;
  schema["properties"] = props;
  return schema;
}

SpatialToolResult TemporalRemoveCollectionTool::execute( const Json::Value &input )
{
  sicnu::data::DataManager *dm = catalog();
  if ( !dm )
    return noCatalog();

  std::string error;
  const QString key = requireStringField( input, "collection", &error );
  if ( !error.empty() )
    return SpatialToolResult::failure( error, "INVALID_PARAMETER", "VALIDATION" );
  const auto id = sicnu::data::CollectionId::fromString( key );
  if ( !id )
    return SpatialToolResult::failure( "'collection' must be a workspace collection id (UUID)",
                                       "INVALID_PARAMETER", "VALIDATION" );
  const auto removed = dm->removeTemporalCollection( *id );
  if ( !removed )
    return SpatialToolResult::failure( "no temporal collection record '" + key.toStdString() + "'",
                                       "TEMPORAL_UNKNOWN_COLLECTION", "VALIDATION" );
  Json::Value out( Json::objectValue );
  out["removed"] = true;
  return SpatialToolResult::ok( out );
}


// ---- ingest_stac ----

std::string TemporalIngestStacTool::description() const
{
  return "Build a temporal collection from a STAC search response (file path "
         "or inline JSON; no network). Filters: bbox, datetime (start/end), "
         "property_filter (key=value), limit. Registers the collection in the "
         "workspace by default; scene hrefs may be remote COG URLs.";
}

std::vector<std::string> TemporalIngestStacTool::tags() const
{
  return { "temporal", "workspace", "stac" };
}

Json::Value TemporalIngestStacTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value result( Json::objectValue );
  result["type"] = "string";
  result["description"] = "Path to a saved STAC search-response JSON document";
  props["result"] = result;
  Json::Value resultJson( Json::objectValue );
  resultJson["type"] = "object";
  resultJson["description"] = "Inline STAC search response ({features:[...]})";
  props["result_json"] = resultJson;
  Json::Value bbox( Json::objectValue );
  bbox["type"] = "string";
  bbox["description"] = "minx,miny,maxx,maxy footprint filter";
  props["bbox"] = bbox;
  Json::Value datetime( Json::objectValue );
  datetime["type"] = "string";
  datetime["description"] = "ISO start/end range (either side may be empty) or a single date";
  props["datetime"] = datetime;
  Json::Value propertyFilter( Json::objectValue );
  propertyFilter["type"] = "string";
  propertyFilter["description"] = "key=value property filter (e.g. platform=Sentinel-2A)";
  props["property_filter"] = propertyFilter;
  Json::Value limit( Json::objectValue );
  limit["type"] = "integer";
  limit["description"] = "Maximum scene count after filtering";
  props["limit"] = limit;
  Json::Value name( Json::objectValue );
  name["type"] = "string";
  name["description"] = "Collection name (workspace record + descriptor)";
  props["name"] = name;
  Json::Value reg( Json::objectValue );
  reg["type"] = "boolean";
  reg["description"] = "Register in the workspace (default true)";
  props["register"] = reg;
  schema["properties"] = props;
  Json::Value required( Json::arrayValue );
  required.append( "name" );
  schema["required"] = required;
  return schema;
}

Json::Value TemporalIngestStacTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value sc( Json::objectValue );
  sc["type"] = "integer";
  props["scene_count"] = sc;
  Json::Value cid( Json::objectValue );
  cid["type"] = "string";
  props["collection_id"] = cid;
  Json::Value scenes( Json::objectValue );
  scenes["type"] = "array";
  props["scenes"] = scenes;
  Json::Value warns( Json::objectValue );
  warns["type"] = "array";
  props["warnings"] = warns;
  schema["properties"] = props;
  return schema;
}

SpatialToolResult TemporalIngestStacTool::execute( const Json::Value &input )
{
  Json::Value searchResponse;
  if ( input.isMember( "result_json" ) && input["result_json"].isObject() )
  {
    searchResponse = input["result_json"];
  }
  else if ( input.isMember( "result" ) && input["result"].isString() )
  {
    QFile file( QString::fromStdString( input["result"].asString() ) );
    if ( !file.open( QIODevice::ReadOnly ) )
      return SpatialToolResult::failure( "cannot open STAC result document: " +
                                           input["result"].asString(),
                                         "DATA_IO", "IO" );
    Json::CharReaderBuilder readerBuilder;
    std::unique_ptr<Json::CharReader> reader( readerBuilder.newCharReader() );
    Json::Value parsed;
    std::string parseError;
    const QByteArray raw = file.readAll();
    if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &parsed, &parseError ) )
      return SpatialToolResult::failure( "invalid STAC result JSON: " + parseError,
                                         "INVALID_PARAMETER", "VALIDATION" );
    searchResponse = std::move( parsed );
  }
  else
  {
    return SpatialToolResult::failure( "provide 'result' (file path) or 'result_json' (inline object)",
                                       "INVALID_PARAMETER", "VALIDATION" );
  }
  if ( !( input.isMember( "name" ) && input["name"].isString() &&
          !input["name"].asString().empty() ) )
    return SpatialToolResult::failure( "missing string parameter 'name'",
                                       "INVALID_PARAMETER", "VALIDATION" );
  const QString name = QString::fromStdString( input["name"].asString() );

  // Parse -> filter -> build (all pure; no network, no raster I/O).
  Json::Value features = searchResponse["features"];
  if ( !features.isArray() && searchResponse.isArray() )
    features = searchResponse;
  if ( !features.isArray() )
    return SpatialToolResult::failure( "not a STAC search response (missing features array)",
                                       "INVALID_PARAMETER", "VALIDATION" );

  QVector<sicnu::temporal::StacItem> items;
  QStringList warnings;
  for ( const Json::Value &feature : features )
  {
    sicnu::temporal::StacItem item;
    QString itemError;
    if ( !sicnu::temporal::parseStacItem( feature, &item, &itemError ) )
    {
      warnings << itemError;
      continue;
    }
    items.append( item );
  }
  if ( items.isEmpty() )
    return SpatialToolResult::failure( "no usable STAC items in the search response",
                                       "TEMPORAL_STAC_NO_ITEMS", "VALIDATION" );

  const QString bbox = input.isMember( "bbox" ) && input["bbox"].isString()
                         ? QString::fromStdString( input["bbox"].asString() )
                         : QString();
  const QString datetime = input.isMember( "datetime" ) && input["datetime"].isString()
                             ? QString::fromStdString( input["datetime"].asString() )
                             : QString();
  const QString propertyFilter = input.isMember( "property_filter" ) &&
                                   input["property_filter"].isString()
                                   ? QString::fromStdString( input["property_filter"].asString() )
                                   : QString();
  const int limit = intOr( input, "limit", -1 );
  const QVector<sicnu::temporal::StacItem> filtered =
    sicnu::temporal::filterStacItems( items, bbox, datetime, limit, propertyFilter, &warnings );

  TemporalCollection collection;
  QString error;
  if ( !sicnu::temporal::temporalCollectionFromStacItems( filtered, name, &collection, &error ) )
    return SpatialToolResult::failure( error.toStdString(), "TEMPORAL_STAC_INSUFFICIENT",
                                       "VALIDATION" );

  Json::Value out( Json::objectValue );
  out["scene_count"] = collection.sceneCount();
  // Compact scene preview: first 10 entries only (full listing stays with
  // temporal:list_scenes on the persisted record).
  Json::Value scenes( Json::arrayValue );
  const int preview = std::min( 10, collection.sceneCount() );
  for ( int i = 0; i < preview; ++i )
  {
    const auto &scene = collection.scenes().at( i );
    Json::Value entry( Json::objectValue );
    entry["path"] = scene.path.toStdString();
    if ( scene.time.valid )
      entry["time"] = scene.time.iso.toStdString();
    if ( !scene.platform.isEmpty() )
      entry["platform"] = scene.platform.toStdString();
    scenes.append( entry );
  }
  out["scenes"] = scenes;
  if ( !warnings.isEmpty() )
  {
    Json::Value warningsJson( Json::arrayValue );
    for ( const QString &w : warnings )
      warningsJson.append( w.toStdString() );
    out["warnings"] = warningsJson;
  }

  if ( input.isMember( "register" ) && input["register"].isBool() && !input["register"].asBool() )
  {
    out["registered"] = false;
    return SpatialToolResult::ok( out );
  }

  sicnu::data::DataManager *dm = catalog();
  if ( !dm )
    return noCatalog();
  bool reused = false;
  const auto id = sicnu::temporal::saveCollectionToWorkspace( *dm, name, collection,
                                                              sicnu::data::CollectionId(),
                                                              &error, &reused );
  if ( id.isNull() )
    return SpatialToolResult::failure( error.toStdString(), "DATA_IO", "IO" );
  out["registered"] = true;
  out["collection_id"] = id.toString().toStdString();
  out["reused_existing"] = reused;
  return SpatialToolResult::ok( out );
}

} // namespace sicnu::agent::spatial_tools

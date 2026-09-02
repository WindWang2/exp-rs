// src/agent/spatial_tools/temporal_collection_tools.cpp
#include "temporal_collection_tools.h"

#include "processing/algorithms/temporal/temporal_preflight.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <map>
#include <vector>

namespace sicnu::agent::spatial_tools {

namespace {

using sicnu::temporal::TemporalCollection;

QString requireStringField( const Json::Value &input, const char *key, std::string *error )
{
  if ( !input.isObject() || !input.isMember( key ) || !input[key].isString() )
  {
    *error = std::string( "missing string parameter '" ) + key + "'";
    return {};
  }
  return QString::fromStdString( input[key].asString() );
}

/// Collection from "collection" (descriptor path) or "scenes" (paths/objects).
bool collectionFromInput( const Json::Value &input, TemporalCollection *out,
                          std::string *error )
{
  if ( input.isMember( "collection" ) && input["collection"].isString() )
  {
    const QString path = QString::fromStdString( input["collection"].asString() );
    QString qErr;
    if ( !TemporalCollection::load( path, out, &qErr ) )
    {
      *error = qErr.toStdString();
      return false;
    }
    return true;
  }
  if ( !input.isMember( "scenes" ) || !input["scenes"].isArray() || input["scenes"].empty() )
  {
    *error = "provide 'collection' (descriptor path) or a non-empty 'scenes' array";
    return false;
  }
  struct Extra
  {
    std::map<QString, int> bands;
    int qualityBand = 0;
    int maskBand = 0;
  };
  QStringList paths;
  QStringList times;
  std::vector<Extra> extras;
  for ( const Json::Value &entry : input["scenes"] )
  {
    Extra extra;
    if ( entry.isString() )
    {
      paths.push_back( QString::fromStdString( entry.asString() ) );
      times.push_back( {} );
    }
    else if ( entry.isObject() && entry.isMember( "path" ) && entry["path"].isString() )
    {
      paths.push_back( QString::fromStdString( entry["path"].asString() ) );
      times.push_back( entry.isMember( "time" ) && entry["time"].isString()
                           ? QString::fromStdString( entry["time"].asString() )
                           : QString() );
      if ( entry.isMember( "bands" ) && entry["bands"].isObject() )
      {
        for ( auto it = entry["bands"].begin(); it != entry["bands"].end(); ++it )
        {
          if ( !( *it ).isNumeric() )
          {
            *error = "scenes[].bands values must be integers";
            return false;
          }
          extra.bands[QString::fromStdString( it.name() )] = ( *it ).asInt();
        }
      }
      if ( entry.isMember( "quality_band" ) )
      {
        if ( !entry["quality_band"].isNumeric() )
        {
          *error = "scenes[].quality_band must be an integer";
          return false;
        }
        extra.qualityBand = entry["quality_band"].asInt();
      }
      if ( entry.isMember( "mask_band" ) )
      {
        if ( !entry["mask_band"].isNumeric() )
        {
          *error = "scenes[].mask_band must be an integer";
          return false;
        }
        extra.maskBand = entry["mask_band"].asInt();
      }
    }
    else
    {
      *error = "scenes[] entries must be path strings or {path, time?} objects";
      return false;
    }
    extras.push_back( std::move( extra ) );
  }
  for ( const QString &p : paths )
  {
    if ( !QFile::exists( p ) )
    {
      *error = "scene not found: " + p.toStdString();
      return false;
    }
  }
  *out = TemporalCollection::fromScenePaths( paths, times,
                                             input.isMember( "name" ) && input["name"].isString()
                                                 ? QString::fromStdString( input["name"].asString() )
                                                 : QString() );
  // fromScenePaths sorts; re-attach per-scene extras via the original index.
  for ( int i = 0; i < static_cast<int>( extras.size() ); ++i )
  {
    for ( auto &scene : out->scenes() )
    {
      if ( scene.originalIndex == i )
      {
        scene.bandOverrides = extras[i].bands;
        scene.qualityBand = extras[i].qualityBand;
        scene.maskBand = extras[i].maskBand;
        break;
      }
    }
  }
  return true;
}

Json::Value compactSummary( const TemporalCollection &collection,
                            const sicnu::temporal::TemporalPreflightReport &report )
{
  Json::Value v( Json::objectValue );
  v["scene_count"] = collection.sceneCount();
  if ( !collection.timeRangeStartIso().isEmpty() )
  {
    Json::Value range( Json::arrayValue );
    range.append( collection.timeRangeStartIso().toStdString() );
    range.append( collection.timeRangeEndIso().toStdString() );
    v["time_range"] = range;
  }
  QStringList platforms;
  for ( const auto &s : collection.scenes() )
    if ( !s.platform.isEmpty() && !platforms.contains( s.platform ) )
      platforms.push_back( s.platform );
  if ( !platforms.isEmpty() )
  {
    Json::Value arr( Json::arrayValue );
    for ( const QString &p : platforms )
      arr.append( p.toStdString() );
    v["platforms"] = arr;
  }
  v["grid_compatible"] = report.gridCompatible;
  v["radiometric_state"] = report.commonRadiometricState.toStdString();
  v["scenes_with_time"] = report.scenesWithTime;
  if ( report.duplicateTimeCount > 0 )
    v["duplicate_times"] = report.duplicateTimeCount;
  int blocking = 0;
  for ( const auto &issue : report.issues )
    if ( issue.blocking )
      ++blocking;
  v["blocking_issues"] = blocking;
  v["ok"] = report.ok();
  return v;
}

} // namespace

// ---- create ----

std::string TemporalCreateCollectionTool::description() const
{
  return "Create and save a temporal collection descriptor from scene paths. "
         "Acquisition times are resolved from product metadata "
         "(SICNU_ACQUISITION_DATE), explicit entries, or a conservative "
         "filename parse (Sentinel-2 / Landsat / MODIS); scenes with no "
         "resolvable time are flagged, never guessed. The saved descriptor is "
         "accepted by every rs:temporal_* algorithm as the 'collection' "
         "parameter.";
}

std::vector<std::string> TemporalCreateCollectionTool::tags() const
{
  return { "temporal", "collection", "time-series", "create" };
}

Json::Value TemporalCreateCollectionTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value scenes( Json::objectValue );
  scenes["type"] = "array";
  scenes["description"] = "Scene entries: path strings or {path, time?, bands?, mask_band?, quality_band?}";
  {
    Json::Value items( Json::objectValue );
    items["type"] = "string";
    scenes["items"] = items;
  }
  props["scenes"] = scenes;
  Json::Value output( Json::objectValue );
  output["type"] = "string";
  output["description"] = "Where to save the descriptor JSON";
  props["output"] = output;
  Json::Value name( Json::objectValue );
  name["type"] = "string";
  props["name"] = name;
  Json::Value dup( Json::objectValue );
  dup["type"] = "string";
  dup["enum"] = Json::Value( Json::arrayValue );
  dup["enum"].append( "keep_all" );
  dup["enum"].append( "reject" );
  props["duplicate_policy"] = dup;
  schema["properties"] = props;
  Json::Value required( Json::arrayValue );
  required.append( "scenes" );
  required.append( "output" );
  schema["required"] = required;
  return schema;
}

Json::Value TemporalCreateCollectionTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  props["collection"] = Json::Value( Json::objectValue );
  props["scene_count"] = Json::Value( Json::intValue );
  props["missing_times"] = Json::Value( Json::intValue );
  schema["properties"] = props;
  return schema;
}

SpatialToolResult TemporalCreateCollectionTool::execute( const Json::Value &input )
{
  std::string error;
  const QString outputPath = requireStringField( input, "output", &error );
  if ( !error.empty() )
    return SpatialToolResult::failure( error, "INVALID_PARAMETER", "VALIDATION" );
  TemporalCollection collection;
  if ( !collectionFromInput( input, &collection, &error ) )
    return SpatialToolResult::failure( error, "INVALID_PARAMETER", "VALIDATION" );

  if ( input.isMember( "name" ) && input["name"].isString() )
    collection.setName( QString::fromStdString( input["name"].asString() ) );
  bool policyOk = false;
  const auto policy = sicnu::temporal::duplicatePolicyFromString(
    input.isMember( "duplicate_policy" ) && input["duplicate_policy"].isString()
        ? QString::fromStdString( input["duplicate_policy"].asString() )
        : QStringLiteral( "keep_all" ),
    &policyOk );
  // Same rejection as the operator path (rs_temporal_collection_input): an
  // unparseable duplicate_policy must fail the tool, never coerce to keep_all
  // and persist a descriptor that silently differs from what was asked.
  if ( !policyOk || ( input.isMember( "duplicate_policy" ) &&
                      !input["duplicate_policy"].isString() ) )
    return SpatialToolResult::failure( "duplicate_policy must be 'keep_all' or 'reject'",
                                       "INVALID_PARAMETER", "VALIDATION" );
  collection.setDuplicatePolicy( policy );

  int missingTimes = 0;
  for ( const auto &s : collection.scenes() )
    if ( !s.time.valid )
      ++missingTimes;
  if ( missingTimes > 0 && policy == sicnu::temporal::DuplicatePolicy::Reject )
    return SpatialToolResult::failure(
      "collection would contain scenes without acquisition time — supply "
      "'time' entries before using reject policies",
      "TEMPORAL_MISSING_TIME", "VALIDATION" );

  if ( !collection.save( outputPath ) )
    return SpatialToolResult::failure( "cannot write descriptor: " + outputPath.toStdString(),
                                       "DATA_IO", "IO" );

  Json::Value out( Json::objectValue );
  out["collection"] = outputPath.toStdString();
  out["scene_count"] = collection.sceneCount();
  out["missing_times"] = missingTimes;
  if ( !collection.timeRangeStartIso().isEmpty() )
  {
    out["time_start"] = collection.timeRangeStartIso().toStdString();
    out["time_end"] = collection.timeRangeEndIso().toStdString();
  }
  return SpatialToolResult::ok( out );
}

// ---- describe ----

std::string TemporalDescribeCollectionTool::description() const
{
  return "Compact summary of a temporal collection: scene count, time range, "
         "platforms, grid compatibility, radiometric state, blocking issue "
         "count. Detail level 'issues' appends the preflight issue list; "
         "per-scene detail belongs to temporal:list_scenes.";
}

std::vector<std::string> TemporalDescribeCollectionTool::tags() const
{
  return { "temporal", "collection", "summary" };
}

Json::Value TemporalDescribeCollectionTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value collection( Json::objectValue );
  collection["type"] = "string";
  collection["description"] = "Descriptor path from temporal:create_collection";
  props["collection"] = collection;
  Json::Value scenes( Json::objectValue );
  scenes["type"] = "array";
  scenes["description"] = "Inline scenes (path strings or {path, time?}) instead of a descriptor";
  props["scenes"] = scenes;
  Json::Value detail( Json::objectValue );
  detail["type"] = "string";
  detail["enum"] = Json::Value( Json::arrayValue );
  detail["enum"].append( "compact" );
  detail["enum"].append( "issues" );
  detail["description"] = "compact (default): summary fields only; issues: + preflight issues";
  props["detail"] = detail;
  schema["properties"] = props;
  return schema;
}

Json::Value TemporalDescribeCollectionTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  props["scene_count"] = Json::Value( Json::intValue );
  props["time_range"] = Json::Value( Json::arrayValue );
  props["grid_compatible"] = Json::Value( Json::booleanValue );
  schema["properties"] = props;
  return schema;
}

SpatialToolResult TemporalDescribeCollectionTool::execute( const Json::Value &input )
{
  std::string error;
  TemporalCollection collection;
  if ( !collectionFromInput( input, &collection, &error ) )
    return SpatialToolResult::failure( error, "INVALID_PARAMETER", "VALIDATION" );

  sicnu::temporal::PreflightOptions options;
  const auto report = sicnu::temporal::runPreflight( collection, options );
  Json::Value out = compactSummary( collection, report );
  out["collection_name"] = collection.name().toStdString();
  const std::string detail = input.isMember( "detail" ) && input["detail"].isString()
                                 ? input["detail"].asString()
                                 : "compact";
  if ( detail == "issues" )
    out["preflight"] = report.toJson();
  return SpatialToolResult::ok( out );
}

// ---- list scenes ----

std::string TemporalListScenesTool::description() const
{
  return "Chronological, paged scene listing of a temporal collection: path, "
         "acquisition time (+ precision and source), platform, processing "
         "level. Pagination via offset/limit (default 50).";
}

std::vector<std::string> TemporalListScenesTool::tags() const
{
  return { "temporal", "collection", "scenes", "listing" };
}

Json::Value TemporalListScenesTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value collection( Json::objectValue );
  collection["type"] = "string";
  props["collection"] = collection;
  Json::Value scenes( Json::objectValue );
  scenes["type"] = "array";
  props["scenes"] = scenes;
  Json::Value offset( Json::objectValue );
  offset["type"] = "integer";
  offset["description"] = "0-based scene offset (default 0)";
  props["offset"] = offset;
  Json::Value limit( Json::objectValue );
  limit["type"] = "integer";
  limit["description"] = "page size (default 50, 0 = all)";
  props["limit"] = limit;
  schema["properties"] = props;
  return schema;
}

Json::Value TemporalListScenesTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  props["scenes"] = Json::Value( Json::arrayValue );
  props["total"] = Json::Value( Json::intValue );
  props["next_offset"] = Json::Value( Json::intValue );
  schema["properties"] = props;
  return schema;
}

SpatialToolResult TemporalListScenesTool::execute( const Json::Value &input )
{
  std::string error;
  TemporalCollection collection;
  if ( !collectionFromInput( input, &collection, &error ) )
    return SpatialToolResult::failure( error, "INVALID_PARAMETER", "VALIDATION" );

  const int total = collection.sceneCount();
  if ( input.isMember( "offset" ) && !input["offset"].isNumeric() )
    return SpatialToolResult::failure( "'offset' must be an integer", "INVALID_PARAMETER",
                                       "VALIDATION" );
  if ( input.isMember( "limit" ) && !input["limit"].isNumeric() )
    return SpatialToolResult::failure( "'limit' must be an integer", "INVALID_PARAMETER",
                                       "VALIDATION" );
  const int offset = std::max( 0, input.isMember( "offset" ) ? input["offset"].asInt() : 0 );
  int limit = input.isMember( "limit" ) ? input["limit"].asInt() : 50;
  if ( limit <= 0 )
    limit = total;

  Json::Value out( Json::objectValue );
  out["total"] = total;
  Json::Value list( Json::arrayValue );
  int nextOffset = -1;
  for ( int i = offset; i < total && i < offset + limit; ++i )
  {
    const auto &s = collection.scenes().at( i );
    Json::Value entry( Json::objectValue );
    entry["index"] = i;
    entry["path"] = s.path.toStdString();
    entry["time"] = s.time.valid ? s.time.iso.toStdString() : Json::Value();
    entry["time_precision"] = s.time.valid && s.time.precision == sicnu::temporal::TimePrecision::DateTime
                                  ? "datetime"
                                  : "date";
    if ( !s.timeSource.isEmpty() )
      entry["time_source"] = s.timeSource.toStdString();
    if ( !s.platform.isEmpty() )
      entry["platform"] = s.platform.toStdString();
    if ( !s.processingLevel.isEmpty() )
      entry["processing_level"] = s.processingLevel.toStdString();
    if ( !s.assetId.isEmpty() )
      entry["asset_id"] = s.assetId.toStdString();
    list.append( entry );
    if ( i + 1 < total )
      nextOffset = i + 1;
  }
  out["scenes"] = list;
  if ( offset + limit < total )
    out["next_offset"] = offset + limit;
  else
    out["next_offset"] = -1;
  return SpatialToolResult::ok( out );
}

// ---- preflight ----

std::string TemporalPreflightCollectionTool::description() const
{
  return "Run the temporal scientific gate without executing an algorithm: "
         "time completeness/duplicates, grid compatibility (CRS / pixel size "
         "/ origin / extent — no hidden resampling), band-role resolution, "
         "radiometric-state and scale/offset consistency, QA/SCL availability. "
         "Returns stable issue codes; blocking issues explain exactly what to "
         "fix before rs:temporal_* algorithms will run.";
}

std::vector<std::string> TemporalPreflightCollectionTool::tags() const
{
  return { "temporal", "collection", "preflight", "validation", "quality" };
}

Json::Value TemporalPreflightCollectionTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value collection( Json::objectValue );
  collection["type"] = "string";
  props["collection"] = collection;
  Json::Value scenes( Json::objectValue );
  scenes["type"] = "array";
  props["scenes"] = scenes;
  Json::Value requiredRoles( Json::objectValue );
  requiredRoles["type"] = "array";
  requiredRoles["description"] = "Band roles every scene must resolve, e.g. [\"red\", \"nir\"]";
  props["required_band_roles"] = requiredRoles;
  schema["properties"] = props;
  return schema;
}

Json::Value TemporalPreflightCollectionTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );
  props["ok"] = Json::Value( Json::booleanValue );
  props["issues"] = Json::Value( Json::arrayValue );
  schema["properties"] = props;
  return schema;
}

SpatialToolResult TemporalPreflightCollectionTool::execute( const Json::Value &input )
{
  std::string error;
  TemporalCollection collection;
  if ( !collectionFromInput( input, &collection, &error ) )
    return SpatialToolResult::failure( error, "INVALID_PARAMETER", "VALIDATION" );

  sicnu::temporal::PreflightOptions options;
  if ( input.isMember( "required_band_roles" ) && input["required_band_roles"].isArray() )
    for ( const Json::Value &r : input["required_band_roles"] )
      if ( r.isString() )
        options.requiredBandRoles.push_back( QString::fromStdString( r.asString() ) );

  const auto report = sicnu::temporal::runPreflight( collection, options );
  Json::Value out = report.toJson();
  out["compact"] = compactSummary( collection, report );
  if ( !report.ok() )
  {
    // The full report rides along in `output` even on failure so agents can
    // inspect every issue, not just the first blocking one.
    SpatialToolResult r = SpatialToolResult::failure(
      "temporal preflight failed: " + report.firstBlocking().message.toStdString() +
          " (see issues array)",
      "TEMPORAL_PREFLIGHT_FAILED", "VALIDATION" );
    r.output = out;
    return r;
  }
  return SpatialToolResult::ok( out );
}

} // namespace sicnu::agent::spatial_tools

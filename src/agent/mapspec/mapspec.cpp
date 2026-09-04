// src/agent/mapspec/mapspec.cpp
#include "mapspec.h"

#include "../contracts/spatial_contracts.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace sicnu::agent::mapspec {

using namespace sicnu::agent::contracts;

namespace {

struct CollectionInfo
{
  const char *name;
  const char *idPrefix;
  /// Items whose rect_mm must lie within the page.
  bool requiresRect;
  /// Item may reference a map frame via "map_ref".
  bool mayReferenceMap;
};

const CollectionInfo kCollectionInfos[] = {
  { "map_frames", "map", true, false },
  { "layers", "layer", false, false },
  { "symbols", "symbol", false, false },
  { "legends", "legend", true, true },
  { "north_arrows", "north_arrow", true, true },
  { "scale_bars", "scale_bar", true, true },
  { "titles", "title", true, false },
  { "labels", "label", true, false },
  { "charts", "chart", true, true },
  { "colorbars", "colorbar", true, true },
  { "inset_maps", "inset_map", true, false },
  { "grids", "grid", false, true },
  { "annotations", "annotation", true, false },
  { "source_notes", "source_note", true, false },
  { "constraints", "constraint", false, false },
};

const CollectionInfo *collectionInfo( const std::string &name )
{
  for ( const auto &info : kCollectionInfos )
    if ( name == info.name )
      return &info;
  return nullptr;
}

/// Validates one rect_mm value against the page; appends problems.
void checkRect( const Json::Value &item, const std::string &id, double pageW, double pageH,
                std::vector<std::string> &problems )
{
  const Json::Value &rect = item["rect_mm"];
  if ( !rect.isArray() || rect.size() != 4 )
  {
    problems.push_back( id + ": rect_mm must be [x, y, w, h]" );
    return;
  }
  for ( const auto &v : rect )
  {
    if ( !v.isNumeric() )
    {
      problems.push_back( id + ": rect_mm entries must be numbers" );
      return;
    }
  }
  const double x = rect[0].asDouble();
  const double y = rect[1].asDouble();
  const double w = rect[2].asDouble();
  const double h = rect[3].asDouble();
  if ( w <= 0 || h <= 0 )
    problems.push_back( id + ": rect_mm width/height must be positive" );
  if ( pageW > 0 && pageH > 0 )
  {
    // Fully outside the page is a spec error; partial clipping is a preflight
    // concern (checked against the rendered layout there).
    if ( x >= pageW || y >= pageH || x + w <= 0 || y + h <= 0 )
      problems.push_back( id + ": rect_mm lies entirely outside the page" );
  }
}

} // namespace

const char *const kCollections[] = { "map_frames", "layers", "symbols", "legends",
                                     "north_arrows", "scale_bars", "titles", "labels",
                                     "charts", "colorbars", "inset_maps", "grids",
                                     "annotations", "source_notes", "constraints" };
const int kCollectionCount = static_cast<int>( sizeof( kCollections ) / sizeof( kCollections[0] ) );

bool isCollection( const std::string &name )
{
  return collectionInfo( name ) != nullptr;
}

std::string idPrefixFor( const std::string &collection )
{
  const CollectionInfo *info = collectionInfo( collection );
  return info ? info->idPrefix : std::string();
}

Json::Value makeMapSpec( const std::string &layoutName, Json::Value page )
{
  Json::Value body( Json::objectValue );
  body["spec_version"] = kMapSpecCurrentVersion;
  body["layout_name"] = layoutName;
  if ( !page.isObject() )
    page = Json::Value( Json::objectValue );
  if ( !page.isMember( "width_mm" ) )
    page["width_mm"] = 297.0;
  if ( !page.isMember( "height_mm" ) )
    page["height_mm"] = 210.0;
  body["page"] = page;
  for ( int i = 0; i < kCollectionCount; ++i )
    body[kCollections[i]] = Json::Value( Json::arrayValue );
  return makeEnvelope( "map_spec", std::move( body ) );
}

std::string appendMapSpecItem( Json::Value &spec, const std::string &collection, Json::Value item )
{
  const CollectionInfo *info = collectionInfo( collection );
  if ( !info || !spec.isObject() )
    return std::string();
  if ( !spec.isMember( collection ) || !spec[collection].isArray() )
    spec[collection] = Json::Value( Json::arrayValue );

  // Assign the next free <prefix>-<n> id.
  std::set<std::string> used;
  for ( const auto &existing : spec[collection] )
  {
    if ( existing.isObject() && existing.isMember( "id" ) )
      used.insert( existing["id"].asString() );
  }
  int ordinal = 1;
  std::string id = std::string( info->idPrefix ) + "-" + std::to_string( ordinal );
  while ( used.count( id ) )
  {
    ++ordinal;
    id = std::string( info->idPrefix ) + "-" + std::to_string( ordinal );
  }
  if ( !item.isObject() )
    item = Json::Value( Json::objectValue );
  item["id"] = id;
  spec[collection].append( item );
  return id;
}

Json::Value findMapSpecItem( const Json::Value &spec, const std::string &id )
{
  if ( !spec.isObject() )
    return Json::Value();
  for ( int i = 0; i < kCollectionCount; ++i )
  {
    const char *collection = kCollections[i];
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    for ( int index = 0; index < static_cast<int>( spec[collection].size() ); ++index )
    {
      const Json::Value &item = spec[collection][index];
      if ( item.isObject() && item.isMember( "id" ) && item["id"].asString() == id )
      {
        Json::Value location( Json::objectValue );
        location["collection"] = collection;
        location["index"] = index;
        return location;
      }
    }
  }
  return Json::Value();
}

bool removeMapSpecItem( Json::Value &spec, const std::string &id )
{
  if ( !spec.isObject() )
    return false;
  for ( int i = 0; i < kCollectionCount; ++i )
  {
    const char *collection = kCollections[i];
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    Json::Value kept( Json::arrayValue );
    bool removed = false;
    for ( const auto &item : spec[collection] )
    {
      if ( !removed && item.isObject() && item.isMember( "id" ) && item["id"].asString() == id )
      {
        removed = true;
        continue;
      }
      kept.append( item );
    }
    if ( removed )
    {
      spec[collection] = kept;
      return true;
    }
  }
  return false;
}

std::vector<std::string> validateMapSpec( const Json::Value &spec )
{
  std::vector<std::string> problems;
  const std::string env = checkEnvelope( spec, "map_spec" );
  if ( !env.empty() )
  {
    problems.push_back( env );
    return problems;
  }
  if ( !spec.isMember( "spec_version" ) || !spec["spec_version"].isIntegral() )
  {
    problems.push_back( "missing integer field 'spec_version'" );
    return problems;
  }
  if ( spec["spec_version"].asInt() > kMapSpecCurrentVersion )
  {
    problems.push_back( "spec_version " + std::to_string( spec["spec_version"].asInt() ) +
                        " is newer than supported version " +
                        std::to_string( kMapSpecCurrentVersion ) );
    return problems;
  }

  double pageW = 0.0;
  double pageH = 0.0;
  if ( !spec.isMember( "page" ) || !spec["page"].isObject() ||
       !spec["page"].isMember( "width_mm" ) || !spec["page"].isMember( "height_mm" ) )
  {
    problems.push_back( "page must carry width_mm/height_mm" );
  }
  else
  {
    pageW = spec["page"]["width_mm"].asDouble();
    pageH = spec["page"]["height_mm"].asDouble();
    if ( pageW <= 0 || pageH <= 0 )
      problems.push_back( "page width_mm/height_mm must be positive" );
  }

  // Per-item checks + global id uniqueness.
  std::set<std::string> mapFrameIds;
  if ( spec.isMember( "map_frames" ) && spec["map_frames"].isArray() )
  {
    for ( const auto &frame : spec["map_frames"] )
      if ( frame.isObject() && frame.isMember( "id" ) )
        mapFrameIds.insert( frame["id"].asString() );
  }

  std::set<std::string> allIds;
  for ( int i = 0; i < kCollectionCount; ++i )
  {
    const CollectionInfo *info = &kCollectionInfos[i];
    if ( !spec.isMember( info->name ) )
    {
      problems.push_back( std::string( "missing collection '" ) + info->name + "'" );
      continue;
    }
    const Json::Value &items = spec[info->name];
    if ( !items.isArray() )
    {
      problems.push_back( std::string( "'" ) + info->name + "' must be an array" );
      continue;
    }
    for ( const auto &item : items )
    {
      if ( !item.isObject() )
      {
        problems.push_back( std::string( info->name ) + ": items must be objects" );
        continue;
      }
      const std::string id = item.isMember( "id" ) && item["id"].isString()
                               ? item["id"].asString()
                               : std::string();
      if ( id.empty() )
      {
        problems.push_back( std::string( info->name ) + ": every item needs a string id" );
        continue;
      }
      if ( !allIds.insert( id ).second )
        problems.push_back( "duplicate item id '" + id + "'" );

      // rect_mm is optional (the compiler applies component/defaults
      // geometry); when present it must be well-formed and on-page.
      if ( info->requiresRect && item.isMember( "rect_mm" ) )
        checkRect( item, id, pageW, pageH, problems );

      if ( info->mayReferenceMap && item.isMember( "map_ref" ) && item["map_ref"].isString() )
      {
        const std::string mapRef = item["map_ref"].asString();
        if ( !mapRef.empty() && !mapFrameIds.count( mapRef ) )
          problems.push_back( id + ": map_ref '" + mapRef + "' does not resolve to a map frame" );
      }

      // Collection-specific required fields.
      if ( std::string( info->name ) == "titles" && !item.isMember( "text" ) )
        problems.push_back( id + ": title needs 'text'" );
      if ( std::string( info->name ) == "source_notes" && !item.isMember( "text" ) )
        problems.push_back( id + ": source note needs 'text'" );
      if ( std::string( info->name ) == "charts" &&
           ( !item.isMember( "chart" ) || !item["chart"].isObject() ) )
        problems.push_back( id + ": chart needs a 'chart' object" );
    }
  }

  return problems;
}

Json::Value upgradeMapSpec( const Json::Value &doc )
{
  // v0 (pre-release drafts): {layout, page, items: [...]} — items carried a
  // "kind" discriminator instead of collections.
  if ( !doc.isObject() || !doc.isMember( "items" ) || !doc["items"].isArray() ||
       doc.isMember( "map_frames" ) )
    return doc;

  const std::string layoutName =
    doc.isMember( "layout_name" ) && doc["layout_name"].isString() ? doc["layout_name"].asString()
                                                                   : std::string( "mapspec" );
  Json::Value upgraded = makeMapSpec( layoutName, doc.get( "page", Json::Value( Json::objectValue ) ) );
  const std::map<std::string, std::string> kindToCollection = {
    { "map", "map_frames" },   { "legend", "legends" },   { "scale_bar", "scale_bars" },
    { "scalebar", "scale_bars" }, { "north_arrow", "north_arrows" }, { "title", "titles" },
    { "label", "labels" },     { "chart", "charts" },     { "colorbar", "colorbars" },
    { "inset_map", "inset_maps" }, { "grid", "grids" },   { "annotation", "annotations" },
    { "source_note", "source_notes" }, { "constraint", "constraints" },
  };
  for ( const auto &item : doc["items"] )
  {
    if ( !item.isObject() || !item.isMember( "kind" ) )
      continue;
    const auto it = kindToCollection.find( item["kind"].asString() );
    if ( it == kindToCollection.end() )
      continue;
    Json::Value migrated = item;
    migrated.removeMember( "kind" );
    appendMapSpecItem( upgraded, it->second, std::move( migrated ) );
  }
  return upgraded;
}

bool applyMapSpecPatch( Json::Value &spec, const Json::Value &patch, std::string *error )
{
  const auto fail = [ error ]( const std::string &message ) {
    if ( error )
      *error = message;
    return false;
  };
  if ( !patch.isObject() || !patch.isMember( "op" ) || !patch["op"].isString() )
    return fail( "patch needs string 'op'" );

  const std::string op = patch["op"].asString();
  if ( op == "add" )
  {
    if ( !patch.isMember( "collection" ) || !patch["collection"].isString() )
      return fail( "add patch needs 'collection'" );
    const std::string collection = patch["collection"].asString();
    if ( !isCollection( collection ) )
      return fail( "unknown collection '" + collection + "'" );
    const std::string id = appendMapSpecItem( spec, collection, patch.get( "value", Json::Value() ) );
    if ( id.empty() )
      return fail( "failed to append item" );
    return true;
  }
  if ( op == "update" || op == "remove" )
  {
    if ( !patch.isMember( "id" ) || !patch["id"].isString() )
      return fail( op + " patch needs 'id'" );
    const std::string id = patch["id"].asString();
    const Json::Value location = findMapSpecItem( spec, id );
    if ( location.isNull() )
      return fail( "unknown item id '" + id + "'" );
    if ( op == "remove" )
      return removeMapSpecItem( spec, id );
    // Update: shallow-merge value fields into the item. The id itself is immutable.
    Json::Value &item = spec[location["collection"].asString()][location["index"].asInt()];
    const Json::Value &value = patch.get( "value", Json::Value( Json::objectValue ) );
    if ( !value.isObject() )
      return fail( "update patch 'value' must be an object" );
    for ( const auto &key : value.getMemberNames() )
    {
      if ( key == "id" )
        continue;
      item[key] = value[key];
    }
    return true;
  }
  return fail( "unknown op '" + op + "' (expected add|update|remove)" );
}

bool applyMapSpecPatches( Json::Value &spec, const Json::Value &patches, std::string *error )
{
  if ( !patches.isArray() )
  {
    if ( error )
      *error = "patches must be an array";
    return false;
  }
  for ( const auto &patch : patches )
  {
    if ( !applyMapSpecPatch( spec, patch, error ) )
      return false;
  }
  return true;
}

} // namespace sicnu::agent::mapspec

// src/agent/mapspec/mapspec.cpp
#include "mapspec.h"

#include "mapspec_conditions.h"
#include "../contracts/spatial_contracts.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace sicnu::agent::mapspec {

using namespace sicnu::agent::contracts;

// Defined below the collection tables; used by the v2 item checks.
bool isAnchorEdge( const std::string &edge );
bool isConstraintKind( const std::string &kind );

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

/// Validates one rect_mm value; appends problems.
void checkRect( const Json::Value &item, const std::string &id,
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
  // Page-bounds violations are cartography-preflight findings (MAP_OFF_PAGE,
  // repairable), not structural validation failures.
}

bool isPositiveSizeArray( const Json::Value &value )
{
  return value.isArray() && value.size() == 2 && value[0].isNumeric() && value[1].isNumeric() &&
         value[0].asDouble() > 0 && value[1].asDouble() > 0;
}

/// v2 per-item shape checks (anchor, min/max size, z_index, page, binding,
/// component reference). Appends problems.
void checkV2ItemFields( const Json::Value &item, const std::string &id,
                        std::vector<std::string> &problems )
{
  if ( item.isMember( "anchor" ) )
  {
    const Json::Value &anchor = item["anchor"];
    if ( !anchor.isObject() )
      problems.push_back( id + ": anchor must be an object" );
    else
    {
      if ( anchor.isMember( "edge" ) &&
           ( !anchor["edge"].isString() ||
             !isAnchorEdge( anchor["edge"].asString() ) ) )
        problems.push_back( id + ": anchor.edge must be one of top-left|top-center|top-right|"
                                   "center-left|center|center-right|bottom-left|bottom-center|"
                                   "bottom-right" );
      if ( anchor.isMember( "margin_mm" ) &&
           ( !anchor["margin_mm"].isNumeric() || anchor["margin_mm"].asDouble() < 0 ) )
        problems.push_back( id + ": anchor.margin_mm must be a non-negative number" );
    }
  }
  for ( const char *member : { "min_size_mm", "max_size_mm" } )
    if ( item.isMember( member ) && !isPositiveSizeArray( item[member] ) )
      problems.push_back( id + ": " + member + " must be [width_mm, height_mm] with positive values" );
  if ( item.isMember( "z_index" ) && !item["z_index"].isIntegral() )
    problems.push_back( id + ": z_index must be an integer" );
  if ( item.isMember( "page" ) &&
       ( !item["page"].isIntegral() || item["page"].asInt() < 0 ) )
    problems.push_back( id + ": page must be a non-negative integer index" );
  if ( item.isMember( "binding" ) && !item["binding"].isObject() )
    problems.push_back( id + ": binding must be an object" );
  if ( item.isMember( "source_component" ) )
  {
    const Json::Value &reference = item["source_component"];
    const bool valid =
      reference.isString() ||
      ( reference.isObject() && reference.isMember( "id" ) && reference["id"].isString() );
    if ( !valid )
      problems.push_back( id + ": source_component must be an id string or {id, variant}" );
  }
}

/// v3 per-item checks: bounded conditional expressions, style references and
/// inset locator descriptors. Appends problems.
void checkV3ItemFields( const Json::Value &item, const std::string &id, const std::string &collection,
                        const std::set<std::string> &mapFrameIds,
                        const std::set<std::string> &insetIds,
                        std::vector<std::string> &problems )
{
  for ( const char *member : { "visible_if", "content_if" } )
  {
    if ( !item.isMember( member ) )
      continue;
    if ( !item[member].isString() )
    {
      problems.push_back( id + ": " + member + " must be a condition string" );
      continue;
    }
    std::vector<std::string> conditionProblems;
    if ( !mapspec::validateConditionSyntax( item[member].asString(), &conditionProblems ) )
      for ( const auto &problem : conditionProblems )
        problems.push_back( id + ": " + member + ": " + problem );
  }
  if ( item.isMember( "style_ref" ) && !item["style_ref"].isString() )
    problems.push_back( id + ": style_ref must be a style id string" );
  if ( collection == "inset_maps" && item.isMember( "locator" ) )
  {
    const Json::Value &locator = item["locator"];
    if ( !locator.isObject() )
    {
      problems.push_back( id + ": locator must be an object" );
    }
    else
    {
      if ( !locator.isMember( "target" ) || !locator["target"].isString() ||
           locator["target"].asString().empty() )
      {
        problems.push_back( id + ": locator.target must name the referenced map frame" );
      }
      else if ( !mapFrameIds.count( locator["target"].asString() ) &&
                !insetIds.count( locator["target"].asString() ) )
      {
        // Nested locators are legal: an inset may target another (earlier)
        // inset; the compiler attaches the overview to whichever map item.
        problems.push_back( id + ": locator.target '" + locator["target"].asString() +
                            "' does not resolve to a map frame or inset" );
      }
      if ( locator.isMember( "style" ) )
      {
        const std::string style = locator["style"].isString() ? locator["style"].asString() : "";
        if ( style != "outline" && style != "region" && style != "frame" )
          problems.push_back( id + ": locator.style must be outline|region|frame" );
      }
      for ( const char *member : { "stroke_mm", "label_size_pt" } )
        if ( locator.isMember( member ) &&
             ( !locator[member].isNumeric() || locator[member].asDouble() <= 0 ) )
          problems.push_back( id + ": locator." + member + " must be positive" );
      if ( locator.isMember( "label" ) && !locator["label"].isString() )
        problems.push_back( id + ": locator.label must be a string" );
    }
  }
}

} // namespace

const char *const kCollections[] = { "map_frames", "layers", "symbols", "legends",
                                     "north_arrows", "scale_bars", "titles", "labels",
                                     "charts", "colorbars", "inset_maps", "grids",
                                     "annotations", "source_notes", "constraints" };
const int kCollectionCount = static_cast<int>( sizeof( kCollections ) / sizeof( kCollections[0] ) );

bool isAnchorEdge( const std::string &edge )
{
  static const char *const kEdges[] = { "top-left", "top-center", "top-right",
                                        "center-left", "center", "center-right",
                                        "bottom-left", "bottom-center", "bottom-right" };
  for ( const char *candidate : kEdges )
    if ( edge == candidate )
      return true;
  return false;
}

bool isConstraintKind( const std::string &kind )
{
  static const char *const kKinds[] = { "align", "match_width", "match_height", "stack",
                                        "distribute", "above", "below", "left_of",
                                        "right_of", "inside", "keep_with",
                                        "avoid_overlap", "fit_content" };
  for ( const char *candidate : kKinds )
    if ( kind == candidate )
      return true;
  return false;
}

bool isRelativeConstraintKind( const std::string &kind )
{
  static const char *const kKinds[] = { "above", "below", "left_of", "right_of", "inside",
                                        "keep_with", "avoid_overlap", "fit_content" };
  for ( const char *candidate : kKinds )
    if ( kind == candidate )
      return true;
  return false;
}

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

  if ( !spec.isMember( "layout_name" ) || !spec["layout_name"].isString() ||
       spec["layout_name"].asString().empty() )
    problems.push_back( "missing non-empty string field 'layout_name'" );

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
  // Inset ids: locator targets may reference another (earlier-declared) inset.
  std::set<std::string> insetIds;
  if ( spec.isMember( "inset_maps" ) && spec["inset_maps"].isArray() )
  {
    for ( const auto &inset : spec["inset_maps"] )
      if ( inset.isObject() && inset.isMember( "id" ) )
        insetIds.insert( inset["id"].asString() );
  }

  std::set<std::string> allIds;
  int totalItems = 0;
  for ( int i = 0; i < kCollectionCount; ++i )
    if ( spec.isMember( kCollectionInfos[i].name ) && spec[kCollectionInfos[i].name].isArray() )
      totalItems += static_cast<int>( spec[kCollectionInfos[i].name].size() );
  // Bounded-document contract: preflight/repair are synchronous agent tools,
  // so the document size they may be handed is capped up front.
  static constexpr int kMaxItemsPerDocument = 2000;
  if ( totalItems > kMaxItemsPerDocument )
  {
    problems.push_back( "document exceeds " + std::to_string( kMaxItemsPerDocument ) +
                        " items (" + std::to_string( totalItems ) + ")" );
    return problems;
  }
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
        checkRect( item, id, problems );

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

      // v2 composition fields.
      checkV2ItemFields( item, id, problems );
      // v3 knowledge-platform fields.
      checkV3ItemFields( item, id, info->name, mapFrameIds, insetIds, problems );
    }
  }

  // --- v2: style block ------------------------------------------------------
  if ( spec.isMember( "style" ) )
  {
    const Json::Value &style = spec["style"];
    if ( !style.isObject() )
      problems.push_back( "style must be an object" );
    else
    {
      if ( style.isMember( "token_set" ) && !style["token_set"].isString() )
        problems.push_back( "style.token_set must be a string" );
      if ( style.isMember( "medium" ) && style["medium"].isString() &&
           style["medium"].asString() != "print" && style["medium"].asString() != "screen" )
        problems.push_back( "style.medium must be print|screen" );
      if ( style.isMember( "overrides" ) && !style["overrides"].isObject() )
        problems.push_back( "style.overrides must be an object" );
    }
  }

  // --- v2: slots (role → item bindings) -------------------------------------
  if ( spec.isMember( "slots" ) )
  {
    const Json::Value &slotList = spec["slots"]; // not `slots` — Qt moc macro
    if ( !slotList.isArray() )
    {
      problems.push_back( "slots must be an array" );
    }
    else
    {
      std::set<std::string> roles;
      for ( const auto &slot : slotList )
      {
        if ( !slot.isObject() || !slot.isMember( "role" ) || !slot["role"].isString() ||
             !slot.isMember( "item" ) || !slot["item"].isString() )
        {
          problems.push_back( "every slot needs string role and item" );
          continue;
        }
        const std::string role = slot["role"].asString();
        if ( !roles.insert( role ).second )
          problems.push_back( "duplicate slot role '" + role + "'" );
        if ( findMapSpecItem( spec, slot["item"].asString() ).isNull() )
          problems.push_back( "slot '" + role + "' references unknown item '" +
                              slot["item"].asString() + "'" );
      }
    }
  }

  // --- v2: solver-enforced constraints --------------------------------------
  if ( spec.isMember( "constraints" ) && spec["constraints"].isArray() )
  {
    for ( const auto &constraint : spec["constraints"] )
    {
      if ( !constraint.isObject() || !constraint.isMember( "id" ) )
        continue; // structural item checks above handle malformed entries
      const std::string cid = constraint["id"].asString();
      if ( constraint.isMember( "kind" ) && constraint["kind"].isString() )
      {
        const std::string kind = constraint["kind"].asString();
        if ( !isConstraintKind( kind ) )
        {
          // "frame_style" is the legacy v1 frame-decoration item; other
          // kinds are rejected so typos cannot silently no-op.
          if ( kind != "frame_style" )
            problems.push_back( cid + ": unknown constraint kind '" + kind +
                                "' (align|match_width|match_height|stack|distribute|"
                                "above|below|left_of|right_of|inside|keep_with|"
                                "avoid_overlap|fit_content|frame_style)" );
          continue;
        }
        if ( isRelativeConstraintKind( kind ) )
        {
          // v3 relative constraints: fit_content resizes one declared item;
          // all others pair exactly one target with one follower.
          const int expectedItems = kind == "fit_content" ? 1 : 2;
          if ( !constraint.isMember( "items" ) || !constraint["items"].isArray() ||
               static_cast<int>( constraint["items"].size() ) != expectedItems )
          {
            problems.push_back( cid + ": constraint '" + kind + "' needs an items array with exactly " +
                                std::to_string( expectedItems ) + " id(s)" );
            continue;
          }
          for ( const auto &reference : constraint["items"] )
          {
            if ( !reference.isString() )
              continue;
            if ( findMapSpecItem( spec, reference.asString() ).isNull() )
              problems.push_back( cid + ": constraint item '" + reference.asString() +
                                  "' does not resolve" );
          }
          if ( kind == "fit_content" )
          {
            const Json::Value &content = constraint.get( "content_mm", Json::Value() );
            if ( !isPositiveSizeArray( content ) )
              problems.push_back( cid + ": fit_content needs content_mm [width_mm, height_mm]" );
          }
          if ( constraint.isMember( "gap_mm" ) &&
               ( !constraint["gap_mm"].isNumeric() || constraint["gap_mm"].asDouble() < 0 ) )
            problems.push_back( cid + ": gap_mm must be a non-negative number" );
          continue;
        }
        if ( !constraint.isMember( "items" ) || !constraint["items"].isArray() ||
             constraint["items"].size() < 2 )
        {
          problems.push_back( cid + ": constraint needs an items array with at least 2 ids" );
          continue;
        }
        for ( const auto &reference : constraint["items"] )
        {
          if ( !reference.isString() )
            continue;
          if ( findMapSpecItem( spec, reference.asString() ).isNull() )
            problems.push_back( cid + ": constraint item '" + reference.asString() +
                                "' does not resolve" );
        }
        if ( kind == "align" &&
             ( !constraint.isMember( "edge" ) || !constraint["edge"].isString() ) )
          problems.push_back( cid + ": align constraint needs edge (top|bottom|left|right)" );
        if ( kind == "stack" &&
             ( !constraint.isMember( "direction" ) || !constraint["direction"].isString() ) )
          problems.push_back( cid + ": stack constraint needs direction "
                                       "(below|above|left_of|right_of)" );
        if ( kind == "distribute" &&
             ( !constraint.isMember( "direction" ) || !constraint["direction"].isString() ) )
          problems.push_back( cid + ": distribute constraint needs direction "
                                       "(horizontal|vertical)" );
      }
    }
  }

  // --- v2: pages + item page indices ----------------------------------------
  if ( spec.isMember( "pages" ) )
  {
    const Json::Value &pages = spec["pages"];
    if ( !pages.isArray() )
      problems.push_back( "pages must be an array" );
    else if ( pages.size() > 10 )
      problems.push_back( "pages capped at 10 per document" );
    else
    {
      for ( const auto &pageEntry : pages )
      {
        if ( !pageEntry.isObject() || !pageEntry.isMember( "width_mm" ) ||
             !pageEntry["width_mm"].isNumeric() || !pageEntry.isMember( "height_mm" ) ||
             !pageEntry["height_mm"].isNumeric() || pageEntry["width_mm"].asDouble() <= 0 ||
             pageEntry["height_mm"].asDouble() <= 0 )
        {
          problems.push_back( "every page needs positive width_mm/height_mm" );
          continue;
        }
        // v3: page roles and feature-gated pages.
        if ( pageEntry.isMember( "role" ) )
        {
          const std::string role = pageEntry["role"].isString() ? pageEntry["role"].asString() : "";
          if ( role != "cover" && role != "map" && role != "report" && role != "appendix" )
            problems.push_back( "page.role must be cover|map|report|appendix" );
        }
        if ( pageEntry.isMember( "page_if" ) )
        {
          if ( !pageEntry["page_if"].isString() )
          {
            problems.push_back( "page.page_if must be a condition string" );
          }
          else
          {
            std::vector<std::string> conditionProblems;
            if ( !mapspec::validateConditionSyntax( pageEntry["page_if"].asString(),
                                                    &conditionProblems ) )
              for ( const auto &problem : conditionProblems )
                problems.push_back( std::string( "page_if: " ) + problem );
          }
        }
      }
    }
  }
  const int maxPageIndex =
    spec.isMember( "pages" ) && spec["pages"].isArray() ? static_cast<int>( spec["pages"].size() ) : 0;
  for ( int i = 0; i < kCollectionCount; ++i )
  {
    if ( !spec.isMember( kCollectionInfos[i].name ) || !spec[kCollectionInfos[i].name].isArray() )
      continue;
    for ( const auto &item : spec[kCollectionInfos[i].name] )
    {
      if ( !item.isObject() || !item.isMember( "id" ) || !item.isMember( "page" ) )
        continue;
      if ( item["page"].isIntegral() && item["page"].asInt() > maxPageIndex )
        problems.push_back( item["id"].asString() + ": page index " +
                            std::to_string( item["page"].asInt() ) +
                            " exceeds declared pages (" + std::to_string( maxPageIndex ) +
                            " additional)" );
    }
  }

  // --- v2/v3: atlas hook -------------------------------------------------------
  if ( spec.isMember( "page" ) && spec["page"].isObject() && spec["page"].isMember( "atlas" ) )
  {
    const Json::Value &atlas = spec["page"]["atlas"];
    if ( !atlas.isObject() )
      problems.push_back( "page.atlas must be an object" );
    else
    {
      if ( atlas.isMember( "enabled" ) && !atlas["enabled"].isBool() )
        problems.push_back( "page.atlas.enabled must be a boolean" );
      if ( atlas.isMember( "coverage_layer" ) && !atlas["coverage_layer"].isString() )
        problems.push_back( "page.atlas.coverage_layer must be a layer reference string" );
      if ( atlas.isMember( "filename_expression" ) && !atlas["filename_expression"].isString() )
        problems.push_back( "page.atlas.filename_expression must be a string" );
      // v3 surface: filter/sort/margins/expressions/feature variables. QGIS
      // expression strings are passed through verbatim (QGIS validates them
      // at atlas preparation); structure is checked here.
      for ( const char *member : { "filter_expression", "filename_expression",
                                   "page_number_expression", "sort_expression" } )
        if ( atlas.isMember( member ) && !atlas[member].isString() )
          problems.push_back( std::string( "page.atlas." ) + member + " must be a string" );
      if ( atlas.isMember( "sort_by" ) && !atlas["sort_by"].isString() )
        problems.push_back( "page.atlas.sort_by must be a field name string" );
      if ( atlas.isMember( "sort_order" ) )
      {
        const std::string order = atlas["sort_order"].isString() ? atlas["sort_order"].asString() : "";
        if ( order != "asc" && order != "desc" )
          problems.push_back( "page.atlas.sort_order must be asc|desc" );
      }
      if ( atlas.isMember( "margins_mm" ) )
      {
        const Json::Value &margins = atlas["margins_mm"];
        if ( !margins.isArray() || margins.size() != 4 )
          problems.push_back( "page.atlas.margins_mm must be [left, right, top, bottom]" );
        else
          for ( const auto &margin : margins )
            if ( !margin.isNumeric() || margin.asDouble() < 0 )
              problems.push_back( "page.atlas.margins_mm entries must be non-negative numbers" );
      }
      if ( atlas.isMember( "margin_fraction" ) )
      {
        const Json::Value &fraction = atlas["margin_fraction"];
        if ( !fraction.isNumeric() || fraction.asDouble() < 0 || fraction.asDouble() >= 1 )
          problems.push_back( "page.atlas.margin_fraction must be within [0, 1)" );
      }
      if ( atlas.isMember( "feature_variables" ) )
      {
        if ( !atlas["feature_variables"].isArray() )
          problems.push_back( "page.atlas.feature_variables must be an array" );
        else if ( atlas["feature_variables"].size() > 32 )
          problems.push_back( "page.atlas.feature_variables capped at 32 entries" );
      }
      if ( atlas.isMember( "filter" ) && !atlas["filter"].isString() )
        problems.push_back( "page.atlas.filter must be a QGIS expression string" );
    }
  }

  return problems;
}

Json::Value upgradeMapSpec( const Json::Value &doc )
{
  Json::Value upgraded = doc;
  // v0 (pre-release drafts): {layout, page, items: [...]} — items carried a
  // "kind" discriminator instead of collections.
  const bool isV0 = doc.isObject() && doc.isMember( "items" ) && doc["items"].isArray() &&
                    !doc.isMember( "map_frames" );
  if ( isV0 )
  {
    const std::string layoutName =
      doc.isMember( "layout_name" ) && doc["layout_name"].isString() ? doc["layout_name"].asString()
                                                                     : std::string( "mapspec" );
    upgraded = makeMapSpec( layoutName, doc.get( "page", Json::Value( Json::objectValue ) ) );
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
  }
  // v1 → v2 → v3: every newer-version field is optional, so the upgrade is a
  // version bump. Idempotent: current-version documents and non-envelope
  // inputs pass through unchanged.
  const std::string env = checkEnvelope( upgraded, "map_spec" );
  if ( env.empty() && upgraded["spec_version"].asInt() < kMapSpecCurrentVersion )
    upgraded["spec_version"] = kMapSpecCurrentVersion;
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

Json::Value resolveMapSpecConditions( Json::Value &spec, const Json::Value &context,
                                      std::vector<std::string> *errors )
{
  Json::Value ledger( Json::arrayValue );
  if ( !spec.isObject() || !spec.isMember( "condition_context" ) )
    return ledger;
  if ( !context.isObject() && !context.isNull() )
  {
    if ( errors )
      errors->push_back( "condition context must be an object" );
    return ledger;
  }

  auto record = [ &ledger ]( const std::string &id, const std::string &field,
                             const std::string &condition, bool visible, const std::string &error ) {
    Json::Value entry( Json::objectValue );
    entry["id"] = id;
    entry["field"] = field;
    entry["condition"] = condition;
    entry["outcome"] = visible ? "visible" : "hidden";
    if ( !error.empty() )
      entry["error"] = error;
    ledger.append( entry );
  };

  const Json::Value &ctx = context;

  // 1. Items: visible_if prunes whole items; content_if strips content.
  std::set<std::string> removedIds;
  for ( int c = 0; c < kCollectionCount; ++c )
  {
    const char *collection = kCollections[c];
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    Json::Value kept( Json::arrayValue );
    for ( const auto &item : spec[collection] )
    {
      if ( !item.isObject() || ( !item.isMember( "visible_if" ) && !item.isMember( "content_if" ) ) )
      {
        kept.append( item );
        continue;
      }
      const std::string id = item.isMember( "id" ) && item["id"].isString()
                               ? item["id"].asString()
                               : std::string( "?" );
      bool keep = true;
      if ( item.isMember( "visible_if" ) && item["visible_if"].isString() )
      {
        const std::string condition = item["visible_if"].asString();
        std::string evalError;
        bool value = false;
        if ( mapspec::evaluateCondition( condition, ctx, &value, &evalError ) )
        {
          keep = value;
          record( id, "visible_if", condition, value, std::string() );
        }
        else
        {
          // Conservative: an unevaluable condition keeps the content.
          keep = true;
          record( id, "visible_if", condition, true, evalError );
          if ( errors )
            errors->push_back( id + ": visible_if: " + evalError );
        }
      }
      if ( !keep )
      {
        removedIds.insert( id );
        continue;
      }
      if ( item.isMember( "content_if" ) && item["content_if"].isString() )
      {
        const std::string condition = item["content_if"].asString();
        std::string evalError;
        bool value = true;
        if ( mapspec::evaluateCondition( condition, ctx, &value, &evalError ) && !value )
        {
          Json::Value pruned = item;
          pruned.removeMember( "content" );
          pruned.removeMember( "content_if" );
          const bool emptyWithoutContent = !pruned.isMember( "text" ) &&
                                           !pruned.isMember( "chart" ) && !pruned.isMember( "colors" );
          record( id, "content_if", condition, false, std::string() );
          if ( emptyWithoutContent )
          {
            removedIds.insert( id );
            continue; // slot furniture without content: drop entirely
          }
          kept.append( pruned );
          continue;
        }
        if ( !evalError.empty() )
        {
          if ( errors )
            errors->push_back( id + ": content_if: " + evalError );
          record( id, "content_if", condition, true, evalError );
        }
      }
      kept.append( item );
    }
    spec[collection] = kept;
  }

  // 2. Pages: page_if prunes pages; items on pruned pages are removed and
  // surviving page indices remap compactly.
  if ( spec.isMember( "pages" ) && spec["pages"].isArray() )
  {
    std::vector<int> keptPages;
    for ( int index = 0; index < static_cast<int>( spec["pages"].size() ); ++index )
    {
      const Json::Value &pageEntry = spec["pages"][index];
      if ( !pageEntry.isObject() || !pageEntry.isMember( "page_if" ) ||
           !pageEntry["page_if"].isString() )
      {
        keptPages.push_back( index );
        continue;
      }
      const std::string condition = pageEntry["page_if"].asString();
      std::string evalError;
      bool value = true;
      if ( mapspec::evaluateCondition( condition, ctx, &value, &evalError ) && !value )
      {
        record( "page-" + std::to_string( index + 1 ), "page_if", condition, false, std::string() );
        continue;
      }
      if ( !evalError.empty() )
      {
        if ( errors )
          errors->push_back( std::string( "page_if: " ) + evalError );
        record( "page-" + std::to_string( index + 1 ), "page_if", condition, true, evalError );
      }
      keptPages.push_back( index );
    }
    if ( keptPages.size() != static_cast<size_t>( spec["pages"].size() ) )
    {
      std::map<int, int> remap;
      Json::Value pages( Json::arrayValue );
      for ( int newIndex = 0; newIndex < static_cast<int>( keptPages.size() ); ++newIndex )
      {
        remap[keptPages[newIndex] + 1] = newIndex + 1; // item page indices are 1-based
        pages.append( spec["pages"][keptPages[newIndex]] );
      }
      spec["pages"] = pages;
      for ( int c = 0; c < kCollectionCount; ++c )
      {
        const char *collection = kCollections[c];
        if ( !spec.isMember( collection ) || !spec[collection].isArray() )
          continue;
        Json::Value remapped( Json::arrayValue );
        for ( const auto &item : spec[collection] )
        {
          if ( !item.isObject() || !item.isMember( "page" ) || !item["page"].isIntegral() )
          {
            remapped.append( item );
            continue;
          }
          const int oldPage = item["page"].asInt();
          if ( removedIds.count( item.isMember( "id" ) && item["id"].isString()
                                   ? item["id"].asString()
                                   : std::string() ) )
            continue;
          if ( oldPage > 0 && remap.count( oldPage ) )
          {
            Json::Value moved = item;
            moved["page"] = remap[oldPage];
            remapped.append( moved );
          }
          else if ( oldPage == 0 )
          {
            remapped.append( item );
          }
          // oldPage > 0 without a remap target: the page was pruned; drop the item.
        }
        spec[collection] = remapped;
      }
    }
  }

  // The context is consumed: clear it so repeated compiles stay deterministic
  // and the ledger (not the context) documents the outcome.
  spec.removeMember( "condition_context" );
  return ledger;
}

} // namespace sicnu::agent::mapspec

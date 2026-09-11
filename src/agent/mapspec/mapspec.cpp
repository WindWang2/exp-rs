// src/agent/mapspec/mapspec.cpp
#include "mapspec.h"

#include "mapspec_conditions.h"
#include "../cartography/typography.h"
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
  // Platform 8.0: declared typography surface on the font block.
  if ( item.isMember( "font" ) && item["font"].isObject() )
  {
    const Json::Value &font = item["font"];
    if ( font.isMember( "break_policy" ) &&
         ( !font["break_policy"].isString() ||
           !sicnu::agent::cartography::isTextBreakPolicy( font["break_policy"].asString() ) ) )
      problems.push_back( id + ": font.break_policy must be none|halfwidth" );
    if ( font.isMember( "line_height" ) &&
         ( !font["line_height"].isNumeric() || font["line_height"].asDouble() <= 0 ||
           font["line_height"].asDouble() > 3.0 ) )
      problems.push_back( id + ": font.line_height must be a number in (0, 3]" );
  }
  // Platform 8.0: typed binding surface. Shape-checked, not key-closed —
  // the goal is better diagnostics for malformed documents without breaking
  // shipped templates (charts carry mode/data/matrix here too).
  if ( item.isMember( "binding" ) && item["binding"].isObject() )
  {
    const Json::Value &binding = item["binding"];
    if ( binding.isMember( "mode" ) &&
         ( !binding["mode"].isString() || binding["mode"].asString().empty() ) )
      problems.push_back( id + ": binding.mode must be a non-empty string" );
    for ( const char *member :
          { "layer", "field", "expression", "x_expression", "y_expression", "filter" } )
      if ( binding.isMember( member ) && !binding[member].isString() )
        problems.push_back( id + ": binding." + member + " must be a string" );
    if ( binding.isMember( "params" ) && !binding["params"].isObject() )
      problems.push_back( id + ": binding.params must be an object" );
    if ( binding.isMember( "data" ) )
    {
      if ( !binding["data"].isArray() )
        problems.push_back( id + ": binding.data must be an array" );
      else if ( static_cast<int>( binding["data"].size() ) > 256 )
        problems.push_back( id + ": binding.data exceeds the 256 entry budget" );
    }
    if ( binding.isMember( "matrix" ) )
    {
      const Json::Value &matrix = binding["matrix"];
      if ( !matrix.isObject() || !matrix.isMember( "labels" ) || !matrix["labels"].isArray() ||
           !matrix.isMember( "rows" ) || !matrix["rows"].isArray() )
        problems.push_back( id + ": binding.matrix must be {labels: [n], rows: [n][n]}" );
      else
      {
        const int n = static_cast<int>( matrix["labels"].size() );
        if ( n > 24 )
          problems.push_back( id + ": binding.matrix exceeds the 24x24 budget" );
        else if ( static_cast<int>( matrix["rows"].size() ) != n )
          problems.push_back( id + ": binding.matrix must be square (rows == labels)" );
        else
        {
          for ( const auto &row : matrix["rows"] )
            if ( !row.isArray() || static_cast<int>( row.size() ) != n )
            {
              problems.push_back( id + ": binding.matrix rows must all carry n entries" );
              break;
            }
        }
      }
    }
  }
  // Platform 6.0 (Milestone C): bounded composite children grammar. Items
  // carry role-qualified blocks materialized from composite components;
  // depth-1, unique roles, small budget — furniture structure, not UI trees.
  if ( item.isMember( "children" ) )
  {
    const Json::Value &children = item["children"];
    if ( !children.isArray() )
    {
      problems.push_back( id + ": children must be an array" );
    }
    else
    {
      if ( static_cast<int>( children.size() ) > 16 )
        problems.push_back( id + ": children exceed the 16 entry budget" );
      std::set<std::string> roles;
      int index = 0;
      for ( const auto &child : children )
      {
        const std::string where = id + ": children[" + std::to_string( index++ ) + "]";
        if ( !child.isObject() )
        {
          problems.push_back( where + " must be an object" );
          continue;
        }
        if ( !child.isMember( "role" ) || !child["role"].isString() ||
             child["role"].asString().empty() )
          problems.push_back( where + " needs a non-empty string role" );
        else if ( !roles.insert( child["role"].asString() ).second )
          problems.push_back( id + ": duplicate child role '" + child["role"].asString() + "'" );
        if ( child.isMember( "children" ) )
          problems.push_back( where + " must not nest children (depth bounded to 1)" );
        if ( child.isMember( "required" ) && !child["required"].isBool() )
          problems.push_back( where + ".required must be a boolean" );
        for ( const char *member : { "content", "overrides" } )
          if ( child.isMember( member ) && !child[member].isObject() )
            problems.push_back( where + "." + member + " must be an object" );
        for ( const char *member : { "source_component", "description" } )
          if ( child.isMember( member ) && !child[member].isString() )
            problems.push_back( where + "." + member + " must be a string" );
      }
    }
  }
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
      // Platform 8.0 connector graphics: presence of the (possibly empty)
      // connector object enables the inset↔target relationship line.
      if ( locator.isMember( "connector" ) )
      {
        if ( !locator["connector"].isObject() )
          problems.push_back( id + ": locator.connector must be an object" );
        else
        {
          const Json::Value &connector = locator["connector"];
          if ( connector.isMember( "style" ) )
          {
            const std::string style =
              connector["style"].isString() ? connector["style"].asString() : "";
            if ( style != "solid" && style != "dash" )
              problems.push_back( id + ": locator.connector.style must be solid|dash" );
          }
          if ( connector.isMember( "stroke_mm" ) &&
               ( !connector["stroke_mm"].isNumeric() || connector["stroke_mm"].asDouble() <= 0 ||
                 connector["stroke_mm"].asDouble() > 5.0 ) )
            problems.push_back( id + ": locator.connector.stroke_mm must be positive (≤ 5 mm)" );
          if ( connector.isMember( "color" ) && !connector["color"].isString() )
            problems.push_back( id + ": locator.connector.color must be a string" );
        }
      }
    }
  }
  // Platform 9.0: declared chart/colorbar overlay intent. `overlay_on`
  // names the map frame(s) the item intentionally covers; references must
  // resolve so a typo can never silently weaken the coverage rule.
  if ( item.isMember( "overlay_on" ) )
  {
    const Json::Value &overlay = item["overlay_on"];
    if ( overlay.isString() )
    {
      if ( overlay.asString().empty() )
        problems.push_back( id + ": overlay_on must be a non-empty frame id" );
      else if ( !mapFrameIds.count( overlay.asString() ) )
        problems.push_back( id + ": overlay_on '" + overlay.asString() +
                            "' does not resolve to a map frame" );
    }
    else if ( overlay.isArray() )
    {
      if ( overlay.empty() )
        problems.push_back( id + ": overlay_on array must not be empty" );
      for ( const auto &frameId : overlay )
        if ( !frameId.isString() || frameId.asString().empty() )
          problems.push_back( id + ": overlay_on entries must be non-empty frame ids" );
        else if ( !mapFrameIds.count( frameId.asString() ) )
          problems.push_back( id + ": overlay_on '" + frameId.asString() +
                              "' does not resolve to a map frame" );
    }
    else
      problems.push_back( id + ": overlay_on must be a frame id or an array of frame ids" );
  }
  // Platform 9.0: text items may declare an atlas-driven `expression`
  // (QGIS-native `[% … %]` label markup; validity is checked at compile) and
  // a `continuation` block for cross-page references.
  if ( item.isMember( "expression" ) &&
       ( !item["expression"].isString() || item["expression"].asString().empty() ) )
    problems.push_back( id + ": expression must be a non-empty QGIS expression string" );
  if ( item.isMember( "continuation" ) )
  {
    const Json::Value &continuation = item["continuation"];
    if ( !continuation.isObject() )
      problems.push_back( id + ": continuation must be an object" );
    else
    {
      if ( continuation.isMember( "label" ) && !continuation["label"].isString() )
        problems.push_back( id + ": continuation.label must be a string" );
      if ( item.isMember( "page" ) && item["page"].isIntegral() && item["page"].asInt() < 1 )
        problems.push_back( id + ": continuation is only meaningful on items moved "
                                 "past page 0 (declare page or a page_break)" );
    }
  }
}

} // namespace


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
                                        "avoid_overlap", "fit_content",
                                        "page_break" };
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

bool isConstraintHardness( const std::string &hardness )
{
  return hardness == "hard" || hardness == "soft";
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
  if ( spec.isMember( "items" ) && spec["items"].isArray() )
  {
    for ( int index = 0; index < static_cast<int>( spec["items"].size() ); ++index )
    {
      const Json::Value &item = spec["items"][index];
      if ( item.isObject() && item.isMember( "id" ) && item["id"].asString() == id )
      {
        Json::Value location( Json::objectValue );
        location["collection"] = "items";
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

  // Platform 8.0 (v5): output declarations. Validation-only surface —
  // compilation never auto-exports; cartography:compose and the harness map
  // confirmation surface the declaration so the delivery contract is explicit.
  if ( spec.isMember( "output" ) )
  {
    const Json::Value &output = spec["output"];
    if ( !output.isObject() )
      problems.push_back( "output must be an object" );
    else
    {
      if ( output.isMember( "formats" ) )
      {
        if ( !output["formats"].isArray() || output["formats"].empty() )
          problems.push_back( "output.formats must be a non-empty array" );
        else
        {
          if ( static_cast<int>( output["formats"].size() ) > 8 )
            problems.push_back( "output.formats exceeds the 8 entry budget" );
          for ( const auto &format : output["formats"] )
          {
            if ( !format.isString() ||
                 ( format.asString() != "png" && format.asString() != "pdf" ) )
              problems.push_back( "output.formats entries must be \"png\" or \"pdf\"" );
          }
        }
      }
      if ( output.isMember( "dpi" ) &&
           ( !output["dpi"].isNumeric() || output["dpi"].asDouble() < 72.0 ||
             output["dpi"].asDouble() > 1200.0 ) )
        problems.push_back( "output.dpi must be a number in [72, 1200]" );
      if ( output.isMember( "dir" ) &&
           ( !output["dir"].isString() || output["dir"].asString().empty() ) )
        problems.push_back( "output.dir must be a non-empty string" );
    }
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

      // Platform 9.0: map frames may declare their CRS. Shape-checked here
      // (non-empty string); presence is a report-quality obligation enforced
      // by preflight (MAP_MISSING_CRS_NOTE accepts a declared frame CRS).
      if ( std::string( info->name ) == "map_frames" && item.isMember( "crs" ) &&
           ( !item["crs"].isString() || item["crs"].asString().empty() ) )
        problems.push_back( id + ": crs must be a non-empty string (e.g. \"EPSG:4326\")" );

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
      else if ( std::string( info->name ) == "charts" && item["chart"].isMember( "dual_axis" ) &&
                !item["chart"]["dual_axis"].isBool() )
        problems.push_back( id + ": chart.dual_axis must be a boolean" );

      // Platform 8.0: NoData legend declaration shape (label/color strings).
      if ( std::string( info->name ) == "legends" && item.isMember( "nodata" ) )
      {
        if ( !item["nodata"].isObject() )
          problems.push_back( id + ": nodata must be an object" );
        else
        {
          for ( const char *member : { "label", "color" } )
            if ( item["nodata"].isMember( member ) && !item["nodata"][member].isString() )
              problems.push_back( id + ": nodata." + member + " must be a string" );
        }
      }
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
    std::set<std::string> constraintIds;
    for ( const auto &constraint : spec["constraints"] )
    {
      // v4: duplicate declared ids would collapse per-constraint reports and
      // rank comparisons — rejected like duplicate item ids.
      if ( constraint.isObject() && constraint.isMember( "id" ) &&
           constraint["id"].isString() && !constraint["id"].asString().empty() &&
           !constraintIds.insert( constraint["id"].asString() ).second )
        problems.push_back( "duplicate constraint id '" + constraint["id"].asString() + "'" );
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
                                "avoid_overlap|fit_content|page_break|frame_style)" );
          continue;
        }
        // v4: explainable-solve surface. Closed vocabulary and bounded
        // ranges — typos and out-of-band values must fail validation, never
        // silently change solver semantics.
        if ( constraint.isMember( "hardness" ) )
        {
          if ( !constraint["hardness"].isString() ||
               !isConstraintHardness( constraint["hardness"].asString() ) )
            problems.push_back( cid + ": constraint hardness must be \"hard\" or \"soft\"" );
        }
        if ( constraint.isMember( "priority" ) )
        {
          if ( !constraint["priority"].isIntegral() ||
               constraint["priority"].asInt() < 0 || constraint["priority"].asInt() > 100 )
            problems.push_back( cid + ": constraint priority must be an integer in [0, 100]" );
        }
        if ( constraint.isMember( "weight" ) )
        {
          const bool softDeclared = constraint.isMember( "hardness" ) &&
                                    constraint["hardness"].isString() &&
                                    constraint["hardness"].asString() == "soft";
          if ( !constraint["weight"].isNumeric() || constraint["weight"].asDouble() < 0 ||
               constraint["weight"].asDouble() > 1000 )
            problems.push_back( cid + ": constraint weight must be a number in [0, 1000]" );
          else if ( !softDeclared )
            problems.push_back( cid + ": constraint weight is only meaningful on "
                                     "hardness \"soft\" constraints" );
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
            // Platform 9.0: content comes from an explicit content_mm or is
            // derived at solve time from a referenced text item
            // (text-driven sizing); exactly one of the two is required for a
            // well-formed declaration.
            const Json::Value &content = constraint.get( "content_mm", Json::Value() );
            const bool hasContent = isPositiveSizeArray( content );
            const Json::Value &textRef = constraint.get( "text_ref", Json::Value() );
            const bool hasTextRef = textRef.isString() && !textRef.asString().empty();
            if ( hasContent && hasTextRef )
              problems.push_back( cid + ": fit_content declares both content_mm and "
                                       "text_ref; content_mm wins — remove one" );
            else if ( !hasContent && !hasTextRef )
              problems.push_back( cid + ": fit_content needs content_mm [width_mm, "
                                       "height_mm] or text_ref <item id>" );
            else if ( hasTextRef && findMapSpecItem( spec, textRef.asString() ).isNull() )
              problems.push_back( cid + ": fit_content text_ref '" + textRef.asString() +
                                  "' does not resolve" );
          }
          if ( constraint.isMember( "gap_mm" ) &&
               ( !constraint["gap_mm"].isNumeric() || constraint["gap_mm"].asDouble() < 0 ) )
            problems.push_back( cid + ": gap_mm must be a non-negative number" );
          continue;
        }
        if ( kind == "page_break" )
        {
          // Platform 9.0: one declared item moves to the next declared page.
          // The target page must exist at validation time so a broken
          // document can never silently re-page content onto page 0.
          if ( !constraint.isMember( "items" ) || !constraint["items"].isArray() ||
               static_cast<int>( constraint["items"].size() ) != 1 )
          {
            problems.push_back( cid + ": page_break needs an items array with exactly 1 id" );
            continue;
          }
          const Json::Value &reference = constraint["items"][0];
          if ( reference.isString() &&
               findMapSpecItem( spec, reference.asString() ).isNull() )
            problems.push_back( cid + ": constraint item '" + reference.asString() +
                                "' does not resolve" );
          const int declaredPages =
            spec.isMember( "pages" ) && spec["pages"].isArray()
              ? static_cast<int>( spec["pages"].size() )
              : 0;
          int currentPage = -1;
          bool alreadyApplied = false;
          if ( reference.isString() )
          {
            const Json::Value location = findMapSpecItem( spec, reference.asString() );
            if ( !location.isNull() )
            {
              const Json::Value &item =
                spec[location["collection"].asString()][location["index"].asInt()];
              if ( item.isMember( "page" ) && item["page"].isIntegral() )
                currentPage = item["page"].asInt();
              else
                currentPage = 0;
              // A resolved document carries the applied-break provenance:
              // the solver owns that state, validation must not re-flag it.
              const Json::Value &stamp =
                item.get( "page_break_applied_by", Json::Value() );
              alreadyApplied = stamp.isString() && !stamp.asString().empty();
            }
          }
          if ( alreadyApplied )
            continue;
          if ( currentPage + 1 > declaredPages )
            problems.push_back( cid + ": page_break target page " +
                                std::to_string( currentPage + 1 ) +
                                " is not declared (pages[] carries " +
                                std::to_string( declaredPages ) + " entries)" );
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
        // Platform 9.0: master furniture — item ids repeated onto this page.
        if ( pageEntry.isMember( "furniture" ) )
        {
          const Json::Value &furniture = pageEntry["furniture"];
          if ( !furniture.isArray() )
            problems.push_back( "page.furniture must be an array of item ids" );
          else if ( static_cast<int>( furniture.size() ) > 32 )
            problems.push_back( "page.furniture exceeds the 32 entry budget" );
          else
          {
            std::set<std::string> seen;
            for ( const auto &reference : furniture )
            {
              if ( !reference.isString() || reference.asString().empty() )
              {
                problems.push_back( "page.furniture entries must be non-empty item id strings" );
                continue;
              }
              if ( !seen.insert( reference.asString() ).second )
                problems.push_back( "page.furniture repeats item '" + reference.asString() + "'" );
              else if ( findMapSpecItem( spec, reference.asString() ).isNull() )
                problems.push_back( "page.furniture item '" + reference.asString() +
                                    "' does not resolve" );
            }
          }
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
  if ( !spec.isObject() )
    return ledger;

  // Issue #802: conditions evaluate against the caller-supplied runtime
  // `context`; an embedded `condition_context` is a convenience default that
  // must never be *required*. Both may be present — the embedded context is
  // the base, the external context overrides key-by-key.
  const bool hasEmbedded = spec.isMember( "condition_context" ) &&
                           spec["condition_context"].isObject();
  const bool hasExternal = context.isObject();
  if ( !hasEmbedded && !hasExternal )
    return ledger;
  if ( !context.isObject() && !context.isNull() )
  {
    if ( errors )
      errors->push_back( "condition context must be an object" );
    return ledger;
  }
  Json::Value effective( Json::objectValue );
  if ( hasEmbedded )
  {
    for ( const auto &key : spec["condition_context"].getMemberNames() )
      effective[key] = spec["condition_context"][key];
  }
  if ( hasExternal )
  {
    for ( const auto &key : context.getMemberNames() )
      effective[key] = context[key];
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

  const Json::Value &ctx = effective;

  // 1. Items: visible_if prunes whole items; content_if strips content.
  std::set<std::string> removedIds;
  std::vector<std::string> targetCollections;
  for ( int c = 0; c < kCollectionCount; ++c )
    targetCollections.push_back( kCollections[c] );
  if ( spec.isMember( "items" ) && spec["items"].isArray() )
    targetCollections.push_back( "items" );

  for ( const auto &collection : targetCollections )
  {
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
        bool value = true;
        if ( mapspec::evaluateCondition( condition, ctx, &value, &evalError ) )
        {
          if ( !value )
          {
            keep = false;
            record( id, "visible_if", condition, false, std::string() );
          }
          else
          {
            record( id, "visible_if", condition, true, std::string() );
          }
        }
        else if ( !evalError.empty() )
        {
          if ( errors )
            errors->push_back( id + ": visible_if: " + evalError );
          record( id, "visible_if", condition, true, evalError );
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
        if ( mapspec::evaluateCondition( condition, ctx, &value, &evalError ) )
        {
          if ( !value )
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
          else
          {
            record( id, "content_if", condition, true, std::string() );
          }
        }
        else if ( !evalError.empty() )
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

  // 2a. Single page entry: spec["page"]["page_if"]
  if ( spec.isMember( "page" ) && spec["page"].isObject() && spec["page"].isMember( "page_if" ) &&
       spec["page"]["page_if"].isString() )
  {
    const std::string condition = spec["page"]["page_if"].asString();
    std::string evalError;
    bool value = true;
    if ( mapspec::evaluateCondition( condition, ctx, &value, &evalError ) )
    {
      if ( !value )
      {
        record( "page", "page_if", condition, false, std::string() );
        spec["page"]["visible"] = false;
      }
      else
      {
        record( "page", "page_if", condition, true, std::string() );
      }
    }
    else if ( !evalError.empty() )
    {
      if ( errors )
        errors->push_back( std::string( "page_if: " ) + evalError );
      record( "page", "page_if", condition, true, evalError );
    }
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
      if ( mapspec::evaluateCondition( condition, ctx, &value, &evalError ) )
      {
        if ( !value )
        {
          record( "page-" + std::to_string( index + 1 ), "page_if", condition, false, std::string() );
          continue;
        }
        else
        {
          record( "page-" + std::to_string( index + 1 ), "page_if", condition, true, std::string() );
        }
      }
      else if ( !evalError.empty() )
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

// src/agent/cartography/cartography_tools.cpp
#include "cartography_tools.h"

#include "../contracts/spatial_contracts.h"
#include "../mapspec/mapspec.h"
#include "../mapspec/mapspec_compiler.h"
#include "../spatial_tools/spatial_tool.h"
#include "chart_registry.h"
#include "registry.h"

#include <qgsprintlayout.h>

#include <algorithm>
#include <cmath>

namespace sicnu::agent::cartography {

using namespace sicnu::agent::contracts;
using sicnu::agent::spatial_tools::SpatialTool;
using sicnu::agent::spatial_tools::SpatialToolRegistry;
using sicnu::agent::spatial_tools::SpatialToolResult;
using sicnu::agent::spatial_tools::requireStringField;

namespace {

Json::Value rect( double x, double y, double w, double h )
{
  Json::Value r( Json::arrayValue );
  r.append( x );
  r.append( y );
  r.append( w );
  r.append( h );
  return r;
}

bool rectsIntersect( const Json::Value &a, const Json::Value &b )
{
  if ( !a.isArray() || !b.isArray() || a.size() != 4 || b.size() != 4 )
    return false;
  const double ax = a[0].asDouble();
  const double ay = a[1].asDouble();
  const double aw = a[2].asDouble();
  const double ah = a[3].asDouble();
  const double bx = b[0].asDouble();
  const double by = b[1].asDouble();
  const double bw = b[2].asDouble();
  const double bh = b[3].asDouble();
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/// First map frame id (or empty).
std::string mainMapRef( const Json::Value &spec )
{
  if ( spec.isMember( "map_frames" ) && spec["map_frames"].isArray() &&
       !spec["map_frames"].empty() && spec["map_frames"][0].isMember( "id" ) )
    return spec["map_frames"][0]["id"].asString();
  return std::string();
}

Json::Value issue( const std::string &code, const std::string &severity, const std::string &message,
                   bool repairable, const std::string &itemId, const char *action )
{
  Json::Value suggestion = action ? makeRepairSuggestion( action, Json::Value() ) : Json::Value();
  return makeIssue( code, severity, message, repairable, itemId, suggestion );
}

} // namespace

Json::Value preflightMapSpec( const Json::Value &spec, const Json::Value &compiledReport )
{
  std::vector<Json::Value> issues;

  const auto specProblems = mapspec::validateMapSpec( spec );
  if ( !specProblems.empty() )
  {
    for ( const auto &problem : specProblems )
      issues.push_back( issue( "MAPSPEC_INVALID", "error", problem, false, "", nullptr ) );
    Json::Value checksArr( Json::arrayValue );
    Json::Value issuesArr( Json::arrayValue );
    for ( const auto &i : issues )
      issuesArr.append( i );
    Json::Value body( Json::objectValue );
    body["quality_score"] = 0;
    body["passed"] = false;
    body["issues"] = issuesArr;
    body["checks"] = checksArr;
    return makeEnvelope( "map_quality_report", body );
  }

  const double pageW = spec["page"]["width_mm"].asDouble();
  const double pageH = spec["page"]["height_mm"].asDouble();
  const std::string mapRef = mainMapRef( spec );

  const auto hasNonEmpty = [ &spec ]( const char *collection ) {
    return spec.isMember( collection ) && spec[collection].isArray() && !spec[collection].empty();
  };

  // --- map frame presence & content ----------------------------------------
  if ( !hasNonEmpty( "map_frames" ) )
  {
    issues.push_back( issue( "MAP_MISSING_MAP", "error", "No map frame in the MapSpec.", false, "",
                             nullptr ) );
  }
  else
  {
    for ( const auto &frame : spec["map_frames"] )
    {
      const std::string id = frame["id"].asString();
      const bool hasLayers = frame.isMember( "layers" ) && frame["layers"].isArray() &&
                             !frame["layers"].empty();
      const bool hasExtent = frame.isMember( "extent" ) && frame["extent"].isArray() &&
                             frame["extent"].size() == 4;
      if ( !hasLayers && !hasExtent )
      {
        issues.push_back( issue(
          "MAP_EMPTY_MAP", "warning",
          "Map frame '" + id + "' has neither layers nor extent — it will render blank.", false,
          id, nullptr ) );
      }
    }
  }

  // --- cartographic furniture -----------------------------------------------
  if ( !hasNonEmpty( "titles" ) )
    issues.push_back( issue( "MAP_MISSING_TITLE", "warning", "No title item.", true, "",
                             "add_title" ) );
  if ( !hasNonEmpty( "legends" ) )
    issues.push_back( issue( "MAP_MISSING_LEGEND", "warning", "No legend item.", true, "",
                             "add_legend" ) );
  if ( !hasNonEmpty( "scale_bars" ) )
    issues.push_back( issue( "MAP_MISSING_SCALE_BAR", "warning", "No scale bar.", true, "",
                             "add_scale_bar" ) );
  if ( !hasNonEmpty( "north_arrows" ) )
    issues.push_back( issue( "MAP_MISSING_NORTH_ARROW", "warning", "No north arrow.", true, "",
                             "add_north_arrow" ) );
  if ( !hasNonEmpty( "source_notes" ) )
    issues.push_back( issue( "MAP_MISSING_SOURCE_NOTE", "warning", "No data-source note.", true,
                             "", "add_source_note" ) );

  // --- per-item geometry & style ----------------------------------------------
  Json::Value page( Json::objectValue );
  page["width_mm"] = pageW;
  page["height_mm"] = pageH;
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
  {
    const char *collection = mapspec::kCollections[c];
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    for ( const auto &item : spec[collection] )
    {
      if ( !item.isObject() || !item.isMember( "id" ) )
        continue;
      const std::string id = item["id"].asString();
      if ( item.isMember( "rect_mm" ) && item["rect_mm"].isArray() && item["rect_mm"].size() == 4 )
      {
        const Json::Value &r = item["rect_mm"];
        const double x = r[0].asDouble();
        const double y = r[1].asDouble();
        const double w = r[2].asDouble();
        const double h = r[3].asDouble();
        if ( w <= 0 || h <= 0 )
          issues.push_back( issue( "MAP_INVALID_RECT", "error",
                                   id + ": rect width/height must be positive", false, id,
                                   nullptr ) );
        else if ( x < -0.5 || y < -0.5 || x + w > pageW + 0.5 || y + h > pageH + 0.5 )
          issues.push_back( issue( "MAP_OFF_PAGE", "error",
                                   id + ": rect exceeds the page bounds", true, id,
                                   "move_in_page" ) );
      }
      if ( ( std::string( collection ) == "titles" || std::string( collection ) == "labels" ||
             std::string( collection ) == "source_notes" ) &&
           item.isMember( "font" ) && item["font"].isObject() &&
           item["font"].isMember( "size_pt" ) && item["font"]["size_pt"].isNumeric() &&
           item["font"]["size_pt"].asDouble() < 6.0 )
        issues.push_back( issue( "MAP_TINY_FONT", "warning",
                                 id + ": font below 6 pt is unreadable at export size", true, id,
                                 "bump_font" ) );
      if ( ( std::string( collection ) == "legends" || std::string( collection ) == "scale_bars" ||
             std::string( collection ) == "north_arrows" || std::string( collection ) == "charts" ) &&
           ( !item.isMember( "map_ref" ) || !item["map_ref"].isString() ) && !mapRef.empty() )
      {
        // Non-fatal: the compiler falls back to the first map. Advisory only.
      }
    }
  }

  // --- pairwise overlap between non-map items ---------------------------------
  const char *overlappable[] = { "titles", "labels", "legends", "scale_bars", "north_arrows",
                                 "source_notes", "annotations", "charts", "colorbars" };
  for ( int i = 0; i < 9; ++i )
  {
    if ( !spec.isMember( overlappable[i] ) )
      continue;
    for ( const auto &a : spec[overlappable[i]] )
    {
      if ( !a.isObject() || !a.isMember( "rect_mm" ) )
        continue;
      for ( int j = i; j < 9; ++j )
      {
        if ( !spec.isMember( overlappable[j] ) )
          continue;
        for ( const auto &b : spec[overlappable[j]] )
        {
          if ( !b.isObject() || !b.isMember( "rect_mm" ) )
            continue;
          if ( i == j && a == b )
            continue;
          if ( a["id"] == b["id"] )
            continue;
          if ( rectsIntersect( a["rect_mm"], b["rect_mm"] ) )
          {
            issues.push_back( issue(
              "MAP_OVERLAP", "warning",
              a["id"].asString() + " overlaps " + b["id"].asString(), true, a["id"].asString(),
              "reposition" ) );
          }
        }
      }
    }
  }

  // --- merge compiled-layout findings (layout:preflight report) ---------------
  if ( compiledReport.isObject() && compiledReport.isMember( "issues" ) &&
       compiledReport["issues"].isArray() )
  {
    for ( const auto &layoutIssue : compiledReport["issues"] )
    {
      if ( !layoutIssue.isObject() )
        continue;
      Json::Value merged = makeIssue( "LAYOUT_" + layoutIssue.get( "check", "unknown" ).asString(),
                                      layoutIssue.get( "severity", "warning" ).asString(),
                                      layoutIssue.get( "message", "" ).asString(),
                                      false,
                                      layoutIssue.get( "item", "" ).asString(), Json::Value() );
      issues.push_back( merged );
    }
  }

  int errorCount = 0;
  int warningCount = 0;
  int repairableCount = 0;
  for ( const auto &i : issues )
  {
    if ( i["severity"].asString() == "error" )
      ++errorCount;
    else
      ++warningCount;
    if ( i.get( "repairable", false ).asBool() )
      ++repairableCount;
  }
  const int score = std::max( 0, 100 - 20 * errorCount - 8 * warningCount );

  Json::Value body( Json::objectValue );
  body["quality_score"] = score;
  // The pass gate drives the repair loop: blocking errors OR unresolved
  // repairable findings (missing furniture, off-page items) keep a map from
  // passing; non-repairable warnings (e.g. empty map frame) are advisory.
  body["passed"] = errorCount == 0 && repairableCount == 0;
  Json::Value issuesArr( Json::arrayValue );
  for ( const auto &i : issues )
    issuesArr.append( i );
  body["issues"] = issuesArr;
  body["error_count"] = errorCount;
  body["warning_count"] = warningCount;
  return makeEnvelope( "map_quality_report", body );
}

int repairMapSpec( Json::Value &spec, const Json::Value &report )
{
  int applied = 0;
  if ( !report.isObject() || !report.isMember( "issues" ) )
    return 0;
  const double pageW = spec["page"]["width_mm"].asDouble();
  const double pageH = spec["page"]["height_mm"].asDouble();
  const std::string mapRef = mainMapRef( spec );

  for ( const auto &item : report["issues"] )
  {
    if ( !item.isObject() || !item.get( "repairable", false ).asBool() )
      continue;
    const std::string code = item.get( "code", "" ).asString();
    const Json::Value &action = item.get( "suggested_action", Json::Value() );

    if ( code == "MAP_MISSING_TITLE" && action.isMember( "action" ) &&
         action["action"].asString() == "add_title" )
    {
      Json::Value title( Json::objectValue );
      title["semantic_role"] = "title.main";
      title["text"] = "地图标题";
      title["rect_mm"] = rect( 12, 6, 200, 14 );
      title["font"] = Json::Value( Json::objectValue );
      title["font"]["size_pt"] = 18;
      mapspec::appendMapSpecItem( spec, "titles", title );
      ++applied;
    }
    else if ( code == "MAP_MISSING_LEGEND" )
    {
      Json::Value legend( Json::objectValue );
      legend["semantic_role"] = "legend.primary";
      legend["title"] = "图例";
      legend["rect_mm"] = rect( pageW - 80, 30, 66, 80 );
      if ( !mapRef.empty() )
        legend["map_ref"] = mapRef;
      mapspec::appendMapSpecItem( spec, "legends", legend );
      ++applied;
    }
    else if ( code == "MAP_MISSING_SCALE_BAR" )
    {
      Json::Value scaleBar( Json::objectValue );
      scaleBar["semantic_role"] = "scalebar.primary";
      scaleBar["style"] = "Single Box";
      scaleBar["units"] = "km";
      scaleBar["rect_mm"] = rect( 14, pageH - 20, 60, 8 );
      if ( !mapRef.empty() )
        scaleBar["map_ref"] = mapRef;
      mapspec::appendMapSpecItem( spec, "scale_bars", scaleBar );
      ++applied;
    }
    else if ( code == "MAP_MISSING_NORTH_ARROW" )
    {
      Json::Value arrow( Json::objectValue );
      arrow["semantic_role"] = "north_arrow.primary";
      arrow["rect_mm"] = rect( pageW - 16, 6, 12, 12 );
      if ( !mapRef.empty() )
        arrow["map_ref"] = mapRef;
      mapspec::appendMapSpecItem( spec, "north_arrows", arrow );
      ++applied;
    }
    else if ( code == "MAP_MISSING_SOURCE_NOTE" )
    {
      Json::Value note( Json::objectValue );
      note["semantic_role"] = "source.primary";
      note["text"] = "数据来源: SICNU GEO RS / exp-rs";
      note["rect_mm"] = rect( pageW - 130, pageH - 16, 116, 8 );
      note["font"] = Json::Value( Json::objectValue );
      note["font"]["size_pt"] = 7;
      mapspec::appendMapSpecItem( spec, "source_notes", note );
      ++applied;
    }
    else if ( code == "MAP_OFF_PAGE" || code == "MAP_INVALID_RECT" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      const Json::Value location = mapspec::findMapSpecItem( spec, id );
      if ( location.isNull() )
        continue;
      Json::Value &found = spec[location["collection"].asString()][location["index"].asInt()];
      if ( !found.isMember( "rect_mm" ) || found["rect_mm"].size() != 4 )
        continue;
      double x = found["rect_mm"][0].asDouble();
      double y = found["rect_mm"][1].asDouble();
      double w = found["rect_mm"][2].asDouble();
      double h = found["rect_mm"][3].asDouble();
      w = std::clamp( w, 1.0, pageW );
      h = std::clamp( h, 1.0, pageH );
      x = std::clamp( x, 0.0, std::max( 0.0, pageW - w ) );
      y = std::clamp( y, 0.0, std::max( 0.0, pageH - h ) );
      found["rect_mm"] = rect( x, y, w, h );
      ++applied;
    }
    else if ( code == "MAP_TINY_FONT" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      const Json::Value location = mapspec::findMapSpecItem( spec, id );
      if ( location.isNull() )
        continue;
      Json::Value &found = spec[location["collection"].asString()][location["index"].asInt()];
      if ( !found.isMember( "font" ) || !found["font"].isObject() )
        found["font"] = Json::Value( Json::objectValue );
      found["font"]["size_pt"] = 8;
      ++applied;
    }
    else if ( code == "MAP_OVERLAP" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      const Json::Value location = mapspec::findMapSpecItem( spec, id );
      if ( location.isNull() )
        continue;
      Json::Value &found = spec[location["collection"].asString()][location["index"].asInt()];
      if ( !found.isMember( "rect_mm" ) || found["rect_mm"].size() != 4 )
        continue;
      // Deterministic relocation: try the classic anchor slots in a fixed
      // order and take the first that neither leaves the page nor collides
      // with any other item. Convergence > cleverness for agent repair.
      const double w = found["rect_mm"][2].asDouble();
      const double h = found["rect_mm"][3].asDouble();
      const double m = 6.0;
      struct Slot { double x; double y; };
      const Slot candidates[] = {
        { pageW - m - w, m },   { m, m },               { m, pageH - m - h },
        { pageW - m - w, pageH - m - h }, { m, ( pageH - h ) / 2.0 },
        { pageW - m - w, ( pageH - h ) / 2.0 }, { ( pageW - w ) / 2.0, pageH - m - h },
      };
      // Collect every other furniture rect — the same collections the
      // overlap detector scans, so a relocated item never re-triggers
      // MAP_OVERLAP. Map frames are overlays, not obstacles.
      const char *overlappable[] = { "titles",   "labels",    "legends",
                                     "scale_bars", "north_arrows", "source_notes",
                                     "annotations", "charts",  "colorbars" };
      std::vector<Json::Value> others;
      for ( const char *collection : overlappable )
      {
        if ( !spec.isMember( collection ) || !spec[collection].isArray() )
          continue;
        for ( const auto &other : spec[collection] )
        {
          if ( !other.isObject() || !other.isMember( "id" ) || other["id"].asString() == id )
            continue;
          if ( other.isMember( "rect_mm" ) && other["rect_mm"].isArray() &&
               other["rect_mm"].size() == 4 )
            others.push_back( other["rect_mm"] );
        }
      }
      for ( const auto &candidate : candidates )
      {
        if ( candidate.x < 0 || candidate.y < 0 || candidate.x + w > pageW ||
             candidate.y + h > pageH )
          continue;
        bool free = true;
        for ( const auto &other : others )
          free = free && !rectsIntersect( rect( candidate.x, candidate.y, w, h ), other );
        if ( free )
        {
          found["rect_mm"] = rect( candidate.x, candidate.y, w, h );
          ++applied;
          break;
        }
      }
    }
  }
  return applied;
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

namespace {

class ListComponentsTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:list_components"; }
    std::string displayName() const override { return "List cartographic components"; }
    std::string description() const override
    {
      return "Compact, paged catalog of cartographic components (north-arrow, scale-bar, "
             "legend, color-bar, title, grid, annotation, source-note, frame …) with variants "
             "and layout constraints. Input: {category?, limit?, offset?}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "components", "catalog" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value category( Json::objectValue );
      category["type"] = "string";
      props["category"] = category;
      Json::Value limit( Json::objectValue );
      limit["type"] = "integer";
      props["limit"] = limit;
      Json::Value offset( Json::objectValue );
      offset["type"] = "integer";
      props["offset"] = offset;
      schema["properties"] = props;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["items"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const QString category = input.isMember( "category" ) && input["category"].isString()
                                 ? QString::fromStdString( input["category"].asString() )
                                 : QString();
      const int limit = input.isMember( "limit" ) && input["limit"].isInt() ? input["limit"].asInt() : 20;
      const int offset = input.isMember( "offset" ) && input["offset"].isInt() ? input["offset"].asInt() : 0;
      Json::Value page = paginate( ComponentRegistry::instance().byCategory( category ),
                                   offset, std::clamp( limit, 1, 50 ) );
      Json::Value out( Json::objectValue );
      out["items"] = page["items"];
      out["total"] = page["total"];
      out["next_offset"] = page["next_offset"];
      return SpatialToolResult::ok( out );
    }
};

class GetComponentTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:get_component"; }
    std::string displayName() const override { return "Get cartographic component"; }
    std::string description() const override
    {
      return "Full descriptor of one component (parameters, constraints, compatibility).";
    }
    std::vector<std::string> tags() const override { return { "cartography", "components" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value id( Json::objectValue );
      id["type"] = "string";
      props["id"] = id;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "id" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString id = requireStringField( input, "id", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const Json::Value component = ComponentRegistry::instance().find( id );
      if ( component.isNull() )
        return SpatialToolResult::failure( "Unknown component: " + id.toStdString(), "NOT_FOUND",
                                           "validation", false );
      return SpatialToolResult::ok( component );
    }
};

class ListTemplatesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:list_templates"; }
    std::string displayName() const override { return "List cartographic templates"; }
    std::string description() const override
    {
      return "Paged catalog of map templates (classification, change-detection, heatmap, "
             "choropleth, time-series, scientific-publication …). Input: {task?, limit?, offset?} "
             "with task filtering by suitable_tasks.";
    }
    std::vector<std::string> tags() const override { return { "cartography", "templates" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value task( Json::objectValue );
      task["type"] = "string";
      task["description"] = "Filter by suitable task (substring)";
      props["task"] = task;
      schema["properties"] = props;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["items"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string task = input.isMember( "task" ) && input["task"].isString()
                                 ? input["task"].asString()
                                 : "";
      Json::Value all( Json::arrayValue );
      Json::Value templates = TemplateRegistry::instance().templates();
      for ( const auto &tmpl : templates )
      {
        if ( task.empty() )
        {
          all.append( tmpl );
          continue;
        }
        bool matches = false;
        if ( tmpl.isMember( "suitable_tasks" ) && tmpl["suitable_tasks"].isArray() )
        {
          for ( const auto &candidate : tmpl["suitable_tasks"] )
            matches = matches || candidate.asString().find( task ) != std::string::npos;
        }
        if ( matches )
          all.append( tmpl );
      }
      const int limit = input.isMember( "limit" ) && input["limit"].isInt() ? input["limit"].asInt() : 20;
      const int offset = input.isMember( "offset" ) && input["offset"].isInt() ? input["offset"].asInt() : 0;
      Json::Value page = paginate( all, offset, std::clamp( limit, 1, 50 ) );
      Json::Value out( Json::objectValue );
      out["items"] = page["items"];
      out["total"] = page["total"];
      out["next_offset"] = page["next_offset"];
      return SpatialToolResult::ok( out );
    }
};

class InstantiateTemplateTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:instantiate_template"; }
    std::string displayName() const override { return "Instantiate template"; }
    std::string description() const override
    {
      return "Creates a MapSpec draft from a template: slots become concrete items with "
             "semantic roles and rects; recommended components are appended. Input: {template, "
             "params?: {layout_name?, title?, source_note?, layers?, extent?}}. Patch the draft "
             "with cartography:compose / MapSpec patch ops afterwards.";
    }
    std::vector<std::string> tags() const override { return { "cartography", "templates", "compose" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value tmpl( Json::objectValue );
      tmpl["type"] = "string";
      tmpl["description"] = "Template id (see cartography:list_templates)";
      props["template"] = tmpl;
      Json::Value params( Json::objectValue );
      params["type"] = "object";
      props["params"] = params;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "template" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["kind"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString tmpl = requireStringField( input, "template", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      QString error;
      Json::Value spec = TemplateRegistry::instance().instantiateTemplate(
        tmpl, input.get( "params", Json::Value( Json::objectValue ) ), &error );
      if ( spec.isNull() )
        return SpatialToolResult::failure( error.toStdString(), "NOT_FOUND", "validation", false );
      return SpatialToolResult::ok( spec );
    }
};

class ComposeTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:compose"; }
    std::string displayName() const override { return "Compose MapSpec layout"; }
    std::string description() const override
    {
      return "Compiles a MapSpec document into a QGIS print layout (created/replaced under "
             "spec.layout_name) and returns the spec-level quality report. Follow with "
             "cartography:repair until quality passes, then layout:export.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "compose", "layout", "mapspec" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value mapspecProp( Json::objectValue );
      mapspecProp["type"] = "object";
      mapspecProp["description"] = "MapSpec document (kind: map_spec)";
      props["mapspec"] = mapspecProp;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "mapspec" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["compiled"] = Json::Value( Json::objectValue );
      schema["properties"]["quality"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "mapspec" ) || !input["mapspec"].isObject() )
        return SpatialToolResult::failure( "Missing required parameter: mapspec (object)",
                                           "INVALID_PARAMETER", "validation" );
      QString error;
      QgsPrintLayout *layout = mapspec::MapSpecCompiler::compile( input["mapspec"], &error );
      Json::Value report = preflightMapSpec( input["mapspec"] );
      if ( !layout )
      {
        Json::Value out( Json::objectValue );
        out["compiled"] = false;
        out["error"] = error.toStdString();
        out["quality"] = report;
        return SpatialToolResult::failure( error.toStdString(), "COMPILE_FAILED", "validation" );
      }
      Json::Value out( Json::objectValue );
      out["compiled"] = true;
      out["layout_name"] = input["mapspec"]["layout_name"].asString();
      out["quality"] = report;
      return SpatialToolResult::ok( out );
    }
};

class PreflightTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:preflight"; }
    std::string displayName() const override { return "Preflight MapSpec"; }
    std::string description() const override
    {
      return "Cartographic quality gate for a MapSpec (runs inside cartography:compose too): "
             "missing title/legend/scale bar/north arrow/source note, empty map frames, "
             "off-page items, item overlaps, tiny fonts. Issues carry code/severity/item_id/"
             "repairable/suggested_action; a 0-100 quality_score summarizes. Input: {mapspec}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "preflight", "quality", "mapspec" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value mapspecProp( Json::objectValue );
      mapspecProp["type"] = "object";
      props["mapspec"] = mapspecProp;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "mapspec" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["quality_score"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "mapspec" ) || !input["mapspec"].isObject() )
        return SpatialToolResult::failure( "Missing required parameter: mapspec (object)",
                                           "INVALID_PARAMETER", "validation" );
      return SpatialToolResult::ok( preflightMapSpec( input["mapspec"] ) );
    }
};

class RepairTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:repair"; }
    std::string displayName() const override { return "Repair MapSpec"; }
    std::string description() const override
    {
      return "Automatically applies repairable suggestions from the preflight report (adds "
             "missing title/legend/scale bar/north arrow/source note, moves off-page items, "
             "bumps tiny fonts, separates overlaps) and re-preflights, up to max_iterations "
             "(default 3). Input: {mapspec, max_iterations?} → {mapspec, repairs_applied, "
             "iterations, quality}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "repair", "preflight", "mapspec" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value mapspecProp( Json::objectValue );
      mapspecProp["type"] = "object";
      props["mapspec"] = mapspecProp;
      Json::Value iterations( Json::objectValue );
      iterations["type"] = "integer";
      props["max_iterations"] = iterations;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "mapspec" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["quality"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "mapspec" ) || !input["mapspec"].isObject() )
        return SpatialToolResult::failure( "Missing required parameter: mapspec (object)",
                                           "INVALID_PARAMETER", "validation" );
      int maxIterations = input.isMember( "max_iterations" ) && input["max_iterations"].isInt()
                            ? std::clamp( input["max_iterations"].asInt(), 1, 10 )
                            : 3;

      Json::Value spec = input["mapspec"];
      int totalRepairs = 0;
      int iterations = 0;
      Json::Value quality = preflightMapSpec( spec );
      while ( iterations < maxIterations && !quality["passed"].asBool() )
      {
        const int repairs = repairMapSpec( spec, quality );
        if ( repairs == 0 )
          break; // nothing repairable remains — Pi decides how to proceed
        totalRepairs += repairs;
        ++iterations;
        quality = preflightMapSpec( spec );
      }

      Json::Value out( Json::objectValue );
      out["mapspec"] = spec;
      out["repairs_applied"] = totalRepairs;
      out["iterations"] = iterations;
      out["quality"] = quality;
      return SpatialToolResult::ok( out );
    }
};

// --- chart tools -------------------------------------------------------------

class ChartCreateTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:chart_create"; }
    std::string displayName() const override { return "Create chart"; }
    std::string description() const override
    {
      return "Registers a workspace chart entity (bar|line|pie|histogram|area|scatter). "
             "Binding: inline {data: [{label, value}]} or vector_expression {layer, "
             "x_expression, y_expression, filter?}. The chart id is a stable referent for "
             "MapSpec chart components (charts[].chart = this spec).";
    }
    std::vector<std::string> tags() const override { return { "cartography", "chart", "workspace" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value chart( Json::objectValue );
      chart["type"] = "object";
      chart["description"] = "Chart spec: {kind, title?, binding, style?, width_px?, height_px?}";
      props["chart"] = chart;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "chart" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["id"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "chart" ) || !input["chart"].isObject() )
        return SpatialToolResult::failure( "Missing required parameter: chart (object)",
                                           "INVALID_PARAMETER", "validation" );
      QString error;
      const QString id = ChartRegistry::instance().createChart( input["chart"], &error );
      if ( id.isEmpty() )
        return SpatialToolResult::failure( error.toStdString(), "INVALID_PARAMETER", "validation" );
      Json::Value out( Json::objectValue );
      out["id"] = id.toStdString();
      out["chart"] = ChartRegistry::instance().find( id );
      return SpatialToolResult::ok( out );
    }
};

class ChartGetTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:chart_get"; }
    std::string displayName() const override { return "Get chart"; }
    std::string description() const override { return "Returns one chart spec by id."; }
    std::vector<std::string> tags() const override { return { "cartography", "chart" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value id( Json::objectValue );
      id["type"] = "string";
      props["id"] = id;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "id" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString id = requireStringField( input, "id", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const Json::Value chart = ChartRegistry::instance().find( id );
      if ( chart.isNull() )
        return SpatialToolResult::failure( "Unknown chart: " + id.toStdString(), "NOT_FOUND",
                                           "validation", false );
      return SpatialToolResult::ok( chart );
    }
};

class ChartListTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:chart_list"; }
    std::string displayName() const override { return "List charts"; }
    std::string description() const override { return "All workspace chart entities (compact)."; }
    std::vector<std::string> tags() const override { return { "cartography", "chart" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["items"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value & ) override
    {
      Json::Value charts = ChartRegistry::instance().listCharts();
      Json::Value out( Json::objectValue );
      out["items"] = charts;
      out["total"] = static_cast<Json::Int>( charts.size() );
      return SpatialToolResult::ok( out );
    }
};

class ChartDeleteTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:chart_delete"; }
    std::string displayName() const override { return "Delete chart"; }
    std::string description() const override { return "Removes a workspace chart entity."; }
    std::vector<std::string> tags() const override { return { "cartography", "chart" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value id( Json::objectValue );
      id["type"] = "string";
      props["id"] = id;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "id" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["removed"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString id = requireStringField( input, "id", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const bool removed = ChartRegistry::instance().removeChart( id );
      if ( !removed )
        return SpatialToolResult::failure( "Unknown chart: " + id.toStdString(), "NOT_FOUND",
                                           "validation", false );
      Json::Value out( Json::objectValue );
      out["removed"] = true;
      out["id"] = id.toStdString();
      return SpatialToolResult::ok( out );
    }
};

} // namespace

void registerCartographyTools()
{
  static const bool registered = [] {
    auto &registry = SpatialToolRegistry::instance();
    registry.registerTool( std::make_shared<ListComponentsTool>() );
    registry.registerTool( std::make_shared<GetComponentTool>() );
    registry.registerTool( std::make_shared<ListTemplatesTool>() );
    registry.registerTool( std::make_shared<InstantiateTemplateTool>() );
    registry.registerTool( std::make_shared<ComposeTool>() );
    registry.registerTool( std::make_shared<PreflightTool>() );
    registry.registerTool( std::make_shared<RepairTool>() );
    registry.registerTool( std::make_shared<ChartCreateTool>() );
    registry.registerTool( std::make_shared<ChartGetTool>() );
    registry.registerTool( std::make_shared<ChartListTool>() );
    registry.registerTool( std::make_shared<ChartDeleteTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

} // namespace sicnu::agent::cartography

// src/agent/cartography/cartography_tools.cpp
#include "cartography_tools.h"

#include "../contracts/spatial_contracts.h"
#include "../mapspec/mapspec.h"
#include "../mapspec/mapspec_compiler.h"
#include "../spatial_tools/spatial_tool.h"
#include "chart_registry.h"
#include "composition.h"
#include "design_tokens.h"
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

using sicnu::agent::cartography::preflightMapSpec;
using sicnu::agent::cartography::repairMapSpec;

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
             "spec.layout_name), resolves anchors/constraints, and returns the quality report. "
             "On success follow with cartography:repair until quality passes, then layout:export; "
             "on COMPILE_FAILED call cartography:preflight for the structural report.";
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
      // Pre-compile composition pass: anchors, size bounds, and constraints
      // resolve into concrete rects; the resolved document is echoed back so
      // agents see the geometry they got.
      Json::Value spec = input["mapspec"];
      const double marginDefault = tokenNumber( resolveTokenSet( spec ), "spacing.margin_mm", 12.0 );
      const Json::Value composition = resolveComposition( spec, marginDefault ).toJson();

      QString error;
      QgsPrintLayout *layout = mapspec::MapSpecCompiler::compile( spec, &error );
      Json::Value report = preflightMapSpec( spec );
      Json::Value out( Json::objectValue );
      out["mapspec"] = spec;
      out["composition"] = composition;
      if ( !layout )
      {
        out["compiled"] = false;
        out["error"] = error.toStdString();
        out["quality"] = report;
        return SpatialToolResult::failure( error.toStdString(), "COMPILE_FAILED", "validation" );
      }
      out["compiled"] = true;
      out["layout_name"] = spec["layout_name"].asString();
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
             "missing furniture, empty map frames, off-page/margin violations, tiny fonts, "
             "title/note text overflow, legend density, duplicate furniture, invalid chart "
             "bindings, unbalanced multi-map frames, inset placement, unresolvable component "
             "references, unsatisfiable constraints, overlaps. Issues carry code/severity/"
             "item_id/repairable/suggested_action; a 0-100 quality_score summarizes. Evaluates "
             "the resolved composition and echoes it as the report's `mapspec` member (issue "
             "lists cap at 500 entries). Input: {mapspec}.";
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
      // Preflight evaluates the *resolved* composition (anchors, size
      // bounds, constraints), echoing the resolved document back.
      Json::Value spec = input["mapspec"];
      resolveComposition( spec, tokenNumber( resolveTokenSet( spec ), "spacing.margin_mm", 12.0 ) );
      Json::Value out = preflightMapSpec( spec );
      out["mapspec"] = spec;
      return SpatialToolResult::ok( out );
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
      // Solve once before the repair loop (see repairMapSpec contract).
      resolveComposition( spec, tokenNumber( resolveTokenSet( spec ), "spacing.margin_mm", 12.0 ) );
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

// --- design token tools --------------------------------------------------------

class ListTokenSetsTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:list_token_sets"; }
    std::string displayName() const override { return "List design token sets"; }
    std::string description() const override
    {
      return "Catalog of design token sets (typography, spacing, colors, palettes, chart "
             "defaults) with the resolved medium variants. Default: scientific-light. "
             "Input: {} (compact list).";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "tokens", "style" };
    }
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
      Json::Value items( Json::arrayValue );
      for ( const auto &set : TokenSetRegistry::instance().tokenSets() )
      {
        Json::Value compact( Json::objectValue );
        compact["id"] = set["id"];
        compact["version"] = set["version"];
        compact["description"] = set["description"];
        if ( set.isMember( "variants" ) && set["variants"].isObject() )
        {
          Json::Value mediums( Json::arrayValue );
          mediums.append( "print" ); // print always resolvable (base document)
          for ( const auto &medium : set["variants"].getMemberNames() )
            if ( medium != "print" )
              mediums.append( medium );
          compact["mediums"] = mediums;
        }
        items.append( compact );
      }
      Json::Value out( Json::objectValue );
      out["items"] = items;
      out["total"] = static_cast<Json::Int>( items.size() );
      out["default"] = kDefaultTokenSetId;
      return SpatialToolResult::ok( out );
    }
};

class GetTokenSetTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:get_token_set"; }
    std::string displayName() const override { return "Get resolved design tokens"; }
    std::string description() const override
    {
      return "Effective (resolved) token document for a token set + medium + overrides: "
             "typography hierarchy, spacing, line weights, colors, palettes, furniture "
             "metrics, chart defaults. Input: {token_set?, medium?, overrides?}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "tokens", "style" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value tokenSet( Json::objectValue );
      tokenSet["type"] = "string";
      props["token_set"] = tokenSet;
      Json::Value medium( Json::objectValue );
      medium["type"] = "string";
      medium["description"] = "print (default) or screen";
      props["medium"] = medium;
      Json::Value overrides( Json::objectValue );
      overrides["type"] = "object";
      props["overrides"] = overrides;
      schema["properties"] = props;
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
      Json::Value style( Json::objectValue );
      if ( input.isMember( "token_set" ) && input["token_set"].isString() )
        style["token_set"] = input["token_set"];
      if ( input.isMember( "medium" ) && input["medium"].isString() )
        style["medium"] = input["medium"];
      if ( input.isMember( "overrides" ) && input["overrides"].isObject() )
        style["overrides"] = input["overrides"];
      return SpatialToolResult::ok( resolveTokenSet( style ) );
    }
};

class ValidateTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:validate"; }
    std::string displayName() const override { return "Validate MapSpec"; }
    std::string description() const override
    {
      return "Structural MapSpec validation without compiling: envelope, ids, geometry, "
             "references, collection rules. Returns one human-readable problem per entry; "
             "empty list means valid. Input: {mapspec}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "validate", "mapspec" };
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
      schema["properties"]["problems"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "mapspec" ) || !input["mapspec"].isObject() )
        return SpatialToolResult::failure( "Missing required parameter: mapspec (object)",
                                           "INVALID_PARAMETER", "validation" );
      Json::Value out( Json::objectValue );
      Json::Value problems( Json::arrayValue );
      for ( const auto &problem : mapspec::validateMapSpec( input["mapspec"] ) )
        problems.append( problem );
      out["problems"] = problems;
      out["valid"] = problems.empty();
      return SpatialToolResult::ok( out );
    }
};

class RuleCatalogTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:list_rules"; }
    std::string displayName() const override { return "List preflight rules"; }
    std::string description() const override
    {
      return "Catalog of cartography preflight rule codes with severity, repairability and "
             "repair behavior. Input: {}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "preflight", "quality", "catalog" };
    }
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
      schema["properties"]["rules"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value & ) override
    {
      Json::Value out( Json::objectValue );
      out["rules"] = preflightRuleCatalog();
      out["total"] = out["rules"].size();
      return SpatialToolResult::ok( out );
    }
};

class CatalogIndexTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:catalog_index"; }
    std::string displayName() const override { return "Cartography catalog index"; }
    std::string description() const override
    {
      return "Generated machine index of the shipped design system: token sets, components "
             "(with variants), and templates (with slot roles, page family, product type). "
             "Use for gallery/documentation sync and offline discovery. Input: {}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "catalog", "index" };
    }
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
      return schema;
    }
    SpatialToolResult execute( const Json::Value & ) override
    {
      return SpatialToolResult::ok( buildCatalogIndex() );
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
      return "Registers a workspace chart entity (bar|line|pie|histogram|area|scatter|"
             "stacked_bar|matrix|metric). "
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
    registry.registerTool( std::make_shared<ListTokenSetsTool>() );
    registry.registerTool( std::make_shared<GetTokenSetTool>() );
    registry.registerTool( std::make_shared<ValidateTool>() );
    registry.registerTool( std::make_shared<ComposeTool>() );
    registry.registerTool( std::make_shared<PreflightTool>() );
    registry.registerTool( std::make_shared<RuleCatalogTool>() );
    registry.registerTool( std::make_shared<CatalogIndexTool>() );
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

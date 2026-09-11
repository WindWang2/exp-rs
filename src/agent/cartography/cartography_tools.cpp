// src/agent/cartography/cartography_tools.cpp
#include "cartography_tools.h"

#include "../contracts/spatial_contracts.h"
#include "../mapspec/mapspec.h"
#include "../mapspec/mapspec_compiler.h"
#include "../layout_tools/layout_service.h"
#include "../spatial_tools/spatial_tool.h"
#include "../workspace_state.h"
#include "chart_registry.h"
#include "composition.h"
#include "design_tokens.h"
#include "export.h"
#include "quality.h"
#include "registry.h"
#include "solution_registry.h"
#include "style_compiler.h"
#include "style_spec.h"

#include <qgsmaplayer.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

#include <algorithm>
#include <map>
#include <cmath>

namespace sicnu::agent::cartography {

using namespace sicnu::agent::contracts;
using sicnu::agent::layout_tools::LayoutService;
using sicnu::agent::spatial_tools::SpatialTool;
using sicnu::agent::spatial_tools::SpatialToolRegistry;
using sicnu::agent::spatial_tools::SpatialToolResult;
using sicnu::agent::spatial_tools::requireStringField;

using sicnu::agent::cartography::preflightMapSpec;
using sicnu::agent::cartography::repairMapSpec;
using sicnu::agent::cartography::repairMapSpecWithLedger;
using sicnu::agent::cartography::diffTemplates;
using sicnu::agent::cartography::exportMapLayout;
using sicnu::agent::cartography::mapExportResultToJson;
using sicnu::agent::cartography::MapExportRequest;
using sicnu::agent::cartography::MapExportResult;

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
             "choropleth, time-series, scientific-publication …). Input: {task?, medium?, "
             "purpose?, keyword?, limit?, offset?}. task/medium/purpose are exact facet "
             "matches (Platform 6.0 taxonomy); keyword is a substring over id/description. "
             "Matches carry a compact summary plus match.reasons; results are deterministic.";
    }
    std::vector<std::string> tags() const override { return { "cartography", "templates" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value task( Json::objectValue );
      task["type"] = "string";
      task["description"] =
        "Task facet (classification|change|sar|vegetation|agriculture|water|disaster|terrain|"
        "time-series|accuracy|publication) or a suitable_tasks substring";
      props["task"] = task;
      Json::Value medium( Json::objectValue );
      medium["type"] = "string";
      medium["description"] = "Medium facet: screen|a4|a3|a0|report|atlas";
      props["medium"] = medium;
      Json::Value purpose( Json::objectValue );
      purpose["type"] = "string";
      purpose["description"] = "Purpose facet: exploration|analysis|operational|scientific|presentation";
      props["purpose"] = purpose;
      Json::Value keyword( Json::objectValue );
      keyword["type"] = "string";
      keyword["description"] = "Substring over id and description";
      props["keyword"] = keyword;
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
      const auto stringParam = [ &input ]( const char *name ) {
        return input.isMember( name ) && input[name].isString() ? input[name].asString() : "";
      };
      const std::string task = stringParam( "task" );
      const std::string medium = stringParam( "medium" );
      const std::string purpose = stringParam( "purpose" );
      const std::string keyword = stringParam( "keyword" );
      const int limit = input.isMember( "limit" ) && input["limit"].isInt() ? input["limit"].asInt() : 20;
      const int offset = input.isMember( "offset" ) && input["offset"].isInt() ? input["offset"].asInt() : 0;

      // Platform 6.0: any facet/criterion beyond the legacy task substring
      // routes through the explainable faceted search (compact summaries +
      // match reasons keep responses inside the token budget).
      if ( !medium.empty() || !purpose.empty() || !keyword.empty() )
      {
        TemplateQuery query;
        query.task = task;
        query.medium = medium;
        query.purpose = purpose;
        query.keyword = keyword;
        const int pageSize = std::clamp( limit, 1, 50 );
        query.page = offset / pageSize;
        query.pageSize = pageSize;
        Json::Value out = TemplateRegistry::instance().search( query );
        // Keep the legacy paginate() sentinel (-1) so clients switching
        // between the task-only and faceted paths see one terminator, and
        // keep offsets page-aligned (offset is the absolute item cursor).
        const int begin = query.page * pageSize;
        const int total = out["total"].asInt();
        out["next_offset"] = ( begin + pageSize < total ) ? Json::Value( begin + pageSize )
                                                          : Json::Value( -1 );
        return SpatialToolResult::ok( out );
      }

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
        // Facets count as suitable tasks too (substring, legacy-compatible).
        if ( !matches && tmpl.isMember( "facets" ) && tmpl["facets"].isObject() &&
             tmpl["facets"].isMember( "tasks" ) && tmpl["facets"]["tasks"].isArray() )
        {
          for ( const auto &candidate : tmpl["facets"]["tasks"] )
            matches = matches || candidate.asString().find( task ) != std::string::npos;
        }
        if ( matches )
          all.append( tmpl );
      }
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
      // Mirror the compiler: v3 conditions resolve BEFORE composition and
      // preflight so hidden items cannot produce false positives.
      if ( spec.isObject() && spec.isMember( "condition_context" ) )
      {
        std::vector<std::string> conditionErrors;
        mapspec::resolveMapSpecConditions( spec, spec["condition_context"], &conditionErrors );
      }
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
      // Platform 8.0: compose identity. The structural digest pins WHAT was
      // composed (rendering-free SHA-256 over the resolved geometry) and the
      // provenance block names the declared template/components so Harness
      // confirmation and workbench listings can identify the composition
      // without a new metadata vocabulary.
      out["structural_digest"] = structuralDigest( spec );
      out["provenance"] = [ &spec ] {
        Json::Value provenance( Json::objectValue );
        if ( spec.isMember( "template" ) && spec["template"].isString() )
          provenance["template"] = spec["template"];
        // Platform 9.0: structured template lineage (id + version + parents)
        // stamped by instantiateTemplate rides along when present.
        if ( spec.isMember( "template_provenance" ) &&
             spec["template_provenance"].isObject() )
          provenance["template_provenance"] = spec["template_provenance"];
        Json::Value components( Json::arrayValue );
        for ( int c = 0; c < mapspec::kCollectionCount; ++c )
        {
          const char *collection = mapspec::kCollections[c];
          if ( !spec.isMember( collection ) || !spec[collection].isArray() )
            continue;
          for ( const auto &item : spec[collection] )
          {
            if ( !item.isObject() || !item.isMember( "source_component" ) )
              continue;
            const Json::Value &ref = item["source_component"];
            const std::string componentId =
              ref.isString() ? ref.asString()
                             : ref.isObject() && ref.isMember( "id" ) && ref["id"].isString()
                                   ? ref["id"].asString()
                                   : std::string();
            if ( componentId.empty() )
              continue;
            Json::Value entry( Json::objectValue );
            entry["id"] = componentId;
            if ( ref.isObject() && ref.isMember( "variant" ) && ref["variant"].isString() )
              entry["variant"] = ref["variant"];
            components.append( entry );
          }
        }
        provenance["components"] = components;
        return provenance;
      }();
      if ( spec.isMember( "output" ) && spec["output"].isObject() )
        out["declared_output"] = spec["output"];
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
      Json::Value repairLedger( Json::arrayValue );
      while ( iterations < maxIterations && !quality["passed"].asBool() )
      {
        // Platform 9.0: every pass records what each repairable finding
        // became (applied / still_reported) — the agent sees WHY the
        // residual findings survived instead of only a repair count.
        Json::Value passLedger;
        const int repairs = repairMapSpecWithLedger( spec, quality, &passLedger );
        if ( repairs == 0 )
          break; // nothing repairable remains — Pi decides how to proceed
        totalRepairs += repairs;
        for ( const auto &entry : passLedger )
        {
          Json::Value record = entry;
          record["pass"] = iterations + 1;
          repairLedger.append( record );
        }
        ++iterations;
        quality = preflightMapSpec( spec );
      }

      Json::Value out( Json::objectValue );
      out["mapspec"] = spec;
      out["repairs_applied"] = totalRepairs;
      out["iterations"] = iterations;
      out["repair_ledger"] = repairLedger;
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

// ---------------------------------------------------------------------------
// Platform 5.0: template discovery, style surfaces, catalog lint
// ---------------------------------------------------------------------------

namespace {

Json::Value objectOutputSchema( const char *key, const char *type = "object" )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value prop( Json::objectValue );
  prop["type"] = type;
  schema["properties"][key] = prop;
  return schema;
}

Json::Value compactTemplateSummary( const Json::Value &tmpl )
{
  Json::Value out( Json::objectValue );
  out["id"] = tmpl.get( "id", "" );
  out["description"] = tmpl.get( "description", "" ).asString().substr( 0, 160 );
  if ( tmpl.isMember( "product_type" ) )
    out["product_type"] = tmpl["product_type"];
  if ( tmpl.isMember( "suitable_tasks" ) )
    out["suitable_tasks"] = tmpl["suitable_tasks"];
  if ( tmpl.isMember( "page" ) )
    out["page"] = tmpl["page"];
  if ( tmpl.isMember( "style" ) && tmpl["style"].isObject() )
  {
    out["token_set"] = tmpl["style"].get( "token_set", "" );
    out["medium"] = tmpl["style"].get( "medium", "" );
  }
  return out;
}

bool templateMatches( const Json::Value &tmpl, const Json::Value &query )
{
  const std::string task = query.get( "task", "" ).asString();
  if ( !task.empty() )
  {
    bool match = false;
    if ( tmpl.isMember( "suitable_tasks" ) && tmpl["suitable_tasks"].isArray() )
      for ( const auto &candidate : tmpl["suitable_tasks"] )
        match = match || candidate.asString().find( task ) != std::string::npos;
    if ( !match )
      return false;
  }
  const std::string product = query.get( "product_type", "" ).asString();
  if ( !product.empty() && tmpl.get( "product_type", "" ).asString() != product )
    return false;
  const std::string tokenSet = query.get( "token_set", "" ).asString();
  if ( !tokenSet.empty() && tmpl.get( "style", Json::Value() ).get( "token_set", "" ).asString() != tokenSet )
    return false;
  const std::string medium = query.get( "medium", "" ).asString();
  if ( !medium.empty() && tmpl.get( "style", Json::Value() ).get( "medium", "" ).asString() != medium )
    return false;
  const std::string keyword = query.get( "keyword", "" ).asString();
  if ( !keyword.empty() )
  {
    std::string haystack = tmpl.get( "id", "" ).asString() + " " +
                           tmpl.get( "description", "" ).asString() + " " +
                           tmpl.get( "product_type", "" ).asString();
    if ( tmpl.isMember( "suitable_tasks" ) && tmpl["suitable_tasks"].isArray() )
      for ( const auto &candidate : tmpl["suitable_tasks"] )
        haystack += " " + candidate.asString();
    if ( haystack.find( keyword ) == std::string::npos )
      return false;
  }
  return true;
}

class SearchTemplatesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "template:search"; }
    std::string displayName() const override { return "Search map templates"; }
    std::string description() const override
    {
      return "Faceted, bounded search over the map template catalog (task, "
             "product_type, token_set, medium, keyword). Returns compact "
             "summaries ordered by id; page with {offset, limit}. Prefer this "
             "over cartography:list_templates when looking for a template by "
             "task semantics.";
    }
    std::vector<std::string> tags() const override { return { "cartography", "templates", "search" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      for ( const char *facet : { "task", "product_type", "token_set", "medium", "keyword" } )
      {
        Json::Value prop( Json::objectValue );
        prop["type"] = "string";
        props[facet] = prop;
      }
      Json::Value offset( Json::objectValue );
      offset["type"] = "integer";
      props["offset"] = offset;
      Json::Value limit( Json::objectValue );
      limit["type"] = "integer";
      props["limit"] = limit;
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"] = props;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      return objectOutputSchema( "items", "array" );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      Json::Value hits( Json::arrayValue );
      for ( const auto &tmpl : TemplateRegistry::instance().templates() )
        if ( templateMatches( tmpl, input ) )
          hits.append( compactTemplateSummary( tmpl ) );
      const int limit = input.isMember( "limit" ) && input["limit"].isInt() ? input["limit"].asInt() : 20;
      const int offset = input.isMember( "offset" ) && input["offset"].isInt() ? input["offset"].asInt() : 0;
      Json::Value page = paginate( hits, offset, std::clamp( limit, 1, 50 ) );
      Json::Value out( Json::objectValue );
      out["items"] = page["items"];
      out["total"] = page["total"];
      out["next_offset"] = page["next_offset"];
      return SpatialToolResult::ok( out );
    }
};

class DescribeTemplateTool final : public SpatialTool
{
  public:
    std::string name() const override { return "template:describe"; }
    std::string displayName() const override { return "Describe map template"; }
    std::string description() const override
    {
      return "Full template descriptor: slots (roles, accepted collections, "
             "rects/content), style policy, page geometry and recommended "
             "components.";
    }
    std::vector<std::string> tags() const override { return { "cartography", "templates" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value id( Json::objectValue );
      id["type"] = "string";
      props["id"] = id;
      Json::Value required( Json::arrayValue );
      required.append( "id" );
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"] = props;
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      return objectOutputSchema( "template" );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString id = requireStringField( input, "id", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const Json::Value tmpl = TemplateRegistry::instance().find( id );
      if ( tmpl.isNull() )
        return SpatialToolResult::failure( "Unknown template: " + id.toStdString(), "NOT_FOUND",
                                           "validation" );
      return SpatialToolResult::ok( tmpl );
    }
};

/// Structural template validation used by template:validate and lint_catalog.
std::vector<std::string> validateTemplateDescriptor( const Json::Value &tmpl )
{
  std::vector<std::string> problems;
  if ( !tmpl.isObject() )
    return { "template must be an object" };
  const std::string id = tmpl.isMember( "id" ) && tmpl["id"].isString() ? tmpl["id"].asString() : "";
  if ( id.empty() )
    problems.push_back( "template needs a string id" );
  if ( !tmpl.isMember( "version" ) || !tmpl["version"].isIntegral() )
    problems.push_back( id + ": needs an integer version" );
  const Json::Value &page = tmpl.get( "page", Json::Value() );
  if ( !page.isObject() || !page.isMember( "width_mm" ) || !page.isMember( "height_mm" ) ||
       !page["width_mm"].isNumeric() || !page["height_mm"].isNumeric() || page["width_mm"].asDouble() <= 0 ||
       page["height_mm"].asDouble() <= 0 )
    problems.push_back( id + ": page needs positive width_mm/height_mm" );
  if ( tmpl.isMember( "extends" ) && !tmpl["extends"].isString() )
    problems.push_back( id + ": extends must be a parent template id string" );
  const Json::Value &slotList = tmpl.isMember( "slots") && tmpl["slots"].isArray()
                                  ? tmpl["slots"]
                                  : tmpl.get( "required_slots", Json::Value() );
  if ( slotList.isArray() )
  {
    int roleCount = 0;
    for ( const auto &slot : slotList )
    {
      if ( !slot.isObject() || !slot.isMember( "role" ) || slot["role"].asString().empty() )
      {
        problems.push_back( id + ": every slot needs a non-empty role" );
        continue;
      }
      ++roleCount;
      if ( slot.isMember( "accepts" ) && !slot["accepts"].isString() )
        problems.push_back( id + ": slot '" + slot["role"].asString() + "' accepts must be a collection" );
      else if ( slot.isMember( "accepts" ) && !mapspec::isCollection( slot["accepts"].asString() ) )
        problems.push_back( id + ": slot '" + slot["role"].asString() + "' references unknown collection '" +
                            slot["accepts"].asString() + "'" );
      if ( slot.isMember( "rect_mm" ) &&
           ( !slot["rect_mm"].isArray() || slot["rect_mm"].size() != 4 ) )
        problems.push_back( id + ": slot '" + slot["role"].asString() + "' rect_mm must be [x, y, w, h]" );
      if ( slot.isMember( "content" ) && !slot["content"].isObject() )
        problems.push_back( id + ": slot '" + slot["role"].asString() + "' content must be an object" );
    }
    if ( roleCount == 0 )
      problems.push_back( id + ": no usable slots declared" );
  }
  if ( tmpl.isMember( "recommended_components" ) )
  {
    for ( const auto &reference : tmpl["recommended_components"] )
    {
      if ( !reference.isString() )
        continue;
      if ( ComponentRegistry::instance().find( QString::fromStdString( reference.asString() ) ).isNull() )
        problems.push_back( id + ": recommended component '" + reference.asString() +
                            "' does not resolve" );
    }
  }
  return problems;
}

class ValidateTemplateTool final : public SpatialTool
{
  public:
    std::string name() const override { return "template:validate"; }
    std::string displayName() const override { return "Validate map template"; }
    std::string description() const override
    {
      return "Structural validation of a template descriptor (slots, page, "
             "extends, recommended component references) against the live "
             "component catalog.";
    }
    std::vector<std::string> tags() const override { return { "cartography", "templates", "validate" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value tmpl( Json::objectValue );
      tmpl["type"] = "object";
      props["template"] = tmpl;
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "template" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      return objectOutputSchema( "problems", "array" );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "template" ) || !input["template"].isObject() )
        return SpatialToolResult::failure( "missing object parameter 'template'", "INVALID_PARAMETER",
                                           "validation" );
      Json::Value out( Json::objectValue );
      Json::Value problems( Json::arrayValue );
      for ( const auto &problem : validateTemplateDescriptor( input["template"] ) )
        problems.append( problem );
      out["problems"] = problems;
      out["valid"] = problems.empty();
      return SpatialToolResult::ok( out );
    }
};

class PreviewTemplateTool final : public SpatialTool
{
  public:
    std::string name() const override { return "template:preview"; }
    std::string displayName() const override { return "Preview template instantiation"; }
    std::string description() const override
    {
      return "Instantiates a template into a MapSpec draft and reports what "
             "composition + preflight would say — without registering a "
             "layout. Use to check slot content/params before committing.";
    }
    std::vector<std::string> tags() const override { return { "cartography", "templates", "preview" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value id( Json::objectValue );
      id["type"] = "string";
      props["template"] = id;
      Json::Value params( Json::objectValue );
      params["type"] = "object";
      props["params"] = params;
      Json::Value required( Json::arrayValue );
      required.append( "template" );
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"] = props;
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      return objectOutputSchema( "mapspec" );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString id = requireStringField( input, "template", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      QString error;
      Json::Value draft = TemplateRegistry::instance().instantiateTemplate(
        id, input.get( "params", Json::Value() ), &error );
      if ( draft.isNull() )
        return SpatialToolResult::failure( error.toStdString(), "NOT_FOUND", "validation" );
      const double margin = tokenNumber( resolveTokenSet( draft ), "spacing.margin_mm", 12.0 );
      const Json::Value composition = resolveComposition( draft, margin ).toJson();
      Json::Value out( Json::objectValue );
      out["mapspec"] = draft;
      out["composition"] = composition;
      out["quality"] = preflightMapSpec( draft );
      return SpatialToolResult::ok( out );
    }
};

class ListStylesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "style:list"; }
    std::string displayName() const override { return "List style specs"; }
    std::string description() const override
    {
      return "Compact catalog of declarative style specs (semantic symbology "
             "knowledge: water, flood, SAR, burn severity, uncertainty …) "
             "that compile onto QGIS renderers.";
    }
    std::vector<std::string> tags() const override { return { "cartography", "style", "catalog" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value semantics( Json::objectValue );
      semantics["type"] = "string";
      semantics["description"] = "Filter by semantic tag (substring).";
      props["semantics"] = semantics;
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"] = props;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      return objectOutputSchema( "items", "array" );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string semantics = input.get( "semantics", "" ).asString();
      Json::Value items( Json::arrayValue );
      for ( const auto &style : StyleRegistry::instance().styles() )
      {
        if ( !semantics.empty() )
        {
          bool match = false;
          if ( style.isMember( "semantics" ) && style["semantics"].isArray() )
            for ( const auto &tag : style["semantics"] )
              match = match || tag.asString().find( semantics ) != std::string::npos;
          if ( !match )
            continue;
        }
        items.append( compactStyleSummary( style ) );
      }
      Json::Value out( Json::objectValue );
      out["items"] = items;
      out["total"] = static_cast<Json::ArrayIndex>( items.size() );
      return SpatialToolResult::ok( out );
    }
};

class DescribeStyleTool final : public SpatialTool
{
  public:
    std::string name() const override { return "style:describe"; }
    std::string displayName() const override { return "Describe style spec"; }
    std::string description() const override
    {
      return "Full style spec document plus a token-resolution preview "
             "(all token: references resolved against the referenced token "
             "set, unresolved ones reported).";
    }
    std::vector<std::string> tags() const override { return { "cartography", "style" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value id( Json::objectValue );
      id["type"] = "string";
      props["id"] = id;
      Json::Value required( Json::arrayValue );
      required.append( "id" );
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"] = props;
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      return objectOutputSchema( "style" );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString id = requireStringField( input, "id", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const Json::Value style = StyleRegistry::instance().find( id );
      if ( style.isNull() )
        return SpatialToolResult::failure( "Unknown style: " + id.toStdString(), "NOT_FOUND",
                                           "validation" );
      Json::Value out( Json::objectValue );
      out["style"] = style;
      std::vector<std::string> tokenProblems;
      out["resolved"] = resolveStyleTokens( style, resolveTokenSet( style ), &tokenProblems );
      Json::Value problems( Json::arrayValue );
      for ( const auto &problem : tokenProblems )
        problems.append( problem );
      out["token_problems"] = problems;
      return SpatialToolResult::ok( out );
    }
};

/// Layer resolution for style:apply — entity ids, uuids or names.
QgsMapLayer *resolveProjectLayer( const std::string &ref )
{
  const QString naturalKey =
    WorkspaceEntityRegistry::instance().naturalKeyFor( QString::fromStdString( ref ) );
  const QString key = naturalKey.isEmpty() ? QString::fromStdString( ref ) : naturalKey;
  QgsProject *project = QgsProject::instance();
  if ( !project )
    return nullptr;
  const QList<QgsMapLayer *> byName = project->mapLayersByName( key );
  if ( !byName.isEmpty() )
    return byName.first();
  return project->mapLayer( key );
}

class ApplyStyleTool final : public SpatialTool
{
  public:
    std::string name() const override { return "style:apply"; }
    std::string displayName() const override { return "Apply style spec to layer"; }
    std::string description() const override
    {
      return "Applies a style spec to a workspace layer by compiling it onto "
             "QGIS renderer primitives (raster pseudocolor/paletted/gray/"
             "multiband, vector categorized/graduated/rule-based/simple, "
             "labels, scale visibility, opacity). Input: {style, layer}.";
    }
    std::vector<std::string> tags() const override { return { "cartography", "style", "apply" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value style( Json::objectValue );
      style["type"] = "string";
      style["description"] = "Style spec id (see style:list).";
      props["style"] = style;
      Json::Value layer( Json::objectValue );
      layer["type"] = "string";
      layer["description"] = "Workspace layer id, uuid or layer name.";
      props["layer"] = layer;
      Json::Value required( Json::arrayValue );
      required.append( "style" );
      required.append( "layer" );
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"] = props;
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      return objectOutputSchema( "report" );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString styleId = requireStringField( input, "style", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const QString layerRef = requireStringField( input, "layer", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const Json::Value style = StyleRegistry::instance().find( styleId );
      if ( style.isNull() )
        return SpatialToolResult::failure( "Unknown style: " + styleId.toStdString(), "NOT_FOUND",
                                           "validation" );
      QgsMapLayer *layer = resolveProjectLayer( layerRef.toStdString() );
      if ( !layer )
        return SpatialToolResult::failure( "Layer not found: " + layerRef.toStdString(), "NOT_FOUND",
                                           "validation" );
      QString error;
      QStringList problems;
      const bool ok = applyStyleSpecToLayer( layer, style, &error, &problems );
      if ( !ok )
        return SpatialToolResult::failure( error.toStdString(), "INVALID_PARAMETER", "validation" );
      return SpatialToolResult::ok(
        styleApplicationReport( layer->id(), QStringList() << styleId, problems ) );
    }
};

class LintCatalogTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:lint_catalog"; }
    std::string displayName() const override { return "Lint knowledge catalog"; }
    std::string description() const override
    {
      return "Whole-catalog authoring check: registry load problems, template "
             "structure + extends, solution references (recipes, templates, "
             "styles), style token resolution, component token references and "
             "mapspec collection names. Returns one check block per domain; "
             "empty problem lists mean clean.";
    }
    std::vector<std::string> tags() const override { return { "cartography", "lint", "catalog" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"] = Json::Value( Json::objectValue );
      return schema;
    }
    Json::Value outputSchema() const override
    {
      return objectOutputSchema( "checks", "array" );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      Q_UNUSED( input );
      Json::Value checks( Json::arrayValue );
      auto addCheck = [ &checks ]( const char *name, const QStringList &problems ) {
        Json::Value check( Json::objectValue );
        check["check"] = name;
        Json::Value list( Json::arrayValue );
        for ( const QString &problem : problems )
          list.append( problem.toStdString() );
        check["problems"] = list;
        checks.append( check );
      };
      auto addProblems = [ &checks ]( const char *name, const std::vector<std::string> &problems ) {
        Json::Value check( Json::objectValue );
        check["check"] = name;
        Json::Value list( Json::arrayValue );
        for ( const std::string &problem : problems )
          list.append( problem );
        check["problems"] = list;
        checks.append( check );
      };

      addCheck( "component_registry", ComponentRegistry::instance().loadProblems().isEmpty()
                                         ? QStringList()
                                         : ComponentRegistry::instance().loadProblems() );
      addCheck( "template_registry", TemplateRegistry::instance().loadProblems() );
      addCheck( "token_registry", QStringList() );
      addCheck( "style_registry", StyleRegistry::instance().loadProblems() );
      addCheck( "solution_registry", SolutionRegistry::instance().loadProblems() );

      // Template structure against the live catalogs.
      std::vector<std::string> templateProblems;
      for ( const auto &tmpl : TemplateRegistry::instance().templates() )
      {
        auto problems = validateTemplateDescriptor( tmpl );
        templateProblems.insert( templateProblems.end(), problems.begin(), problems.end() );
      }
      addProblems( "template_structure", templateProblems );

      // Solution reference resolution.
      std::vector<std::string> solutionProblems;
      const auto recipes = [&] {
        // RecipeCatalog lives in the harness layer; reference checking is
        // wired by harness tools. Here we only check cartography-side refs.
        return true;
      }();
      Q_UNUSED( recipes );
      for ( const auto &solution : SolutionRegistry::instance().solutions() )
      {
        for ( const char *ref : { "map_template" } )
        {
          const std::string id = solution.get( ref, "" ).asString();
          if ( !id.empty() &&
               TemplateRegistry::instance().find( QString::fromStdString( id ) ).isNull() )
            solutionProblems.push_back( solution.get( "id", "" ).asString() + ": unknown " + ref +
                                        " '" + id + "'" );
        }
        if ( solution.isMember( "style_spec" ) && solution["style_spec"].isObject() )
        {
          for ( const auto &role : solution["style_spec"].getMemberNames() )
          {
            const std::string styleId = solution["style_spec"][role].asString();
            if ( !styleId.empty() &&
                 StyleRegistry::instance().find( QString::fromStdString( styleId ) ).isNull() )
              solutionProblems.push_back( solution.get( "id", "" ).asString() + ": unknown style '" +
                                          styleId + "' (role '" + role + "')" );
          }
        }
      }
      addProblems( "solution_references", solutionProblems );

      // Style token resolution.
      std::vector<std::string> tokenProblems;
      for ( const auto &style : StyleRegistry::instance().styles() )
      {
        std::vector<std::string> styleTokenProblems;
        resolveStyleTokens( style, resolveTokenSet( style ), &styleTokenProblems );
        for ( const auto &problem : styleTokenProblems )
          tokenProblems.push_back( style.get( "id", "" ).asString() + ": " + problem );
      }
      addProblems( "style_tokens", tokenProblems );

      Json::Value out( Json::objectValue );
      out["checks"] = checks;
      int total = 0;
      bool clean = true;
      for ( const auto &check : checks )
      {
        total += static_cast<int>( check["problems"].size() );
        clean = clean && check["problems"].empty();
      }
      out["problem_count"] = total;
      out["clean"] = clean;
      return SpatialToolResult::ok( out );
    }
};

// --- Platform 9.0: template semantic diff ---------------------------------------

class DiffTemplatesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:diff_templates"; }
    std::string displayName() const override { return "Diff two templates"; }
    std::string description() const override
    {
      return "Bounded semantic diff of two resolved templates: slots by role "
             "(added/removed/changed), top-level members (added/removed/changed), with "
             "truncation flagged. Read-only evidence for template reasoning. Input: "
             "{before, after} (template ids) → {added_slots, removed_slots, changed_slots, "
             "added_keys, removed_keys, changed_keys, truncated}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "templates", "diff", "catalog" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      Json::Value props( Json::objectValue );
      props["before"]["type"] = "string";
      props["after"]["type"] = "string";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "before" );
      required.append( "after" );
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
      const QString beforeId = QString::fromStdString( input.get( "before", "" ).asString() );
      const QString afterId = QString::fromStdString( input.get( "after", "" ).asString() );
      if ( beforeId.isEmpty() || afterId.isEmpty() )
        return SpatialToolResult::failure( "before and after must be non-empty template ids",
                                           "INVALID_PARAMETER", "validation" );
      const Json::Value before = TemplateRegistry::instance().find( beforeId );
      if ( before.isNull() )
        return SpatialToolResult::failure( "template '" + beforeId.toStdString() + "' not found",
                                           "NOT_FOUND", "validation" );
      const Json::Value after = TemplateRegistry::instance().find( afterId );
      if ( after.isNull() )
        return SpatialToolResult::failure( "template '" + afterId.toStdString() + "' not found",
                                           "NOT_FOUND", "validation" );
      Json::Value out = diffTemplates( before, after );
      out["before"] = beforeId.toStdString();
      out["after"] = afterId.toStdString();
      return SpatialToolResult::ok( out );
    }
};

// --- Platform 9.0: governed export + bounded explain ---------------------------

class ExportTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:export"; }
    std::string displayName() const override { return "Export composed map"; }
    std::string description() const override
    {
      return "Governed atomic export of a composed layout (png|pdf|svg, dpi 72..1200). png "
             "supports page selection on this QGIS build; pdf/svg always export all pages and "
             "REFUSE a declared page selection. The file is written to a temp file, verified, "
             "hashed (sha256) and renamed — a failed export leaves no partial file. Font "
             "substitutions are reported as diagnostics. Input: {layout, format, directory, "
             "file_name?, dpi?, pages?} → {ok, path, sha256, bytes, diagnostics}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "export", "reproducibility", "mapspec" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      Json::Value props( Json::objectValue );
      props["layout"]["type"] = "string";
      props["format"]["type"] = "string";
      Json::Value formats( Json::arrayValue );
      formats.append( "png" );
      formats.append( "pdf" );
      formats.append( "svg" );
      props["format"]["enum"] = formats;
      props["directory"]["type"] = "string";
      props["file_name"]["type"] = "string";
      props["dpi"]["type"] = "number";
      Json::Value pages( Json::objectValue );
      pages["type"] = "array";
      Json::Value pageIndex( Json::objectValue );
      pageIndex["type"] = "integer";
      pages["items"] = pageIndex;
      props["pages"] = pages;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "layout" );
      required.append( "format" );
      required.append( "directory" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["path"] = Json::Value( Json::objectValue );
      schema["properties"]["sha256"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const auto requireString = [ & ]( const char *key, std::string *err ) -> std::string {
        if ( !input.isMember( key ) || !input[key].isString() || input[key].asString().empty() )
        {
          if ( err )
            *err = std::string( "Missing required parameter: " ) + key + " (non-empty string)";
          return std::string();
        }
        return input[key].asString();
      };
      std::string err;
      const std::string layoutName = requireString( "layout", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const std::string format = requireString( "format", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const std::string directory = requireString( "directory", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QgsPrintLayout *layout =
        LayoutService::instance().findLayout( QString::fromStdString( layoutName ) );
      if ( !layout )
        return SpatialToolResult::failure( "No layout named '" + layoutName + "'",
                                           "NOT_FOUND", "validation" );

      MapExportRequest request;
      request.format = format;
      request.directory = directory;
      if ( input.isMember( "file_name" ) && input["file_name"].isString() )
        request.file_name = input["file_name"].asString();
      if ( input.isMember( "dpi" ) && input["dpi"].isNumeric() )
        request.dpi = input["dpi"].asDouble();
      if ( input.isMember( "pages" ) && input["pages"].isArray() )
        for ( const auto &page : input["pages"] )
          if ( page.isIntegral() )
            request.pages.push_back( page.asInt() );

      const MapExportResult result = exportMapLayout( layout, request );
      if ( !result.ok )
        return SpatialToolResult::failure( result.error.toStdString(), "EXPORT_FAILED",
                                           "runtime" );
      Json::Value out = mapExportResultToJson( result );
      out["format"] = format;
      out["dpi"] = request.dpi;
      if ( !request.pages.empty() )
      {
        Json::Value pagesJson( Json::arrayValue );
        for ( const int page : request.pages )
          pagesJson.append( page );
        out["pages"] = pagesJson;
      }
      return SpatialToolResult::ok( out );
    }
};

class ExplainTool final : public SpatialTool
{
  public:
    std::string name() const override { return "cartography:explain"; }
    std::string displayName() const override { return "Explain map geometry/quality"; }
    std::string description() const override
    {
      return "Bounded explanation of WHY a document composes the way it does: for one item_id "
             "(or one page), the solver decisions and violations that touch it, its unsat "
             "cores, and its preflight findings. Pure evidence report — nothing is modified. "
             "Input: {mapspec, item_id?} → {item_id, solver: {decisions, violated, "
             "unsat_cores}, quality_issues}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "cartography", "explain", "diagnostics", "mapspec" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      Json::Value props( Json::objectValue );
      props["mapspec"]["type"] = "object";
      props["item_id"]["type"] = "string";
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
      schema["properties"]["item_id"] = Json::Value( Json::objectValue );
      schema["properties"]["solver"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "mapspec" ) || !input["mapspec"].isObject() )
        return SpatialToolResult::failure( "Missing required parameter: mapspec (object)",
                                           "INVALID_PARAMETER", "validation" );
      const std::string itemId =
        input.isMember( "item_id" ) && input["item_id"].isString() ? input["item_id"].asString()
                                                                   : std::string();
      if ( itemId.empty() )
        return SpatialToolResult::failure( "item_id is required (explain is item-scoped)",
                                           "INVALID_PARAMETER", "validation" );
      // Locate the item first: explaining a ghost is a typed miss, not an
      // empty report.
      const Json::Value location = mapspec::findMapSpecItem( input["mapspec"], itemId );
      if ( location.isNull() )
        return SpatialToolResult::failure( "item '" + itemId + "' does not exist in the document",
                                           "NOT_FOUND", "validation" );

      Json::Value spec = input["mapspec"];
      if ( spec.isObject() && spec.isMember( "condition_context" ) )
        mapspec::resolveMapSpecConditions( spec, spec["condition_context"], nullptr );
      const double marginDefault =
        tokenNumber( resolveTokenSet( spec ), "spacing.margin_mm", 12.0 );
      const CompositionResult solved = resolveComposition( spec, marginDefault );
      const Json::Value quality = preflightMapSpec( spec );

      // The solve ran on a local copy: re-derive the reports that mention
      // the item from the SAME solve so decisions/violated stay consistent.
      // Constraint identity → declared item ids (the solver synthesizes
      // `kind#index` for id-less constraints; mirror that here so decision
      // filtering covers both).
      std::map<std::string, std::vector<std::string>> constraintItems;
      if ( spec.isMember( "constraints" ) && spec["constraints"].isArray() )
      {
        // Mirror buildRuntimes EXACTLY: only solver kinds consume a
        // declaration index (legacy/free-form items are skipped there too),
        // otherwise synthesized kind#index identities diverge and explain
        // would name the wrong constraint.
        int declaredIndex = 0;
        for ( const auto &constraint : spec["constraints"] )
        {
          if ( !constraint.isObject() || !constraint.isMember( "kind" ) ||
               !constraint["kind"].isString() || !constraint.isMember( "items" ) ||
               !constraint["items"].isArray() )
            continue;
          if ( !mapspec::isConstraintKind( constraint["kind"].asString() ) )
            continue;
          std::string cid =
            constraint.isMember( "id" ) && constraint["id"].isString() &&
                !constraint["id"].asString().empty()
              ? constraint["id"].asString()
              : constraint["kind"].asString() + "#" + std::to_string( declaredIndex );
          ++declaredIndex;
          for ( const auto &reference : constraint["items"] )
            if ( reference.isString() )
              constraintItems[cid].push_back( reference.asString() );
        }
      }
      const auto touchesItem = [ & ]( const std::string &cid ) {
        if ( cid == itemId )
          return true;
        const auto it = constraintItems.find( cid );
        if ( it == constraintItems.end() )
          return false;
        for ( const auto &candidate : it->second )
          if ( candidate == itemId )
            return true;
        return false;
      };

      Json::Value decisions( Json::arrayValue );
      for ( const auto &decision : solved.decisions )
      {
        if ( !touchesItem( decision.cid ) )
          continue;
        Json::Value entry( Json::objectValue );
        entry["cid"] = decision.cid;
        entry["kind"] = decision.kind;
        entry["outcome"] = decision.outcome;
        entry["reason"] = decision.reason;
        decisions.append( entry );
      }
      Json::Value violated( Json::arrayValue );
      for ( const auto &violation : solved.violated )
      {
        if ( !touchesItem( violation.cid ) &&
             violation.reason.find( itemId ) == std::string::npos )
          continue;
        Json::Value entry( Json::objectValue );
        entry["cid"] = violation.cid;
        entry["kind"] = violation.kind;
        entry["reason"] = violation.reason;
        violated.append( entry );
      }
      Json::Value cores( Json::arrayValue );
      for ( const auto &core : solved.unsatCores )
      {
        if ( !core.isObject() || !core.isMember( "constraint" ) )
          continue;
        if ( core["constraint"].asString() == itemId )
          cores.append( core );
      }

      Json::Value qualityIssues( Json::arrayValue );
      if ( quality.isObject() && quality.isMember( "issues" ) && quality["issues"].isArray() )
        for ( const auto &issueEntry : quality["issues"] )
          if ( issueEntry.isObject() && issueEntry.isMember( "item_id" ) &&
               issueEntry["item_id"].isString() && issueEntry["item_id"].asString() == itemId )
            qualityIssues.append( issueEntry );

      Json::Value out( Json::objectValue );
      out["item_id"] = itemId;
      Json::Value solver( Json::objectValue );
      solver["converged"] = solved.converged;
      solver["passes"] = solved.passes;
      solver["decisions"] = decisions;
      solver["violated"] = violated;
      solver["unsat_cores"] = cores;
      out["solver"] = solver;
      out["quality_issues"] = qualityIssues;
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
    // Platform 5.0: discovery + style + lint surfaces.
    registry.registerTool( std::make_shared<SearchTemplatesTool>() );
    registry.registerTool( std::make_shared<DescribeTemplateTool>() );
    registry.registerTool( std::make_shared<ValidateTemplateTool>() );
    registry.registerTool( std::make_shared<PreviewTemplateTool>() );
    registry.registerTool( std::make_shared<ListStylesTool>() );
    registry.registerTool( std::make_shared<DescribeStyleTool>() );
    registry.registerTool( std::make_shared<ApplyStyleTool>() );
    registry.registerTool( std::make_shared<LintCatalogTool>() );
    registry.registerTool( std::make_shared<DiffTemplatesTool>() );
    // Platform 9.0: governed export + bounded explain.
    registry.registerTool( std::make_shared<ExportTool>() );
    registry.registerTool( std::make_shared<ExplainTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

} // namespace sicnu::agent::cartography

// src/agent/harness/harness_tools.cpp
#include "harness_tools.h"

#include "agent/tool_catalog/agent_tool_catalog.h"
#include "contracts/spatial_contracts.h"
#include "harness_error.h"
#include "tool_manifest.h"
#include "tool_taxonomy.h"

#include <algorithm>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

Json::Value objectSchema( Json::Value properties, Json::Value required )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = std::move( properties );
  if ( required.isArray() && !required.empty() )
    schema["required"] = std::move( required );
  return schema;
}

/// manifestForToolId enriched with the catalog entry's AgentMetadata when the
/// id belongs to a Processing tool — operators declare their resources in
/// code, and this is where those declarations reach the agent surface.
ToolManifest manifestForCatalogTool( const std::string &toolId )
{
  ToolManifest manifest = manifestForToolId( toolId );
  const auto tool = sicnu::agent::tool_catalog::AgentToolCatalog::instance().findTool( toolId );
  if ( !tool )
    return manifest;

  const auto &meta = tool->agentMetadata;
  if ( !meta.memoryPolicy.empty() )
    manifest.memoryPolicy = meta.memoryPolicy;
  if ( !meta.determinismGrade.empty() )
    manifest.determinismGrade = meta.determinismGrade;
  if ( !meta.costClass.empty() )
    manifest.costClass = meta.costClass;
  manifest.largeRasterSafe = meta.largeRasterSafe;
  manifest.gpuAccelerated = meta.gpuAccelerated;
  manifest.producesProvenance = manifest.producesProvenance || meta.producesProvenance;
  manifest.cancellable = manifest.cancellable || meta.supportsCancellation;
  manifest.sideEffects = manifest.sideEffects || meta.sideEffects;
  manifest.idempotent = !manifest.sideEffects && meta.idempotent;
  for ( const std::string &prerequisite : meta.prerequisites )
    manifest.preconditions.push_back( { "declared", prerequisite } );
  return manifest;
}

class ToolManifestTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:tool_manifest"; }
    std::string displayName() const override { return "Harness Tool Manifest"; }
    std::string description() const override
    {
      return "Returns the canonical harness manifest for catalog tools: taxonomy "
             "classification (domain.action), risk class, side effects, resource "
             "hints, cancellability, preconditions, and expected artifacts. One "
             "page per call (default 50, max 100); pass tool_id for a single tool. "
             "Staged discovery contract: search_tools -> get_tool_schema -> "
             "harness:tool_manifest -> invoke.";
    }
    std::vector<std::string> tags() const override { return { "harness", "catalog", "risk", "taxonomy" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value toolId( Json::objectValue );
      toolId["type"] = "string";
      toolId["description"] = "Exact tool id (e.g. rs:ndvi, spatial:raster_inspect).";
      props["tool_id"] = toolId;
      Json::Value limit( Json::objectValue );
      limit["type"] = "integer";
      limit["description"] = "Page size for catalog-wide listing (default 50, max 100).";
      props["limit"] = limit;
      Json::Value offset( Json::objectValue );
      offset["type"] = "integer";
      offset["description"] = "Pagination offset.";
      props["offset"] = offset;
      Json::Value required( Json::arrayValue );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["tools"] = Json::Value( Json::arrayValue );
      props["total"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const auto &catalog = sicnu::agent::tool_catalog::AgentToolCatalog::instance();
      if ( input.isMember( "tool_id" ) )
      {
        if ( !input["tool_id"].isString() )
          return SpatialToolResult::failure( "tool_id must be a string",
                                             error_codes::kInvalidParameter, "validation" );
        const std::string toolId = input["tool_id"].asString();
        if ( !catalog.findTool( toolId ) )
        {
          return SpatialToolResult::failure( "Unknown tool id: " + toolId,
                                             error_codes::kToolNotFound, "validation" );
        }
        Json::Value out( Json::objectValue );
        out["tool"] = manifestForCatalogTool( toolId ).toJson();
        return SpatialToolResult::ok( std::move( out ) );
      }

      auto tools = catalog.listTools();
      std::sort( tools.begin(), tools.end(),
                 []( const auto &a, const auto &b ) { return a.name < b.name; } );
      Json::Value items( Json::arrayValue );
      for ( const auto &tool : tools )
      {
        Json::Value entry( Json::objectValue );
        entry["tool_id"] = tool.name;
        items.append( std::move( entry ) );
      }
      int limit = 50;
      if ( input.isMember( "limit" ) && input["limit"].isNumeric() )
        limit = std::clamp( input["limit"].asInt(), 1, 100 );
      int offset = 0;
      if ( input.isMember( "offset" ) && input["offset"].isNumeric() )
        offset = std::max( 0, input["offset"].asInt() );

      Json::Value manifests( Json::arrayValue );
      const int total = static_cast<int>( tools.size() );
      for ( int i = offset; i < total && static_cast<int>( manifests.size() ) < limit; ++i )
      {
        Json::Value entry( Json::objectValue );
        entry["tool_id"] = tools[static_cast<size_t>( i )].name;
        entry["manifest"] = manifestForCatalogTool( tools[static_cast<size_t>( i )].name ).toJson();
        manifests.append( std::move( entry ) );
      }
      Json::Value out = sicnu::agent::contracts::paginate( manifests, offset, limit );
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class ErrorCodesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:error_codes"; }
    std::string displayName() const override { return "Harness Error Taxonomy"; }
    std::string description() const override
    {
      return "Lists the stable harness error taxonomy: every code with its "
             "category, retry class, recoverability, and the closed risk-class "
             "vocabulary. Read this once to interpret any harness failure "
             "without parsing log text.";
    }
    std::vector<std::string> tags() const override { return { "harness", "errors", "contract" }; }

    Json::Value inputSchema() const override { return objectSchema( Json::Value( Json::objectValue ), Json::Value() ); }
    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["codes"] = Json::Value( Json::arrayValue );
      props["risk_classes"] = Json::Value( Json::arrayValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value & ) override
    {
      static const char *kCodes[] = {
        "DATASET_NOT_FOUND", "BAND_ROLE_UNRESOLVED", "CRS_MISMATCH", "GRID_MISMATCH",
        "INVALID_RADIOMETRY", "INSUFFICIENT_MEMORY", "MODEL_INCOMPATIBLE", "MODEL_NOT_READY",
        "EXECUTION_FAILED", "CANCELLED", "OUTPUT_INVALID", "MAP_PREFLIGHT_FAILED",
        "PREFLIGHT_BLOCKED", "ENTITY_AMBIGUOUS", "INVALID_PLAN", "INVALID_PARAMETER",
        "TRANSIENT_FAILURE", "IO_ERROR", "PATH_OUTSIDE_WORKSPACE", "WORKFLOW_NOT_FOUND",
        "TOOL_NOT_FOUND", "NOT_SUPPORTED",
      };
      Json::Value codes( Json::arrayValue );
      for ( const char *code : kCodes )
      {
        Json::Value entry( Json::objectValue );
        entry["code"] = code;
        entry["category"] = errorCategoryForCode( code );
        entry["retry_class"] = retryClassToString( retryClassForCode( code ) );
        codes.append( entry );
      }
      Json::Value risks( Json::arrayValue );
      for ( const char *risk : { risk_classes::kReadOnly, risk_classes::kModifiesDisplay,
                                 risk_classes::kCreatesArtifact, risk_classes::kModifiesProject,
                                 risk_classes::kDestructive, risk_classes::kExternalProcess,
                                 risk_classes::kNetwork } )
      {
        risks.append( risk );
      }
      Json::Value out( Json::objectValue );
      out["codes"] = std::move( codes );
      out["risk_classes"] = std::move( risks );
      return SpatialToolResult::ok( std::move( out ) );
    }
};

} // namespace

void registerHarnessTools()
{
  static bool registered = false;
  if ( registered )
    return;
  registered = true;
  auto &registry = SpatialToolRegistry::instance();
  registry.registerTool( std::make_shared<ToolManifestTool>() );
  registry.registerTool( std::make_shared<ErrorCodesTool>() );
}

} // namespace sicnu::agent::harness

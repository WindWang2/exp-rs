// src/agent/spatial_tools/spatial_tool.cpp
#include "spatial_tool.h"

#include "../layout_tools/layout_tools.h"

#include "model_catalog_tool.h"
#include "temporal_collection_tools.h"
#include "temporal_workspace_tools.h"
#include "raster_inspect_tool.h"
#include "vector_inspect_tool.h"
#include "workspace_tools.h"
#include "governance_tools.h"
#include "sample_tools.h"
#include "result_assessment_tool.h"
#include "capability_tools.h"
#include "io_tools.h"
#include "workflow_preflight_tool.h"
#include "../cartography/cartography_tools.h"
#include "../symbology/symbology_tools.h"
#include "../commands/workspace_commands.h"
#include "../harness/harness_tools.h"
#include "../harness/capability_graph.h"
#include "../harness/grounding_tools.h"
#include "../harness/plan_tools.h"
#include "../harness/recipe_tools.h"
#include "../harness/solution_tools.h"
#include "../contracts/spatial_contracts.h"

namespace sicnu::agent::spatial_tools {

std::string validateAgainstRequired( const Json::Value &input, const Json::Value &schema )
{
  if ( !schema.isObject() || !schema.isMember( "required" ) || !schema["required"].isArray() )
    return std::string();

  if ( !input.isObject() )
    return "Tool input must be a JSON object";

  for ( const auto &key : schema["required"] )
  {
    if ( !key.isString() )
      continue;
    if ( !input.isMember( key.asString() ) )
      return "Missing required parameter: " + key.asString();
  }

  // Declared-type check (#620): validating only `required` let a
  // {"path": {"a": 1}} input reach asString() and escape as an untyped
  // -32000 instead of a structured INVALID_PARAMETER.
  const Json::Value &properties = schema.isMember( "properties" ) && schema["properties"].isObject()
                                      ? schema["properties"]
                                      : Json::Value::nullSingleton();
  if ( properties.isNull() )
    return std::string();
  for ( const auto &name : input.getMemberNames() )
  {
    if ( !properties.isMember( name ) )
      continue;
    const Json::Value &decl = properties[name];
    if ( !decl.isObject() || !decl.isMember( "type" ) || !decl["type"].isString() )
      continue;
    const std::string type = decl["type"].asString();
    const Json::Value &value = input[name];
    bool ok = true;
    if ( type == "string" )
      ok = value.isString();
    else if ( type == "integer" )
      ok = value.isIntegral();
    else if ( type == "number" )
      ok = value.isNumeric();
    else if ( type == "boolean" )
      ok = value.isBool();
    else if ( type == "array" )
      ok = value.isArray();
    else if ( type == "object" )
      ok = value.isObject();
    if ( !ok )
      return "Parameter '" + name + "' must be of type " + type;
  }
  return std::string();
}

SpatialToolRegistry &SpatialToolRegistry::instance()
{
  static SpatialToolRegistry registry;
  return registry;
}

namespace {

/// Harness 7.0 (mission Area I): runtime output meter. Every tool result
/// passing through the registry is measured against
/// contracts::kMaxToolOutputBytes; oversized outputs are compacted
/// schema-aware — the largest array-valued member is trimmed first (the
/// shape survives, the envelope stays parseable) and the truncation is
/// declared, never silent. This turns the 3.0 advisory cap into an enforced
/// one at the single choke point every agent-facing tool passes through.
class MeteredTool final : public SpatialTool
{
  public:
    explicit MeteredTool( SpatialToolPtr inner ) : mInner( std::move( inner ) ) {}

    std::string name() const override { return mInner->name(); }
    std::string displayName() const override { return mInner->displayName(); }
    std::string description() const override { return mInner->description(); }
    std::vector<std::string> tags() const override { return mInner->tags(); }
    Json::Value inputSchema() const override { return mInner->inputSchema(); }
    Json::Value outputSchema() const override { return mInner->outputSchema(); }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      SpatialToolResult result = mInner->execute( input );
      if ( result.success )
      {
        compactIfOversized( result.output );
      }
      else if ( result.error.size() > sicnu::agent::contracts::kMaxToolOutputBytes )
      {
        // Failure envelopes are bounded by the error taxonomy in practice;
        // a runaway operator log is clamped instead of passed through.
        result.error.erase( sicnu::agent::contracts::kMaxToolOutputBytes );
        result.error += "[truncated: error exceeded the 512 KiB tool budget]";
      }
      return result;
    }

  private:
    /// Trims `member` (an array) to the largest prefix whose serialized size
    /// fits the budget. Deterministic: halving from the full length.
    void trimArray( Json::Value &output, const std::string &member, size_t budget ) const
    {
      int count = static_cast<int>( output[member].size() );
      while ( count >= 1 )
      {
        Json::Value trimmed( Json::arrayValue );
        for ( int i = 0; i < count; ++i )
          trimmed.append( output[member][i] );
        Json::Value candidate = output;
        candidate[member] = trimmed;
        candidate["truncated"] = true;
        candidate["truncated_field"] = member;
        if ( sicnu::agent::contracts::serializedSize( candidate ) <= budget )
        {
          output[member] = trimmed;
          output["truncated"] = true;
          output["truncated_field"] = member;
          return;
        }
        count /= 2;
      }
    }

    void compactIfOversized( Json::Value &output ) const
    {
      using sicnu::agent::contracts::kMaxToolOutputBytes;
      using sicnu::agent::contracts::serializedSize;
      if ( serializedSize( output ) <= kMaxToolOutputBytes )
        return;
      const size_t budget = kMaxToolOutputBytes - 512; // room for markers
      const Json::UInt64 originalBytes = static_cast<Json::UInt64>( serializedSize( output ) );

      // Prefer trimming the largest array-valued member.
      std::string largest;
      size_t largestSize = 0;
      for ( const std::string &key : output.getMemberNames() )
      {
        if ( !output[key].isArray() )
          continue;
        const size_t size = serializedSize( output[key] );
        if ( size > largestSize )
        {
          largestSize = size;
          largest = key;
        }
      }
      if ( !largest.empty() )
      {
        trimArray( output, largest, budget );
        if ( serializedSize( output ) <= kMaxToolOutputBytes )
          return;
      }
      // Last resort: an honest compact envelope replaces the payload.
      Json::Value compact( Json::objectValue );
      compact["truncated"] = true;
      compact["truncated_field"] = largest.empty() ? "*" : largest;
      compact["original_bytes"] = originalBytes;
      compact["note"] = "output exceeded the 512 KiB tool budget and was elided; "
                        "re-query with a narrower filter or pagination";
      output = compact;
    }

    SpatialToolPtr mInner;
};

} // namespace

bool SpatialToolRegistry::registerTool( SpatialToolPtr tool )
{
  if ( !tool || tool->name().empty() )
    return false;

  std::lock_guard<std::mutex> lock( mMutex );
  // Every registration is metered (Harness 7.0 Area I): callers get the
  // decorator transparently from find(), so the 512 KiB cap holds no matter
  // which surface executes the tool. The name is captured and the wrapper
  // built BEFORE the emplace — the emplace arguments' evaluation order is
  // unspecified, and moving the pointer away before name() reads it was a
  // null dereference (found as a segfault in the #725 facets test).
  const std::string name = tool->name();
  const SpatialToolPtr metered = std::make_shared<MeteredTool>( std::move( tool ) );
  return mTools.emplace( name, metered ).second;
}

void SpatialToolRegistry::registerBuiltinTools()
{
  static const std::vector<SpatialToolPtr> kBuiltinTools = {
    std::make_shared<RasterInspectTool>(),
    std::make_shared<VectorInspectTool>(),
    std::make_shared<ModelCatalogTool>(),
    std::make_shared<TemporalCreateCollectionTool>(),
    std::make_shared<TemporalDescribeCollectionTool>(),
    std::make_shared<TemporalListScenesTool>(),
    std::make_shared<TemporalPreflightCollectionTool>(),
    std::make_shared<TemporalListCollectionsTool>(),
    std::make_shared<TemporalGetCollectionTool>(),
    std::make_shared<TemporalRegisterCollectionTool>(),
    std::make_shared<TemporalRemoveCollectionTool>(),
    std::make_shared<TemporalIngestStacTool>(),
  };
  for ( const auto &tool : kBuiltinTools )
    registerTool( tool );
  // Cartographic layout tools (Cartographic Layout Studio); layout:* tools
  // mutate layout state and must register alongside the spatial tool surface.
  layout_tools::registerBuiltinLayoutTools();
  // Spatial Scientist 3.0 surfaces (ADR 0128): workspace understanding,
  // bounded sampling/compare, result assessment, capability ranking, model
  // selection, and static workflow preflight.
  registerWorkspaceTools();
  registerSampleTools();
  registerResultAssessmentTool();
  registerCapabilityTools();
  // Foundation 5.0: read-only io: probe/capabilities/product surfaces.
  registerIoTools();
  registerWorkflowPreflightTool();
  cartography::registerCartographyTools();
  symbology::registerSymbologyTools();
  commands::registerWorkspaceCommandTools();
  // Workspace Governance 3.0 (Platform 3.0): bounded project/asset/lineage/
  // result/run surfaces over the WorkspaceService.
  registerGovernanceTools();
  // Harness 4.0: error taxonomy + tool manifest catalog surfaces.
  harness::registerHarnessTools();
  // Harness 4.0 grounding: data:understand + revision-stamped harness:context.
  harness::registerGroundingTools();
  // Harness 4.0 plan lifecycle: scientific preflight, plan compile, execute,
  // run status with automatic verification and map confirmation.
  harness::registerPlanTools();
  // Harness 4.0 scientific recipes (metadata under data/agent/recipes).
  harness::registerRecipeTools();
  // Harness 7.0 intent->capability graph: deterministic goal classification +
  // feasibility-ranked candidates (typed ambiguity, no guessing).
  harness::registerCapabilityGraphTools();
  // Platform 5.0 solution knowledge: solution:search/describe/validate/instantiate.
  harness::registerSolutionTools();
}

void SpatialToolRegistry::reset()
{
  {
    std::lock_guard<std::mutex> lock( mMutex );
    mTools.clear();
  }
  // registerBuiltinTools() locks mMutex itself — call it after releasing.
  registerBuiltinTools();
}

std::optional<SpatialToolPtr> SpatialToolRegistry::find( const std::string &name ) const
{
  std::lock_guard<std::mutex> lock( mMutex );
  const auto it = mTools.find( name );
  if ( it == mTools.end() )
    return std::nullopt;
  return it->second;
}

std::vector<SpatialToolPtr> SpatialToolRegistry::tools() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  std::vector<SpatialToolPtr> result;
  result.reserve( mTools.size() );
  for ( const auto &[name, tool] : mTools )
    result.push_back( tool );
  return result;
}

size_t SpatialToolRegistry::size() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  return mTools.size();
}

} // namespace sicnu::agent::spatial_tools

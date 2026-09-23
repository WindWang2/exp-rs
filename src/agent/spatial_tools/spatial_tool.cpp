// src/agent/spatial_tools/spatial_tool.cpp
#include "spatial_tool.h"

#include "explain_step_tool.h"

#include "../layout_tools/layout_tools.h"

#include "model_catalog_tool.h"
#include "temporal_collection_tools.h"
#include "temporal_workspace_tools.h"
#include "raster_inspect_tool.h"
#include "spectral_spatial_tools.h"
#include "vector_inspect_tool.h"
#include "geometric_spatial_tool.h"
#include "workspace_tools.h"
#include "governance_tools.h"
#include "sample_tools.h"
#include "result_assessment_tool.h"
#include "capability_tools.h"
#include "io_tools.h"
#include "terrain_spatial_tools.h"
#include "workflow_preflight_tool.h"
#include "../cartography/cartography_tools.h"
#include "../symbology/symbology_tools.h"
#include "../commands/workspace_commands.h"
#include "../harness/harness_tools.h"
#include "../harness/autonomy_tools.h"
#include "../harness/lab_tools.h"
#include "../harness/capability_graph.h"
#include "../harness/grounding_tools.h"
#include "../harness/grounding_probes.h"
#include "../harness/plan_tools.h"
#include "../harness/recipe_tools.h"
#include "../harness/run_loop.h"
#include "../harness/solution_tools.h"
#include "../harness/workflow_planner.h"
#include "../harness/context_checkpoint.h"
#include "../harness/tool_shortlist.h"
#include "../contracts/spatial_contracts.h"
#include "mission_tools.h"

#include <atomic>

namespace sicnu::agent::spatial_tools {

namespace {
// Once-per-process guard for registerBuiltinTools(): the MCP routing consults
// the registry on every unknown-prefix call, and each consult used to rebuild
// every builtin tool object (~130 constructions per io: call) and rely on
// emplace() failures to dedupe. reset() clears the flag so an explicit
// registry wipe re-registers on the next call.
std::atomic<bool> gBuiltinToolsInstalled{ false };
} // namespace

void SpatialToolRegistry::reset()
{
  {
    std::lock_guard<std::mutex> lock( mMutex );
    mTools.clear();
  }
  gBuiltinToolsInstalled.store( false, std::memory_order_release );
  // registerBuiltinTools() locks mMutex itself — call it after releasing.
  registerBuiltinTools();
}

void SpatialToolRegistry::registerBuiltinTools()
{
  if ( gBuiltinToolsInstalled.load( std::memory_order_acquire ) )
    return;
  static const std::vector<SpatialToolPtr> kBuiltinTools = {
    std::make_shared<RasterInspectTool>(),
    // D13 · Radiometric Spectral Workbench: agent spectral tools.
    std::make_shared<exp_agent::SpectralInspectTool>(),
    std::make_shared<exp_agent::ValidateBoaPhysicsTool>(),
    // RS14-15 Explainable Workflow: the why-this-step teaching surface.
    createExplainStepTool(),
    std::make_shared<VectorInspectTool>(),
    // F13: geometric registration surface (D14 tool finally cataloged).
    std::make_shared<GeometricSpatialTool>(),
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
  registerTerrainTools();
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
  // Compiler & grounding 11.0: bounded fact/model probe surfaces.
  harness::registerGroundingProbeTools();
  // Harness 4.0 plan lifecycle: scientific preflight, plan compile, execute,
  // run status with automatic verification and map confirmation.
  harness::registerPlanTools();
  // Harness 4.0 scientific recipes (metadata under data/agent/recipes).
  harness::registerRecipeTools();
  // Harness 7.0 intent->capability graph: deterministic goal classification +
  // feasibility-ranked candidates (typed ambiguity, no guessing).
  harness::registerCapabilityGraphTools();
  // Harness 9.0 (M5): bounded run diagnosis + structured repair proposals.
  harness::registerRunLoopTools();
  // D9: lab copilot (teaching mode) — student-safe ask + teacher reference.
  harness::registerLabTools();
  // RS14-12: teaching autonomy ladder — read-only policy status projection.
  harness::registerAutonomyTools();
  // Platform 5.0 solution knowledge: solution:search/describe/validate/instantiate.
  harness::registerSolutionTools();
  // Compiler 10.0 (ADR 0149): typed WorkflowIR compiler, harness session
  // checkpoint/resume, and budgeted tool shortlist / knowledge budget.
  harness::registerWorkflowPlannerTools();
  harness::registerContextSessionTools();
  harness::registerToolShortlistTools();
  // Mission Runtime 13.0: the real mission:* tool objects over the
  // single-authority mission runtime store.
  registerMissionTools();
  gBuiltinToolsInstalled.store( true, std::memory_order_release );
}

} // namespace sicnu::agent::spatial_tools

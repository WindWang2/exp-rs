// src/agent/harness/tool_manifest.cpp
#include "tool_manifest.h"

#include <array>

#include "tool_taxonomy.h"

namespace sicnu::agent::harness {

namespace {

struct MutatingTool {
  const char *toolId;
  const char *riskClass;
};

/// Inline tools that mutate state, despite living in read-mostly namespaces.
/// Execution ids (rs:/gdal:/otb:/qgis:) are creates_artifact by prefix rule;
/// this table only needs the exceptions and the destructive/network cases.
constexpr std::array<MutatingTool, 41> kMutating = { {
  // temporal: registration + removal
  { "temporal:create_collection", risk_classes::kCreatesArtifact },
  { "temporal:register_collection", risk_classes::kModifiesProject },
  { "temporal:remove_collection", risk_classes::kDestructive },
  { "temporal:ingest_stac", risk_classes::kCreatesArtifact },
  // cartography: compose/repair/chart mutations write layouts
  { "cartography:compose", risk_classes::kModifiesProject },
  { "cartography:repair", risk_classes::kModifiesProject },
  { "cartography:chart_create", risk_classes::kModifiesProject },
  { "cartography:chart_delete", risk_classes::kDestructive },
  { "cartography:instantiate_template", risk_classes::kModifiesProject },
  // symbology: renderer mutations (undoable)
  { "symbology:apply_categorical", risk_classes::kModifiesProject },
  { "symbology:apply_graduated", risk_classes::kModifiesProject },
  { "symbology:apply_raster_ramp", risk_classes::kModifiesProject },
  // Platform 5.0: StyleSpec application mutates layer renderers (undoable);
  // solution instantiation produces plan/mapspec drafts for later execution.
  { "style:apply", risk_classes::kModifiesProject },
  { "solution:instantiate", risk_classes::kCreatesArtifact },
  // layout: everything except list/preflight/inspect mutates layouts
  { "layout:create", risk_classes::kModifiesProject },
  { "layout:export", risk_classes::kCreatesArtifact },
  { "layout:add_item", risk_classes::kModifiesProject },
  { "layout:set_item_properties", risk_classes::kModifiesProject },
  { "layout:remove_item", risk_classes::kDestructive },
  { "layout:auto_arrange", risk_classes::kModifiesProject },
  { "layout:align_items", risk_classes::kModifiesProject },
  { "layout:distribute_items", risk_classes::kModifiesProject },
  { "layout:save_template", risk_classes::kCreatesArtifact },
  { "layout:apply_template", risk_classes::kModifiesProject },
  { "layout:save_project", risk_classes::kModifiesProject },
  { "layout:load_project", risk_classes::kModifiesProject },
  // workspace: undo/redo change project state
  { "workspace:undo", risk_classes::kModifiesProject },
  { "workspace:redo", risk_classes::kModifiesProject },
  // governance: relink moves an asset locator
  { "asset:relink", risk_classes::kModifiesProject },
  // interaction: canvas/display mutations (ephemeral display state)
  { "view:set_extent", risk_classes::kModifiesDisplay },
  { "view:set_scale", risk_classes::kModifiesDisplay },
  { "view:zoom_to_layer", risk_classes::kModifiesDisplay },
  { "view:zoom_to_asset", risk_classes::kModifiesDisplay },
  { "view:fit_all", risk_classes::kModifiesDisplay },
  { "roi:set", risk_classes::kModifiesDisplay },
  { "roi:clear", risk_classes::kModifiesDisplay },
  { "canvas:draw_roi", risk_classes::kModifiesDisplay },
  { "canvas:zoom_to_extent", risk_classes::kModifiesDisplay },
  { "raster:set_band_composite", risk_classes::kModifiesDisplay },
  { "raster:set_stretch", risk_classes::kModifiesDisplay },
  { "raster:reset_display", risk_classes::kModifiesDisplay },
} };

/// Provider-algorithm prefixes run external processes (CLI providers) —
/// risk class notes that, though the outputs are still artifacts.
bool isExternalProviderId( const std::string &id )
{
  return id.rfind( "gdal:", 0 ) == 0 || id.rfind( "gdal_tools:", 0 ) == 0 ||
         id.rfind( "otb:", 0 ) == 0 || id.rfind( "otb_tools:", 0 ) == 0 ||
         id.rfind( "qgis:", 0 ) == 0 || id.rfind( "qgis_algorithms:", 0 ) == 0;
}

bool isExecutionId( const std::string &id )
{
  return isExternalProviderId( id ) || id.rfind( "rs:", 0 ) == 0 ||
         id.rfind( "opencv:", 0 ) == 0;
}

} // namespace

bool isKnownRiskClass( const std::string &riskClass )
{
  return riskClass == risk_classes::kReadOnly || riskClass == risk_classes::kModifiesDisplay ||
         riskClass == risk_classes::kCreatesArtifact || riskClass == risk_classes::kModifiesProject ||
         riskClass == risk_classes::kDestructive || riskClass == risk_classes::kExternalProcess ||
         riskClass == risk_classes::kNetwork;
}

std::string riskClassForToolId( const std::string &toolId )
{
  for ( const MutatingTool &entry : kMutating )
  {
    if ( entry.toolId && toolId == entry.toolId )
      return entry.riskClass;
  }
  if ( isExecutionId( toolId ) )
  {
    // OTB/GDAL/QGIS providers shell out to external binaries.
    return isExternalProviderId( toolId ) ? risk_classes::kExternalProcess
                                          : risk_classes::kCreatesArtifact;
  }
  return risk_classes::kReadOnly;
}

bool isKnownMutatingTool( const std::string &toolId )
{
  const std::string risk = riskClassForToolId( toolId );
  return risk != risk_classes::kReadOnly && risk != risk_classes::kModifiesDisplay;
}

Json::Value ToolManifest::toJson() const
{
  Json::Value v( Json::objectValue );
  v["taxonomy"] = taxonomy;
  v["risk_class"] = riskClass;
  v["side_effects"] = sideEffects;
  v["idempotent"] = idempotent;
  v["cancellable"] = cancellable;
  if ( !memoryPolicy.empty() )
    v["memory_policy"] = memoryPolicy;
  if ( !determinismGrade.empty() )
    v["determinism_grade"] = determinismGrade;
  if ( !costClass.empty() )
    v["cost_class"] = costClass;
  v["large_raster_safe"] = largeRasterSafe;
  v["gpu"] = gpuAccelerated;
  v["produces_provenance"] = producesProvenance;
  if ( !expectedArtifacts.empty() )
  {
    Json::Value artifacts( Json::arrayValue );
    for ( const ExpectedArtifact &a : expectedArtifacts )
    {
      Json::Value obj( Json::objectValue );
      obj["name"] = a.name;
      obj["kind"] = a.kind;
      obj["persistence"] = a.persistence;
      artifacts.append( obj );
    }
    v["expected_artifacts"] = artifacts;
  }
  if ( !preconditions.empty() )
  {
    Json::Value pre( Json::arrayValue );
    for ( const Precondition &p : preconditions )
    {
      Json::Value obj( Json::objectValue );
      obj["check"] = p.check;
      if ( !p.description.empty() )
        obj["description"] = p.description;
      pre.append( obj );
    }
    v["preconditions"] = pre;
  }
  return v;
}

ToolManifest manifestForToolId( const std::string &toolId )
{
  ToolManifest m;
  m.toolId = toolId;
  const ToolTaxonomy taxonomy = taxonomyForTool( toolId );
  m.taxonomy = taxonomy.toString();
  m.riskClass = riskClassForToolId( toolId );
  m.sideEffects = isKnownMutatingTool( toolId );
  m.idempotent = !m.sideEffects;

  if ( isExecutionId( toolId ) )
  {
    // Operator/algorithm runs produce committed artifacts, honor cooperative
    // cancellation in the Task Center, and stamp provenance.
    m.cancellable = true;
    m.producesProvenance = true;
    ExpectedArtifact output;
    output.name = "output";
    output.kind = "raster";
    output.persistence = "committed_asset";
    m.expectedArtifacts.push_back( output );
    m.preconditions.push_back( { "input_exists", "all input paths resolve" } );
  }
  return m;
}

Json::Value harnessBlockForToolId( const std::string &toolId )
{
  return manifestForToolId( toolId ).toJson();
}

} // namespace sicnu::agent::harness

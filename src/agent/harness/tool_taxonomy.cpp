// src/agent/harness/tool_taxonomy.cpp
#include "tool_taxonomy.h"

#include <algorithm>
#include <array>

namespace sicnu::agent::harness {

namespace {

constexpr const char *kDomains[] = {
  "data", "raster", "processing", "workflow", "model",
  "map", "project", "result", "provenance", "context", "harness", "other",
};

constexpr const char *kActions[] = {
  "inspect", "search", "register", "convert", "sample", "statistics",
  "describe", "run", "preflight", "plan", "resume", "select",
  "compose", "render", "repair", "verify", "execute",
  "recipe", "catalog", "mutate", "other",
};

struct Override {
  const char *toolId;
  const char *domain;
  const char *action;
};

/// Explicit classifications for tools whose name alone does not determine the
/// taxonomy. Prefix rules handle the rest (see classifyByPrefix).
constexpr std::array<Override, 84> kOverrides = { {
  // data:
  { "data:list_layers", "data", "inspect" },
  { "data:describe_dataset", "data", "inspect" },
  { "data:get_lineage", "provenance", "inspect" },
  // spatial: inspection & sampling
  { "spatial:understand", "data", "inspect" },
  { "spatial:raster_inspect", "raster", "inspect" },
  { "spatial:vector_inspect", "data", "inspect" },
  { "spatial:sample_pixels", "raster", "sample" },
  { "spatial:sample_features", "data", "sample" },
  { "spatial:compare_rasters", "raster", "statistics" },
  { "spatial:assess_result", "result", "verify" },
  { "spatial:workspace_summary", "context", "read" },
  { "spatial:layer_summary", "context", "read" },
  { "spatial:search_capabilities", "processing", "search" },
  { "spatial:select_model", "model", "select" },
  { "spatial:list_models", "model", "inspect" },
  // temporal:
  { "temporal:create_collection", "data", "register" },
  { "temporal:register_collection", "data", "register" },
  { "temporal:remove_collection", "data", "mutate" },
  { "temporal:ingest_stac", "data", "register" },
  { "temporal:describe_collection", "data", "inspect" },
  { "temporal:list_scenes", "data", "inspect" },
  { "temporal:preflight_collection", "data", "preflight" },
  { "temporal:list_collections", "data", "search" },
  { "temporal:get_collection", "data", "inspect" },
  // cartography:
  { "cartography:compose", "map", "compose" },
  { "cartography:preflight", "map", "preflight" },
  { "cartography:repair", "map", "repair" },
  { "cartography:list_components", "map", "search" },
  { "cartography:get_component", "map", "inspect" },
  { "cartography:list_templates", "map", "search" },
  { "cartography:instantiate_template", "map", "compose" },
  { "cartography:chart_create", "map", "compose" },
  { "cartography:chart_get", "map", "inspect" },
  { "cartography:chart_list", "map", "search" },
  { "cartography:chart_delete", "map", "repair" },
  // symbology:
  { "symbology:describe", "map", "inspect" },
  { "symbology:apply_categorical", "map", "mutate" },
  { "symbology:apply_graduated", "map", "mutate" },
  { "symbology:apply_raster_ramp", "map", "mutate" },
  // layout:
  { "layout:list", "map", "search" },
  { "layout:create", "map", "compose" },
  { "layout:export", "map", "render" },
  { "layout:preflight", "map", "preflight" },
  { "layout:list_items", "map", "inspect" },
  { "layout:get_item_properties", "map", "inspect" },
  { "layout:add_item", "map", "compose" },
  { "layout:set_item_properties", "map", "mutate" },
  { "layout:remove_item", "map", "repair" },
  { "layout:auto_arrange", "map", "repair" },
  { "layout:align_items", "map", "repair" },
  { "layout:distribute_items", "map", "repair" },
  { "layout:save_template", "map", "compose" },
  { "layout:apply_template", "map", "compose" },
  { "layout:save_project", "map", "mutate" },
  { "layout:load_project", "map", "mutate" },
  // workspace:
  { "workspace:undo", "context", "mutate" },
  { "workspace:redo", "context", "mutate" },
  { "workspace:history", "context", "read" },
  // governance:
  { "project:summary", "project", "inspect" },
  { "project:search", "project", "search" },
  { "project:health", "project", "inspect" },
  { "asset:inspect", "data", "inspect" },
  { "asset:validate", "data", "preflight" },
  { "asset:relink", "data", "mutate" },
  { "collection:query", "data", "search" },
  { "lineage:upstream", "provenance", "inspect" },
  { "lineage:downstream", "provenance", "inspect" },
  { "result:inspect", "result", "inspect" },
  { "run:compare", "result", "inspect" },
  // workflow:
  { "workflow:preflight", "workflow", "preflight" },
  // interaction:
  { "view:get_state", "context", "read" },
  { "view:set_extent", "context", "mutate" },
  { "view:set_scale", "context", "mutate" },
  { "view:zoom_to_layer", "context", "mutate" },
  { "view:zoom_to_asset", "context", "mutate" },
  { "view:fit_all", "context", "mutate" },
  { "roi:set", "context", "mutate" },
  { "roi:clear", "context", "mutate" },
  { "canvas:draw_roi", "context", "mutate" },
  { "canvas:zoom_to_extent", "context", "mutate" },
  { "raster:get_display", "map", "inspect" },
  { "raster:set_band_composite", "map", "mutate" },
  { "raster:set_stretch", "map", "mutate" },
  { "raster:reset_display", "map", "mutate" },
} };

ToolTaxonomy classifyByPrefix( const std::string &id )
{
  // Execution ids are operator runs: the authoritative "processing.run".
  if ( id.rfind( "rs:", 0 ) == 0 || id.rfind( "gdal:", 0 ) == 0 ||
       id.rfind( "gdal_tools:", 0 ) == 0 || id.rfind( "otb:", 0 ) == 0 ||
       id.rfind( "otb_tools:", 0 ) == 0 || id.rfind( "qgis:", 0 ) == 0 ||
       id.rfind( "qgis_algorithms:", 0 ) == 0 || id.rfind( "opencv:", 0 ) == 0 )
    return { "processing", "run" };

  struct PrefixRule {
    const char *prefix;
    const char *domain;
    const char *action;
  };
  static const PrefixRule kRules[] = {
    { "harness:preflight", "harness", "preflight" },
    { "harness:plan", "harness", "plan" },
    { "harness:execute", "harness", "execute" },
    { "harness:verify", "harness", "verify" },
    { "harness:run", "workflow", "resume" },
    { "harness:recipe", "harness", "recipe" },
    { "harness:context", "context", "read" },
    { "harness:tool", "harness", "catalog" },
    { "harness:error", "harness", "catalog" },
    { "harness:", "harness", "catalog" },
    { "model:", "model", "run" },
    { "workflow:", "workflow", "plan" },
    { "data:", "data", "inspect" },
    { "spatial:", "data", "search" },
    { "temporal:", "data", "inspect" },
    { "cartography:", "map", "preflight" },
    { "symbology:", "map", "mutate" },
    { "layout:", "map", "compose" },
    { "workspace:", "context", "mutate" },
    { "project:", "project", "inspect" },
    { "asset:", "data", "inspect" },
    { "collection:", "data", "search" },
    { "lineage:", "provenance", "inspect" },
    { "result:", "result", "inspect" },
    { "run:", "result", "inspect" },
    { "view:", "context", "mutate" },
    { "roi:", "context", "mutate" },
    { "canvas:", "context", "mutate" },
    { "raster:", "map", "mutate" },
  };
  for ( const PrefixRule &rule : kRules )
  {
    if ( id.rfind( rule.prefix, 0 ) == 0 )
      return { rule.domain, rule.action };
  }
  return { "other", "other" };
}

} // namespace

std::string ToolTaxonomy::toString() const
{
  if ( !valid() )
    return {};
  return domain + "." + action;
}

bool isKnownTaxonomyDomain( const std::string &domain )
{
  for ( const char *d : kDomains )
  {
    if ( domain == d )
      return true;
  }
  return false;
}

bool isKnownTaxonomyAction( const std::string &domain, const std::string &action )
{
  if ( !isKnownTaxonomyDomain( domain ) )
    return false;
  for ( const char *a : kActions )
  {
    if ( action == a )
      return true;
  }
  return false;
}

ToolTaxonomy taxonomyForTool( const std::string &toolId )
{
  for ( const Override &override : kOverrides )
  {
    if ( override.toolId && toolId == override.toolId )
      return { override.domain, override.action };
  }
  return classifyByPrefix( toolId );
}

} // namespace sicnu::agent::harness

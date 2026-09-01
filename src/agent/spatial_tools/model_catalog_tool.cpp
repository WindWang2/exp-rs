// src/agent/spatial_tools/model_catalog_tool.cpp
#include "model_catalog_tool.h"

#include "operators/framework/model_catalog.h"

// The GPU default comes from the platform's own CUDA detection (the model
// runtime layer) — reused, never re-implemented here. That layer only exists
// in OpenCV builds; without OpenCV gpu_available keeps its false default.
#ifdef SICNU_HAS_OPENCV
#include "operators/runtime/model_runtime.h"
#endif

namespace sicnu::agent::spatial_tools {

std::string ModelCatalogTool::description() const
{
  return "Discover, filter, and rank registered model runtimes from the model "
         "catalog (models/*/model.json manifests): name, task (segmentation, "
         "classification, detection, ...), input/output contract, framework "
         "(onnx), GPU requirement, benchmark accuracy, supported sensors, and "
         "local weight paths. Supports automated multi-criteria ranking by "
         "input band roles, sensor, spatial resolution, and GPU/VRAM hardware "
         "constraints to assist Pi in selecting optimal models before "
         "rs:inference. gpu_available defaults to the platform's own CUDA "
         "detection when the argument is omitted.";
}

std::vector<std::string> ModelCatalogTool::tags() const
{
  return { "spatial", "models", "catalog", "inference", "onnx", "ranking", "selection", "machine-learning" };
}

Json::Value ModelCatalogTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );
  Json::Value task( Json::objectValue );
  task["type"] = "string";
  task["description"] = "Only list or rank models for this task (e.g. 'segmentation', 'classification', 'detection')";
  props["task"] = task;

  Json::Value name( Json::objectValue );
  name["type"] = "string";
  name["description"] = "Look up a single model by name";
  props["name"] = name;

  Json::Value sensor( Json::objectValue );
  sensor["type"] = "string";
  sensor["description"] = "Filter / rank models compatible with this sensor (e.g. 'Sentinel-2', 'Landsat-8', 'GF-2')";
  props["sensor"] = sensor;

  // Optional array of band-role names (mirrors the manifest input.band_roles
  // vocabulary, e.g. ["Red", "Green", "Blue", "NIR"]) feeding rankModels'
  // band-role scoring (#705).
  Json::Value bandRoles( Json::objectValue );
  bandRoles["type"] = "array";
  bandRoles["description"] = "Band roles to rank models by (e.g. ['Red', 'Green', 'Blue', 'NIR'])";
  Json::Value roleItems( Json::objectValue );
  roleItems["type"] = "string";
  bandRoles["items"] = roleItems;
  props["band_roles"] = bandRoles;

  Json::Value resolution( Json::objectValue );
  resolution["type"] = "number";
  resolution["description"] = "Spatial resolution in meters for compatibility evaluation";
  props["resolution"] = resolution;

  Json::Value gpuAvailable( Json::objectValue );
  gpuAvailable["type"] = "boolean";
  gpuAvailable["description"] = "Whether GPU acceleration is available (default: auto-detected from the platform)";
  props["gpu_available"] = gpuAvailable;

  Json::Value vramBudget( Json::objectValue );
  vramBudget["type"] = "integer";
  vramBudget["description"] = "VRAM budget in MiB for hardware fit ranking";
  props["vram_budget_mb"] = vramBudget;

  Json::Value rank( Json::objectValue );
  rank["type"] = "boolean";
  rank["description"] = "When true, return ranked candidate models with compatibility scores and reasons";
  props["rank"] = rank;

  schema["properties"] = props;
  return schema;
}

Json::Value ModelCatalogTool::outputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  Json::Value props( Json::objectValue );

  Json::Value models( Json::objectValue );
  models["type"] = "array";
  props["models"] = models;

  Json::Value candidates( Json::objectValue );
  candidates["type"] = "array";
  props["candidates"] = candidates;

  schema["properties"] = props;
  return schema;
}

SpatialToolResult ModelCatalogTool::execute( const Json::Value &input )
{
  // #701: reload() rescans + re-hashes the manifest directory on EVERY call
  // (ready-state checks re-verify artifact checksums). Use the cached catalog
  // and fall back to ONE refresh only when a requested name misses — the
  // same lazy retry rs:infer uses for newly installed models.
  auto &catalog = sicnu::operators::ModelCatalog::instance();
  const std::string requestedName = input.isMember( "name" ) ? input["name"].asString() : std::string();
  if ( !requestedName.empty() && !catalog.find( requestedName ) )
    catalog.reload();

  Json::Value out( Json::objectValue );
  out["directory"] = catalog.directory();

  const std::string name = input.isMember( "name" ) ? input["name"].asString() : std::string();
  if ( !name.empty() )
  {
    const auto model = catalog.find( name );
    if ( !model )
      return SpatialToolResult::failure( "Model not found in catalog: " + name, "MODEL_NOT_FOUND", "validation", false );
    Json::Value models( Json::arrayValue );
    models.append( model->toJson() );
    out["models"] = models;
    return SpatialToolResult::ok( out );
  }

  const bool shouldRank = ( input.isMember( "rank" ) && input["rank"].asBool() ) ||
                          input.isMember( "sensor" ) ||
                          input.isMember( "band_roles" ) ||
                          input.isMember( "resolution" ) ||
                          input.isMember( "gpu_available" ) ||
                          input.isMember( "vram_budget_mb" );

  if ( shouldRank )
  {
    sicnu::operators::ModelQueryCriteria criteria;
    if ( input.isMember( "task" ) )
      criteria.task = input["task"].asString();
    if ( input.isMember( "sensor" ) )
      criteria.sensor = input["sensor"].asString();
    if ( input.isMember( "band_roles" ) && input["band_roles"].isArray() )
    {
      const Json::Value &roles = input["band_roles"];
      for ( Json::ArrayIndex i = 0; i < roles.size(); ++i )
      {
        if ( roles[i].isString() && !roles[i].asString().empty() )
          criteria.bandRoles.push_back( roles[i].asString() );
      }
    }
    if ( input.isMember( "resolution" ) && input["resolution"].isNumeric() )
      criteria.resolutionMeters = input["resolution"].asDouble();
    // #705: gpu_available defaults to the platform's own CUDA detection so
    // GPU-accelerated models are not silently penalized (−0.05) in every
    // default ranking; an explicit argument still wins.
    if ( input.isMember( "gpu_available" ) )
      criteria.gpuAvailable = input["gpu_available"].asBool();
    else
    {
#ifdef SICNU_HAS_OPENCV
      criteria.gpuAvailable =
        sicnu::operators::runtime::ModelRuntimeRegistry::instance().hardware().cudaAvailable;
#endif
    }
    if ( input.isMember( "vram_budget_mb" ) && input["vram_budget_mb"].isNumeric() )
      criteria.maxVramMb = input["vram_budget_mb"].asInt();

    const auto ranked = catalog.rankModels( criteria );
    Json::Value candidateList( Json::arrayValue );
    for ( const auto &cand : ranked )
      candidateList.append( cand.toJson() );
    out["candidates"] = candidateList;
  }
  else
  {
    const std::string task = input.isMember( "task" ) ? input["task"].asString() : std::string();
    const auto all = task.empty() ? catalog.models() : catalog.modelsByTask( task );
    Json::Value models( Json::arrayValue );
    for ( const auto &model : all )
      models.append( model.toJson() );
    out["models"] = models;
  }

  return SpatialToolResult::ok( out );
}

} // namespace sicnu::agent::spatial_tools

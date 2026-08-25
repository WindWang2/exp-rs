// src/agent/spatial_tools/model_catalog_tool.cpp
#include "model_catalog_tool.h"

#include "operators/framework/model_catalog.h"

namespace sicnu::agent::spatial_tools {

std::string ModelCatalogTool::description() const
{
  return "List registered model runtimes from the model catalog (models/*/ "
         "model.json manifests): name, task (segmentation, classification, "
         "detection, ...), input/output contract, framework (onnx), GPU "
         "requirement, benchmark accuracy, and local weight path when "
         "downloaded. Filter by task or look up a single model by name. Use "
         "before rs:inference to pick a model and check its contract.";
}

std::vector<std::string> ModelCatalogTool::tags() const
{
  return { "spatial", "models", "catalog", "inference", "onnx", "machine-learning" };
}

Json::Value ModelCatalogTool::inputSchema() const
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );
  Json::Value task( Json::objectValue );
  task["type"] = "string";
  task["description"] = "Only list models for this task (e.g. 'segmentation', 'classification')";
  props["task"] = task;

  Json::Value name( Json::objectValue );
  name["type"] = "string";
  name["description"] = "Look up a single model by name";
  props["name"] = name;

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

  schema["properties"] = props;
  return schema;
}

SpatialToolResult ModelCatalogTool::execute( const Json::Value &input )
{
  auto &catalog = sicnu::operators::ModelCatalog::instance();
  catalog.reload();

  Json::Value out( Json::objectValue );
  out["directory"] = catalog.directory();

  Json::Value models( Json::arrayValue );

  const std::string name = input.isMember( "name" ) ? input["name"].asString() : std::string();
  if ( !name.empty() )
  {
    const auto model = catalog.find( name );
    if ( !model )
      return SpatialToolResult::failure( "Model not found in catalog: " + name );
    models.append( model->toJson() );
  }
  else
  {
    const std::string task = input.isMember( "task" ) ? input["task"].asString() : std::string();
    const auto all = task.empty() ? catalog.models() : catalog.modelsByTask( task );
    for ( const auto &model : all )
      models.append( model.toJson() );
  }

  out["models"] = models;
  return SpatialToolResult::ok( out );
}

} // namespace sicnu::agent::spatial_tools

// src/agent/tool_catalog/data_tool_provider.cpp
#include "data_tool_provider.h"
#include <algorithm>

namespace sicnu::agent::tool_catalog {

namespace {

AgentTool makeListLayersTool()
{
  AgentTool tool;
  tool.name = "data:list_layers";
  tool.displayName = "List Loaded Map Layers";
  tool.category = ToolCategory::Data;
  tool.group = "data";
  tool.description = "List all raster and vector layers currently loaded in the project and map canvas with layer IDs, names, types, and CRS.";
  tool.tags = { "data", "layers", "project", "catalog", "map", "workspace" };

  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = Json::Value( Json::objectValue );
  tool.inputSchema = schema;

  sicnu::processing::PortDescriptor pLayers;
  pLayers.name = "layers";
  pLayers.displayName = "Layers List";
  pLayers.type = sicnu::processing::DataType::Json;
  tool.outputs.push_back( pLayers );

  tool.agentMetadata.purpose = "List all active map layers in the project";
  tool.agentMetadata.tags = tool.tags;

  return tool;
}

AgentTool makeDescribeDatasetTool()
{
  AgentTool tool;
  tool.name = "data:describe_dataset";
  tool.displayName = "Describe Dataset Metadata";
  tool.category = ToolCategory::Data;
  tool.group = "data";
  tool.description = "Get detailed dataset metadata for a layer or file: dimensions, bounding box, CRS, resolution, data types, and band/attribute information.";
  tool.tags = { "data", "metadata", "dataset", "describe", "raster", "vector", "statistics" };

  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );
  Json::Value layerId( Json::objectValue );
  layerId["type"] = "string";
  layerId["description"] = "Layer ID, layer name, or file path to describe";
  props["layer_id"] = layerId;

  schema["properties"] = props;
  Json::Value req( Json::arrayValue );
  req.append( "layer_id" );
  schema["required"] = req;

  tool.inputSchema = schema;

  sicnu::processing::PortDescriptor pLayer;
  pLayer.name = "layer_id";
  pLayer.displayName = "Layer ID";
  pLayer.type = sicnu::processing::DataType::String;
  pLayer.required = true;
  tool.inputs.push_back( pLayer );

  sicnu::processing::PortDescriptor pMeta;
  pMeta.name = "metadata";
  pMeta.displayName = "Dataset Metadata";
  pMeta.type = sicnu::processing::DataType::Json;
  tool.outputs.push_back( pMeta );

  tool.agentMetadata.purpose = "Query layer or file spatial and attribute metadata";
  tool.agentMetadata.tags = tool.tags;

  return tool;
}

AgentTool makeGetLineageTool()
{
  AgentTool tool;
  tool.name = "data:get_lineage";
  tool.displayName = "Get Asset Lineage and Provenance";
  tool.category = ToolCategory::Data;
  tool.group = "data";
  tool.description = "Query the processing provenance and derivation history for a DataManager asset: source inputs, algorithm, parameters, task reference, and downstream outputs.";
  tool.tags = { "data", "lineage", "provenance", "asset", "history", "derivation" };

  Json::Value schema( Json::objectValue );
  schema["type"] = "object";

  Json::Value props( Json::objectValue );
  Json::Value assetId( Json::objectValue );
  assetId["type"] = "string";
  assetId["description"] = "UUID string of the DataManager asset";
  props["asset_id"] = assetId;

  schema["properties"] = props;
  Json::Value req( Json::arrayValue );
  req.append( "asset_id" );
  schema["required"] = req;

  tool.inputSchema = schema;

  sicnu::processing::PortDescriptor pAsset;
  pAsset.name = "asset_id";
  pAsset.displayName = "Asset ID";
  pAsset.type = sicnu::processing::DataType::String;
  pAsset.required = true;
  tool.inputs.push_back( pAsset );

  sicnu::processing::PortDescriptor pLineage;
  pLineage.name = "lineage";
  pLineage.displayName = "Lineage Record";
  pLineage.type = sicnu::processing::DataType::Json;
  tool.outputs.push_back( pLineage );

  tool.agentMetadata.purpose = "Query asset derivation graph and provenance history";
  tool.agentMetadata.tags = tool.tags;

  return tool;
}

} // namespace

DataToolProvider::DataToolProvider()
{
  resetDefaults();
}

void DataToolProvider::resetDefaults()
{
  std::lock_guard<std::mutex> lock( mMutex );
  mTools.clear();

  auto listLayers = makeListLayersTool();
  mTools[listLayers.name] = listLayers;

  auto describe = makeDescribeDatasetTool();
  mTools[describe.name] = describe;

  auto lineage = makeGetLineageTool();
  mTools[lineage.name] = lineage;
}

std::vector<AgentTool> DataToolProvider::provideTools() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  std::vector<AgentTool> result;
  result.reserve( mTools.size() );
  for ( const auto &pair : mTools )
  {
    result.push_back( pair.second );
  }
  // Deterministic listing order (#634).
  std::sort( result.begin(), result.end(),
             []( const AgentTool &a, const AgentTool &b ) { return a.name < b.name; } );
  return result;
}

std::optional<AgentTool> DataToolProvider::findTool( const std::string &name ) const
{
  std::lock_guard<std::mutex> lock( mMutex );
  auto it = mTools.find( name );
  if ( it != mTools.end() )
    return it->second;

  // Check aliases without "data:" prefix or with normalized underscores
  for ( const auto &pair : mTools )
  {
    if ( pair.first == "data:" + name )
      return pair.second;

    std::string norm = pair.first;
    for ( auto &ch : norm )
    {
      if ( ch == ':' || ch == '-' ) ch = '_';
    }
    if ( norm == name )
      return pair.second;
  }

  return std::nullopt;
}

void DataToolProvider::registerTool( const AgentTool &tool )
{
  std::lock_guard<std::mutex> lock( mMutex );
  mTools[tool.name] = tool;
}

bool DataToolProvider::unregisterTool( const std::string &name )
{
  std::lock_guard<std::mutex> lock( mMutex );
  return mTools.erase( name ) > 0;
}

} // namespace sicnu::agent::tool_catalog

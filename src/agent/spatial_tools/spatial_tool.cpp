// src/agent/spatial_tools/spatial_tool.cpp
#include "spatial_tool.h"

#include "model_catalog_tool.h"
#include "temporal_collection_tools.h"
#include "raster_inspect_tool.h"
#include "vector_inspect_tool.h"

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

bool SpatialToolRegistry::registerTool( SpatialToolPtr tool )
{
  if ( !tool || tool->name().empty() )
    return false;

  std::lock_guard<std::mutex> lock( mMutex );
  return mTools.emplace( tool->name(), std::move( tool ) ).second;
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
  };
  for ( const auto &tool : kBuiltinTools )
    registerTool( tool );
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

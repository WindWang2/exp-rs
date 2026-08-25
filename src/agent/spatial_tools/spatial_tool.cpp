// src/agent/spatial_tools/spatial_tool.cpp
#include "spatial_tool.h"

#include "model_catalog_tool.h"
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

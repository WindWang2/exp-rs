// src/agent/spatial_tools/spatial_tool_provider.cpp
#include "spatial_tool_provider.h"

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

using sicnu::agent::tool_catalog::AgentTool;
using sicnu::agent::tool_catalog::ToolCategory;

ToolCategory SpatialToolProvider::category() const
{
  return ToolCategory::Data;
}

std::vector<AgentTool> SpatialToolProvider::provideTools() const
{
  std::vector<AgentTool> tools;
  for ( const auto &spatial : SpatialToolRegistry::instance().tools() )
  {
    AgentTool tool;
    tool.name = spatial->name();
    tool.displayName = spatial->displayName();
    tool.category = ToolCategory::Data;
    tool.group = "spatial";
    tool.description = spatial->description();
    tool.tags = spatial->tags();
    tool.inputSchema = spatial->inputSchema();
    tool.outputSchema = spatial->outputSchema();

    tool.agentMetadata.purpose = spatial->description();
    tool.agentMetadata.tags = spatial->tags();

    tools.push_back( std::move( tool ) );
  }
  return tools;
}

} // namespace sicnu::agent::spatial_tools

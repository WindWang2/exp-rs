// src/agent/spatial_tools/spatial_tool_provider.cpp
#include <algorithm>
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
    // layout:* tools join the catalog through their own provider/group.
    if ( spatial->name().rfind( "spatial:", 0 ) != 0 )
      continue;

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
  // Deterministic listing order (#643): SpatialToolRegistry::tools() iterates
  // an unordered_map; sort so tools/list is stable across runs.
  std::sort( tools.begin(), tools.end(),
             []( const AgentTool &a, const AgentTool &b ) { return a.name < b.name; } );
  return tools;
}

} // namespace sicnu::agent::spatial_tools

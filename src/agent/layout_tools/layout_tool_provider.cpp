// src/agent/layout_tools/layout_tool_provider.cpp
#include <algorithm>

#include "layout_tool_provider.h"

#include "../spatial_tools/spatial_tool.h"

namespace sicnu::agent::layout_tools {

using sicnu::agent::tool_catalog::AgentTool;
using sicnu::agent::tool_catalog::ToolCategory;

ToolCategory LayoutToolProvider::category() const
{
  return ToolCategory::Interaction;
}

std::vector<AgentTool> LayoutToolProvider::provideTools() const
{
  std::vector<AgentTool> tools;
  for ( const auto &tool : sicnu::agent::spatial_tools::SpatialToolRegistry::instance().tools() )
  {
    if ( tool->name().rfind( "layout:", 0 ) != 0 )
      continue;

    AgentTool entry;
    entry.name = tool->name();
    entry.displayName = tool->displayName();
    entry.category = ToolCategory::Interaction;
    entry.group = "layout";
    entry.description = tool->description();
    entry.tags = tool->tags();
    entry.inputSchema = tool->inputSchema();
    entry.outputSchema = tool->outputSchema();
    entry.agentMetadata.purpose = tool->description();
    entry.agentMetadata.tags = tool->tags();
    tools.push_back( std::move( entry ) );
  }
  std::sort( tools.begin(), tools.end(),
             []( const AgentTool &a, const AgentTool &b ) { return a.name < b.name; } );
  return tools;
}

} // namespace sicnu::agent::layout_tools

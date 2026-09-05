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
    const bool isSpatial = ( spatial->name().rfind( "spatial:", 0 ) == 0 );
    const bool isTemporal = ( spatial->name().rfind( "temporal:", 0 ) == 0 );
    // Spatial Scientist 3.0 namespaces share the registry surface (#ADR 0128).
    const bool isCartography = ( spatial->name().rfind( "cartography:", 0 ) == 0 );
    const bool isSymbology = ( spatial->name().rfind( "symbology:", 0 ) == 0 );
    const bool isWorkflow = ( spatial->name().rfind( "workflow:", 0 ) == 0 );
    const bool isWorkspaceCommand = ( spatial->name().rfind( "workspace:", 0 ) == 0 );
    // Workspace Governance 3.0 namespaces (Platform 3.0).
    const bool isGovernance =
        ( spatial->name().rfind( "project:", 0 ) == 0 || spatial->name().rfind( "asset:", 0 ) == 0
          || spatial->name().rfind( "lineage:", 0 ) == 0 || spatial->name().rfind( "result:", 0 ) == 0
          || spatial->name().rfind( "run:", 0 ) == 0 || spatial->name().rfind( "collection:", 0 ) == 0 );
    if ( !isSpatial && !isTemporal && !isCartography && !isSymbology && !isWorkflow &&
         !isWorkspaceCommand && !isGovernance )
      continue;

    AgentTool tool;
    tool.name = spatial->name();
    tool.displayName = spatial->displayName();
    tool.category = ToolCategory::Data;
    tool.group = isTemporal    ? "temporal"
                 : isCartography ? "cartography"
                 : isSymbology   ? "symbology"
                 : isWorkflow    ? "workflow"
                 : isWorkspaceCommand ? "workspace"
                 : isGovernance      ? "governance"
                                      : "spatial";
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

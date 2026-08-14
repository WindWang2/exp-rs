// src/agent/tool_catalog/algorithm_tool_provider.cpp
#include "algorithm_tool_provider.h"
#include <algorithm>

namespace sicnu::agent::tool_catalog {

namespace {

AgentTool descriptorToAgentTool( const sicnu::processing::AlgorithmDescriptor &desc )
{
  AgentTool tool;
  tool.name = desc.id;
  tool.displayName = desc.displayName;
  tool.category = ToolCategory::Processing;
  tool.group = desc.group;
  tool.description = desc.description;
  tool.tags = desc.agentMetadata.tags;

  // If group is not in tags, add group for easier discovery
  if ( !desc.group.empty() )
  {
    if ( std::find( tool.tags.begin(), tool.tags.end(), desc.group ) == tool.tags.end() )
    {
      tool.tags.push_back( desc.group );
    }
  }

  tool.inputSchema = desc.toInputSchema();
  tool.outputSchema = desc.toOutputSchema();
  tool.inputs = desc.inputs;
  tool.outputs = desc.outputs;
  tool.agentMetadata = desc.agentMetadata;

  return tool;
}

} // namespace

std::vector<AgentTool> AlgorithmToolProvider::provideTools() const
{
  std::vector<AgentTool> result;
  const auto descriptors = sicnu::processing::AtomicAlgorithmRegistry::instance().listDescriptors();
  result.reserve( descriptors.size() );
  for ( const auto &desc : descriptors )
  {
    result.push_back( descriptorToAgentTool( desc ) );
  }
  return result;
}

std::optional<AgentTool> AlgorithmToolProvider::findTool( const std::string &name ) const
{
  auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();
  auto adapter = registry.findAdapter( name );
  if ( !adapter )
  {
    // Try resolving with underscore to colon normalization
    std::string candidate = name;
    auto pos = candidate.find( '_' );
    if ( pos != std::string::npos )
    {
      candidate[pos] = ':';
      adapter = registry.findAdapter( candidate );
    }
  }

  if ( adapter )
  {
    return descriptorToAgentTool( adapter->descriptor() );
  }

  return std::nullopt;
}

} // namespace sicnu::agent::tool_catalog

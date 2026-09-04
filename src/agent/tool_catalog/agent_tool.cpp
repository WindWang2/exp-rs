// src/agent/tool_catalog/agent_tool.cpp
#include "agent_tool.h"
#include <algorithm>
#include <stdexcept>

namespace sicnu::agent::tool_catalog {

std::string toolCategoryToString( ToolCategory category )
{
  switch ( category )
  {
    case ToolCategory::Processing:
      return "Processing";
    case ToolCategory::Interaction:
      return "Interaction";
    case ToolCategory::Data:
      return "Data";
    case ToolCategory::Custom:
      return "Custom";
  }
  return "Processing";
}

std::optional<ToolCategory> tryParseToolCategory( const std::string &catStr )
{
  std::string lower = catStr;
  std::transform( lower.begin(), lower.end(), lower.begin(),
                  []( unsigned char c ) { return std::tolower( c ); } );

  if ( lower == "processing" ) return ToolCategory::Processing;
  if ( lower == "interaction" ) return ToolCategory::Interaction;
  if ( lower == "data" ) return ToolCategory::Data;
  if ( lower == "custom" || lower == "custom_tools" ) return ToolCategory::Custom;
  return std::nullopt;
}

ToolCategory toolCategoryFromString( const std::string &catStr )
{
  auto cat = tryParseToolCategory( catStr );
  return cat.value_or( ToolCategory::Processing );
}

Json::Value AgentTool::toOpenAiToolDefinition() const
{
  Json::Value root( Json::objectValue );
  root["type"] = "function";

  Json::Value funcObj( Json::objectValue );

  // LLM function name normalized (e.g. replace ':' and '-' with '_')
  std::string normName = name;
  for ( auto &ch : normName )
  {
    if ( ch == ':' || ch == '-' ) ch = '_';
  }
  funcObj["name"] = normName;

  // Synthesize LLM function description
  std::string fullDesc;
  if ( !displayName.empty() && displayName != description )
  {
    fullDesc = displayName + ": " + description;
  }
  else
  {
    fullDesc = description.empty() ? displayName : description;
  }

  if ( !agentMetadata.purpose.empty() )
  {
    fullDesc += " 适用场景：" + agentMetadata.purpose;
  }
  if ( !agentMetadata.prerequisites.empty() )
  {
    fullDesc += " 前置条件：";
    for ( size_t i = 0; i < agentMetadata.prerequisites.size(); ++i )
    {
      if ( i > 0 ) fullDesc += "；";
      fullDesc += agentMetadata.prerequisites[i];
    }
  }

  funcObj["description"] = fullDesc;
  funcObj["parameters"] = normalizedInputSchema();
  root["function"] = funcObj;
  return root;
}

Json::Value AgentTool::toMcpToolDefinition() const
{
  Json::Value root( Json::objectValue );
  root["category"] = toolCategoryToString( category );
  root["name"] = name;
  root["description"] = description.empty() ? displayName : description;
  root["schema"] = normalizedInputSchema();
  return root;
}

Json::Value AgentTool::normalizedInputSchema() const
{
  if ( inputSchema.isNull() || ( inputSchema.isObject() && inputSchema.empty() ) )
  {
    Json::Value emptySchema( Json::objectValue );
    emptySchema["type"] = "object";
    emptySchema["properties"] = Json::Value( Json::objectValue );
    return emptySchema;
  }

  if ( !inputSchema.isObject() )
  {
    throw std::invalid_argument(
      "AgentTool '" + name + "': inputSchema root must be a JSON object" );
  }

  Json::Value normalized = inputSchema;

  if ( normalized.isMember( "type" ) )
  {
    if ( !normalized["type"].isString() || normalized["type"].asString() != "object" )
    {
      throw std::invalid_argument(
        "AgentTool '" + name + "': inputSchema root type must be 'object', but found '" +
        ( normalized["type"].isString() ? normalized["type"].asString() : "<non-string>" ) + "'" );
    }
  }
  else
  {
    normalized["type"] = "object";
  }

  if ( !normalized.isMember( "properties" ) || !normalized["properties"].isObject() )
  {
    normalized["properties"] = Json::Value( Json::objectValue );
  }

  return normalized;
}

Json::Value AgentTool::toJson() const
{
  Json::Value root( Json::objectValue );
  root["name"] = name;
  root["displayName"] = displayName;
  root["category"] = toolCategoryToString( category );
  root["group"] = group;
  root["description"] = description;

  Json::Value tagsArr( Json::arrayValue );
  for ( const auto &t : tags )
    tagsArr.append( t );
  root["tags"] = tagsArr;

  root["inputSchema"] = inputSchema;
  if ( !outputSchema.isNull() && !outputSchema.empty() )
    root["outputSchema"] = outputSchema;

  root["agentMetadata"] = agentMetadata.toJson();
  return root;
}

} // namespace sicnu::agent::tool_catalog

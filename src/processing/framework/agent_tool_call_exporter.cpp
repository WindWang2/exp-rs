// src/processing/framework/agent_tool_call_exporter.cpp
#include "agent_tool_call_exporter.h"
#include <sstream>

namespace sicnu::processing {

namespace {
// Markdown table cells break on raw '|' and newlines — escape pipe, collapse
// line breaks to spaces (#634).
std::string escapeTableCell( const std::string &text )
{
  std::string out;
  out.reserve( text.size() );
  for ( const char c : text )
  {
    switch ( c )
    {
      case '|': out += "\\|"; break;
      case '\n':
      case '\r': out += ' '; break;
      default: out += c; break;
    }
  }
  return out;
}
} // namespace

Json::Value AgentToolCallExporter::exportOpenAiToolDefinitions( const std::vector<AlgorithmDescriptor> &descriptors )
{
  Json::Value root( Json::arrayValue );
  for ( const auto &desc : descriptors )
  {
    root.append( desc.toToolCallDefinition() );
  }
  return root;
}

std::string AgentToolCallExporter::exportSystemPromptCatalog( const std::vector<AlgorithmDescriptor> &descriptors )
{
  std::stringstream ss;
  ss << "# AI Agent Remote Sensing Tool Catalog\n\n";
  ss << "| Algorithm ID | Display Name | Group | Description | Required Parameters |\n";
  ss << "|---|---|---|---|---|\n";

  for ( const auto &desc : descriptors )
  {
    std::string reqStr;
    for ( size_t i = 0; i < desc.inputs.size(); ++i )
    {
      if ( desc.inputs[i].required )
      {
        if ( !reqStr.empty() ) reqStr += ", ";
        reqStr += desc.inputs[i].name + " (" + dataTypeToString( desc.inputs[i].type ) + ")";
      }
    }
    if ( reqStr.empty() ) reqStr = "None";

    std::string cleanDesc = desc.description;
    if ( !desc.agentMetadata.purpose.empty() )
      cleanDesc += " (" + desc.agentMetadata.purpose + ")";

    ss << "| `" << escapeTableCell( desc.id ) << "` | " << escapeTableCell( desc.displayName )
       << " | " << escapeTableCell( desc.group )
       << " | " << escapeTableCell( cleanDesc ) << " | " << escapeTableCell( reqStr ) << " |\n";
  }

  return ss.str();
}

} // namespace sicnu::processing

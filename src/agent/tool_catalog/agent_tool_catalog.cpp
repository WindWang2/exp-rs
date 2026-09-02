// src/agent/tool_catalog/agent_tool_catalog.cpp
#include "agent_tool_catalog.h"

#include "../layout_tools/layout_tool_provider.h"
#include "algorithm_tool_provider.h"
#include "interaction_tool_provider.h"
#include "data_tool_provider.h"
#include "agent/interaction_tool_registry.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "agent/spatial_tools/spatial_tool_provider.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <unordered_set>

namespace sicnu::agent::tool_catalog {

namespace {

// Custom tools live in an unordered_map; flattening it directly makes tool
// listings nondeterministic across runs. Append sorted by name so catalogs,
// MCP tool arrays, and prompt exports are stable (#634).
void appendCustomToolsSorted( const std::unordered_map<std::string, AgentTool> &custom,
                              std::vector<AgentTool> &out,
                              const std::optional<ToolCategory> &category = {} )
{
  std::vector<const AgentTool *> refs;
  refs.reserve( custom.size() );
  for ( const auto &pair : custom )
  {
    if ( !category || pair.second.category == *category )
      refs.push_back( &pair.second );
  }
  std::sort( refs.begin(), refs.end(),
             []( const AgentTool *a, const AgentTool *b ) { return a->name < b->name; } );
  for ( const AgentTool *tool : refs )
    out.push_back( *tool );
}

std::string toLower( const std::string &str )
{
  std::string result = str;
  std::transform( result.begin(), result.end(), result.begin(),
                  []( unsigned char c ) { return std::tolower( c ); } );
  return result;
}

std::vector<std::string> splitTokens( const std::string &str )
{
  std::vector<std::string> tokens;
  std::string token;
  for ( char ch : str )
  {
    if ( std::isalnum( static_cast<unsigned char>( ch ) ) || ch == '_' || ch == ':' )
    {
      token.push_back( std::tolower( static_cast<unsigned char>( ch ) ) );
    }
    else if ( !token.empty() )
    {
      tokens.push_back( token );
      token.clear();
    }
  }
  if ( !token.empty() )
  {
    tokens.push_back( token );
  }
  return tokens;
}

bool containsIgnoreCase( const std::string &haystack, const std::string &needle )
{
  if ( needle.empty() ) return true;
  return toLower( haystack ).find( toLower( needle ) ) != std::string::npos;
}

bool matchesPortType( const std::vector<sicnu::processing::PortDescriptor> &ports,
                      const std::string &typeStr )
{
  if ( typeStr.empty() ) return true;
  const std::string lowerType = toLower( typeStr );

  for ( const auto &port : ports )
  {
    std::string portTypeStr = toLower( sicnu::processing::dataTypeToString( port.type ) );
    if ( portTypeStr == lowerType || containsIgnoreCase( portTypeStr, lowerType ) )
      return true;
    if ( containsIgnoreCase( port.name, lowerType ) || containsIgnoreCase( port.displayName, lowerType ) )
      return true;
  }
  return false;
}

int computeMatchScore( const AgentTool &tool, const std::string &rawQuery, const std::vector<std::string> &tokens )
{
  if ( rawQuery.empty() ) return 1;

  int score = 0;
  const std::string lowerQuery = toLower( rawQuery );
  const std::string lowerName = toLower( tool.name );
  const std::string lowerDisplayName = toLower( tool.displayName );
  const std::string lowerDesc = toLower( tool.description );
  const std::string lowerPurpose = toLower( tool.agentMetadata.purpose );
  const std::string lowerGroup = toLower( tool.group );

  // Full query exact matches
  if ( lowerName == lowerQuery ) score += 200;
  else if ( lowerName.find( lowerQuery ) != std::string::npos ) score += 80;

  if ( lowerDisplayName.find( lowerQuery ) != std::string::npos ) score += 70;

  // Check tags for full query
  for ( const auto &t : tool.tags )
  {
    const std::string lowerTag = toLower( t );
    if ( lowerTag == lowerQuery ) score += 100;
    else if ( lowerTag.find( lowerQuery ) != std::string::npos || lowerQuery.find( lowerTag ) != std::string::npos )
      score += 50;
  }

  if ( lowerDesc.find( lowerQuery ) != std::string::npos ) score += 40;
  if ( lowerPurpose.find( lowerQuery ) != std::string::npos ) score += 45;
  if ( lowerGroup.find( lowerQuery ) != std::string::npos ) score += 30;

  // Token level scoring
  int matchedTokens = 0;
  for ( const auto &token : tokens )
  {
    bool tokenMatched = false;
    if ( lowerName.find( token ) != std::string::npos ) { score += 25; tokenMatched = true; }
    if ( lowerDisplayName.find( token ) != std::string::npos ) { score += 20; tokenMatched = true; }
    if ( lowerGroup.find( token ) != std::string::npos ) { score += 15; tokenMatched = true; }
    if ( lowerDesc.find( token ) != std::string::npos ) { score += 10; tokenMatched = true; }
    if ( lowerPurpose.find( token ) != std::string::npos ) { score += 15; tokenMatched = true; }

    for ( const auto &t : tool.tags )
    {
      std::string lowerTag = toLower( t );
      if ( lowerTag == token ) { score += 35; tokenMatched = true; }
      else if ( lowerTag.find( token ) != std::string::npos ) { score += 20; tokenMatched = true; }
    }

    if ( tokenMatched ) matchedTokens++;
  }

  // If query contains multiple tokens, boost tools that match all tokens
  if ( !tokens.empty() && matchedTokens == static_cast<int>( tokens.size() ) )
  {
    score += 50;
  }

  return score;
}

} // namespace

AgentToolCatalog &AgentToolCatalog::instance()
{
  static AgentToolCatalog sInstance;
  return sInstance;
}

AgentToolCatalog::AgentToolCatalog()
{
  initializeDefaults();
}

void AgentToolCatalog::invalidateCache()
{
  std::lock_guard<std::mutex> lock( mMutex );
  mCacheValid = false;
  mCachedTools.clear();
  mCachedOpenAiDefs.clear();
  mCachedMcpTools.clear();
}

void AgentToolCatalog::initializeDefaults()
{
  std::lock_guard<std::mutex> lock( mMutex );
  mProviders.clear();
  mCustomTools.clear();

  mProviders.push_back( std::make_shared<AlgorithmToolProvider>() );
  mProviders.push_back( std::make_shared<InteractionToolProvider>() );
  mProviders.push_back( std::make_shared<DataToolProvider>() );

  // ADR 0122: spatial inspection/catalog tools (spatial:*) join the unified
  // catalog as descriptors; execution stays owned by SpatialToolRegistry
  // (its own mutex — no interaction with mMutex).
  sicnu::agent::spatial_tools::SpatialToolRegistry::instance().registerBuiltinTools();
  mProviders.push_back( std::make_shared<sicnu::agent::spatial_tools::SpatialToolProvider>() );
  // Cartographic layout tools (layout:*) join under their own provider/group.
  mProviders.push_back( std::make_shared<sicnu::agent::layout_tools::LayoutToolProvider>() );

  mCacheValid = false;
  mCachedTools.clear();
  mCachedOpenAiDefs.clear();
  mCachedMcpTools.clear();
}

void AgentToolCatalog::reset()
{
  initializeDefaults();
}

void AgentToolCatalog::registerProvider( ToolProviderPtr provider )
{
  if ( !provider ) return;
  std::lock_guard<std::mutex> lock( mMutex );
  mCacheValid = false;
  mCachedTools.clear();
  mCachedOpenAiDefs = Json::Value();
  mCachedMcpTools = Json::Value();

  for ( auto it = mProviders.begin(); it != mProviders.end(); ++it )
  {
    if ( ( *it )->providerName() == provider->providerName() )
    {
      *it = provider;
      return;
    }
  }
  mProviders.push_back( provider );
}

bool AgentToolCatalog::unregisterProvider( const std::string &providerName )
{
  std::lock_guard<std::mutex> lock( mMutex );
  mCacheValid = false;
  mCachedTools.clear();
  mCachedOpenAiDefs = Json::Value();
  mCachedMcpTools = Json::Value();
  for ( auto it = mProviders.begin(); it != mProviders.end(); ++it )
  {
    if ( ( *it )->providerName() == providerName )
    {
      mProviders.erase( it );
      return true;
    }
  }
  return false;
}

std::vector<ToolProviderPtr> AgentToolCatalog::providers() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  return mProviders;
}

ToolProviderPtr AgentToolCatalog::provider( const std::string &providerName ) const
{
  std::lock_guard<std::mutex> lock( mMutex );
  for ( const auto &p : mProviders )
  {
    if ( p->providerName() == providerName )
      return p;
  }
  return nullptr;
}

void AgentToolCatalog::registerCustomTool( const AgentTool &tool )
{
  std::lock_guard<std::mutex> lock( mMutex );
  mCacheValid = false;
  mCachedTools.clear();
  mCachedOpenAiDefs = Json::Value();
  mCachedMcpTools = Json::Value();
  mCustomTools[tool.name] = tool;
}

bool AgentToolCatalog::unregisterCustomTool( const std::string &toolName )
{
  std::lock_guard<std::mutex> lock( mMutex );
  mCacheValid = false;
  mCachedTools.clear();
  mCachedOpenAiDefs = Json::Value();
  mCachedMcpTools = Json::Value();
  return mCustomTools.erase( toolName ) > 0;
}

bool AgentToolCatalog::cacheIsFresh() const
{
  if ( !mCacheValid )
    return false;
  // #701: the interaction tool set can change after the cache was built
  // (registerBuiltinTools/registerDataTools run late in some entry points).
  // The registry's monotonic revision counter detects that cheaply — without
  // it, tools registered after the first catalog use stayed invisible until
  // an explicit reset().
  return mCachedInteractionRevision
         == sicnu::agent::InteractionToolRegistry::instance().revision();
}

std::vector<AgentTool> AgentToolCatalog::listTools( std::optional<ToolCategory> category ) const
{
  std::lock_guard<std::mutex> lock( mMutex );
  if ( !category && cacheIsFresh() )
  {
    return mCachedTools;
  }

  std::vector<AgentTool> result;

  for ( const auto &prov : mProviders )
  {
    auto tools = prov->provideTools();
    for ( auto &tool : tools )
    {
      if ( !category || tool.category == *category )
      {
        result.push_back( std::move( tool ) );
      }
    }
  }

  appendCustomToolsSorted( mCustomTools, result, category );

  if ( !category )
  {
    mCachedTools = result;
    mCachedInteractionRevision =
      sicnu::agent::InteractionToolRegistry::instance().revision();
    mCacheValid = true;
    // The exported snapshots hold a flattened view of mCachedTools; a
    // registry tool added since the last export stays invisible in the
    // cached export until something invalidates mCacheValid. Clear the
    // snapshots whenever the cache is rebuilt so the next export reflects
    // every live tool (#701).
    mCachedOpenAiDefs.clear();
    mCachedMcpTools.clear();
  }

  return result;
}

std::optional<AgentTool> AgentToolCatalog::findTool( const std::string &name ) const
{
  std::lock_guard<std::mutex> lock( mMutex );

  auto customIt = mCustomTools.find( name );
  if ( customIt != mCustomTools.end() )
    return customIt->second;

  for ( const auto &prov : mProviders )
  {
    auto tool = prov->findTool( name );
    if ( tool ) return tool;
  }

  // Normalized fallback (e.g. replace '_' with ':')
  std::string candidate = name;
  auto pos = candidate.find( '_' );
  if ( pos != std::string::npos )
  {
    candidate[pos] = ':';
    for ( const auto &prov : mProviders )
    {
      auto tool = prov->findTool( candidate );
      if ( tool ) return tool;
    }
  }

  return std::nullopt;
}

Json::Value AgentToolCatalog::getSchema( const std::string &toolName ) const
{
  auto tool = findTool( toolName );
  if ( tool )
  {
    return tool->inputSchema;
  }
  return Json::Value( Json::nullValue );
}

size_t AgentToolCatalog::toolCount( std::optional<ToolCategory> category ) const
{
  return listTools( category ).size();
}

std::vector<AgentTool> AgentToolCatalog::searchTools( const std::string &queryText ) const
{
  SearchQuery query;
  query.text = queryText;
  return searchTools( query );
}

std::vector<AgentTool> AgentToolCatalog::searchTools( const SearchQuery &query ) const
{
  const auto allTools = listTools( query.category );
  const auto tokens = splitTokens( query.text );

  struct ScoredTool {
    AgentTool tool;
    int score = 0;
  };

  std::vector<ScoredTool> scored;
  scored.reserve( allTools.size() );

  for ( const auto &tool : allTools )
  {
    // Filter by group: substring case-insensitive (description says "Exact or substring")
    if ( !query.group.empty() && !containsIgnoreCase( tool.group, query.group ) )
      continue;

    // Filter by tag
    if ( !query.tag.empty() )
    {
      bool tagFound = false;
      for ( const auto &t : tool.tags )
      {
        if ( containsIgnoreCase( t, query.tag ) )
        {
          tagFound = true;
          break;
        }
      }
      if ( !tagFound ) continue;
    }

    // Filter by input type
    if ( !query.inputType.empty() && !matchesPortType( tool.inputs, query.inputType ) )
    {
      // Also check inputSchema properties
      bool schemaMatch = false;
      if ( tool.inputSchema.isObject() && tool.inputSchema.isMember( "properties" ) )
      {
        const auto &props = tool.inputSchema["properties"];
        for ( const auto &propName : props.getMemberNames() )
        {
          if ( containsIgnoreCase( propName, query.inputType ) )
          {
            schemaMatch = true;
            break;
          }
          if ( props[propName].isMember( "type" ) &&
               containsIgnoreCase( props[propName]["type"].asString(), query.inputType ) )
          {
            schemaMatch = true;
            break;
          }
        }
      }
      if ( !schemaMatch ) continue;
    }

    // Filter by output type
    if ( !query.outputType.empty() && !matchesPortType( tool.outputs, query.outputType ) )
      continue;

    // Filter large raster safety
    if ( query.largeRasterSafeOnly )
    {
      const bool safe = tool.agentMetadata.largeRasterSafe ||
                        tool.agentMetadata.memoryPolicy == "streaming" ||
                        tool.agentMetadata.memoryPolicy == "multipass_streaming";
      if ( !safe ) continue;
    }

    int score = computeMatchScore( tool, query.text, tokens );
    if ( score > 0 )
    {
      scored.push_back( { tool, score } );
    }
  }

  // Sort descending by score, tiebreak by tool name
  std::stable_sort( scored.begin(), scored.end(), []( const ScoredTool &a, const ScoredTool &b ) {
    if ( a.score != b.score ) return a.score > b.score;
    return a.tool.name < b.tool.name;
  } );

  std::vector<AgentTool> results;
  results.reserve( scored.size() );
  for ( auto &item : scored )
  {
    results.push_back( std::move( item.tool ) );
  }

  return results;
}

std::vector<std::string> AgentToolCatalog::findDuplicateNames() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  std::map<std::string, int> counts;

  for ( const auto &prov : mProviders )
  {
    for ( const auto &tool : prov->provideTools() )
    {
      counts[tool.name]++;
    }
  }

  for ( const auto &pair : mCustomTools )
  {
    counts[pair.second.name]++;
  }

  std::vector<std::string> duplicates;
  for ( const auto &pair : counts )
  {
    if ( pair.second > 1 )
    {
      duplicates.push_back( pair.first );
    }
  }
  return duplicates;
}

bool AgentToolCatalog::hasDuplicates() const
{
  return !findDuplicateNames().empty();
}

Json::Value AgentToolCatalog::exportOpenAiToolDefinitions( const std::vector<AgentTool> &tools ) const
{
  if ( !tools.empty() )
  {
    Json::Value root( Json::arrayValue );
    for ( const auto &tool : tools )
    {
      root.append( tool.toOpenAiToolDefinition() );
    }
    return root;
  }

  std::lock_guard<std::mutex> lock( mMutex );
  if ( cacheIsFresh() && !mCachedOpenAiDefs.isNull() && !mCachedOpenAiDefs.empty() )
  {
    return mCachedOpenAiDefs;
  }

  if ( !cacheIsFresh() )
  {
    mCachedTools.clear();
    for ( const auto &prov : mProviders )
    {
      auto provTools = prov->provideTools();
      for ( auto &tool : provTools )
      {
        mCachedTools.push_back( std::move( tool ) );
      }
    }
    appendCustomToolsSorted( mCustomTools, mCachedTools );
    mCachedInteractionRevision =
      sicnu::agent::InteractionToolRegistry::instance().revision();
    mCacheValid = true;
  }

  Json::Value root( Json::arrayValue );
  for ( const auto &tool : mCachedTools )
  {
    root.append( tool.toOpenAiToolDefinition() );
  }
  mCachedOpenAiDefs = root;
  return root;
}

Json::Value AgentToolCatalog::exportMcpTools( const std::vector<AgentTool> &tools ) const
{
  if ( !tools.empty() )
  {
    Json::Value root( Json::arrayValue );
    for ( const auto &tool : tools )
    {
      root.append( tool.toMcpToolDefinition() );
    }
    return root;
  }

  std::lock_guard<std::mutex> lock( mMutex );
  if ( cacheIsFresh() && !mCachedMcpTools.isNull() && !mCachedMcpTools.empty() )
  {
    return mCachedMcpTools;
  }

  if ( !cacheIsFresh() )
  {
    mCachedTools.clear();
    for ( const auto &prov : mProviders )
    {
      auto provTools = prov->provideTools();
      for ( auto &tool : provTools )
      {
        mCachedTools.push_back( std::move( tool ) );
      }
    }
    appendCustomToolsSorted( mCustomTools, mCachedTools );
    mCachedInteractionRevision =
      sicnu::agent::InteractionToolRegistry::instance().revision();
    mCacheValid = true;
  }

  Json::Value root( Json::arrayValue );
  for ( const auto &tool : mCachedTools )
  {
    root.append( tool.toMcpToolDefinition() );
  }
  mCachedMcpTools = root;
  return root;
}

std::string AgentToolCatalog::exportSystemPromptCatalog( const std::vector<AgentTool> &tools ) const
{
  const auto toolList = tools.empty() ? listTools() : tools;
  std::stringstream ss;
  ss << "# AI Agent Unified Tool Catalog\n\n";

  // Group tools by Category
  for ( ToolCategory cat : { ToolCategory::Processing, ToolCategory::Interaction, ToolCategory::Data, ToolCategory::Custom } )
  {
    std::vector<AgentTool> catTools;
    for ( const auto &t : toolList )
    {
      if ( t.category == cat ) catTools.push_back( t );
    }

    if ( catTools.empty() ) continue;

    ss << "## " << toolCategoryToString( cat ) << " Tools (" << catTools.size() << ")\n\n";
    ss << "| Tool ID | Display Name | Group | Description | Required Parameters |\n";
    ss << "|---|---|---|---|---|\n";

    for ( const auto &tool : catTools )
    {
      std::string reqStr;
      if ( tool.inputSchema.isObject() && tool.inputSchema.isMember( "required" ) &&
           tool.inputSchema["required"].isArray() )
      {
        const auto &reqArr = tool.inputSchema["required"];
        for ( Json::ArrayIndex i = 0; i < reqArr.size(); ++i )
        {
          if ( i > 0 ) reqStr += ", ";
          reqStr += reqArr[i].asString();
        }
      }
      if ( reqStr.empty() ) reqStr = "None";

      std::string desc = tool.description;
      if ( !tool.agentMetadata.purpose.empty() && tool.agentMetadata.purpose != tool.description )
        desc += " (" + tool.agentMetadata.purpose + ")";

      ss << "| `" << tool.name << "` | " << tool.displayName << " | " << tool.group
         << " | " << desc << " | " << reqStr << " |\n";
    }
    ss << "\n";
  }

  return ss.str();
}

} // namespace sicnu::agent::tool_catalog

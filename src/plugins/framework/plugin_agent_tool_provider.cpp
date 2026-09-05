/***************************************************************************
 * src/plugins/framework/plugin_agent_tool_provider.cpp
 ***************************************************************************/
#include "plugin_agent_tool_provider.h"

namespace sicnu::plugins {

namespace {
std::vector<std::string> defaultTagsFor( const exprs::ManifestAgentTool &tool )
{
    std::vector<std::string> tags = { "plugin" };
    if ( !tool.category.empty() )
        tags.push_back( tool.category );
    return tags;
}
} // namespace

std::mutex &PluginAgentToolProvider::mutex()
{
    static std::mutex instance;
    return instance;
}

std::map<std::string, std::shared_ptr<exprs::IPluginAgentToolV1>> &
PluginAgentToolProvider::executors()
{
    static std::map<std::string, std::shared_ptr<exprs::IPluginAgentToolV1>> instance;
    return instance;
}

std::map<std::string, std::string> &PluginAgentToolProvider::executorOwners()
{
    static std::map<std::string, std::string> instance;
    return instance;
}

PluginAgentToolProvider::PluginAgentToolProvider( std::string pluginId,
                                                  std::vector<exprs::ManifestAgentTool> tools )
    : mPluginId( std::move( pluginId ) )
    , mTools( std::move( tools ) )
{
}

sicnu::agent::tool_catalog::AgentTool PluginAgentToolProvider::toAgentTool(
    const exprs::ManifestAgentTool &tool )
{
    sicnu::agent::tool_catalog::AgentTool agentTool;
    agentTool.name = tool.id;
    agentTool.displayName = tool.displayName;
    agentTool.category = sicnu::agent::tool_catalog::ToolCategory::Custom;
    agentTool.group = "plugin";
    agentTool.description = tool.description;
    agentTool.tags = defaultTagsFor( tool );
    agentTool.inputSchema = tool.inputSchema;
    agentTool.outputSchema = tool.outputSchema;
    return agentTool;
}

std::vector<sicnu::agent::tool_catalog::AgentTool> PluginAgentToolProvider::provideTools() const
{
    std::vector<sicnu::agent::tool_catalog::AgentTool> tools;
    tools.reserve( mTools.size() );
    for ( const exprs::ManifestAgentTool &tool : mTools )
        tools.push_back( toAgentTool( tool ) );
    return tools;
}

std::optional<sicnu::agent::tool_catalog::AgentTool> PluginAgentToolProvider::findTool(
    const std::string &name ) const
{
    for ( const exprs::ManifestAgentTool &tool : mTools )
    {
        if ( tool.id == name )
            return toAgentTool( tool );
    }
    return std::nullopt;
}

void PluginAgentToolProvider::registerExecutor(
    const std::string &pluginId, const std::string &toolId,
    std::shared_ptr<exprs::IPluginAgentToolV1> tool )
{
    std::lock_guard<std::mutex> lock( mutex() );
    executors()[toolId] = std::move( tool );
    executorOwners()[toolId] = pluginId;
}

void PluginAgentToolProvider::unregisterPluginExecutors( const std::string &pluginId )
{
    std::lock_guard<std::mutex> lock( mutex() );
    for ( auto iterator = executors().begin(); iterator != executors().end(); )
    {
        const auto owner = executorOwners().find( iterator->first );
        if ( owner != executorOwners().end() && owner->second == pluginId )
        {
            iterator = executors().erase( iterator );
            executorOwners().erase( owner );
        }
        else
        {
            ++iterator;
        }
    }
}

bool PluginAgentToolProvider::hasExecutor( const std::string &toolId )
{
    std::lock_guard<std::mutex> lock( mutex() );
    return executors().count( toolId ) > 0;
}

Json::Value PluginAgentToolProvider::execute( const std::string &toolId,
                                              const Json::Value &params )
{
    std::shared_ptr<exprs::IPluginAgentToolV1> tool;
    {
        std::lock_guard<std::mutex> lock( mutex() );
        auto iterator = executors().find( toolId );
        if ( iterator == executors().end() )
        {
            Json::Value failure( Json::objectValue );
            failure["success"] = false;
            Json::Value error( Json::objectValue );
            error["message"] = "plugin agent tool '" + toolId
                               + "' has no executor (plugin not loaded or failed)";
            error["code"] = "NOT_LOADED";
            error["category"] = "runtime";
            error["retryable"] = false;
            failure["error"] = error;
            return failure;
        }
        tool = iterator->second;
    }
    try
    {
        return tool->execute( params );
    }
    catch ( const std::exception &exception )
    {
        Json::Value failure( Json::objectValue );
        failure["success"] = false;
        Json::Value error( Json::objectValue );
        error["message"] = std::string( "plugin tool threw: " ) + exception.what();
        error["code"] = "PLUGIN_EXCEPTION";
        error["category"] = "runtime";
        error["retryable"] = false;
        failure["error"] = error;
        return failure;
    }
}

} // namespace sicnu::plugins

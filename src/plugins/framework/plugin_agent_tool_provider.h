/***************************************************************************
 * src/plugins/framework/plugin_agent_tool_provider.h
 *
 * Bridges manifest-declared plugin agent tools into the unified
 * AgentToolCatalog. Descriptors come from the manifest (no dlopen at
 * tools/list time); executors are resolved at execute time from the
 * registry populated by PluginRuntimeHost::registerAgentTool.
 ***************************************************************************/
#pragma once

#include "agent/tool_catalog/agent_tool.h"
#include "agent/tool_catalog/tool_provider.h"
#include "exprs/plugin_interface.h"
#include "exprs/plugin_manifest.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sicnu::plugins {

class PluginAgentToolProvider : public sicnu::agent::tool_catalog::ToolProvider
{
public:
    PluginAgentToolProvider( std::string pluginId,
                             std::vector<exprs::ManifestAgentTool> tools );

    std::string providerName() const override
    {
        return "PluginToolProvider:" + mPluginId;
    }
    sicnu::agent::tool_catalog::ToolCategory category() const override
    {
        return sicnu::agent::tool_catalog::ToolCategory::Custom;
    }
    std::vector<sicnu::agent::tool_catalog::AgentTool> provideTools() const override;
    std::optional<sicnu::agent::tool_catalog::AgentTool> findTool(
        const std::string &name ) const override;

    /// Executor store shared by all providers (filled by the sink).
    static void registerExecutor( const std::string &pluginId, const std::string &toolId,
                                  std::shared_ptr<exprs::IPluginAgentToolV1> tool );
    static void unregisterPluginExecutors( const std::string &pluginId );
    /// Executes a plugin tool; returns nullopt-style failure JSON when the
    /// executor is not available (plugin not loaded).
    static Json::Value execute( const std::string &toolId, const Json::Value &params );
    static bool hasExecutor( const std::string &toolId );

private:
    static sicnu::agent::tool_catalog::AgentTool toAgentTool( const exprs::ManifestAgentTool &tool );

    std::string mPluginId;
    std::vector<exprs::ManifestAgentTool> mTools;

    static std::mutex &mutex();
    static std::map<std::string, std::shared_ptr<exprs::IPluginAgentToolV1>> &executors();
    static std::map<std::string, std::string> &executorOwners();
};

} // namespace sicnu::plugins

/***************************************************************************
 * src/plugins/framework/plugin_runtime_host.h
 *
 * The host-side bridge between the plugin infrastructure (sicnu_sdk) and
 * the application registries. One PluginRuntimeHost::bootstrap() call in
 * the CLI/GUI startup path:
 *
 *   1. configures PluginRegistry (roots, policy, services)
 *   2. installs this host as the PluginContributionSink
 *   3. refreshes discovery (manifest parse + validation + policy only)
 *   4. installs LAZY contributions for every validated plugin:
 *        - operator contributions  -> AtomicAlgorithmRegistry (descriptor
 *                                     from manifest; binary loaded on first
 *                                     execute) + RSOperatorRegistry factory
 *        - external tool operators -> ExternalToolOperator instances
 *        - agent tools             -> PluginAgentToolProvider (schema from
 *                                     manifest; executor loaded on demand)
 *        - model runtimes          -> ModelRuntimeRegistry (OpenCV bridge)
 *        - data providers          -> DataProviderRegistry (on plugin load)
 *
 * Startup cost is proportional to the number of plugin.json files, not the
 * number of plugin binaries: no dlopen happens during bootstrap.
 ***************************************************************************/
#pragma once

#include "exprs/plugin_registry.h"

#include <map>
#include <mutex>

namespace sicnu::plugins {

class PluginRuntimeHost : public exprs::PluginContributionSink
{
public:
    static PluginRuntimeHost &instance();

    /// Configures and boots the plugin subsystem. @p options.roots default
    /// to PluginDiscovery::defaultRoots. Safe to call from GUI and CLI.
    void bootstrap( const exprs::PluginRegistryOptions &options );

    /// Installs lazy contributions for every validated plugin record.
    /// Called by bootstrap(); safe to call again after refresh().
    void installManifestContributions();

    /// Removes every contribution registered for @p pluginId (after unload
    /// or a failed state transition).
    void revokePluginContributions( const std::string &pluginId );

    /// True when @p operatorId belongs to a manifest-declared plugin
    /// operator (validated record present).
    bool isPluginOperator( const std::string &operatorId ) const;

    /// Manifest declaration for a plugin operator (nullptr when unknown).
    const exprs::ManifestOperator *manifestOperator( const std::string &operatorId ) const;

    /// Current factory for @p operatorId (empty when the plugin has not
    /// registered it yet). The lazy adapter resolves through this so a
    /// binary plugin's factory installed at load time is honoured.
    std::function<std::unique_ptr<sicnu::operators::RSOperator>()>
    resolveOperatorFactory( const std::string &operatorId ) const;

    // -- PluginContributionSink ---------------------------------------------
    bool registerOperatorFactory(
        const std::string &pluginId, const std::string &operatorId,
        std::function<std::unique_ptr<sicnu::operators::RSOperator>()> factory ) override;
    bool registerDataProvider( const std::string &pluginId, const std::string &providerId,
                               std::shared_ptr<exprs::IPluginDataProviderV1> provider ) override;
    bool registerModelRuntime( const std::string &pluginId, const std::string &framework,
                               exprs::PluginModelRuntimeFactoryV1 factory ) override;
    bool registerAgentTool( const std::string &pluginId, const std::string &toolId,
                            std::shared_ptr<exprs::IPluginAgentToolV1> tool ) override;
    void revokePlugin( const std::string &pluginId ) override;

private:
    PluginRuntimeHost() = default;

    struct OperatorEntry
    {
        std::string pluginId;
        exprs::ManifestOperator manifest;   ///< empty for binary-registered operators
        bool isExternalTool = false;
        std::function<std::unique_ptr<sicnu::operators::RSOperator>()> factory;
    };

    void installPluginOperator( const std::string &pluginId, const exprs::ManifestOperator &op );
    void installPluginAgentTools( const exprs::PluginRecord &record );
    void installPluginModelRuntimes( const exprs::PluginRecord &record );

    mutable std::mutex mMutex;
    std::map<std::string, OperatorEntry> mOperators;      ///< operatorId -> entry
    std::map<std::string, exprs::PluginModelRuntimeFactoryV1> mModelRuntimeFactories;
    std::vector<std::string> mRegisteredAgentToolIds;
    bool mBootstrapped = false;
};

/// Boot convenience: PluginRuntimeHost::instance().bootstrap(options).
void bootstrapPluginRuntime( const exprs::PluginRegistryOptions &options );

} // namespace sicnu::plugins

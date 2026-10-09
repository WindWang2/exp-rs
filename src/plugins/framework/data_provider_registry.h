/***************************************************************************
 * src/plugins/framework/data_provider_registry.h
 *
 * Process-wide registry of plugin data providers (Phase O). Providers are
 * stored here by the PluginRuntimeHost when a plugin registers them; the
 * CLI, GUI and agent data tools query it by id or URI scheme. This registry
 * intentionally stays separate from the QGIS provider registry — plugin
 * providers are higher-level reference/metadata services, not QgsDataProvider
 * implementations.
 ***************************************************************************/
#pragma once

#include "exprs/plugin_interface.h"

#include <mutex>
#include <string>
#include <vector>

namespace sicnu::plugins {

class DataProviderRegistry
{
public:
    struct Entry
    {
        std::string pluginId;
        std::string providerId;
        std::string displayName;
        std::string description;
        std::vector<std::string> schemes;
        std::shared_ptr<exprs::IPluginDataProviderV1> provider;
    };

    static DataProviderRegistry &instance();

    /// Registers (or replaces) a provider. Returns false when providerId is
    /// empty or provider is null.
    bool registerProvider( const std::string &pluginId, const std::string &providerId,
                           const std::string &displayName, const std::string &description,
                           const std::vector<std::string> &schemes,
                           std::shared_ptr<exprs::IPluginDataProviderV1> provider );

    /// Unregisters every provider contributed by @p pluginId (unload path).
    void unregisterPlugin( const std::string &pluginId );

    /// Finds a registered provider by id. The returned handle CO-OWNS the
    /// provider: a raw pointer into the entry storage would dangle as soon
    /// as this method releases its lock and a concurrent
    /// unregisterPlugin()/re-registration erases or reallocates mEntries
    /// (#1449). The handle stays callable even after the plugin is gone.
    std::shared_ptr<exprs::IPluginDataProviderV1> find(
        const std::string &providerId ) const;
    std::vector<Entry> providers() const;
    size_t count() const;

private:
    DataProviderRegistry() = default;
    mutable std::mutex mMutex;
    std::vector<Entry> mEntries;
};

} // namespace sicnu::plugins

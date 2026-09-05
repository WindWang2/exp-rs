/***************************************************************************
 * exprs/plugin_interface.h — ExpRS native plugin ABI (interface version 1)
 *
 * Contract for native plugins:
 *
 *   - The plugin shared library exports exactly one well-known symbol:
 *
 *       extern "C" exprs::PluginV1* EXPRS_createPluginV1();
 *
 *     The host resolves this symbol by name; the V1 suffix pins the entry
 *     point signature. A future incompatible V2 entry point would be a new
 *     symbol, never a change to this one.
 *
 *   - The host checks manifest api_version/abi_version BEFORE dlopen, so an
 *     incompatible plugin fails with a diagnostic, not a crash.
 *
 *   - ABI rules for interface classes (PluginV1 and the *V1 interfaces it
 *     hands out): never reorder or remove virtuals within a V1 interface;
 *     additions go at the END and must have a default implementation.
 *     Implementations should use `override` and avoid relying on the
 *     concrete size of host-side objects.
 *
 *   - Pinned cross-ABI types: Json::Value (jsoncpp), std::string,
 *     std::vector, std::function. Qt types are NOT used in this header —
 *     they are confined to exprs/plugin_ui.h which is build-locked to
 *     the host.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "exprs/version.h"

namespace exprs {

/// Handled URI scheme / store identity contributed by a data provider plugin.
struct DataProviderInfoV1
{
    std::string id;
    std::string displayName;
    std::string description;
    std::vector<std::string> schemes;
    Json::Value capabilities;
};

/// Loaded model request handed to a plugin model runtime. Decoupled from
/// the host's ModelInfo so plugin builds need no OpenCV/QGIS headers.
struct PluginModelRequestV1
{
    std::string modelName;
    std::string artifactPath;
    Json::Value manifest;    ///< full model.json content
    bool gpuRequested = false;
};

/// Plain float tensor exchanged with plugin model runtimes (NCHW layout).
struct PluginTensorV1
{
    std::vector<float> data;
    int batch = 1;
    int channels = 1;
    int rows = 0;
    int cols = 0;
};

/// Result of a plugin model runtime inference step.
struct PluginInferenceResultV1
{
    bool success = false;
    std::string error;
    PluginTensorV1 output;
    Json::Value diagnostics;
};

/// Model runtime interface implemented by plugin model runtime backends.
class IPluginModelRuntimeV1
{
public:
    virtual ~IPluginModelRuntimeV1() = default;
    virtual std::string backendName() const = 0;
    virtual std::string deviceName() const = 0;
    /// Loads the model artifact. Returns false with @p error set on failure.
    virtual bool load( const PluginModelRequestV1 &request, std::string &error ) = 0;
    virtual PluginInferenceResultV1 infer( const PluginTensorV1 &input,
                                           const std::string &outputTensorName ) = 0;
    virtual std::vector<std::string> outputTensorNames() const = 0;
};

using PluginModelRuntimePtrV1 = std::unique_ptr<IPluginModelRuntimeV1>;

/// Factory: creates a runtime instance for a model request. Returns nullptr
/// with @p error set when the request cannot be served.
using PluginModelRuntimeFactoryV1 =
    std::function<PluginModelRuntimePtrV1( const PluginModelRequestV1 &request, std::string &error )>;

/// Data provider plugin interface (custom stores / remote APIs / catalogs).
class IPluginDataProviderV1
{
public:
    virtual ~IPluginDataProviderV1() = default;

    /// Enumerate items. @p query is provider-specific JSON
    /// ({"uri": ..., "filter": {...}, "page": {...}}). Returns a JSON array
    /// of item descriptors (bounded: respect query["page"]).
    virtual Json::Value discover( const Json::Value &query ) = 0;

    /// Metadata for one resource URI (no payload transfer).
    virtual Json::Value inspect( const std::string &uri ) = 0;

    /**
     * Opens a resource and returns a host-consumable reference:
     *   {"kind": "raster"|"vector",
     *    "path": "<GDAL/OGR-readable path or /vsi/ URL>",
     *    "metadata": {...}}
     * Providers may materialize a local cache copy (with the
     * filesystem_write permission) or hand out a direct URL.
     */
    virtual Json::Value open( const std::string &uri ) = 0;

    /// Static capability description (maxPageSize, auth kinds, ...).
    virtual Json::Value capabilities() const = 0;
};

/// Executor for one declared agent tool. Results follow the SpatialTool
/// envelope: {"success": bool, "result": ...} or
/// {"success": false, "error": {"message", "code", "category", "retryable"}}.
class IPluginAgentToolV1
{
public:
    virtual ~IPluginAgentToolV1() = default;
    virtual Json::Value execute( const Json::Value &params ) = 0;
};

/// Host services handed to a plugin at initialize() time. Pure abstract;
/// versioned like everything else in this header.
class HostServicesV1
{
public:
    virtual ~HostServicesV1() = default;

    /// Directory the plugin may use for temporary artifacts (writable).
    virtual std::string tempDirectory() const = 0;
    /// Read-only workspace root when one is configured ("" otherwise).
    virtual std::string workspaceRoot() const = 0;
    /// Installed ExpRS data directory (share/exp-rs).
    virtual std::string dataDirectory() const = 0;
    /// Structured host log sink (level: "info"|"warning"|"error").
    virtual void log( const char *level, const std::string &message ) = 0;

    /// Absolute directory of the plugin being initialized (default "").
    /// Appended in V1.0 with a default implementation, so older plugins
    /// that predate it remain source/ABI compatible.
    virtual std::string pluginDirectory() const { return {}; }
};

/// Registration context passed to registerContributions(). A plugin may
/// register fewer things than its manifest declares; manifest-declared
/// entries without a matching registration are diagnosed at load time.
/// (Pure-manifest external tool operators never reach this interface.)
class ContributionContextV1
{
public:
    virtual ~ContributionContextV1() = default;

    /// Registers an operator factory. The id must match a manifest
    /// "operators" entry. Re-registering an id is a registration failure.
    virtual bool registerOperatorFactory(
        const std::string &operatorId,
        std::function<std::unique_ptr<sicnu::operators::RSOperator>()> factory ) = 0;

    /// Registers a data provider implementation for a manifest
    /// "data_providers" entry.
    virtual bool registerDataProvider( const std::string &providerId,
                                       std::shared_ptr<IPluginDataProviderV1> provider ) = 0;

    /// Registers a model runtime backend for a manifest "model_runtimes"
    /// framework key.
    virtual bool registerModelRuntime( const std::string &framework,
                                       PluginModelRuntimeFactoryV1 factory ) = 0;

    /// Registers the executor for a manifest "agent_tools" entry.
    virtual bool registerAgentTool( const std::string &toolId,
                                    std::shared_ptr<IPluginAgentToolV1> tool ) = 0;

    /// Manifest access for the plugin being loaded (read-only).
    virtual const class PluginManifest &manifest() const = 0;
};

/// Root plugin interface (V1).
class PluginV1
{
public:
    virtual ~PluginV1() = default;

    /// Must equal the manifest "id".
    virtual std::string pluginId() const = 0;

    /// Called once after load, before registerContributions(). Return false
    /// (and optionally log via services) to abort loading cleanly.
    virtual bool initialize( HostServicesV1 &services ) = 0;

    /// Called once after initialize(). Register everything the plugin
    /// contributes.
    virtual void registerContributions( ContributionContextV1 &context ) = 0;

    /// Called before the library is unloaded (or at host shutdown).
    virtual void shutdown() = 0;
};

/// The well-known entry point symbol a native plugin must export.
///   extern "C" exprs::PluginV1* EXPRS_createPluginV1();
constexpr const char *kPluginEntryPointV1 = "EXPRS_createPluginV1";

/// Optional second entry point for plugins contributing UI. Declared here
/// (Qt-free) so the Qt-free loader core can resolve it; see the
/// Qt-dependent exprs/plugin_ui.h for the interface it returns.
constexpr const char *kUiContributionEntryPointV1 = "EXPRS_createUiContributionV1";

/// Convenience macro for plugin main translation units:
///   EXPRS_EXPORT_PLUGIN(org_example_plugin::MyPlugin)
#define EXPRS_EXPORT_PLUGIN(PluginClass)                                       \
    extern "C" ::exprs::PluginV1 *EXPRS_createPluginV1()                       \
    {                                                                          \
        try                                                                    \
        {                                                                      \
            return new PluginClass();                                          \
        }                                                                      \
        catch ( ... )                                                          \
        {                                                                      \
            return nullptr;                                                    \
        }                                                                      \
    }

} // namespace exprs

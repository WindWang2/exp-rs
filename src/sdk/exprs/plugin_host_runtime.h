/***************************************************************************
 * exprs/plugin_host_runtime.h — seam for out-of-process native plugin
 * hosting (isolation runtime 5.0)
 *
 * The registry owns lifecycle state for EVERY plugin; how a native plugin's
 * code is hosted is a strategy. The historical strategy maps the binary into
 * the host process (exprs/plugin_loader.h). A host-process strategy instead
 * spawns exprs_plugin_host_worker, drives the PluginV1 lifecycle over the
 * versioned IPC contract (exprs/ipc_*.h) and hands PROXY contributions to
 * the sink — plugin code never runs in the host process.
 *
 * The SDK only defines the seam; the implementation lives in the plugin
 * host module (sicnu_plugins_hostprocess). The registry delegates when the
 * manifest declares runtime "host-process" and a strategy is installed;
 * without one the load is refused typed (E6006), never silently downgraded
 * to in-process.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>

#include "exprs/plugin_record.h"

namespace exprs {

class HostServicesV1;
class PluginContributionSink;
class PluginDiagnosticLog;

class HostProcessRuntime
{
public:
    virtual ~HostProcessRuntime() = default;

    /// Launches (or reuses) the worker for @p record.manifest.id, performs
    /// the handshake, drives plugin.load and registers proxy contributions
    /// into @p sink. Called by the registry with its lifecycle lock held —
    /// implementations must not re-enter the registry.
    virtual bool loadPlugin( const PluginRecord &record, HostServicesV1 &services,
                             PluginContributionSink &sink, PluginDiagnosticLog &log ) = 0;

    /// Sends plugin.shutdown (bounded), reaps the worker process and closes
    /// the session. Called after the registry drained in-flight executions;
    /// on failure the implementation must still guarantee the process dies
    /// (kill ladder) before returning false.
    virtual bool unloadPlugin( const std::string &pluginId, PluginDiagnosticLog &log ) = 0;

    /// Diagnostic/counters snapshot for doctor surfaces.
    virtual Json::Value diagnosticsSnapshot() const = 0;
};

} // namespace exprs

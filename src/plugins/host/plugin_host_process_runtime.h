/***************************************************************************
 * src/plugins/host/plugin_host_process_runtime.h — launcher-side runtime
 *
 * Implements exprs::HostProcessRuntime: one PluginHostProcessSession per
 * plugin, PROXY contributions registered into the ordinary sink so the
 * platform execution face (AtomicAlgorithmRegistry, agent tool catalog,
 * data provider registry, model runtime registry) is unchanged. Plugin code
 * never executes in this process: every contribution call marshals over the
 * IPC contract, and a plugin crash/hang degrades to typed failures plus the
 * bounded restart policy.
 ***************************************************************************/
#pragma once

#include "exprs/plugin_host_runtime.h"

#include "plugin_host_session.h"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace sicnu::plugins {

class PluginHostProcessRuntime;

/// Mutable session state shared between the runtime and its proxies: a
/// respawn swaps the session in place, so stale proxies pick up the fresh
/// worker while stale GENERATION checks keep older worker references from
/// being reused after an unload (barrier close).
struct PluginHostSessionEntry
{
    std::shared_ptr<PluginHostProcessSession> session;
    exprs::PluginQuota quota;
    Json::Value loadParams;
    std::string entrypointPath;
    std::atomic<bool> respawnArmed{ false };
    /// Recovery wiring (set by the runtime at load).
    PluginHostProcessRuntime *runtime = nullptr;
    std::string pluginId;
    /// Serializes session swaps against concurrent requesters.
    std::mutex mutex;
};
using PluginHostSessionEntryPtr = std::shared_ptr<PluginHostSessionEntry>;

class PluginHostProcessRuntime : public exprs::HostProcessRuntime
{
public:
    struct Options
    {
        std::string workerPath;      ///< exprs_plugin_host_worker absolute path
        exprs::PluginQuota quotaCeilings = exprs::PluginQuota::fromEnvironment();
        int maxRestarts = 3;
        long long restartWindowMs = 60000;
        int handshakeTimeoutMs = 15000;
        /// plugin.load budget (cold first LoadLibrary of a heavy plugin can
        /// take tens of seconds; never bounded by the execution quota).
        int loadTimeoutMs = 120000;
        /// Grace between the cancel frame and forced kill (protocol 1.1
        /// timeout escalation). Bounded tests lower this.
        int killGraceMs = 3000;
    };

    explicit PluginHostProcessRuntime( Options options );

    // -- exprs::HostProcessRuntime ------------------------------------------
    bool loadPlugin( const exprs::PluginRecord &record, exprs::HostServicesV1 &services,
                     exprs::PluginContributionSink &sink,
                     exprs::PluginDiagnosticLog &log ) override;
    bool unloadPlugin( const std::string &pluginId, exprs::PluginDiagnosticLog &log ) override;
    Json::Value diagnosticsSnapshot() const override;

    /// True when the plugin's worker is currently alive (crash detection
    /// surface for tests and doctor).
    bool isWorkerAlive( const std::string &pluginId ) const;

    /// Recovery path used by proxies: apply the restart policy and reload
    /// the plugin into the fresh worker. Returns false when the policy is
    /// exhausted (typed failure stays with the caller). Not const: it mutates
    /// the session entry. Serializable via the entry's respawnArmed flag.
    bool respawn( const std::string &pluginId, PluginHostSessionEntry &entry,
                  exprs::PluginDiagnosticLog &log );

private:
    bool loadParamsFor( const exprs::PluginRecord &record, exprs::HostServicesV1 &services,
                        Json::Value &params, exprs::PluginDiagnosticLog &log ) const;

    Options mOptions;
    mutable std::mutex mMutex;
    int mRestartCount = 0;                     ///< restart policy counter
    bool mRestartWindowArmed = false;
    std::chrono::steady_clock::time_point mFirstRestart;
    std::map<std::string, PluginHostSessionEntryPtr> mSessions;
};

/// Builds the default worker path next to the current executable
/// (exprs_plugin_host_worker[.exe]).
std::string defaultPluginHostWorkerPath();

} // namespace sicnu::plugins

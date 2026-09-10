/***************************************************************************
 * src/plugins/host/plugin_host_session.h — launcher-side worker session
 *
 * Owns ONE worker process for ONE plugin: spawn (inherited protocol
 * handles), handshake validation, request plumbing with quotas applied,
 * crash detection, the cancel/kill ladder and the restart policy.
 *
 * Sessions are reference-counted through shared_ptr so an in-flight proxy
 * call keeps the object alive across an unload; the process handle inside
 * may already be dead, in which case requests fail typed (E6005).
 ***************************************************************************/
#pragma once

#include "exprs/host_protocol.h"
#include "exprs/ipc_channel.h"
#include "exprs/ipc_envelope.h"
#include "exprs/plugin_host_runtime.h"
#include "exprs/plugin_quotas.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

namespace sicnu::plugins {

class PluginHostProcessSession
{
public:
    struct SpawnOptions
    {
        std::string workerPath;         ///< absolute path of exprs_plugin_host_worker
        std::string pluginId;           ///< for diagnostics
        std::string pluginDirectory;    ///< containment root / ${plugin}
        exprs::PluginQuota quota;
        /// Bounded restart policy (crash recovery): at most maxRestarts
        /// respawns inside restartWindowMs; after that the session stays
        /// dead and requests fail typed until an explicit unload/load
        /// cycle resets the counter.
        int maxRestarts = 3;
        long long restartWindowMs = 60000;
        int handshakeTimeoutMs = 15000;
    };

    /// Spawns the worker and validates the handshake. Returns an empty ptr
    /// with @p diagnostics carrying a typed record on failure.
    static std::shared_ptr<PluginHostProcessSession> spawn(
        const SpawnOptions &options, exprs::PluginDiagnosticLog &diagnostics );

    ~PluginHostProcessSession();

    PluginHostProcessSession( const PluginHostProcessSession & ) = delete;
    PluginHostProcessSession &operator=( const PluginHostProcessSession & ) = delete;

    /// Sends one request with the session's quotas applied: the deadline is
    /// clamped to the quota ceiling, and on timeout the cancel/kill ladder
    /// runs (cancel frame, grace, TerminateJobObject/SIGKILL).
    exprs::IpcChannel::Outcome request( const std::string &method, const Json::Value &params,
                                        int deadlineMs,
                                        const exprs::IpcChannel::CancelPredicate &cancelPredicate = {},
                                        const exprs::IpcChannel::ProgressSink &progressSink = {} );

    /// Unclamped variant for INTERNAL control requests (plugin.load during
    /// spawn/recovery): the request-deadline quota must not bound these —
    /// a cold first LoadLibrary of a heavy plugin legitimately exceeds it.
    exprs::IpcChannel::Outcome requestRaw(
        const std::string &method, const Json::Value &params, int deadlineMs,
        const exprs::IpcChannel::CancelPredicate &cancelPredicate = {},
        const exprs::IpcChannel::ProgressSink &progressSink = {} );

    /// True while the worker process is alive. A dead session fails
    /// requests typed; respawnSession() applies the restart policy.
    bool isAlive() const;

    /// Crash liveness probe: after a crash, in-flight requests have failed
    /// and this returns false.
    bool respawnSession( exprs::PluginDiagnosticLog &diagnostics, const Json::Value &loadParams,
                         const std::string &entrypointPath );

    /// Graceful shutdown request + bounded wait, then kill ladder. Always
    /// ends with the process dead.
    bool shutdown( int timeoutMs, exprs::PluginDiagnosticLog &diagnostics );

    const exprs::PluginQuota &quota() const { return mOptions.quota; }
    const Json::Value &hello() const { return mHello; }
    unsigned generation() const { return mGeneration; }
    int restartCount() const { return mRestartCount; }

private:
    PluginHostProcessSession() = default;

    bool spawnWorkerProcess( exprs::PluginDiagnosticLog &diagnostics );
    bool awaitHandshake( exprs::PluginDiagnosticLog &diagnostics );
    void killProcess( const char *reason );
    void enforceDeadline( long long requestId );

    SpawnOptions mOptions;
    Json::Value mHello;
    std::unique_ptr<exprs::IpcChannel> mChannel;

    // OS process plumbing (platform handles owned here).
    void *mProcessHandle = nullptr;   ///< Windows: HANDLE; POSIX: pid as void*
    void *mJobHandle = nullptr;       ///< Windows: job object

    std::atomic<bool> mProcessAlive{ false };
    std::atomic<unsigned> mGeneration{ 1 };
    std::atomic<int> mRestartCount{ 0 };
    std::chrono::steady_clock::time_point mFirstRestart;
    bool mRestartWindowArmed = false;

    // Handshake state (written by the channel reader thread).
    std::mutex mHelloMutex;
    std::condition_variable mHelloCv;
    bool mHelloReceived = false;
};

} // namespace sicnu::plugins

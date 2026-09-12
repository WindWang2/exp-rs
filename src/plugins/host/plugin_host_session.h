/***************************************************************************
 * src/plugins/host/plugin_host_session.h — launcher-side worker session
 *
 * Owns ONE worker process for ONE plugin: spawn (inherited protocol
 * handles), handshake validation, request plumbing with quotas applied,
 * crash detection, the cancel/kill ladder and the restart policy.
 *
 * Protocol 1.1: quota.maxRequestConcurrency is enforced EXACTLY here via a
 * FIFO-fair concurrency gate — at most N requests are in flight on the
 * wire; further requesters wait bounded and then fail typed (E6007
 * overload refusal). A timed-out request is cancelled per-id; when other
 * requests are still in flight the worker keeps serving them and the
 * session is marked POISONED — a poisoned worker is killed as soon as its
 * last in-flight request drains, and the next request applies the restart
 * policy. A sole timed-out request escalates to the kill ladder directly
 * (v1 semantics).
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
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

namespace sicnu::plugins {

/// FIFO-fair counting semaphore (the per-plugin request-slot gate). Waiters
/// are served strictly in arrival order, so a burst of callers cannot
/// starve the first one; acquisition is bounded (typed refusal at the call
/// site on timeout — E6007 overload).
class ConcurrencyGate
{
public:
    explicit ConcurrencyGate( int width ) : mSlots( width < 1 ? 1 : width ) {}

    /// Resizes the gate BEFORE traffic (spawn-time quota application only).
    void setWidth( int width )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        mSlots = width < 1 ? 1 : width;
    }
    int width() const
    {
        std::lock_guard<std::mutex> lock( mMutex );
        return mSlots;
    }

    /// Acquires one slot; FIFO order. Returns false when @p timeoutMs
    /// elapsed without a slot (caller refuses typed).
    bool acquire( int timeoutMs );
    /// Releases one slot (wakes the next waiter in order).
    void release();

    /// Callers currently waiting for a slot (observability, M2).
    int waiting() const;
    /// Peak concurrently-active slots since construction (monotonic).
    int peakActive() const;

private:
    mutable std::mutex mMutex;
    std::condition_variable mCv;
    std::deque<unsigned long long> mWaiters;   ///< FIFO tickets
    int mSlots;
    int mActive = 0;
    int mPeakActive = 0;
    unsigned long long mNextTicket = 1;
};

class PluginHostProcessSession
{
public:
    struct SpawnOptions
    {
        std::string workerPath;         ///< absolute path of exprs_plugin_host_worker
        std::string pluginId;           ///< for diagnostics
        std::string pluginDirectory;    ///< containment root / ${plugin}
        exprs::PluginQuota quota;
        // The bounded restart policy is owned by the RUNTIME (respawn()
        // swaps in a fresh session); the session itself never respawns.
        int handshakeTimeoutMs = 15000;
        /// Grace between the cancel frame and forced kill (kill ladder and
        /// poison-drain checks). Exposed for bounded tests.
        int killGraceMs = 3000;
    };

    /// Spawns the worker and validates the handshake. Returns an empty ptr
    /// with @p diagnostics carrying a typed record on failure.
    static std::shared_ptr<PluginHostProcessSession> spawn(
        const SpawnOptions &options, exprs::PluginDiagnosticLog &diagnostics );

    ~PluginHostProcessSession();

    PluginHostProcessSession( const PluginHostProcessSession & ) = delete;
    PluginHostProcessSession &operator=( const PluginHostProcessSession & ) = delete;

    /// Sends one request with the session's quotas applied: the deadline is
    /// clamped to the quota ceiling, a concurrency slot is acquired (FIFO,
    /// bounded wait = the effective deadline; otherwise typed E6007
    /// refusal), and on timeout the cancel/escalation policy runs.
    exprs::IpcChannel::Outcome request( const std::string &method, const Json::Value &params,
                                        int deadlineMs,
                                        const exprs::IpcChannel::CancelPredicate &cancelPredicate = {},
                                        const exprs::IpcChannel::ProgressSink &progressSink = {} );

    /// Unclamped, UNGATED variant for INTERNAL control requests
    /// (plugin.load during spawn/recovery, plugin.shutdown): the request
    /// deadline quota and the concurrency gate must not bound these — a
    /// cold first LoadLibrary legitimately exceeds the quota, and lifecycle
    /// traffic must never wait behind data-plane slots.
    exprs::IpcChannel::Outcome requestRaw(
        const std::string &method, const Json::Value &params, int deadlineMs,
        const exprs::IpcChannel::CancelPredicate &cancelPredicate = {},
        const exprs::IpcChannel::ProgressSink &progressSink = {} );

    /// True while the worker process is alive. A dead session fails
    /// requests typed; the RUNTIME's respawn() applies the restart policy
    /// and swaps in a fresh session.
    bool isAlive() const;
    /// True when a timed-out request was abandoned while peers kept the
    /// worker alive (protocol 1.1); the worker is killed when in-flight
    /// drains to zero.
    bool isPoisoned() const { return mPoisoned; }
    /// Effective concurrent-request width: min(quota gate slots, worker
    /// hello maxConcurrentRequests) — diagnostic surface.
    int effectiveConcurrency() const;
    unsigned generation() const { return mGeneration; }
    /// Observability (M2): in-flight requests, monotonic peak, gate waiters.
    int inFlight() const;
    int peakInFlight() const;
    int gateWaiters() const { return mGate.waiting(); }
    /// Typed last failure ("", code, message): the most recent non-Ok
    /// outcome this session produced, for doctor/debug-bundle surfaces.
    Json::Value lastFailure() const
    {
        std::lock_guard<std::mutex> lock( mFailureMutex );
        return mLastFailure;
    }
    /// Records one outcome as the typed last failure (no-op for Ok). Used by
    /// the request paths; public so test harnesses can annotate too.
    void recordLastFailure( const exprs::IpcChannel::Outcome &outcome );
    /// Worker process id (diagnostics surface, M10): the OS pid on POSIX,
    /// the process id from the handle on Windows, 0 when not running.
    long long workerPid() const;
    /// Events dropped by the channel's pending-event queue cap (M10).
    long long droppedEvents() const { return mChannel ? mChannel->droppedEvents() : 0; }
    /// Orphan detection (M3): "yes" when the worker's process group still
    /// has members (alive worker, or worker-spawned survivors), "no" when
    /// the group is fully reaped, "unknown" when the probe cannot decide
    /// (EPERM, Windows, no group). FAILS HONEST: never claims clean without
    /// evidence.
    std::string processGroupState() const;
    /// True when the worker advertised protocol 1.2 "directionalFrameCaps"
    /// in its hello features (the host then applies per-direction caps after
    /// plugin.load; a 1.1 worker keeps exact 1.1 shared-cap semantics).
    bool peerSupportsDirectionalFrameCaps() const { return mPeerDirectionalCaps; }
    /// Applies the host-side per-direction frame caps derived from the
    /// effective quota (send = maxRequestBytes, recv = maxResponseBytes).
    /// MUST run only after plugin.load succeeded — the plugin.load frame
    /// itself carried the bounds to the worker, so capping the channel
    /// earlier could strand a large manifest below the worker's knowledge.
    /// No-op when the peer did not advertise the 1.2 feature.
    void applyQuotaFrameCaps();

    /// Graceful shutdown request + bounded wait, then kill ladder. Always
    /// ends with the process dead.
    bool shutdown( int timeoutMs, exprs::PluginDiagnosticLog &diagnostics );

    const exprs::PluginQuota &quota() const { return mOptions.quota; }
    /// Handshake copy (the hello event arrives on the reader thread).
    Json::Value hello() const
    {
        std::lock_guard<std::mutex> lock( mHelloMutex );
        return mHello;
    }

private:
    PluginHostProcessSession() = default;

    bool spawnWorkerProcess( exprs::PluginDiagnosticLog &diagnostics );
    bool awaitHandshake( exprs::PluginDiagnosticLog &diagnostics );
    void killProcess( const char *reason );
    /// Shared timeout escalation: per-id cancel frame, bounded grace, then
    /// either direct kill (sole in-flight request) or poison (peers still
    /// in flight; the worker dies when the last request drains).
    void escalateTimeout( long long requestId );

    SpawnOptions mOptions;
    Json::Value mHello;
    std::unique_ptr<exprs::IpcChannel> mChannel;
    ConcurrencyGate mGate{ 1 };

    // Request/concurrency state (mStateMutex; NEVER held while writing to
    // the channel or waiting on the channel's own locks).
    mutable std::mutex mStateMutex;
    int mInFlight = 0;
    int mPeakInFlight = 0;
    bool mPoisoned = false;
    int mWorkerMaxConcurrent = 1;   ///< from worker.hello (protocol 1.1)
    bool mPeerDirectionalCaps = false;  ///< hello features, protocol 1.2

    // Typed last failure (own mutex: written on every request path).
    mutable std::mutex mFailureMutex;
    Json::Value mLastFailure;

    // OS process plumbing (platform handles owned here).
    void *mProcessHandle = nullptr;   ///< Windows: HANDLE; POSIX: pid as void*
    void *mJobHandle = nullptr;       ///< Windows: job object
    long long mProcessGroupId = -1;   ///< POSIX: worker process group (-pid)
    long long mLastKnownGroup = -1;   ///< POSIX: group id retained after death
                                      ///< (orphan probe, M3)

    std::atomic<bool> mProcessAlive{ false };
    std::atomic<unsigned> mGeneration{ 1 };

    // Handshake state (written by the channel reader thread).
    mutable std::mutex mHelloMutex;
    std::condition_variable mHelloCv;
    bool mHelloReceived = false;
};

} // namespace sicnu::plugins

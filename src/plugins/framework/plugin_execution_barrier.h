/***************************************************************************
 * src/plugins/framework/plugin_execution_barrier.h
 *
 * Owner-scoped in-flight execution barrier (issues #747/#755). Every path
 * that executes plugin code in-process (operator run, agent tool execute,
 * model runtime factory/infer) holds an ExecutionLease for the owning
 * plugin. Plugin unload must first beginDrain() (new leases refused), then
 * waitIdle() (bounded); a timeout means unload is REFUSED — code is never
 * unmapped under an executing thread.
 *
 * After a completed unload the plugin's barrier entry stays closed: leases
 * are permanently refused, so a stale adapter/executor held elsewhere fails
 * with a typed refusal instead of calling into unmapped code.
 *
 * External-tool (pure-manifest) operators execute no plugin code and need
 * no lease. Data providers are listed today but not executed in-process;
 * their future call surface must take leases the same way.
 ***************************************************************************/
#pragma once

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace sicnu::plugins {

class PluginExecutionBarrier;

/// RAII token proving one in-flight execution of @p pluginId's code.
class ExecutionLease
{
public:
    ~ExecutionLease();
    ExecutionLease( ExecutionLease &&other ) noexcept;
    ExecutionLease &operator=( ExecutionLease &&other ) noexcept;
    ExecutionLease( const ExecutionLease & ) = delete;
    ExecutionLease &operator=( const ExecutionLease & ) = delete;

    const std::string &pluginId() const { return mPluginId; }

private:
    friend class PluginExecutionBarrier;
    ExecutionLease( PluginExecutionBarrier &barrier, std::string pluginId );
    void reset();

    PluginExecutionBarrier *mBarrier = nullptr;
    std::string mPluginId;
};

class PluginExecutionBarrier
{
public:
    static PluginExecutionBarrier &instance();

    using LeasePtr = std::unique_ptr<ExecutionLease>;

    /// Acquires an execution lease. Returns nullptr when the plugin is
    /// draining, drained, or unloaded (typed refusal at the call site).
    LeasePtr acquire( const std::string &pluginId );

    /// Marks @p pluginId as draining: new acquire() calls fail. Idempotent.
    void beginDrain( const std::string &pluginId );
    /// Reverts beginDrain (unload was refused; plugin stays usable).
    void cancelDrain( const std::string &pluginId );
    /// Bounded wait until no lease is active for @p pluginId. Returns false
    /// on timeout (drain stays armed; callers must cancelDrain on refusal).
    bool waitIdle( const std::string &pluginId, int timeoutMs );
    /// Marks the entry closed (unload completed): leases refused until the
    /// plugin is loaded again. Stale handles retained from before the unload
    /// stay invalid even across a reload (see generation()).
    void close( const std::string &pluginId );
    /// Reopens a closed entry for a fresh load and bumps the generation so
    /// handles created before the previous unload can never pass the
    /// generation check. No-op while draining (a load cannot race an armed
    /// drain — the registry serializes them).
    void open( const std::string &pluginId );
    /// Test/diagnostic accessor: number of active leases.
    size_t activeCount( const std::string &pluginId ) const;
    /// True while draining or closed (no new leases).
    bool isRefusing( const std::string &pluginId ) const;
    /// Monotonic per-plugin generation; bumped by open(). Handles/objects
    /// that capture raw plugin memory compare this before touching it.
    unsigned long long generation( const std::string &pluginId ) const;

private:
    PluginExecutionBarrier() = default;
    void release( const std::string &pluginId );

    friend class ExecutionLease;

    mutable std::mutex mMutex;
    std::condition_variable mIdle;
    struct State
    {
        bool draining = false;     ///< beginDrain armed; new acquires refused
        bool closed = false;       ///< unload completed; refused until reopened
        size_t active = 0;
        unsigned drainers = 0;     ///< concurrent unload attempts sharing the drain
        unsigned long long generation = 0; ///< bumped by open()
    };
    std::map<std::string, State> mStates;
};

} // namespace sicnu::plugins

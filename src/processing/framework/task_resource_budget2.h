// task_resource_budget2.h — Multi-dimension resource scheduler contracts
// (Data Plane 3.0, Phase G). Extends (does not replace) TaskResourceBudget:
//
//   TaskResourceBudget2 gates on {cpu_threads, ram_mb, vram_mb, disk_read_w,
//   disk_write_w, network_w} with latency classes, per-dimension caps, an
//   interactive reserve (UI-facing tasks keep a guaranteed lane), and
//   first-class aging (no starvation under a steady high-priority stream).
//
// Design rules kept from TaskResourceBudget:
//   - pure accounting helper, no Qt/GDAL dependencies, unit-testable;
//   - the gate only DELAYS launches, never drops work;
//   - a never-starve rule always allows progress (see canLaunch docs).
#pragma once

#include "task_resource_budget.h"

#include <algorithm>
#include <chrono>
#include <string>

namespace sicnu
{

/// Execution latency classes (Phase G). Interactive tasks (UI-triggered,
/// agent-waited) keep a guaranteed capacity share even while batch work
/// saturates the machine.
enum class LatencyClass
{
    Interactive = 0, ///< UI / agent-waited — reserve-protected lane
    Background = 1,  ///< user-initiated but not blocking anyone
    Batch = 2,       ///< bulk processing; lowest scheduling urgency
    Gpu = 3,         ///< GPU-resident work (admitted by the VRAM budget)
    Io = 4,          ///< IO-heavy (imports, exports, scans)
    Network = 5,     ///< network-heavy (remote COG, STAC ingestion)
};

inline const char *latencyClassName( LatencyClass cls )
{
    switch ( cls )
    {
    case LatencyClass::Interactive:
        return "interactive";
    case LatencyClass::Background:
        return "background";
    case LatencyClass::Batch:
        return "batch";
    case LatencyClass::Gpu:
        return "gpu";
    case LatencyClass::Io:
        return "io";
    case LatencyClass::Network:
        return "network";
    }
    return "background";
}

/// Multi-dimension resource request. Weights are 0..100 relative loads; a
/// dimension of 0 means "does not gate on this resource". MiB for *_mb.
struct ResourceRequest
{
    unsigned int cpuThreads = 1;
    unsigned int ramMb = 0;
    unsigned int vramMb = 0;
    unsigned int diskReadWeight = 0;
    unsigned int diskWriteWeight = 0;
    unsigned int networkWeight = 0;
    int gpuDevice = -1; ///< -1 = none / any (resolved by the GPU plane)
    LatencyClass latencyClass = LatencyClass::Background;
    /// Monotonic submit stamp used for aging (set by the scheduler).
    std::chrono::steady_clock::time_point submitStamp{};
    int priority = 1; ///< 0 high, 1 normal, 2 low (mirrors TaskPriority)
};

/// Summed usage of the currently running tasks, per dimension.
struct ResourceUsage
{
    unsigned int cpuThreads = 0;
    unsigned int ramMb = 0;
    unsigned int vramMb = 0;
    unsigned int diskReadWeight = 0;
    unsigned int diskWriteWeight = 0;
    unsigned int networkWeight = 0;
};

/// Per-dimension caps and the interactive reserve. Weights share the 0..100
/// scale of ResourceRequest; ramMb/vramMb/cpuThreads are absolute. A cap of 0
/// disables the gate on that dimension (unlimited).
struct SchedulerLimits
{
    unsigned int cpuThreads = 0;
    unsigned int ramMb = 0;
    unsigned int vramMb = 0;
    unsigned int diskReadWeight = 100;
    unsigned int diskWriteWeight = 100;
    unsigned int networkWeight = 100;
    /// Percent of each weight-dimension reserved for Interactive tasks:
    /// non-interactive candidates cannot push projected usage past
    /// (cap - reserve) while Interactive candidates admit regardless of the
    /// reserve (they still respect the full cap).
    unsigned int interactiveReservePercent = 25;
};

/// Aging: effective priority improves the longer a task waits (one level per
/// interval). A Batch task waiting two intervals admits like an Interactive
/// one, so a steady high-priority stream cannot starve queued work forever.
inline int agedPriority( const ResourceRequest &request,
                         std::chrono::steady_clock::time_point now,
                         std::chrono::milliseconds promoteAfterMs =
                             std::chrono::milliseconds( 5000 ) )
{
    const auto interval = promoteAfterMs.count() > 0 ? promoteAfterMs.count() : 1;
    const auto waited =
        std::chrono::duration_cast<std::chrono::milliseconds>( now - request.submitStamp )
            .count();
    const long long promotions = waited > 0 ? waited / interval : 0;
    return std::max( 0, request.priority - static_cast<int>( promotions ) );
}

/// Multi-dimension admission. The caller sums running usage per dimension.
/// Never-starve semantics: an Interactive candidate (or one aged to
/// Interactive) admits against the FULL cap on weight dimensions (bypassing
/// the reserve but never the cap) and always admits when nothing is running.
class TaskResourceBudget2
{
  public:
    explicit TaskResourceBudget2( SchedulerLimits limits = {} ) : m_limits( limits ) {}

    void setLimits( const SchedulerLimits &limits ) { m_limits = limits; }
    const SchedulerLimits &limits() const { return m_limits; }

    bool canLaunch( const ResourceUsage &running, const ResourceRequest &candidate,
                    std::chrono::steady_clock::time_point now ) const
    {
        const bool protectedLane =
            candidate.latencyClass == LatencyClass::Interactive ||
            agedPriority( candidate, now ) <= 0;
        return fitsCount( running.cpuThreads, candidate.cpuThreads, m_limits.cpuThreads ) &&
               fitsCount( running.ramMb, candidate.ramMb, m_limits.ramMb ) &&
               fitsCount( running.vramMb, candidate.vramMb, m_limits.vramMb ) &&
               fitsWeight( running.diskReadWeight, candidate.diskReadWeight,
                           m_limits.diskReadWeight, protectedLane ) &&
               fitsWeight( running.diskWriteWeight, candidate.diskWriteWeight,
                           m_limits.diskWriteWeight, protectedLane ) &&
               fitsWeight( running.networkWeight, candidate.networkWeight,
                           m_limits.networkWeight, protectedLane );
    }

  private:
    /// Absolute-count dimensions (threads, MiB): cap 0 = unlimited.
    static bool fitsCount( unsigned int used, unsigned int want, unsigned int cap )
    {
        if ( cap == 0 || want == 0 )
            return true;
        return used + want <= cap;
    }
    /// Weight dimensions (0..100): the interactive reserve bounds only
    /// non-protected candidates; protected candidates admit against the full
    /// cap. The never-starve rule lives at the caller: when NOTHING is
    /// running, admit regardless (mirrors TaskResourceBudget::canLaunch).
    bool fitsWeight( unsigned int used, unsigned int want, unsigned int cap,
                     bool protectedLane ) const
    {
        if ( cap == 0 || want == 0 )
            return true;
        unsigned int capForCandidate = cap;
        if ( !protectedLane )
        {
            const unsigned int reserve = cap * m_limits.interactiveReservePercent / 100;
            capForCandidate = reserve < cap ? cap - reserve : 0;
            if ( capForCandidate == 0 )
                capForCandidate = 1; // always allow a sliver of non-reserved work
        }
        return used + want <= capForCandidate;
    }

    SchedulerLimits m_limits;
};

} // namespace sicnu

// worker_lease.cpp — see worker_lease.h for the lease/poison semantics.
#include "worker_lease.h"

#include <chrono>

namespace sicnu::runtime::worker
{

namespace
{
std::int64_t steadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch() )
        .count();
}
} // namespace

WorkerLeaseTracker::WorkerLeaseTracker( WorkerLeaseConfig config ) : m_config( std::move( config ) )
{
}

void WorkerLeaseTracker::setClock( ClockMs clock )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_clock = std::move( clock );
}

WorkerLeaseTracker::WorkerState &WorkerLeaseTracker::stateLocked(
    const std::string &workerId ) const
{
    auto it = m_workers.find( workerId );
    if ( it == m_workers.end() )
    {
        it = m_workers.emplace( workerId, WorkerState{} ).first;
        it->second.lastLiveMs = nowMs();
    }
    return it->second;
}

std::int64_t WorkerLeaseTracker::nowMs() const
{
    // m_mutex HELD by callers; the clock itself is immutable after setClock.
    return m_clock ? m_clock() : steadyNowMs();
}

WorkerHealth WorkerLeaseTracker::onLiveness( const std::string &workerId )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    WorkerState &state = stateLocked( workerId );
    state.lastLiveMs = nowMs();
    state.expiryCounted = false; // a new silence episode may be counted again
    if ( state.quarantined )
        return WorkerHealth::Quarantined;
    return WorkerHealth::Healthy;
}

void WorkerLeaseTracker::onJobStart( const std::string &workerId )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    WorkerState &state = stateLocked( workerId );
    state.holdingJob = true;
    state.lastLiveMs = nowMs();
}

WorkerHealth WorkerLeaseTracker::onJobOutcome( const std::string &workerId, bool succeeded )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    WorkerState &state = stateLocked( workerId );
    state.holdingJob = false;
    state.lastLiveMs = nowMs();
    state.expiryCounted = false;
    if ( succeeded )
    {
        state.consecutiveFailures = 0;
        return state.quarantined ? WorkerHealth::Quarantined : WorkerHealth::Healthy;
    }
    ++state.consecutiveFailures;
    if ( state.consecutiveFailures >= m_config.poisonFailureThreshold && !state.quarantined )
    {
        state.quarantined = true;
        ++m_stats.quarantines;
        return WorkerHealth::Quarantined;
    }
    return WorkerHealth::Healthy;
}

WorkerHealth WorkerLeaseTracker::verdict( const std::string &workerId ) const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    auto it = m_workers.find( workerId );
    if ( it == m_workers.end() )
        return WorkerHealth::Healthy;
    WorkerState &state = it->second;
    if ( state.quarantined )
        return WorkerHealth::Quarantined;
    if ( state.holdingJob )
    {
        const std::int64_t silentMs = nowMs() - state.lastLiveMs;
        if ( silentMs > m_config.leaseTtl.count() )
        {
            if ( !state.expiryCounted )
            {
                state.expiryCounted = true;
                ++m_stats.expiries;
            }
            return WorkerHealth::Expired;
        }
    }
    return WorkerHealth::Healthy;
}

bool WorkerLeaseTracker::mayTakeover( const std::string &workerId, std::uint32_t attempt ) const
{
    (void)workerId; // the bound is per-episode; the host counts attempts
    std::lock_guard<std::mutex> lock( m_mutex );
    return attempt <= m_config.maxTakeoverRetries;
}

void WorkerLeaseTracker::reset( const std::string &workerId )
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_workers.erase( workerId );
}

WorkerLeaseTracker::Stats WorkerLeaseTracker::stats() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_stats;
}

} // namespace sicnu::runtime::worker

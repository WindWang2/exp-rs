// worker_lease.h — host-side worker lease & poison policy (Execution Runtime
// Convergence 11.0, WP-E).
//
// A pure, Qt-free, clock-injected policy object: it NEVER talks to processes
// and is NOT a scheduler. The worker host (LocalWorkerPool) feeds it
// liveness/job-outcome facts and asks for verdicts; enforcing them (recycle,
// quarantine, refuse reuse) stays with the host — preserving the pool's
// fail-loud contracts ("a failed job reports its typed error; no silent
// automatic retry").
//
// Semantics (known-answer tested in tests/test_worker_lease_11.cpp):
//   Liveness lease — any frame from a worker (ready/progress/heartbeat/
//   result/error) renews its lease. Silence longer than leaseTtl while the
//   worker holds a job ⇒ Expired: the host should stop trusting it, kill +
//   recycle, and (host policy permitting) take the job over elsewhere.
//   Poison — consecutive FAILED job outcomes ≥ poisonFailureThreshold ⇒
//   Quarantined (sticky): the worker never receives new jobs until an
//   explicit reset() after a restart. A success clears the streak.
//   Bounded takeover — mayTakeover() is true only for attempt numbers
//   within maxTakeoverRetries, so a host's takeover ladder cannot loop.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace sicnu::runtime::worker
{

struct WorkerLeaseConfig
{
    /// Max silence while a job runs before the lease expires. Worker protocol
    /// v1 heartbeats are optional; progress/result frames also renew.
    std::chrono::milliseconds leaseTtl{ 60'000 };
    /// Consecutive failed job outcomes before quarantine (poison threshold).
    int poisonFailureThreshold = 3;
    /// Bounded takeover attempts a host may make after one lease loss.
    std::uint32_t maxTakeoverRetries = 2;
};

enum class WorkerHealth
{
    Healthy,    ///< lease fresh, failure streak below threshold
    Expired,    ///< liveness silent past the TTL while holding a job
    Quarantined ///< failure streak hit the threshold (sticky until reset)
};

class WorkerLeaseTracker
{
  public:
    explicit WorkerLeaseTracker( WorkerLeaseConfig config = {} );

    using ClockMs = std::function<std::int64_t()>; // steady milliseconds

    /// Sets the clock source (tests inject; default = steady_clock).
    void setClock( ClockMs clock );

    /// Registers/renews a worker's lease. Call on EVERY frame received.
    WorkerHealth onLiveness( const std::string &workerId );

    /// Marks that a worker is now HOLDING a job (starts the lease countdown
    /// even before the first frame).
    void onJobStart( const std::string &workerId );

    /// Records a job terminal outcome; drives the poison streak.
    WorkerHealth onJobOutcome( const std::string &workerId, bool succeeded );

    /// Current verdict (Expired only while a job is held and silent).
    WorkerHealth verdict( const std::string &workerId ) const;

    /// Bounded-takeover policy: true while @p attempt (0-based, counted per
    /// lease-loss EPISODE by the host) is within the configured retry budget.
    /// Pure bound — the ladder cannot loop regardless of who counts.
    bool mayTakeover( const std::string &workerId, std::uint32_t attempt ) const;

    /// Post-restart recovery: clears quarantine and the failure streak.
    void reset( const std::string &workerId );

    struct Stats
    {
        std::uint64_t expiries = 0;     ///< lease-expiry episodes observed
        std::uint64_t quarantines = 0;  ///< workers that hit the poison threshold
    };
    Stats stats() const;

    const WorkerLeaseConfig &config() const { return m_config; }

  private:
    struct WorkerState
    {
        std::int64_t lastLiveMs = 0;
        bool holdingJob = false;
        int consecutiveFailures = 0;
        bool quarantined = false;
        bool expiryCounted = false; ///< one stats bump per silence episode
    };

    WorkerState &stateLocked( const std::string &workerId ) const;
    std::int64_t nowMs() const;

    WorkerLeaseConfig m_config;
    ClockMs m_clock;
    mutable std::mutex m_mutex;
    mutable std::unordered_map<std::string, WorkerState> m_workers;
    mutable Stats m_stats;
};

} // namespace sicnu::runtime::worker

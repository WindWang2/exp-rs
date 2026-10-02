// execution_governor.h — unified resource admission face (Execution Runtime
// Convergence 11.0, WP-D).
//
// One place that owns the execution budget triple (RAM / scratch /
// write-in-flight) and the objects that enforce it:
//
//   - RAM admission runs through chunk::planTileMemory, whose model is an
//     UPPER BOUND since 11.0 (F-A-13 closed) — admitOrRefuse() turns the
//     Refuse rung into a typed AdmissionRefused so a caller can never
//     discover the working set via std::bad_alloc;
//   - scratch bytes flow through an owned ScratchRegistry (budgeted leases,
//     typed ScratchBudgetExceeded);
//   - in-flight WRITE bytes flow through an owned BoundedWriteGate (writer
//     backpressure — 10.0 library capability, now wired by construction
//     instead of caller discipline);
//   - dynamic RSS watermark sampling and throttling;
//   - dynamic TileMemoryPool for reusable tile float buffers.
//
// Leak detection (fail-closed record, not a crash): a governor destroyed
// with outstanding scratch bytes, gated writes, or live pooled memory buffers
// emits a DiagnosticReport (exp.diag.v1) and bumps the
// resource_leaks_detected telemetry counter; tests assert both.
//
// The degradation ladder is the planner's: Admit → ReduceConcurrency →
// Spill → Refuse. This class adds NO second scheduler and no threads.
#pragma once

#include "runtime/chunk/bounded_chunk_queue.h"
#include "runtime/chunk/chunk_pipeline.h"
#include "runtime/chunk/disk_tile_store.h"
#include "runtime/chunk/memory_planner.h"
#include "runtime/chunk/scratch_registry.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sicnu::runtime::exec
{

/// Memory pool and bounding recycler for reusable tile float buffers.
///
/// Manages allocation, recycling, and byte-level bounding of `std::vector<float>`
/// tile buffers. Integrates seamlessly with `TilePayload` via type-erased
/// custom deleters in `std::shared_ptr<std::vector<float>>`.
///
/// Thread-safe: concurrent acquisitions, releases, and recycles are protected
/// by an internal mutex and condition variable.
class TileMemoryPool
{
  public:
    struct Config
    {
        /// Hard upper bound on active in-flight buffer bytes (0 = unbounded).
        /// When active bytes + requested bytes > maxAllocatedBytes, acquireBuffer()
        /// blocks on a condition variable until buffers are released.
        std::uint64_t maxAllocatedBytes = 0;

        /// Upper bound on idle/cached buffer bytes retained in the pool (0 = unbounded).
        /// When buffers are returned to the pool, if pooledBytes + capacityBytes > maxPoolBytes,
        /// the excess buffer is freed instead of retained.
        std::uint64_t maxPoolBytes = 0;
    };

    TileMemoryPool();
    explicit TileMemoryPool( Config config );
    ~TileMemoryPool();

    TileMemoryPool( const TileMemoryPool & ) = delete;
    TileMemoryPool &operator=( const TileMemoryPool & ) = delete;

    /// Acquires a float buffer with at least @p elementCount elements.
    /// If @p maxAllocatedBytes > 0 and budget is full, blocks until sufficient
    /// memory is released, unless the pool is idle (never-starve rule: an oversized
    /// allocation is admitted when allocatedBytes == 0).
    /// If @p cancelFlag is provided and raised while waiting, throws chunk::ChunkCancelled.
    /// Returns a shared_ptr whose custom deleter automatically returns the
    /// buffer to the pool when all references are dropped.
    std::shared_ptr<std::vector<float>> acquireBuffer(
        std::size_t elementCount,
        const std::atomic<bool> *cancelFlag = nullptr,
        std::chrono::milliseconds timeout = std::chrono::milliseconds::max() );

    /// Non-blocking acquire: returns nullptr if the allocation would exceed
    /// maxAllocatedBytes and the pool is not idle.
    std::shared_ptr<std::vector<float>> tryAcquireBuffer( std::size_t elementCount );

    /// Adopts an externally-allocated buffer, tracking its bytes under maxAllocatedBytes
    /// and attaching the pool recycling deleter.
    std::shared_ptr<std::vector<float>> adopt(
        std::vector<float> &&vec,
        const std::atomic<bool> *cancelFlag = nullptr );

    /// Manually recycles an unreferenced buffer into the pool's free list.
    void releaseBuffer( std::unique_ptr<std::vector<float>> buf );

    /// Releases all idle cached buffers back to the system allocator.
    void clearPool();

    /// Shrinks the pool until pooledBytes <= @p targetBytes.
    void trimPool( std::uint64_t targetBytes = 0 );

    /// Active in-flight bytes currently held by callers (unreleased).
    std::uint64_t allocatedBytes() const;

    /// Idle cached bytes currently retained in the pool.
    std::uint64_t pooledBytes() const;

    /// Total count of idle cached buffers in the pool.
    std::size_t pooledCount() const;

    /// Lifetime count of buffer acquisitions.
    std::uint64_t totalAllocations() const;

    /// Lifetime count of cache hits (reused buffers from pool).
    std::uint64_t poolHits() const;

    /// Lifetime count of cache misses (new heap allocations).
    std::uint64_t poolMisses() const;

    const Config &config() const { return m_config; }

  private:
    struct PoolState;
    Config m_config;
    std::shared_ptr<PoolState> m_state;
};

/// Typed admission refusal: the planner's Refuse rung as an exception. The
/// operator-layer bridge maps it onto RSOperatorError(ResourceBudgetExceeded).
struct AdmissionRefused : std::runtime_error
{
    explicit AdmissionRefused( const std::string &plannerReason )
        : std::runtime_error( "execution admission refused: " + plannerReason )
        , reason( plannerReason )
    {
    }
    std::string reason;
};

class ExecutionGovernor
{
  public:
    using RssSampler = std::function<std::uint64_t()>;

    struct Config
    {
        /// Peak-RAM budget for tile streams admitted through this governor.
        /// 0 = unbounded (advisory planning only — tests / trusted hosts).
        std::uint64_t ramBytes = 0;
        /// Scratch-disk budget (ScratchRegistry budgetBytes). 0 = unbounded.
        std::uint64_t scratchBytes = 0;
        /// In-flight write-bytes cap (BoundedWriteGate). 0 = disabled.
        std::uint64_t writeInFlightBytes = 0;
        /// Scratch root directory; empty = platform temp.
        std::string scratchRoot;

        /// High watermark for dynamic RSS throttling (0 = disabled).
        std::uint64_t rssWatermarkBytes = 0;
        /// Low watermark for hysteresis unthrottling (0 = defaults to rssWatermarkBytes).
        std::uint64_t rssLowWatermarkBytes = 0;
        /// Injected sampler returning current process RSS bytes (nullptr = default procfs sampler).
        RssSampler rssSampler = nullptr;

        /// Dynamic TileMemoryPool configuration:
        /// Hard upper bound on active in-flight buffer bytes (0 = unbounded).
        std::uint64_t maxAllocatedBytes = 0;
        /// Upper bound on idle/cached buffer bytes retained in pool (0 = unbounded).
        std::uint64_t maxPoolBytes = 0;
    };

    explicit ExecutionGovernor( Config config );
    ~ExecutionGovernor();

    ExecutionGovernor( const ExecutionGovernor & ) = delete;
    ExecutionGovernor &operator=( const ExecutionGovernor & ) = delete;

    /// Hard admission: plans the tile stream and throws AdmissionRefused on
    /// the Refuse rung. Admit / ReduceConcurrency / Spill plans return
    /// normally (the caller executes the recommended shape).
    chunk::TileMemoryPlan admitOrRefuse( const chunk::TileMemoryRequest &request ) const;

    /// Advisory planning (budget-free estimate of THIS governor's ram cap
    /// when ramBytes==0 → planner Advisory mode).
    chunk::TileMemoryPlan advise( const chunk::TileMemoryRequest &request ) const;

    /// Budgeted scratch leases (ScratchBudgetExceeded is the typed refusal).
    chunk::ScratchRegistry &scratch() { return m_scratch; }
    /// Write backpressure (acquire/release bytes crossing to disk).
    chunk::BoundedWriteGate &writeGate() { return m_writeGate; }
    /// Tile float buffer memory pool (recycling, bounding, and leak tracking).
    TileMemoryPool &memoryPool() { return m_memoryPool; }
    const TileMemoryPool &memoryPool() const { return m_memoryPool; }

    const Config &config() const { return m_config; }

    /// True if rssWatermarkBytes > 0 and current RSS >= rssWatermarkBytes.
    /// Returns false if watermark is disabled (0) or sampler returns 0 (unavailable).
    bool isWatermarkExceeded() const;

    /// Blocks calling thread if RSS >= rssWatermarkBytes until RSS falls below
    /// rssLowWatermarkBytes (or rssWatermarkBytes if low watermark is 0).
    /// Polled every pollInterval. Throws ChunkCancelled if cancelFlag is set.
    void throttleWait( const std::atomic<bool> *cancelFlag = nullptr,
                       std::chrono::milliseconds pollInterval = std::chrono::milliseconds( 10 ) ) const;

    /// Predicate overload allowing arbitrary cancellation / abort conditions.
    void throttleWait( const std::function<bool()> &cancelPredicate,
                       std::chrono::milliseconds pollInterval = std::chrono::milliseconds( 10 ) ) const;

    /// Instantaneous process resident memory in bytes (from sampler or /proc/self/statm).
    std::uint64_t currentRssBytes() const;

    /// True when scratch bytes, gated writes, or active memory buffers are still outstanding (leak
    /// state — checked by the destructor and by tests).
    bool hasOutstandingResources() const;

    /// The last leak report emitted by a destructor check (empty when none).
    static std::string lastLeakReportJson();

  private:
    Config m_config;
    chunk::ScratchRegistry m_scratch;
    chunk::BoundedWriteGate m_writeGate;
    TileMemoryPool m_memoryPool;
};

} // namespace sicnu::runtime::exec

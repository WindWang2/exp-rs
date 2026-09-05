// execution_telemetry.h — Execution observability (Data Plane 3.0, Phase L).
//
// Lock-light, bounded, off by default. Records an execution timeline:
// submission → admission wait → queue wait → resource wait → dispatch →
// execution → cache hit/miss → artifact lifecycle, plus RSS/VRAM samples and
// worker status. Consumers:
//   - dumpJson(): structured event log + counters (agent inspection, CLI);
//   - summaryLines(): human-readable digest;
//   - counters(): cheap atomic snapshot for GUI diagnostics.
//
// Hot-path rule (MEMORY_BUDGET.md): recording is one atomic counter increment
// when disabled; when enabled, one ring slot write under a small mutex never
// held by execution-critical code paths. Capacity is fixed (no unbounded
// growth); oldest events overwrite.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace sicnu::runtime::observability
{

enum class EventKind : uint8_t
{
    Submitted = 0,
    AdmissionWait,
    QueueWait,
    ResourceWait,
    Dispatched,
    ExecutionStart,
    ExecutionEnd,
    CacheHit,
    CacheMiss,
    ChunkProgress,
    WorkerStatus,
    ArtifactRegistered,
    ArtifactReclaimed,
    RunResumed,
    RssSample,
    VramSample,
};

inline const char *eventKindName( EventKind kind )
{
    switch ( kind )
    {
    case EventKind::Submitted: return "submitted";
    case EventKind::AdmissionWait: return "admission_wait";
    case EventKind::QueueWait: return "queue_wait";
    case EventKind::ResourceWait: return "resource_wait";
    case EventKind::Dispatched: return "dispatched";
    case EventKind::ExecutionStart: return "execution_start";
    case EventKind::ExecutionEnd: return "execution_end";
    case EventKind::CacheHit: return "cache_hit";
    case EventKind::CacheMiss: return "cache_miss";
    case EventKind::ChunkProgress: return "chunk_progress";
    case EventKind::WorkerStatus: return "worker_status";
    case EventKind::ArtifactRegistered: return "artifact_registered";
    case EventKind::ArtifactReclaimed: return "artifact_reclaimed";
    case EventKind::RunResumed: return "run_resumed";
    case EventKind::RssSample: return "rss_sample";
    case EventKind::VramSample: return "vram_sample";
    }
    return "unknown";
}

struct TelemetryEvent
{
    int64_t timestampMs = 0; ///< epoch ms
    EventKind kind = EventKind::Submitted;
    long taskId = -1;
    long pipelineId = -1;
    int64_t valueNanos = 0; ///< durations (wait times), byte counts, samples
    std::string subject;    ///< operator id / url / artifact id / worker name
    std::string detail;     ///< one-line diagnostic (bounded)
};

/// Named counters (atomic so they are safe to increment from any thread).
enum class Counter : uint8_t
{
    TasksSubmitted = 0,
    TasksCompleted,
    TasksFailed,
    TasksCanceled,
    CacheHits,
    CacheMisses,
    TilesProcessed,
    WorkersSpawned,
    WorkersCrashed,
    ArtifactsRegistered,
    ArtifactsReclaimed,
    _Count
};

class ExecutionTelemetry
{
  public:
    static ExecutionTelemetry &instance();

    /// Enabled via env SICNU_TELEMETRY=1 (or setEnabled for hosts). When
    /// disabled (default), record* are no-ops costing one atomic load.
    void setEnabled( bool on );
    bool isEnabled() const { return m_enabled.load( std::memory_order_relaxed ); }

    void record( const TelemetryEvent &event );
    void recordSimple( EventKind kind, long taskId, int64_t valueNanos,
                       const std::string &subject = {} );
    void increment( Counter counter );

    /// Snapshot of all counters.
    std::map<std::string, uint64_t> counters() const;
    /// Event log (oldest-first, capacity-bounded). Empty when disabled.
    std::vector<TelemetryEvent> events() const;
    /// Structured dump: {"schema","generatedAt","counters","events":[...]}.
    std::string dumpJson() const;
    /// Human-readable summary (counters + last N events).
    std::vector<std::string> summaryLines( size_t maxEvents = 10 ) const;
    /// Bounded memory: drop stored events (counters persist).
    void clearEvents();

    static constexpr size_t kEventCapacity = 8192;

  private:
    ExecutionTelemetry();
    std::atomic<bool> m_enabled{ false };
    mutable std::mutex m_mutex;
    std::vector<TelemetryEvent> m_events; ///< ring: push_back + erase-front
    std::atomic<uint64_t> m_counters[static_cast<size_t>( Counter::_Count )]{};
    std::atomic<bool> m_overflowWarned{ false };
};

/// RAII duration helper: records AdmissionWait/QueueWait/… on destruction.
class ScopedTelemetrySpan
{
  public:
    ScopedTelemetrySpan( EventKind kind, long taskId );
    ~ScopedTelemetrySpan();
    ScopedTelemetrySpan( const ScopedTelemetrySpan & ) = delete;
    ScopedTelemetrySpan &operator=( const ScopedTelemetrySpan & ) = delete;

  private:
    EventKind m_kind;
    long m_taskId;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace sicnu::runtime::observability

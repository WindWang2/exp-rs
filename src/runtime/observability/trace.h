// trace.h — unified execution trace: bounded NDJSON event chain.
//
// Contract (mirrors execution_telemetry's hot-path rules, MEMORY_BUDGET.md):
//   * OFF by default. When no sink is installed, emit() costs one relaxed
//     atomic load — telemetry must never become a performance accident.
//   * When enabled, the caller pays one bounded-queue try-push; formatting
//     and I/O happen on the sink's writer thread. A full queue drops the
//     OLDEST pending record and counts the drop (bounded memory, honest
//     accounting — the drop counter is part of the trace itself).
//   * FileTraceSink writes NDJSON ("exp.trace.v1"), rotates at maxBytes into
//     "<name>.1", ".2", … keeping maxFiles. No fsync by default (throughput
//     over durability for a diagnostic stream; the trace is evidence, not
//     the commit path).
//
// install(): tests install a RingTraceSink (in-memory) or a FileTraceSink
// (tmp dir); hosts install a FileTraceSink from SICNU_TRACE / SICNU_TRACE_DIR.
// installFileSinkFromEnv() is the production entry point — call it once from
// the host bootstrap; it does nothing unless the env asks for tracing.
#pragma once

#include "trace_id.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sicnu::runtime::observability::trace
{

/// One trace record: a state transition somewhere on the execution chain.
struct TraceEvent
{
    int64_t tsMs = 0;      ///< epoch ms (filled by emit() when 0)
    std::string run;       ///< workflow / harness run id
    std::string task;      ///< TaskCenter task id
    std::string job;       ///< JobEngine job id
    std::string worker;    ///< worker id
    std::string op;        ///< operator / algorithm id
    std::string artifact;  ///< artifact id
    std::string event;     ///< e.g. "submitted", "dispatched", "committed"
    std::string phase;     ///< "" | "start" | "end"
    std::string status;    ///< "" | "ok" | "error" | "cancelled"
    std::string detail;       ///< one-line, bounded annotation
    int64_t durationUs = 0;   ///< span duration in microseconds (0 = absent)

    TraceEvent &at( const TraceContext &ctx )
    {
        run = ctx.run;
        task = ctx.task;
        job = ctx.job;
        worker = ctx.worker;
        op = ctx.op;
        artifact = ctx.artifact;
        return *this;
    }
};

class ITraceSink
{
  public:
    virtual ~ITraceSink() = default;
    /// Called on the writer thread (or inline for ring sinks). Implementations
    /// must be bounded: never grow without limit.
    virtual void write( const TraceEvent &event ) = 0;
    /// Machine-readable location hint (file path or "ring:<capacity>").
    virtual std::string describe() const = 0;
};

/// Bounded in-memory sink (tests, GUI inspector). Oldest records overwrite.
class RingTraceSink : public ITraceSink
{
  public:
    explicit RingTraceSink( size_t capacity = 4096 );
    void write( const TraceEvent &event ) override;
    std::string describe() const override { return "ring:" + std::to_string( m_capacity ); }

    std::vector<TraceEvent> snapshot() const;
    size_t size() const;
    void clear();

  private:
    mutable std::mutex m_mutex;
    size_t m_capacity;
    std::deque<TraceEvent> m_events;
};

/// NDJSON file sink with size-based rotation and a background writer thread.
/// All methods are thread-safe; the destructor drains pending records.
class FileTraceSink : public ITraceSink
{
  public:
    struct Options
    {
        std::string directory;                 ///< created on demand
        std::string baseName = "exp-trace";    ///< files: <baseName>.ndjson, .1, .2…
        uint64_t maxFileBytes = 8u * 1024 * 1024; ///< rotate threshold
        uint32_t maxFiles = 4;                 ///< rotated history kept (not counting current)
        size_t queueCapacity = 8192;           ///< bounded pending queue (drop-oldest)
    };

    explicit FileTraceSink( Options options );
    ~FileTraceSink() override;

    void write( const TraceEvent &event ) override;
    std::string describe() const override;

    /// Path of the active .ndjson file (empty before the first record).
    std::string activeFilePath() const;
    /// Records dropped because the queue was full (honest accounting).
    uint64_t droppedCount() const { return m_dropped.load( std::memory_order_relaxed ); }

  private:
    void writerLoop();
    void rotateIfNeeded_locked();
    void openFile_locked();

    Options m_options;
    mutable std::mutex m_io;        ///< guards file state + rotation
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<TraceEvent> m_queue;
    std::atomic<uint64_t> m_dropped{ 0 };
    std::atomic<bool> m_stopping{ false };
    std::thread m_writer;
    std::string m_activePath;
    uint64_t m_activeBytes = 0;
};

/// Global trace control. Disabled until a sink is installed.
class Trace
{
  public:
    /// The process sink. install(nullptr) disables.
    static void install( std::shared_ptr<ITraceSink> sink );
    static std::shared_ptr<ITraceSink> sink();

    /// Hot-path entry point: one relaxed load when disabled.
    static void publish( const TraceEvent &event );
    static bool enabled() { return s_enabled.load( std::memory_order_relaxed ); }

  private:
    static std::atomic<bool> s_enabled;
    static std::mutex s_mutex;
    static std::shared_ptr<ITraceSink> s_sink;
};

/// Host bootstrap: honors SICNU_TRACE=1 (+ optional SICNU_TRACE_DIR,
/// SICNU_TRACE_MAX_MB). Returns the active file path, empty when disabled.
std::string installFileSinkFromEnv();

/// Convenience: emit a one-line event; fills tsMs when 0.
inline void emitEvent( TraceEvent event )
{
    Trace::publish( event );
}

/// Serialize one event as a single-line NDJSON record (exposed for tests and
/// debugging dumps; the file sink uses the same encoder).
std::string encodeNdjson( const TraceEvent &event );

} // namespace sicnu::runtime::observability::trace

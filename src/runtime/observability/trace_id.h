// trace_id.h — stable correlation identifiers for the unified execution trace.
//
// The trace chain is Pi/Harness → Workflow → TaskCenter → JobEngine → Worker
// → Operator → OutputCommitter → Data/Experiment. Every link carries the SAME
// run/task ids by value; each layer adds its own id. Ids are textual (wire
// and log friendly), uppercase Crockford base32, sortable within a process
// (monotonic generator), and safe to embed in file names.
//
//   TraceId      — 26-char ULID-shaped token: 48-bit ms epoch + 80-bit
//                  process-unique counter/randomness. Generation is
//                  monotonic (never repeats within a process); a
//                  deterministic mode (setDeterministicSeed) exists for
//                  known-answer tests only.
//   TraceContext — the ids one execution carries: run (workflow/agent run),
//                  task, job, worker, operator, artifact. `with*` helpers
//                  derive child contexts immutably.
#pragma once

#include <cstdint>
#include <string>

namespace sicnu::runtime::observability::trace
{

/// Monotonic, process-unique id generator. Thread-safe.
class TraceIdGenerator
{
  public:
    /// New id stamped with the current wall clock.
    static std::string next( const char *prefix = nullptr );

    /// Deterministic ids for known-answer tests: resets the generator so a
    /// test that creates ids gets the same sequence every run. NEVER call in
    /// production. Pair with restoreAfterTests() (RAII) so other tests in the
    /// same binary keep clock-stamped ids.
    static void resetForTests( uint64_t seed = 0 );
    static void restoreAfterTests();

    /// Milliseconds captured by the last generated id (test assertions).
    static uint64_t lastEpochMs();
};

/// Schema marker written into every NDJSON record.
inline constexpr const char *kTraceSchema = "exp.trace.v1";

/// The id set carried through one execution.
struct TraceContext
{
    std::string run;      ///< workflow run / harness run
    std::string task;     ///< TaskCenter task
    std::string job;      ///< JobEngine job / worker jobId
    std::string worker;   ///< worker process or pool slot
    std::string op;       ///< operator / algorithm id
    std::string artifact; ///< output artifact id

    /// True when no id is set (caller has nothing to correlate yet).
    bool empty() const
    {
        return run.empty() && task.empty() && job.empty() && worker.empty() && op.empty() && artifact.empty();
    }

    TraceContext withRun( const std::string &id ) const
    {
        TraceContext c = *this;
        c.run = id;
        return c;
    }
    TraceContext withTask( const std::string &id ) const
    {
        TraceContext c = *this;
        c.task = id;
        return c;
    }
    TraceContext withJob( const std::string &id ) const
    {
        TraceContext c = *this;
        c.job = id;
        return c;
    }
    TraceContext withWorker( const std::string &id ) const
    {
        TraceContext c = *this;
        c.worker = id;
        return c;
    }
    TraceContext withOperator( const std::string &id ) const
    {
        TraceContext c = *this;
        c.op = id;
        return c;
    }
    TraceContext withArtifact( const std::string &id ) const
    {
        TraceContext c = *this;
        c.artifact = id;
        return c;
    }
};

} // namespace sicnu::runtime::observability::trace

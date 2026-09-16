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
//     instead of caller discipline).
//
// Leak detection (fail-closed record, not a crash): a governor destroyed
// with outstanding scratch bytes or gated writes emits a DiagnosticReport
// (exp.diag.v1, first production emitter of that envelope) and bumps the
// resource_leaks_detected telemetry counter; tests assert both.
//
// The degradation ladder is the planner's: Admit → ReduceConcurrency →
// Spill → Refuse. This class adds NO second scheduler and no threads.
#pragma once

#include "runtime/chunk/bounded_chunk_queue.h"
#include "runtime/chunk/disk_tile_store.h"
#include "runtime/chunk/memory_planner.h"
#include "runtime/chunk/scratch_registry.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace sicnu::runtime::exec
{

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

    const Config &config() const { return m_config; }

    /// True when scratch bytes or gated writes are still outstanding (leak
    /// state — checked by the destructor and by tests).
    bool hasOutstandingResources() const;

    /// The last leak report emitted by a destructor check (empty when none).
    static std::string lastLeakReportJson();

  private:
    Config m_config;
    chunk::ScratchRegistry m_scratch;
    chunk::BoundedWriteGate m_writeGate;
};

} // namespace sicnu::runtime::exec

// fault_runner.h — the scenario runner.
//
// One pipeline, both modes: teaching mode renders the report, agent mode
// consumes the JSON. The pipeline is fixed and every step leaves evidence:
//
//   create sandbox → materialize fixture (seeded) → digest source →
//   copy within byte budget → apply fault to the copy only → measure
//   clean + faulted observables → check expectations → diagnose the
//   transition → replay (second copy, compare digests) → cleanup +
//   verify no residue → re-digest source → assemble report.
//
// Guarantees: faults never touch the source (re-digest after the run),
// sandboxes are always cleaned and verified, and the same scenario +
// seed produces a byte-identical report digest on any machine (no wall
// clock, no absolute paths in the canonical body).
#pragma once

#include "fault_report.h"
#include "fault_types.h"

#include <string>

namespace sicnu::faultlab
{

struct FaultRunOptions
{
    /// Sandbox root (default: the system temp directory).
    std::string sandboxRoot;
};

/// Runs one scenario. The returned Result is ok whenever the pipeline
/// itself completed — a failed scenario (impossible expectation, refused
/// fault, budget overflow) is a successful run with `passed == false` and
/// typed diagnostics, not an error.
FaultResult<FaultRunReport> runFaultScenario( const FaultScenario &scenario,
                                              const FaultRunOptions &options = {} );

} // namespace sicnu::faultlab

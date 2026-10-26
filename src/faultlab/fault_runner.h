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
#include <functional>

namespace sicnu::faultlab
{

/// Outcome of the artifact-level verifier hook (#1613): what the installed
/// grader observed on the FAULTED artifact.
struct FaultVerifierOutcome
{
    bool ran = false;                ///< the hook executed and produced a verdict
    bool mustFailSatisfied = false;  ///< every declared must-fail assertion failed
    std::vector<std::string> observedFailedAssertions; ///< failing ids (evidence)
    std::string note;                ///< human-readable detail (grading error, ...)
};

/// Dependency inversion for the artifact-level verifier: the jsoncpp-only
/// faultlab leaf cannot link the grading stack, so the composition layer
/// above (studio_live) installs the implementation. Receives the faulted
/// grid in memory plus the strict-parsed declaration.
using FaultVerifierHook =
  std::function<FaultVerifierOutcome( const FaultGrid &, const FaultVerifierSpec & )>;

struct FaultRunOptions
{
    /// Sandbox root (default: the system temp directory).
    std::string sandboxRoot;
    /// R21-E (#1613): when a scenario declares expected.verifier, the runner
    /// calls this hook and folds the outcome into report.passed (a declared
    /// verifier with NO hook installed fails closed — an unchecked fault
    /// must never read as passed).
    FaultVerifierHook verifierHook;
};

/// Runs one scenario. The returned Result is ok whenever the pipeline
/// itself completed — a failed scenario (impossible expectation, refused
/// fault, budget overflow) is a successful run with `passed == false` and
/// typed diagnostics, not an error.
FaultResult<FaultRunReport> runFaultScenario( const FaultScenario &scenario,
                                              const FaultRunOptions &options = {} );

} // namespace sicnu::faultlab

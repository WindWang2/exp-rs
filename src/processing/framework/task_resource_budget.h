// src/processing/framework/task_resource_budget.h
//
// ADR (perf/architecture goal 2026-08-08) — resource-aware scheduling.
//
// Before this module, RSOperator::memoryPolicy() and executionEstimate() were
// metadata only: they flowed into the agent tool catalog (LLM-facing strings)
// but the TaskCenter scheduler never consulted them. processNextQueuedTasks
// gated launch on (1) per-profile count, (2) global count, (3) process RSS
// watermark — a FullRaster operator and a Streaming operator were throttled
// identically, and estimatedRamBytes was never summed against a budget.
//
// TaskResourceBudget closes that gap. It is a pure, header-linkable accounting
// helper (no Qt widget / GDAL dependency) so it can be unit-tested in isolation:
//
//   - resolveEstimateMb(algorithmId): returns the operator's declared
//     estimatedRamBytes (in MB) via an injectable resolver, with a conservative
//     fallback bucketed by memoryPolicy class when no estimate is declared.
//   - canLaunch(...): true iff projected (running + candidate) RAM is within the
//     budget cap, with a never-starve rule (a profile is always allowed ≥1 task).
//
// The budget cap defaults to the ResourceMonitor watermark so the new gate is
// consistent with (not independent of) the existing RSS gate. The gate is purely
// additive and only DELAYS a launch (the task stays Queued and is re-evaluated
// on every terminal task transition); it never drops work.
#pragma once

#include <functional>
#include <string>

namespace sicnu {

/// Coarse memory-policy class used for the conservative fallback when an
/// operator declares no estimatedRamBytes. Mirrors RSOperatorMemoryPolicy but is
/// duplicated here so this header needs no link dependency on the operators
/// library (same rationale as memoryPolicyName() being header-inline).
enum class TaskMemoryClass
{
    Streaming,                 ///< O(tile) memory — small default footprint
    MultiPassStreaming,        ///< O(tile) + O(global state) — medium default
    FullRaster,                ///< O(width*height*bands) — large default
    ExternalProcess,           ///< external process owns its memory — small
    UnsupportedForLargeRaster, ///< documented heavy — large default
    Unknown                    ///< no policy declared — assume FullRaster (safe)
};

/// A resolved per-task resource estimate. RAM is the only dimension the budget
/// gates on today (matching the existing RSS watermark); disk/CPU are carried
/// for future use and for diagnostics but do not gate launch.
struct TaskResourceEstimate
{
    /// Estimated peak RAM in MiB. 0 means "unknown" (caller applies fallback).
    unsigned int ramMb = 0;
    /// Memory class used by the fallback when ramMb == 0.
    TaskMemoryClass memoryClass = TaskMemoryClass::Unknown;
};

/// Signature of the injected resolver: algorithmId → estimate. The default
/// implementation (wired by TaskCenter) reads the operator's executionEstimate()
/// + memoryPolicy() via the AtomicAlgorithmRegistry. Tests inject fakes.
using TaskEstimateResolver =
    std::function<TaskResourceEstimate( const std::string &algorithmId )>;

/// Conservative default RAM estimate (MiB) for each memory class. Used when an
/// operator declares no estimatedRamBytes. These are deliberately modest — they
/// only need to prevent the worst over-commit (many heavy operators at once);
/// the never-starve rule ensures a single heavy operator always runs.
unsigned int defaultEstimateMbForClass( TaskMemoryClass cls );

/// Pure accounting + decision helper. Owns no state beyond configuration; the
/// caller tracks the set of running tasks and their estimates.
class TaskResourceBudget
{
  public:
    TaskResourceBudget();

    /// Budget cap in MiB. A launch is allowed when projected (running + this
    /// candidate) RAM ≤ cap. 0 disables the gate entirely (everything allowed).
    /// Defaults to the ResourceMonitor watermark (queried lazily via the cap
    /// callback) so the gate is consistent with the RSS watermark.
    void setBudgetMb( unsigned int mb );
    unsigned int budgetMb() const;

    /// Injectable resolver. The default returns TaskResourceEstimate{} (Unknown
    /// → FullRaster fallback). TaskCenter installs the registry-backed resolver.
    void setEstimateResolver( TaskEstimateResolver resolver );
    TaskEstimateResolver estimateResolver() const;

    /// Resolve an estimate for an algorithm (applies the fallback when the
    /// resolver returns ramMb == 0).
    TaskResourceEstimate resolve( const std::string &algorithmId ) const;

    /// True iff a candidate with @a candidateEstimate can launch given the
    /// current @a runningTotalMb and @a runningCountInProfile. Implements the
    /// never-starve rule: if nothing is running in the profile, always allow
    /// (returns true regardless of budget) so a wrong/missing estimate cannot
    /// permanently block all work.
    bool canLaunch( unsigned int runningTotalMb,
                    unsigned int candidateEstimateMb,
                    unsigned int runningCountInProfile ) const;

  private:
    unsigned int m_budgetMb = 0;
    TaskEstimateResolver m_resolver;
};

} // namespace sicnu

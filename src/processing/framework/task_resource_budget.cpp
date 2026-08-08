// src/processing/framework/task_resource_budget.cpp
#include "task_resource_budget.h"

namespace sicnu {

unsigned int defaultEstimateMbForClass( TaskMemoryClass cls )
{
    // Conservative per-class defaults. Streaming/external operators hold only a
    // tile of state; full-raster operators are assumed to need a whole raster
    // band stack. These only need to prevent many heavy operators launching at
    // once — the never-starve rule (canLaunch with runningCountInProfile==0)
    // guarantees a single heavy operator always proceeds even if the default is
    // an underestimate for a particular input.
    switch ( cls )
    {
    case TaskMemoryClass::Streaming:
        return 64;          // O(tile): a few MB; round up generously
    case TaskMemoryClass::MultiPassStreaming:
        return 128;         // O(tile) + histograms / global state
    case TaskMemoryClass::ExternalProcess:
        return 64;          // child process owns its memory; parent's footprint small
    case TaskMemoryClass::FullRaster:
        return 1024;        // whole raster(s) resident — 1 GiB default
    case TaskMemoryClass::UnsupportedForLargeRaster:
        return 1024;        // treated as heavy
    case TaskMemoryClass::Unknown:
        return 1024;        // safest assumption when no policy is declared
    }
    return 1024;
}

TaskResourceBudget::TaskResourceBudget() = default;

void TaskResourceBudget::setBudgetMb( unsigned int mb ) { m_budgetMb = mb; }
unsigned int TaskResourceBudget::budgetMb() const { return m_budgetMb; }

void TaskResourceBudget::setEstimateResolver( TaskEstimateResolver resolver )
{
    m_resolver = std::move( resolver );
}

TaskEstimateResolver TaskResourceBudget::estimateResolver() const
{
    return m_resolver;
}

TaskResourceEstimate TaskResourceBudget::resolve( const std::string &algorithmId ) const
{
    TaskResourceEstimate est;
    if ( m_resolver )
        est = m_resolver( algorithmId );
    if ( est.ramMb == 0 )
        est.ramMb = defaultEstimateMbForClass( est.memoryClass );
    return est;
}

bool TaskResourceBudget::canLaunch( unsigned int runningTotalMb,
                                    unsigned int candidateEstimateMb,
                                    unsigned int runningCountInProfile ) const
{
    // A budget of 0 disables the gate (everything allowed) — preserves the
    // legacy behavior when no cap is configured.
    if ( m_budgetMb == 0 )
        return true;

    // Never-starve: a profile with nothing running always proceeds, even if the
    // candidate's estimate alone exceeds the cap. This prevents a wrong/missing
    // estimate from permanently blocking all work in a profile (deadlock).
    if ( runningCountInProfile == 0 )
        return true;

    const unsigned int projected = runningTotalMb + candidateEstimateMb;
    return projected <= m_budgetMb;
}

} // namespace sicnu

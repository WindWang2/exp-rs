// fault_transforms.h/.cpp — the fault transforms.
//
// A transform is a pure, deterministic function from (grid copy, spec) to a
// typed outcome: it mutates ONLY the grid it is handed (the runner guarantees
// that grid is a sandbox copy) and never reads or writes anything else.
// Every refusal is typed — `faultlab.fault_unknown_family`,
// `faultlab.fault_unsupported_params`, `faultlab.fault_unsafe_target` —
// there is no path where a fault silently does nothing.
#pragma once

#include "fault_types.h"

#include <cstdint>
#include <vector>

namespace sicnu::faultlab
{

struct FaultOutcome
{
    bool ok = false;
    /// Number of atomic mutations applied (evidence for reports).
    std::uint32_t mutations = 0;
    std::vector<FaultDiagnostic> diagnostics;

    const FaultDiagnostic *firstError() const
    {
        for ( const auto &diagnostic : diagnostics )
        {
            if ( diagnostic.severity == FaultSeverity::Error )
            {
                return &diagnostic;
            }
        }
        return nullptr;
    }
};

/// Applies `spec` to `grid`. The caller must pass a sandbox copy: the
/// transform mutates it in place and records how many atomic mutations it
/// made.
FaultOutcome applyFault( FaultGrid &grid, const FaultSpec &spec );

} // namespace sicnu::faultlab

// src/planner/provider_interfaces.h
#pragma once

//
// RS14-09 Scientific Task Planner — injected fact seams (plan.md D1/D5).
//
// Capability facts enter the planner ONLY through these interfaces: the
// planner links no capability data, no Qt and owns no registry. A future
// adapter projects the harness CapabilityKnowledge onto CapabilityProvider;
// the planner never invents operators. Provider DTOs are validated against
// the linked contracts authority before use; a fact that contradicts the
// authority is excluded and surfaces as a typed question, never silently
// consumed.
//

#include <string>
#include <vector>

namespace sicnu::planner {

/// One capability fact the planner may plan over (provider-delegated).
struct PlannerCapability
{
    std::string operatorId;      ///< registered operator id (e.g. "rs:ndvi")
    std::string family;          ///< kFamilySlots member this capability fills
    std::string costClass;       ///< kCostClasses member
    long long estimatedRamMb = 0;
    bool deterministic = true;   ///< false = stochastic operator
    std::string inputDomain;     ///< contracts numeric domain; "" = unconstrained ("any")
    std::string outputDomain;    ///< contracts numeric domain produced; "" = unchanged
};

/// Capability fact seam. Deterministic implementations only: the same call
/// must return the same list (planner output is deterministic end-to-end).
class CapabilityProvider
{
public:
    virtual ~CapabilityProvider() = default;
    /// @returns every known capability whose family slot is @p family.
    virtual std::vector<PlannerCapability> capabilitiesForFamily(
        const std::string &family ) const = 0;
};

/// The provider bundle planScientificWork consumes.
struct PlannerProviders
{
    const CapabilityProvider *capability = nullptr;
};

} // namespace sicnu::planner

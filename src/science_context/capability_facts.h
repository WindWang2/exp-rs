// src/science_context/capability_facts.h
#pragma once

//
// CapabilityFactsLookup — narrow read-only seam over the live capability
// authority (harness CapabilityKnowledge + registry presence checks).
//
// science_context is Qt-free and must not include harness/operator headers,
// so the agent layer installs this lookup; without it the router keeps its
// builtin table but the bundle provenance marks that section
// builtin_fallback/degraded — never mistaken for authority output.
//

#include <json/json.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::science_context {

/// Tri-state presence of the operator/tool backing a capability.
enum class OperatorPresence
{
    Unknown,
    Absent,
    Present
};

/// Resolved capability fact-sets, shaped like merged CapabilityKnowledge
/// entries: { id, surface?, modality[], band_roles{role:min},
/// radiometric{acceptable[],warn[]} }.
struct CapabilityFactsLookup
{
    /// All fact-sets the authority serves for a closed intent — one per
    /// operator entry that declares the intent directly, plus one per variant
    /// that declares it (variant constraints override the base for that
    /// intent). Empty = the authority serves no capability for the intent.
    /// Callers MUST evaluate every candidate: picking one arbitrarily loses
    /// variant-scoped requirements and fabricates feasibility.
    std::function<std::vector<Json::Value>( const std::string &intent )> entriesForIntent;

    /// Presence of the operator/tool `capabilityId` on `surface`
    /// ("operator" | "spatial_tool" | "data_platform_tool" | unset).
    std::function<OperatorPresence( const std::string &capabilityId,
                                    const std::string &surface )>
        presence;

    /// Authority id for bundle provenance (e.g. "harness.capability_knowledge").
    std::string authority;
    /// LIVE authority revision, read on every synthesize: a reload/reinstall
    /// that changes loaded entries advances it and thereby invalidates cached
    /// bundles (cache-key material). Empty = unknown (0).
    std::function<std::uint64_t()> revision;
};

} // namespace sicnu::science_context

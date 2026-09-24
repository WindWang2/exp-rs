// src/planner/live/capability_live.h
#pragma once

//
// Live capability authority → planner CapabilityProvider adapter.
//
// The planner core (sicnu::planner) consumes capability facts ONLY through
// the injected CapabilityProvider seam; this adapter is the production
// implementation of that seam. It projects the SAME live authority the
// rest of the platform reads — the capability knowledge mirror documents
// (data/agent/capabilities/*.json) — through the shared Qt-free read path
// (sicnu::preflight::CapabilityMirrorProjection, the single merge-semantics
// implementation). No second registry, no second merge, no invented facts:
//
//   * a PlannerCapability is minted ONLY for a mirror operator id that (a)
//     carries a declared planner family slot (the explicit table below) and
//     (b) is declared by the contracts registry — the planner re-checks the
//     latter itself (capabilityAgreesWithContracts stays the authority);
//   * numeric input/output domains are DECLARED FROM THE CONTRACTS REGISTRY
//     (the numeric-domain authority), not guessed; the planner still
//     cross-checks every fact it consumes;
//   * determinism comes from the contract seed policy ("seed_param" ⇒
//     stochastic, "none"/"deterministic_internal" ⇒ deterministic);
//   * cost class comes from the mirror's merged resource.cost_class,
//     projected light/medium/heavy → low/medium/high (explicit map).
//
// Everything the mirror declares that this adapter does NOT project is
// visible in stats() — skips are counted and listed, never silent.
//

#include "planner/provider_interfaces.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::preflight {
class CapabilityMirrorProjection;
}

namespace sicnu::planner {

/// Where every projected fact came from, and what was left out.
struct LiveCapabilityProjectionStats
{
    /// Mirror operator ids with no declared planner family slot (sorted).
    /// These stay inert to planning by declaration, not by accident.
    std::vector<std::string> notProjected;
    /// Mirror operator ids with a declared slot but no contracts record
    /// (sorted) — excluded because the planner could never lawfully plan
    /// over them; listed so the exclusion stays inspectable.
    std::vector<std::string> skippedNoContract;
    /// Mirror operator ids whose merged entry carries no usable
    /// resource.cost_class (sorted) — the planner rejects unknown cost
    /// classes, so projecting one would only manufacture questions.
    std::vector<std::string> skippedNoCostClass;
    /// Number of PlannerCapability facts the provider serves.
    int projected = 0;
};

class LiveCapabilityProvider final : public CapabilityProvider
{
  public:
    /// An un-created provider serves no facts; callers must go through
    /// create() — the only path that populates (and fails closed).
    LiveCapabilityProvider() = default;

    /// Projects @p mirror (already populated — loadDirectory/addDocument)
    /// into provider facts. Fails (false + typed @p error) when the mirror
    /// is unconfigured or unhealthy: the planner must never plan over a
    /// degraded authority that looks live.
    static bool create( const sicnu::preflight::CapabilityMirrorProjection &mirror,
                        LiveCapabilityProvider &out, std::string &error );

    /// CapabilityProvider: deterministic family query over the projected
    /// facts (sorted by operator id; the planner re-ranks by cost class).
    std::vector<PlannerCapability> capabilitiesForFamily(
        const std::string &family ) const override;

    const LiveCapabilityProjectionStats &stats() const { return mStats; }

    /// Authority id for provenance consumers (stable wire spelling).
    static const char *authorityId() { return "planner.capability_mirror"; }

    /// sha256/16 over the sorted "id\nmergedJson" set of the PROJECTED
    /// PLANNING FACTS (the rs: operator entries this provider serves, after
    /// merge) — the planning-facts revision handle. Two providers over the
    /// same planning facts carry the same revision; any change to those
    /// facts (entry removed, merged content edited) moves it, so a replan
    /// after a revision change is attributable to facts, not drift. Mirror
    /// content that plans nothing (non-rs: tool ids, family defaults) is
    /// intentionally out of scope.
    std::string revision() const { return mRevision; }

    /// The planner family slots this adapter can serve (sorted). The planner
    /// core queries exactly these names (planner_core.cpp).
    static std::vector<std::string> declaredFamilySlots();

    /// Mirror operator ids declared for @p slot by the explicit table
    /// (sorted). Unknown slots yield an empty vector — never a guess.
    static std::vector<std::string> operatorIdsForSlot( const std::string &slot );

  private:
    std::vector<PlannerCapability> mFacts;               // sorted by (family, operatorId)
    LiveCapabilityProjectionStats mStats;
    std::string mRevision;
};

/// The mirror cost-class vocabulary projected onto the planner cost
/// vocabulary. Returns "" for anything the mirror does not declare —
/// the caller then skips the entry (never maps unknown → "low").
std::string plannerCostClassForMirrorCostClass( const std::string &mirrorCostClass );

/// RAM honesty: the mirror declares cost classes but NO memory estimates,
/// so every projected fact carries estimatedRamMb = 0 (undeclared). A
/// caller setting max_estimated_ram_mb therefore gets a plan whose RAM
/// budget is VACUOUS under this provider — the cost-class budget stays
/// enforced, the RAM one cannot trip until the authority declares RAM.
std::string plannerCostClassForMirrorCostClass( const std::string &mirrorCostClass );

} // namespace sicnu::planner

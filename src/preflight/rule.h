// rule.h — the versioned rule contract of the Scientific Preflight Engine.
//
// A rule is a pure, deterministic judgment: given the typed request and the
// facts resolved through the provider seams, it emits typed findings and an
// honest per-rule trace outcome. Rules do no I/O, see no wall clock and
// never mutate the engine.

#pragma once

#include "preflight/facts.h"
#include "preflight/finding.h"
#include "preflight/provider.h"

#include <memory>
#include <string>
#include <vector>

namespace sicnu::preflight {

struct PreflightRequest;

/// What one rule decided, per the evaluated-trace vocabulary of the report
/// schema: pass | finding | insufficient_facts (skipped is engine-reserved).
struct RuleResult
{
    std::vector<PreflightFinding> findings;
    std::string outcome;  ///< Empty -> engine derives from findings.
    std::string detail;   ///< Short reason, e.g. "missing facts: radiometric_state".
};

/// The facts one rule evaluation may consult: the typed request plus what
/// the provider seams resolved. Nothing else — no globals, no registries.
struct RuleFacts
{
    struct Slot
    {
        std::string slot;
        std::string assetRef;
        SlotFactsResult facts;
    };

    std::vector<Slot> slots;          ///< Request order, bounded by max_inputs.
    CapabilityEntryResult capability; ///< Merged entry for the requested operator.

    const SlotFactsResult *slotByName( const std::string &name ) const;
    /// Slots whose facts resolved to a given kind (e.g. "raster"), in request order.
    std::vector<const Slot *> slotsOfKind( const std::string &kind ) const;
};

class IPreflightRule
{
  public:
    virtual ~IPreflightRule() = default;

    /// Stable registry id, e.g. "preflight.band_role". Duplicate ids are
    /// rejected by the engine.
    virtual std::string id() const = 0;

    /// Rule revision — bumped whenever the judgment semantics change; feeds
    /// the report's rules_revision fingerprint and every finding trace.
    virtual int revision() const = 0;

    virtual RuleResult evaluate( const PreflightRequest &request,
                                 const RuleFacts &facts ) const = 0;
};

using PreflightRulePtr = std::unique_ptr<IPreflightRule>;

} // namespace sicnu::preflight

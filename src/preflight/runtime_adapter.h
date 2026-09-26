// runtime_adapter.h — the thin runtime adapter over the PreflightEngine
// (RS14-02 Track 04: live integration).
//
// Tool surfaces (agent data-platform tools, teaching surfaces) stay THIN:
// they resolve facts and capabilities through their live authorities and
// hand them to this adapter, which owns exactly one evaluation and the three
// read-only projections of the one report:
//
//   * AuthorityCapabilityProvider — ICapabilityProvider over a lookup the
//     embedding layer installs (e.g. the harness CapabilityKnowledge). The
//     authority's MERGED entry travels as-is; nothing here re-implements
//     extends/variant merging, so authority changes flow through verbatim.
//   * TemporalFactsProvider — IAssetFactsProvider decorator that resolves a
//     slot's declared temporal collection references (passports carry
//     pointers, not scene facts) into scene counts/dates through an injected
//     collection lookup. Missing/bad times stay typed: dropped scenes are
//     COUNTED (temporalInvalidTimeCount), never silently narrowed.
//   * preflightCheckJson — args document in, {report, teaching, agent} out.
//     One local engine, builtin rules, no second truth source.
//
// No Qt, no QGIS, no GDAL — the same leaf discipline as the rest of
// src/preflight. Grid facts (size/extent/pixel size/CRS) need no extra
// provider: the passport projection (asset_state_adapter.h) carries them,
// and rules only judge them when provable — typed unknowns otherwise.

#pragma once

#include "preflight/facts.h"
#include "preflight/provider.h"
#include "preflight/report.h"

#include <json/json.h>

#include <functional>
#include <string>
#include <vector>

namespace sicnu::preflight {

/// ICapabilityProvider over an authority lookup handed in by the embedding
/// layer. The lookup returns the MERGED entry for an operator id (null when
/// the authority does not declare it).
class AuthorityCapabilityProvider : public ICapabilityProvider
{
  public:
    using Lookup = std::function<Json::Value( const std::string &operatorId,
                                              const Json::Value &variantParams )>;

    /// @p loadProblems carries authority load problems; they distinguish a
    /// typed Unknown ("not declared") from a typed Unavailable ("authority
    /// broken") when a lookup returns null. Found entries are served either
    /// way — the authority's own contract is skip-invalid, not fail-everything.
    AuthorityCapabilityProvider( Lookup lookup, std::vector<std::string> loadProblems = {} );

    CapabilityEntryResult entryForOperator( const std::string &operatorId,
                                            const Json::Value &variantParams ) const override;

  private:
    Lookup lookup_;
    std::vector<std::string> loadProblems_;
};

/// Facts one temporal collection resolves to, shaped for SlotFacts
/// enrichment. `datesIso` keeps COLLECTION order — rules judge ordering, the
/// provider never sorts (sorting here would erase an order-invalid series).
struct TemporalCollectionFacts
{
    FactStatus status = FactStatus::Unknown;  ///< Resolved collection or typed miss.
    int sceneCount = 0;                       ///< Total declared scenes.
    std::vector<std::string> datesIso;        ///< Parseable scene dates, collection order.
    int invalidTimeScenes = 0;                ///< Scenes without a parseable time (counted).
    bool truncated = false;                   ///< Dates dropped beyond the provider cap.
    std::string detail;                       ///< Set for Unknown / Unavailable.
};

/// Collection-side lookup: collection id → resolved facts. The embedding
/// layer adapts its store (workspace catalog descriptors); the adapter never
/// opens one itself.
using TemporalFactsLookup =
    std::function<TemporalCollectionFacts( const std::string &collectionId )>;

/// IAssetFactsProvider decorator: resolves the inner facts, then enriches
/// the temporal bundle from the injected collection lookup. Uses the FIRST
/// resolvable declared collection reference (sorted, deterministic); several
/// collections on one asset are never merged implicitly — the unresolvable
/// ones surface as the typed temporal unknowns the rules already report.
class TemporalFactsProvider : public IAssetFactsProvider
{
  public:
    /// @p cap bounds retained dates (0 = provider default 512, the rule-side
    /// maximum); dates beyond it are dropped and `truncated` says so.
    TemporalFactsProvider( const IAssetFactsProvider &inner, TemporalFactsLookup lookup,
                           int cap = 512 );

    SlotFactsResult slotFacts( const std::string &assetRef ) const override;

  private:
    const IAssetFactsProvider *inner_;
    TemporalFactsLookup lookup_;
    int cap_;
};

/// The `preflight:check` argument document:
/// {
///   "operator": "rs:ndvi",                  // required non-empty
///   "operator_params": {...},               // optional variant selector
///   "intent": "...",                        // optional free tag (digested)
///   "mode": "teaching" | "agent",           // optional; default "agent"
///   "human_operator_id": "...",             // optional
///   "inputs": [ {"slot": "primary", "ref": "asset-3"}, ... ],  // required non-empty
///   "acknowledgements": ["SPF_..."],        // optional finding codes
///   "budgets": {"max_rules": n, "max_inputs": n, "max_findings": n}  // optional
/// }
///
/// Evaluates ONCE through a local PreflightEngine with the builtin rules and
/// returns:
/// {
///   "kind": "sicnu.preflight.check/1",
///   "report":   { sicnu.preflight.report/1 },
///   "teaching": { sicnu.preflight.teaching/1 },
///   "agent":    { sicnu.preflight.agent/1 }
/// }
/// All three projections derive from the same PreflightReport object — one
/// verdict, one digest, one finding multiset. Byte-deterministic.
///
/// Malformed arguments produce a fail-closed typed error document instead:
/// { "kind": "sicnu.preflight.check_error/1", "code": ..., "detail": ... } —
/// never an empty "ok" report.
Json::Value preflightCheckJson( const Json::Value &args,
                                const IAssetFactsProvider &assetFacts,
                                const ICapabilityProvider &capability,
                                const TemporalFactsLookup *temporal = nullptr );

} // namespace sicnu::preflight

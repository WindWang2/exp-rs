// engine.h — PreflightEngine, the deterministic core of RS14-02 slice B.
//
// Registry and evaluation contract (slice-B design of PR #1207, absent from
// master e4904cd3c):
//   * duplicate rule ids are rejected at registration;
//   * rules are evaluated in sorted-id order — registration order can never
//     leak into the report;
//   * rules_revision = shortDigest of the sorted "id@revision" line set;
//   * request_digest = shortDigest of the canonical request document;
//   * budgets are deterministic and their truncation is LOUD: a typed
//     SPF_BUDGET_EXCEEDED require_ack marker means an over-budget report can
//     never be verdict "ok";
//   * acknowledgement is decided at exactly one point (the engine): an ack
//     clears a require_ack finding with the matching code; a block is never
//     acknowledgable, whatever any rule claims.

#pragma once

#include "preflight/finding.h"
#include "preflight/provider.h"
#include "preflight/report.h"
#include "preflight/rule.h"

#include <json/json.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::preflight {

/// The evaluated request: what should run, on which inputs, with which
/// acknowledgements. Inputs keep request order (slot, assetRef); pairs are
/// the slot name and the asset reference the facts authority resolves.
struct PreflightRequest
{
    std::string operatorId;       ///< Capability entry id, e.g. "rs:ndvi"; empty = undeclared.
    Json::Value operatorParams{ Json::objectValue };  ///< Variant selector for the entry.
    std::string intent;           ///< Optional free intent tag, digested but not interpreted.
    std::string mode;             ///< "teaching" | "agent" (report vocabulary).
    std::string humanOperatorId;  ///< Report-level operator_id (who runs this).
    std::vector<std::pair<std::string, std::string>> inputs;
    std::vector<std::string> acknowledgements;  ///< Finding codes the operator accepts.
    PreflightBudgets budgets;   ///< Per-run caps (max_inputs / max_findings).

    /// Canonical request document (sorted keys, deterministic) — the digest
    /// input. Inputs keep request order; acknowledgements keep request order.
    Json::Value toRequestJson() const;
};

enum class RegistrationResult
{
    Ok,
    DuplicateId,
    RegistryFull,
};

class PreflightEngine
{
  public:
    PreflightEngine() = default;

    /// Registry-level cap for the rules_revision budget (max_rules).
    void setBudgets( const PreflightBudgets &budgets );
    PreflightBudgets budgets() const;

    RegistrationResult registerRule( PreflightRulePtr rule );
    /// Non-empty after a failed registration; names the offending id.
    std::string registrationError() const;

    std::vector<std::string> ruleIds() const;  ///< Sorted.
    std::size_t ruleCount() const;

    /// Deterministic evaluation. The same engine state, the same request and
    /// the same authority answers always produce byte-identical reports.
    PreflightReport evaluate( const PreflightRequest &request,
                              const IAssetFactsProvider &assetFacts,
                              const ICapabilityProvider &capability ) const;

  private:
    std::vector<std::unique_ptr<IPreflightRule>> rules_;  ///< Kept sorted by id.
    PreflightBudgets registryBudgets_;
    std::string lastError_;
};

/// Request digest shared with tests: shortDigest(canonicalRequestJson(request)).
std::string computeRequestDigest( const PreflightRequest &request );

/// Rules revision over an explicit id@revision list (sorted, '\n'-joined).
std::string computeRulesRevision( const std::vector<std::pair<std::string, int>> &idRevisions );

/// Total presentation order: findingLess, then the canonical finding JSON as
/// tie-break — report order never depends on rule registration or emission
/// order for findings that differ at all.
bool totalFindingOrder( const PreflightFinding &a, const PreflightFinding &b );

} // namespace sicnu::preflight

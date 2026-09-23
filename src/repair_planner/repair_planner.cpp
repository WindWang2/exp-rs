// src/repair_planner/repair_planner.cpp
#include "repair_planner.h"

#include "repair_requirement.h"
#include "repair_sha256.h"

#include <algorithm>
#include <map>

namespace sicnu::repair {

namespace {

/// Closed per-kind candidate contract: the action key (only keys that exist
/// in the harness closed action table or the preparation whitelist — the
/// planner invents none), the risk class, and the schema action kind. A ""
/// action key means no harness surface exists; the policy then refuses
/// auto-execution on the whitelist conjunct.
struct CandidateContract
{
    std::string actionKey;
    std::string riskClass;
    std::string kind;       ///< action_kind::*
    Json::Value beforeState;
    Json::Value afterState;
    std::vector<std::string> informationLoss;
    std::vector<std::string> assumptions;
};

Json::Value objOf( const std::string &key, const std::string &value )
{
    Json::Value doc( Json::objectValue );
    doc[key] = value;
    return doc;
}

const std::map<std::string, CandidateContract> &candidateContractTable()
{
    static const std::map<std::string, CandidateContract> kTable = {
        { requirement_kind::kCrsAlign,
          { "reproject_to_reference", repair_risk::kShapePreserving,
            action_kind::kCapabilityRef,
            objOf( "crs", "mismatched" ), objOf( "crs", "reference" ),
            { "reprojection resamples pixel values" },
            { "reference CRS is the authority", "source georeferencing is trustworthy" } } },
        { requirement_kind::kGridAlign,
          { "align_to_reference", repair_risk::kShapePreserving,
            action_kind::kCapabilityRef,
            objOf( "grid", "mismatched" ), objOf( "grid", "aligned_to_reference" ),
            { "resampling changes effective resolution" },
            { "reference grid is the authority", "nearest neighbour preserves values" } } },
        { requirement_kind::kRadiometricState,
          { "normalize_radiometry", repair_risk::kRadiometric,
            action_kind::kCapabilityRef,
            objOf( "radiometric_state", "inconsistent" ),
            objOf( "radiometric_state", "normalized" ),
            { "normalization alters absolute radiometric values" },
            { "metadata-declared radiometric state is correct" } } },
        { requirement_kind::kCalibrationDomain,
          { "calibrate_consistently", repair_risk::kRadiometric,
            action_kind::kCapabilityRef,
            objOf( "calibration", "mixed_domains" ), objOf( "calibration", "consistent" ),
            { "calibration to a common domain changes pixel values" },
            { "calibration targets are declared correctly" } } },
        { requirement_kind::kQualityMask,
          { "", repair_risk::kScienceChanging, action_kind::kCapabilityRef,
            objOf( "valid_pixels", "unmasked" ), objOf( "valid_pixels", "masked" ),
            { "masked pixels leave gaps in coverage" },
            { "quality band reliably flags invalid pixels" } } },
        { requirement_kind::kBandRole,
          { "inspect_bands", repair_risk::kShapePreserving, action_kind::kCapabilityRef,
            objOf( "band_roles", "unresolved" ), objOf( "band_roles", "resolved" ),
            { "non-selected bands are dropped" },
            { "band naming reflects actual roles" } } },
        { requirement_kind::kPolarizationSelect,
          { "select_matching_polarization", repair_risk::kShapePreserving,
            action_kind::kCapabilityRef,
            objOf( "polarization", "mixed" ), objOf( "polarization", "matched" ),
            { "non-selected polarization is dropped" },
            { "matching polarization exists in the source" } } },
        { requirement_kind::kModalityCheck,
          { "check_dataset", repair_risk::kShapePreserving, action_kind::kDecision,
            objOf( "modality", "unverified" ), objOf( "modality", "verified" ),
            {}, { "dataset inspection resolves the declared modality" } } },
        { requirement_kind::kTemporalAlign,
          { "check_collection", repair_risk::kScienceChanging, action_kind::kCapabilityRef,
            objOf( "time_axis", "irregular" ), objOf( "time_axis", "regularized" ),
            { "regularization interpolates or drops irregular scenes" },
            { "acquisition times are declared honestly" } } },
        { requirement_kind::kModelContract,
          { "", repair_risk::kScienceChanging, action_kind::kCapabilityRef,
            objOf( "model_input", "incompatible" ), objOf( "model_input", "conformed" ),
            { "input conforming may resample or drop channels" },
            { "model manifest describes the deployed model" } } },
        { requirement_kind::kTrainingData,
          { "", repair_risk::kScienceChanging, action_kind::kDecision,
            objOf( "training", "invalid" ), objOf( "training", "curated" ),
            {}, { "existing samples are representative of the target classes" } } },
        { requirement_kind::kDatasetSubstitution,
          { "", repair_risk::kScienceChanging, action_kind::kDecision,
            objOf( "dataset", "missing" ), objOf( "dataset", "substituted" ),
            { "substitute data may differ in acquisition conditions" },
            { "a substitute dataset covering the same AOI exists" } } },
        { requirement_kind::kCategoricalCheck,
          { "", repair_risk::kScienceChanging, action_kind::kDecision,
            objOf( "legend", "mismatched" ), objOf( "legend", "reconciled" ),
            { "legend remapping merges categories" },
            { "legend mapping is authoritative" } } },
    };
    return kTable;
}

/// Decision-only kinds synthesize a decision candidate even when the provider
/// has no kernel: surfacing the choice IS the repair plan for them.
bool isDecisionOnlyKind( const std::string &kind )
{
    return kind == requirement_kind::kModalityCheck ||
           kind == requirement_kind::kTrainingData ||
           kind == requirement_kind::kDatasetSubstitution ||
           kind == requirement_kind::kCategoricalCheck;
}

/// Cost rank from a capability entry's declared resource cost class — the
/// closed mapping onto the 1..9 decisionCostRank convention. 0 = unknown.
int costRankFromClass( const Json::Value &entry )
{
    const Json::Value &resource = entry["resource"];
    if ( !resource.isObject() || !resource.isMember( "cost_class" ) ||
         !resource["cost_class"].isString() )
        return 0;
    const std::string costClass = resource["cost_class"].asString();
    if ( costClass == "light" )
        return 2;
    if ( costClass == "medium" )
        return 4;
    if ( costClass == "heavy" )
        return 6;
    return 0;
}

/// Candidate order: offerable first by (cost rank asc, operator id asc),
/// refusals last by operator id, then id. The final tie-break on the
/// (positional, unique) id makes the order total, so equal-keyed entries
/// cannot reorder across toolchains.
bool candidateLess( const RepairAction &a, const RepairAction &b )
{
    const bool aRefusal = !a.refusalCause.empty();
    const bool bRefusal = !b.refusalCause.empty();
    if ( aRefusal != bRefusal )
        return !aRefusal;
    if ( a.cost.rank != b.cost.rank )
        return a.cost.rank < b.cost.rank;
    if ( a.operatorId != b.operatorId )
        return a.operatorId < b.operatorId;
    return a.id < b.id;
}

RepairAction buildCandidate( const RepairRequirement &requirement,
                             const CandidateContract &contract, const Json::Value &entry,
                             int candidateIndex, const std::string &contractKind )
{
    RepairAction action;
    action.id = "ra-" + requirement.requirementId + "-" + std::to_string( candidateIndex );
    action.ruleId = contractKind; // the closed rule that produced the candidate
    action.kind = contract.kind;
    action.actionKey = contract.actionKey;
    action.riskClass = contract.riskClass;
    action.risk.riskClass = contract.riskClass;
    action.risk.severity = requirement.blocking ? "high" : "medium";
    action.beforeState = contract.beforeState;
    action.afterState = contract.afterState;
    action.informationLoss = contract.informationLoss;
    action.assumptions = contract.assumptions;
    action.sourceFinding["code"] = requirement.findingCode;
    action.sourceFinding["severity"] = requirement.severity;
    action.sourceFinding["requirement_id"] = requirement.requirementId;

    // Decision candidates surface a choice — no operator wiring exists
    // (schema: only capability_ref actions must name one), and the cost is
    // the cheapest convention rank (nothing to schedule but the decision).
    if ( contract.kind == action_kind::kDecision )
    {
        action.cost.rank = 1;
        action.cost.notes = "decision only; nothing to schedule";
        action.factsSufficient = true;
        return action;
    }

    // Operator wiring comes from the provider entry; a family default or an
    // entry with no usable operator id is an unknown capability, not a
    // candidate (fail-closed, auditable).
    const std::string operatorId =
        entry.isObject() && entry.isMember( "id" ) && entry["id"].isString()
            ? entry["id"].asString()
            : std::string();
    if ( operatorId.empty() || operatorId.rfind( "family:", 0 ) == 0 )
    {
        action.operatorId = operatorId;
        action.refusalCause = "unknown_capability";
        action.factsSufficient = false;
        action.missingFacts.append( "capability.operator_id" );
        return action;
    }
    action.operatorId = operatorId;

    action.cost.rank = costRankFromClass( entry );
    const Json::Value &resource = entry["resource"];
    if ( resource.isObject() && resource.isMember( "cost_class" ) &&
         resource["cost_class"].isString() )
    {
        action.cost.costClass = resource["cost_class"].asString();
        action.factsUsed["capability_family"] =
            entry.isMember( "family" ) && entry["family"].isString()
                ? entry["family"].asString()
                : "";
    }
    if ( action.cost.rank == 0 )
    {
        // A candidate without a declared cost cannot be scheduled or ranked
        // honestly: a documented refusal with the exact missing fact.
        action.refusalCause = "cost_unknown";
        action.factsSufficient = false;
        action.missingFacts.append( "capability.cost_class" );
        return action;
    }
    action.factsSufficient = true;
    action.cost.notes = "ordering device only";
    return action;
}

/// The decision candidate synthesized for decision-only requirements when the
/// provider offers nothing: surfacing the choice, with the full contract.
RepairAction buildDecisionCandidate( const RepairRequirement &requirement )
{
    const CandidateContract &contract = candidateContractTable().at( requirement.kind );
    return buildCandidate( requirement, contract, Json::Value(), 1, requirement.kind );
}

Json::Value actionDocWithRequirement( const RepairAction &action )
{
    Json::Value doc = repairActionToJson( action );
    // Alternatives carry their requirement linkage so fragments and callers
    // can regroup candidates without relying on selection order.
    doc["requirement_id"] = action.sourceFinding["requirement_id"];
    return doc;
}

} // namespace

std::string findingsDigest( const std::vector<Json::Value> &findings )
{
    Json::Value array( Json::arrayValue );
    for ( const Json::Value &finding : findings )
        array.append( finding );
    std::string hex = sha256Hex( jsonToString( array ) );
    hex.resize( 16 );
    return hex;
}

bool planRepairsForFindings( const std::vector<Json::Value> &findings,
                             const RepairCapabilityProvider &provider,
                             const RepairPolicyContext &context,
                             const RepairPlannerOptions &options,
                             RepairPlannerOutcome &out, RepairError &error )
{
    out = RepairPlannerOutcome{};
    if ( findings.empty() )
    {
        error = RepairError{ "invalid_input", "no findings to plan from" };
        return false;
    }
    // Caps are fail-closed in ONE polarity: every cap must be a positive
    // bound. Zero or negative values are nonsensical requests ("plan
    // nothing", "unlimited" spellings), not silent escapes from the budget.
    if ( options.maxRequirements < 1 || options.maxCandidatesPerRequirement < 1 ||
         options.maxTotalCandidates < 1 )
    {
        error = RepairError{ "invalid_input",
                             "planner caps must all be >= 1" };
        return false;
    }

    std::vector<RepairRequirement> requirements;
    if ( !synthesizeRequirements( findings, requirements, error ) )
        return false; // typed invalid_input from the synthesizer

    // Budget: deterministic requirement truncation, counted — and never
    // folded into a success: a plan that did not even consider all findings
    // cannot claim full blocker resolution.
    const std::size_t maxRequirements = static_cast<std::size_t>( options.maxRequirements );
    const bool requirementsTruncated = requirements.size() > maxRequirements;
    const std::size_t droppedRequirements = requirements.size() - maxRequirements;
    if ( requirementsTruncated )
        requirements.resize( maxRequirements );

    int totalCandidateBudget = options.maxTotalCandidates;
    int truncatedCandidates = 0;

    RepairPlan &plan = out.plan;
    plan.intent = options.intent;
    plan.status = plan_status::kNoSafeRepair;
    plan.resolvesAllBlockers = true;
    plan.provenance["planner"] = kPlannerId;
    plan.provenance["source"] = "sicnu::repair/planner";
    plan.provenance["findings_digest"] = findingsDigest( findings );
    plan.policy["domain"] = context.domain;
    plan.policy["role"] = context.role;
    plan.policy["allow_autonomous_exec"] = context.allowAutonomousExec;
    plan.policy["science_change_approved"] = context.scienceChangeApproved;
    plan.bounds["max_requirements"] = options.maxRequirements;
    plan.bounds["max_candidates_per_requirement"] = options.maxCandidatesPerRequirement;
    plan.bounds["max_total_candidates"] = options.maxTotalCandidates;
    plan.bounds["requirements_truncated"] = requirementsTruncated;
    plan.bounds["dropped_requirements"] = static_cast<int>( droppedRequirements );
    plan.bounds["truncated_candidates"] = 0;

    bool anySelected = false;
    bool allBlockingResolved = true;

    for ( const RepairRequirement &requirement : requirements )
    {
        plan.requirements.append( requirementToJson( requirement ) );

        // Unsupported requirements are typed refusals, never offers.
        if ( requirement.kind == requirement_kind::kUnsupported )
        {
            Json::Value entry( Json::objectValue );
            entry["requirement_id"] = requirement.requirementId;
            entry["cause"] = unresolved_cause::kUnsupportedFinding;
            entry["detail"] = requirement.unsupportedReason;
            plan.unresolved.push_back( entry );
            if ( requirement.blocking )
                allBlockingResolved = false;
            continue;
        }

        // Provider-driven candidates, then deterministic ordering and caps.
        std::vector<RepairAction> candidates;
        bool providerServed = false;
        // Closed alternative-family rule: an inconsistent radiometric state
        // can legitimately be answered by masking invalid pixels first, so
        // the provider's quality_mask family joins the candidate pool (the
        // science_changing contracts keep them confirmation-gated).
        const bool wantsMaskAlternatives = requirement.kind == requirement_kind::kRadiometricState;
        if ( provider.knowsRequirementKind( requirement.kind ) ||
             ( wantsMaskAlternatives &&
               provider.knowsRequirementKind( requirement_kind::kQualityMask ) ) )
        {
            int candidateIndex = 1;
            const auto entries = provider.capabilitiesForRequirement( requirement.kind );
            providerServed = providerServed || !entries.empty();
            for ( const Json::Value &entry : entries )
                candidates.push_back(
                    buildCandidate( requirement, candidateContractTable().at( requirement.kind ),
                                    entry, candidateIndex++, requirement.kind ) );
            if ( wantsMaskAlternatives )
            {
                const auto maskEntries =
                    provider.capabilitiesForRequirement( requirement_kind::kQualityMask );
                providerServed = providerServed || !maskEntries.empty();
                for ( const Json::Value &entry : maskEntries )
                    candidates.push_back(
                        buildCandidate( requirement,
                                        candidateContractTable().at( requirement_kind::kQualityMask ),
                                        entry, candidateIndex++,
                                        requirement_kind::kQualityMask ) );
            }
        }
        // Primary-family candidates rank ahead of equal-cost alternative
        // families; the rest of the order is candidateLess (total).
        const std::string primaryKind = requirement.kind;
        std::stable_sort( candidates.begin(), candidates.end(),
                          [primaryKind]( const RepairAction &a, const RepairAction &b ) {
                              const bool aPrimary = a.ruleId == primaryKind;
                              const bool bPrimary = b.ruleId == primaryKind;
                              if ( aPrimary != bPrimary )
                                  return aPrimary;
                              return candidateLess( a, b );
                          } );

        // Decision-only kinds never fall through silently: when no provider
        // candidate exists, the synthesized decision IS the offer. A fully
        // consumed candidate budget prevents even the synthesis and is
        // reported as what it is: budget exhaustion.
        bool decisionBlockedByBudget = false;
        if ( candidates.empty() && isDecisionOnlyKind( requirement.kind ) )
        {
            if ( totalCandidateBudget == 0 )
                decisionBlockedByBudget = true;
            else
            {
                candidates.push_back( buildDecisionCandidate( requirement ) );
                providerServed = true;
            }
        }

        // Budget: per-requirement cap first, then the whole-plan candidate
        // budget. Only a WHOLE-PLAN budget cut can starve a requirement into
        // budget_exhausted; a per-requirement cap cut always keeps at least
        // one candidate, so the requirement still resolves on its best offer
        // (the cut is visible in bounds.truncated_candidates).
        const int budgetBefore = totalCandidateBudget;
        const std::size_t perRequirementCap =
            static_cast<std::size_t>( options.maxCandidatesPerRequirement );
        if ( candidates.size() > perRequirementCap )
        {
            truncatedCandidates += static_cast<int>( candidates.size() - perRequirementCap );
            candidates.resize( perRequirementCap );
        }
        bool planBudgetCut = false;
        if ( static_cast<int>( candidates.size() ) > budgetBefore )
        {
            truncatedCandidates +=
                static_cast<int>( candidates.size() ) - budgetBefore;
            planBudgetCut = true;
            candidates.resize( std::max( 0, budgetBefore ) );
        }
        totalCandidateBudget = budgetBefore - static_cast<int>( candidates.size() );

        // Selection: first offerable candidate; the rest stay as auditable
        // alternatives (refusals included — their contract explains why).
        const RepairAction *selected = nullptr;
        for ( const RepairAction &candidate : candidates )
        {
            if ( selected == nullptr && candidate.refusalCause.empty() )
                selected = &candidate;
            else
                plan.alternatives.append( actionDocWithRequirement( candidate ) );
        }

        Json::Value policyEntry( Json::objectValue );
        policyEntry["requirement_id"] = requirement.requirementId;

        if ( selected == nullptr )
        {
            Json::Value entry( Json::objectValue );
            entry["requirement_id"] = requirement.requirementId;
            if ( planBudgetCut || decisionBlockedByBudget )
                entry["cause"] = unresolved_cause::kBudgetExhausted;
            else if ( candidates.empty() )
                entry["cause"] = unresolved_cause::kNoCandidate;
            else
                entry["cause"] = unresolved_cause::kAllCandidatesRefused;
            entry["finding_code"] = requirement.findingCode;
            plan.unresolved.push_back( entry );
            if ( requirement.blocking )
                allBlockingResolved = false;
            policyEntry["candidate_id"] = "";
            policyEntry["decision"] = policy_decision::kNeedsConfirmation;
            policyEntry["cause"] = entry["cause"];
            plan.policy["per_candidate"].append( policyEntry );
            continue;
        }

        plan.selected.push_back( *selected );
        anySelected = true;
        const RepairPolicyDecision decision =
            evaluateRepairPolicy( *selected, context );
        policyEntry["candidate_id"] = selected->id;
        policyEntry["decision"] = decision.decision;
        policyEntry["reason_code"] = decision.reasonCode;
        plan.policy["per_candidate"].append( policyEntry );
    }

    plan.bounds["truncated_candidates"] = truncatedCandidates;
    if ( requirementsTruncated )
    {
        // Findings that were never considered cannot be claimed resolved.
        Json::Value entry( Json::objectValue );
        entry["requirement_id"] = "";
        entry["cause"] = unresolved_cause::kBudgetExhausted;
        entry["detail"] = std::to_string( droppedRequirements ) +
                          " finding(s) dropped by the requirement budget";
        plan.unresolved.push_back( entry );
        allBlockingResolved = false;
    }
    plan.resolvesAllBlockers = allBlockingResolved;
    plan.subject = requirements.empty() ? std::string() : requirements.front().subject;
    if ( anySelected )
    {
        plan.status = plan_status::kPlanned;
    }
    else
    {
        plan.status = plan_status::kNoSafeRepair;
        Json::Value noSafeRepair( Json::objectValue );
        if ( plan.unresolved.empty() )
        {
            // Cannot happen (every requirement resolves or unresolved), but
            // fail closed rather than emitting a naked status.
            noSafeRepair["cause"] = unresolved_cause::kNoCandidate;
            noSafeRepair["requirement_id"] = "";
        }
        else
        {
            noSafeRepair["cause"] = plan.unresolved.front()["cause"];
            noSafeRepair["requirement_id"] = plan.unresolved.front()["requirement_id"];
        }
        plan.noSafeRepair = noSafeRepair;
    }
    assignRepairPlanIdentity( plan );

    // The result receipt: what planning decided, per requirement, with the
    // digests that tie it to the findings and the plan. Deterministic bytes.
    Json::Value &result = out.result;
    result["kind"] = "repair_result";
    result["schema_version"] = "1.0";
    result["plan_id"] = plan.planId;
    result["plan_fingerprint"] = repairPlanFingerprint( plan );
    result["status"] = plan.status;
    result["planning_only"] = true;
    result["intent"] = plan.intent;
    result["findings_digest"] = plan.provenance["findings_digest"];
    Json::Value perRequirement( Json::arrayValue );
    for ( const RepairRequirement &requirement : requirements )
    {
        Json::Value entry( Json::objectValue );
        entry["requirement_id"] = requirement.requirementId;
        entry["kind"] = requirement.kind;
        entry["finding_code"] = requirement.findingCode;
        entry["severity"] = requirement.severity;
        bool resolved = false;
        for ( const RepairAction &action : plan.selected )
        {
            if ( action.sourceFinding["requirement_id"].isString() &&
                 action.sourceFinding["requirement_id"].asString() ==
                     requirement.requirementId )
            {
                resolved = true;
                entry["selected_candidate_id"] = action.id;
                entry["operator_id"] = action.operatorId;
                break;
            }
        }
        int alternativeCount = 0;
        for ( const Json::Value &alternative : plan.alternatives )
        {
            if ( alternative["requirement_id"].isString() &&
                 alternative["requirement_id"].asString() == requirement.requirementId )
                ++alternativeCount;
        }
        entry["alternative_count"] = alternativeCount;
        entry["resolved"] = resolved;
        for ( const Json::Value &unresolved : plan.unresolved )
        {
            if ( unresolved["requirement_id"].isString() &&
                 unresolved["requirement_id"].asString() == requirement.requirementId )
                entry["cause"] = unresolved["cause"];
        }
        for ( const Json::Value &policyEntry : plan.policy["per_candidate"] )
        {
            if ( policyEntry["requirement_id"].isString() &&
                 policyEntry["requirement_id"].asString() == requirement.requirementId )
            {
                entry["decision"] = policyEntry["decision"];
                if ( policyEntry.isMember( "reason_code" ) )
                    entry["reason_code"] = policyEntry["reason_code"];
                if ( policyEntry.isMember( "cause" ) )
                    entry["cause"] = policyEntry["cause"];
            }
        }
        perRequirement.append( entry );
    }
    result["per_requirement"] = perRequirement;
    Json::Value counts( Json::objectValue );
    counts["requirements"] = static_cast<int>( requirements.size() );
    counts["selected"] = static_cast<int>( plan.selected.size() );
    counts["alternatives"] = static_cast<int>( plan.alternatives.size() );
    counts["unresolved"] = static_cast<int>( plan.unresolved.size() );
    counts["candidates_truncated"] = truncatedCandidates;
    counts["requirements_dropped"] = static_cast<int>( droppedRequirements );
    result["counts"] = counts;
    result["policy"] = plan.policy;
    result["findings_digest_format"] = "sha256/16";
    return true;
}

bool repairPlanFragment( const RepairPlan &plan, const std::string &requirementId,
                         Json::Value &fragment, RepairError &error )
{
    const Json::Value *requirement = nullptr;
    for ( const Json::Value &entry : plan.requirements )
    {
        if ( entry.isObject() && entry["requirement_id"].isString() &&
             entry["requirement_id"].asString() == requirementId )
        {
            requirement = &entry;
            break;
        }
    }
    if ( requirement == nullptr )
    {
        error = RepairError{ "invalid_input",
                             "unknown requirement id: " + requirementId };
        return false;
    }

    fragment = Json::Value( Json::objectValue );
    fragment["kind"] = "repair_fragment";
    fragment["schema_version"] = "1.0";
    fragment["plan_id"] = plan.planId;
    fragment["plan_fingerprint"] = repairPlanFingerprint( plan );
    fragment["planning_only"] = true;
    fragment["requirement"] = *requirement;

    Json::Value candidates( Json::arrayValue );
    for ( const RepairAction &action : plan.selected )
    {
        if ( action.sourceFinding["requirement_id"].isString() &&
             action.sourceFinding["requirement_id"].asString() == requirementId )
            candidates.append( actionDocWithRequirement( action ) );
    }
    for ( const Json::Value &alternative : plan.alternatives )
    {
        if ( alternative["requirement_id"].isString() &&
             alternative["requirement_id"].asString() == requirementId )
            candidates.append( alternative );
    }
    fragment["candidates"] = candidates;
    return true;
}

} // namespace sicnu::repair

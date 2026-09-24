// src/planner/planner_vocab.cpp — closed-vocabulary definitions + membership.
#include "planner/planner_vocab.h"

#include "contracts/scientific_contract.h"

#include <algorithm>

namespace sicnu::planner {

const std::vector<std::string> kGoalKinds = {
    "measurement", "classification", "change",
    "monitoring",  "detection",       "temporal_analysis",
    "map_product",
};

// Mirror of MissionStage (import/preprocess/analyze/verify/publish; the
// workbench spells the analyze stage "analyze") + "record".
const std::vector<std::string> kStepRoles = {
    "import", "preprocess", "analyze", "verify", "publish", "record",
};

const std::vector<std::string> kVerdicts = {
    "feasible", "feasible_with_gaps", "infeasible",
};

const std::vector<std::string> kQuestionKinds = {
    "insufficient_data", "ambiguity", "decision_required",
};

const std::vector<std::string> kRiskKinds = {
    "resource_over_budget", "stochastic_operator", "data_gap", "degraded_projection",
};

const std::vector<std::string> kCostClasses = { "low", "medium", "high" };

const std::vector<std::string> kAssetKinds = { "raster", "vector", "collection", "model" };

// Declared mirror of scientific_state assetLifecycleToString — drift-pinned.
const std::vector<std::string> kAssetStates = {
    "registered", "resolving", "ready", "missing", "unavailable_source",
    "offline",    "authentication_required", "error", "stale", "unknown",
};

// Declared mirror of the scientific_state modality axis — drift-pinned.
const std::vector<std::string> kAssetModalities = {
    "optical", "sar", "dem", "unknown",
};

const std::vector<std::string> kModeKinds = { "standard", "teaching", "agent" };

const std::vector<std::string> kAutonomyLevels = { "full", "guided", "minimal" };

// Sorted by construction; tests pin both closure and sort order.
const std::vector<std::string> kProposalRejectionCodes = {
    "planner:proposal_determinism_violation",
    "planner:proposal_family_mismatch",
    "planner:proposal_forbidden_operator",
    "planner:proposal_goal_mismatch",
    "planner:proposal_inconsistent_verdict",
    "planner:proposal_mode_mismatch",
    "planner:proposal_over_budget",
    "planner:proposal_schema_invalid",
    "planner:proposal_structure_invalid",
    "planner:proposal_unknown_asset",
    "planner:proposal_unknown_operator",
    "planner:proposal_unmet_precondition",
    "planner:proposal_unverified_transition",
};

// Family slots the rule table queries through the CapabilityProvider. These
// are stage-granular capability FAMILIES, not operator ids (plan.md D5: the
// provider owns which concrete operators exist).
const std::vector<std::string> kFamilySlots = {
    "data_import",     // scene/collection import into the platform
    "calibration",     // numeric-domain bridge (e.g. dn → reflectance)
    "alignment",       // grid/CRS/resolution alignment across scenes
    "feature_stack",   // band stack / feature preparation
    "analysis",        // the goal's scientific core
    "uncertainty",     // uncertainty quantification products
    "verification",    // accuracy/validation evidence
    "publication",     // map/product publication
    "record",          // scientific record keeping
};

bool isKnownVocabValue( const std::vector<std::string> &vocab, const std::string &value )
{
    return std::find( vocab.begin(), vocab.end(), value ) != vocab.end();
}

bool isKnownGoalKind( const std::string &value ) { return isKnownVocabValue( kGoalKinds, value ); }
bool isKnownStepRole( const std::string &value ) { return isKnownVocabValue( kStepRoles, value ); }
bool isKnownVerdict( const std::string &value ) { return isKnownVocabValue( kVerdicts, value ); }
bool isKnownQuestionKind( const std::string &value ) { return isKnownVocabValue( kQuestionKinds, value ); }
bool isKnownRiskKind( const std::string &value ) { return isKnownVocabValue( kRiskKinds, value ); }
bool isKnownCostClass( const std::string &value ) { return isKnownVocabValue( kCostClasses, value ); }
bool isKnownAssetState( const std::string &value ) { return isKnownVocabValue( kAssetStates, value ); }
bool isKnownAssetKind( const std::string &value ) { return isKnownVocabValue( kAssetKinds, value ); }
bool isKnownModeKind( const std::string &value ) { return isKnownVocabValue( kModeKinds, value ); }
bool isKnownAutonomy( const std::string &value ) { return isKnownVocabValue( kAutonomyLevels, value ); }
bool isKnownProposalRejectionCode( const std::string &value ) { return isKnownVocabValue( kProposalRejectionCodes, value ); }
bool isKnownFamilySlot( const std::string &value ) { return isKnownVocabValue( kFamilySlots, value ); }

int costClassRank( const std::string &value )
{
    for ( size_t i = 0; i < kCostClasses.size(); ++i )
    {
        if ( kCostClasses[i] == value )
            return static_cast<int>( i );
    }
    return -1;
}

std::string maxCostClass( const std::string &a, const std::string &b )
{
    return costClassRank( a ) >= costClassRank( b ) ? a : b;
}

std::string aggregateCostClass( const std::vector<std::string> &classes )
{
    std::string agg = kCostClasses.front();
    for ( const auto &c : classes )
        agg = maxCostClass( agg, c );
    return agg;
}

bool isHardFactAssetState( const std::string &state )
{
    // integration.md §3: only "known/ready" facts may back a hard precondition.
    return state == "ready";
}

bool isKnownContractsNumericDomain( const std::string &value )
{
    return isKnownVocabValue( sicnu::contracts::kNumericDomains, value );
}

const std::vector<std::string> &contractsNumericDomains()
{
    return sicnu::contracts::kNumericDomains;
}

bool isAvailabilityBlockedState( const std::string &state )
{
    return state == "offline" || state == "unavailable_source"
           || state == "authentication_required" || state == "registered"
           || state == "resolving" || state == "stale";
}

} // namespace sicnu::planner

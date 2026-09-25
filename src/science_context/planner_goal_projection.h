// src/science_context/planner_goal_projection.h
#pragma once

//
// science_context bundle → scientific planner inputs (R3 track 14, seam 2).
//
// Projects the exp.science_context.v1 bundle (plus caller-selected passport
// facts the bundle deliberately does not carry) onto the planner's
// ScientificGoal / PlanningContext documents. Discipline:
//
//   * NOTHING is default-filled. An intent the planner vocabulary cannot
//     name leaves the goal kind EMPTY (the planner then answers infeasible
//     with a typed reason) and an explicit unresolved issue; a foreign
//     radiometric unit degrades the asset domain to "unknown" with an
//     issue — the planner answers with a blocking calibration question,
//     never a silent guess;
//   * the bundle does not carry asset lifecycle state, so without an
//     explicit enrichment every asset plans as state "unknown" — the
//     planner treats it as unable to back a hard fact and asks;
//   * caller-supplied enrichment (passport-backed state/domain facts for
//     the selected assets) is vocabulary-VALIDATED: anything the planner
//     vocabularies do not declare is rejected into `unresolved`, never
//     copied through;
//   * provenance travels: the result echoes the bundle's section sources
//     (authority/revision/degraded) and the enrichment's own provenance.
//

#include "science_context/bundle.h"
#include "planner/planning_context.h"
#include "planner/scientific_goal.h"

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

namespace sicnu::science_context {

/// Closed issue codes the projection reports (sorted emission by code, then
/// detail). The projection never swallows an input it could not honestly
/// map — it names it here.
namespace projection_issue {
inline constexpr const char *kUnresolvedIntent = "unresolved_intent";
inline constexpr const char *kForeignRadiometricUnit = "foreign_radiometric_unit";
inline constexpr const char *kUnmappedModality = "unmapped_modality";
inline constexpr const char *kConflictedEvidence = "conflicted_evidence";
inline constexpr const char *kEnrichmentRejected = "enrichment_rejected";
inline constexpr const char *kEmptyAssetId = "empty_asset_id";
inline constexpr const char *kOversizedAssetId = "oversized_asset_id";
inline constexpr const char *kDuplicateAssetId = "duplicate_asset_id";
inline constexpr const char *kUnmappedAutonomyLevel = "unmapped_autonomy_level";
} // namespace projection_issue

struct ProjectionIssue
{
    std::string code;   ///< projection_issue::* member
    std::string detail; ///< human/agent-readable sentence naming the input
};

/// Passport-backed facts for the SELECTED assets, keyed by bundle assetId.
/// The bundle drops asset lifecycle state (and resolution) by design; the
/// caller that holds the passports supplies them here. Every field is
/// checked against the planner vocabularies — a rejected field becomes an
/// `enrichment_rejected` issue and the field stays at its honest default.
struct PlannerAssetEnrichment
{
    std::map<std::string, sicnu::planner::PlannerAssetFacts> assets;
    /// Provenance of the enrichment itself (e.g. "scientific_state.passports").
    std::string source;
    std::uint64_t revision = 0;
};

struct PlannerProjectionResult
{
    sicnu::planner::ScientificGoal goal;
    sicnu::planner::PlanningContext context;
    std::vector<ProjectionIssue> unresolved;
    Json::Value provenance{ Json::objectValue };
};

/// Projects @p bundle into planner inputs. Deterministic: the same bundle
/// and enrichment always project byte-identically.
PlannerProjectionResult projectPlannerInputs(
    const ScientificContextBundle &bundle,
    const PlannerAssetEnrichment &enrichment = {} );

/// The closed intent → planner goal-kind map (single definition). Intents
/// outside the map have no honest planner kind — callers must treat the
/// goal kind "" as "ask the user", never as a wildcard.
std::map<std::string, std::string> intentToGoalKindMap();

/// The passport radiometric-unit → contracts numeric-domain map (single
/// definition). Units outside the map project to "unknown" with an issue.
std::map<std::string, std::string> radiometricUnitToDomainMap();

} // namespace sicnu::science_context

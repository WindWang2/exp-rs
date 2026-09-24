// src/planner/planner_vocab.h
#pragma once

//
// RS14-09 Scientific Task Planner (recovery of PR #1193) — closed vocabularies.
//
// Planning-only decision layer: a ScientificGoal plus a PlanningContext become
// an explainable, versioned ScientificPlan (candidates, typed open questions,
// costs/risks). This module NEVER executes operators, never opens datasets and
// never touches TaskCenter; every capability fact enters through the injected
// provider seam (provider_interfaces.h) and every state-transition gate keys
// off sicnu::contracts (the single per-operator scientific authority).
//
// Every list below is a closed vocabulary: values are wire-stable strings.
// Membership checks live in planner_vocab.cpp so tests can pin closure and
// sort order. Declared mirrors (asset lifecycle, artifact kinds, modalities)
// are pinned against their authorities by tests/test_scientific_planner_drift.
//

#include <string>
#include <vector>

namespace sicnu::planner {

/// Document envelopes. Readers are fail-closed and accept only "1.0".
inline constexpr const char *kGoalEnvelopeKind = "scientific_goal";
inline constexpr const char *kContextEnvelopeKind = "planning_context";
inline constexpr const char *kPlanEnvelopeKind = "scientific_plan";
inline constexpr const char *kSchemaVersion = "1.0";

/// What the user wants to know/produce (plan.md §4.1; 7 kinds).
extern const std::vector<std::string> kGoalKinds;

/// Step roles: mirror of the workbench MissionStage axis (import → preprocess
/// → analyze → verify → publish) plus `record` (scientific record semantics).
/// Kept wire-compatible with missionStageKey so a future mission projection is
/// translation-free (plan.md D4).
extern const std::vector<std::string> kStepRoles;

/// Plan verdicts. feasible_with_gaps = plan exists but carries blocking or
/// non-blocking questions; infeasible = no lawful plan from these facts.
extern const std::vector<std::string> kVerdicts;

/// Typed open-question kinds (plan.md §4.1). No silent fallbacks: every unmet
/// need is one of these.
extern const std::vector<std::string> kQuestionKinds;

/// Closed risk vocabulary (planner_constraints): aggregate/step risk notes.
extern const std::vector<std::string> kRiskKinds;

/// Coarse provider-delegated cost classes. The CapabilityProvider maps each
/// operator's capability facts to ONE class; the planner owns only the order.
extern const std::vector<std::string> kCostClasses;

/// Asset kinds accepted in a PlanningContext: the harness artifact-kind axis
/// (raster/vector/table/model/structured) is the declared authority, with
/// `structured` dropped (no planning semantics) and `collection` added for
/// multi-scene bundles. The intersection is drift-pinned by
/// tests/test_scientific_planner_drift.cpp.
extern const std::vector<std::string> kAssetKinds;

/// Asset lifecycle states — declared mirror of
/// sicnu::scientific_state::assetLifecycleToString (the authority).
/// Drift-pinned byte-for-byte by tests/test_scientific_planner_drift.cpp.
extern const std::vector<std::string> kAssetStates;

/// Asset modalities — declared mirror of the harness workflow_ir modality
/// axis (kModalityOptical/Sar/Dem/Unknown), drift-pinned by
/// tests/test_scientific_planner_drift.cpp. (The scientific_state Modality
/// axis is finer: thermal/hyperspectral project onto `optical` there.)
extern const std::vector<std::string> kAssetModalities;

/// Mode kinds for the plan consumer.
extern const std::vector<std::string> kModeKinds;

/// Autonomy axis (teaching policy; ADR 0155-aligned: guided/minimal never
/// let the plan do the experiment for the student).
extern const std::vector<std::string> kAutonomyLevels;

/// Closed proposal-rejection vocabulary (planner_proposal). Sorted; every
/// code is `planner:proposal_*`. The validator may only emit these.
extern const std::vector<std::string> kProposalRejectionCodes;

/// True when `value` is a member of `vocab` (exact string match).
bool isKnownVocabValue( const std::vector<std::string> &vocab, const std::string &value );

/// Convenience membership checks (planner_vocab.cpp).
bool isKnownGoalKind( const std::string &value );
bool isKnownStepRole( const std::string &value );
bool isKnownVerdict( const std::string &value );
bool isKnownQuestionKind( const std::string &value );
bool isKnownRiskKind( const std::string &value );
bool isKnownCostClass( const std::string &value );
bool isKnownAssetState( const std::string &value );
bool isKnownAssetKind( const std::string &value );
bool isKnownModeKind( const std::string &value );
bool isKnownAutonomy( const std::string &value );
bool isKnownProposalRejectionCode( const std::string &value );

/// Rank of `value` in kCostClasses (low = 0 < medium = 1 < high = 2).
/// @returns -1 when unknown.
int costClassRank( const std::string &value );

/// Coarse aggregate of a list of cost classes (max rank; empty → "low").
std::string aggregateCostClass( const std::vector<std::string> &classes );

/// Deterministic lexical max of two cost classes ("low"/"medium"/"high").
std::string maxCostClass( const std::string &a, const std::string &b );

/// True when the asset state allows a step to consume the asset as a hard
/// fact (only "ready" is a hard fact; everything else is a question).
bool isHardFactAssetState( const std::string &state );

/// True when the asset lifecycle state means the data exists but is not
/// reachable right now (offline / unavailable_source / authentication_required
/// / registered / resolving / stale): a plan may reference it but must raise
/// a typed availability question.
bool isAvailabilityBlockedState( const std::string &state );

/// Plan resource self-limits (plan.md §8, IrLimits-style). Out of bounds is
/// a typed rejection, never a silent truncation.
struct PlanLimits
{
    static constexpr int kMaxSteps = 64;
    static constexpr int kMaxCandidates = 8;
    static constexpr int kMaxInputsPerStep = 8;
    static constexpr int kMaxAssets = 256;
    static constexpr int kMaxAcceptanceCriteria = 32;
    static constexpr size_t kMaxIdChars = 64;
    static constexpr size_t kMaxTextChars = 512;
};

/// Staged capability-family slots the rule table can require. A family slot
/// is a QUERY against the CapabilityProvider ("give me every capability whose
/// family is X"); the planner never hardcodes operator chains (plan.md D5).
extern const std::vector<std::string> kFamilySlots;

bool isKnownFamilySlot( const std::string &value );

/// True when `domain` is a sicnu::contracts kNumericDomains member. The
/// authority is the linked contracts vector itself; this is a lookup, never
/// a local copy.
bool isKnownContractsNumericDomain( const std::string &value );

/// The contracts numeric-domain list the planner plans over (linked from
/// sicnu::contracts; exposed for readers of planner documents).
const std::vector<std::string> &contractsNumericDomains();

} // namespace sicnu::planner

// src/planner/scientific_plan.h
#pragma once

//
// RS14-09 Scientific Task Planner — ScientificPlan document (slice A).
//
// Versioned, fail-closed document ("scientific_plan/1.0"): the explainable
// plan — ordered steps with typed preconditions, expected state transitions
// (contracts numeric domains), verifier targets, cost/risk annotations,
// ranked alternatives, and typed open questions. PLANNING ONLY: a step names
// an operator, it never runs one.
//
// Determinism: same content ⇒ byte-identical canonical JSON and the same
// SHA-256/16 fingerprint (content identity excludes plan_id, so an accepted
// external proposal can be re-minted under a fresh id without losing
// identity).
//

#include <json/json.h>
#include <string>
#include <vector>

#include "planner/planner_vocab.h"

namespace sicnu::planner {

struct StepInput
{
    std::string fromStepId; ///< upstream step ("")
    std::string assetRef;   ///< or a context asset (""); exactly one form
    std::string as;         ///< local input name
};

/// Typed precondition kinds (closed set, plan.md §4.1).
extern const std::vector<std::string> kPreconditionKinds;

struct StepPrecondition
{
    std::string kind;        ///< kPreconditionKinds member
    std::string assetRef;    ///< when the precondition is about one asset
    std::string domain;      ///< contracts numeric domain, when relevant
    int minScenes = 0;
    std::string detail;      ///< ≤ 512 chars
};

struct ExpectedTransition
{
    std::string assetRef;
    std::string fromDomain; ///< contracts numeric domain
    std::string toDomain;   ///< contracts numeric domain
};

struct VerifierTarget
{
    std::string check;   ///< what the verifier should check, ≤ 512 chars
    std::string target;  ///< expected value/threshold, "" when none
};

struct PlannerStep
{
    std::string stepId;      ///< ≤ 64 chars, unique in the plan
    std::string role;        ///< kStepRoles member
    std::string operatorId;  ///< "" for decision/record placeholders
    std::string family;      ///< kFamilySlots member
    std::vector<StepInput> inputs;
    Json::Value params{ Json::objectValue }; ///< bounded serialized size
    std::vector<StepPrecondition> preconditions;
    std::vector<ExpectedTransition> expectedTransitions;
    std::vector<VerifierTarget> verifierTargets;
    std::string costClass;       ///< kCostClasses member
    long long estimatedRamMb = 0;
    bool deterministic = true;   ///< false = stochastic operator (seed policy)
    std::vector<std::string> riskNotes;
    bool studentDecision = false;///< parameters are the student's decision
};

struct PlanAlternative
{
    std::string alternativeId;
    std::string summary;
    std::string whyChosen;    ///< why the PRIMARY was chosen over this one
    std::string whyNot;       ///< why this candidate was not the primary
    int candidateIndex = -1;  ///< index into the PlanningResult candidate list
};

struct PlanOpenQuestion
{
    std::string questionId;
    std::string kind;      ///< kQuestionKinds member
    bool blocking = false;
    std::string detail;    ///< ≤ 512 chars
    std::string suggestion;///< advisory, ≤ 512 chars
};

struct PlanRisk
{
    std::string kind;    ///< kRiskKinds member
    std::string detail;  ///< ≤ 512 chars
    std::string stepId;  ///< "" = plan-level risk
};

struct PlanCost
{
    std::string aggregateCostClass = "low";
    long long totalEstimatedRamMb = 0;
};

struct ScientificPlan
{
    std::string planId;         ///< ≤ 64 chars (excluded from fingerprint)
    std::string goalId;
    std::string goalKind;       ///< kGoalKinds member
    std::string rulesRevision;  ///< planner_rules table revision that built it
    std::string modeKind;       ///< kModeKinds member the plan was built for
    std::string autonomy;       ///< kAutonomyLevels member
    std::string verdict;        ///< kVerdicts member
    std::vector<std::string> verdictReasons;
    std::vector<PlannerStep> steps;
    std::vector<PlanAlternative> alternatives;
    std::vector<PlanOpenQuestion> openQuestions;
    PlanCost cost;
    std::vector<PlanRisk> risks;
};

/// Canonical JSON projection ("scientific_plan"/"1.0"), byte-deterministic:
/// key-sorted, no whitespace, arrays in plan order.
Json::Value scientificPlanToJson( const ScientificPlan &plan );

/// Fail-closed reader: only "scientific_plan"/"1.0". Unknown vocabularies,
/// duplicate step ids, dangling step-input references, oversized text and
/// wrong typing are typed errors, never coercion or truncation.
bool scientificPlanFromJson( const Json::Value &doc, ScientificPlan &out, std::string &error );

/// SHA-256/16 over the canonical compact JSON of the plan CONTENT
/// (plan_id excluded). Same science ⇒ same fingerprint.
std::string scientificPlanFingerprint( const ScientificPlan &plan );

/// Structural self-consistency of a plan VALUE OBJECT (unique step ids,
/// wiring resolves, closed vocabularies, bounds). Empty = consistent.
std::vector<std::string> validateScientificPlan( const ScientificPlan &plan );

/// Ordered role:operator summary ("import→preprocess:calibrate→…") for
/// logs/goldens; steps without an operator render their role only.
std::string scientificPlanSequenceSummary( const ScientificPlan &plan );

} // namespace sicnu::planner

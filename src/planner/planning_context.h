// src/planner/planning_context.h
#pragma once

//
// RS14-09 Scientific Task Planner — PlanningContext document (slice A).
//
// Versioned, fail-closed document ("planning_context/1.0"): the data the
// caller brings (asset facts), the constraints and budgets that narrow the
// plan, quality requirements, and the consumer mode policy.
//
// Single-truth discipline: `numeric_domain` values are sicnu::contracts
// kNumericDomains members (linked authority); asset lifecycle states mirror
// scientific_state (drift-pinned by tests); family names are kFamilySlots.
//

#include <json/json.h>
#include <string>
#include <vector>

#include "planner/planner_vocab.h"

namespace sicnu::planner {

struct PlannerAssetFacts
{
    std::string ref;         ///< caller-side asset identity, ≤ 64 chars
    std::string kind;        ///< kAssetKinds member
    std::string modality;    ///< kAssetModalities member
    std::string numericDomain; ///< contracts kNumericDomains member
    std::string crs;         ///< authid or "", "" = unknown
    double resolutionM = -1; ///< < 0 = unknown
    std::vector<std::string> bandRoles;
    std::vector<std::string> dates; ///< acquisition dates (ISO), optional
    bool qualityMaskAvailable = false;
    std::string state;           ///< kAssetStates member (declared mirror)
    std::string calibrationState;///< e.g. "raw_dn" / "calibrated", "" = unknown
};

struct PlannerConstraints
{
    int maxSteps = 0;    ///< 0 = unset
    std::vector<std::string> forbiddenOperators; ///< operator ids
    bool requiredDeterminism = false;
    std::vector<std::string> allowedFamilies;    ///< kFamilySlots members; empty = unrestricted
};

struct PlannerQuality
{
    bool requireUncertainty = false;
    bool requireValidationSplit = false;
    double minAccuracy = -1.0; ///< [0,1]; < 0 = unset
};

struct PlannerResourceBudget
{
    int maxSteps = 0;             ///< 0 = unset
    long long maxEstimatedRamMb = 0; ///< 0 = unset
    std::string maxCostClass;     ///< kCostClasses member; "" = unset
};

struct ModePolicy
{
    std::string kind = "standard";     ///< kModeKinds member
    std::string autonomy = "full";     ///< kAutonomyLevels member
    bool studentDecisionDefault = false;
};

struct PlanningContext
{
    std::vector<PlannerAssetFacts> assets;
    PlannerConstraints constraints;
    PlannerQuality quality;
    PlannerResourceBudget resourceBudget;
    ModePolicy mode;
};

/// Canonical JSON projection ("planning_context"/"1.0"), byte-deterministic.
Json::Value planningContextToJson( const PlanningContext &context );

/// Fail-closed reader: only "planning_context"/"1.0"; unknown vocabularies,
/// duplicate asset refs and out-of-bounds values are typed errors.
bool planningContextFromJson( const Json::Value &doc, PlanningContext &out, std::string &error );

/// @returns the context asset with @p ref, or nullptr.
const PlannerAssetFacts *findContextAsset( const PlanningContext &context,
                                           const std::string &ref );

} // namespace sicnu::planner

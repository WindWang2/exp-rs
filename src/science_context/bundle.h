// src/science_context/bundle.h
#pragma once

//
// ScientificContextBundle — exp.science_context.v1
//
// Single versioned, bounded, deterministic projection shared by Agent,
// Planner, and UI. Not a second passport, recipe store, or planner.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::science_context {

inline constexpr const char *kBundleSchemaId = "exp.science_context.v1";

/// Claim evidence buckets projected from passport claims (plus summary).
enum class EvidenceBucket
{
    Known,
    Assumed,
    Unknown,
    Conflicted
};

std::string evidenceBucketToString( EvidenceBucket bucket );
bool evidenceBucketFromString( const std::string &text, EvidenceBucket &out );

struct AssetSummary
{
    std::string assetId;
    std::string revision;
    std::string displayName;
    std::string modality;          ///< optical|sar|thermal|hyperspectral|unknown
    std::string radiometricUnit;   ///< passport vocabulary or ""
    std::string crsAuthid;
    std::vector<std::string> bandRoles; ///< sorted unique non-empty roles
    EvidenceBucket evidence = EvidenceBucket::Unknown;
    std::vector<std::string> evidencePaths; ///< claim paths supporting the bucket
    std::vector<std::string> conflictAlternatives;
    std::string pathHint; ///< basename-only or redacted; never casual absolute paths
};

struct CapabilityEntry
{
    std::string capabilityId;
    std::string intent;
    std::string status; ///< "direct" | "prep" | "unavailable" | "impossible"
    double score = 0.0;
    std::vector<std::string> reasons;      ///< why feasible / why not (typed codes)
    std::vector<std::string> prepActions;  ///< structural prep candidates ≠ executable plan
    bool structuralOnly = true;            ///< always true here — planner owns execution
};

struct RecipeEntry
{
    std::string recipeId;
    std::string title;
    std::string intent;
    std::string modality;
    double score = 0.0;
    int stageCount = 0;
    bool hasHumanOnly = false;
    bool hasVerifierHooks = false;
    std::vector<std::string> matched;
};

struct ContextConstraints
{
    std::string autonomyLevel = "L2"; ///< L0..L5 wire spelling
    bool allowAutonomousExec = false; ///< false unless level >= L5
    bool offline = false;
    int maxBytes = 65536;
    int maxRecipes = 5;
    int maxCapabilities = 8;
    bool determinismRequired = true;
};

struct PlannerProjection
{
    std::string goal;
    std::string intent;
    std::string recipeId;                 ///< optional preferred recipe (never auto-exec)
    Json::Value inputFacts{Json::objectValue}; ///< slot → DatasetUnderstanding-shaped facts
    Json::Value missingFacts{Json::arrayValue};
    Json::Value limitations{Json::arrayValue};
    Json::Value openQuestions{Json::arrayValue};
    bool executionBlocked = false;
    std::string blockReason;
};

struct TruncationMeta
{
    bool truncated = false;
    std::vector<std::string> sections; ///< which sections were trimmed
    int droppedRecipes = 0;
    int droppedCapabilities = 0;
    int droppedQuestions = 0;
    int originalBytes = 0;
    int finalBytes = 0;
};

struct ScientificContextBundle
{
    std::string schemaId = kBundleSchemaId;
    std::string bundleId;   ///< deterministic id from inputs
    std::string goal;
    std::string intent;
    std::vector<AssetSummary> assets;
    std::vector<CapabilityEntry> capabilities;
    std::vector<RecipeEntry> recipes;
    ContextConstraints constraints;
    std::vector<std::string> openQuestions;
    PlannerProjection planner;
    TruncationMeta truncation;
    Json::Value observability{Json::objectValue}; ///< lightweight metrics for Control Center later
};

/// Byte-deterministic serialization (compact, sorted keys via jsoncpp).
Json::Value bundleToJson( const ScientificContextBundle &bundle );
bool bundleFromJson( const Json::Value &json, ScientificContextBundle &out, std::string *error = nullptr );
std::string serializeBundle( const ScientificContextBundle &bundle );

/// Deterministic bundle id: first 16 hex of FNV-1a over canonical payload.
std::string computeBundleId( const ScientificContextBundle &bundle );

} // namespace sicnu::science_context

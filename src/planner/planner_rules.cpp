// src/planner/planner_rules.cpp — the rule table (single definition).
#include "planner/planner_rules.h"

#include <map>

namespace sicnu::planner {

namespace {

// Staged spine per goal kind: import → (alignment gate) → (calibration gate)
// → analyze → verify → publish. Conditional gates are inserted by the core
// only when their condition fires (grid mismatch / numeric-domain bridge).
const std::vector<RuleStage> kCommonSpine = {
    { "import", "data_import", true },
    { "preprocess", "alignment", false },   // gate: cross-scene grid mismatch
    { "preprocess", "calibration", false }, // gate: numeric-domain bridge needed
    { "analyze", "analysis", true },
    { "verify", "verification", false },    // gate: acceptance criteria / quality requirements
    { "publish", "publication", true },
};

const std::map<std::string, std::vector<std::string>> kAnalysisOutputs = {
    { "measurement", { "index", "probability", "count", "table" } },
    { "classification", { "classes", "none" } },
    { "change", { "none", "classes", "mask", "index" } },
    { "monitoring", { "index", "count", "table", "classes" } },
    { "detection", { "probability", "mask", "classes" } },
    { "temporal_analysis", { "index", "features", "table", "count", "classes" } },
    { "map_product", { "classes", "index", "mask", "features" } },
};

} // namespace

const std::vector<RuleStage> &ruleStagesForGoalKind( const std::string &goalKind )
{
    // Every known goal kind currently shares the common spine; the map keyed
    // lookup keeps per-kind divergence possible without reshaping callers.
    static const std::map<std::string, std::vector<RuleStage>> kSpines = {
        { "measurement", kCommonSpine },  { "classification", kCommonSpine },
        { "change", kCommonSpine },       { "monitoring", kCommonSpine },
        { "detection", kCommonSpine },    { "temporal_analysis", kCommonSpine },
        { "map_product", kCommonSpine },
    };
    static const std::vector<RuleStage> kEmpty;
    const auto it = kSpines.find( goalKind );
    return it == kSpines.end() ? kEmpty : it->second;
}

const std::vector<std::string> &allowedAnalysisOutputsForGoalKind( const std::string &goalKind )
{
    static const std::vector<std::string> kNone;
    const auto it = kAnalysisOutputs.find( goalKind );
    return it == kAnalysisOutputs.end() ? kNone : it->second;
}

std::string stageRationale( const std::string &goalKind, const std::string &role,
                            const std::string &family )
{
    if ( role == "import" )
        return "bring the declared assets into the platform before any science runs";
    if ( role == "preprocess" && family == "alignment" )
        return "scenes on different grids cannot be compared lawfully; align before analysis";
    if ( role == "preprocess" && family == "calibration" )
        return "bridge the numeric domain so the analysis consumes the data scale it requires";
    if ( role == "analyze" )
        return "the " + goalKind + " core: produce the measured/derived scientific product";
    if ( role == "verify" )
        return "check the product against the goal's acceptance criteria before publishing";
    if ( role == "publish" )
        return "publish the product so downstream consumers see one stable result";
    if ( role == "record" )
        return "record the scientific steps for reproducibility";
    return "stage " + role + " (" + family + ") for a " + goalKind + " goal";
}

} // namespace sicnu::planner

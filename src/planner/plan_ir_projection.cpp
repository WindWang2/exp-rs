// src/planner/plan_ir_projection.cpp
#include "planner/plan_ir_projection.h"

#include "contracts/scientific_contract.h"
#include "planner/json_util.h"

#include <algorithm>
#include <map>

namespace sicnu::planner {

namespace {

/// The one contracts→artifact_facts map. Keys are the FULL contracts
/// kNumericDomains vocabulary (drift-pinned by test_scientific_planner_drift;
/// adding a contracts domain without a map entry fails the drift test).
/// `token` "" = no raster-surface fact to claim; token "unknown" = honest
/// degradation (carries a projection warning).
struct DomainMapping
{
    const char *token;
    const char *warning; // non-empty → degradation warning when projected
};

const std::map<std::string, DomainMapping> &domainMap()
{
    static const std::map<std::string, DomainMapping> kMap = {
        { "none", { "", nullptr } },
        { "any", { "", nullptr } },
        { "dn", { "dn", nullptr } },
        { "reflectance", { "surface_reflectance", nullptr } },
        { "radiance", { "unknown", "radiance has no honest artifact_facts token" } },
        { "temperature", { "unknown", "temperature has no honest artifact_facts token" } },
        { "amplitude", { "unknown", "amplitude has no honest artifact_facts token" } },
        { "sigma0", { "linear_power", nullptr } },
        { "gamma0", { "linear_power", nullptr } },
        { "beta0", { "linear_power", nullptr } },
        { "phase", { "unknown", "interferometric phase has no honest artifact_facts token" } },
        { "displacement", { "unknown", "displacement has no honest artifact_facts token" } },
        { "db", { "db", nullptr } },
        { "index", { "index", nullptr } },
        { "probability", { "unknown", "probability has no honest artifact_facts token" } },
        { "mask", { "masked", nullptr } },
        { "classes", { "categorical", nullptr } },
        { "features", { "unknown", "feature stacks have no honest artifact_facts token" } },
        { "count", { "unknown", "counts have no honest artifact_facts token" } },
        { "vector", { "unknown", "vector surfaces have no raster domain token" } },
        { "table", { "unknown", "tabular surfaces have no raster domain token" } },
    };
    return kMap;
}

std::string intentForGoalKind( const std::string &goalKind, std::vector<std::string> *warnings )
{
    // Honest mapping: only goal kinds whose science the harness intent
    // vocabulary actually names get an intent; the rest stay "" (custom)
    // WITH a warning instead of pretending a specific preflight applies.
    static const std::map<std::string, std::string> kIntentMap = {
        { "change", "change" },
        { "classification", "classify" },
        { "temporal_analysis", "temporal" },
        { "monitoring", "temporal" },
    };
    static const std::map<std::string, std::string> kWhyNot = {
        { "measurement", "measurement goals are quantity-specific (ndvi/evi/…); the plan does "
                         "not claim one" },
        { "detection", "detection goals are target-specific; the plan does not claim one" },
        { "map_product", "map products are composition-specific; the plan does not claim one" },
    };
    const auto it = kIntentMap.find( goalKind );
    if ( it != kIntentMap.end() )
        return it->second;
    const auto why = kWhyNot.find( goalKind );
    if ( warnings && why != kWhyNot.end() )
        warnings->push_back( why->second );
    return "";
}

} // namespace

std::string artifactFactsTokenForDomain( const std::string &contractsDomain )
{
    const auto it = domainMap().find( contractsDomain );
    return it == domainMap().end() ? "" : it->second.token;
}

bool domainProjectsToUnknown( const std::string &contractsDomain )
{
    const auto it = domainMap().find( contractsDomain );
    return it != domainMap().end() && std::string( it->second.token ) == "unknown";
}

std::string harnessIntentForGoalKind( const std::string &goalKind )
{
    return intentForGoalKind( goalKind, nullptr );
}

std::vector<std::string> knownProjectionDomains()
{
    std::vector<std::string> domains;
    domains.reserve( domainMap().size() );
    for ( const auto &entry : domainMap() )
        domains.push_back( entry.first );
    return domains;
}

Json::Value projectPlanToIr( const ScientificPlan &plan, std::vector<std::string> *warnings,
                             std::string *error )
{
    const auto fail = [&]( const std::string &message )
    {
        if ( error )
            *error = "unsupported_projection: " + message;
        return Json::Value( Json::nullValue );
    };

    const auto problems = validateScientificPlan( plan );
    if ( !problems.empty() )
        return fail( "plan failed structural validation: " + problems.front() );
    if ( plan.steps.empty() )
        return fail( "a plan without steps has no state basis to project" );

    std::vector<std::string> localWarnings;

    Json::Value doc( Json::objectValue );
    doc["kind"] = "workflow_ir";
    doc["schema_version"] = "1.0";
    doc["ir_id"] = "wir-" + scientificPlanFingerprint( plan );
    doc["goal"] = plan.goalId;
    doc["intent"] = intentForGoalKind( plan.goalKind, &localWarnings );

    // ---- document input slots (one per referenced asset) ----------------
    Json::Value inputs( Json::arrayValue );
    std::map<std::string, std::string> slotForAsset; // assetRef → slot name
    for ( const auto &step : plan.steps )
    {
        for ( const auto &input : step.inputs )
        {
            if ( input.assetRef.empty() || slotForAsset.count( input.assetRef ) )
                continue;
            Json::Value slot( Json::objectValue );
            const std::string slotName = "in-" + input.assetRef;
            slot["name"] = slotName;
            slot["reference"] = input.assetRef;
            inputs.append( slot );
            slotForAsset[input.assetRef] = slotName;
        }
    }
    if ( !inputs.empty() )
        doc["inputs"] = inputs;

    // ---- nodes --------------------------------------------------------------
    Json::Value nodes( Json::arrayValue );
    for ( const auto &step : plan.steps )
    {
        if ( step.operatorId.empty() && step.expectedTransitions.empty() )
            return fail( "step \"" + step.stepId
                         + "\" has no state basis (no operator and no expected transitions)" );
        Json::Value node( Json::objectValue );
        node["id"] = step.stepId;
        node["operator"] = step.operatorId;
        node["params"] = step.params;

        Json::Value nodeInputs( Json::arrayValue );
        for ( const auto &input : step.inputs )
        {
            Json::Value wire( Json::objectValue );
            if ( !input.fromStepId.empty() )
            {
                wire["node"] = input.fromStepId;
                wire["output"] = "output";
            }
            else
            {
                wire["input"] = slotForAsset[input.assetRef];
            }
            wire["as"] = input.as;
            nodeInputs.append( wire );
        }
        if ( !nodeInputs.empty() )
            node["inputs"] = nodeInputs;

        Json::Value outputs( Json::arrayValue );
        Json::Value outputPort( Json::objectValue );
        outputPort["name"] = "output";
        Json::Value artifact( Json::objectValue );
        for ( const auto &transition : step.expectedTransitions )
        {
            const std::string token = artifactFactsTokenForDomain( transition.toDomain );
            if ( token.empty() )
                continue;
            artifact["numeric_domain"] = token;
            if ( domainProjectsToUnknown( transition.toDomain ) )
            {
                const std::string warning = std::string(
                                                domainMap().at( transition.toDomain ).warning )
                                            + " — projected as unknown; downstream checks degrade";
                if ( std::find( localWarnings.begin(), localWarnings.end(), warning )
                     == localWarnings.end() )
                    localWarnings.push_back( warning ); // dedupe repeated degradations
            }
        }
        if ( !artifact.empty() )
            outputPort["artifact"] = artifact;
        outputs.append( outputPort );
        node["outputs"] = outputs;

        node["verification"] = step.family == "verification" ? "raster" : "";
        node["resource_estimate_mb"] = Json::Value::Int64( step.estimatedRamMb );
        node["device"] = "cpu";
        node["determinism"] = step.deterministic ? "" : "stochastic";
        node["semantic_output"] = step.role + " of " + plan.goalKind + " for " + plan.goalId;
        node["source"] = "planner:" + plan.planId;
        nodes.append( node );
    }
    doc["nodes"] = nodes;

    // ---- declared outputs (publication steps) --------------------------------
    Json::Value declaredOutputs( Json::arrayValue );
    for ( const auto &step : plan.steps )
    {
        if ( step.role != "publish" )
            continue;
        Json::Value decl( Json::objectValue );
        decl["name"] = "published-" + plan.goalId;
        decl["node"] = step.stepId;
        decl["port"] = "output";
        decl["semantic"] = "published product of " + plan.goalId;
        declaredOutputs.append( decl );
    }
    if ( !declaredOutputs.empty() )
        doc["outputs"] = declaredOutputs;

    Json::Value expectations( Json::objectValue );
    expectations["max_ram_mb"] = Json::Value::Int64( plan.cost.totalEstimatedRamMb );
    expectations["deterministic"] =
        plan.risks.empty()
        || std::none_of( plan.risks.begin(), plan.risks.end(),
                         []( const PlanRisk &risk ) {
                             return risk.kind == "stochastic_operator";
                         } );
    doc["expectations"] = expectations;

    if ( warnings )
        warnings->insert( warnings->end(), localWarnings.begin(), localWarnings.end() );
    if ( error )
        error->clear();
    return doc;
}

} // namespace sicnu::planner

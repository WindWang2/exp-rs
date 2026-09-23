// src/planner/planning_context.cpp
#include "planner/planning_context.h"

#include "planner/json_util.h"

#include <algorithm>
#include <set>

namespace sicnu::planner {

namespace {
using json_util::error;
constexpr size_t kIdMax = PlanLimits::kMaxIdChars;
constexpr size_t kTextMax = PlanLimits::kMaxTextChars;

bool readAsset( const Json::Value &entry, PlannerAssetFacts &asset, std::string &err )
{
    if ( !entry.isObject() )
    {
        err = json_util::error( "invalid_field", "asset entry must be an object" );
        return false;
    }
    if ( !json_util::readBoundedString( entry, "ref", kIdMax, asset.ref, err ) )
        return false;
    if ( !json_util::readBoundedString( entry, "asset_kind", kIdMax, asset.kind, err ) )
        return false;
    if ( !isKnownAssetKind( asset.kind ) )
    {
        err = json_util::error( "invalid_field", "unknown asset kind \"" + asset.kind + "\"" );
        return false;
    }
    if ( entry.isMember( "modality" ) )
    {
        if ( !json_util::readBoundedString( entry, "modality", kIdMax, asset.modality, err ) )
            return false;
        if ( !isKnownVocabValue( kAssetModalities, asset.modality ) )
        {
            err = json_util::error( "invalid_field", "unknown modality \"" + asset.modality + "\"" );
            return false;
        }
    }
    if ( entry.isMember( "numeric_domain" ) )
    {
        if ( !json_util::readBoundedString( entry, "numeric_domain", kIdMax,
                                            asset.numericDomain, err ) )
            return false;
        if ( !isKnownContractsNumericDomain( asset.numericDomain ) )
        {
            err = json_util::error( "invalid_field",
                         "unknown numeric domain \"" + asset.numericDomain
                             + "\" (authority: contracts kNumericDomains)" );
            return false;
        }
    }
    if ( !json_util::readOptionalBoundedString( entry, "crs", PlanLimits::kMaxTextChars,
                                                asset.crs, err ) )
        return false;
    if ( entry.isMember( "resolution_m" ) )
    {
        if ( !entry["resolution_m"].isNumeric() )
        {
            err = json_util::error( "invalid_field", "resolution_m must be a number" );
            return false;
        }
        asset.resolutionM = entry["resolution_m"].asDouble();
    }
    if ( entry.isMember( "band_roles" ) )
    {
        if ( !entry["band_roles"].isArray() )
        {
            err = json_util::error( "invalid_field", "band_roles must be an array" );
            return false;
        }
        for ( const auto &role : entry["band_roles"] )
        {
            if ( !role.isString() || role.asString().empty()
                 || role.asString().size() > kIdMax )
            {
                err = json_util::error( "invalid_field", "band_roles entries must be non-empty strings" );
                return false;
            }
            asset.bandRoles.push_back( role.asString() );
        }
    }
    if ( entry.isMember( "dates" ) )
    {
        if ( !entry["dates"].isArray() )
        {
            err = json_util::error( "invalid_field", "dates must be an array" );
            return false;
        }
        for ( const auto &date : entry["dates"] )
        {
            if ( !date.isString() || date.asString().empty() || date.asString().size() > kIdMax )
            {
                err = json_util::error( "invalid_field", "dates entries must be non-empty strings" );
                return false;
            }
            asset.dates.push_back( date.asString() );
        }
    }
    if ( entry.isMember( "quality_mask_available" ) )
    {
        if ( !entry["quality_mask_available"].isBool() )
        {
            err = json_util::error( "invalid_field", "quality_mask_available must be a bool" );
            return false;
        }
        asset.qualityMaskAvailable = entry["quality_mask_available"].asBool();
    }
    if ( entry.isMember( "state" ) )
    {
        if ( !json_util::readBoundedString( entry, "state", kIdMax, asset.state, err ) )
            return false;
        if ( !isKnownAssetState( asset.state ) )
        {
            err = json_util::error( "invalid_field", "unknown asset state \"" + asset.state + "\"" );
            return false;
        }
    }
    if ( !json_util::readOptionalBoundedString( entry, "calibration_state", kTextMax,
                                                asset.calibrationState, err ) )
        return false;
    if ( asset.state.empty() )
        asset.state = "unknown";
    return true;
}

bool readStringList( const Json::Value &parent, const std::string &key, size_t maxChars,
                     std::vector<std::string> &out, std::string &err )
{
    if ( !parent.isMember( key ) )
        return true;
    if ( !parent[key].isArray() )
    {
        err = json_util::error( "invalid_field", key + " must be an array" );
        return false;
    }
    for ( const auto &entry : parent[key] )
    {
        if ( !entry.isString() || entry.asString().empty() || entry.asString().size() > maxChars )
        {
            err = json_util::error( "invalid_field", key + " entries must be non-empty bounded strings" );
            return false;
        }
        out.push_back( entry.asString() );
    }
    // Deterministic canonical order regardless of document order.
    std::sort( out.begin(), out.end() );
    return true;
}
} // namespace

Json::Value planningContextToJson( const PlanningContext &context )
{
    Json::Value doc( Json::objectValue );
    doc["kind"] = kContextEnvelopeKind;
    doc["schema_version"] = kSchemaVersion;

    Json::Value assets( Json::arrayValue );
    for ( const auto &asset : context.assets )
    {
        Json::Value entry( Json::objectValue );
        entry["ref"] = asset.ref;
        entry["asset_kind"] = asset.kind;
        if ( !asset.modality.empty() )
            entry["modality"] = asset.modality;
        if ( !asset.numericDomain.empty() )
            entry["numeric_domain"] = asset.numericDomain;
        if ( !asset.crs.empty() )
            entry["crs"] = asset.crs;
        if ( asset.resolutionM >= 0 )
            entry["resolution_m"] = asset.resolutionM;
        if ( !asset.bandRoles.empty() )
        {
            Json::Value roles( Json::arrayValue );
            for ( const auto &role : asset.bandRoles )
                roles.append( role );
            entry["band_roles"] = roles;
        }
        if ( !asset.dates.empty() )
        {
            Json::Value dates( Json::arrayValue );
            for ( const auto &date : asset.dates )
                dates.append( date );
            entry["dates"] = dates;
        }
        entry["quality_mask_available"] = asset.qualityMaskAvailable;
        entry["state"] = asset.state;
        if ( !asset.calibrationState.empty() )
            entry["calibration_state"] = asset.calibrationState;
        assets.append( entry );
    }
    doc["assets"] = assets;

    Json::Value constraints( Json::objectValue );
    if ( context.constraints.maxSteps > 0 )
        constraints["max_steps"] = context.constraints.maxSteps;
    if ( !context.constraints.forbiddenOperators.empty() )
    {
        Json::Value forbidden( Json::arrayValue );
        for ( const auto &op : context.constraints.forbiddenOperators )
            forbidden.append( op );
        constraints["forbidden_operators"] = forbidden;
    }
    if ( context.constraints.requiredDeterminism )
        constraints["required_determinism"] = true;
    if ( !context.constraints.allowedFamilies.empty() )
    {
        Json::Value families( Json::arrayValue );
        for ( const auto &family : context.constraints.allowedFamilies )
            families.append( family );
        constraints["allowed_families"] = families;
    }
    doc["constraints"] = constraints;

    Json::Value quality( Json::objectValue );
    if ( context.quality.requireUncertainty )
        quality["require_uncertainty"] = true;
    if ( context.quality.requireValidationSplit )
        quality["require_validation_split"] = true;
    if ( context.quality.minAccuracy >= 0 )
        quality["min_accuracy"] = context.quality.minAccuracy;
    doc["quality"] = quality;

    Json::Value budget( Json::objectValue );
    if ( context.resourceBudget.maxSteps > 0 )
        budget["max_steps"] = context.resourceBudget.maxSteps;
    if ( context.resourceBudget.maxEstimatedRamMb > 0 )
        budget["max_estimated_ram_mb"] = Json::Value::Int64(
            context.resourceBudget.maxEstimatedRamMb );
    if ( !context.resourceBudget.maxCostClass.empty() )
        budget["max_cost_class"] = context.resourceBudget.maxCostClass;
    doc["resource_budget"] = budget;

    Json::Value mode( Json::objectValue );
    mode["kind"] = context.mode.kind;
    mode["autonomy"] = context.mode.autonomy;
    mode["student_decision_default"] = context.mode.studentDecisionDefault;
    doc["mode"] = mode;
    return doc;
}

bool planningContextFromJson( const Json::Value &doc, PlanningContext &out, std::string &error )
{
    out = PlanningContext{};
    if ( !json_util::checkEnvelope( doc, kContextEnvelopeKind, error ) )
        return false;

    if ( !doc.isMember( "assets" ) || !doc["assets"].isArray() )
    {
        error = json_util::error( "invalid_field", "assets must be an array" );
        return false;
    }
    if ( static_cast<int>( doc["assets"].size() ) > PlanLimits::kMaxAssets )
    {
        error = json_util::error( "out_of_bounds", "assets over "
                                            + std::to_string( PlanLimits::kMaxAssets )
                                            + " entries" );
        return false;
    }
    std::set<std::string> seenRefs;
    for ( const auto &entry : doc["assets"] )
    {
        PlannerAssetFacts asset;
        if ( !readAsset( entry, asset, error ) )
            return false;
        if ( !seenRefs.insert( asset.ref ).second )
        {
            error = json_util::error( "invalid_field", "duplicate asset ref \"" + asset.ref + "\"" );
            return false;
        }
        out.assets.push_back( asset );
    }

    const Json::Value empty = Json::Value( Json::objectValue );
    const Json::Value &constraints = doc.get( "constraints", empty );
    if ( !constraints.isNull() )
    {
        if ( !constraints.isObject() )
        {
            error = json_util::error( "invalid_field", "constraints must be an object" );
            return false;
        }
        if ( constraints.isMember( "max_steps" ) )
        {
            if ( !constraints["max_steps"].isInt() || constraints["max_steps"].asInt() < 0
                 || constraints["max_steps"].asInt() > PlanLimits::kMaxSteps )
            {
                error = json_util::error( "out_of_bounds", "constraints.max_steps must be within [0,64]" );
                return false;
            }
            out.constraints.maxSteps = constraints["max_steps"].asInt();
        }
        if ( !readStringList( constraints, "forbidden_operators", kIdMax,
                              out.constraints.forbiddenOperators, error ) )
            return false;
        if ( constraints.isMember( "required_determinism" ) )
        {
            if ( !constraints["required_determinism"].isBool() )
            {
                error = json_util::error( "invalid_field", "required_determinism must be a bool" );
                return false;
            }
            out.constraints.requiredDeterminism = constraints["required_determinism"].asBool();
        }
        if ( constraints.isMember( "allowed_families" ) )
        {
            if ( !readStringList( constraints, "allowed_families", kIdMax,
                                  out.constraints.allowedFamilies, error ) )
                return false;
            for ( const auto &family : out.constraints.allowedFamilies )
            {
                if ( !isKnownFamilySlot( family ) )
                {
                    error = json_util::error( "invalid_field",
                                   "unknown family slot \"" + family + "\"" );
                    return false;
                }
            }
        }
    }

    const Json::Value &quality = doc.get( "quality", empty );
    if ( !quality.isNull() )
    {
        if ( !quality.isObject() )
        {
            error = json_util::error( "invalid_field", "quality must be an object" );
            return false;
        }
        if ( quality.isMember( "require_uncertainty" ) )
        {
            if ( !quality["require_uncertainty"].isBool() )
            {
                error = json_util::error( "invalid_field", "require_uncertainty must be a bool" );
                return false;
            }
            out.quality.requireUncertainty = quality["require_uncertainty"].asBool();
        }
        if ( quality.isMember( "require_validation_split" ) )
        {
            if ( !quality["require_validation_split"].isBool() )
            {
                error = json_util::error( "invalid_field", "require_validation_split must be a bool" );
                return false;
            }
            out.quality.requireValidationSplit = quality["require_validation_split"].asBool();
        }
        if ( quality.isMember( "min_accuracy" ) )
        {
            if ( !quality["min_accuracy"].isNumeric()
                 || quality["min_accuracy"].asDouble() < 0
                 || quality["min_accuracy"].asDouble() > 1 )
            {
                error = json_util::error( "out_of_bounds", "min_accuracy must be within [0,1]" );
                return false;
            }
            out.quality.minAccuracy = quality["min_accuracy"].asDouble();
        }
    }

    const Json::Value &budget = doc.get( "resource_budget", empty );
    if ( !budget.isNull() && budget.isObject() )
    {
        if ( budget.isMember( "max_steps" ) )
        {
            if ( !budget["max_steps"].isInt() || budget["max_steps"].asInt() < 0
                 || budget["max_steps"].asInt() > PlanLimits::kMaxSteps )
            {
                error = json_util::error( "out_of_bounds", "resource_budget.max_steps must be within [0,64]" );
                return false;
            }
            out.resourceBudget.maxSteps = budget["max_steps"].asInt();
        }
        if ( budget.isMember( "max_estimated_ram_mb" ) )
        {
            if ( !budget["max_estimated_ram_mb"].isInt64()
                 || budget["max_estimated_ram_mb"].asInt64() < 0 )
            {
                error = json_util::error( "out_of_bounds", "max_estimated_ram_mb must be non-negative" );
                return false;
            }
            out.resourceBudget.maxEstimatedRamMb = budget["max_estimated_ram_mb"].asInt64();
        }
        if ( budget.isMember( "max_cost_class" ) )
        {
            if ( !json_util::readBoundedString( budget, "max_cost_class", kIdMax,
                                                out.resourceBudget.maxCostClass, error ) )
                return false;
            if ( !isKnownCostClass( out.resourceBudget.maxCostClass ) )
            {
                error = json_util::error( "invalid_field",
                               "unknown cost class \"" + out.resourceBudget.maxCostClass + "\"" );
                return false;
            }
        }
    }

    const Json::Value &mode = doc.get( "mode", empty );
    if ( !mode.isNull() && mode.isObject() )
    {
        if ( mode.isMember( "kind" ) )
        {
            if ( !json_util::readBoundedString( mode, "kind", kIdMax, out.mode.kind, error ) )
                return false;
            if ( !isKnownModeKind( out.mode.kind ) )
            {
                error = json_util::error( "invalid_field", "unknown mode kind \"" + out.mode.kind + "\"" );
                return false;
            }
        }
        if ( mode.isMember( "autonomy" ) )
        {
            if ( !json_util::readBoundedString( mode, "autonomy", kIdMax, out.mode.autonomy,
                                                error ) )
                return false;
            if ( !isKnownAutonomy( out.mode.autonomy ) )
            {
                error = json_util::error( "invalid_field", "unknown autonomy \"" + out.mode.autonomy + "\"" );
                return false;
            }
        }
        if ( mode.isMember( "student_decision_default" ) )
        {
            if ( !mode["student_decision_default"].isBool() )
            {
                error = json_util::error( "invalid_field", "student_decision_default must be a bool" );
                return false;
            }
            out.mode.studentDecisionDefault = mode["student_decision_default"].asBool();
        }
    }
    return true;
}

const PlannerAssetFacts *findContextAsset( const PlanningContext &context,
                                           const std::string &ref )
{
    for ( const auto &asset : context.assets )
    {
        if ( asset.ref == ref )
            return &asset;
    }
    return nullptr;
}

} // namespace sicnu::planner

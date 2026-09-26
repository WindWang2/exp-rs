#include "preflight/runtime_adapter.h"

#include "preflight/engine.h"
#include "preflight/render.h"
#include "preflight/rules.h"

#include <json/json.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::preflight {
namespace {

Json::Value checkError( const std::string &code, const std::string &detail )
{
    Json::Value out( Json::objectValue );
    out["kind"] = "sicnu.preflight.check_error/1";
    out["code"] = code;
    out["detail"] = detail;
    return out;
}

/// The one-report-three-projections envelope: report, teaching and agent all
/// derive from the same PreflightReport object.
Json::Value renderCheckResult( const PreflightReport &report )
{
    Json::Value out( Json::objectValue );
    out["kind"] = "sicnu.preflight.check/1";
    out["report"] = report.toJson();
    out["teaching"] = renderTeaching( report );
    out["agent"] = renderAgent( report );
    return out;
}

/// One requested input slot: {"slot": "...", "ref": "..."}; slot defaults to
/// the asset ref when absent (mirrors the request contract where the engine
/// re-stamps the slot name).
bool parseInput( const Json::Value &item, std::pair<std::string, std::string> &out,
                 std::string &detail )
{
    if ( !item.isObject() )
    {
        detail = "each input must be an object {slot?, ref}";
        return false;
    }
    if ( !item["ref"].isString() || item["ref"].asString().empty() )
    {
        detail = "input.ref must be a non-empty string";
        return false;
    }
    out.second = item["ref"].asString();
    out.first = item["slot"].isString() && !item["slot"].asString().empty()
                    ? item["slot"].asString()
                    : out.second;
    return true;
}

} // namespace

AuthorityCapabilityProvider::AuthorityCapabilityProvider( Lookup lookup,
                                                          std::vector<std::string> loadProblems )
    : lookup_( std::move( lookup ) ), loadProblems_( std::move( loadProblems ) )
{
}

CapabilityEntryResult AuthorityCapabilityProvider::entryForOperator(
    const std::string &operatorId, const Json::Value &variantParams ) const
{
    CapabilityEntryResult result;
    if ( !lookup_ )
    {
        result.status = FactStatus::Unavailable;
        result.detail = "no capability authority wired";
        return result;
    }
    const Json::Value entry = lookup_( operatorId, variantParams );
    if ( entry.isObject() )
    {
        result.status = FactStatus::Available;
        result.entry = entry;
        return result;
    }
    if ( !loadProblems_.empty() )
    {
        result.status = FactStatus::Unavailable;
        result.detail = "capability authority failed to load: " + loadProblems_.front();
        return result;
    }
    result.status = FactStatus::Unknown;
    result.detail = "operator not declared in the capability authority";
    return result;
}

TemporalFactsProvider::TemporalFactsProvider( const IAssetFactsProvider &inner,
                                              TemporalFactsLookup lookup, int cap )
    : inner_( &inner ), lookup_( std::move( lookup ) ), cap_( cap > 0 ? cap : 512 )
{
}

SlotFactsResult TemporalFactsProvider::slotFacts( const std::string &assetRef ) const
{
    SlotFactsResult result = inner_->slotFacts( assetRef );
    if ( result.status != FactStatus::Available )
        return result;
    SlotFacts &facts = result.facts;
    if ( facts.temporalCollectionRefs.empty() || !lookup_ )
        return result;

    // First resolvable declared reference (refs are stored sorted); the
    // others are never merged in implicitly.
    for ( const auto &collectionId : facts.temporalCollectionRefs )
    {
        const TemporalCollectionFacts collection = lookup_( collectionId );
        if ( collection.status != FactStatus::Available )
            continue;
        facts.temporalSceneCount = collection.sceneCount;
        facts.temporalInvalidTimeCount = collection.invalidTimeScenes;
        facts.temporalTruncated = collection.truncated;
        facts.temporalDates.clear();
        for ( const auto &date : collection.datesIso )
        {
            if ( static_cast<int>( facts.temporalDates.size() ) >= cap_ )
            {
                facts.temporalTruncated = true;
                break;
            }
            facts.temporalDates.push_back( date );
        }
        return result;
    }
    // No declared collection resolved: keep the typed temporal unknowns the
    // rules report (never fabricate counts from nothing).
    return result;
}

Json::Value preflightCheckJson( const Json::Value &args, const IAssetFactsProvider &assetFacts,
                                const ICapabilityProvider &capability,
                                const TemporalFactsLookup *temporal )
{
    if ( !args.isObject() )
        return checkError( "invalid_arguments", "arguments must be an object" );
    if ( !args["operator"].isString() || args["operator"].asString().empty() )
        return checkError( "invalid_arguments", "operator must be a non-empty string" );
    if ( !args["inputs"].isArray() || args["inputs"].empty() )
        return checkError( "invalid_arguments", "inputs must be a non-empty array" );

    PreflightRequest request;
    request.operatorId = args["operator"].asString();
    // A malformed operator_params would silently drop the variant selector —
    // the engine would judge the BASE entry while the caller meant a variant
    // (a forged pass). Present-but-wrong-type is a typed error, never a
    // degraded judgment; absent/null keeps the defaults.
    if ( !args["operator_params"].isNull() && !args["operator_params"].isObject() )
        return checkError( "invalid_arguments",
                           "operator_params must be an object (variant selector)" );
    if ( args["operator_params"].isObject() )
        request.operatorParams = args["operator_params"];
    if ( !args["intent"].isNull() && !args["intent"].isString() )
        return checkError( "invalid_arguments", "intent must be a string" );
    if ( args["intent"].isString() )
        request.intent = args["intent"].asString();
    // An invalid mode STRING is the engine's SPF_REQUEST_INVALID block, not a
    // tool error: the request is judgeable enough to be reported honestly.
    // A non-string mode, though, would silently flip the report vocabulary.
    if ( !args["mode"].isNull() && !args["mode"].isString() )
        return checkError( "invalid_arguments", "mode must be a string" );
    if ( args["mode"].isString() )
        request.mode = args["mode"].asString();
    else
        request.mode = "agent";
    if ( !args["human_operator_id"].isNull() && !args["human_operator_id"].isString() )
        return checkError( "invalid_arguments", "human_operator_id must be a string" );
    if ( args["human_operator_id"].isString() )
        request.humanOperatorId = args["human_operator_id"].asString();

    for ( const auto &item : args["inputs"] )
    {
        std::pair<std::string, std::string> input;
        std::string detail;
        if ( !parseInput( item, input, detail ) )
            return checkError( "invalid_arguments", detail );
        request.inputs.push_back( std::move( input ) );
    }
    if ( args["acknowledgements"].isArray() )
    {
        for ( const auto &code : args["acknowledgements"] )
            if ( !code.isString() )
                return checkError( "invalid_arguments",
                                   "acknowledgements must be an array of strings" );
        for ( const auto &code : args["acknowledgements"] )
            request.acknowledgements.push_back( code.asString() );
    }
    else if ( !args["acknowledgements"].isNull() )
    {
        return checkError( "invalid_arguments", "acknowledgements must be an array of codes" );
    }
    if ( args["budgets"].isObject() )
    {
        const std::optional<PreflightBudgets> budgets = PreflightBudgets::fromJson( args["budgets"] );
        if ( !budgets )
            return checkError( "invalid_arguments", "budgets do not parse" );
        request.budgets = *budgets;
    }

    PreflightEngine engine;
    for ( auto &rule : builtinRules() )
    {
        if ( engine.registerRule( std::move( rule ) ) != RegistrationResult::Ok )
            return checkError( "engine_error", engine.registrationError() );
    }

    // Enrich facts through the temporal provider when a collection lookup is
    // wired; the inner provider stays the single passport authority.
    if ( temporal )
    {
        const TemporalFactsProvider enriched( assetFacts, *temporal );
        return renderCheckResult( engine.evaluate( request, enriched, capability ) );
    }
    return renderCheckResult( engine.evaluate( request, assetFacts, capability ) );
}

} // namespace sicnu::preflight

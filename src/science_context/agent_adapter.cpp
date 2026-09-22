// src/science_context/agent_adapter.cpp
#include "science_context/agent_adapter.h"
#include "science_context/capability_router.h"
#include "science_context/recipe_router.h"

#include "science_context/observed_state.h"
#include "scientific_state/asset_state_json.h"

#include <stdexcept>

namespace sicnu::science_context::agent_adapter {

using ::sicnu::science_context::CapabilityQuery;
using ::sicnu::science_context::routeCapabilities;
using ::sicnu::science_context::RecipeQuery;
using ::sicnu::science_context::AssetResolveRequest;
using ::sicnu::science_context::AssetResolveResult;
using ::sicnu::science_context::AssetStateProvider;
using ::sicnu::science_context::evidenceBucketToString;
using ::sicnu::science_context::observedStateFromPassport;
using ::sicnu::science_context::bundleToJson;
using ::sicnu::science_context::planningContextToCompileRequest;
using ::sicnu::science_context::kBundleSchemaId;
using ::sicnu::science_context::SynthesizeRequest;

namespace {

ScienceContextBroker *gTestBroker = nullptr;
ScienceContextBroker &defaultBroker()
{
    static ScienceContextBroker broker;
    return broker;
}

Json::Value requireObject( const Json::Value &args )
{
    if ( !args.isObject() )
        throw std::runtime_error( "arguments must be a JSON object" );
    return args;
}

sicnu::state::RemoteSensingAssetState passportFromArgs( const Json::Value &args )
{
    sicnu::state::RemoteSensingAssetState state;
    if ( args.isMember( "passport" ) )
    {
        sicnu::state::AssetStateError err;
        if ( !sicnu::state::assetStateFromJson( args["passport"], state, err ) )
            throw std::runtime_error( "passport_invalid:" + err.message );
        return state;
    }
    if ( args.isMember( "passport_json" ) && args["passport_json"].isString() )
    {
        sicnu::state::AssetStateError err;
        if ( !sicnu::state::assetStateFromJson( args["passport_json"].asString(), state, err ) )
            throw std::runtime_error( "passport_invalid:" + err.message );
        return state;
    }
    throw std::runtime_error( "passport_or_passport_json_required" );
}

} // namespace

ScienceContextBroker &sharedBroker()
{
    if ( gTestBroker )
        return *gTestBroker;
    return defaultBroker();
}

void setSharedBrokerForTest( ScienceContextBroker *broker )
{
    gTestBroker = broker;
}

Json::Value scientificContext( const Json::Value &argsIn )
{
    const Json::Value args = requireObject( argsIn );
    SynthesizeRequest req;
    req.goal = args.get( "goal", "" ).asString();
    req.intent = args.get( "intent", "" ).asString();
    if ( args.isMember( "asset_keys" ) && args["asset_keys"].isArray() )
    {
        for ( const auto &k : args["asset_keys"] )
        {
            if ( k.isString() )
                req.assetKeys.push_back( k.asString() );
        }
    }
    if ( args.isMember( "asset_id" ) && args["asset_id"].isString() &&
         !args["asset_id"].asString().empty() )
        req.assetKeys.push_back( args["asset_id"].asString() );
    if ( args.isMember( "passport" ) || args.isMember( "passport_json" ) )
        req.passports.push_back( passportFromArgs( args ) );
    if ( args.isMember( "passports" ) && args["passports"].isArray() )
    {
        for ( const auto &p : args["passports"] )
        {
            sicnu::state::RemoteSensingAssetState state;
            sicnu::state::AssetStateError err;
            if ( !sicnu::state::assetStateFromJson( p, state, err ) )
                throw std::runtime_error( "passport_invalid:" + err.message );
            req.passports.push_back( state );
        }
    }
    req.constraints.autonomyLevel = args.get( "autonomy_level", "L2" ).asString();
    req.constraints.offline = args.get( "offline", false ).asBool();
    if ( args.isMember( "max_bytes" ) )
        req.budget.maxBytes = args["max_bytes"].asInt();
    if ( args.isMember( "max_recipes" ) )
        req.budget.maxRecipes = args["max_recipes"].asInt();
    req.useCache = args.get( "use_cache", true ).asBool();

    auto result = sharedBroker().synthesize( req );
    Json::Value out = bundleToJson( result.bundle );
    out["cache_hit"] = result.cacheHit;
    out["planning_context"] = planningContextToCompileRequest( result.planning );
    out["counts"] = Json::objectValue;
    out["counts"]["assets"] = static_cast<int>( result.bundle.assets.size() );
    out["counts"]["capabilities"] = static_cast<int>( result.bundle.capabilities.size() );
    out["counts"]["recipes"] = static_cast<int>( result.bundle.recipes.size() );
    out["counts"]["open_questions"] = static_cast<int>( result.bundle.openQuestions.size() );
    out["truncated"] = result.bundle.truncation.truncated;
    return out;
}

Json::Value scientificCapabilities( const Json::Value &argsIn )
{
    const Json::Value args = requireObject( argsIn );
    CapabilityQuery q;
    q.goal = args.get( "goal", "" ).asString();
    q.intent = args.get( "intent", "" ).asString();
    if ( args.isMember( "passport" ) || args.isMember( "passport_json" ) )
    {
        auto state = passportFromArgs( args );
        q.observedState = observedStateFromPassport( state );
    }
    else if ( args.isMember( "observed_state" ) )
        q.observedState = args["observed_state"];
    q.constraints.autonomyLevel = args.get( "autonomy_level", "L2" ).asString();
    q.limit = args.get( "limit", 8 ).asInt();
    auto routed = routeCapabilities( q );
    Json::Value out( Json::objectValue );
    out["intent"] = routed.intent;
    out["intent_status"] = routed.intentStatus;
    Json::Value entries( Json::arrayValue );
    for ( const auto &e : routed.entries )
    {
        Json::Value o( Json::objectValue );
        o["capability_id"] = e.capabilityId;
        o["intent"] = e.intent;
        o["status"] = e.status;
        o["score"] = e.score;
        Json::Value reasons( Json::arrayValue );
        for ( const auto &r : e.reasons )
            reasons.append( r );
        o["reasons"] = reasons;
        Json::Value prep( Json::arrayValue );
        for ( const auto &p : e.prepActions )
            prep.append( p );
        o["prep_actions"] = prep;
        o["structural_only"] = e.structuralOnly;
        entries.append( o );
    }
    out["capabilities"] = entries;
    Json::Value qs( Json::arrayValue );
    for ( const auto &qtext : routed.openQuestions )
        qs.append( qtext );
    out["open_questions"] = qs;
    out["truncated"] = false;
    out["counts"] = Json::objectValue;
    out["counts"]["capabilities"] = static_cast<int>( routed.entries.size() );
    return out;
}

Json::Value dataAssetPassport( const Json::Value &argsIn )
{
    const Json::Value args = requireObject( argsIn );
    const std::string assetKey = args.get( "asset_id", args.get( "asset_key", "" ).asString() ).asString();
    AssetResolveResult resolved;
    if ( args.isMember( "passport" ) || args.isMember( "passport_json" ) )
    {
        auto state = passportFromArgs( args );
        resolved.ok = true;
        resolved.state = state;
        resolved.summary = AssetStateProvider::summarize( state );
    }
    else
    {
        if ( assetKey.empty() )
            throw std::runtime_error( "asset_id_required" );
        AssetResolveRequest req;
        req.assetKey = assetKey;
        resolved = sharedBroker().assets().resolve( req );
        if ( !resolved.ok )
            throw std::runtime_error( resolved.error );
    }

    Json::Value out( Json::objectValue );
    out["asset_id"] = resolved.summary.assetId;
    out["revision"] = resolved.summary.revision;
    out["display_name"] = resolved.summary.displayName;
    out["modality"] = resolved.summary.modality;
    out["radiometric_unit"] = resolved.summary.radiometricUnit;
    out["crs_authid"] = resolved.summary.crsAuthid;
    Json::Value roles( Json::arrayValue );
    for ( const auto &r : resolved.summary.bandRoles )
        roles.append( r );
    out["band_roles"] = roles;
    out["evidence"] = evidenceBucketToString( resolved.summary.evidence );
    out["path_hint"] = resolved.summary.pathHint;
    out["observed_state"] = observedStateFromPassport( resolved.state );
    if ( args.get( "include_full", false ).asBool() )
        out["passport"] = sicnu::state::assetStateToJson( resolved.state );
    out["truncated"] = false;
    return out;
}

Json::Value recipeSearch( const Json::Value &argsIn )
{
    const Json::Value args = requireObject( argsIn );
    RecipeQuery q;
    q.intent = args.get( "intent", "" ).asString();
    q.modality = args.get( "modality", "" ).asString();
    q.text = args.get( "text", args.get( "goal", "" ).asString() ).asString();
    q.limit = args.get( "limit", 5 ).asInt();
    if ( args.isMember( "observed_state" ) )
        q.observedState = args["observed_state"];
    else if ( args.isMember( "passport" ) || args.isMember( "passport_json" ) )
        q.observedState = observedStateFromPassport( passportFromArgs( args ) );

    auto hits = sharedBroker().recipes().search( q );
    Json::Value out( Json::objectValue );
    Json::Value arr( Json::arrayValue );
    for ( const auto &h : hits.hits )
    {
        Json::Value o( Json::objectValue );
        o["recipe_id"] = h.recipeId;
        o["title"] = h.title;
        o["intent"] = h.intent;
        o["modality"] = h.modality;
        o["score"] = h.score;
        o["stage_count"] = h.stageCount;
        o["has_human_only"] = h.hasHumanOnly;
        o["has_verifier_hooks"] = h.hasVerifierHooks;
        Json::Value matched( Json::arrayValue );
        for ( const auto &m : h.matched )
            matched.append( m );
        o["matched"] = matched;
        arr.append( o );
    }
    out["hits"] = arr;
    out["total"] = static_cast<int>( hits.hits.size() );
    out["registry_revision"] = Json::UInt64( hits.registryRevision );
    out["truncated"] = false;
    out["counts"] = Json::objectValue;
    out["counts"]["hits"] = static_cast<int>( hits.hits.size() );
    // Never auto-execute.
    out["auto_execute"] = false;
    return out;
}

} // namespace sicnu::science_context::agent_adapter

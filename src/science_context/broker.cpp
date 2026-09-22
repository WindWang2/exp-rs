// src/science_context/broker.cpp
#include "science_context/broker.h"

#include "science_context/capability_router.h"
#include "science_context/observed_state.h"

#include <algorithm>
#include <sstream>

namespace sicnu::science_context {

namespace {

bool autonomyAllowsExec( const std::string &level )
{
    // L5 only.
    return level == "L5";
}

std::string assetDigestOf( const std::vector<AssetSummary> &assets,
                           const std::vector<sicnu::state::RemoteSensingAssetState> &passports )
{
    std::ostringstream oss;
    for ( const auto &a : assets )
        oss << a.assetId << '@' << a.revision << ';';
    for ( const auto &p : passports )
        oss << p.assetId << '@' << p.revision << '#' << p.radiometric.unit << ';';
    return oss.str();
}

} // namespace

SynthesizeResult ScienceContextBroker::synthesize( const SynthesizeRequest &request )
{
    SynthesizeResult result;
    ContextConstraints constraints = request.constraints;
    constraints.allowAutonomousExec = autonomyAllowsExec( constraints.autonomyLevel );

    // Resolve assets
    std::vector<AssetSummary> summaries;
    std::vector<sicnu::state::RemoteSensingAssetState> states;
    Json::Value mergedObserved( Json::objectValue );
    bool conflicted = false;
    bool unknown = false;

    for ( const auto &passport : request.passports )
    {
        summaries.push_back( AssetStateProvider::summarize( passport ) );
        states.push_back( passport );
    }
    for ( const auto &key : request.assetKeys )
    {
        AssetResolveRequest req;
        req.assetKey = key;
        auto resolved = mAssets.resolve( req );
        if ( resolved.ok )
        {
            summaries.push_back( resolved.summary );
            states.push_back( resolved.state );
        }
        else
        {
            AssetSummary missing;
            missing.assetId = key;
            missing.evidence = EvidenceBucket::Unknown;
            missing.evidencePaths.push_back( "identity" );
            summaries.push_back( missing );
            unknown = true;
        }
    }

    for ( const auto &s : summaries )
    {
        if ( s.evidence == EvidenceBucket::Conflicted )
            conflicted = true;
        if ( s.evidence == EvidenceBucket::Unknown )
            unknown = true;
    }

    if ( !states.empty() )
        mergedObserved = observedStateFromPassport( states.front() );
    // Multi-asset CRS/grid conflict detection (structural).
    if ( states.size() >= 2 )
    {
        const auto &a = states[0].geometry;
        const auto &b = states[1].geometry;
        if ( a.hasCrs && b.hasCrs && !a.crsAuthid.empty() && !b.crsAuthid.empty() &&
             a.crsAuthid != b.crsAuthid )
            mergedObserved["crs_conflicted"] = true;
        if ( a.hasPixelSize && b.hasPixelSize &&
             ( a.pixelSizeX != b.pixelSizeX || a.pixelSizeY != b.pixelSizeY ) )
            mergedObserved["grid_conflicted"] = true;
    }

    CacheKeyMaterial keyMat;
    keyMat.assetDigest = assetDigestOf( summaries, states );
    keyMat.catalogGeneration = mAssets.catalogGeneration();
    keyMat.registryRevision = mRecipes.registryRevision();
    keyMat.recipePackDigest = mRecipes.packDigest();
    keyMat.autonomyRevision = constraints.autonomyLevel;
    keyMat.goal = request.goal;
    keyMat.intent = request.intent;
    keyMat.offline = constraints.offline;
    const std::string cacheKey = makeCacheKey( keyMat );

    if ( request.useCache )
    {
        auto hit = mCache.get( cacheKey );
        if ( hit )
        {
            result.bundle = *hit;
            result.cacheHit = true;
            result.planning = projectPlanningContext( result.bundle );
            mObs.recordSynthesize( true, result.bundle.truncation.truncated,
                                   result.planning.executionBlocked, result.bundle.intent,
                                   conflicted, unknown );
            return result;
        }
    }

    CapabilityQuery cq;
    cq.goal = request.goal;
    cq.intent = request.intent;
    cq.observedState = mergedObserved;
    cq.constraints = constraints;
    cq.limit = request.budget.maxCapabilities;
    CapabilityRouterResult caps = routeCapabilities( cq );

    RecipeQuery rq;
    rq.intent = caps.intent.empty() ? request.intent : caps.intent;
    rq.modality = mergedObserved.get( "modality", "" ).asString();
    rq.text = request.goal;
    rq.observedState = mergedObserved;
    // Over-fetch so ContextBudget can record deterministic truncation metadata.
    rq.limit = std::max( request.budget.maxRecipes * 3, request.budget.maxRecipes + 8 );
    RecipeRouterResult recipes = mRecipes.search( rq );

    ScientificContextBundle bundle;
    bundle.goal = request.goal;
    bundle.intent = caps.intent;
    bundle.assets = summaries;
    bundle.capabilities = caps.entries;
    bundle.recipes = recipes.hits;
    bundle.constraints = constraints;
    bundle.openQuestions = caps.openQuestions;

    // Offline: note limitation, do not invent remote facts.
    if ( constraints.offline )
        bundle.openQuestions.push_back( "offline_mode_no_remote_enrichment" );

    // Fill planner input facts from first passport.
    if ( !states.empty() )
    {
        bundle.planner.inputFacts["primary"] = understandingEnvelopeFromPassport( states.front() );
    }
    applyPlannerProjection( bundle );

    // Autonomy L2: never mark autonomous exec allowed (already set).
    if ( !constraints.allowAutonomousExec )
        bundle.openQuestions.push_back( "autonomy_" + constraints.autonomyLevel +
                                        "_no_autonomous_exec" );

    // Keep live counters OFF the bundle (byte-stable contract). Metrics live on
    // BrokerObservability for a future Control Center — never mutate scientific bytes.
    bundle.observability = Json::Value( Json::objectValue );
    bundle.bundleId.clear();
    applyContextBudget( bundle, request.budget );
    bundle.bundleId = computeBundleId( bundle );
    // bundle_id is assigned after budgeting; refresh final_bytes to match emitted form.
    bundle.truncation.finalBytes = static_cast<int>( serializeBundle( bundle ).size() );

    if ( request.useCache )
        mCache.put( cacheKey, bundle );

    result.bundle = bundle;
    result.cacheHit = false;
    result.planning = projectPlanningContext( bundle );
    mObs.recordSynthesize( false, bundle.truncation.truncated, result.planning.executionBlocked,
                           bundle.intent, conflicted, unknown );
    return result;
}

} // namespace sicnu::science_context

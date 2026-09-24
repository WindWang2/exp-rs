// src/science_context/broker.cpp
#include "science_context/broker.h"

#include "science_context/capability_router.h"
#include "science_context/observed_state.h"
#include "recipes/recipe_registry.h"

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

void ScienceContextBroker::setCapabilityFacts( CapabilityFactsLookup lookup )
{
    mCapFacts = std::move( lookup );
    // Bundles computed under the previous facts authority must not survive an
    // authority reinstall.
    mCache.clear();
}

void ScienceContextBroker::setRecipeRegistry(
    sicnu::recipes::ScientificRecipeRegistry *registry )
{
    mRecipeRegistry = registry;
}

bool ScienceContextBroker::refreshRecipes()
{
    if ( !mRecipeRegistry )
        return false;
    const bool ok = mRecipes.loadFromRegistry( *mRecipeRegistry );
    // Stale projections must never outlive an authority refresh.
    mCache.clear();
    return ok;
}

void ScienceContextBroker::invalidateAsset( const std::string &assetKey )
{
    mAssets.invalidate( assetKey );
    // Bundles embedding this asset are keyed by its digest; drop them so a
    // re-synthesize picks up the mutated passport instead of a stale match.
    mCache.clear();
}

void ScienceContextBroker::invalidateAllAssets()
{
    mAssets.invalidateAll();
    mCache.clear();
}

void ScienceContextBroker::notifyProjectSwitch()
{
    mAssets.invalidateAll();
    mCache.clear();
}

SynthesizeResult ScienceContextBroker::synthesize( const SynthesizeRequest &request )
{
    SynthesizeResult result;
    ContextConstraints constraints = request.constraints;
    constraints.allowAutonomousExec = autonomyAllowsExec( constraints.autonomyLevel );

    // Resolve assets — provenance per asset: inline (caller passports),
    // live (wired resolver), unavailable (lookup failed; unknown ≠ default).
    std::vector<AssetSummary> summaries;
    std::vector<sicnu::state::RemoteSensingAssetState> states;
    Json::Value mergedObserved( Json::objectValue );
    bool conflicted = false;
    bool unknown = false;
    bool anyMissing = false;
    bool anyLive = false;
    bool anyInline = false;
    // Provenance of the planner-facts primary: it is states.front(), which is
    // an inline passport whenever the caller supplied one (passports are
    // appended first), otherwise the first live-resolved asset.
    bool firstStateInline = !request.passports.empty();
    const std::string liveAuthority = mAssets.resolverAuthority();

    for ( const auto &passport : request.passports )
    {
        AssetSummary summary = AssetStateProvider::summarize( passport );
        summary.source = contentSourceToString( ContentSource::InlineInput );
        summaries.push_back( summary );
        states.push_back( passport );
        anyInline = true;
    }
    for ( const auto &key : request.assetKeys )
    {
        AssetResolveRequest req;
        req.assetKey = key;
        auto resolved = mAssets.resolve( req );
        if ( resolved.ok )
        {
            resolved.summary.source =
                contentSourceToString( ContentSource::LiveAuthority );
            summaries.push_back( resolved.summary );
            states.push_back( resolved.state );
            anyLive = true;
        }
        else
        {
            AssetSummary missing;
            missing.assetId = key;
            missing.evidence = EvidenceBucket::Unknown;
            missing.evidencePaths.push_back( "identity" );
            missing.source = contentSourceToString( ContentSource::Unavailable );
            summaries.push_back( missing );
            unknown = true;
            anyMissing = true;
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

    const CapabilityFactsLookup *capFacts = capabilityFacts();
    const bool haveCapProvider = capFacts && static_cast<bool>( capFacts->entriesForIntent );

    CacheKeyMaterial keyMat;
    keyMat.assetDigest = assetDigestOf( summaries, states );
    keyMat.catalogGeneration = mAssets.catalogGeneration();
    keyMat.registryRevision = mRecipes.registryRevision();
    keyMat.recipePackDigest = mRecipes.packDigest();
    keyMat.autonomyRevision = constraints.autonomyLevel;
    keyMat.goal = request.goal;
    keyMat.intent = request.intent;
    keyMat.offline = constraints.offline;
    keyMat.maxBytes = request.budget.maxBytes;
    keyMat.maxRecipes = request.budget.maxRecipes;
    keyMat.maxCapabilities = request.budget.maxCapabilities;
    keyMat.maxOpenQuestions = request.budget.maxOpenQuestions;
    keyMat.maxAssets = request.budget.maxAssets;
    keyMat.determinismRequired = constraints.determinismRequired;
    keyMat.capabilityAuthority =
        haveCapProvider ? capFacts->authority : std::string( "__builtin__" );
    keyMat.capabilityRevision = haveCapProvider ? capFacts->revision : 0;
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
    cq.facts = capFacts;
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

    // Section provenance — consumers must be able to tell live authority
    // data from caller input, broker fallbacks, or absence.
    SectionSource &assetsSource = bundle.sources.assets;
    if ( summaries.empty() )
    {
        assetsSource.source = ContentSource::Unavailable;
    }
    else if ( anyLive )
    {
        assetsSource.source = ContentSource::LiveAuthority;
        assetsSource.authority = liveAuthority;
        assetsSource.revision = mAssets.catalogGeneration();
        assetsSource.degraded = anyMissing;
    }
    else if ( anyInline )
    {
        assetsSource.source = ContentSource::InlineInput;
        assetsSource.authority = "inline_passport";
        assetsSource.degraded = anyMissing;
    }
    else
    {
        // Only failed lookups: nothing authority-backed and nothing inline.
        assetsSource.source = ContentSource::Unavailable;
        assetsSource.degraded = true;
    }

    SectionSource &capsSource = bundle.sources.capabilities;
    if ( haveCapProvider )
    {
        capsSource.source = ContentSource::LiveAuthority;
        capsSource.authority = capFacts->authority;
        capsSource.revision = capFacts->revision;
        capsSource.degraded = caps.presenceUnknown;
    }
    else if ( !caps.entries.empty() )
    {
        capsSource.source = ContentSource::BuiltinFallback;
        capsSource.authority = "broker_builtin_spec";
        capsSource.degraded = true; // builtin table, never authority output
    }
    else
    {
        capsSource.source = ContentSource::Unavailable;
    }

    const RecipeSourceInfo recipeInfo = mRecipes.sourceInfo();
    SectionSource &recipesSource = bundle.sources.recipes;
    switch ( recipeInfo.mode )
    {
        case RecipeSourceMode::LiveAuthority:
            recipesSource.source = ContentSource::LiveAuthority;
            recipesSource.authority = recipeInfo.authority;
            recipesSource.revision = recipeInfo.revision;
            recipesSource.degraded = recipeInfo.registryStatus != "ok" ||
                                     recipeInfo.registryProblems > 0;
            break;
        case RecipeSourceMode::InlineInput:
            recipesSource.source = ContentSource::InlineInput;
            recipesSource.authority = recipeInfo.authority;
            recipesSource.revision = recipeInfo.revision;
            recipesSource.degraded = false;
            break;
        case RecipeSourceMode::None:
            recipesSource.source = ContentSource::Unavailable;
            break;
    }

    // Offline: note limitation, do not invent remote facts.
    if ( constraints.offline )
        bundle.openQuestions.push_back( "offline_mode_no_remote_enrichment" );

    // Fill planner input facts from first passport.
    if ( !states.empty() )
    {
        bundle.planner.inputFacts["primary"] = understandingEnvelopeFromPassport( states.front() );
    }
    SectionSource &factsSource = bundle.sources.plannerFacts;
    if ( states.empty() )
    {
        factsSource.source = ContentSource::Unavailable;
    }
    else if ( firstStateInline )
    {
        factsSource.source = ContentSource::InlineInput;
        factsSource.authority = "inline_passport";
    }
    else
    {
        factsSource.source = ContentSource::LiveAuthority;
        factsSource.authority = liveAuthority;
        factsSource.revision = mAssets.catalogGeneration();
    }
    applyPlannerProjection( bundle );

    // Autonomy L2: never mark autonomous exec allowed (already set).
    if ( !constraints.allowAutonomousExec )
        bundle.openQuestions.push_back( "autonomy_" + constraints.autonomyLevel +
                                        "_no_autonomous_exec" );

    // Keep live counters OFF the bundle (byte-stable contract). Metrics live on
    // BrokerObservability for a future Control Center — never mutate scientific bytes.
    bundle.observability = Json::Value( Json::objectValue );
    // Same-width placeholder so the budget loop measures the emitted form:
    // the real id is assigned right after budgeting.
    bundle.bundleId = std::string( 16, '0' );
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

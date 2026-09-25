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

std::uint64_t fnv1a64( const std::string &s )
{
    std::uint64_t h = 14695981039346656037ull;
    for ( unsigned char c : s )
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string hex16( std::uint64_t v )
{
    static const char *kHex = "0123456789abcdef";
    std::string out( 16, '0' );
    for ( int i = 15; i >= 0; --i )
    {
        out[static_cast<std::size_t>( i )] = kHex[v & 0xf];
        v >>= 4;
    }
    return out;
}

std::string assetDigestOf(
    const std::vector<AssetSummary> &assets,
    const std::vector<sicnu::state::RemoteSensingAssetState> &passports,
    const std::vector<Json::Value> &observed )
{
    // Identity AND content: the same id/revision with mutated content (a
    // re-imported file, an authority reinstall that keeps revisions) must
    // land on a different cache key — the digest covers the passport
    // projection the bundle is actually built from. @p observed carries the
    // SAME projections the synthesize path uses (each built exactly once).
    std::ostringstream oss;
    for ( const auto &a : assets )
        oss << a.assetId << '@' << a.revision << ';';
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    for ( std::size_t i = 0; i < passports.size() && i < observed.size(); ++i )
    {
        oss << passports[i].assetId << '@' << passports[i].revision << '#'
            << hex16( fnv1a64( Json::writeString( builder, observed[i] ) ) ) << ';';
    }
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
    std::vector<std::string> resolveFailures;
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
            // Typed reason survives into the bundle problem channel:
            // gdal_open_failed ≠ asset_not_found ≠ resolver_unavailable.
            std::string marker = "asset_resolve_failed:" + resolved.error;
            if ( !resolved.errorDetail.empty() )
                marker += ":" + resolved.errorDetail;
            resolveFailures.push_back( std::move( marker ) );
        }
    }

    for ( const auto &s : summaries )
    {
        if ( s.evidence == EvidenceBucket::Conflicted )
            conflicted = true;
        if ( s.evidence == EvidenceBucket::Unknown )
            unknown = true;
    }

    // One projection per passport, reused for the digest, the observed
    // state, and the planner facts (never rebuilt on the cached path).
    std::vector<Json::Value> observed;
    observed.reserve( states.size() );
    for ( const auto &s : states )
        observed.push_back( observedStateFromPassport( s ) );
    if ( !observed.empty() )
        mergedObserved = observed.front();
    // Multi-asset CRS/grid conflict detection (structural): pairwise across
    // ALL assets — a conflict between assets 2 and 3 is as blocking as one
    // between 1 and 2.
    if ( states.size() >= 2 )
    {
        bool crsConflict = false;
        bool gridConflict = false;
        bool haveCrs = false;
        bool havePixel = false;
        std::string crsAuthid;
        double pixelX = 0.0;
        double pixelY = 0.0;
        for ( const auto &s : states )
        {
            const auto &g = s.geometry;
            if ( g.hasCrs && !g.crsAuthid.empty() )
            {
                if ( haveCrs && crsAuthid != g.crsAuthid )
                    crsConflict = true;
                if ( !haveCrs )
                {
                    crsAuthid = g.crsAuthid;
                    haveCrs = true;
                }
            }
            if ( g.hasPixelSize )
            {
                if ( havePixel && ( pixelX != g.pixelSizeX || pixelY != g.pixelSizeY ) )
                    gridConflict = true;
                if ( !havePixel )
                {
                    pixelX = g.pixelSizeX;
                    pixelY = g.pixelSizeY;
                    havePixel = true;
                }
            }
        }
        if ( crsConflict )
            mergedObserved["crs_conflicted"] = true;
        if ( gridConflict )
            mergedObserved["grid_conflicted"] = true;
    }

    const CapabilityFactsLookup *capFacts = capabilityFacts();
    const bool haveCapProvider = capFacts && static_cast<bool>( capFacts->entriesForIntent );
    // One revision read per synthesize: cache key and provenance must agree
    // even if the authority reloads mid-flight.
    const std::uint64_t capRevision =
        haveCapProvider && capFacts->revision ? capFacts->revision() : 0;

    CacheKeyMaterial keyMat;
    keyMat.assetDigest = assetDigestOf( summaries, states, observed );
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
    keyMat.capabilityRevision = capRevision;
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
    // Typed resolve failures join the problem channel (deduped, order-stable).
    for ( const auto &failure : resolveFailures )
    {
        if ( std::find( bundle.openQuestions.begin(), bundle.openQuestions.end(), failure ) ==
             bundle.openQuestions.end() )
            bundle.openQuestions.push_back( failure );
    }

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
        capsSource.revision = capRevision;
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

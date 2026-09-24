// tests/test_science_context_goal_projection.cpp — R3 track 14 seam 2:
// science_context bundle → planner ScientificGoal/PlanningContext.
//
// Oracles pinned here:
//   * known intents project to their planner goal kinds; an UNKNOWN intent
//     leaves the kind empty with a typed unresolved issue, and the planner
//     answers infeasible — nothing is default-filled into a runnable goal;
//   * a foreign radiometric unit degrades to domain "unknown" WITH an issue
//     and the plan answers with a blocking calibration question — never a
//     silent pass-through;
//   * the bundle cannot claim asset readiness: without enrichment every
//     asset plans as "unknown" and the planner asks; enrichment is
//     vocabulary-validated (a hostile state string is rejected into
//     `unresolved`, not copied);
//   * provenance (authority/revision/degraded per section + enrichment)
//     round-trips into the projection result, byte-stable;
//   * end-to-end: a projected NDVI goal over the live capability provider
//     plans the mirror-backed candidate deterministically.
#include <catch2/catch_test_macros.hpp>

#include "contracts/scientific_contract.h"
#include "planner/json_util.h"
#include "planner/live/capability_live.h"
#include "planner/planner_core.h"
#include "planner/scientific_plan.h"
#include "preflight/capability_mirror.h"
#include "science_context/bundle.h"
#include "science_context/planner_goal_projection.h"

#include <json/json.h>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

using namespace sicnu::science_context;
using namespace sicnu::planner;

namespace
{

sicnu::preflight::CapabilityMirrorProjection makeLiveMirror()
{
    sicnu::preflight::CapabilityMirrorProjection mirror;
    REQUIRE( mirror.loadDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" )
             > 0 );
    return mirror;
}

AssetSummary asset( const std::string &id, const std::string &modality,
                    const std::string &radiometricUnit )
{
    AssetSummary summary;
    summary.assetId = id;
    summary.modality = modality;
    summary.radiometricUnit = radiometricUnit;
    summary.crsAuthid = "EPSG:32633";
    summary.bandRoles = { "red", "nir" };
    summary.evidence = EvidenceBucket::Known;
    return summary;
}

/// Bundle with one optical surface-reflectance scene and a live-capability
/// provenance stamp.
ScientificContextBundle bundle( const std::string &intent )
{
    ScientificContextBundle bundle;
    bundle.bundleId = "bnd-test-0001";
    bundle.goal = "vegetation state over scene-a";
    bundle.intent = intent;
    bundle.assets.push_back( asset( "scene-a", "optical", "surface_reflectance" ) );
    bundle.sources.assets = SectionSource{ ContentSource::LiveAuthority,
                                           "scientific_state.passports", 7, false };
    bundle.sources.capabilities = SectionSource{ ContentSource::LiveAuthority,
                                                  "harness.capability_knowledge", 11, false };
    return bundle;
}

PlanningResult planOverLiveProvider( const PlannerProjectionResult &projection )
{
    REQUIRE( projection.goal.kind != "" ); // callers must not plan unknown intents
    const auto mirror = makeLiveMirror();
    LiveCapabilityProvider provider;
    std::string error;
    REQUIRE( LiveCapabilityProvider::create( mirror, provider, error ) );
    PlannerProviders providers;
    providers.capability = &provider;
    return planScientificWork( projection.goal, projection.context, providers );
}

bool hasIssue( const PlannerProjectionResult &result, const std::string &code,
               const std::string &needle = "" )
{
    for ( const auto &item : result.unresolved )
    {
        if ( item.code != code )
            continue;
        if ( needle.empty() || item.detail.find( needle ) != std::string::npos )
            return true;
    }
    return false;
}

std::string canonical( const PlannerProjectionResult &result )
{
    return json_util::canonicalCompact( result.provenance );
}

} // namespace

TEST_CASE( "Known intents project onto their planner goal kinds", "[goal_projection]" )
{
    const auto &map = intentToGoalKindMap();
    REQUIRE( map.at( "ndvi" ) == "measurement" );
    REQUIRE( map.at( "change" ) == "change" );
    REQUIRE( map.at( "classify" ) == "classification" );
    REQUIRE( map.at( "temporal" ) == "temporal_analysis" );
    REQUIRE( map.at( "sar_change" ) == "change" );
    // radar/water/phenology and friends have honest homes too
    REQUIRE( map.at( "phenology" ) == "temporal_analysis" );
    REQUIRE( map.at( "flood" ) == "detection" );
    // preprocessing is not a science goal: no kind, on purpose
    REQUIRE( map.find( "preprocess" ) == map.end() );
    REQUIRE( map.find( "qa" ) == map.end() );
}

TEST_CASE( "NDVI bundle → planner inputs → live plan (E2E projection)", "[goal_projection]" )
{
    PlannerAssetEnrichment enrichment;
    PlannerAssetFacts ready;
    ready.state = "ready";
    ready.resolutionM = 10.0;
    enrichment.assets["scene-a"] = ready;
    enrichment.source = "scientific_state.passports";
    enrichment.revision = 42;

    const auto projection = projectPlannerInputs( bundle( "ndvi" ), enrichment );
    REQUIRE( projection.goal.kind == "measurement" );
    REQUIRE_FALSE( projection.goal.goalId.empty() );
    REQUIRE( projection.context.assets.size() == 1 );
    REQUIRE( projection.context.assets[0].state == "ready" );
    REQUIRE( projection.context.assets[0].numericDomain == "reflectance" );
    REQUIRE( projection.context.mode.kind == "agent" );
    REQUIRE( projection.unresolved.empty() );

    // Provenance travels: section sources + enrichment land in the result.
    REQUIRE( projection.provenance["sections"]["assets"]["authority"].asString()
             == "scientific_state.passports" );
    REQUIRE( projection.provenance["sections"]["assets"]["revision"].asUInt64() == 7 );
    REQUIRE( projection.provenance["enrichment"]["revision"].asUInt64() == 42 );

    // ...and the projection plans over the live capability provider: the
    // primary's analysis step must be a LIVE-authority index operator
    // (lawful for the measurement goal's output contract).
    {
        const auto mirror = makeLiveMirror();
        LiveCapabilityProvider provider;
        std::string error;
        REQUIRE( LiveCapabilityProvider::create( mirror, provider, error ) );
        auto result = planOverLiveProvider( projection );
        REQUIRE( result.primary() != nullptr );
        bool analyzedLawfulIndex = false;
        for ( const auto &step : result.primary()->steps )
        {
            if ( step.role != "analyze" )
                continue;
            for ( const auto &fact : provider.capabilitiesForFamily( "analysis" ) )
            {
                analyzedLawfulIndex = analyzedLawfulIndex
                                      || ( fact.operatorId == step.operatorId
                                           && fact.outputDomain == "index" );
            }
        }
        REQUIRE( analyzedLawfulIndex );
    }
}

TEST_CASE( "Unknown intent: the goal kind stays empty and planning refuses",
           "[goal_projection]" )
{
    const auto projection = projectPlannerInputs( bundle( "mosaic_of_life" ) );
    REQUIRE( projection.goal.kind.empty() );
    REQUIRE( hasIssue( projection, projection_issue::kUnresolvedIntent, "mosaic_of_life" ) );

    // The planner — not the projection — answers, and it refuses rather
    // than defaulting to some runnable kind.
    const auto mirror = makeLiveMirror();
    LiveCapabilityProvider provider;
    std::string error;
    REQUIRE( LiveCapabilityProvider::create( mirror, provider, error ) );
    PlannerProviders providers;
    providers.capability = &provider;
    auto result = planScientificWork( projection.goal, projection.context, providers );
    REQUIRE( result.primary() != nullptr );
    REQUIRE( result.primary()->verdict == "infeasible" );
    REQUIRE_FALSE( result.primary()->verdictReasons.empty() );
    for ( const auto &step : result.primary()->steps )
        FAIL( "an unprojectable goal must not produce steps, got step \""
              + step.stepId + "\"" );
}

TEST_CASE( "Foreign radiometric unit degrades loudly, and the plan asks", "[goal_projection]" )
{
    // Classification candidates all consume "features" (contracts): with the
    // asset domain undeclared, no lawful candidate can silently proceed.
    ScientificContextBundle odd = bundle( "classify" );
    odd.assets[0].radiometricUnit = "quantized_wiggles";
    PlannerAssetEnrichment enrichment;
    PlannerAssetFacts ready;
    ready.state = "ready";
    enrichment.assets["scene-a"] = ready;
    const auto projection = projectPlannerInputs( odd, enrichment );
    REQUIRE( projection.context.assets[0].numericDomain.empty() ); // undeclared, never faked
    REQUIRE( hasIssue( projection, projection_issue::kForeignRadiometricUnit,
                       "quantized_wiggles" ) );

    auto result = planOverLiveProvider( projection );
    // The enrichment validated "ready" but the domain stayed undeclared: the
    // plan must carry a BLOCKING domain question, not a silent analysis.
    REQUIRE( result.primary() != nullptr );
    bool blockingDomainQuestion = false;
    for ( const auto &question : result.primary()->openQuestions )
    {
        INFO( "Q blocking=" << question.blocking << " kind=" << question.kind << " "
              << question.detail );
        if ( question.blocking && question.kind == "insufficient_data"
             && question.detail.find( "features" ) != std::string::npos )
            blockingDomainQuestion = true;
    }
    REQUIRE( blockingDomainQuestion );
}

TEST_CASE( "Without enrichment the bundle cannot claim readiness", "[goal_projection]" )
{
    const auto projection = projectPlannerInputs( bundle( "ndvi" ) );
    REQUIRE( projection.context.assets[0].state == "unknown" );

    auto result = planOverLiveProvider( projection );
    REQUIRE( result.primary() != nullptr );
    REQUIRE( result.primary()->verdict != "feasible" );
    bool askedAboutAsset = false;
    for ( const auto &question : result.primary()->openQuestions )
    {
        if ( question.kind == "insufficient_data"
             && question.detail.find( "scene-a" ) != std::string::npos )
            askedAboutAsset = true;
    }
    REQUIRE( askedAboutAsset );
}

TEST_CASE( "Enrichment is vocabulary-validated: hostile facts are rejected, not copied",
           "[goal_projection]" )
{
    PlannerAssetEnrichment enrichment;
    PlannerAssetFacts hostile;
    hostile.state = "tottered";          // not a lifecycle state
    hostile.numericDomain = "wiggles";   // not a contracts domain
    enrichment.assets["scene-a"] = hostile;
    const auto projection = projectPlannerInputs( bundle( "ndvi" ), enrichment );
    REQUIRE( projection.context.assets[0].state == "unknown" ); // rejected → honest default
    REQUIRE( projection.context.assets[0].numericDomain == "reflectance" ); // bundle truth kept
    REQUIRE( hasIssue( projection, projection_issue::kEnrichmentRejected, "state" ) );
    REQUIRE( hasIssue( projection, projection_issue::kEnrichmentRejected, "numeric_domain" ) );

    // Lawful enrichment values DO override the bundle projection: an
    // explicit passport-backed domain is the caller's authority.
    PlannerAssetEnrichment lawful;
    PlannerAssetFacts passport;
    passport.state = "ready";
    passport.numericDomain = "index";
    lawful.assets["scene-a"] = passport;
    const auto projection2 = projectPlannerInputs( bundle( "ndvi" ), lawful );
    REQUIRE( projection2.context.assets[0].state == "ready" );
    REQUIRE( projection2.context.assets[0].numericDomain == "index" );
    REQUIRE( projection2.unresolved.empty() );

    // Enrichment for an asset the bundle does not carry is named too.
    PlannerAssetEnrichment stray;
    stray.assets["scene-z"] = PlannerAssetFacts{};
    const auto projection3 = projectPlannerInputs( bundle( "ndvi" ), stray );
    REQUIRE( hasIssue( projection3, projection_issue::kEnrichmentRejected, "scene-z" ) );
}

TEST_CASE( "Conflicted evidence and constraints travel into the projection",
           "[goal_projection]" )
{
    ScientificContextBundle conflicted = bundle( "ndvi" );
    conflicted.assets[0].conflictAlternatives = { "path-a", "path-b" };
    const auto projection = projectPlannerInputs( conflicted );
    REQUIRE( hasIssue( projection, projection_issue::kConflictedEvidence, "scene-a" ) );
}

TEST_CASE( "Mode policy and determinism constraints project from the bundle", "[goal_projection]" )
{
    ScientificContextBundle autonomous = bundle( "classify" );
    autonomous.constraints.autonomyLevel = "L5";
    autonomous.constraints.determinismRequired = false;
    auto projection = projectPlannerInputs( autonomous );
    REQUIRE( projection.goal.kind == "classification" );
    REQUIRE( projection.context.mode.autonomy == "full" );
    REQUIRE_FALSE( projection.context.constraints.requiredDeterminism );

    ScientificContextBundle restricted = bundle( "classify" );
    restricted.constraints.autonomyLevel = "L1";
    projection = projectPlannerInputs( restricted );
    REQUIRE( projection.context.mode.autonomy == "minimal" );
    REQUIRE( projection.context.constraints.requiredDeterminism );
}

TEST_CASE( "Projection is byte-stable for the same bundle + enrichment", "[goal_projection]" )
{
    PlannerAssetEnrichment enrichment;
    PlannerAssetFacts ready;
    ready.state = "ready";
    enrichment.assets["scene-a"] = ready;
    enrichment.source = "scientific_state.passports";
    enrichment.revision = 42;

    const auto first = projectPlannerInputs( bundle( "ndvi" ), enrichment );
    const auto second = projectPlannerInputs( bundle( "ndvi" ), enrichment );
    REQUIRE( first.goal.goalId == second.goal.goalId );
    REQUIRE( json_util::canonicalCompact( scientificGoalToJson( first.goal ) )
             == json_util::canonicalCompact( scientificGoalToJson( second.goal ) ) );
    REQUIRE( json_util::canonicalCompact( planningContextToJson( first.context ) )
             == json_util::canonicalCompact( planningContextToJson( second.context ) ) );
    REQUIRE( canonical( first ) == canonical( second ) );
}

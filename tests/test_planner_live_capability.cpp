// tests/test_planner_live_capability.cpp — the live capability seam for the
// scientific planner (R3 track 14).
//
// Oracles pinned here:
//   * the adapter fails CLOSED on an unconfigured/unhealthy mirror and never
//     reads as live authority;
//   * over the REAL data/agent/capabilities documents: every projected fact
//     is contracts-lawful, every declared table id exists in mirror AND
//     contracts, and every planner goal kind keeps at least one lawful
//     analysis candidate (planability drift);
//   * authority deletion: an operator removed from the mirror documents can
//     never reappear in a replan (no stale id), and the revision moves;
//   * same input + same authority ⇒ byte-identical replan; a base cost-class
//     change moves the plan deterministically;
//   * slots the mirror does not declare (verification/publication) stay
//     honestly unprojected — the planner asks, it never default-fills.
#include <catch2/catch_test_macros.hpp>

#include "contracts/scientific_contract.h"
#include "planner/json_util.h"
#include "planner/live/capability_live.h"
#include "planner/planner_core.h"
#include "planner/planner_rules.h"
#include "planner/scientific_plan.h"
#include "preflight/capability_mirror.h"

#include <json/json.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

using namespace sicnu::planner;

namespace
{

Json::Value parse( const std::string &text )
{
    Json::Value out;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream stream( text );
    [[maybe_unused]] const bool ok = Json::parseFromStream( builder, stream, &out, &errs );
    return out;
}

/// A synthetic mirror directory with one document per named entry set.
/// Unique per process (pid suffix), wiped before writing so stale content
/// from an older run cannot leak into loadDirectory.
std::string writeMirrorDir( const std::string &name,
                            const std::vector<std::pair<std::string, Json::Value>> &documents )
{
    const std::string dir = "/tmp/planner_live_" + name + "_"
                            + std::to_string( static_cast<long>( ::getpid() ) );
    std::filesystem::remove_all( dir );
    std::filesystem::create_directories( dir );
    for ( const auto &[fileName, array] : documents )
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::ofstream out( dir + "/" + fileName, std::ios::binary );
        out << Json::writeString( builder, array );
    }
    return dir;
}

/// Measurement goal + one ready reflectance asset lawful for @p analysisOperator.
ScientificGoal goal()
{
    ScientificGoal goal;
    goal.goalId = "goal-live-ndvi";
    goal.kind = "measurement";
    goal.subject = "vegetation index";
    return goal;
}

PlanningContext contextFor( const std::string &analysisOperator )
{
    PlanningContext context;
    PlannerAssetFacts asset;
    asset.ref = "scene-a";
    asset.kind = "raster";
    asset.modality = "optical";
    const auto *contract = sicnu::contracts::findScientificContract( analysisOperator );
    REQUIRE( contract != nullptr );
    asset.numericDomain = contract->inputDomain == "any" ? "reflectance"
                                                         : contract->inputDomain;
    asset.crs = "EPSG:32633";
    asset.resolutionM = 10.0;
    asset.state = "ready";
    context.assets.push_back( asset );
    return context;
}

PlanningResult planOver( const LiveCapabilityProvider &provider, const ScientificGoal &goal,
                         const PlanningContext &context )
{
    PlannerProviders providers;
    providers.capability = &provider;
    return planScientificWork( goal, context, providers );
}

std::set<std::string> analysisOperatorsOf( const ScientificPlan &plan )
{
    std::set<std::string> operators;
    for ( const auto &step : plan.steps )
    {
        if ( step.role == "analyze" )
            operators.insert( step.operatorId );
    }
    return operators;
}

} // namespace

TEST_CASE( "LiveCapabilityProvider fails closed on unusable mirrors", "[planner_live]" )
{
    SECTION( "unconfigured mirror" )
    {
        sicnu::preflight::CapabilityMirrorProjection mirror;
        LiveCapabilityProvider provider;
        std::string error;
        REQUIRE_FALSE( LiveCapabilityProvider::create( mirror, provider, error ) );
        REQUIRE( error.find( "not configured" ) != std::string::npos );
    }
    SECTION( "unhealthy mirror (malformed document)" )
    {
        sicnu::preflight::CapabilityMirrorProjection mirror;
        Json::Value notAnArray( Json::objectValue );
        mirror.addDocument( notAnArray, "broken.json" );
        LiveCapabilityProvider provider;
        std::string error;
        REQUIRE_FALSE( LiveCapabilityProvider::create( mirror, provider, error ) );
        REQUIRE( error.find( "unhealthy" ) != std::string::npos );
    }
    SECTION( "hostile resource shape degrades to a counted skip, never an abort" )
    {
        // The mirror's fail-closed validation does not police `resource`;
        // an entry authoring a non-object resource must come back as a
        // typed skip, not an exception out of create().
        sicnu::preflight::CapabilityMirrorProjection mirror;
        mirror.addDocument(
            parse( R"( [
                {"id":"family:spectral_index","kind":"family_default","family":"spectral_index",
                 "resource":{"cost_class":"light"}},
                {"id":"rs:ndvi","family":"spectral_index","extends":"family:spectral_index",
                 "resource":"heavy"},
                {"id":"rs:evi","family":"spectral_index","extends":"family:spectral_index",
                 "resource":{"cost_class":42}}
            ] )" ),
            "hostile.json" );
        LiveCapabilityProvider provider;
        std::string error;
        REQUIRE( LiveCapabilityProvider::create( mirror, provider, error ) );
        REQUIRE( provider.capabilitiesForFamily( "analysis" ).empty() );
        REQUIRE( provider.stats().skippedNoCostClass.size() == 2 );
    }
}

TEST_CASE( "LiveCapabilityProvider projects the real mirror into lawful planner facts",
           "[planner_live]" )
{
    sicnu::preflight::CapabilityMirrorProjection mirror;
    REQUIRE( mirror.loadDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" )
             > 0 );

    LiveCapabilityProvider provider;
    std::string error;
    REQUIRE( LiveCapabilityProvider::create( mirror, provider, error ) );
    REQUIRE( error.empty() );

    SECTION( "every projected fact is contracts-lawful and planner-vocabulary-clean" )
    {
        REQUIRE( provider.stats().projected > 0 );
        for ( const auto &slot : LiveCapabilityProvider::declaredFamilySlots() )
        {
            for ( const auto &fact : provider.capabilitiesForFamily( slot ) )
            {
                REQUIRE( fact.family == slot );
                REQUIRE( isKnownCostClass( fact.costClass ) );
                REQUIRE( sicnu::contracts::findScientificContract( fact.operatorId ) != nullptr );
                REQUIRE( capabilityAgreesWithContracts( fact ) );
            }
        }
    }

    SECTION( "declared table ids exist in the live mirror and in contracts (drift)" )
    {
        const std::vector<std::string> ids = mirror.entryIds();
        const std::set<std::string> mirrorIds( ids.begin(), ids.end() );
        for ( const auto &slot : LiveCapabilityProvider::declaredFamilySlots() )
        {
            for ( const auto &id : LiveCapabilityProvider::operatorIdsForSlot( slot ) )
            {
                INFO( slot << "/" << id );
                REQUIRE( mirrorIds.count( id ) == 1 );
                REQUIRE( sicnu::contracts::findScientificContract( id ) != nullptr );
            }
        }
    }

    SECTION( "declared slots stay inside the planner spine vocabulary" )
    {
        for ( const auto &slot : LiveCapabilityProvider::declaredFamilySlots() )
        {
            INFO( slot );
            REQUIRE( isKnownFamilySlot( slot ) );
            REQUIRE( slot != "verification" ); // the mirror declares no verification OPERATOR
            REQUIRE( slot != "publication" );
        }
    }

    SECTION( "planability drift: every goal kind keeps a lawful analysis candidate" )
    {
        for ( const auto &goalKind : kGoalKinds )
        {
            const auto allowed = allowedAnalysisOutputsForGoalKind( goalKind );
            REQUIRE_FALSE( allowed.empty() );
            bool lawful = false;
            for ( const auto &fact : provider.capabilitiesForFamily( "analysis" ) )
            {
                const auto *contract =
                    sicnu::contracts::findScientificContract( fact.operatorId );
                if ( contract
                     && std::find( allowed.begin(), allowed.end(), contract->outputDomain )
                            != allowed.end() )
                {
                    lawful = true;
                    break;
                }
            }
            INFO( "goal kind " << goalKind );
            REQUIRE( lawful );
        }
    }

    SECTION( "unprojected mirror operators stay visible, never silent" )
    {
        // The mirror declares far more rs: operators than the planner table
        // projects; the skip lists must name them.
        REQUIRE_FALSE( provider.stats().notProjected.empty() );
        REQUIRE( provider.stats().skippedNoContract.empty() ); // table ⊆ contracts
        REQUIRE( provider.stats().skippedNoCostClass.empty() ); // real docs carry cost classes
    }

    SECTION( "same documents ⇒ same revision (twice)" )
    {
        LiveCapabilityProvider again;
        std::string error2;
        REQUIRE( LiveCapabilityProvider::create( mirror, again, error2 ) );
        REQUIRE( again.revision() == provider.revision() );
        REQUIRE_FALSE( provider.revision().empty() );
    }
}

TEST_CASE( "Planning over the live provider produces the mirror-backed candidate",
           "[planner_live]" )
{
    sicnu::preflight::CapabilityMirrorProjection mirror;
    REQUIRE( mirror.loadDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" )
             > 0 );
    LiveCapabilityProvider provider;
    std::string error;
    REQUIRE( LiveCapabilityProvider::create( mirror, provider, error ) );

    // The deterministic baseline ranks lawful candidates by cost class then
    // operator id — the primary is one of the low-cost index operators, and
    // rs:ndvi must be among the minted candidate plans.
    auto result = planOver( provider, goal(), contextFor( "rs:ndvi" ) );
    REQUIRE( result.primary() != nullptr );
    const ScientificPlan *primary = result.primary();
    REQUIRE( primary->verdict == "feasible_with_gaps" ); // verification/publication gaps are honest
    REQUIRE_FALSE( analysisOperatorsOf( *primary ).empty() );

    bool sawNdviCandidate = false;
    for ( const auto &candidate : result.candidates )
        sawNdviCandidate = sawNdviCandidate || analysisOperatorsOf( candidate ).count( "rs:ndvi" ) == 1;
    REQUIRE( sawNdviCandidate );

    // No goal kind can smuggle an operator the mirror does not declare.
    for ( const auto &candidate : result.candidates )
    {
        for ( const auto &step : candidate.steps )
        {
            INFO( candidate.planId << " step " << step.stepId );
            REQUIRE( sicnu::contracts::findScientificContract( step.operatorId ) != nullptr );
        }
    }
}

TEST_CASE( "Authority deletion: the deleted operator can never be replanned", "[planner_live]" )
{
    const std::string dir = writeMirrorDir(
        "deletion",
        {
            { "io.json",
              Json::Value( Json::arrayValue ) }, // real docs are per-file arrays
            { "spectral.json",
              parse( R"( [
                {"id":"family:spectral_index","kind":"family_default","family":"spectral_index",
                 "resource":{"cost_class":"light"}},
                {"id":"rs:ndvi","family":"spectral_index","extends":"family:spectral_index",
                 "resource":{"cost_class":"light"}},
                {"id":"rs:evi","family":"spectral_index","extends":"family:spectral_index",
                 "resource":{"cost_class":"light"}}
              ] )" ) },
            { "import.json",
              parse( R"( [
                {"id":"family:io","kind":"family_default","family":"io",
                 "resource":{"cost_class":"medium"}},
                {"id":"rs:landsat_import","family":"io","extends":"family:io",
                 "resource":{"cost_class":"medium"}}
              ] )" ) },
        } );

    auto buildProvider = [&]( LiveCapabilityProvider &provider ) {
        sicnu::preflight::CapabilityMirrorProjection mirror;
        REQUIRE( mirror.loadDirectory( dir ) > 0 );
        std::string error;
        REQUIRE( LiveCapabilityProvider::create( mirror, provider, error ) );
    };

    LiveCapabilityProvider before;
    buildProvider( before );
    auto resultBefore = planOver( before, goal(), contextFor( "rs:ndvi" ) );
    REQUIRE( resultBefore.primary() != nullptr );
    bool ndviBefore = false;
    for ( const auto &candidate : resultBefore.candidates )
        ndviBefore = ndviBefore || analysisOperatorsOf( candidate ).count( "rs:ndvi" ) == 1;
    REQUIRE( ndviBefore );

    // --- the authority deletes rs:ndvi -----------------------------------
    std::filesystem::remove( dir + "/spectral.json" );
    const std::string withoutNdvi = R"( [
        {"id":"family:spectral_index","kind":"family_default","family":"spectral_index",
         "resource":{"cost_class":"light"}},
        {"id":"rs:evi","family":"spectral_index","extends":"family:spectral_index",
         "resource":{"cost_class":"light"}}
    ] )";
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::ofstream out( dir + "/spectral.json", std::ios::binary );
        out << Json::writeString( builder, parse( withoutNdvi ) );
    }

    LiveCapabilityProvider after;
    buildProvider( after );
    REQUIRE( after.revision() != before.revision() ); // the revision moved

    auto resultAfter = planOver( after, goal(), contextFor( "rs:evi" ) );
    REQUIRE( resultAfter.primary() != nullptr );
    for ( const auto &candidate : resultAfter.candidates )
    {
        INFO( candidate.planId );
        REQUIRE( analysisOperatorsOf( candidate ).count( "rs:ndvi" ) == 0 );
    }
    for ( const auto &slot : LiveCapabilityProvider::declaredFamilySlots() )
    {
        for ( const auto &fact : after.capabilitiesForFamily( slot ) )
            REQUIRE( fact.operatorId != "rs:ndvi" );
    }
}

TEST_CASE( "Same input + same authority replans byte-identically", "[planner_live]" )
{
    const std::string dir = writeMirrorDir(
        "stability",
        {
            { "spectral.json", parse( R"( [
                {"id":"family:spectral_index","kind":"family_default","family":"spectral_index",
                 "resource":{"cost_class":"light"}},
                {"id":"rs:ndvi","family":"spectral_index","extends":"family:spectral_index",
                 "resource":{"cost_class":"light"}},
                {"id":"rs:evi","family":"spectral_index","extends":"family:spectral_index",
                 "resource":{"cost_class":"light"}}
              ] )" ) },
        } );

    sicnu::preflight::CapabilityMirrorProjection mirror;
    REQUIRE( mirror.loadDirectory( dir ) > 0 );
    LiveCapabilityProvider provider;
    std::string error;
    REQUIRE( LiveCapabilityProvider::create( mirror, provider, error ) );

    const PlanningContext context = contextFor( "rs:ndvi" );
    auto first = planOver( provider, goal(), context );
    auto second = planOver( provider, goal(), context );
    REQUIRE( first.candidates.size() == second.candidates.size() );
    for ( size_t i = 0; i < first.candidates.size(); ++i )
    {
        REQUIRE( first.candidates[i].planId == second.candidates[i].planId );
        REQUIRE( json_util::canonicalCompact( scientificPlanToJson( first.candidates[i] ) )
                 == json_util::canonicalCompact( scientificPlanToJson( second.candidates[i] ) ) );
    }
}

TEST_CASE( "A base cost-class change replans deterministically", "[planner_live]" )
{
    const std::string docs = R"( [
        {"id":"family:spectral_index","kind":"family_default","family":"spectral_index",
         "resource":{"cost_class":"light"}},
        {"id":"rs:ndvi","family":"spectral_index","extends":"family:spectral_index",
         "resource":{"cost_class":"light"}},
        {"id":"rs:evi","family":"spectral_index","extends":"family:spectral_index",
         "resource":{"cost_class":"light"}}
    ] )";
    const std::string docsHeavy = R"( [
        {"id":"family:spectral_index","kind":"family_default","family":"spectral_index",
         "resource":{"cost_class":"light"}},
        {"id":"rs:ndvi","family":"spectral_index","extends":"family:spectral_index",
         "resource":{"cost_class":"heavy"}},
        {"id":"rs:evi","family":"spectral_index","extends":"family:spectral_index",
         "resource":{"cost_class":"light"}}
    ] )";
    const std::string dir = writeMirrorDir( "cost", { { "spectral.json", parse( docs ) } } );

    auto planJson = [&]( const std::string &spectralDoc, std::string &revision ) {
        {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            std::ofstream out( dir + "/spectral.json", std::ios::binary );
            out << Json::writeString( builder, parse( spectralDoc ) );
        } // close (and flush) before the authority re-reads the directory
        sicnu::preflight::CapabilityMirrorProjection mirror;
        REQUIRE( mirror.loadDirectory( dir ) > 0 );
        LiveCapabilityProvider provider;
        std::string error;
        REQUIRE( LiveCapabilityProvider::create( mirror, provider, error ) );
        revision = provider.revision();
        auto result = planOver( provider, goal(), contextFor( "rs:ndvi" ) );
        REQUIRE( result.primary() != nullptr );
        return json_util::canonicalCompact( scientificPlanToJson( *result.primary() ) );
    };

    std::string revisionLight, revisionHeavy;
    const std::string light = planJson( docs, revisionLight );
    const std::string lightAgain = planJson( docs, revisionLight );
    REQUIRE( light == lightAgain );

    const std::string heavy = planJson( docsHeavy, revisionHeavy );
    REQUIRE( revisionHeavy != revisionLight );
    REQUIRE( heavy != light ); // the fact change is visible in the plan
}

TEST_CASE( "Undeclared slots stay empty and the planner asks instead of defaulting",
           "[planner_live]" )
{
    sicnu::preflight::CapabilityMirrorProjection mirror;
    REQUIRE( mirror.loadDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" )
             > 0 );
    LiveCapabilityProvider provider;
    std::string error;
    REQUIRE( LiveCapabilityProvider::create( mirror, provider, error ) );

    // The live mirror declares no verification or publication capability:
    // the provider must serve none, and a goal with acceptance criteria must
    // surface that as a typed blocking question — not a fabricated verify step.
    REQUIRE( provider.capabilitiesForFamily( "verification" ).empty() );
    REQUIRE( provider.capabilitiesForFamily( "publication" ).empty() );
    REQUIRE( provider.capabilitiesForFamily( "uncertainty" ).empty() );
    REQUIRE( provider.capabilitiesForFamily( "feature_stack" ).empty() );
    REQUIRE( provider.capabilitiesForFamily( "record" ).empty() );

    ScientificGoal requiring;
    requiring.goalId = "goal-live-verified";
    requiring.kind = "measurement";
    requiring.subject = "vegetation index with proof";
    requiring.acceptanceCriteria.push_back(
        GoalAcceptanceCriterion{ "acc-finite", "finite_fraction", ">= 0.99 of samples finite" } );
    auto result = planOver( provider, requiring, contextFor( "rs:ndvi" ) );
    REQUIRE( result.primary() != nullptr );
    bool askedAboutVerification = false;
    for ( const auto &question : result.primary()->openQuestions )
    {
        if ( question.kind == "decision_required"
             && question.detail.find( "verification" ) != std::string::npos )
            askedAboutVerification = true;
    }
    REQUIRE( askedAboutVerification );
    for ( const auto &step : result.primary()->steps )
        REQUIRE( step.role != "verify" );
}

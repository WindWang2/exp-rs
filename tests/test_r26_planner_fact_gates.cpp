// tests/test_r26_planner_fact_gates.cpp
//
// R26-harness planner fact-derivation oracles (fail-closed planning):
//   D — assets whose CRS is unknown make grid alignment UNVERIFIABLE: the
//       plan must surface a blocking question and must not claim plain
//       feasibility. Today an empty CRS silently joins the CRS set, so an
//       ""+EPSG:4326 pair reads as "aligned".
//   E — an analysis capability whose contract demands role-tagged bands
//       (wavelengthPolicy=band_roles) cannot be judged feasible against
//       assets that declare no band roles: blocking question + downgrade.
//       Declared roles keep the plan feasible (sufficiency of the specific
//       pair stays the science preflight's call, not the planner's guess).

#include <catch2/catch_test_macros.hpp>

#include "contracts/scientific_contract.h"
#include "planner/planner_core.h"
#include "planner/planner_rules.h"

#include <json/json.h>

#include <map>
#include <string>

using namespace sicnu::planner;

namespace
{

class FakeProvider : public CapabilityProvider
{
public:
    void add( const std::string &family, PlannerCapability capability )
    {
        capability.family = family;
        byFamily_[family].push_back( std::move( capability ) );
    }

    std::vector<PlannerCapability> capabilitiesForFamily( const std::string &family ) const override
    {
        auto it = byFamily_.find( family );
        return it == byFamily_.end() ? std::vector<PlannerCapability>{} : it->second;
    }

private:
    std::map<std::string, std::vector<PlannerCapability>> byFamily_;
};

PlannerCapability capability( const std::string &operatorId, const std::string &costClass,
                              const std::string &inputDomain, const std::string &outputDomain,
                              bool deterministic = true, long long ramMb = 256 )
{
    PlannerCapability capability;
    capability.operatorId = operatorId;
    capability.costClass = costClass;
    capability.inputDomain = inputDomain;
    capability.outputDomain = outputDomain;
    capability.deterministic = deterministic;
    capability.estimatedRamMb = ramMb;
    return capability;
}

ScientificGoal changeGoal()
{
    ScientificGoal goal;
    goal.goalId = "goal-r26";
    goal.kind = "change";
    goal.subject = "water extent change";
    goal.acceptanceCriteria = { GoalAcceptanceCriterion{ "acc-1", "change map accuracy",
                                                         "kappa >= 0.8" } };
    return goal;
}

PlannerAssetFacts dnAsset( const std::string &ref, const std::string &crs,
                           const std::vector<std::string> &bandRoles = {} )
{
    PlannerAssetFacts asset;
    asset.ref = ref;
    asset.kind = "raster";
    asset.modality = "optical";
    asset.numericDomain = "dn";
    asset.crs = crs;
    asset.resolutionM = 10.0;
    asset.dates = { "2024-01-10" };
    asset.state = "ready";
    asset.bandRoles = bandRoles;
    return asset;
}

FakeProvider fullChangeProvider()
{
    FakeProvider provider;
    provider.add( "data_import", capability( "rs:mosaic", "low", "", "" ) );
    provider.add( "alignment", capability( "rs:align", "low", "", "" ) );
    provider.add( "calibration", capability( "rs:brdf_normalization", "medium", "dn", "reflectance" ) );
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );
    provider.add( "verification", capability( "rs:regress", "low", "", "" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );
    return provider;
}

const ScientificPlan &primaryOf( const PlanningResult &result )
{
    REQUIRE( result.candidates.size() == 1 );
    return *result.primary();
}

bool hasBlockingQuestionMentioning( const ScientificPlan &plan, const std::string &needle )
{
    for ( const PlanOpenQuestion &question : plan.openQuestions )
    {
        if ( !question.blocking )
            continue;
        if ( question.detail.find( needle ) != std::string::npos )
            return true;
    }
    return false;
}

} // namespace

TEST_CASE( "D: an unknown CRS makes alignment unverifiable, not silently compatible",
           "[planner][r26][crs]" )
{
    const ScientificGoal goal = changeGoal();
    FakeProvider provider = fullChangeProvider();
    PlannerProviders providers{ &provider };

    SECTION( "empty CRS plus a declared CRS is not verifiable as aligned" )
    {
        PlanningContext context;
        // One asset declares no CRS ("" = unknown per PlanningContext), the
        // other declares EPSG:4326.
        context.assets = { dnAsset( "asset-unknown", "" ), dnAsset( "asset-known", "EPSG:4326" ) };
        context.mode = ModePolicy{ "agent", "full", false };

        const ScientificPlan &plan = primaryOf( planScientificWork( goal, context, providers ) );

        CHECK( plan.verdict != "feasible" );
        CHECK( hasBlockingQuestionMentioning( plan, "CRS" ) );
    }

    SECTION( "both CRS declared and equal stays feasible (control)" )
    {
        PlanningContext context;
        // band roles are declared so the control isolates the CRS axis —
        // the band-role gate (E) must stay silent here.
        context.assets = { dnAsset( "asset-a", "EPSG:32648", { "green", "nir" } ),
                           dnAsset( "asset-b", "EPSG:32648", { "green", "nir" } ) };
        context.mode = ModePolicy{ "agent", "full", false };

        const ScientificPlan &plan = primaryOf( planScientificWork( goal, context, providers ) );

        CHECK( plan.verdict == "feasible" );
        CHECK( plan.openQuestions.empty() );
    }
}

TEST_CASE( "E: band-role-demanded analysis with undeclared roles is not silently feasible",
           "[planner][r26][band_roles]" )
{
    const ScientificGoal goal = changeGoal();
    FakeProvider provider = fullChangeProvider();
    PlannerProviders providers{ &provider };

    // Ground truth: the chosen analysis operator's contract demands
    // role-tagged bands (indexFamily → wavelengthPolicy=band_roles).
    const sicnu::contracts::ScientificContract *contract =
      sicnu::contracts::findScientificContract( "rs:mndwi" );
    REQUIRE( contract != nullptr );
    REQUIRE( contract->wavelengthPolicy == "band_roles" );

    SECTION( "no consumable asset declares band roles" )
    {
        PlanningContext context;
        context.assets = { dnAsset( "asset-a", "EPSG:32648" ), dnAsset( "asset-b", "EPSG:32648" ) };
        context.mode = ModePolicy{ "agent", "full", false };

        const ScientificPlan &plan = primaryOf( planScientificWork( goal, context, providers ) );

        CHECK( plan.verdict != "feasible" );
        CHECK( hasBlockingQuestionMentioning( plan, "band" ) );
    }

    SECTION( "declared band roles keep the plan feasible (control)" )
    {
        PlanningContext context;
        context.assets = { dnAsset( "asset-a", "EPSG:32648", { "green", "nir" } ),
                           dnAsset( "asset-b", "EPSG:32648", { "green", "nir" } ) };
        context.mode = ModePolicy{ "agent", "full", false };

        const ScientificPlan &plan = primaryOf( planScientificWork( goal, context, providers ) );

        CHECK( plan.verdict == "feasible" );
        CHECK( plan.openQuestions.empty() );
    }
}

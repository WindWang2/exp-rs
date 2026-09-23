// tests/test_scientific_planner_core.cpp — slice B:
// deterministic goal→plan baseline: staged spine, numeric-domain gating with
// contracts-verified bridges, grid gate, verifier targets, typed questions,
// verdicts, deterministic replay. Provider fixtures use REAL operator ids so
// the linked contracts registry gates every fact (real-shaped integration).
#include <catch2/catch_test_macros.hpp>

#include "contracts/scientific_contract.h"
#include "planner/json_util.h"
#include "planner/planner_core.h"
#include "planner/planner_rules.h"

#include <json/json.h>

#include <map>

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

ScientificGoal changeGoal( int minScenes = 0 )
{
    ScientificGoal goal;
    goal.goalId = "goal-water";
    goal.kind = "change";
    goal.subject = "water extent change";
    if ( minScenes > 0 )
        goal.temporalScope = GoalTemporalScope{ "2024-01-10", "2024-03-10", minScenes, 16 };
    goal.acceptanceCriteria = { GoalAcceptanceCriterion{ "acc-1", "change map accuracy",
                                                         "kappa >= 0.8" } };
    return goal;
}

PlannerAssetFacts dnAsset( const std::string &ref, const std::string &date,
                           const std::string &crs = "EPSG:32648", double resolution = 10.0,
                           const std::string &state = "ready" )
{
    PlannerAssetFacts asset;
    asset.ref = ref;
    asset.kind = "raster";
    asset.modality = "optical";
    asset.numericDomain = "dn";
    asset.crs = crs;
    asset.resolutionM = resolution;
    asset.dates = { date };
    asset.state = state;
    return asset;
}

PlanningContext twoAssetContext( const std::string &crsA = "EPSG:32648",
                                 const std::string &crsB = "EPSG:32648" )
{
    PlanningContext context;
    context.assets = { dnAsset( "asset-a", "2024-01-10", crsA ),
                       dnAsset( "asset-b", "2024-03-10", crsB ) };
    context.mode = ModePolicy{ "agent", "full", false };
    return context;
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

std::string sequenceOf( const ScientificPlan &plan )
{
    return scientificPlanSequenceSummary( plan );
}
} // namespace

TEST_CASE( "contracts registry is the linked authority for real operator ids",
           "[scientific_planner][core][authority]" )
{
    // The planner gates provider facts against THIS registry (single truth).
    CHECK( sicnu::contracts::findScientificContract( "rs:mndwi" ) != nullptr );
    CHECK( sicnu::contracts::findScientificContract( "rs:brdf_normalization" ) != nullptr );
    CHECK( sicnu::contracts::findScientificContract( "rs:no_such_operator" ) == nullptr );
    CHECK( capabilityAgreesWithContracts( capability( "rs:mndwi", "low", "", "index" ) ) );
    // a provider fact that lies about the contract is rejected
    CHECK_FALSE( capabilityAgreesWithContracts( capability( "rs:mndwi", "low", "dn", "classes" ) ) );
    // "any" on either side is the wildcard the vocabulary defines
    CHECK( capabilityAgreesWithContracts( capability( "rs:mosaic", "low", "", "" ) ) );
}

TEST_CASE( "change goal produces the staged spine deterministically",
           "[scientific_planner][core]" )
{
    const ScientificGoal goal = changeGoal();
    const PlanningContext context = twoAssetContext();
    FakeProvider provider = fullChangeProvider();
    PlannerProviders providers{ &provider };

    const PlanningResult result = planScientificWork( goal, context, providers );
    REQUIRE( result.candidates.size() == 1 );
    const ScientificPlan &plan = *result.primary();
    CHECK( plan.verdict == "feasible" );
    CHECK( plan.openQuestions.empty() );
    CHECK( plan.rulesRevision == std::string( kPlannerRulesRevision ) );
    // import → analyze → verify → publish (no gates fired)
    CHECK( sequenceOf( plan ) == "import:rs:mosaic->analyze:rs:mndwi->verify:rs:regress->publish:io:translate" );
    // acceptance criteria landed as verifier targets on the verify stage
    const PlannerStep *verifyStep = nullptr;
    for ( const auto &step : plan.steps )
    {
        if ( step.role == "verify" )
            verifyStep = &step;
    }
    REQUIRE( verifyStep != nullptr );
    REQUIRE( verifyStep->verifierTargets.size() == 1 );
    CHECK( verifyStep->verifierTargets[0].target == "kappa >= 0.8" );
    CHECK( validateScientificPlan( plan ).empty() );

    // deterministic replay: byte-identical canonical JSON and fingerprints
    const PlanningResult replay = planScientificWork( goal, context, providers );
    REQUIRE( replay.candidates.size() == result.candidates.size() );
    for ( size_t i = 0; i < result.candidates.size(); ++i )
    {
        CHECK( json_util::canonicalCompact( scientificPlanToJson( replay.candidates[i] ) )
               == json_util::canonicalCompact( scientificPlanToJson( result.candidates[i] ) ) );
        CHECK( scientificPlanFingerprint( replay.candidates[i] )
               == scientificPlanFingerprint( result.candidates[i] ) );
    }
}

TEST_CASE( "numeric-domain gate inserts a contracts-verified calibration bridge",
           "[scientific_planner][core]" )
{
    ScientificGoal goal = changeGoal();
    goal.acceptanceCriteria.clear();
    PlanningContext context = twoAssetContext();
    FakeProvider provider;
    provider.add( "data_import", capability( "rs:mosaic", "low", "", "" ) );
    provider.add( "calibration", capability( "rs:band_math", "low", "dn", "features" ) );
    provider.add( "analysis", capability( "rs:change", "medium", "features", "none" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    REQUIRE( result.candidates.size() == 1 );
    const ScientificPlan &plan = *result.primary();
    CHECK( plan.verdict == "feasible" );
    // import → calibration bridge (dn→features) → analyze → publish
    CHECK( sequenceOf( plan )
          == "import:rs:mosaic->preprocess:rs:band_math->analyze:rs:change->publish:io:translate" );
    const PlannerStep &bridge = plan.steps[1];
    REQUIRE( bridge.expectedTransitions.size() == 2 );
    CHECK( bridge.expectedTransitions[0].fromDomain == "dn" );
    CHECK( bridge.expectedTransitions[0].toDomain == "features" );
}

TEST_CASE( "unbridgeable numeric domain raises a typed blocking question, never a fake bridge",
           "[scientific_planner][core]" )
{
    ScientificGoal goal = changeGoal();
    goal.acceptanceCriteria.clear();
    PlanningContext context = twoAssetContext();
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:change", "medium", "features", "none" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    REQUIRE( result.candidates.size() == 1 );
    const ScientificPlan &plan = *result.primary();
    CHECK( plan.verdict == "feasible_with_gaps" );
    REQUIRE_FALSE( plan.openQuestions.empty() );
    bool found = false;
    for ( const auto &question : plan.openQuestions )
    {
        if ( question.kind == "insufficient_data" && question.blocking )
            found = question.detail.find( "dn→features" ) != std::string::npos;
    }
    CHECK( found );
    // no calibration step was invented
    for ( const auto &step : plan.steps )
        CHECK( step.family != "calibration" );
}

TEST_CASE( "grid mismatch inserts the alignment stage", "[scientific_planner][core]" )
{
    ScientificGoal goal = changeGoal();
    goal.acceptanceCriteria.clear();
    PlanningContext context = twoAssetContext( "EPSG:32648", "EPSG:32647" );
    FakeProvider provider;
    provider.add( "data_import", capability( "rs:mosaic", "low", "", "" ) );
    provider.add( "alignment", capability( "rs:align", "low", "", "" ) );
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    REQUIRE( result.candidates.size() == 1 );
    const ScientificPlan &plan = *result.primary();
    CHECK( sequenceOf( plan )
          == "import:rs:mosaic->preprocess:rs:align->analyze:rs:mndwi->publish:io:translate" );
}

TEST_CASE( "non-ready assets become blocking typed questions", "[scientific_planner][core]" )
{
    ScientificGoal goal = changeGoal();
    goal.acceptanceCriteria.clear();
    PlanningContext context = twoAssetContext();
    context.assets[1].state = "offline";
    FakeProvider provider = fullChangeProvider();

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    const ScientificPlan &plan = *result.primary();
    CHECK( plan.verdict == "feasible_with_gaps" );
    bool found = false;
    for ( const auto &question : plan.openQuestions )
    {
        if ( question.kind == "insufficient_data" && question.blocking
             && question.detail.find( "asset-b" ) != std::string::npos
             && question.detail.find( "offline" ) != std::string::npos )
            found = true;
    }
    CHECK( found );
}

TEST_CASE( "min-scenes gap raises a blocking insufficient-data question",
           "[scientific_planner][core]" )
{
    ScientificGoal goal = changeGoal( 3 );
    goal.acceptanceCriteria.clear();
    PlanningContext context = twoAssetContext();
    FakeProvider provider = fullChangeProvider();

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    const ScientificPlan &plan = *result.primary();
    CHECK( plan.verdict == "feasible_with_gaps" );
    bool found = false;
    for ( const auto &question : plan.openQuestions )
    {
        if ( question.kind == "insufficient_data" && question.blocking
             && question.detail.find( "min 3" ) != std::string::npos )
            found = true;
    }
    CHECK( found );
}

TEST_CASE( "unlawful situations: unknown kind, missing seam, no analysis candidate",
           "[scientific_planner][core]" )
{
    const PlanningContext context = twoAssetContext();

    ScientificGoal unknownKind = changeGoal();
    unknownKind.kind = "alchemy";
    const PlanningResult unknown =
        planScientificWork( unknownKind, context, PlannerProviders{ nullptr } );
    REQUIRE( unknown.candidates.size() == 1 );
    CHECK( unknown.primary()->verdict == "infeasible" );
    CHECK_FALSE( unknown.primary()->verdictReasons.empty() );

    ScientificGoal goal = changeGoal();
    goal.acceptanceCriteria.clear();
    const PlanningResult noSeam = planScientificWork( goal, context, PlannerProviders{ nullptr } );
    CHECK( noSeam.primary()->verdict == "infeasible" );
    bool seamReason = false;
    for ( const auto &reason : noSeam.primary()->verdictReasons )
    {
        if ( reason.find( "provider seam" ) != std::string::npos )
            seamReason = true;
    }
    CHECK( seamReason );

    FakeProvider emptyProvider;
    emptyProvider.add( "publication", capability( "io:translate", "low", "", "" ) );
    const PlanningResult noAnalysis =
        planScientificWork( goal, context, PlannerProviders{ &emptyProvider } );
    CHECK( noAnalysis.primary()->verdict == "infeasible" );
    bool blockingQuestion = false;
    for ( const auto &question : noAnalysis.primary()->openQuestions )
    {
        if ( question.kind == "insufficient_data" && question.blocking )
            blockingQuestion = true;
    }
    CHECK( blockingQuestion );
}

TEST_CASE( "provider facts contradicting contracts are excluded with a typed ambiguity",
           "[scientific_planner][core]" )
{
    ScientificGoal goal = changeGoal();
    goal.acceptanceCriteria.clear();
    PlanningContext context = twoAssetContext();
    FakeProvider provider;
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "classes" ) ); // lies: contract says index
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    // the only analysis candidate lied → infeasible, with the typed ambiguity
    CHECK( result.primary()->verdict == "infeasible" );
    bool ambiguity = false;
    for ( const auto &question : result.primary()->openQuestions )
    {
        if ( question.kind == "ambiguity"
             && question.detail.find( "contradicts the contracts registry" ) != std::string::npos )
            ambiguity = true;
    }
    CHECK( ambiguity );
}

TEST_CASE( "emitted oracle: every candidate re-reads through the fail-closed reader",
           "[scientific_planner][core][round-trip]" )
{
    // The producer and the reader share one schema truth: anything the
    // planner emits must survive scientificPlanFromJson with an empty
    // validateScientificPlan, ids and bounds included.
    ScientificGoal goal = changeGoal();
    goal.acceptanceCriteria.clear();
    PlanningContext context = twoAssetContext();
    FakeProvider provider;
    provider.add( "data_import", capability( "rs:mosaic", "low", "", "" ) );
    provider.add( "calibration", capability( "rs:brdf_normalization", "medium", "dn", "reflectance" ) );
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );
    provider.add( "analysis", capability( "rs:ndwi", "low", "", "index" ) );
    provider.add( "analysis", capability( "rs:change", "high", "features", "none" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    REQUIRE_FALSE( result.candidates.empty() );
    for ( const auto &plan : result.candidates )
    {
        ScientificPlan reparsed;
        std::string error;
        INFO( "plan " << plan.planId );
        REQUIRE( scientificPlanFromJson( scientificPlanToJson( plan ), reparsed, error ) );
        CHECK( validateScientificPlan( reparsed ).empty() );
        CHECK( scientificPlanFingerprint( reparsed ) == scientificPlanFingerprint( plan ) );
    }
}

TEST_CASE( "sibling gaps stay visible on the primary but never flip its verdict",
           "[scientific_planner][core]" )
{
    ScientificGoal goal = changeGoal();
    goal.acceptanceCriteria.clear();
    PlanningContext context = twoAssetContext();
    FakeProvider provider;
    provider.add( "data_import", capability( "rs:mosaic", "low", "", "" ) );
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );
    provider.add( "analysis", capability( "rs:change", "high", "features", "none" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    REQUIRE( result.candidates.size() == 2 );
    const ScientificPlan &primary = *result.primary();
    CHECK( primary.verdict == "feasible" );
    REQUIRE( primary.openQuestions.size() == 1 );
    CHECK_FALSE( primary.openQuestions[0].blocking );
    CHECK( primary.openQuestions[0].detail.find( "sibling candidate alt-1" )
           != std::string::npos );
    CHECK( result.candidates[1].verdict == "feasible_with_gaps" );
}

TEST_CASE( "multiple lawful analysis variants become ranked candidates with alternatives",
           "[scientific_planner][core]" )
{
    ScientificGoal goal = changeGoal();
    goal.acceptanceCriteria.clear();
    PlanningContext context = twoAssetContext();
    FakeProvider provider;
    provider.add( "data_import", capability( "rs:mosaic", "low", "", "" ) );
    provider.add( "analysis", capability( "rs:mndwi", "low", "", "index" ) );
    provider.add( "analysis", capability( "rs:ndwi", "low", "", "index" ) );
    provider.add( "analysis", capability( "rs:change", "high", "features", "none" ) );
    provider.add( "publication", capability( "io:translate", "low", "", "" ) );

    const PlanningResult result = planScientificWork( goal, context, PlannerProviders{ &provider } );
    REQUIRE( result.candidates.size() == 3 );
    const ScientificPlan &primary = *result.primary();
    CHECK( primary.alternatives.size() == 2 );
    CHECK( primary.alternatives[0].candidateIndex == 1 );
    CHECK( primary.alternatives[0].alternativeId == "alt-1" );
    CHECK_FALSE( primary.alternatives[0].whyNot.empty() );
    // deterministic ranking: low cost classes first, operator id tiebreak
    CHECK( sequenceOf( primary ).find( "rs:mndwi" ) != std::string::npos );
    CHECK( sequenceOf( result.candidates[1] ).find( "rs:ndwi" ) != std::string::npos );
    CHECK( sequenceOf( result.candidates[2] ).find( "rs:change" ) != std::string::npos );
    CHECK( costClassRank( result.candidates[2].cost.aggregateCostClass )
           >= costClassRank( primary.cost.aggregateCostClass ) );
}

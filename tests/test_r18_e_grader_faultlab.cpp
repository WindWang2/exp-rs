/***************************************************************************
  tests/test_r18_e_grader_faultlab.cpp — R18-E E04/E06 lanes.

  RED-first pins:

    V1 (grader): AtMost + linearWindow partial credit was dead code — the
      matcher's AtMost branch required windowFrom > windowTo while the
      loader only accepts windowTo > windowFrom, so every parseable AtMost
      window rubric silently awarded 0 below the band instead of the
      declared partial credit. The window is oriented exactly like
      AtLeast's (from = full-credit edge, to = zero-credit edge).

    V3 (faultlab): numeric expectations fabricated delta = 0.0 when the
      clean baseline observable was absent — Equals/DeltaGe/DeltaLe/
      NotEquals passed vacuously on zero evidence (#1521 closed only the
      Changed/Text/Truth paths). The loader also accepted operand-less
      delta relations that grade against the 0.0 default.

    V6 (faultlab): the declared `expected.verifier` block was loaded,
      stored and never enforced — a declared expectation silently
      dropped. The runner must mark it unevaluated in the report.

    V10 (faultlab): grid_shift {dx:0, dy:0} was accepted as an applied
      fault (mutations=1, nothing moved) — every other transform family
      refuses identity parameters.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "faultlab/fault_expectations.h"
#include "faultlab/fault_observables.h"
#include "faultlab/fault_diagnosis.h"
#include "faultlab/fault_report.h"
#include "faultlab/fault_runner.h"
#include "faultlab/fault_types.h"
#include "grader/grader_engine.h"
#include "grader/grader_types.h"

#include <json/json.h>

#include <cmath>
#include <string>
#include <vector>

using namespace sicnu::faultlab;
using namespace sicnu::grader;

// ---------------------------------------------------------------------------
// V1 — AtMost linear window
// ---------------------------------------------------------------------------

namespace
{

GradingRubric atMostWindowRubric()
{
    GradingRubric rubric;
    rubric.rubricId = "r18e-atmost-window";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "result";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "cloud_ratio";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Metric;
    c.evidenceKey = "cloud_ratio";
    c.metric.mode = MetricExpectation::Mode::AtMost;
    c.metric.value = 1.0;          // full credit at <= 1.0
    c.metric.hasLinearWindow = true;
    c.metric.windowFrom = 1.2;     // decay starts (mirrors AtLeast's floor)
    c.metric.windowTo = 1.6;       // zero credit at and beyond
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );
    return rubric;
}

GradeEvidence metricEvidence( double value )
{
    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    GradeEvidenceItem item;
    item.evidenceId = "ev-m1";
    item.kind = EvidenceKind::Metric;
    item.key = "cloud_ratio";
    item.hasValue = true;
    item.value = value;
    evidence.items.push_back( item );
    return evidence;
}

} // namespace

TEST_CASE( "r18e V1: AtMost with a linear window awards the declared partial credit",
           "[grader][metric][r18_e]" )
{
    const GradingRubric rubric = atMostWindowRubric();

    // Inside the band: full.
    const GradeOutcome at = grade( rubric, metricEvidence( 1.0 ) );
    REQUIRE( at.ok );
    CHECK( at.report.dimensions[0].criteria[0].status == OutcomeStatus::Earned );

    // Mid-window: half credit — the exact mirror of the pinned AtLeast
    // window case ((0.725-0.60)/(0.85-0.60) = 1/2 in test_grader_engine).
    const GradeOutcome mid = grade( rubric, metricEvidence( 1.4 ) );
    REQUIRE( mid.ok );
    const CriterionOutcome &o = mid.report.dimensions[0].criteria[0];
    CHECK( o.status == OutcomeStatus::Partial );
    CHECK( o.rawEarned > 49.99 );
    CHECK( o.rawEarned < 50.01 );
    CHECK( o.reasonCodes[0] == "grader:metric-partial-window" );

    // Inside the ramp: 1.25 sits one-eighth into [1.2, 1.6] from the top
    // — (1.6-1.25)/0.4 = 0.875 of the points.
    const GradeOutcome nearFull = grade( rubric, metricEvidence( 1.25 ) );
    REQUIRE( nearFull.ok );
    const CriterionOutcome &n = nearFull.report.dimensions[0].criteria[0];
    CHECK( n.status == OutcomeStatus::Partial );
    CHECK( n.rawEarned > 87.4 );
    CHECK( n.rawEarned < 87.6 );

    // The gap between the full band (<=1.0) and windowFrom (1.2) earns
    // FULL credit via the clamp — the exact AtLeast mirror, where the
    // (windowTo, threshold) gap earns full the same way.
    for ( const double gapValue : { 1.05, 1.19, 1.2 } )
    {
        const GradeOutcome gap = grade( rubric, metricEvidence( gapValue ) );
        REQUIRE( gap.ok );
        const CriterionOutcome &g = gap.report.dimensions[0].criteria[0];
        INFO( "value=" << gapValue << " earned=" << g.rawEarned );
        CHECK( g.rawEarned > 99.9 );
        CHECK( g.rawEarned <= 100.0 );
    }

    // Beyond the window: zero.
    const GradeOutcome far = grade( rubric, metricEvidence( 1.7 ) );
    REQUIRE( far.ok );
    CHECK( far.report.dimensions[0].criteria[0].status == OutcomeStatus::NotEarned );
    CHECK( far.report.dimensions[0].criteria[0].reasonCodes[0] == "grader:metric-above-band" );
}

// ---------------------------------------------------------------------------
// V3 — no vacuous numeric relations without a clean baseline
// ---------------------------------------------------------------------------

namespace
{

ObservableSet setOf( const std::string &id, double number )
{
    ObservableSet set;
    Observable observable;
    observable.id = id;
    observable.kind = ObservableKind::Number;
    observable.number = number;
    set[ id ] = observable;
    return set;
}

ObservableExpectation expectation( const std::string &id, ExpectationRelation relation,
                                   double value )
{
    ObservableExpectation e;
    e.id = id;
    e.relation = relation;
    e.value = value;
    return e;
}

} // namespace

TEST_CASE( "r18e V3: numeric delta relations fail honestly without a clean baseline",
           "[faultlab][expectations][r18_e]" )
{
    // The faulted run produced the observable; the CLEAN run never did.
    const ObservableSet clean;                       // no "index_mean"
    const ObservableSet faulted = setOf( "index_mean", 5.0 );

    const std::vector<ObservableExpectation> cases = {
        expectation( "index_mean", ExpectationRelation::Equals, 0.0 ),
        expectation( "index_mean", ExpectationRelation::DeltaGe, -1.0 ),
        expectation( "index_mean", ExpectationRelation::DeltaLe, 1.0 ),
        expectation( "index_mean", ExpectationRelation::NotEquals, 5.0 ),
    };
    const auto results = checkExpectations( clean, faulted, cases );
    REQUIRE( results.size() == 4 );
    for ( const ExpectationResult &result : results )
    {
        CHECK_FALSE( result.passed );
        CHECK( result.note.find( "baseline" ) != std::string::npos );
    }
}

TEST_CASE( "r18e V3b: baseline-free relations keep their verdicts",
           "[faultlab][expectations][r18_e]" )
{
    const ObservableSet clean;
    const ObservableSet faulted = setOf( "index_mean", 0.5 );

    const auto results = checkExpectations(
        clean, faulted,
        { expectation( "index_mean", ExpectationRelation::InRange, 0.0 ) } );
    REQUIRE( results.size() == 1 );
    // in_range judges the faulted value alone — no baseline needed.
    // (rangeLo/rangeHi default to 0 here; 0.5 is outside [0,0].)
    CHECK_FALSE( results[0].passed );

    const auto inside = checkExpectations(
        clean, faulted,
        { expectation( "index_mean", ExpectationRelation::InRange, 0.0 ) } );
    (void)inside;

    ObservableExpectation inRange = expectation( "index_mean", ExpectationRelation::InRange, 0.0 );
    inRange.rangeLo = 0.0;
    inRange.rangeHi = 1.0;
    const auto pass = checkExpectations( clean, faulted, { inRange } );
    REQUIRE( pass.size() == 1 );
    CHECK( pass[0].passed );
}

// ---------------------------------------------------------------------------
// V3c — the loader refuses operand-less delta relations
// ---------------------------------------------------------------------------

namespace
{

Json::Value validScenarioDoc()
{
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = kFaultScenarioSchemaId;
    doc["scenario_id"] = "fs_band_role_swap_ndvi";
    doc["title"] = "Band role swap on an index pair";

    Json::Value fault( Json::objectValue );
    fault["family"] = "band_role_swap";
    Json::Value params( Json::objectValue );
    params["role_a"] = "red";
    params["role_b"] = "nir";
    fault["params"] = params;
    fault["seed"] = 42u;
    doc["fault"] = fault;

    Json::Value fixture( Json::objectValue );
    fixture["fixture_id"] = "two_band_index_pair";
    fixture["params"] = Json::objectValue;
    fixture["seed"] = 42u;
    doc["base_fixture"] = fixture;

    Json::Value expected( Json::objectValue );
    Json::Value obs( Json::arrayValue );
    Json::Value o1( Json::objectValue );
    o1["id"] = "band_roles";
    o1["relation"] = "changed";
    obs.append( o1 );
    Json::Value o2( Json::objectValue );
    o2["id"] = "index_mean";
    o2["relation"] = "delta_le";
    o2["value"] = -0.5;
    obs.append( o2 );
    expected["observables"] = obs;
    Json::Value diagnosis( Json::objectValue );
    diagnosis["signature"] = "all_negative_index";
    expected["diagnosis"] = diagnosis;
    doc["expected"] = expected;

    Json::Value lo( Json::objectValue );
    lo["id"] = "LO-03";
    lo["statement"] = "Recognize that band metadata, not band position, defines an index.";
    doc["learning_objective"] = lo;
    return doc;
}

} // namespace

TEST_CASE( "r18e V3c: a delta relation without its operand is refused at load",
           "[faultlab][loader][r18_e]" )
{
    for ( const char *relation : { "delta_ge", "delta_le", "equals", "not_equals" } )
    {
        Json::Value doc = validScenarioDoc();
        Json::Value obs( Json::arrayValue );
        Json::Value o( Json::objectValue );
        o["id"] = "index_mean";
        o["relation"] = relation; // no value member
        obs.append( o );
        doc["expected"]["observables"] = obs;
        const auto result = loadFaultScenario( doc );
        CHECK_FALSE( result.ok );
    }
}

// ---------------------------------------------------------------------------
// V6 — the declared verifier block is surfaced, never silently dropped
// ---------------------------------------------------------------------------

TEST_CASE( "r18e V6: a declared expected.verifier marks the report unevaluated",
           "[faultlab][runner][r18_e]" )
{
    Json::Value doc = validScenarioDoc();
    Json::Value verifier( Json::objectValue );
    verifier["rules_ref"] = "labs/grading/ndvi_basics.rules.json";
    Json::Value ids( Json::arrayValue );
    ids.append( "band_count" );
    verifier["assertion_ids"] = ids;
    doc["expected"]["verifier"] = verifier;

    const auto scenario = loadFaultScenario( doc );
    REQUIRE( scenario.ok );

    const auto run = runFaultScenario( scenario.value );
    REQUIRE( run.ok );
    // The declared verifier expectations were NOT evaluated by this runner —
    // the report must say so instead of letting the block vanish.
    CHECK( run.value.verifierDeclUnevaluated );
    const Json::Value body = run.value.toJson();
    CHECK( body.get( "verifier_decl_unevaluated", Json::Value( false ) ).asBool() );

    // Without a declared block the flag stays quiet.
    const auto quiet = runFaultScenario( loadFaultScenario( validScenarioDoc() ).value );
    REQUIRE( quiet.ok );
    CHECK_FALSE( quiet.value.verifierDeclUnevaluated );
}

// ---------------------------------------------------------------------------
// V10 — identity grid_shift is refused like every other identity transform
// ---------------------------------------------------------------------------

TEST_CASE( "r18e V10: grid_shift of (0,0) is refused as a no-op fault",
           "[faultlab][transforms][r18_e]" )
{
    Json::Value doc = validScenarioDoc();
    doc["scenario_id"] = "fs_grid_shift_zero";
    doc["fault"]["family"] = "grid_shift";
    Json::Value params( Json::objectValue );
    params["dx"] = 0.0;
    params["dy"] = 0.0;
    doc["fault"]["params"] = params;
    // A fixture with a geo transform: reuse the index pair fixture shape.
    doc["base_fixture"]["fixture_id"] = "two_band_index_pair";

    const auto scenario = loadFaultScenario( doc );
    REQUIRE( scenario.ok );
    const auto run = runFaultScenario( scenario.value );
    REQUIRE( run.ok );
    CHECK_FALSE( run.value.faultApplied );
    CHECK_FALSE( run.value.passed );
    bool refused = false;
    for ( const auto &d : run.value.diagnostics )
        if ( d.code.find( "fault_unsupported_params" ) != std::string::npos )
            refused = true;
    CHECK( refused );
}

// ---------------------------------------------------------------------------
// V8 — a stage criterion must declare its expected state, not inherit one
// ---------------------------------------------------------------------------

TEST_CASE( "r18e V8: a stage criterion without a stage block is refused at parse",
           "[grader][rubric][r18_e]" )
{
    const auto buildDoc = []( bool withStage ) {
        Json::Value doc( Json::objectValue );
        doc["schema"] = kRubricSchemaId;
        doc["rubricId"] = "r18e-stage";
        doc["revision"] = 1;
        doc["totalPoints"] = 100.0;
        doc["passingScore"] = 60.0;
        Json::Value dimension( Json::objectValue );
        dimension["dimensionId"] = "process";
        dimension["weight"] = 100.0;
        Json::Value criterion( Json::objectValue );
        criterion["criterionId"] = "proc-pre";
        criterion["kind"] = "stage";
        criterion["evidenceKey"] = "preprocess";
        criterion["maxPoints"] = 100.0;
        if ( withStage )
        {
            Json::Value stage( Json::objectValue );
            stage["expectedState"] = "Completed";
            criterion["stage"] = stage;
        }
        Json::Value criteria( Json::arrayValue );
        criteria.append( criterion );
        dimension["criteria"] = criteria;
        Json::Value dimensions( Json::arrayValue );
        dimensions.append( dimension );
        doc["dimensions"] = dimensions;
        return doc;
    };

    // Control: with the stage block the rubric parses.
    GraderError okError;
    const auto withStage = GradingRubric::fromJson( buildDoc( true ), okError );
    REQUIRE( withStage.has_value() );

    // Without the block the parser used to grade silently against an
    // invisible expectedState="Completed" default.
    GraderError error;
    const auto withoutStage = GradingRubric::fromJson( buildDoc( false ), error );
    CHECK_FALSE( withoutStage.has_value() );
    CHECK( error.message.find( "expectedState" ) != std::string::npos );
}

// ---------------------------------------------------------------------------
// V10b — the diagnosis rules need a clean baseline, not just a faulted value
// ---------------------------------------------------------------------------

TEST_CASE( "r18e V10b: all_negative_index never fires without a clean baseline",
           "[faultlab][diagnosis][r18_e]" )
{
    // R1 review P2-7: the rule used to accept "!cleanPresent || clean>=0" —
    // a MISSING clean observable satisfied it, fabricating the signature
    // from no evidence (the vacuous class the expectation engine closed).
    ObservableSet clean; // no index_max at all
    ObservableSet faulted = setOf( "index_max", -0.5 );

    const std::string signature = diagnoseTransition( clean, faulted );
    CHECK( signature != "all_negative_index" );

    ObservableSet cleanOk = setOf( "index_max", 0.3 );
    CHECK( diagnoseTransition( cleanOk, faulted ) == "all_negative_index" );
}

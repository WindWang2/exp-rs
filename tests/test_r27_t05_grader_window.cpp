// tests/test_r27_t05_grader_window.cpp — #1744 (WE-11) + #1773 (WE-12).
//
// #1744: an AtLeast/AtMost linear window must never award full credit to a
// value that fails the band, and the loader must refuse windows that sit on
// the passing side of the band (or straddle it). Oracles cover values
// below / equal / above the threshold, boundary tolerance, and the verdict
// cascade when a window-capped criterion is the deciding evidence.
//
// #1773: requiredStages gate the verdict — a pass-level score with a
// missing required stage reads Partial, never Pass.
#include "grader/grader_engine.h"
#include "grader/grader_error.h"
#include "grader/grader_types.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace sicnu::grader;

namespace {

/// Minimal one-criterion metric rubric in C++ (the scoring path; loader
/// oracles build the JSON form instead).
GradingRubric metricRubric( MetricExpectation::Mode mode, double value, double tolerance,
                            double windowFrom, double windowTo )
{
    GradingRubric rubric;
    rubric.rubricId = "r27t05-window";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "result";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "metric";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Metric;
    c.evidenceKey = "m";
    c.metric.mode = mode;
    c.metric.value = value;
    c.metric.tolerance = tolerance;
    c.metric.hasLinearWindow = true;
    c.metric.windowFrom = windowFrom;
    c.metric.windowTo = windowTo;
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
    item.key = "m";
    item.hasValue = true;
    item.value = value;
    evidence.items.push_back( item );
    return evidence;
}

const CriterionOutcome &soleCriterion( const GradeOutcome &outcome )
{
    return outcome.report.dimensions.at( 0 ).criteria.at( 0 );
}

Json::Value windowRubricJson( const char *mode, double value, double tolerance,
                              double from, double to )
{
    Json::Value doc{ Json::objectValue };
    doc["schema"] = "sicnu.grader.rubric/1";
    doc["rubricId"] = "r27t05-window";
    doc["revision"] = 1;
    doc["title"] = "window loader";
    doc["totalPoints"] = 100.0;
    doc["passingScore"] = 60.0;
    Json::Value criterion{ Json::objectValue };
    criterion["criterionId"] = "metric";
    criterion["title"] = "metric";
    criterion["maxPoints"] = 100.0;
    criterion["kind"] = "metric";
    criterion["evidenceKey"] = "m";
    criterion["metric"]["mode"] = mode;
    criterion["metric"]["value"] = value;
    if ( tolerance > 0.0 )
        criterion["metric"]["tolerance"] = tolerance;
    criterion["metric"]["linearWindow"]["from"] = from;
    criterion["metric"]["linearWindow"]["to"] = to;
    Json::Value dim{ Json::objectValue };
    dim["dimensionId"] = "result";
    dim["title"] = "Result";
    dim["weight"] = 100.0;
    dim["criteria"] = Json::Value{ Json::arrayValue };
    dim["criteria"].append( criterion );
    doc["dimensions"] = Json::Value{ Json::arrayValue };
    doc["dimensions"].append( dim );
    return doc;
}

} // namespace

// ---------------------------------------------------------------------------
// #1744 — scoring: full credit is reserved for the band
// ---------------------------------------------------------------------------

TEST_CASE( "#1744 AtLeast window touching the band edge keeps ramp semantics",
           "[grader][r27t05][issue-1744]" )
{
    // Band: value >= 0.85 (tolerance 0). Window {0.60, 0.85} touches the
    // edge: ceiling 1, ramp [0.60→0, 0.85→full]. The canonical R18 shape.
    const GradingRubric rubric =
        metricRubric( MetricExpectation::Mode::AtLeast, 0.85, 0.0, 0.60, 0.85 );

    // Below the threshold, mid-window: the pinned half-credit value.
    {
        const GradeOutcome o = grade( rubric, metricEvidence( 0.725 ) );
        REQUIRE( o.ok );
        CHECK( soleCriterion( o ).status == OutcomeStatus::Partial );
        CHECK( soleCriterion( o ).rawEarned > 49.99 );
        CHECK( soleCriterion( o ).rawEarned < 50.01 );
    }
    // Equal to the threshold: full.
    {
        const GradeOutcome o = grade( rubric, metricEvidence( 0.85 ) );
        REQUIRE( o.ok );
        CHECK( soleCriterion( o ).status == OutcomeStatus::Earned );
        CHECK( soleCriterion( o ).rawEarned == 100.0 );
    }
    // Above the threshold: full.
    {
        const GradeOutcome o = grade( rubric, metricEvidence( 0.9 ) );
        REQUIRE( o.ok );
        CHECK( soleCriterion( o ).status == OutcomeStatus::Earned );
    }
    // At/below the window floor: nothing.
    {
        const GradeOutcome o = grade( rubric, metricEvidence( 0.60 ) );
        REQUIRE( o.ok );
        CHECK( soleCriterion( o ).status == OutcomeStatus::NotEarned );
        CHECK( soleCriterion( o ).reasonCodes[0] == "grader:metric-below-band" );
    }
}

TEST_CASE( "#1744 AtLeast short window caps band-failing values below full",
           "[grader][r27t05][issue-1744]" )
{
    // Band: value >= 0.85. Window {0.60, 0.70} stops short: ceiling
    // (0.70-0.60)/(0.85-0.60) = 0.4. The (0.70, 0.85) gap used to clamp to
    // FULL — the WE-11 bug; it must now plateau at 40%.
    const GradingRubric rubric =
        metricRubric( MetricExpectation::Mode::AtLeast, 0.85, 0.0, 0.60, 0.70 );

    for ( const double gapValue : { 0.70, 0.75, 0.8499 } ) {
        const GradeOutcome o = grade( rubric, metricEvidence( gapValue ) );
        REQUIRE( o.ok );
        INFO( "gap value=" << gapValue << " earned=" << soleCriterion( o ).rawEarned );
        CHECK( soleCriterion( o ).status == OutcomeStatus::Partial );
        CHECK( soleCriterion( o ).rawEarned > 39.99 );
        CHECK( soleCriterion( o ).rawEarned < 40.01 );
        CHECK( soleCriterion( o ).rawEarned < 100.0 );
    }
    // Inside the ramp, above the ceiling line: 0.68's ramp fraction
    // (0.68-0.60)/0.10 = 0.8 caps at 0.4.
    {
        const GradeOutcome o = grade( rubric, metricEvidence( 0.68 ) );
        REQUIRE( o.ok );
        CHECK( soleCriterion( o ).rawEarned > 39.99 );
        CHECK( soleCriterion( o ).rawEarned < 40.01 );
    }
    // The bar itself still awards full — the fix must not lower the pass
    // standard.
    {
        const GradeOutcome o = grade( rubric, metricEvidence( 0.85 ) );
        REQUIRE( o.ok );
        CHECK( soleCriterion( o ).status == OutcomeStatus::Earned );
        CHECK( soleCriterion( o ).rawEarned == 100.0 );
    }
}

TEST_CASE( "#1744 AtMost mirror: gap above the band never reaches full",
           "[grader][r27t05][issue-1744]" )
{
    // Band: value <= 1.0. Window {1.2, 1.6}: ceiling (1.6-1.2)/(1.6-1.0)
    // = 2/3. The (1.0, 1.2] grace gap must cap at 2/3, not clamp to full.
    const GradingRubric rubric =
        metricRubric( MetricExpectation::Mode::AtMost, 1.0, 0.0, 1.2, 1.6 );

    for ( const double gapValue : { 1.0001, 1.1, 1.2 } ) {
        const GradeOutcome o = grade( rubric, metricEvidence( gapValue ) );
        REQUIRE( o.ok );
        INFO( "gap value=" << gapValue << " earned=" << soleCriterion( o ).rawEarned );
        CHECK( soleCriterion( o ).status == OutcomeStatus::Partial );
        CHECK( soleCriterion( o ).rawEarned > 66.65 );
        CHECK( soleCriterion( o ).rawEarned < 66.7 );
        CHECK( soleCriterion( o ).rawEarned < 100.0 );
    }
    // Mid-window stays the declared ramp value (1.4 → 0.5, below ceiling).
    {
        const GradeOutcome o = grade( rubric, metricEvidence( 1.4 ) );
        REQUIRE( o.ok );
        CHECK( soleCriterion( o ).rawEarned > 49.99 );
        CHECK( soleCriterion( o ).rawEarned < 50.01 );
    }
    // Inside the band: full — pass standard unchanged.
    {
        const GradeOutcome o = grade( rubric, metricEvidence( 1.0 ) );
        REQUIRE( o.ok );
        CHECK( soleCriterion( o ).status == OutcomeStatus::Earned );
    }
}

TEST_CASE( "#1744 tolerance widens the band and re-anchors the ceiling",
           "[grader][r27t05][issue-1744]" )
{
    // Band: value >= 0.85 - 0.05 = 0.80. Window {0.60, 0.70}: ceiling
    // (0.70-0.60)/(0.80-0.60) = 0.5. Values in [0.80, ∞) earn full even
    // below the nominal threshold 0.85 — the declared tolerance.
    const GradingRubric rubric =
        metricRubric( MetricExpectation::Mode::AtLeast, 0.85, 0.05, 0.60, 0.70 );

    {
        const GradeOutcome o = grade( rubric, metricEvidence( 0.80 ) );
        REQUIRE( o.ok );
        CHECK( soleCriterion( o ).status == OutcomeStatus::Earned );
    }
    {
        const GradeOutcome o = grade( rubric, metricEvidence( 0.75 ) );
        REQUIRE( o.ok );
        CHECK( soleCriterion( o ).status == OutcomeStatus::Partial );
        CHECK( soleCriterion( o ).rawEarned > 49.99 );
        CHECK( soleCriterion( o ).rawEarned < 50.01 );
    }
}

TEST_CASE( "#1744 verdict cascade: a window-capped criterion cannot carry a Pass alone",
           "[grader][r27t05][issue-1744]" )
{
    // Passing needs 60; the window-capped gap value earns 40 → Partial.
    const GradingRubric rubric =
        metricRubric( MetricExpectation::Mode::AtLeast, 0.85, 0.0, 0.60, 0.70 );
    const GradeOutcome o = grade( rubric, metricEvidence( 0.75 ) );
    REQUIRE( o.ok );
    CHECK( o.report.verdict == ReportVerdict::Partial );
    CHECK( o.report.score < o.report.passingScore );
    // At the bar the same rubric passes — the cap did not move the bar.
    const GradeOutcome pass = grade( rubric, metricEvidence( 0.85 ) );
    REQUIRE( pass.ok );
    CHECK( pass.report.verdict == ReportVerdict::Pass );
}

// ---------------------------------------------------------------------------
// #1744 — loader: windows must sit on the failing side of the band
// ---------------------------------------------------------------------------

TEST_CASE( "#1744 loader rejects windows on the passing side or straddling the band",
           "[grader][r27t05][issue-1744]" )
{
    GraderError err;
    // AtLeast, band edge 0.85: window ending above the edge — refused.
    CHECK_FALSE( GradingRubric::fromJson(
                     windowRubricJson( "at_least", 0.85, 0.0, 0.60, 0.90 ), err )
                     .has_value() );
    // AtLeast window entirely above the threshold — refused.
    CHECK_FALSE( GradingRubric::fromJson(
                     windowRubricJson( "at_least", 0.85, 0.0, 0.90, 1.20 ), err )
                     .has_value() );
    // AtMost, band edge 1.0: window starting below the edge — refused.
    CHECK_FALSE( GradingRubric::fromJson(
                     windowRubricJson( "at_most", 1.0, 0.0, 0.80, 1.20 ), err )
                     .has_value() );
    // AtMost window entirely below the threshold — refused.
    CHECK_FALSE( GradingRubric::fromJson(
                     windowRubricJson( "at_most", 1.0, 0.0, 0.60, 0.90 ), err )
                     .has_value() );
    // Tolerance shifts the edge with the same rule (AtLeast edge 0.80).
    CHECK_FALSE( GradingRubric::fromJson(
                     windowRubricJson( "at_least", 0.85, 0.05, 0.60, 0.85 ), err )
                     .has_value() );

    // Valid shapes still load: touching the edge from the failing side.
    CHECK( GradingRubric::fromJson(
               windowRubricJson( "at_least", 0.85, 0.0, 0.60, 0.85 ), err )
               .has_value() );
    CHECK( GradingRubric::fromJson(
               windowRubricJson( "at_most", 1.0, 0.0, 1.0, 1.4 ), err )
               .has_value() );
    // Equals/Range ignore the window — loading stays permissive there.
    CHECK( GradingRubric::fromJson(
               windowRubricJson( "equals", 0.85, 0.0, 0.60, 0.70 ), err )
               .has_value() );
}

// ---------------------------------------------------------------------------
// #1773 — requiredStages gate the verdict
// ---------------------------------------------------------------------------

namespace {

GradingRubric requiredStageRubric()
{
    GradingRubric rubric;
    rubric.rubricId = "r27t05-required-stage";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "process";
    dim.weight = 100.0;
    Criterion stage;
    stage.criterionId = "train";
    stage.maxPoints = 40.0;
    stage.kind = CriterionKind::Stage;
    stage.evidenceKey = "train";
    stage.stage.expectedState = "Completed";
    dim.criteria.push_back( stage );
    Criterion c;
    c.criterionId = "m";
    c.maxPoints = 60.0;
    c.kind = CriterionKind::Metric;
    c.evidenceKey = "m";
    c.metric.mode = MetricExpectation::Mode::AtLeast;
    c.metric.value = 0.5;
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );
    StageRequirement req;
    req.stageKey = "train";
    req.expectedState = "Completed";
    rubric.requiredStages.push_back( req );
    return rubric;
}

GradeEvidence passingEvidenceWithoutStage()
{
    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    GradeEvidenceItem item;
    item.evidenceId = "ev-m1";
    item.kind = EvidenceKind::Metric;
    item.key = "m";
    item.hasValue = true;
    item.value = 0.9;
    evidence.items.push_back( item );
    return evidence;
}

} // namespace

TEST_CASE( "#1773 a missing required stage blocks Pass at pass-level score",
           "[grader][r27t05][issue-1773]" )
{
    const GradingRubric rubric = requiredStageRubric();

    const GradeOutcome missing = grade( rubric, passingEvidenceWithoutStage() );
    REQUIRE( missing.ok );
    // Metric evidence alone earns 60 >= 60 (pass level), but the declared
    // "train" stage never happened: the verdict must not read Pass.
    CHECK( missing.report.verdict == ReportVerdict::Partial );
    CHECK( missing.report.score == 60.0 );
    REQUIRE( missing.report.requiredStageOutcomes.size() == 1 );
    CHECK_FALSE( missing.report.requiredStageOutcomes[0].satisfied );

    // Same evidence plus the recorded stage: a clean Pass.
    GradeEvidence withStage = passingEvidenceWithoutStage();
    GradeEvidenceItem stage;
    stage.evidenceId = "ev-train";
    stage.kind = EvidenceKind::Stage;
    stage.key = "train";
    stage.state = "Completed";
    stage.source = "provenance:prov.json";
    withStage.items.push_back( stage );
    const GradeOutcome complete = grade( rubric, withStage );
    REQUIRE( complete.ok );
    CHECK( complete.report.verdict == ReportVerdict::Pass );
    CHECK( complete.report.score == 100.0 );
    CHECK( complete.report.requiredStageOutcomes[0].satisfied );
}

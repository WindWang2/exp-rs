// test_grader_engine.cpp — RS14-05 Slices B/C: the matching and scoring
// engine behind grade() (ADR 0174). Light lane: Catch2 + sicnu_grader only.
//
// RED-first record: against the slice-A stub every grade() case below fails
// with the typed "grading engine not implemented (slices B/C)" Internal
// error — the legal (rubric, evidence) exemplar MUST move from that RED to
// a deterministic GREEN report.
//
// Contracts pinned here (normative, append-only reason vocabulary):
//   * Stage criteria match stage-kind evidence by key (any-of accepted
//     alternatives), expected state, minDistinct, and declared
//     `orderedAfter` sequencing — the ONLY order the grader can see,
//     carried by evidence recordedAtUtc (lexicographic = chronological).
//   * Metric criteria score against the observed value: tolerance bands
//     (full-credit half width), optional teacher-declared linear partial
//     window, at_least/at_most/equals/range modes.
//   * Fact criteria subset-match requiredFacts (canonical JSON string
//     compare) over fact-capable kinds, narrowed by requireEvidenceKind.
//   * Answer criteria: deterministic keyword-boundary concept matching +
//     misconception deductions on normalized text. No LLM, no invented
//     curve.
//   * Hard constraints (forbidden/required evidence) cap or zero the
//     report; alternate pathways resolve deterministically (highest raw
//     earned, tie → pathwayId lexicographic).
//   * Missing/ambiguous evidence → indeterminate outcomes with reasons,
//     never silent zeros; contradicted evidence → not_earned.
//   * Double grading is byte-identical; evidence permutation (shuffled
//     item order) leaves every judgment member identical — while the
//     evidenceDigest deliberately binds the input BYTE order, so permuted
//     inputs carry different digests (tamper evidence) and both verify.
#include "grader/grader_engine.h"
#include "grader/grader_error.h"
#include "grader/grader_json.h"
#include "grader/grader_sha256.h"
#include "grader/grader_types.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace sicnu::grader;

namespace {

GradingRubric stageRubric()
{
    GradingRubric rubric;
    rubric.rubricId = "lab03-stage";
    rubric.revision = 1;
    rubric.title = "stage lab";
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "process";
    dim.title = "Process";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "proc-train";
    c.title = "Training stage completed";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Stage;
    c.evidenceKey = "train";
    c.stage.expectedState = "Completed";
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );
    return rubric;
}

GradeEvidence stageEvidence( const std::string &state = "Completed", const std::string &recordedAt = "" )
{
    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    GradeEvidenceItem item;
    item.evidenceId = "ev-1";
    item.kind = EvidenceKind::Stage;
    item.key = "train";
    item.state = state;
    item.recordedAtUtc = recordedAt;
    item.source = "provenance:prov.json";
    evidence.items.push_back( item );
    return evidence;
}

/// Appends a stage item with an explicit id (ordering/permutation cases).
void addStage( GradeEvidence &evidence, const std::string &id, const std::string &key,
               const std::string &state, const std::string &recordedAt = std::string() )
{
    GradeEvidenceItem item;
    item.evidenceId = id;
    item.kind = EvidenceKind::Stage;
    item.key = key;
    item.state = state;
    item.recordedAtUtc = recordedAt;
    evidence.items.push_back( item );
}

} // namespace

TEST_CASE( "legal exemplar grades to a deterministic earned report (RED stub → GREEN)",
           "[grader][engine][happy]" )
{
    const GradingRubric rubric = stageRubric();
    const GradeEvidence evidence = stageEvidence();
    const GradeOutcome outcome = grade( rubric, evidence );
    CAPTURE( outcome.error.message );
    REQUIRE( outcome.ok );
    REQUIRE( outcome.error.ok() );
    const GradeReport &report = outcome.report;

    CHECK( report.rubricId == "lab03-stage" );
    CHECK( report.rubricRevision == 1 );
    CHECK( report.subject.experimentId == "exp-1" );
    CHECK( report.totalPoints == 100.0 );
    CHECK( report.passingScore == 60.0 );
    CHECK( report.score == 100.0 );
    CHECK( report.verdict == ReportVerdict::Pass );

    REQUIRE( report.dimensions.size() == 1 );
    REQUIRE( report.dimensions[0].criteria.size() == 1 );
    const CriterionOutcome &outcome1 = report.dimensions[0].criteria[0];
    CHECK( outcome1.criterionId == "proc-train" );
    CHECK( outcome1.maxPoints == 100.0 );
    CHECK( outcome1.rawEarned == 100.0 );
    CHECK( outcome1.earned == 100.0 );
    CHECK( outcome1.status == OutcomeStatus::Earned );
    // Reason chain: FULL-credit outcomes may carry no reason; the judgment
    // must still cite the evidence it was judged against (ADR 0174 §4).
    REQUIRE( outcome1.evidenceIds.size() == 1 );
    CHECK( outcome1.evidenceIds[0] == "ev-1" );

    // Digest binding: sha256 over the canonical body, verify passes, and the
    // rubric/evidence digests are present.
    CHECK( report.rubricDigest.size() == 64 );
    CHECK( report.evidenceDigest.size() == 64 );
    GraderError err;
    REQUIRE( report.verifyDigest( err ) );

    // Double grading is byte-identical (determinism doctrine).
    const GradeOutcome again = grade( rubric, evidence );
    REQUIRE( again.ok );
    GraderError ignored;
    CHECK( canonicalizeJson( again.report.toJson(), ignored ).value() ==
           canonicalizeJson( report.toJson(), ignored ).value() );
}

TEST_CASE( "stage criteria: alternatives, minDistinct and orderedAfter sequencing",
           "[grader][engine][stage]" )
{
    GradingRubric rubric = stageRubric();
    rubric.dimensions[0].criteria[0].stage.acceptedAlternatives = { "training" };
    rubric.dimensions[0].criteria[0].stage.minDistinct = 1;

    SECTION( "accepted alternative key satisfies" )
    {
        GradeEvidence evidence;
        addStage( evidence, "ev-alt", "training", "Completed" );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.dimensions[0].criteria[0].status == OutcomeStatus::Earned );
    }

    SECTION( "wrong state is not earned with a cited reason" )
    {
        const GradeOutcome outcome = grade( rubric, stageEvidence( "Failed" ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &c = outcome.report.dimensions[0].criteria[0];
        CHECK( c.status == OutcomeStatus::NotEarned );
        CHECK( c.rawEarned == 0.0 );
        REQUIRE_FALSE( c.reasonCodes.empty() );
        CHECK( c.reasonCodes[0].rfind( "grader:", 0 ) == 0 );
        REQUIRE_FALSE( c.evidenceIds.empty() );
        CHECK( outcome.report.verdict == ReportVerdict::Fail );
    }

    SECTION( "missing stage evidence is indeterminate, never a silent zero" )
    {
        GradeEvidence evidence;
        evidence.subject.experimentId = "exp-1";
        GradeEvidenceItem other;
        other.evidenceId = "ev-other";
        other.kind = EvidenceKind::Stage;
        other.key = "unrelated";
        other.state = "Completed";
        evidence.items.push_back( other );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &c = outcome.report.dimensions[0].criteria[0];
        CHECK( c.status == OutcomeStatus::Indeterminate );
        REQUIRE_FALSE( c.reasonCodes.empty() );
    }

    SECTION( "minDistinct requires distinct evidence ids" )
    {
        rubric.dimensions[0].criteria[0].stage.minDistinct = 2;
        GradeEvidence evidence;
        addStage( evidence, "ev-a", "train", "Completed" );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &c = outcome.report.dimensions[0].criteria[0];
        CHECK( c.status == OutcomeStatus::NotEarned );
        CHECK( c.reasonCodes[0] == "grader:stage-insufficient-distinct" );
    }

    SECTION( "orderedAfter satisfied in order" )
    {
        // train orderedAfter preprocess: preprocess evidence recorded first.
        Criterion pre;
        pre.criterionId = "proc-pre";
        pre.maxPoints = 0.0001; // tiny; weight rebalanced below
        rubric.dimensions[0].criteria[0].maxPoints = 99.9999;
        pre.kind = CriterionKind::Stage;
        pre.evidenceKey = "preprocess";
        pre.stage.expectedState = "Completed";
        rubric.dimensions[0].criteria.push_back( pre );
        rubric.dimensions[0].criteria[0].stage.orderedAfter = { "preprocess" };

        GradeEvidence evidence;
        addStage( evidence, "ev-pre", "preprocess", "Completed", "2026-09-23T08:00:00Z" );
        addStage( evidence, "ev-train", "train", "Completed", "2026-09-23T09:00:00Z" );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.dimensions[0].criteria[0].status == OutcomeStatus::Earned );
    }

    SECTION( "orderedAfter violated by recorded order is not earned" )
    {
        Criterion pre;
        pre.criterionId = "proc-pre";
        pre.maxPoints = 0.0001;
        rubric.dimensions[0].criteria[0].maxPoints = 99.9999;
        pre.kind = CriterionKind::Stage;
        pre.evidenceKey = "preprocess";
        pre.stage.expectedState = "Completed";
        rubric.dimensions[0].criteria.push_back( pre );
        rubric.dimensions[0].criteria[0].stage.orderedAfter = { "preprocess" };

        GradeEvidence evidence;
        // train recorded BEFORE preprocess → sequencing violated.
        addStage( evidence, "ev-train", "train", "Completed", "2026-09-23T08:00:00Z" );
        addStage( evidence, "ev-pre", "preprocess", "Completed", "2026-09-23T09:00:00Z" );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &c = outcome.report.dimensions[0].criteria[0];
        CHECK( c.status == OutcomeStatus::NotEarned );
        CHECK( c.reasonCodes[0] == "grader:stage-order-violated" );
        REQUIRE_FALSE( c.evidenceIds.empty() );
    }

    SECTION( "never-satisfied prerequisite is not earned with the prerequisite reason" )
    {
        Criterion pre;
        pre.criterionId = "proc-pre";
        pre.maxPoints = 0.0001;
        rubric.dimensions[0].criteria[0].maxPoints = 99.9999;
        pre.kind = CriterionKind::Stage;
        pre.evidenceKey = "preprocess";
        pre.stage.expectedState = "Completed";
        rubric.dimensions[0].criteria.push_back( pre );
        rubric.dimensions[0].criteria[0].stage.orderedAfter = { "preprocess" };

        GradeEvidence evidence;
        addStage( evidence, "ev-train", "train", "Completed", "2026-09-23T09:00:00Z" );
        addStage( evidence, "ev-pre", "preprocess", "Failed", "2026-09-23T08:00:00Z" );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &c = outcome.report.dimensions[0].criteria[0];
        CHECK( c.status == OutcomeStatus::NotEarned );
        CHECK( c.reasonCodes[0] == "grader:prerequisite-stage-missing" );
    }

    SECTION( "orderedAfter with timestamp-less evidence cannot prove order → indeterminate" )
    {
        Criterion pre;
        pre.criterionId = "proc-pre";
        pre.maxPoints = 0.0001;
        rubric.dimensions[0].criteria[0].maxPoints = 99.9999;
        pre.kind = CriterionKind::Stage;
        pre.evidenceKey = "preprocess";
        pre.stage.expectedState = "Completed";
        rubric.dimensions[0].criteria.push_back( pre );
        rubric.dimensions[0].criteria[0].stage.orderedAfter = { "preprocess" };

        GradeEvidence evidence;
        addStage( evidence, "ev-train", "train", "Completed" ); // no recordedAtUtc
        addStage( evidence, "ev-pre", "preprocess", "Completed" );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &c = outcome.report.dimensions[0].criteria[0];
        CHECK( c.status == OutcomeStatus::Indeterminate );
        CHECK( c.reasonCodes[0] == "grader:stage-order-unprovable" );
    }
}

TEST_CASE( "metric criteria: bands, tolerance, linear window, range and ambiguity",
           "[grader][engine][metric]" )
{
    GradingRubric rubric;
    rubric.rubricId = "lab03-metric";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "result";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "res-acc";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Metric;
    c.evidenceKey = "overall_accuracy";
    c.metric.mode = MetricExpectation::Mode::AtLeast;
    c.metric.value = 0.85;
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );

    auto metricEvidence = []( double value, const std::string &id = "ev-m1" ) {
        GradeEvidence evidence;
        evidence.subject.experimentId = "exp-1";
        GradeEvidenceItem item;
        item.evidenceId = id;
        item.kind = EvidenceKind::Metric;
        item.key = "overall_accuracy";
        item.hasValue = true;
        item.value = value;
        evidence.items.push_back( item );
        return evidence;
    };

    SECTION( "at threshold earns full" )
    {
        const GradeOutcome outcome = grade( rubric, metricEvidence( 0.85 ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Earned );
        CHECK( o.rawEarned == 100.0 );
        CHECK( outcome.report.verdict == ReportVerdict::Pass );
    }

    SECTION( "below band without window is not earned" )
    {
        const GradeOutcome outcome = grade( rubric, metricEvidence( 0.70 ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::NotEarned );
        CHECK( o.rawEarned == 0.0 );
        CHECK( o.reasonCodes[0] == "grader:metric-below-band" );
        CHECK( outcome.report.verdict == ReportVerdict::Fail );
    }

    SECTION( "tolerance band earns full below the threshold" )
    {
        rubric.dimensions[0].criteria[0].metric.tolerance = 0.02;
        const GradeOutcome outcome = grade( rubric, metricEvidence( 0.83 ) );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.dimensions[0].criteria[0].status == OutcomeStatus::Earned );
    }

    SECTION( "linear window yields teacher-bounded partial credit" )
    {
        rubric.dimensions[0].criteria[0].metric.hasLinearWindow = true;
        rubric.dimensions[0].criteria[0].metric.windowFrom = 0.60;
        rubric.dimensions[0].criteria[0].metric.windowTo = 0.85;
        const GradeOutcome outcome = grade( rubric, metricEvidence( 0.725 ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Partial );
        CHECK( ( o.rawEarned > 49.99 && o.rawEarned < 50.01 ) ); // (0.725-0.60)/(0.85-0.60) = 1/2
        CHECK( o.reasonCodes[0] == "grader:metric-partial-window" );
        CHECK( outcome.report.verdict == ReportVerdict::Partial ); // 50 < 60 but credit exists
    }

    SECTION( "window floor: at/below windowFrom earns nothing" )
    {
        rubric.dimensions[0].criteria[0].metric.hasLinearWindow = true;
        rubric.dimensions[0].criteria[0].metric.windowFrom = 0.60;
        rubric.dimensions[0].criteria[0].metric.windowTo = 0.85;
        const GradeOutcome outcome = grade( rubric, metricEvidence( 0.60 ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::NotEarned );
        CHECK( o.rawEarned == 0.0 );
    }

    SECTION( "at_most mode mirrors the band" )
    {
        rubric.dimensions[0].criteria[0].metric.mode = MetricExpectation::Mode::AtMost;
        rubric.dimensions[0].criteria[0].metric.value = 0.10;
        const GradeOutcome above = grade( rubric, metricEvidence( 0.50 ) );
        REQUIRE( above.ok );
        CHECK( above.report.dimensions[0].criteria[0].status == OutcomeStatus::NotEarned );
        CHECK( above.report.dimensions[0].criteria[0].reasonCodes[0] == "grader:metric-above-band" );
        const GradeOutcome at = grade( rubric, metricEvidence( 0.10 ) );
        REQUIRE( at.ok );
        CHECK( at.report.dimensions[0].criteria[0].status == OutcomeStatus::Earned );
    }

    SECTION( "equals honors tolerance" )
    {
        rubric.dimensions[0].criteria[0].metric.mode = MetricExpectation::Mode::Equals;
        rubric.dimensions[0].criteria[0].metric.value = 1.0;
        rubric.dimensions[0].criteria[0].metric.tolerance = 0.05;
        const GradeOutcome near = grade( rubric, metricEvidence( 0.96 ) );
        REQUIRE( near.ok );
        CHECK( near.report.dimensions[0].criteria[0].status == OutcomeStatus::Earned );
        const GradeOutcome far = grade( rubric, metricEvidence( 0.90 ) );
        REQUIRE( far.ok );
        const CriterionOutcome &o = far.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::NotEarned );
        CHECK( o.reasonCodes[0] == "grader:metric-value-mismatch" );
    }

    SECTION( "range mode needs the value inside [value, valueMax]" )
    {
        rubric.dimensions[0].criteria[0].metric.mode = MetricExpectation::Mode::Range;
        rubric.dimensions[0].criteria[0].metric.value = 0.8;
        rubric.dimensions[0].criteria[0].metric.valueMax = 0.95;
        const GradeOutcome inside = grade( rubric, metricEvidence( 0.87 ) );
        REQUIRE( inside.ok );
        CHECK( inside.report.dimensions[0].criteria[0].status == OutcomeStatus::Earned );
        const GradeOutcome below = grade( rubric, metricEvidence( 0.70 ) );
        REQUIRE( below.ok );
        CHECK( below.report.dimensions[0].criteria[0].reasonCodes[0] == "grader:metric-out-of-range" );
    }

    SECTION( "missing metric key is indeterminate" )
    {
        GradeEvidence evidence;
        evidence.subject.experimentId = "exp-1";
        GradeEvidenceItem item;
        item.evidenceId = "ev-x";
        item.kind = EvidenceKind::Metric;
        item.key = "kappa";
        item.hasValue = true;
        item.value = 0.9;
        evidence.items.push_back( item );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Indeterminate );
        CHECK( o.reasonCodes[0] == "grader:metric-missing" );
    }

    SECTION( "conflicting observations for one key are ambiguous → indeterminate" )
    {
        GradeEvidence evidence;
        evidence.subject.experimentId = "exp-1";
        GradeEvidenceItem a;
        a.evidenceId = "ev-a";
        a.kind = EvidenceKind::Metric;
        a.key = "overall_accuracy";
        a.hasValue = true;
        a.value = 0.9;
        GradeEvidenceItem b;
        b.evidenceId = "ev-b";
        b.kind = EvidenceKind::Metric;
        b.key = "overall_accuracy";
        b.hasValue = true;
        b.value = 0.7;
        evidence.items.push_back( b );
        evidence.items.push_back( a );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Indeterminate );
        CHECK( o.reasonCodes[0] == "grader:metric-ambiguous" );
        // Both conflicting records are cited.
        REQUIRE( o.evidenceIds.size() == 2 );
    }

    SECTION( "duplicate agreeing observations are not ambiguous (stable tie-break)" )
    {
        GradeEvidence evidence;
        evidence.subject.experimentId = "exp-1";
        for ( const char *id : { "ev-b", "ev-a" } ) {
            GradeEvidenceItem item;
            item.evidenceId = id;
            item.kind = EvidenceKind::Metric;
            item.key = "overall_accuracy";
            item.hasValue = true;
            item.value = 0.9;
            evidence.items.push_back( item );
        }
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Earned );
        // Evidence citations are in the stable (evidenceId-sorted) order.
        REQUIRE( o.evidenceIds.size() == 2 );
        CHECK( o.evidenceIds[0] == "ev-a" );
        CHECK( o.evidenceIds[1] == "ev-b" );
    }
}

TEST_CASE( "fact criteria: kind narrowing, requiredFacts subset, minCount",
           "[grader][engine][fact]" )
{
    GradingRubric rubric;
    rubric.rubricId = "lab03-fact";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "facts";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "fact-manifest";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Fact;
    c.evidenceKey = "dataset_manifest";
    c.fact.expectedState = "verified";
    c.fact.requiredFacts["algorithm"] = "\"unet\"";
    c.fact.minCount = 1;
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );

    auto factEvidence = []( EvidenceKind kind, const std::string &state, Json::Value facts,
                            const std::string &id = "ev-f1" ) {
        GradeEvidence evidence;
        evidence.subject.experimentId = "exp-1";
        GradeEvidenceItem item;
        item.evidenceId = id;
        item.kind = kind;
        item.key = "dataset_manifest";
        item.state = state;
        item.facts = std::move( facts );
        evidence.items.push_back( item );
        return evidence;
    };

    SECTION( "matching state + requiredFacts earns" )
    {
        Json::Value facts{ Json::objectValue };
        facts["algorithm"] = "unet";
        facts["extra"] = true; // subset match: extra members are fine
        const GradeOutcome outcome = grade( rubric, factEvidence( EvidenceKind::Provenance, "verified", facts ) );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.dimensions[0].criteria[0].status == OutcomeStatus::Earned );
    }

    SECTION( "state mismatch is not earned (evidence exists and contradicts)" )
    {
        Json::Value facts{ Json::objectValue };
        facts["algorithm"] = "unet";
        const GradeOutcome outcome = grade( rubric, factEvidence( EvidenceKind::Provenance, "pending", facts ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::NotEarned );
        CHECK( o.reasonCodes[0] == "grader:fact-state-mismatch" );
    }

    SECTION( "missing required fact is not earned" )
    {
        Json::Value facts{ Json::objectValue };
        facts["algorithm"] = "svm";
        const GradeOutcome outcome = grade( rubric, factEvidence( EvidenceKind::Provenance, "verified", facts ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::NotEarned );
        CHECK( o.reasonCodes[0] == "grader:fact-missing-required-fact" );
    }

    SECTION( "no evidence for the key at all is indeterminate" )
    {
        GradeEvidence evidence;
        evidence.subject.experimentId = "exp-1";
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Indeterminate );
        CHECK( o.reasonCodes[0] == "grader:fact-missing" );
    }

    SECTION( "requireEvidenceKind excludes other fact-capable kinds" )
    {
        rubric.dimensions[0].criteria[0].requireEvidenceKind = EvidenceKind::VerifierVerdict;
        Json::Value facts{ Json::objectValue };
        facts["algorithm"] = "unet";
        // A provenance item carries the right state+facts but is the wrong kind.
        const GradeOutcome outcome = grade( rubric, factEvidence( EvidenceKind::Provenance, "verified", facts ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Indeterminate ); // nothing of the required kind exists
        CHECK( o.reasonCodes[0] == "grader:fact-missing" );

        GradeEvidence both = factEvidence( EvidenceKind::Provenance, "verified", facts );
        GradeEvidenceItem verdict;
        verdict.evidenceId = "ev-v1";
        verdict.kind = EvidenceKind::VerifierVerdict;
        verdict.key = "dataset_manifest";
        verdict.state = "verified";
        verdict.facts = Json::Value{ Json::objectValue };
        verdict.facts["algorithm"] = "unet";
        both.items.push_back( verdict );
        const GradeOutcome ok = grade( rubric, both );
        REQUIRE( ok.ok );
        CHECK( ok.report.dimensions[0].criteria[0].status == OutcomeStatus::Earned );
    }

    SECTION( "minCount across distinct items" )
    {
        rubric.dimensions[0].criteria[0].fact.minCount = 2;
        Json::Value facts{ Json::objectValue };
        facts["algorithm"] = "unet";
        GradeEvidence evidence = factEvidence( EvidenceKind::Provenance, "verified", facts, "ev-1" );
        const GradeOutcome one = grade( rubric, evidence );
        REQUIRE( one.ok );
        CHECK( one.report.dimensions[0].criteria[0].reasonCodes[0] == "grader:fact-insufficient-count" );

        GradeEvidenceItem second;
        second.evidenceId = "ev-2";
        second.kind = EvidenceKind::Checkpoint;
        second.key = "dataset_manifest";
        second.state = "verified";
        second.facts = facts;
        evidence.items.push_back( second );
        const GradeOutcome two = grade( rubric, evidence );
        REQUIRE( two.ok );
        CHECK( two.report.dimensions[0].criteria[0].status == OutcomeStatus::Earned );
    }

    SECTION( "stage/metric/answer-kind items do not satisfy fact criteria" )
    {
        // A stage-kind item with the same key+state must NOT satisfy a fact
        // criterion (fact joins onto the fact-capable taxonomy only).
        Json::Value facts{ Json::objectValue };
        facts["algorithm"] = "unet";
        GradeEvidence evidence = factEvidence( EvidenceKind::Stage, "verified", facts );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.dimensions[0].criteria[0].status == OutcomeStatus::Indeterminate );
    }
}

TEST_CASE( "answer criteria: deterministic keyword boundaries and misconception deductions",
           "[grader][engine][answer]" )
{
    GradingRubric rubric;
    rubric.rubricId = "lab03-answer";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "interpretation";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "interp-q1";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Answer;
    c.answer.questionId = "q1";
    AnswerConcept concept1;
    concept1.conceptId = "c-ndvi";
    concept1.points = 60.0;
    concept1.keywordGroups = { { "normalized difference", "ndvi" } }; // ANY group: all keywords
    AnswerConcept concept2;
    concept2.conceptId = "c-health";
    concept2.points = 40.0;
    concept2.keywordGroups = { { "vegetation", "health" } };
    c.answer.concepts = { concept1, concept2 };
    AnswerMisconception mis;
    mis.misconceptionId = "m-cloud";
    mis.patterns = { "clouds do not matter", "ignore the clouds" };
    mis.deductPoints = 30.0;
    mis.explanation = "Cloud contamination matters.";
    c.answer.misconceptions = { mis };
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );

    auto answerEvidence = []( const std::string &text ) {
        GradeEvidence evidence;
        evidence.subject.experimentId = "exp-1";
        GradeEvidenceItem item;
        item.evidenceId = "ev-a1";
        item.kind = EvidenceKind::Answer;
        item.key = "q1";
        item.facts["answerText"] = text;
        evidence.items.push_back( item );
        return evidence;
    };

    SECTION( "full concepts hit without misconception" )
    {
        const GradeOutcome outcome =
            grade( rubric, answerEvidence( "The NDVI (normalized difference vegetation index) tracks vegetation health." ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Earned );
        CHECK( o.rawEarned == 100.0 );
    }

    SECTION( "keyword boundary: substring inside a word does NOT hit" )
    {
        // "ndvier" contains "ndvi" as a substring but not at a word boundary.
        const GradeOutcome outcome =
            grade( rubric, answerEvidence( "The ndvier field tracks vegetation health." ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Partial );
        CHECK( o.rawEarned == 40.0 );
        CHECK( o.reasonCodes[0] == "grader:concept-missing" );
    }

    SECTION( "partial credit from a subset of concepts" )
    {
        const GradeOutcome outcome = grade( rubric, answerEvidence( "Vegetation health looks fine." ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Partial );
        CHECK( o.rawEarned == 40.0 );
    }

    SECTION( "misconception deduction clamps at zero and cites a reason" )
    {
        const GradeOutcome outcome =
            grade( rubric, answerEvidence( "Vegetation health is fine; ignore the clouds." ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Partial );
        CHECK( o.rawEarned == 10.0 ); // 40 - 30
        CHECK( o.reasonCodes[0] == "grader:misconception-deduction" );
    }

    SECTION( "deduction exceeds earned → clamped to zero, not negative" )
    {
        const GradeOutcome outcome = grade( rubric,
            answerEvidence( "Ignore the clouds; clouds do not matter at all." ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::NotEarned );
        CHECK( o.rawEarned == 0.0 );
    }

    SECTION( "missing answer evidence is indeterminate" )
    {
        GradeEvidence evidence;
        evidence.subject.experimentId = "exp-1";
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Indeterminate );
        CHECK( o.reasonCodes[0] == "grader:answer-missing" );
    }

    SECTION( "declared concept points short of the maximum still carry a reason (reason-chain invariant)" )
    {
        // Teacher-legal under-sum: one 60-point concept on a 100-point criterion.
        rubric.dimensions[0].criteria[0].answer.concepts.resize( 1 );
        const GradeOutcome outcome =
            grade( rubric, answerEvidence( "The NDVI (normalized difference) tracks vegetation health." ) );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[0].criteria[0];
        CHECK( o.status == OutcomeStatus::Partial );
        CHECK( o.rawEarned == 60.0 );
        CHECK( o.reasonCodes[0] == "grader:answer-points-short" );
    }

    SECTION( "misconception-only criterion with no hit is not a silent zero" )
    {
        Criterion misconceptionOnly;
        misconceptionOnly.criterionId = "interp-q2";
        misconceptionOnly.maxPoints = 40.0;
        misconceptionOnly.kind = CriterionKind::Answer;
        misconceptionOnly.answer.questionId = "q2";
        AnswerMisconception mis;
        mis.misconceptionId = "m1";
        mis.patterns = { "flat earth" };
        mis.deductPoints = 10.0;
        misconceptionOnly.answer.misconceptions = { mis };
        Dimension dim2;
        dim2.dimensionId = "interp2";
        dim2.title = "interp2";
        dim2.weight = 40.0;
        dim2.criteria.push_back( misconceptionOnly );
        rubric.dimensions.push_back( dim2 );
        rubric.dimensions[0].weight = 60.0;
        rubric.dimensions[0].criteria[0].maxPoints = 60.0;
        rubric.dimensions[0].criteria[0].answer.concepts[0].points = 30.0;
        rubric.dimensions[0].criteria[0].answer.concepts[1].points = 30.0;

        GradeEvidence evidence;
        evidence.subject.experimentId = "exp-1";
        GradeEvidenceItem answer;
        answer.evidenceId = "ev-a2";
        answer.kind = EvidenceKind::Answer;
        answer.key = "q2";
        answer.facts["answerText"] = "The satellite orbits the sphere.";
        evidence.items.push_back( answer );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const CriterionOutcome &o = outcome.report.dimensions[1].criteria[0];
        CHECK( o.status == OutcomeStatus::NotEarned );
        CHECK( o.rawEarned == 0.0 );
        REQUIRE_FALSE( o.reasonCodes.empty() );
        CHECK( o.reasonCodes[0] == "grader:answer-points-short" );
    }

    SECTION( "case and whitespace normalization is deterministic" )
    {
        const GradeOutcome outcome = grade( rubric,
            answerEvidence( "  the NDVI (the normalized difference) tracks VEGETATION    health  " ) );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.dimensions[0].criteria[0].rawEarned == 100.0 );
    }
}

TEST_CASE( "hard constraints: forbidden evidence zeroes, required evidence gates, cap binds",
           "[grader][engine][constraints]" )
{
    GradingRubric rubric = stageRubric();
    rubric.passingScore = 60.0;

    SECTION( "forbidden evidence zeroes the report and blocks" )
    {
        HardConstraint hc;
        hc.constraintId = "hc-no-leak";
        hc.title = "No data leakage";
        hc.mode = HardConstraint::Mode::ForbiddenEvidence;
        hc.evidenceKey = "leaky_step";
        hc.effect = HardConstraint::Effect::Zero;
        hc.explanation = "Leakage was recorded.";
        rubric.hardConstraints.push_back( hc );

        GradeEvidence evidence = stageEvidence();
        GradeEvidenceItem leak;
        leak.evidenceId = "ev-leak";
        leak.kind = EvidenceKind::Provenance;
        leak.key = "leaky_step";
        leak.state = "recorded";
        evidence.items.push_back( leak );

        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const GradeReport &report = outcome.report;
        CHECK( report.score == 0.0 );
        CHECK( report.verdict == ReportVerdict::Blocked );
        REQUIRE( report.hardConstraintOutcomes.size() == 1 );
        CHECK( report.hardConstraintOutcomes[0].violated );
        CHECK( report.hardConstraintOutcomes[0].evidenceIds == std::vector<std::string>{ "ev-leak" } );
        // The earned criterion is marked capped with a cited constraint reason.
        const CriterionOutcome &c = report.dimensions[0].criteria[0];
        CHECK( c.rawEarned == 100.0 );
        CHECK( c.earned == 0.0 );
        CHECK( c.status == OutcomeStatus::Capped );
        CHECK( c.reasonCodes[0] == "grader:constraint-zero" );
    }

    SECTION( "forbidden evidence absent → constraint holds, report pays out" )
    {
        HardConstraint hc;
        hc.constraintId = "hc-no-leak";
        hc.mode = HardConstraint::Mode::ForbiddenEvidence;
        hc.evidenceKey = "leaky_step";
        hc.effect = HardConstraint::Effect::Zero;
        rubric.hardConstraints.push_back( hc );
        const GradeOutcome outcome = grade( rubric, stageEvidence() );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.score == 100.0 );
        CHECK( outcome.report.verdict == ReportVerdict::Pass );
        CHECK_FALSE( outcome.report.hardConstraintOutcomes[0].violated );
    }

    SECTION( "required evidence gate: fact-matched presence satisfies" )
    {
        HardConstraint hc;
        hc.constraintId = "hc-manifest";
        hc.mode = HardConstraint::Mode::RequiredEvidence;
        hc.evidenceKey = "dataset_manifest";
        hc.effect = HardConstraint::Effect::Cap;
        hc.capPoints = 40.0;
        hc.fact.expectedState = "verified";
        rubric.hardConstraints.push_back( hc );

        GradeEvidence evidence = stageEvidence();
        GradeEvidenceItem manifest;
        manifest.evidenceId = "ev-manifest";
        manifest.kind = EvidenceKind::ArtifactState;
        manifest.key = "dataset_manifest";
        manifest.state = "verified";
        evidence.items.push_back( manifest );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.score == 100.0 );
        CHECK_FALSE( outcome.report.hardConstraintOutcomes[0].violated );
    }

    SECTION( "cap scales earned points and marks criteria capped" )
    {
        HardConstraint hc;
        hc.constraintId = "hc-late";
        hc.mode = HardConstraint::Mode::ForbiddenEvidence;
        hc.evidenceKey = "late_submission";
        hc.effect = HardConstraint::Effect::Cap;
        hc.capPoints = 40.0;
        rubric.hardConstraints.push_back( hc );

        GradeEvidence evidence = stageEvidence();
        GradeEvidenceItem late;
        late.evidenceId = "ev-late";
        late.kind = EvidenceKind::Custom;
        late.key = "late_submission";
        late.state = "recorded";
        evidence.items.push_back( late );

        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        const GradeReport &report = outcome.report;
        CHECK( report.score == 40.0 );
        CHECK( report.verdict == ReportVerdict::Partial ); // 40 < 60 but credit exists
        const CriterionOutcome &c = report.dimensions[0].criteria[0];
        CHECK( c.rawEarned == 100.0 );
        CHECK( c.earned == 40.0 );
        CHECK( c.status == OutcomeStatus::Capped );
        CHECK( c.reasonCodes[0] == "grader:constraint-cap" );
        CHECK( report.hardConstraintOutcomes[0].effect == "cap" );
    }

    SECTION( "zero dominates cap when both violated" )
    {
        HardConstraint zero;
        zero.constraintId = "hc-zero";
        zero.mode = HardConstraint::Mode::ForbiddenEvidence;
        zero.evidenceKey = "leaky";
        zero.effect = HardConstraint::Effect::Zero;
        HardConstraint cap;
        cap.constraintId = "hc-cap";
        cap.mode = HardConstraint::Mode::ForbiddenEvidence;
        cap.evidenceKey = "late";
        cap.effect = HardConstraint::Effect::Cap;
        cap.capPoints = 50.0;
        rubric.hardConstraints = { zero, cap };

        GradeEvidence evidence = stageEvidence();
        GradeEvidenceItem leak;
        leak.evidenceId = "ev-leak";
        leak.kind = EvidenceKind::Custom;
        leak.key = "leaky";
        evidence.items.push_back( leak );
        GradeEvidenceItem late;
        late.evidenceId = "ev-late";
        late.kind = EvidenceKind::Custom;
        late.key = "late";
        evidence.items.push_back( late );

        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.score == 0.0 );
        CHECK( outcome.report.verdict == ReportVerdict::Blocked );
    }
}

TEST_CASE( "required-evidence constraints accept any taxonomy kind on the key (documented semantics)",
           "[grader][engine][constraints]" )
{
    // A required-evidence constraint means "a record with this key exists and
    // satisfies the gate" — ANY taxonomy kind can carry it (a checkpoint
    // stage item is as good as a fact item). Pin this deliberately, so a
    // future kind filter is a reviewed contract change, not an accident.
    GradingRubric rubric;
    rubric.rubricId = "req-kind";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension dim;
    dim.dimensionId = "d";
    dim.weight = 100.0;
    Criterion c;
    c.criterionId = "c1";
    c.maxPoints = 100.0;
    c.kind = CriterionKind::Stage;
    c.evidenceKey = "train";
    c.stage.expectedState = "Completed";
    dim.criteria.push_back( c );
    rubric.dimensions.push_back( dim );
    HardConstraint hc;
    hc.constraintId = "hc-manifest";
    hc.mode = HardConstraint::Mode::RequiredEvidence;
    hc.evidenceKey = "dataset_manifest";
    hc.effect = HardConstraint::Effect::Cap;
    hc.capPoints = 50.0;
    hc.fact.expectedState = "verified";
    rubric.hardConstraints.push_back( hc );

    GradeEvidence evidence;
    GradeEvidenceItem stage;
    stage.evidenceId = "ev-1";
    stage.kind = EvidenceKind::Stage;
    stage.key = "train";
    stage.state = "Completed";
    evidence.items.push_back( stage );
    GradeEvidenceItem manifestStage;
    manifestStage.evidenceId = "ev-2";
    manifestStage.kind = EvidenceKind::Stage; // NOT a fact-capable kind
    manifestStage.key = "dataset_manifest";
    manifestStage.state = "verified";
    evidence.items.push_back( manifestStage );

    const GradeOutcome outcome = grade( rubric, evidence );
    REQUIRE( outcome.ok );
    CHECK_FALSE( outcome.report.hardConstraintOutcomes[0].violated );
    CHECK( outcome.report.score == 100.0 );

    // The same record does NOT satisfy a FACT criterion of identical gate —
    // fact criteria join the fact-capable taxonomy only.
    Criterion fact;
    fact.criterionId = "fact-manifest";
    fact.maxPoints = 100.0;
    fact.kind = CriterionKind::Fact;
    fact.evidenceKey = "dataset_manifest";
    fact.fact.expectedState = "verified";
    Dimension factDim;
    factDim.dimensionId = "d2";
    factDim.title = "d2";
    factDim.weight = 100.0;
    factDim.criteria.push_back( fact );
    rubric.dimensions.clear();
    rubric.dimensions.push_back( factDim );
    rubric.hardConstraints.clear();
    const GradeOutcome factOutcome = grade( rubric, evidence );
    REQUIRE( factOutcome.ok );
    CHECK( factOutcome.report.dimensions[0].criteria[0].status == OutcomeStatus::Indeterminate );
}

TEST_CASE( "required stages and alternate pathways resolve deterministically",
           "[grader][engine][structure]" )
{
    GradingRubric rubric = stageRubric();
    // Second criterion: preprocess stage (same dimension, rebalanced weights).
    Criterion pre;
    pre.criterionId = "proc-pre";
    pre.maxPoints = 50.0;
    rubric.dimensions[0].criteria[0].maxPoints = 50.0;
    pre.kind = CriterionKind::Stage;
    pre.evidenceKey = "preprocess";
    pre.stage.expectedState = "Completed";
    rubric.dimensions[0].criteria.push_back( pre );

    SECTION( "requiredStages report satisfaction with cited evidence" )
    {
        StageRequirement req;
        req.stageKey = "preprocess";
        req.expectedState = "Completed";
        rubric.requiredStages.push_back( req );

        GradeEvidence missing;
        addStage( missing, "ev-train", "train", "Completed" );
        GradeOutcome outcome = grade( rubric, missing );
        REQUIRE( outcome.ok );
        REQUIRE( outcome.report.requiredStageOutcomes.size() == 1 );
        CHECK_FALSE( outcome.report.requiredStageOutcomes[0].satisfied );
        CHECK( outcome.report.requiredStageOutcomes[0].explanation.size() > 0 );

        GradeEvidence present;
        addStage( present, "ev-train", "train", "Completed" );
        addStage( present, "ev-pre", "preprocess", "Completed" );
        outcome = grade( rubric, present );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.requiredStageOutcomes[0].satisfied );
        CHECK( outcome.report.requiredStageOutcomes[0].evidenceIds ==
               std::vector<std::string>{ "ev-pre" } );
    }

    SECTION( "pathway: highest raw earned wins; tie breaks lexicographically" )
    {
        AlternatePathway deep;
        deep.pathwayId = "pathway-deep";
        deep.title = "Deep route";
        deep.criterionIds = { "proc-pre" };
        AlternatePathway classic;
        classic.pathwayId = "pathway-classic";
        classic.title = "Classic route";
        classic.criterionIds = { "proc-train" };
        rubric.alternatePathways = { deep, classic };

        // Both pathways hold (both criteria earned) → tie on earned → the
        // lexicographically smaller pathwayId is chosen.
        GradeEvidence both;
        addStage( both, "ev-train", "train", "Completed" );
        addStage( both, "ev-pre", "preprocess", "Completed" );
        GradeOutcome outcome = grade( rubric, both );
        REQUIRE( outcome.ok );
        REQUIRE( outcome.report.matchedPathways.size() == 1 );
        CHECK( outcome.report.matchedPathways[0] == "pathway-classic" );

        // Only the deep pathway holds (train missing).
        GradeEvidence onlyPre;
        addStage( onlyPre, "ev-pre", "preprocess", "Completed" );
        outcome = grade( rubric, onlyPre );
        REQUIRE( outcome.ok );
        REQUIRE( outcome.report.matchedPathways.size() == 1 );
        CHECK( outcome.report.matchedPathways[0] == "pathway-deep" );

        // No pathway holds.
        GradeEvidence none;
        outcome = grade( rubric, none );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.matchedPathways.empty() );
    }

    SECTION( "pathway with a zero-earned member does not hold" )
    {
        AlternatePathway p;
        p.pathwayId = "p1";
        p.criterionIds = { "proc-train", "proc-pre" };
        rubric.alternatePathways = { p };
        GradeEvidence evidence;
        addStage( evidence, "ev-train", "train", "Completed" );
        const GradeOutcome outcome = grade( rubric, evidence );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.matchedPathways.empty() );
    }
}

TEST_CASE( "dimension aggregation and score accounting", "[grader][engine][aggregate]" )
{
    GradingRubric rubric;
    rubric.rubricId = "lab03-agg";
    rubric.revision = 1;
    rubric.totalPoints = 100.0;
    rubric.passingScore = 60.0;
    Dimension d1;
    d1.dimensionId = "process";
    d1.weight = 50.0;
    Dimension d2;
    d2.dimensionId = "result";
    d2.weight = 50.0;
    Criterion c1;
    c1.criterionId = "p1";
    c1.maxPoints = 50.0;
    c1.kind = CriterionKind::Stage;
    c1.evidenceKey = "train";
    c1.stage.expectedState = "Completed";
    d1.criteria.push_back( c1 );
    Criterion c2;
    c2.criterionId = "r1";
    c2.maxPoints = 50.0;
    c2.kind = CriterionKind::Metric;
    c2.evidenceKey = "kappa";
    c2.metric.mode = MetricExpectation::Mode::AtLeast;
    c2.metric.value = 0.8;
    d2.criteria.push_back( c2 );
    rubric.dimensions = { d1, d2 };

    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    addStage( evidence, "ev-stage", "train", "Completed" );
    GradeEvidenceItem metric;
    metric.evidenceId = "ev-metric";
    metric.kind = EvidenceKind::Metric;
    metric.key = "kappa";
    metric.hasValue = true;
    metric.value = 0.5; // below band → not earned
    evidence.items.push_back( metric );

    const GradeOutcome outcome = grade( rubric, evidence );
    REQUIRE( outcome.ok );
    const GradeReport &report = outcome.report;
    REQUIRE( report.dimensions.size() == 2 );
    CHECK( report.dimensions[0].dimensionId == "process" );
    CHECK( report.dimensions[0].earned == 50.0 );
    CHECK( report.dimensions[1].earned == 0.0 );
    CHECK( report.dimensions[1].criteria[0].status == OutcomeStatus::NotEarned );
    CHECK( report.score == 50.0 );
    CHECK( report.verdict == ReportVerdict::Partial );
}

TEST_CASE( "permutation invariance: shuffled evidence order yields the identical report",
           "[grader][engine][determinism]" )
{
    GradingRubric rubric;
    rubric.rubricId = "perm";
    rubric.revision = 3;
    rubric.totalPoints = 10.0;
    rubric.passingScore = 6.0;
    Dimension dim;
    dim.dimensionId = "d";
    dim.weight = 10.0;
    for ( int i = 0; i < 4; ++i ) {
        Criterion c;
        c.criterionId = "c" + std::to_string( i );
        c.maxPoints = 2.5;
        c.kind = CriterionKind::Stage;
        c.evidenceKey = "stage" + std::to_string( i );
        c.stage.expectedState = "Completed";
        dim.criteria.push_back( c );
    }
    rubric.dimensions.push_back( dim );

    GradeEvidence evidence;
    evidence.subject.experimentId = "exp-1";
    for ( int i = 0; i < 4; ++i )
        addStage( evidence, "ev-" + std::to_string( ( i * 7 ) % 4 ), "stage" + std::to_string( i ),
                  "Completed", "2026-09-23T0" + std::to_string( i ) + ":00:00Z" );

    const GradeOutcome base = grade( rubric, evidence );
    REQUIRE( base.ok );

    // Every rotation of the item array must produce the identical judgment.
    // The evidenceDigest is deliberately EXCLUDED from that comparison: it
    // binds the recorded document bytes (tamper evidence), so a permuted
    // input document carries a different digest by design — but every
    // judgment member (scores, statuses, citations, verdict) must match.
    GradeEvidence shuffled = evidence;
    GraderError ignored;
    std::string baseBody;
    for ( int rotation = 0; rotation < 4; ++rotation ) {
        const GradeOutcome outcome = grade( rubric, shuffled );
        REQUIRE( outcome.ok );
        GradeReport normalized = outcome.report;
        normalized.evidenceDigest.clear();
        const std::string body = canonicalizeJson( normalized.toBodyJson(), ignored ).value();
        if ( rotation == 0 )
            baseBody = body;
        CHECK( body == baseBody );
        std::rotate( shuffled.items.begin(), shuffled.items.begin() + 1, shuffled.items.end() );
    }

    // The permuted inputs still verify: each digest binds ITS OWN document.
    // One MORE rotation so the final document is genuinely a different byte
    // sequence (4 rotations would be the identity).
    std::rotate( shuffled.items.begin(), shuffled.items.begin() + 1, shuffled.items.end() );
    const GradeOutcome rotated = grade( rubric, shuffled );
    REQUIRE( rotated.ok );
    CHECK( base.report.evidenceDigest != rotated.report.evidenceDigest );
    GraderError digestError;
    CHECK( base.report.verifyDigest( digestError ) );
    CHECK( rotated.report.verifyDigest( digestError ) );
}

TEST_CASE( "budget overrun is a typed refusal with no report", "[grader][engine][budget]" )
{
    GradingRubric rubric = stageRubric();
    rubric.budgets.maxEvidenceItems = 2;
    GradeEvidence evidence;
    for ( int i = 0; i < 3; ++i ) {
        GradeEvidenceItem item;
        item.evidenceId = "ev-" + std::to_string( i );
        item.kind = EvidenceKind::Custom;
        item.key = "k" + std::to_string( i );
        evidence.items.push_back( item );
    }
    const GradeOutcome outcome = grade( rubric, evidence );
    CHECK_FALSE( outcome.ok );
    CHECK( toCodeString( outcome.error.code ) == "grader:e-schema-shape" );
    CHECK( outcome.error.message.find( "budget" ) != std::string::npos );
}

TEST_CASE( "digests bind the graded documents; tampering breaks verification",
           "[grader][engine][digest]" )
{
    const GradingRubric rubric = stageRubric();
    const GradeEvidence evidence = stageEvidence();
    const GradeOutcome outcome = grade( rubric, evidence );
    REQUIRE( outcome.ok );
    const GradeReport report = outcome.report;

    // rubricDigest is the sha256 of the canonical rubric document.
    GraderError ignored;
    const std::string rubricCanon = canonicalizeJson( rubric.toJson(), ignored ).value();
    CHECK( report.rubricDigest == sha256Hex( rubricCanon ) );
    const std::string evidenceCanon = canonicalizeJson( evidence.toJson(), ignored ).value();
    CHECK( report.evidenceDigest == sha256Hex( evidenceCanon ) );

    // Tampering with any body member breaks verifyDigest.
    GradeReport tampered = report;
    tampered.score = 0.0;
    GraderError err;
    CHECK_FALSE( tampered.verifyDigest( err ) );

    // The digest travels through JSON round-trips.
    const Json::Value doc = report.toJson();
    auto parsed = GradeReport::fromJson( doc, err );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->verifyDigest( err ) );
}

TEST_CASE( "rounding and verdict boundaries behave deterministically",
           "[grader][engine][verdict]" )
{
    GradingRubric rubric = stageRubric();
    rubric.passingScore = 100.0; // exact-full boundary

    SECTION( "exact pass boundary" )
    {
        const GradeOutcome outcome = grade( rubric, stageEvidence() );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.verdict == ReportVerdict::Pass );
    }

    SECTION( "just below the boundary is partial" )
    {
        // Split the rubric: 50/50, only one side earns → 50 < 100 → partial.
        Criterion c2;
        c2.criterionId = "proc-eval";
        c2.maxPoints = 50.0;
        rubric.dimensions[0].criteria[0].maxPoints = 50.0;
        c2.kind = CriterionKind::Stage;
        c2.evidenceKey = "evaluate";
        c2.stage.expectedState = "Completed";
        rubric.dimensions[0].criteria.push_back( c2 );
        const GradeOutcome outcome = grade( rubric, stageEvidence() );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.score == 50.0 );
        CHECK( outcome.report.verdict == ReportVerdict::Partial );
    }

    SECTION( "zero score without constraint violation is fail, not blocked" )
    {
        const GradeOutcome outcome = grade( rubric, stageEvidence( "Failed" ) );
        REQUIRE( outcome.ok );
        CHECK( outcome.report.score == 0.0 );
        CHECK( outcome.report.verdict == ReportVerdict::Fail );
    }
}

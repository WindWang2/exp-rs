// test_grader_render.cpp — RS14-05 Slices B/C: report renderers (ADR 0174:
// "the same machine-readable report" feeds every audience). Light lane:
// Catch2 + sicnu_grader only.
//
// Three views over ONE GradeReport:
//   * student feedback  — never leaks rubric internals: no teacher hints, no
//     requiredFacts values, no misconception patterns, no accepted
//     alternatives (the golden answers stay with the teacher).
//   * teacher diagnostics — the opposite contract: everything the grader
//     saw, joined with teacher hints and indeterminate-item triage.
//   * machine summary — compact, digest-carrying, verdict-complete.
//
// RED-first record: no renderer surface exists on master.
#include "grader/grader_engine.h"
#include "grader/grader_json.h"
#include "grader/grader_render.h"
#include "grader/grader_types.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace sicnu::grader;

namespace {

/// A rubric whose TEACHER-ONLY members are distinguishable poison strings:
/// if any of them reaches the student view, the leak check fires.
struct Fixture
{
    GradingRubric rubric;
    GradeEvidence evidence;

    Fixture()
    {
        rubric.rubricId = "lab03";
        rubric.revision = 2;
        rubric.title = "Lab 03";
        rubric.totalPoints = 100.0;
        rubric.passingScore = 70.0;
        Dimension dim;
        dim.dimensionId = "process";
        dim.title = "Process dimension";
        dim.weight = 100.0;
        Criterion stage;
        stage.criterionId = "proc-train";
        stage.title = "Training stage";
        stage.maxPoints = 60.0;
        stage.kind = CriterionKind::Stage;
        stage.evidenceKey = "train";
        stage.stage.expectedState = "Completed";
        stage.studentHint = "Check the training step finished.";
        stage.teacherHint = "TEACHER-POISON: most students fail the loader.";
        dim.criteria.push_back( stage );
        Criterion fact;
        fact.criterionId = "fact-manifest";
        fact.title = "Manifest verified";
        fact.maxPoints = 40.0;
        fact.kind = CriterionKind::Fact;
        fact.evidenceKey = "dataset_manifest";
        fact.fact.expectedState = "verified";
        fact.fact.requiredFacts["secret_algorithm"] = "\"golden-svm\"";
        fact.studentHint = "Verify your dataset manifest.";
        fact.teacherHint = "TEACHER-POISON: manifest fact is the golden key.";
        dim.criteria.push_back( fact );
        rubric.dimensions.push_back( dim );

        // Stage completed; manifest fact contradicted → not earned.
        evidence.subject.experimentId = "exp-1";
        GradeEvidenceItem train;
        train.evidenceId = "ev-1";
        train.kind = EvidenceKind::Stage;
        train.key = "train";
        train.state = "Completed";
        evidence.items.push_back( train );
        GradeEvidenceItem manifest;
        manifest.evidenceId = "ev-2";
        manifest.kind = EvidenceKind::Provenance;
        manifest.key = "dataset_manifest";
        manifest.state = "pending";
        evidence.items.push_back( manifest );
    }
};

} // namespace

TEST_CASE( "student feedback never leaks golden rubric internals",
           "[grader][render][student][leak]" )
{
    Fixture f;
    const GradeOutcome outcome = grade( f.rubric, f.evidence );
    REQUIRE( outcome.ok );
    const Json::Value view = renderStudentFeedback( outcome.report, &f.rubric );
    const std::string text = Json::writeString( Json::StreamWriterBuilder{}, view );

    // The poison strings live only in teacher hints / requiredFacts.
    CHECK( text.find( "TEACHER-POISON" ) == std::string::npos );
    CHECK( text.find( "golden-svm" ) == std::string::npos );
    CHECK( text.find( "secret_algorithm" ) == std::string::npos );

    // The view still carries the student-relevant facts.
    CHECK( view["schema"].asString() == "sicnu.grader.student-feedback/1" );
    CHECK( view["score"].asDouble() == 60.0 );
    CHECK( view["totalPoints"].asDouble() == 100.0 );
    CHECK( view["verdict"].asString() == "partial" );
    CHECK( view["rubricId"].asString() == "lab03" );
    REQUIRE( view["dimensions"].isArray() );
    REQUIRE( view["dimensions"].size() == 1 );
    REQUIRE( view["dimensions"][0]["criteria"].size() == 2 );
    // Not-earned criterion surfaces the STUDENT hint (joined from the rubric).
    bool sawStudentHint = false;
    for ( const auto &c : view["dimensions"][0]["criteria"] ) {
        if ( c["criterionId"].asString() == "fact-manifest" ) {
            CHECK( c["earned"].asDouble() == 0.0 );
            CHECK( c["status"].asString() == "not_earned" );
            CHECK( c["studentHint"].asString() == "Verify your dataset manifest." );
            sawStudentHint = true;
        }
    }
    CHECK( sawStudentHint );
}

TEST_CASE( "student feedback works without a rubric (id-only view)",
           "[grader][render][student]" )
{
    Fixture f;
    const GradeOutcome outcome = grade( f.rubric, f.evidence );
    REQUIRE( outcome.ok );
    const Json::Value view = renderStudentFeedback( outcome.report, nullptr );
    CHECK( view["schema"].asString() == "sicnu.grader.student-feedback/1" );
    CHECK( view["score"].asDouble() == 60.0 );
    REQUIRE( view["dimensions"][0]["criteria"].size() == 2 );
    // No hint join without the rubric — and no leak either way.
    const std::string text = Json::writeString( Json::StreamWriterBuilder{}, view );
    CHECK( text.find( "TEACHER-POISON" ) == std::string::npos );
}

TEST_CASE( "teacher diagnostics join teacher hints and cite evidence",
           "[grader][render][teacher]" )
{
    Fixture f;
    const GradeOutcome outcome = grade( f.rubric, f.evidence );
    REQUIRE( outcome.ok );
    const Json::Value view = renderTeacherDiagnostics( outcome.report, f.rubric );
    CHECK( view["schema"].asString() == "sicnu.grader.teacher-diagnostics/1" );
    CHECK( view["score"].asDouble() == 60.0 );

    // Teacher hints ARE present here (the teacher owns the rubric).
    const std::string text = Json::writeString( Json::StreamWriterBuilder{}, view );
    CHECK( text.find( "TEACHER-POISON" ) != std::string::npos );

    // The contradicted criterion lists its reason chain + cited evidence.
    bool sawReasons = false;
    for ( const auto &dim : view["dimensions"] )
        for ( const auto &c : dim["criteria"] ) {
            if ( c["criterionId"].asString() == "fact-manifest" ) {
                CHECK( c["status"].asString() == "not_earned" );
                REQUIRE( c["reasonCodes"].size() >= 1 );
                CHECK( c["reasonCodes"][0].asString().rfind( "grader:", 0 ) == 0 );
                REQUIRE( c["evidenceIds"].size() == 1 );
                CHECK( c["evidenceIds"][0].asString() == "ev-2" );
                CHECK( c["teacherHint"].asString().find( "TEACHER-POISON" ) != std::string::npos );
                sawReasons = true;
            }
        }
    CHECK( sawReasons );

    // Indeterminate items get their own triage list (empty here).
    CHECK( view["indeterminate"].isArray() );
}

TEST_CASE( "machine summary is compact, digest-carrying and stable",
           "[grader][render][machine]" )
{
    Fixture f;
    const GradeOutcome outcome = grade( f.rubric, f.evidence );
    REQUIRE( outcome.ok );
    const Json::Value view = renderMachineSummary( outcome.report );
    CHECK( view["schema"].asString() == "sicnu.grader.machine-summary/1" );
    CHECK( view["rubricId"].asString() == "lab03" );
    CHECK( view["rubricRevision"].asInt() == 2 );
    CHECK( view["score"].asDouble() == 60.0 );
    CHECK( view["totalPoints"].asDouble() == 100.0 );
    CHECK( view["passingScore"].asDouble() == 70.0 );
    CHECK( view["verdict"].asString() == "partial" );
    CHECK( view["digest"].asString() == outcome.report.digest );
    CHECK( view["evidenceDigest"].asString() == outcome.report.evidenceDigest );
    CHECK( view["subject"]["experimentId"].asString() == "exp-1" );
    // Compact: no per-criterion detail in the machine summary.
    CHECK_FALSE( view.isMember( "dimensions" ) );
    // Violated constraints surface (blocking conditions must be machine-visible).
    CHECK( view["violatedConstraints"].isArray() );
    CHECK( view["violatedConstraints"].empty() );
}

TEST_CASE( "all renderers are deterministic over the same report",
           "[grader][render][determinism]" )
{
    Fixture f;
    const GradeOutcome first = grade( f.rubric, f.evidence );
    const GradeOutcome second = grade( f.rubric, f.evidence );
    REQUIRE( first.ok );
    REQUIRE( second.ok );

    GraderError ignored;
    CHECK( canonicalizeJson( renderStudentFeedback( first.report, &f.rubric ), ignored ).value() ==
           canonicalizeJson( renderStudentFeedback( second.report, &f.rubric ), ignored ).value() );
    CHECK( canonicalizeJson( renderTeacherDiagnostics( first.report, f.rubric ), ignored ).value() ==
           canonicalizeJson( renderTeacherDiagnostics( second.report, f.rubric ), ignored ).value() );
    CHECK( canonicalizeJson( renderMachineSummary( first.report ), ignored ).value() ==
           canonicalizeJson( renderMachineSummary( second.report ), ignored ).value() );
}

TEST_CASE( "blocked (zeroed) reports surface the constraint in every view",
           "[grader][render][blocked]" )
{
    Fixture f;
    HardConstraint hc;
    hc.constraintId = "hc-no-leak";
    hc.title = "No leakage";
    hc.mode = HardConstraint::Mode::ForbiddenEvidence;
    hc.evidenceKey = "leaky_step";
    hc.effect = HardConstraint::Effect::Zero;
    hc.explanation = "Leakage recorded.";
    f.rubric.hardConstraints.push_back( hc );
    GradeEvidenceItem leak;
    leak.evidenceId = "ev-leak";
    leak.kind = EvidenceKind::Custom;
    leak.key = "leaky_step";
    leak.state = "recorded";
    f.evidence.items.push_back( leak );

    const GradeOutcome outcome = grade( f.rubric, f.evidence );
    REQUIRE( outcome.ok );
    CHECK( outcome.report.verdict == ReportVerdict::Blocked );

    const Json::Value student = renderStudentFeedback( outcome.report, &f.rubric );
    CHECK( student["verdict"].asString() == "blocked" );
    REQUIRE( student["blockedBy"].isArray() );
    CHECK( student["blockedBy"][0].asString() == "hc-no-leak" );

    const Json::Value machine = renderMachineSummary( outcome.report );
    REQUIRE( machine["violatedConstraints"].size() == 1 );
    CHECK( machine["violatedConstraints"][0]["constraintId"].asString() == "hc-no-leak" );

    const Json::Value teacher = renderTeacherDiagnostics( outcome.report, f.rubric );
    CHECK( teacher["verdict"].asString() == "blocked" );
}

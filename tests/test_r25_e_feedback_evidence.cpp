/***************************************************************************
  tests/test_r25_e_feedback_evidence.cpp — R25-E (W2/W4): the evidence a
  batch row ALREADY carries must reach the people it explains.

  The batch rows grade under a real CLI and come back with additive
  explainability fields — topDeduction (first deduction id of the grading
  transcript), graderDigest (digest of the sicnu.lab.grade/1 transcript
  body) and unavailableReason (typed grader-side refusal vocabulary). The
  per-student feedback pack used to drop all three: the student saw a
  score and a verdict with NO auditable why, and an unavailable row gave
  no re-runnable explanation. Ditto the class summary: the report-level
  run anomalies (resumed rows, checkpoint invalidated by a rules change,
  truncated discovery, refused files, cancelled early) were invisible to
  the teacher's at-a-glance view.

  Pins (all additive; every value sourced from the row/report itself —
  nothing is invented, and absent evidence stays visibly absent):
    1. grading_evidence{top_deduction, grader_digest, unavailable_reason}
       in the pack, populated from the row.
    2. run_integrity{resumed, checkpoint_invalidated, truncated,
       skipped_files, cancelled_early} in the class summary, populated
       from the report.
    3. The #1689 cohort leak guard still holds over the extended pack.
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/class_summary.h"
#include "teaching_admin/feedback_pack.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace sicnu::teaching_admin;

namespace
{

BatchRowResult row( const QString &studentId )
{
    BatchRowResult r;
    r.studentId = studentId;
    r.labId = QStringLiteral( "lab25e" );
    r.status = QStringLiteral( "fail" );
    r.verdict = QStringLiteral( "fail" );
    r.score = 55.0;
    return r;
}

} // namespace

TEST_CASE( "feedback pack carries the row's grading evidence",
           "[r25_e][feedback][evidence]" )
{
    SECTION( "a graded row with deductions exposes the auditable why" )
    {
        BatchRowResult r = row( QStringLiteral( "alice" ) );
        r.topDeduction = QStringLiteral( "stats.ndvi_mean_sigma" );
        r.graderDigest = QStringLiteral( "sha256:0123abcd" );

        const QJsonObject pack = buildFeedbackPack( FeedbackPackInput{ r } );
        const QJsonObject evidence =
          pack.value( QStringLiteral( "grading_evidence" ) ).toObject();
        REQUIRE( evidence.value( QStringLiteral( "top_deduction" ) ).toString() ==
                 QStringLiteral( "stats.ndvi_mean_sigma" ) );
        REQUIRE( evidence.value( QStringLiteral( "grader_digest" ) ).toString() ==
                 QStringLiteral( "sha256:0123abcd" ) );
        REQUIRE( evidence.value( QStringLiteral( "unavailable_reason" ) ).toString().isEmpty() );
    }

    SECTION( "an unavailable row explains itself as re-runnable, not failed" )
    {
        BatchRowResult r = row( QStringLiteral( "bob" ) );
        r.status = QStringLiteral( "unavailable" );
        r.score = -1.0;
        r.unavailableReason = QStringLiteral( "grader_timeout" );

        const QJsonObject pack = buildFeedbackPack( FeedbackPackInput{ r } );
        const QJsonObject evidence =
          pack.value( QStringLiteral( "grading_evidence" ) ).toObject();
        REQUIRE( evidence.value( QStringLiteral( "unavailable_reason" ) ).toString() ==
                 QStringLiteral( "grader_timeout" ) );
        // No grade was produced — so no deduction/digest may be implied.
        REQUIRE( evidence.value( QStringLiteral( "top_deduction" ) ).toString().isEmpty() );
        REQUIRE( evidence.value( QStringLiteral( "grader_digest" ) ).toString().isEmpty() );
    }

    SECTION( "a clean row keeps the block visibly empty, never fabricated" )
    {
        BatchRowResult r = row( QStringLiteral( "cara" ) );
        r.status = QStringLiteral( "pass" );
        r.score = 100.0;

        const QJsonObject pack = buildFeedbackPack( FeedbackPackInput{ r } );
        const QJsonObject evidence =
          pack.value( QStringLiteral( "grading_evidence" ) ).toObject();
        REQUIRE( evidence.contains( QStringLiteral( "top_deduction" ) ) );
        REQUIRE( evidence.value( QStringLiteral( "top_deduction" ) ).toString().isEmpty() );
        REQUIRE( evidence.value( QStringLiteral( "grader_digest" ) ).toString().isEmpty() );
        REQUIRE( evidence.value( QStringLiteral( "unavailable_reason" ) ).toString().isEmpty() );
    }
}

TEST_CASE( "class summary surfaces the run-integrity anomalies",
           "[r25_e][feedback][summary]" )
{
    BatchAssessmentReport report;
    report.labId = QStringLiteral( "lab25e" );
    report.total = 5;
    report.graded = 3;
    report.resumed = 2;
    report.truncated = 1;
    report.checkpointInvalidated = true;
    DiscoverySkip skip;
    skip.studentId = QStringLiteral( "dave" );
    skip.path = QStringLiteral( "/class/dave/dave_feedback.json" );
    skip.reason = QStringLiteral( "reserved output name" );
    report.skippedFiles.push_back( skip );

    const QJsonObject summary = buildClassSummary( report );
    const QJsonObject integrity =
      summary.value( QStringLiteral( "run_integrity" ) ).toObject();
    REQUIRE( integrity.value( QStringLiteral( "resumed_rows" ) ).toInt() == 2 );
    REQUIRE( integrity.value( QStringLiteral( "truncated_rows" ) ).toInt() == 1 );
    REQUIRE( integrity.value( QStringLiteral( "skipped_files" ) ).toInt() == 1 );
    REQUIRE( integrity.value( QStringLiteral( "checkpoint_invalidated" ) ).toBool() == true );
    REQUIRE( integrity.value( QStringLiteral( "cancelled_early" ) ).toBool() == false );
}

TEST_CASE( "the cohort leak guard holds over the extended pack (#1689 regression)",
           "[r25_e][feedback][privacy]" )
{
    BatchRowResult foreign = row( QStringLiteral( "alice" ) );
    foreign.topDeduction = QStringLiteral( "stats.ndvi_mean_sigma" );
    const QJsonObject pack = buildFeedbackPack( FeedbackPackInput{ foreign } );

    // The owner's own evidence is legitimate in its own pack…
    const ValidationResult ownOk =
      assertNoCrossStudentLeak( pack, QStringLiteral( "alice" ), { QStringLiteral( "alice" ), QStringLiteral( "bob" ) } );
    REQUIRE( ownOk.ok );

    // …and a foreign id planted anywhere the guard scans — including the
    // new evidence block — still withholds the pack.
    BatchRowResult leaker = row( QStringLiteral( "alice" ) );
    leaker.message = QStringLiteral( "compared with bob submission" );
    const QJsonObject leakingPack = buildFeedbackPack( FeedbackPackInput{ leaker } );
    const ValidationResult blocked =
      assertNoCrossStudentLeak( leakingPack, QStringLiteral( "alice" ), { QStringLiteral( "alice" ), QStringLiteral( "bob" ) } );
    REQUIRE( !blocked.ok );
}

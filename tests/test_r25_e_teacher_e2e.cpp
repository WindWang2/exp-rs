/***************************************************************************
  tests/test_r25_e_teacher_e2e.cpp — R25-E (W7): the teacher's whole-class
  loop as ONE scenario, on the production seams.

  导入班级 → (single-walk discovery via the CT-C6 seam) → 批量评分 →
  JSON+CSV 成对发布 → 反馈包(带评分证据, 泄露守卫) → 班级摘要(异常可见).

  The rows are produced by an injected fake grader (the same seam the real
  CLI adapter implements — runBatchAssessment's contract), with statuses a
  real class produces: a pass, a grader-side unavailable (typed reason),
  and a corrupted artifact. Every published artifact must account for all
  three — no silent vanish, no fabricated pass.
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/class_summary.h"
#include "teaching_admin/feedback_pack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace sicnu::teaching_admin;

namespace
{

struct ClassDirectory
{
    QTemporaryDir dir;
    QDir submissions;

    explicit ClassDirectory()
    {
        submissions = dir.path() + QStringLiteral( "/class2025" );
        submissions.mkpath( QStringLiteral( "." ) );

        auto writeFile = []( const QString &path, const QByteArray &bytes ) {
            QFile f( path );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( bytes );
        };
        QDir( submissions.filePath( QStringLiteral( "alice" ) ) )
          .mkpath( QStringLiteral( "." ) );
        writeFile( submissions.filePath( QStringLiteral( "alice/ndvi.tif" ) ),
                   QByteArray( "alice-graded-bytes" ) );
        QDir( submissions.filePath( QStringLiteral( "bob" ) ) )
          .mkpath( QStringLiteral( "." ) );
        writeFile( submissions.filePath( QStringLiteral( "bob/ndvi.tif" ) ),
                   QByteArray( "bob-graded-bytes" ) );
        QDir( submissions.filePath( QStringLiteral( "cara" ) ) )
          .mkpath( QStringLiteral( "." ) );
        writeFile( submissions.filePath( QStringLiteral( "cara/ndvi.tif" ) ),
                   QByteArray( "this is not a raster" ) );
        // The console's own feedback output sitting inside the class tree:
        // discovery must refuse AND account it (#1466).
        writeFile( submissions.filePath( QStringLiteral( "cara/cara_feedback.json" ) ),
                   QByteArray( "{}" ) );
    }

    QString outPrefix() const { return dir.path() + QStringLiteral( "/grades2025" ); }
    QString checkpointPath() const { return dir.path() + QStringLiteral( "/grades2025.ckpt.json" ); }
};

/// Fake grading authority with the real CLI's row vocabulary: deterministic
/// per student — alice passes with a deduction trail, bob's grade cannot be
/// produced (typed reason), cara's artifact is corrupt.
BatchRowResult gradeLikeTheCli( const SubmissionItem &item )
{
    BatchRowResult row;
    row.studentId = item.studentId;
    row.artifactPath = item.path;
    row.labId = QStringLiteral( "lab25e_e2e" );
    row.rubricVersion = QStringLiteral( "rules:fp:abc123" );
    row.labVersion = QStringLiteral( "v3" );
    row.softwareVersion = QStringLiteral( "test" );
    if ( item.studentId == QLatin1String( "alice" ) )
    {
        row.status = QStringLiteral( "pass" );
        row.verdict = QStringLiteral( "pass" );
        row.score = 85.0;
        row.graderDigest = QStringLiteral( "sha256:cafebabe" );
        row.topDeduction = QString();
    }
    else if ( item.studentId == QLatin1String( "bob" ) )
    {
        row.status = QStringLiteral( "unavailable" );
        row.verdict = QString();
        row.score = -1.0;
        row.unavailableReason = QStringLiteral( "grader_timeout" );
    }
    else
    {
        row.status = QStringLiteral( "corrupted" );
        row.verdict = QStringLiteral( "corrupted" );
        row.score = -1.0;
        row.message = QStringLiteral( "not readable as a raster" );
    }
    return row;
}

} // namespace

TEST_CASE( "teacher loop: import class -> batch grade -> publish pair -> feedback -> summary",
           "[r25_e][e2e][teacher]" )
{
    ClassDirectory cls;

    // Import: the ONE directory walk (CT-C6 seam) — the console's preview
    // scan doubles as the orchestrator's discovery.
    QVector<DiscoverySkip> skips;
    const auto walked = discoverSubmissions( cls.submissions.path(), &skips );
    REQUIRE( walked.size() == 3 );
    REQUIRE( skips.size() == 1 );
    REQUIRE( skips.front().studentId == QLatin1String( "cara" ) );

    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab25e_e2e" );
    cfg.submissionsDir = cls.submissions.path();
    cfg.outDir = cls.dir.path();
    cfg.rubricVersion = QStringLiteral( "rules:fp:abc123" );
    cfg.labVersion = QStringLiteral( "v3" );
    cfg.softwareVersion = QStringLiteral( "test" );
    cfg.maxConcurrency = 1;
    cfg.checkpointPath = cls.checkpointPath();
    cfg.preDiscovered = walked;
    cfg.preDiscoveredSkips = skips;

    const BatchAssessmentReport report = runBatchAssessment( cfg, gradeLikeTheCli );
    REQUIRE( report.total == 3 );
    REQUIRE( report.graded == 1 );    // alice (graded = pass|fail rows)
    REQUIRE( report.corrupted == 1 ); // cara
    REQUIRE( report.unavailable == 1 ); // bob (typed, not a silent zero)
    REQUIRE( report.skippedFiles.size() == 1 );

    // Publish: the JSON+CSV pair lands together or not at all.
    REQUIRE( publishBatchOutputsAtomic( report, cls.outPrefix() ) );
    REQUIRE( QFileInfo::exists( cls.outPrefix() + QStringLiteral( ".json" ) ) );
    REQUIRE( QFileInfo::exists( cls.outPrefix() + QStringLiteral( ".csv" ) ) );

    // The published JSON reloads through the fail-closed loader with the
    // same accounting.
    BatchAssessmentReport reloaded;
    QString error, warning;
    REQUIRE( loadBatchReportFromFile( cls.outPrefix() + QStringLiteral( ".json" ), reloaded,
                                      error, &warning ) );
    REQUIRE( reloaded.total == 3 );
    REQUIRE( reloaded.rows.size() == 3 );
    REQUIRE( warning.isEmpty() );

    // Feedback packs: guarded with the class roster, every pack carries its
    // own grading evidence, and the unusable grades say WHY they are
    // re-runnable instead of reading as zero.
    QVector<FeedbackPackInput> packInputs;
    QStringList cohort;
    for ( const auto &row : report.rows )
    {
        FeedbackPackInput in;
        in.row = row;
        packInputs.append( in );
        cohort.append( row.studentId );
    }
    const FeedbackPackEmission emission = buildGuardedFeedbackPacks( packInputs, cohort );
    REQUIRE( emission.blockedStudents.isEmpty() );
    REQUIRE( emission.packs.size() == 3 );

    bool sawUnavailable = false;
    bool sawPass = false;
    for ( const QJsonObject &pack : emission.packs )
    {
        const QString student = pack.value( QStringLiteral( "student_id" ) ).toString();
        const QJsonObject evidence = pack.value( QStringLiteral( "grading_evidence" ) ).toObject();
        if ( student == QLatin1String( "alice" ) )
        {
            sawPass = true;
            REQUIRE( evidence.value( QStringLiteral( "grader_digest" ) ).toString() ==
                     QStringLiteral( "sha256:cafebabe" ) );
        }
        if ( student == QLatin1String( "bob" ) )
        {
            sawUnavailable = true;
            REQUIRE( evidence.value( QStringLiteral( "unavailable_reason" ) ).toString() ==
                     QStringLiteral( "grader_timeout" ) );
        }
    }
    REQUIRE( sawPass );
    REQUIRE( sawUnavailable );

    // Class summary: the anomaly lens shows the refused file and the run's
    // integrity in one view.
    const QJsonObject summary = buildClassSummary( report );
    const QJsonObject integrity = summary.value( QStringLiteral( "run_integrity" ) ).toObject();
    REQUIRE( integrity.value( QStringLiteral( "skipped_files" ) ).toInt() == 1 );
    REQUIRE( integrity.value( QStringLiteral( "resumed_rows" ) ).toInt() == 0 );

    // Restart honesty (the #1694 seam-transport angle): re-running with the
    // SAME config adopts the checkpoint rows instead of re-grading, and the
    // summary reports the adoption.
    BatchAssessmentConfig resumeCfg = cfg;
    const BatchAssessmentReport resumed = runBatchAssessment( resumeCfg, gradeLikeTheCli );
    REQUIRE( resumed.resumed == 3 );
    const QJsonObject resumedSummary = buildClassSummary( resumed );
    REQUIRE( resumedSummary.value( QStringLiteral( "run_integrity" ) ).toObject()
               .value( QStringLiteral( "resumed_rows" ) )
               .toInt() == 3 );
}

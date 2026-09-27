/***************************************************************************
  tests/test_teaching_batch_r4.cpp — WP-B (Track 12, teaching-lab R4):
  batch failure SEMANTICS over the public orchestration seam
  (runBatchAssessment / publishBatchOutputsAtomic / buildClassSummary).

  Selected semantics (DECISIONS.md): durable resume — completed rows are
  checkpointed, cancelled items are never checkpointed, a corrupt or foreign
  checkpoint degrades to a fresh start. Every case drives the public API
  only; the checkpoint file is read back as a document (independent truth),
  not via internal state.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/class_summary.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <atomic>
#include <stdexcept>

using namespace sicnu::teaching_admin;

namespace
{

/// submissions/<student>/<file> layout with names the discovery filters do
/// NOT skip (no batch/summary/feedback/grades substrings).
struct SubmissionTree
{
    QTemporaryDir dir;
    QDir submissions;

    SubmissionTree( const QStringList &students )
    {
        submissions = dir.path() + QStringLiteral( "/submissions" );
        submissions.mkpath( QStringLiteral( "." ) );
        int fileNo = 0;
        for ( const QString &student : students )
        {
            QDir sd( submissions.filePath( student ) );
            sd.mkpath( QStringLiteral( "." ) );
            sd.mkpath( QStringLiteral( "." ) );
            const QString file = QStringLiteral( "artifact_%1.tif" ).arg( ++fileNo );
            QFile f( sd.filePath( file ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( "tif-bytes" );
        }
    }

    QString checkpointPath() const { return dir.path() + QStringLiteral( "/ckpt.json" ); }
};

BatchAssessmentConfig baseConfig( const QString &submissionsDir, const QString &checkpointPath )
{
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab90_r4" );
    cfg.submissionsDir = submissionsDir;
    cfg.outDir = submissionsDir;
    cfg.rubricVersion = QStringLiteral( "r1" );
    cfg.labVersion = QStringLiteral( "v1" );
    cfg.softwareVersion = QStringLiteral( "test" );
    cfg.maxConcurrency = 1; // deterministic single-worker schedule
    cfg.maxSubmissions = -1;
    cfg.checkpointPath = checkpointPath;
    return cfg;
}

BatchRowResult passingRow( const SubmissionItem &item )
{
    BatchRowResult row;
    row.studentId = item.studentId;
    row.artifactPath = item.path;
    row.status = QStringLiteral( "pass" );
    row.verdict = QStringLiteral( "pass" );
    row.score = 88.0;
    return row;
}

QJsonObject readJson( const QString &path )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    return QJsonDocument::fromJson( f.readAll() ).object();
}

} // namespace

TEST_CASE( "batch r4: empty submissions dir is a typed empty run, not an error",
           "[teaching_r4][batch]" )
{
    SubmissionTree tree( {} );
    const QString ckpt = tree.checkpointPath();
    const BatchAssessmentReport report =
      runBatchAssessment( baseConfig( tree.submissions.path(), ckpt ),
                          []( const SubmissionItem & ) {
                              FAIL( "callable must not run on an empty batch" );
                              return BatchRowResult{};
                          } );

    CHECK( report.total == 0 );
    CHECK( report.rows.isEmpty() );
    CHECK( report.graded == 0 );
    CHECK( report.truncated == 0 );
    CHECK_FALSE( report.cancelledEarly );

    // The checkpoint is a valid, schema-carrying empty document — an audit
    // can tell "ran on an empty class" from "never ran".
    const QJsonObject doc = readJson( ckpt );
    CHECK( doc.value( "schema" ).toString() == QLatin1String( "sicnu.teaching.batch_checkpoint/1" ) );
    CHECK( doc.value( "rows" ).toArray().isEmpty() );

    // Summary of an empty class: zero, not NaN / not fabricated.
    const QJsonObject summary = buildClassSummary( report );
    CHECK( summary.value( "total" ).toInt() == 0 );
    CHECK( summary.value( "completion_rate" ).toDouble() == 0.0 );
}

TEST_CASE( "batch r4: all-fail run keeps every row typed and never fabricates a pass",
           "[teaching_r4][batch]" )
{
    SubmissionTree tree( { QStringLiteral( "s1" ), QStringLiteral( "s2" ), QStringLiteral( "s3" ) } );
    BatchAssessmentConfig cfg = baseConfig( tree.submissions.path(), QString() );

    const BatchAssessmentReport report = runBatchAssessment(
      cfg,
      []( const SubmissionItem &item ) {
          BatchRowResult row = passingRow( item );
          row.status = QStringLiteral( "fail" );
          row.verdict = QStringLiteral( "fail" );
          row.score = 42.0;
          row.message = QStringLiteral( "band math wrong" );
          return row;
      } );

    REQUIRE( report.rows.size() == 3 );
    CHECK( report.graded == 3 ); // pass|fail are both "graded" — no silent zero
    CHECK( report.failed == 0 );
    for ( const BatchRowResult &row : report.rows )
    {
        CHECK( row.status == QLatin1String( "fail" ) );
        CHECK( row.verdict == QLatin1String( "fail" ) );
    }

    const QJsonObject summary = buildClassSummary( report );
    CHECK( summary.value( "completion_count" ).toInt() == 3 );
    CHECK( summary.value( "status_counts" ).toObject().value( "fail" ).toInt() == 3 );
    // All scores 42 → exactly one populated bucket (40-50); the -1-free
    // histogram stays honest.
    const QJsonArray hist = summary.value( "rubric_score_distribution" ).toArray();
    int populated = 0;
    for ( const auto &b : hist )
        if ( b.toObject().value( "count" ).toInt() > 0 )
            ++populated;
    CHECK( populated == 1 );
}

TEST_CASE( "batch r4: a throwing callable is isolated to one typed error row",
           "[teaching_r4][batch]" )
{
    SubmissionTree tree( { QStringLiteral( "s1" ), QStringLiteral( "s2" ), QStringLiteral( "s3" ) } );
    const BatchAssessmentReport report = runBatchAssessment(
      baseConfig( tree.submissions.path(), QString() ),
      []( const SubmissionItem &item ) {
          if ( item.studentId == QLatin1String( "s2" ) )
              throw std::runtime_error( "grader exploded" );
          return passingRow( item );
      } );

    REQUIRE( report.rows.size() == 3 );
    CHECK( report.graded == 2 );
    CHECK( report.failed == 1 );

    const BatchRowResult *errorRow = nullptr;
    for ( const BatchRowResult &row : report.rows )
        if ( row.status == QLatin1String( "error" ) )
            errorRow = &row;
    REQUIRE( errorRow != nullptr );
    CHECK( errorRow->studentId == QLatin1String( "s2" ) );
    CHECK_FALSE( errorRow->message.isEmpty() );
}

TEST_CASE( "batch r4: duplicate submissions of one student stay two deterministic rows",
           "[teaching_r4][batch]" )
{
    // One student, two artifact files — discovery must surface both (the
    // teacher sees every artifact), ordered deterministically by path.
    SubmissionTree tree( { QStringLiteral( "s1" ) } );
    QDir sd( tree.submissions.filePath( QStringLiteral( "s1" ) ) );
    QFile second( sd.filePath( QStringLiteral( "artifact_extra.tif" ) ) );
    REQUIRE( second.open( QIODevice::WriteOnly ) );
    second.write( "tif-bytes-2" );

    const BatchAssessmentReport report = runBatchAssessment(
      baseConfig( tree.submissions.path(), QString() ),
      []( const SubmissionItem &item ) { return passingRow( item ); } );

    REQUIRE( report.rows.size() == 2 );
    CHECK( report.rows[0].studentId == QLatin1String( "s1" ) );
    CHECK( report.rows[1].studentId == QLatin1String( "s1" ) );
    CHECK( report.rows[0].artifactPath < report.rows[1].artifactPath );
    CHECK( regradeTraceability( report ).value( "row_count" ).toInt() == 2 );
}

TEST_CASE( "batch r4: corrupt and foreign checkpoints degrade to a fresh start",
           "[teaching_r4][batch]" )
{
    SubmissionTree tree( { QStringLiteral( "s1" ), QStringLiteral( "s2" ) } );
    const QString ckpt = tree.checkpointPath();

    {
        QFile f( ckpt );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "not-json{{{" );
    }
    BatchAssessmentConfig cfg = baseConfig( tree.submissions.path(), ckpt );
    const BatchAssessmentReport corrupt =
      runBatchAssessment( cfg, []( const SubmissionItem &item ) { return passingRow( item ); } );
    CHECK( corrupt.resumed == 0 );
    CHECK( corrupt.graded == 2 );

    // A schema-shaped document whose config digest belongs to a DIFFERENT
    // run must not be adopted either (never grade the wrong class's rows).
    QJsonObject foreign = readJson( ckpt );
    foreign["config_digest"] = QStringLiteral( "deadbeef" );
    QFile f( ckpt );
    REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    f.write( QJsonDocument( foreign ).toJson() );
    const BatchAssessmentReport foreignRun =
      runBatchAssessment( cfg, []( const SubmissionItem &item ) { return passingRow( item ); } );
    CHECK( foreignRun.resumed == 0 );
    CHECK( foreignRun.graded == 2 );
}

TEST_CASE( "batch r4: cancellation checkpoint keeps completed rows only; restart resumes",
           "[teaching_r4][batch]" )
{
    SubmissionTree tree(
      { QStringLiteral( "s1" ), QStringLiteral( "s2" ), QStringLiteral( "s3" ),
        QStringLiteral( "s4" ), QStringLiteral( "s5" ) } );
    const QString ckpt = tree.checkpointPath();
    BatchAssessmentConfig cfg = baseConfig( tree.submissions.path(), ckpt );

    // Phase A: cancel after the first two rows completed (single worker ⇒
    // deterministic). Cancelled items get typed rows and are NOT written to
    // the checkpoint — a restart must re-grade them, not disguise them.
    std::atomic<bool> cancel{ false };
    BatchAssessmentReport phaseA = runBatchAssessment(
      cfg,
      []( const SubmissionItem &item ) { return passingRow( item ); },
      &cancel,
      [&cancel]( int done, int ) {
          if ( done == 2 )
              cancel.store( true );
      } );
    CHECK( phaseA.cancelledEarly );
    CHECK( phaseA.cancelled == 3 );
    CHECK( phaseA.resumed == 0 );

    const QJsonObject ckptDoc = readJson( ckpt );
    const QJsonArray durable = ckptDoc.value( "rows" ).toArray();
    CHECK( durable.size() == 2 );
    for ( const auto &row : durable )
        CHECK( row.toObject().value( "status" ).toString() != QLatin1String( "cancelled" ) );

    // Phase B: restart over the same config adopts exactly the durable rows
    // and re-grades the rest to a full, complete report.
    BatchAssessmentReport phaseB =
      runBatchAssessment( cfg, []( const SubmissionItem &item ) { return passingRow( item ); } );
    CHECK( phaseB.resumed == 2 );
    CHECK( phaseB.cancelled == 0 );
    REQUIRE( phaseB.rows.size() == 5 );
    CHECK( phaseB.graded == 5 );
}

TEST_CASE( "batch r4: atomic publish fails typed on unwritable prefix, writes BOM CSV on success",
           "[teaching_r4][batch]" )
{
    SubmissionTree tree( { QStringLiteral( "s1" ) } );
    const BatchAssessmentReport report =
      runBatchAssessment( baseConfig( tree.submissions.path(), QString() ),
                          []( const SubmissionItem &item ) { return passingRow( item ); } );

    QTemporaryDir outDir;
    const QString unwritable = outDir.filePath( QStringLiteral( "no/such/dir/prefix" ) );
    CHECK_FALSE( publishBatchOutputsAtomic( report, unwritable ) );

    const QString prefix = outDir.filePath( QStringLiteral( "run/prefix" ) );
    outDir.filePath( QStringLiteral( "run" ) );
    QDir().mkpath( outDir.filePath( QStringLiteral( "run" ) ) );
    REQUIRE( publishBatchOutputsAtomic( report, prefix ) );

    QFile csv( prefix + QStringLiteral( ".csv" ) );
    REQUIRE( csv.open( QIODevice::ReadOnly ) );
    const QByteArray bytes = csv.readAll();
    REQUIRE( bytes.size() >= 3 );
    CHECK( static_cast<unsigned char>( bytes[0] ) == 0xEF );
    CHECK( static_cast<unsigned char>( bytes[1] ) == 0xBB );
    CHECK( static_cast<unsigned char>( bytes[2] ) == 0xBF );
    CHECK( bytes.contains( "student_id" ) );

    const QJsonObject doc = readJson( prefix + QStringLiteral( ".json" ) );
    CHECK( doc.value( "schema" ).toString() == QLatin1String( "sicnu.teaching.batch_orchestration/1" ) );
}

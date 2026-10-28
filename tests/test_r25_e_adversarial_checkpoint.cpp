/***************************************************************************
  tests/test_r25_e_adversarial_checkpoint.cpp — R25-E (W7): the batch
  authority's durable state under hostile input (fail-closed matrix).

  The checkpoint and the published report are FILES beside the submissions:
  a student, a stale run or a sync accident can hand the teacher's console
  anything. Every class below must degrade to a typed outcome — a fresh
  start, a typed load refusal — never a crash, never a fabricated grade:

    A1  deep-nested checkpoint JSON (stack bomb) → fresh start, rows graded
    A2  deep-nested published report JSON → typed load refusal
    A3  tampered row statuses (foreign vocabulary) → not adopted
    A4  checkpoint rows for students the class does not have → not adopted
    A5  oversize checkpoint (> 64 MiB bound) → fresh start
    A6  checksum-shaped garbage (truncated JSON) → fresh start / typed error
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <QString>

using namespace sicnu::teaching_admin;

namespace
{

struct OneStudentClass
{
    QTemporaryDir dir;
    QDir submissions;

    OneStudentClass()
    {
        submissions = dir.path() + QStringLiteral( "/class" );
        submissions.mkpath( QStringLiteral( "." ) );
        QDir sd( submissions.filePath( QStringLiteral( "alice" ) ) );
        sd.mkpath( QStringLiteral( "." ) );
        QFile f( sd.filePath( QStringLiteral( "ndvi.tif" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "adversarial-payload" );
    }

    QString checkpointPath() const { return dir.path() + QStringLiteral( "/grades.ckpt.json" ); }
};

BatchAssessmentConfig config( const OneStudentClass &cls )
{
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab25e_adv" );
    cfg.submissionsDir = cls.submissions.path();
    cfg.outDir = cls.dir.path();
    cfg.rubricVersion = QStringLiteral( "r1" );
    cfg.labVersion = QStringLiteral( "v1" );
    cfg.softwareVersion = QStringLiteral( "test" );
    cfg.maxConcurrency = 1;
    cfg.maxSubmissions = -1;
    cfg.checkpointPath = cls.checkpointPath();
    return cfg;
}

BatchRowResult passingRow( const SubmissionItem &item )
{
    BatchRowResult row;
    row.studentId = item.studentId;
    row.artifactPath = item.path;
    row.status = QStringLiteral( "pass" );
    row.verdict = QStringLiteral( "pass" );
    row.score = 90.0;
    return row;
}

void writeFile( const QString &path, const QByteArray &bytes )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( bytes );
}

/// Nested JSON ~1200 levels deep: the real checkpoint schema member wrapped
/// in that many objects. Enough to exceed any recursive parser's appetite.
QByteArray deepJson( int depth )
{
    QByteArray bytes;
    for ( int i = 0; i < depth; ++i )
        bytes += "{\"a\":";
    bytes += "{\"schema\":\"sicnu.teaching.batch_checkpoint/1\"}";
    for ( int i = 0; i < depth; ++i )
        bytes += "}";
    return bytes;
}

} // namespace

TEST_CASE( "A1: deep-nested checkpoint degrades to a fresh start",
           "[r25_e][adversarial][checkpoint]" )
{
    OneStudentClass cls;
    writeFile( cls.checkpointPath(), deepJson( 1200 ) );

    const auto report =
      runBatchAssessment( config( cls ), []( const SubmissionItem &item ) { return passingRow( item ); } );
    // No crash, no adoption of anything from the bomb: the class is graded
    // from scratch and the result is the honest one-student run.
    REQUIRE( report.total == 1 );
    REQUIRE( report.resumed == 0 );
    REQUIRE( report.graded == 1 );
    REQUIRE( !report.checkpointInvalidated );
}

TEST_CASE( "A2: deep-nested published report refuses to load, typed",
           "[r25_e][adversarial][report]" )
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral( "/grades.json" );
    writeFile( path, deepJson( 1200 ) );

    BatchAssessmentReport report;
    QString error;
    REQUIRE( !loadBatchReportFromFile( path, report, error ) );
    REQUIRE( !error.isEmpty() );
}

TEST_CASE( "A3+A4: tampered checkpoint rows are not adopted",
           "[r25_e][adversarial][checkpoint]" )
{
    OneStudentClass cls;
    const auto grader = []( const SubmissionItem &item ) { return passingRow( item ); };

    // Phase 1: a REAL run writes a valid checkpoint (correct config digest,
    // valid alice row with her artifact stamp).
    {
        const auto first = runBatchAssessment( config( cls ), grader );
        REQUIRE( first.resumed == 0 );
        REQUIRE( first.graded == 1 );
    }

    // Phase 2: tamper the durable state in place — the schema and digest
    // stay exactly right (so the loader reaches the adoption path), but the
    // rows turn hostile: a foreign status vocabulary and a foreign student.
    {
        QFile f( cls.checkpointPath() );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson( f.readAll(), &parseError );
        REQUIRE( parseError.error == QJsonParseError::NoError );
        f.close();

        QJsonObject tampered = doc.object();
        QJsonArray rows;
        {
            QJsonObject foreignStatus = doc.object().value( QStringLiteral( "rows" ) ).toArray().at( 0 ).toObject();
            foreignStatus.insert( QStringLiteral( "status" ), QStringLiteral( "spectacular" ) );
            foreignStatus.insert( QStringLiteral( "score" ), 100.0 );
            rows.append( foreignStatus );

            QJsonObject mallory = foreignStatus;
            mallory.insert( QStringLiteral( "student_id" ), QStringLiteral( "mallory" ) );
            mallory.insert( QStringLiteral( "artifact_path" ),
                            QStringLiteral( "/elsewhere/mallory.tif" ) );
            mallory.insert( QStringLiteral( "artifact_stamp" ), QStringLiteral( "whatever" ) );
            rows.append( mallory );
        }
        tampered.insert( QStringLiteral( "rows" ), rows );
        writeFile( cls.checkpointPath(),
                   QJsonDocument( tampered ).toJson( QJsonDocument::Indented ) );
    }

    // Phase 3: the re-run adopts NOTHING hostile: the foreign-status row
    // fails the vocabulary check and mallory is not in the class. Alice is
    // re-graded fresh.
    const auto report = runBatchAssessment( config( cls ), grader );
    REQUIRE( report.total == 1 );
    REQUIRE( report.resumed == 0 );
    REQUIRE( report.rows.size() == 1 );
    REQUIRE( report.rows.front().studentId == QLatin1String( "alice" ) );
    for ( const auto &row : report.rows )
        REQUIRE( row.status != QLatin1String( "spectacular" ) );
}

TEST_CASE( "A5: oversize checkpoint degrades to a fresh start",
           "[r25_e][adversarial][checkpoint]" )
{
    OneStudentClass cls;
    // 65 MiB of nulls: over the 64 MiB bounded read. Written sparsely-fast
    // via a single pread-style truncate + fixed write.
    QFile f( cls.checkpointPath() );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    const QByteArray chunk( 1024 * 1024, '\0' );
    for ( int i = 0; i < 65; ++i )
        REQUIRE( f.write( chunk ) == chunk.size() );
    f.close();

    const auto report =
      runBatchAssessment( config( cls ), []( const SubmissionItem &item ) { return passingRow( item ); } );
    REQUIRE( report.total == 1 );
    REQUIRE( report.resumed == 0 );
    REQUIRE( report.graded == 1 );
}

TEST_CASE( "A6: truncated garbage checkpoint degrades to a fresh start",
           "[r25_e][adversarial][checkpoint]" )
{
    OneStudentClass cls;
    writeFile( cls.checkpointPath(), QByteArray( "{\"schema\": \"sicnu.batch.checkpoint/1\", \"row" ) );

    const auto report =
      runBatchAssessment( config( cls ), []( const SubmissionItem &item ) { return passingRow( item ); } );
    REQUIRE( report.total == 1 );
    REQUIRE( report.resumed == 0 );
    REQUIRE( report.graded == 1 );
}

/***************************************************************************
  tests/test_r18_e_batch_checkpoint.cpp — R18-E E03 checkpoint lane.

  RED-first pins:

    T302 (D4): the artifact stamp was taken AFTER the grade callable
      returned, so an artifact swapped DURING grading bound the OLD verdict
      to the NEW bytes — a restart then adopted the stale grade over the
      re-submitted content (#1448/#1525's class, reopened through a race).
      A mid-grade swap must degrade the row (typed unavailable, never a
      published grade) and never checkpoint it as final.

    T303 (D3): checkpoint adoption publishes rows the current run did not
      grade. That is the durable-resume contract — but the published row
      must SAY so (`resumed: true`), so a consumer reading the report can
      tell a fresh grade from an adopted one. (Full verdict-provenance
      binding needs a keyed MAC — out of scope, recorded in DECISIONS.)
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace sicnu::teaching_admin;

namespace
{

struct SubmissionTree
{
    QTemporaryDir dir;
    QDir submissions;

    explicit SubmissionTree( const QStringList &students )
    {
        submissions = dir.path() + QStringLiteral( "/submissions" );
        submissions.mkpath( QStringLiteral( "." ) );
        int fileNo = 0;
        for ( const QString &student : students )
        {
            QDir sd( submissions.filePath( student ) );
            sd.mkpath( QStringLiteral( "." ) );
            const QString file = QStringLiteral( "artifact_%1.tif" ).arg( ++fileNo );
            QFile f( sd.filePath( file ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( "original-bytes" );
        }
    }

    QString checkpointPath() const { return dir.path() + QStringLiteral( "/ckpt.json" ); }
};

BatchAssessmentConfig baseConfig( const QString &submissionsDir, const QString &checkpointPath )
{
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab90_r18e" );
    cfg.submissionsDir = submissionsDir;
    cfg.outDir = submissionsDir;
    cfg.rubricVersion = QStringLiteral( "r1" );
    cfg.labVersion = QStringLiteral( "v1" );
    cfg.softwareVersion = QStringLiteral( "test" );
    cfg.maxConcurrency = 1;
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

bool hasRow( const BatchAssessmentReport &report, const QString &studentId )
{
    for ( const auto &row : report.rows )
        if ( row.studentId == studentId )
            return true;
    return false;
}

const BatchRowResult *rowOf( const BatchAssessmentReport &report, const QString &studentId )
{
    for ( const auto &row : report.rows )
        if ( row.studentId == studentId )
            return &row;
    return nullptr;
}

} // namespace

TEST_CASE( "r18e T302: an artifact swapped mid-grade degrades and re-grades on restart",
           "[teaching][batch][checkpoint][r18_e]" )
{
    SubmissionTree tree( { QStringLiteral( "s01" ) } );
    const auto cfg = baseConfig( tree.submissions.path(), tree.checkpointPath() );

    // The grade callable is slow (it is a subprocess in production); the
    // student re-submits while it runs.
    const auto swappingGrade = []( const SubmissionItem &item ) {
        QFile f( item.path );
        REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        f.write( "re-submitted bytes" );
        f.close();
        return passingRow( item );
    };
    const BatchAssessmentReport first = runBatchAssessment( cfg, swappingGrade, nullptr, nullptr );

    // The published row must not carry the stale grade over swapped bytes.
    const BatchRowResult *row = rowOf( first, QStringLiteral( "s01" ) );
    REQUIRE( row != nullptr );
    CHECK( row->status == QStringLiteral( "unavailable" ) );
    CHECK( row->message.contains( QStringLiteral( "changed" ) ) );

    // Restart over the re-submitted bytes: the swapped-mid-grade row must
    // be re-graded, not adopted.
    const BatchAssessmentReport second =
      runBatchAssessment( cfg, []( const SubmissionItem &item ) { return passingRow( item ); },
                          nullptr, nullptr );
    CHECK( second.resumed == 0 );
    const BatchRowResult *fresh = rowOf( second, QStringLiteral( "s01" ) );
    REQUIRE( fresh != nullptr );
    CHECK( fresh->status == QStringLiteral( "pass" ) );
}

TEST_CASE( "r18e T303: adopted checkpoint rows are published as resumed, not as fresh grades",
           "[teaching][batch][checkpoint][r18_e]" )
{
    SubmissionTree tree( { QStringLiteral( "s01" ) } );
    const auto cfg = baseConfig( tree.submissions.path(), tree.checkpointPath() );

    // Phase A: a normal run fills the checkpoint.
    const BatchAssessmentReport phaseA =
      runBatchAssessment( cfg, []( const SubmissionItem &item ) { return passingRow( item ); },
                          nullptr, nullptr );
    REQUIRE( phaseA.resumed == 0 );
    REQUIRE( hasRow( phaseA, QStringLiteral( "s01" ) ) );

    // Phase B: resume adopts the durable row — and the PUBLISHED row must
    // carry the marker so report consumers can tell adoption from grading.
    const BatchAssessmentReport phaseB =
      runBatchAssessment( cfg, []( const SubmissionItem &item ) { return passingRow( item ); },
                          nullptr, nullptr );
    REQUIRE( phaseB.resumed == 1 );

    // The marker lives in the published JSON (additive schema field).
    const QString prefix = tree.submissions.path() + QStringLiteral( "/batch_report" );
    REQUIRE( publishBatchOutputsAtomic( phaseB, prefix ) );
    QFile published( prefix + QStringLiteral( ".json" ) );
    REQUIRE( published.open( QIODevice::ReadOnly ) );
    const QJsonObject doc = QJsonDocument::fromJson( published.readAll() ).object();
    const QJsonArray rows = doc.value( QStringLiteral( "rows" ) ).toArray();
    REQUIRE( rows.size() == 1 );
    CHECK( rows.at( 0 ).toObject().value( QStringLiteral( "resumed" ) ).toBool() == true );
}

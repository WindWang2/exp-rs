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

    // Phase B: resume adopts the durable row — the report-level counter
    // is the published visibility (the row document stays byte-stable so
    // a resumed run publishes identical rows to an uninterrupted one).
    const BatchAssessmentReport phaseB =
      runBatchAssessment( cfg, []( const SubmissionItem &item ) { return passingRow( item ); },
                          nullptr, nullptr );
    REQUIRE( phaseB.resumed == 1 );
    CHECK( phaseB.rows.at( 0 ).resumed );

    // Byte identity: a resumed run and a fresh run over the same final
    // state publish the same row document (the restart-identity oracle).
    const QString prefix = tree.submissions.path() + QStringLiteral( "/batch_report" );
    REQUIRE( publishBatchOutputsAtomic( phaseB, prefix ) );
    QFile published( prefix + QStringLiteral( ".json" ) );
    REQUIRE( published.open( QIODevice::ReadOnly ) );
    const QJsonObject doc = QJsonDocument::fromJson( published.readAll() ).object();
    const QJsonArray rows = doc.value( QStringLiteral( "rows" ) ).toArray();
    REQUIRE( rows.size() == 1 );
    CHECK_FALSE( rows.at( 0 ).toObject().contains( QStringLiteral( "resumed" ) ) );
}

TEST_CASE( "r18e T302b: an artifact that APPEARS during grading is demoted too",
           "[teaching][batch][checkpoint][r18_e]" )
{
    // R1 review P2-1: the demotion used to fire only on a non-empty
    // pre-stamp changing — a file absent at the pre-stamp and present
    // after the call bound a fresh stamp over bytes the grade never saw,
    // and restart adopted it.
    //
    // Construction (single worker, discovery order s00 < s01): s00's grade
    // deletes s01's file, so s01's PRE-stamp is empty; s01's callable then
    // recreates it — post-stamp non-empty over ungraded bytes.
    SubmissionTree tree( { QStringLiteral( "s00" ), QStringLiteral( "s01" ) } );
    const auto cfg = baseConfig( tree.submissions.path(), tree.checkpointPath() );

    const auto appearingGrade = [&]( const SubmissionItem &item ) {
        if ( item.studentId == QStringLiteral( "s00" ) )
        {
            const QDir dir( tree.submissions.path() + QStringLiteral( "/s01" ) );
            const auto entries = dir.entryList( QDir::Files );
            for ( const QString &name : entries )
                QFile::remove( dir.filePath( name ) );
        }
        else
        {
            QFile f( item.path );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( "appeared mid-grade" );
            f.close();
        }
        return passingRow( item );
    };
    const BatchAssessmentReport report = runBatchAssessment( cfg, appearingGrade, nullptr, nullptr );

    const BatchRowResult *row = rowOf( report, QStringLiteral( "s01" ) );
    REQUIRE( row != nullptr );
    CHECK( row->status == QStringLiteral( "unavailable" ) );
    CHECK( row->message.contains( QStringLiteral( "changed" ) ) );

    // Restart: s00's row was legitimately graded over unchanged bytes and
    // resumes (resumed == 1); s01's demoted row carried an EMPTY stamp and
    // must be re-graded — its published row is the fresh pass.
    const BatchAssessmentReport second =
      runBatchAssessment( cfg, []( const SubmissionItem &item ) { return passingRow( item ); },
                          nullptr, nullptr );
    CHECK( second.resumed == 1 );
    const BatchRowResult *freshS00 = rowOf( second, QStringLiteral( "s00" ) );
    REQUIRE( freshS00 != nullptr );
    CHECK( freshS00->resumed );
    const BatchRowResult *freshS01 = rowOf( second, QStringLiteral( "s01" ) );
    REQUIRE( freshS01 != nullptr );
    CHECK( freshS01->status == QStringLiteral( "pass" ) );
    CHECK_FALSE( freshS01->resumed );
}

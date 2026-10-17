/***************************************************************************
  tests/test_teaching_admin_student_id_authority_r16_c_t06.cpp — R16-C/T06
  WP4: batch row identity comes from discovery, never from the callable.

  runBatchAssessment() isolates throwing callables and backfills EMPTY row
  fields, but published a callable-supplied studentId verbatim: a callable
  bug (e.g. an adapter mapping capsule ids, or a merged-duplicate path)
  attributed one student's score to ANOTHER id in the published JSON/CSV —
  exactly the misattribution the counting-truth contract must prevent.
  Discovery owns the (student, artifact) identity; the orchestrator now
  overwrites the row's studentId with the discovered one.

  Determinism: two students, serial worker pool, content-keyed callable.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

using namespace sicnu::teaching_admin;

namespace
{

BatchAssessmentConfig authorityConfig( const QString &submissions )
{
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab15" );
    cfg.submissionsDir = submissions;
    cfg.rubricVersion = QStringLiteral( "r1" );
    cfg.labVersion = QStringLiteral( "l1" );
    cfg.softwareVersion = QStringLiteral( "sw1" );
    cfg.maxConcurrency = 1;
    cfg.maxSubmissions = -1;
    return cfg;
}

} // namespace

TEST_CASE( "batch rows are attributed to the discovered student, never to a callable-supplied id",
           "[teaching_admin][batch][r16_c_t06]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QDir root( dir.path() );
    REQUIRE( root.mkpath( QStringLiteral( "s001" ) ) );
    REQUIRE( root.mkpath( QStringLiteral( "s002" ) ) );
    {
        QFile f( root.filePath( QStringLiteral( "s001/a.tif" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArrayLiteral( "AAA" ) );
    }
    {
        QFile f( root.filePath( QStringLiteral( "s002/a.tif" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArrayLiteral( "BBB" ) );
    }

    // Buggy callable: for the BBB artifact it answers with a FOREIGN
    // student id (the shape of an adapter mapping ids wrong) and an
    // inflated score.
    const auto report = runBatchAssessment(
        authorityConfig( dir.path() ),
        []( const SubmissionItem &item ) {
            QFile f( item.path );
            REQUIRE( f.open( QIODevice::ReadOnly ) );
            BatchRowResult row;
            row.artifactPath = item.path;
            if ( f.readAll() == QByteArrayLiteral( "BBB" ) )
            {
                row.studentId = QStringLiteral( "s999-imposter" );
                row.status = QStringLiteral( "pass" );
                row.verdict = QStringLiteral( "pass" );
                row.score = 100.0;
            }
            else
            {
                row.studentId = item.studentId;
                row.status = QStringLiteral( "pass" );
                row.verdict = QStringLiteral( "pass" );
                row.score = 90.0;
            }
            return row;
        } );

    // Counting truth: two discovered submissions, two graded rows, and
    // every row attributed to a DISCOVERED student id.
    REQUIRE( report.total == 2 );
    REQUIRE( report.rows.size() == 2 );
    REQUIRE( report.graded == 2 );

    QSet<QString> seen;
    for ( const auto &row : report.rows )
    {
        CHECK( ( row.studentId == QStringLiteral( "s001" )
                 || row.studentId == QStringLiteral( "s002" ) ) );
        seen.insert( row.studentId );
    }
    CHECK( seen.size() == 2 );

    // The row for s002's artifact specifically carries s002's id and its
    // own grade — never the imposter's.
    for ( const auto &row : report.rows )
    {
        if ( row.artifactPath.endsWith( QStringLiteral( "s002/a.tif" ) ) )
        {
            CHECK( row.studentId == QStringLiteral( "s002" ) );
            CHECK( row.score == 100.0 ); // the grade itself is kept verbatim
        }
    }

    // The published JSON keeps the same attribution.
    const QJsonObject json = report.toJson();
    const auto rowsArr = json.value( QStringLiteral( "rows" ) ).toArray();
    REQUIRE( rowsArr.size() == 2 );
    for ( const auto &v : rowsArr )
    {
        const QString sid = v.toObject().value( QStringLiteral( "student_id" ) ).toString();
        CHECK( ( sid == QLatin1String( "s001" ) || sid == QLatin1String( "s002" ) ) );
    }
}

TEST_CASE( "an empty discovered id keeps the callable's attribution instead of publishing empty",
           "[teaching_admin][batch][r16_c_t06]" )
{
    // Round-1 review P2: discovery yields an EMPTY student id for a flat
    // root file named exactly "_submission.*" (the suffix strip empties the
    // base name). The unconditional identity overwrite then published an
    // unattributable empty id in the report/CSV. Discovery owns identity
    // when it HAS one; when it does not, the callable's id (if any) is the
    // only attribution available — strictly better than empty.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QDir root( dir.path() );
    {
        QFile f( root.filePath( QStringLiteral( "_submission.tif" ) ) );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArrayLiteral( "AAA" ) );
    }

    const BatchAssessmentConfig cfg = authorityConfig( dir.path() );
    const auto report = runBatchAssessment(
        cfg,
        []( const SubmissionItem &item ) {
            BatchRowResult row;
            row.studentId = QStringLiteral( "callable_attributed" );
            row.labId = QStringLiteral( "lab15" );
            row.artifactPath = item.path;
            row.status = QStringLiteral( "pass" );
            row.verdict = QStringLiteral( "pass" );
            row.score = 70.0;
            return row;
        } );

    REQUIRE( report.rows.size() == 1 );
    CHECK( report.rows.first().studentId == QLatin1String( "callable_attributed" ) );
    CHECK( report.graded == 1 );
}

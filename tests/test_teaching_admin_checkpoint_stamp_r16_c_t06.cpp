/***************************************************************************
  tests/test_teaching_admin_checkpoint_stamp_r16_c_t06.cpp — R16-C/T06 WP1:
  checkpoint artifact identity must fingerprint content, not stat metadata.

  artifactStamp() (batch_assessment.cpp) was mtime+size only, so a content
  swap that preserves both — cp -p with a backdated mtime, or a same-length
  rewrite with the mtime restored — made the resume path adopt the STALE
  durable row over the re-submitted artifact: the #1448 re-grade gate never
  fired and the class report published the old grade for new bytes.

  Determinism: the swap restores the EXACT captured mtime via
  QFile::setFileTime and keeps the byte length identical, so the stat proxy
  is bit-identical; only content differs. Worker pool is serial so adoption
  is deterministic.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace sicnu::teaching_admin;

namespace
{

/// s001..s00N one-level student dirs, each with a 4-byte a.tif ("GOOD").
struct ClassTree
{
    QTemporaryDir dir;
    QString submissions;

    explicit ClassTree( int count )
    {
        submissions = QDir( dir.path() ).filePath( QStringLiteral( "submissions" ) );
        QDir root( submissions );
        for ( int i = 1; i <= count; ++i )
        {
            const QString sid = QStringLiteral( "s%1" ).arg( i, 3, 10, QLatin1Char( '0' ) );
            REQUIRE( root.mkpath( sid ) );
            QFile f( root.filePath( sid + QStringLiteral( "/a.tif" ) ) );
            REQUIRE( f.open( QIODevice::WriteOnly ) );
            f.write( QByteArrayLiteral( "GOOD" ) );
        }
    }

    QString artifact( const QString &sid ) const
    {
        return QDir( submissions ).filePath( sid + QStringLiteral( "/a.tif" ) );
    }

    QString checkpointPath() const { return QDir( dir.path() ).filePath( QStringLiteral( "cp.json" ) ); }
};

BatchAssessmentConfig stampConfig( const QString &submissions, const QString &checkpoint )
{
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab15" );
    cfg.submissionsDir = submissions;
    cfg.rubricVersion = QStringLiteral( "r1" );
    cfg.labVersion = QStringLiteral( "l1" );
    cfg.softwareVersion = QStringLiteral( "sw1" );
    cfg.maxConcurrency = 1;
    cfg.maxSubmissions = -1;
    cfg.checkpointPath = checkpoint;
    return cfg;
}

/// Content-keyed grader: GOOD → pass 90, anything else → fail 10.
BatchRowResult gradeByContent( const SubmissionItem &item, const BatchAssessmentConfig &cfg )
{
    BatchRowResult row;
    row.studentId = item.studentId;
    row.labId = cfg.labId;
    row.artifactPath = item.path;
    QFile f( item.path );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    if ( f.readAll() == QByteArrayLiteral( "GOOD" ) )
    {
        row.status = QStringLiteral( "pass" );
        row.verdict = QStringLiteral( "pass" );
        row.score = 90.0;
    }
    else
    {
        row.status = QStringLiteral( "fail" );
        row.verdict = QStringLiteral( "fail" );
        row.score = 10.0;
        row.message = QStringLiteral( "swapped content graded" );
    }
    return row;
}

} // namespace

TEST_CASE( "checkpoint adoption survives an untouched restart (stamp stability)",
           "[teaching_admin][batch][checkpoint][r16_c_t06]" )
{
    ClassTree tree( 3 );
    const BatchAssessmentConfig cfg = stampConfig( tree.submissions, tree.checkpointPath() );

    const auto first = runBatchAssessment(
        cfg, [&]( const SubmissionItem &item ) { return gradeByContent( item, cfg ); } );
    REQUIRE( first.graded == 3 );
    REQUIRE( first.resumed == 0 );

    // No change at all: the fast path must still adopt every durable row.
    int regrades = 0;
    const auto second = runBatchAssessment(
        cfg,
        [&]( const SubmissionItem &item ) {
            ++regrades;
            return gradeByContent( item, cfg );
        } );
    CHECK( second.resumed == 3 );
    CHECK( regrades == 0 );
    CHECK( second.graded == 3 );
}

TEST_CASE( "checkpoint stamp rejects a same-stat content swap",
           "[teaching_admin][batch][checkpoint][r16_c_t06]" )
{
    ClassTree tree( 3 );
    const BatchAssessmentConfig cfg = stampConfig( tree.submissions, tree.checkpointPath() );
    const QString sid = QStringLiteral( "s002" );
    const QString artifact = tree.artifact( sid );

    const auto first = runBatchAssessment(
        cfg, [&]( const SubmissionItem &item ) { return gradeByContent( item, cfg ); } );
    REQUIRE( first.graded == 3 );

    // Same-length content swap, then restore the exact mtime: stat metadata
    // (mtime + size) becomes indistinguishable from the graded original.
    const QDateTime mtime = QFileInfo( artifact ).lastModified();
    {
        QFile f( artifact );
        REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        f.write( QByteArrayLiteral( "EVIL" ) ); // same 4 bytes
    }
    {
        QFile f( artifact );
        REQUIRE( f.open( QIODevice::ReadWrite ) );
        REQUIRE( f.setFileTime( mtime, QFileDevice::FileModificationTime ) );
    }
    const QFileInfo after( artifact );
    REQUIRE( after.size() == 4 );
    REQUIRE( after.lastModified().toMSecsSinceEpoch() == mtime.toMSecsSinceEpoch() );

    // Resume over the swapped artifact: s002 must be RE-GRADED (the durable
    // row is stale), not adopted.
    int regrades = 0;
    const auto second = runBatchAssessment(
        cfg,
        [&]( const SubmissionItem &item ) {
            ++regrades;
            return gradeByContent( item, cfg );
        } );
    CHECK( second.resumed == 2 );
    CHECK( regrades == 1 );

    // The published s002 row describes the NEW bytes, never the stale grade.
    bool sawSwapped = false;
    for ( const auto &row : second.rows )
    {
        if ( row.artifactPath != artifact )
            continue;
        sawSwapped = true;
        CHECK( row.studentId == sid );
        CHECK( row.status == QLatin1String( "fail" ) );
        CHECK( row.score == 10.0 );
    }
    REQUIRE( sawSwapped );

    // The rewritten checkpoint still carries an opaque, non-empty stamp for
    // every row (adoption contract of #1448).
    {
        QFile cp( tree.checkpointPath() );
        REQUIRE( cp.open( QIODevice::ReadOnly ) );
        const auto rows = QJsonDocument::fromJson( cp.readAll() )
                            .object()
                            .value( QStringLiteral( "rows" ) )
                            .toArray();
        REQUIRE( rows.size() == 3 );
        for ( const auto &v : rows )
            REQUIRE_FALSE(
              v.toObject().value( QStringLiteral( "artifact_stamp" ) ).toString().isEmpty() );
    }
}

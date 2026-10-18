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

TEST_CASE( "checkpoint stamp rejects a tail-only content swap beyond the head window",
           "[teaching_admin][batch][checkpoint][r16_c_t06]" )
{
    // Round-1 review P1: hashing only the artifact's HEAD lets a same-length,
    // same-mtime rewrite that touches ONLY bytes past the head window adopt
    // the stale durable row — realistic for classroom rasters, which are
    // typically far larger than the 64 KiB window with informative pixels
    // throughout the file. The stamp must fingerprint the tail as well.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString submissions = QDir( dir.path() ).filePath( QStringLiteral( "submissions" ) );
    const QString sid = QStringLiteral( "s001" );
    REQUIRE( QDir( submissions ).mkpath( sid ) );
    const QString artifact =
      QDir( submissions ).filePath( sid + QStringLiteral( "/a.tif" ) );

    // 200 KiB artifact: head window (64 KiB) all 'G', everything past it 'X'.
    constexpr qint64 kTotal = 200 * 1024;
    constexpr qint64 kTamperOffset = 150 * 1024;
    {
        QFile f( artifact );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( 64 * 1024, 'G' ) );
        f.write( QByteArray( kTotal - 64 * 1024, 'X' ) );
        REQUIRE( f.size() == kTotal );
    }

    // Tail-keyed grader: probe byte inside the tail window.
    auto tailGrade = []( const SubmissionItem &item ) {
        BatchRowResult row;
        row.studentId = item.studentId;
        row.labId = QStringLiteral( "lab15" );
        row.artifactPath = item.path;
        QFile f( item.path );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        REQUIRE( f.seek( kTamperOffset ) );
        const char probe = f.read( 1 ).at( 0 );
        if ( probe == 'X' )
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
            row.message = QStringLiteral( "tail swapped content graded" );
        }
        return row;
    };

    const QString checkpoint = QDir( dir.path() ).filePath( QStringLiteral( "cp.json" ) );
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab15" );
    cfg.submissionsDir = submissions;
    cfg.rubricVersion = QStringLiteral( "r1" );
    cfg.labVersion = QStringLiteral( "l1" );
    cfg.softwareVersion = QStringLiteral( "sw1" );
    cfg.maxConcurrency = 1;
    cfg.maxSubmissions = -1;
    cfg.checkpointPath = checkpoint;

    const auto first = runBatchAssessment( cfg, tailGrade );
    REQUIRE( first.graded == 1 );
    REQUIRE( first.rows.first().score == 90.0 );

    // Flip ONE tail byte (past the 64 KiB head window), keep the length,
    // restore the exact mtime: stat + head fingerprint both unchanged.
    const QDateTime mtime = QFileInfo( artifact ).lastModified();
    {
        QFile f( artifact );
        REQUIRE( f.open( QIODevice::ReadWrite ) );
        REQUIRE( f.seek( kTamperOffset ) );
        REQUIRE( f.write( QByteArrayLiteral( "T" ) ) == 1 );
        f.flush();
    }
    {
        QFile f( artifact );
        REQUIRE( f.open( QIODevice::ReadWrite ) );
        REQUIRE( f.setFileTime( mtime, QFileDevice::FileModificationTime ) );
    }
    const QFileInfo after( artifact );
    REQUIRE( after.size() == kTotal );
    REQUIRE( after.lastModified().toMSecsSinceEpoch() == mtime.toMSecsSinceEpoch() );

    int regrades = 0;
    const auto second = runBatchAssessment(
        cfg,
        [&]( const SubmissionItem &item ) {
            ++regrades;
            return tailGrade( item );
        } );
    CHECK( second.resumed == 0 );
    CHECK( regrades == 1 );

    // The published row describes the tampered tail, never the stale grade.
    REQUIRE( second.rows.size() == 1 );
    CHECK( second.rows.first().status == QLatin1String( "fail" ) );
    CHECK( second.rows.first().score == 10.0 );
}

TEST_CASE( "checkpoint stamp rejects a middle-only content swap (fixed-stride interior sampling)",
           "[teaching_admin][batch][checkpoint][r16_c_t06][r17]" )
{
    // #1520: the head+tail windows leave the INTERIOR of a >2x-window
    // artifact unfingerprinted. 256 KiB is the worst case — the exact
    // midpoint (128 KiB) sits outside both the head window [0,64 KiB) and
    // the tail window [192 KiB,256 KiB), so a same-stat, same-length flip
    // there was adopted as the graded original. The stamp must sample the
    // interior at a fixed stride so the flip is caught.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString submissions = QDir( dir.path() ).filePath( QStringLiteral( "submissions" ) );
    const QString sid = QStringLiteral( "s001" );
    REQUIRE( QDir( submissions ).mkpath( sid ) );
    const QString artifact =
      QDir( submissions ).filePath( sid + QStringLiteral( "/a.tif" ) );

    constexpr qint64 kTotal = 256 * 1024;
    constexpr qint64 kMidpoint = 128 * 1024; // strictly between both end windows
    {
        QFile f( artifact );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( kTotal, 'G' ) );
        REQUIRE( f.size() == kTotal );
    }

    // Midpoint-keyed grader: probe the byte the tamper is going to touch.
    auto midGrade = [kMidpoint]( const SubmissionItem &item ) {
        BatchRowResult row;
        row.studentId = item.studentId;
        row.labId = QStringLiteral( "lab15" );
        row.artifactPath = item.path;
        QFile f( item.path );
        REQUIRE( f.open( QIODevice::ReadOnly ) );
        REQUIRE( f.seek( kMidpoint ) );
        const char probe = f.read( 1 ).at( 0 );
        if ( probe == 'G' )
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
            row.message = QStringLiteral( "middle swapped content graded" );
        }
        return row;
    };

    const QString checkpoint = QDir( dir.path() ).filePath( QStringLiteral( "cp.json" ) );
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab15" );
    cfg.submissionsDir = submissions;
    cfg.rubricVersion = QStringLiteral( "r1" );
    cfg.labVersion = QStringLiteral( "l1" );
    cfg.softwareVersion = QStringLiteral( "sw1" );
    cfg.maxConcurrency = 1;
    cfg.maxSubmissions = -1;
    cfg.checkpointPath = checkpoint;

    const auto first = runBatchAssessment( cfg, midGrade );
    REQUIRE( first.graded == 1 );
    REQUIRE( first.rows.first().score == 90.0 );

    // Flip ONE middle byte, keep the length, restore the exact mtime: the
    // stat proxy AND the head/tail fingerprint all stay unchanged.
    const QDateTime mtime = QFileInfo( artifact ).lastModified();
    {
        QFile f( artifact );
        REQUIRE( f.open( QIODevice::ReadWrite ) );
        REQUIRE( f.seek( kMidpoint ) );
        REQUIRE( f.write( QByteArrayLiteral( "M" ) ) == 1 );
        f.flush();
    }
    {
        QFile f( artifact );
        REQUIRE( f.open( QIODevice::ReadWrite ) );
        REQUIRE( f.setFileTime( mtime, QFileDevice::FileModificationTime ) );
    }
    const QFileInfo after( artifact );
    REQUIRE( after.size() == kTotal );
    REQUIRE( after.lastModified().toMSecsSinceEpoch() == mtime.toMSecsSinceEpoch() );

    int regrades = 0;
    const auto second = runBatchAssessment(
        cfg,
        [&]( const SubmissionItem &item ) {
            ++regrades;
            return midGrade( item );
        } );
    CHECK( second.resumed == 0 );
    CHECK( regrades == 1 );

    // The published row describes the tampered interior, never the stale grade.
    REQUIRE( second.rows.size() == 1 );
    CHECK( second.rows.first().status == QLatin1String( "fail" ) );
    CHECK( second.rows.first().score == 10.0 );
}

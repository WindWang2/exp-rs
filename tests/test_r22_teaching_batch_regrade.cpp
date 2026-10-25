/***************************************************************************
  tests/test_r22_teaching_batch_regrade.cpp
  R22-06 / issue #1694: a durable batch checkpoint may only be adopted when
  the grading AUTHORITY that produced the completed rows is the authority of
  the resuming run. Version strings alone ("ui-1") say nothing about the
  rules/rubric CONTENT or the grader CLI binary; editing a rules file in
  place (same path, same lab id) used to be invisible to the digest, so a
  re-run silently published stale grades (resumed rows).

  RED phase: the rules-edit test below compiles against the CURRENT config
  API and fails on master — resumed == 2 with old-rule scores.
  GREEN phase extends this file with the fingerprint helpers and the
  grader-identity / version-label oracles.
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace sicnu::teaching_admin;

namespace
{

void writeBytes( const QString &path, const QByteArray &bytes )
{
    REQUIRE( QDir( QFileInfo( path ).absolutePath() ).mkpath( QStringLiteral( "." ) ) );
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( f.write( bytes ) == bytes.size() );
    f.close();
}

/// A grade callable with the shape of the real authority: it READS the
/// rules file, so its score encodes the CURRENT rules content. An adopted
/// stale row is then detectable as a wrong score, exactly like a real
/// re-grade under new thresholds would be.
GradeCallable rulesReadingGrader( const QString &rulesPath )
{
    return [rulesPath]( const SubmissionItem &item ) {
        BatchRowResult row;
        row.studentId = item.studentId;
        row.artifactPath = item.path;
        row.status = QStringLiteral( "pass" );
        row.verdict = QStringLiteral( "pass" );
        QFile f( rulesPath );
        const bool currentRules =
          f.open( QIODevice::ReadOnly ) && f.readAll() == QByteArrayLiteral( "R2" );
        row.score = currentRules ? 2.0 : 1.0;
        row.message = QStringLiteral( "graded against current rules content" );
        return row;
    };
}

struct ClassFixture
{
    QTemporaryDir dir;
    QString submissionsDir;
    QString outPrefix;
    QString rulesPath;

    ClassFixture()
    {
        submissionsDir = QDir( dir.path() ).filePath( QStringLiteral( "class" ) );
        for ( const char *sid : { "alice", "bob" } )
        {
            const QString sd = QDir( submissionsDir ).filePath( QLatin1String( sid ) );
            REQUIRE( QDir().mkpath( sd ) );
            writeBytes( QDir( sd ).filePath( QStringLiteral( "result.tif" ) ),
                        QByteArrayLiteral( "raster-bytes-" ) + QByteArray( sid ) );
        }
        outPrefix = QDir( dir.path() ).filePath( QStringLiteral( "out" ) );
        rulesPath = QDir( dir.path() ).filePath( QStringLiteral( "lab99.rules.json" ) );
        writeBytes( rulesPath, QByteArrayLiteral( "R1" ) );
    }
};

BatchAssessmentConfig makeConfig( const ClassFixture &fx )
{
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab99" );
    cfg.submissionsDir = fx.submissionsDir;
    cfg.outDir = fx.dir.path();
    // Deliberately CONSTANT across both runs: the point of #1694 is that
    // version STRINGS do not capture a rules-content edit.
    cfg.rubricVersion = QStringLiteral( "rubric-A" );
    cfg.labVersion = QStringLiteral( "lab-A" );
    cfg.softwareVersion = QStringLiteral( "sw-A" );
    cfg.maxConcurrency = 1;
    cfg.checkpointPath = fx.outPrefix + QStringLiteral( ".checkpoint.json" );
    return cfg;
}

} // namespace

TEST_CASE( "#1694: editing the rules file in place must invalidate the checkpoint",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    ClassFixture fx;
    const BatchAssessmentConfig cfg = makeConfig( fx );

    const auto report1 = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( report1.total == 2 );
    REQUIRE( report1.graded == 2 );
    REQUIRE( report1.resumed == 0 );
    REQUIRE( report1.rows.at( 0 ).score == 1.0 );
    REQUIRE( report1.rows.at( 1 ).score == 1.0 );

    // The teacher fixes the rubric/rules CONTENT — same path, same lab id,
    // same declared version strings, untouched submissions.
    writeBytes( fx.rulesPath, QByteArrayLiteral( "R2" ) );

    const auto report2 = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    // Master adopts both stale rows and publishes R1 scores (the bug).
    REQUIRE( report2.resumed == 0 );
    REQUIRE( report2.graded == 2 );
    REQUIRE( report2.rows.at( 0 ).score == 2.0 );
    REQUIRE( report2.rows.at( 1 ).score == 2.0 );
}

TEST_CASE( "#1694: an unchanged grading setup still resumes from the checkpoint",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    // Guard rail for the fix: invalidation must key on AUTHORITY changes,
    // not on every re-run — durability (the point of the checkpoint) has to
    // survive an unchanged rules file.
    ClassFixture fx;
    const BatchAssessmentConfig cfg = makeConfig( fx );

    const auto report1 = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( report1.graded == 2 );

    const auto report2 = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( report2.resumed == 2 );
    // Published-row semantics: adopted rows keep status "pass", so `graded`
    // counts them (the counters describe the published rows) — the resume
    // oracle is the resumed counter plus the ADOPTED scores.
    REQUIRE( report2.rows.at( 0 ).score == 1.0 );
    REQUIRE( report2.rows.at( 1 ).score == 1.0 );
}

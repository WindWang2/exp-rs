/***************************************************************************
  tests/test_r22_teaching_batch_regrade.cpp
  R22-06 / issue #1694: a durable batch checkpoint may only be adopted when
  the grading AUTHORITY that produced the completed rows is the authority of
  the resuming run. Version strings alone ("ui-1") say nothing about the
  rules/rubric CONTENT or the grader CLI binary; editing a rules file in
  place (same path, same lab id) used to be invisible to the digest, so a
  re-run silently published stale grades (resumed rows).

  RED evidence (pre-fix, commit 1610c9b75): the rules-edit case failed with
  resumed == 2. GREEN: the config digest covers the rules-content
  fingerprint and the grader identity; the report types the invalidation.
***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/grader_cli_adapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
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

BatchAssessmentConfig makeConfig( const ClassFixture &fx, const QString &graderIdentity )
{
    BatchAssessmentConfig cfg;
    cfg.labId = QStringLiteral( "lab99" );
    cfg.submissionsDir = fx.submissionsDir;
    cfg.outDir = fx.dir.path();
    // Deliberately CONSTANT across both runs: the point of #1694 is that
    // version STRINGS do not capture a rules-content edit — the digest
    // additionally carries the rules fingerprint and grader identity.
    cfg.rubricVersion = QStringLiteral( "rubric-A" );
    cfg.labVersion = QStringLiteral( "lab-A" );
    cfg.softwareVersion = QStringLiteral( "sw-A" );
    cfg.maxConcurrency = 1;
    cfg.checkpointPath = fx.outPrefix + QStringLiteral( ".checkpoint.json" );
    GraderCliConfig grader;
    grader.labIdOrRulesPath = fx.rulesPath;
    cfg.gradingRulesFingerprint = gradingRulesFingerprint( grader );
    cfg.graderIdentity = graderIdentity;
    return cfg;
}

} // namespace

TEST_CASE( "#1694: editing the rules file in place must invalidate the checkpoint",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    ClassFixture fx;
    const BatchAssessmentConfig cfg = makeConfig( fx, QStringLiteral( "grader-test-1" ) );

    const auto report1 = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( report1.total == 2 );
    REQUIRE( report1.graded == 2 );
    REQUIRE( report1.resumed == 0 );
    REQUIRE_FALSE( report1.checkpointInvalidated );
    REQUIRE( report1.rows.at( 0 ).score == 1.0 );
    REQUIRE( report1.rows.at( 1 ).score == 1.0 );

    // The teacher fixes the rubric/rules CONTENT — same path, same lab id,
    // same declared version strings, untouched submissions.
    writeBytes( fx.rulesPath, QByteArrayLiteral( "R2" ) );

    // The config is rebuilt the way the console builds it: the fingerprint
    // helper reads the CURRENT rules content.
    const BatchAssessmentConfig cfg2 = makeConfig( fx, QStringLiteral( "grader-test-1" ) );
    REQUIRE( cfg2.gradingRulesFingerprint != cfg.gradingRulesFingerprint );

    const auto report2 = runBatchAssessment( cfg2, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( report2.resumed == 0 );
    REQUIRE( report2.rows.at( 0 ).score == 2.0 );
    REQUIRE( report2.rows.at( 1 ).score == 2.0 );
    // The invalidation is published, not silent: the console says why the
    // run started fresh.
    REQUIRE( report2.checkpointInvalidated );
    REQUIRE( report2.toJson().value( QStringLiteral( "checkpoint_invalidated" ) ).toBool() );
}

TEST_CASE( "#1694: an unchanged grading setup still resumes from the checkpoint",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    // Guard rail for the fix: invalidation must key on AUTHORITY changes,
    // not on every re-run — durability (the point of the checkpoint) has to
    // survive an unchanged rules file.
    ClassFixture fx;
    const BatchAssessmentConfig cfg = makeConfig( fx, QStringLiteral( "grader-test-1" ) );

    const auto report1 = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( report1.graded == 2 );

    const BatchAssessmentConfig cfg2 = makeConfig( fx, QStringLiteral( "grader-test-1" ) );
    REQUIRE( cfg2.gradingRulesFingerprint == cfg.gradingRulesFingerprint );
    const auto report2 = runBatchAssessment( cfg2, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( report2.resumed == 2 );
    REQUIRE_FALSE( report2.checkpointInvalidated );
    // Published-row semantics: adopted rows keep status "pass", so `graded`
    // counts them (the counters describe the published rows) — the resume
    // oracle is the resumed counter plus the ADOPTED scores.
    REQUIRE( report2.rows.at( 0 ).score == 1.0 );
    REQUIRE( report2.rows.at( 1 ).score == 1.0 );
}

TEST_CASE( "#1694: swapping the grader binary invalidates the checkpoint",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    // Same rules content, same submissions — only the grader identity
    // changes (rebuilt CLI, different binary). Old grades must not carry
    // over: they were produced by a different authority.
    ClassFixture fx;
    const BatchAssessmentConfig cfg = makeConfig( fx, QStringLiteral( "grader-test-1" ) );
    const auto report1 = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( report1.graded == 2 );

    const BatchAssessmentConfig cfg2 = makeConfig( fx, QStringLiteral( "grader-test-2" ) );
    const auto report2 = runBatchAssessment( cfg2, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( report2.resumed == 0 );
    REQUIRE( report2.checkpointInvalidated );
    // Re-graded under the SAME rules: scores identical, provenance new.
    REQUIRE( report2.rows.at( 0 ).score == 1.0 );
}

TEST_CASE( "#1694: gradingRulesFingerprint is content-sensitive and touch-stable",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    QTemporaryDir dir;
    const QString rules = QDir( dir.path() ).filePath( QStringLiteral( "lab.rules.json" ) );
    writeBytes( rules, QByteArrayLiteral( "{\"threshold\":0.5}" ) );

    GraderCliConfig cfg;
    cfg.labIdOrRulesPath = rules;
    const QString fp1 = gradingRulesFingerprint( cfg );
    REQUIRE( fp1.startsWith( QLatin1String( "sha256:" ) ) );

    // Same bytes, new mtime: fingerprint unchanged (a touch is not an edit).
    QFile f( rules );
    REQUIRE( f.open( QIODevice::ReadWrite ) );
    const QByteArray same = f.readAll();
    f.seek( 0 );
    f.write( same );
    f.close();
    REQUIRE( gradingRulesFingerprint( cfg ) == fp1 );

    // Content edit under the same path: fingerprint changes.
    writeBytes( rules, QByteArrayLiteral( "{\"threshold\":0.7}" ) );
    REQUIRE( gradingRulesFingerprint( cfg ) != fp1 );

    // Non-file (a lab id the CLI resolves internally): the stable labid
    // form, distinct from every content fingerprint.
    cfg.labIdOrRulesPath = QStringLiteral( "lab90_runtime_probe" );
    const QString labidForm = gradingRulesFingerprint( cfg );
    REQUIRE( labidForm == QLatin1String( "labid:lab90_runtime_probe" ) );
    REQUIRE( labidForm != fp1 );

    // Empty authority: explicit none form.
    cfg.labIdOrRulesPath = QString();
    REQUIRE( gradingRulesFingerprint( cfg ) == QLatin1String( "none" ) );
}

TEST_CASE( "#1694: graderCliIdentity fingerprints the authority binary",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    QTemporaryDir dir;
    const QString cli = QDir( dir.path() ).filePath( QStringLiteral( "grader_cli.exe" ) );
    writeBytes( cli, QByteArrayLiteral( "MZ-binary-bytes-v1" ) );

    const QString id1 = graderCliIdentity( cli );
    REQUIRE( id1.startsWith( QLatin1String( "path:" ) ) );
    REQUIRE( id1.contains( QLatin1String( ";sha256:" ) ) );
    REQUIRE( id1.contains( QLatin1String( ";size:" ) ) );
    // Deterministic for identical bytes.
    REQUIRE( graderCliIdentity( cli ) == id1 );

    // Same SIZE, different content (a rebuild): identity changes.
    writeBytes( cli, QByteArrayLiteral( "MZ-binary-bytes-v2" ) );
    REQUIRE( graderCliIdentity( cli ) != id1 );

    // Content growth (size change): identity changes.
    writeBytes( cli, QByteArrayLiteral( "MZ-binary-bytes-v2-plus-more" ) );
    REQUIRE( graderCliIdentity( cli ) != id1 );
}

TEST_CASE( "#1694: gradingVersionLabels derive real labels, never a constant",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    const QJsonObject withVersion = QJsonDocument::fromJson( R"( {"version":"2026.10"} )" ).object();
    const QJsonObject withoutVersion =
      QJsonDocument::fromJson( R"( {"schema":"sicnu.grader.rubric/1"} )" ).object();
    const QJsonObject editedRules =
      QJsonDocument::fromJson( R"( {"schema":"sicnu.grader.rubric/1","threshold":0.7} )" ).object();

    // Explicit authored version wins.
    const auto explicitLabels = gradingVersionLabels( withVersion, QJsonObject{}, QStringLiteral( "lab99" ), QString() );
    REQUIRE( explicitLabels.rubricVersion == QLatin1String( "2026.10" ) );

    // No version member: short content fingerprint, stable and
    // edit-sensitive, and NEVER the hardcoded "ui-1".
    const auto fp1 = gradingVersionLabels( withoutVersion, QJsonObject{}, QStringLiteral( "lab99" ), QString() );
    REQUIRE( fp1.rubricVersion.startsWith( QLatin1String( "fp:" ) ) );
    REQUIRE( fp1.rubricVersion.size() == 3 + 12 );
    REQUIRE( fp1.rubricVersion != QLatin1String( "ui-1" ) );
    REQUIRE( gradingVersionLabels( withoutVersion, QJsonObject{}, QStringLiteral( "lab99" ), QString() )
               .rubricVersion == fp1.rubricVersion );
    REQUIRE( gradingVersionLabels( editedRules, QJsonObject{}, QStringLiteral( "lab99" ), QString() )
               .rubricVersion != fp1.rubricVersion );

    // No authored rubric at all: fall back to the rules-file fingerprint.
    const auto fromRules = gradingVersionLabels( QJsonObject{}, QJsonObject{}, QStringLiteral( "lab99" ),
                                                 QStringLiteral( "sha256:deadbeef" ) );
    REQUIRE( fromRules.rubricVersion.startsWith( QLatin1String( "fp:" ) ) );
    // And with nothing at all: honest none.
    const auto none = gradingVersionLabels( QJsonObject{}, QJsonObject{}, QString(), QString() );
    REQUIRE( none.rubricVersion == QLatin1String( "none" ) );
    REQUIRE( none.labVersion == QLatin1String( "none" ) );

    // Lab side: explicit version, else content fp, else labid form.
    REQUIRE( gradingVersionLabels( QJsonObject{}, withVersion, QStringLiteral( "lab99" ), QString() )
               .labVersion == QLatin1String( "2026.10" ) );
    REQUIRE( gradingVersionLabels( QJsonObject{}, withoutVersion, QStringLiteral( "lab99" ), QString() )
               .labVersion.startsWith( QLatin1String( "fp:" ) ) );
    REQUIRE( gradingVersionLabels( QJsonObject{}, QJsonObject{}, QStringLiteral( "lab99" ), QString() )
               .labVersion == QLatin1String( "labid:lab99" ) );
}

TEST_CASE( "#1694 R1: lab-id authority resolves its rules file (dock's primary path)",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    // R1 finding 1: with labIdOrRulesPath = a lab ID, the CLI reads
    // <searchRoot>/data/labs/grading/<id>.rules.json — the fingerprint must
    // track THAT file's content, or an in-place rules edit stays invisible
    // to the checkpoint digest on the console's main configuration.
    QTemporaryDir repo;
    const QString gradingDir = QDir( repo.path() ).filePath( QStringLiteral( "data/labs/grading" ) );
    REQUIRE( QDir().mkpath( gradingDir ) );
    const QString rules = QDir( gradingDir ).filePath( QStringLiteral( "lab15.rules.json" ) );
    writeBytes( rules, QByteArrayLiteral( "{\"threshold\":0.5}" ) );

    GraderCliConfig cfg;
    cfg.labIdOrRulesPath = QStringLiteral( "lab15" );
    const QString fp1 = gradingRulesFingerprint( cfg, repo.path() );
    REQUIRE( fp1.startsWith( QLatin1String( "labid:lab15;sha256:" ) ) );

    // Edit the resolved rules file in place: fingerprint changes.
    writeBytes( rules, QByteArrayLiteral( "{\"threshold\":0.7}" ) );
    REQUIRE( gradingRulesFingerprint( cfg, repo.path() ) != fp1 );

    // Unresolvable id (no file anywhere): stable plain labid marker.
    GraderCliConfig unknown;
    unknown.labIdOrRulesPath = QStringLiteral( "no_such_lab" );
    REQUIRE( gradingRulesFingerprint( unknown, repo.path() )
             == QLatin1String( "labid:no_such_lab" ) );

    // End-to-end: a batch keyed by LAB ID adopts nothing when the resolved
    // rules file changes between runs.
    ClassFixture fx;
    const QString labGradingDir =
      QDir( fx.dir.path() ).filePath( QStringLiteral( "data/labs/grading" ) );
    REQUIRE( QDir().mkpath( labGradingDir ) );
    const QString labRules =
      QDir( labGradingDir ).filePath( QStringLiteral( "lab99.rules.json" ) );
    GraderCliConfig g;
    g.labIdOrRulesPath = QStringLiteral( "lab99" );

    writeBytes( labRules, QByteArrayLiteral( "R1" ) );
    BatchAssessmentConfig c1 = makeConfig( fx, QStringLiteral( "grader-test-1" ) );
    c1.gradingRulesFingerprint = gradingRulesFingerprint( g, fx.dir.path() );
    const auto run1 = runBatchAssessment( c1, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( run1.graded == 2 );

    writeBytes( labRules, QByteArrayLiteral( "R2" ) );
    BatchAssessmentConfig c2 = makeConfig( fx, QStringLiteral( "grader-test-1" ) );
    c2.gradingRulesFingerprint = gradingRulesFingerprint( g, fx.dir.path() );
    const auto run2 = runBatchAssessment( c2, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( run2.resumed == 0 );
    REQUIRE( run2.checkpointInvalidated );
}

TEST_CASE( "#1694 R1: graderCliIdentity has no interior blind spot",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    // R1 finding 4: head+tail sampling left >8 MiB interiors (and the tail
    // of 4–8 MiB files) unfingerprinted. The hash is a streamed FULL-file
    // hash now: same-size interior and late-tail rewrites must change it.
    QTemporaryDir dir;
    const QString cli = QDir( dir.path() ).filePath( QStringLiteral( "big_cli.exe" ) );
    QByteArray big( 9 * 1024 * 1024, 'A' );
    big[0] = 'M';
    writeBytes( cli, big );
    const QString id1 = graderCliIdentity( cli );
    REQUIRE( graderCliIdentity( cli ) == id1 );

    // Interior-only rewrite (middle of the file, far from both ends).
    QByteArray mid = big;
    mid[big.size() / 2] = 'X';
    writeBytes( cli, mid );
    REQUIRE( graderCliIdentity( cli ) != id1 );

    // Late-tail rewrite (inside the old 4–8 MiB blind zone).
    QByteArray tail = big;
    tail[7 * 1024 * 1024] = 'Y';
    writeBytes( cli, tail );
    REQUIRE( graderCliIdentity( cli ) != id1 );
}

TEST_CASE( "#1694 R1: checkpoint_invalidated is false for absent/corrupt/foreign checkpoints",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    ClassFixture fx;
    BatchAssessmentConfig cfg = makeConfig( fx, QStringLiteral( "grader-test-1" ) );

    // No checkpoint file at all → fresh run, NOT an invalidation.
    const auto fresh = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE_FALSE( fresh.checkpointInvalidated );

    // Corrupt checkpoint (garbage bytes) → degrade to fresh, not invalidation.
    writeBytes( cfg.checkpointPath, QByteArrayLiteral( "not json at all" ) );
    const auto corrupt = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE_FALSE( corrupt.checkpointInvalidated );

    // Foreign-schema checkpoint → same: schema mismatch is not a rules change.
    writeBytes( cfg.checkpointPath, QByteArrayLiteral( "{\"schema\":\"something.else/1\"}" ) );
    const auto foreign = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE_FALSE( foreign.checkpointInvalidated );

    // Well-formed checkpoint of a DIFFERENT identity → invalidation.
    const auto warm = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( warm.graded == 2 );
    const BatchAssessmentConfig changed = makeConfig( fx, QStringLiteral( "grader-test-2" ) );
    const auto invalidated = runBatchAssessment( changed, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( invalidated.checkpointInvalidated );
}

TEST_CASE( "#1694 R1: regradeTraceability ignores run metadata (incl. checkpoint_invalidated)",
           "[teaching_admin][batch][r22][issue-1694]" )
{
    // R1 finding 3: the outcome digest must not depend on HOW the run came
    // to be — a rules-change re-grade and a clean identical-outcome run of
    // the same rows share report_digest.
    ClassFixture fx;
    const BatchAssessmentConfig cfg = makeConfig( fx, QStringLiteral( "grader-test-1" ) );
    auto report = runBatchAssessment( cfg, rulesReadingGrader( fx.rulesPath ) );
    REQUIRE( report.graded == 2 );
    const QString digest1 = regradeTraceability( report ).value( QStringLiteral( "report_digest" ) ).toString();

    // Same rows/counter values, only run metadata flips.
    report.checkpointInvalidated = true;
    report.resumed = 1; // also covered by the pre-existing removal list
    const QString digest2 = regradeTraceability( report ).value( QStringLiteral( "report_digest" ) ).toString();
    REQUIRE( digest1 == digest2 );
    REQUIRE( !digest1.isEmpty() );
}

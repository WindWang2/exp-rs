// test_r33_t04_batch_progress.cpp — R33-T04 / WP15 (#1953 residual):
//
//   Batch assessment progress counted ONLY newly graded rows against a
//   total that included the adopted ones: doneRows started at 0, so a
//   resumed run looked like it restarted, and an ALL-adopted resume never
//   emitted progress (nor the completion tick) at all. Progress is now
//   monotone over "adopted + newly completed" with an explicit initial
//   adopted emit; adopted rows are never re-graded.
#include "teaching_admin/batch_assessment.h"
#include "teaching_admin/grader_cli_adapter.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <vector>

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
    cfg.rubricVersion = QStringLiteral( "rubric-A" );
    cfg.labVersion = QStringLiteral( "lab-A" );
    cfg.softwareVersion = QStringLiteral( "sw-A" );
    cfg.maxConcurrency = 1;
    cfg.checkpointPath = fx.outPrefix + QStringLiteral( ".checkpoint.json" );
    GraderCliConfig grader;
    grader.labIdOrRulesPath = fx.rulesPath;
    cfg.gradingRulesFingerprint = gradingRulesFingerprint( grader );
    cfg.graderIdentity = QStringLiteral( "fake-grader-r33t04" );
    return cfg;
}
} // namespace

TEST_CASE( "resumed batch progress starts at the adopted count and never re-grades adopted rows",
           "[teaching_admin][batch][r33t04]" )
{
    ClassFixture fx;
    BatchAssessmentConfig cfg = makeConfig( fx );

    std::atomic<int> gradeCalls{ 0 };
    const auto grader = [ & ]( const SubmissionItem &item ) {
        gradeCalls.fetch_add( 1 );
        BatchRowResult row;
        row.studentId = item.studentId;
        row.artifactPath = item.path;
        row.status = QStringLiteral( "pass" );
        row.verdict = QStringLiteral( "pass" );
        row.score = 2.0;
        return row;
    };

    // Run 1: fresh — grades both, progress goes 0 → 1 → 2 of 2.
    std::vector<std::pair<int, int>> progressLog;
    auto report1 = runBatchAssessment( cfg, grader, /*cancelFlag=*/nullptr,
                                       [ & ]( int done, int total ) {
                                           progressLog.emplace_back( done, total );
                                       } );
    REQUIRE( gradeCalls.load() == 2 );
    REQUIRE( report1.resumed == 0 );
    REQUIRE( progressLog.size() == 3 );
    CHECK( progressLog.front() == std::make_pair( 0, 2 ) ); // initial emit
    CHECK( progressLog.back() == std::make_pair( 2, 2 ) );

    // Run 2: same config + untouched artifacts → EVERYTHING is adopted.
    // Previously progress NEVER fired here (doneRows never incremented) and
    // the resume was invisible until the final report.
    std::vector<std::pair<int, int>> resumeLog;
    gradeCalls.store( 0 );
    auto report2 = runBatchAssessment( cfg, grader, /*cancelFlag=*/nullptr,
                                       [ & ]( int done, int total ) {
                                           resumeLog.emplace_back( done, total );
                                       } );
    CHECK( gradeCalls.load() == 0 ); // adopted rows are never re-graded
    REQUIRE( report2.resumed == 2 );
    REQUIRE_FALSE( resumeLog.empty() );
    CHECK( resumeLog.front() == std::make_pair( 2, 2 ) ); // adopted head start = completion
    CHECK( resumeLog.back() == std::make_pair( 2, 2 ) );
    // Byte-stable rows: a resumed run publishes identical verdicts.
    REQUIRE( report2.rows.size() == report1.rows.size() );
    for ( std::size_t i = 0; i < report1.rows.size(); ++i )
        CHECK( report2.rows[i].score == report1.rows[i].score );
}

TEST_CASE( "a cancelled run resumes from its durable rows and grades only the remainder",
           "[teaching_admin][batch][r33t04]" )
{
    ClassFixture fx;
    // Both runs share ONE config (the checkpoint digest keys on it, #1694):
    // run 1 is cancelled after alice, run 2 adopts her durable row.
    BatchAssessmentConfig cfg = makeConfig( fx );

    std::atomic<int> gradeCalls{ 0 };
    std::atomic<bool> cancelFlag{ false };
    // Cancel AFTER the first graded row lands: alice is durable in the
    // checkpoint, bob never starts (typed cancelled row).
    const auto grader = [ & ]( const SubmissionItem &item ) {
        BatchRowResult row;
        row.studentId = item.studentId;
        row.artifactPath = item.path;
        row.status = QStringLiteral( "pass" );
        row.verdict = QStringLiteral( "pass" );
        row.score = 2.0;
        gradeCalls.fetch_add( 1 );
        cancelFlag.store( true );
        return row;
    };

    const auto first = runBatchAssessment( cfg, grader, &cancelFlag );
    REQUIRE( gradeCalls.load() == 1 );
    REQUIRE( first.graded == 1 );
    REQUIRE( first.cancelledEarly );
    // total counts DISCOVERED submissions: both students found.
    REQUIRE( first.total == 2 );

    // Run 2 (same config, no cancel): adopts alice, grades bob.
    std::vector<std::pair<int, int>> resumeLog;
    auto second = runBatchAssessment( cfg, grader, /*cancelFlag=*/nullptr,
                                      [ & ]( int done, int total ) {
                                          resumeLog.emplace_back( done, total );
                                      } );
    CHECK( gradeCalls.load() == 2 ); // exactly one more GRADE call (bob)
    REQUIRE( second.resumed == 1 );  // alice's durable row adopted, not re-run
    REQUIRE( second.total == 2 );
    // graded counts final pass ROWS (adopted + newly graded alike).
    REQUIRE( second.graded == 2 );
    REQUIRE_FALSE( second.cancelledEarly );
    REQUIRE_FALSE( resumeLog.empty() );
    CHECK( resumeLog.front() == std::make_pair( 1, 2 ) ); // adopted alice
    CHECK( resumeLog.back() == std::make_pair( 2, 2 ) );  // + graded bob
    // Monotone, and the total covers this run's whole processing set.
    for ( std::size_t i = 1; i < resumeLog.size(); ++i )
        CHECK( resumeLog[i].first > resumeLog[i - 1].first );
}
